/*
 * input_core.c -- M8-E Input Abstraction Layer (IAL) implementation.
 *
 * Static ring + static device table + static subscriber slots. Zero heap.
 * No libm. IRQ-safe production path (all storage volatile-marked where
 * shared, updated with a compiler barrier before publish).
 */

#include "../include/input_core.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

/* ---------------- storage ---------------- */

static input_device_t g_devs[INPUT_MAX_DEVICES + 1];  /* slot 0 unused */
static uint16_t       g_dev_count = 0;

typedef struct {
    input_listener_fn fn;
    void             *user;
    uint8_t           in_use;
} subscriber_slot_t;

static subscriber_slot_t g_subs[INPUT_MAX_SUBSCRIBERS];

static volatile input_event_t g_ring[INPUT_RING_CAPACITY];
static volatile uint32_t      g_ring_head = 0;   /* write index */
static volatile uint32_t      g_ring_tail = 0;   /* read index  */
static volatile uint32_t      g_stat_produced = 0;
static volatile uint32_t      g_stat_dropped  = 0;
static uint8_t                g_initialised   = 0;

#define RING_MASK (INPUT_RING_CAPACITY - 1u)

/* ---------------- helpers ---------------- */

static int str_eq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

/* ---------------- init ---------------- */

void input_core_init(void) {
    if (g_initialised) return;
    for (int i = 0; i <= INPUT_MAX_DEVICES; ++i) {
        g_devs[i].dev_id      = 0;
        g_devs[i].klass       = INPUT_DEV_UNKNOWN;
        g_devs[i].name        = NULL;
        g_devs[i].present     = 0;
        g_devs[i].event_count = 0;
    }
    for (int i = 0; i < INPUT_MAX_SUBSCRIBERS; ++i) {
        g_subs[i].fn = NULL;
        g_subs[i].user = NULL;
        g_subs[i].in_use = 0;
    }
    g_dev_count     = 0;
    g_ring_head     = 0;
    g_ring_tail     = 0;
    g_stat_produced = 0;
    g_stat_dropped  = 0;
    g_initialised   = 1;
}

/* ---------------- device registry ---------------- */

uint16_t input_device_register(input_dev_class_t klass, const char *name) {
    if (!g_initialised) input_core_init();

    /* Idempotent: if a slot with same class+name exists, revive & return it. */
    for (int i = 1; i <= INPUT_MAX_DEVICES; ++i) {
        if (g_devs[i].dev_id != 0
            && g_devs[i].klass == klass
            && str_eq(g_devs[i].name, name)) {
            if (!g_devs[i].present) {
                g_devs[i].present = 1;
                input_report_hotplug_add(g_devs[i].dev_id, name, klass);
            }
            return g_devs[i].dev_id;
        }
    }
    /* First free slot */
    for (int i = 1; i <= INPUT_MAX_DEVICES; ++i) {
        if (g_devs[i].dev_id == 0) {
            g_devs[i].dev_id      = (uint16_t)i;
            g_devs[i].klass       = klass;
            g_devs[i].name        = name;
            g_devs[i].present     = 1;
            g_devs[i].event_count = 0;
            if (i > g_dev_count) g_dev_count = (uint16_t)i;
            input_report_hotplug_add(g_devs[i].dev_id, name, klass);
            return g_devs[i].dev_id;
        }
    }
    return 0;
}

int input_device_unregister(uint16_t dev_id) {
    if (dev_id == 0 || dev_id > INPUT_MAX_DEVICES) return -1;
    if (g_devs[dev_id].dev_id == 0) return -1;
    if (g_devs[dev_id].present) {
        g_devs[dev_id].present = 0;
        input_report_hotplug_remove(dev_id);
    }
    return 0;
}

const input_device_t *input_device_get(uint16_t dev_id) {
    if (dev_id == 0 || dev_id > INPUT_MAX_DEVICES) return NULL;
    if (g_devs[dev_id].dev_id == 0) return NULL;
    return &g_devs[dev_id];
}

uint16_t input_stat_device_count(void) {
    uint16_t n = 0;
    for (int i = 1; i <= INPUT_MAX_DEVICES; ++i) {
        if (g_devs[i].dev_id != 0) n++;
    }
    return n;
}

/* ---------------- ring buffer ---------------- */

static void ring_push(const input_event_t *ev) {
    uint32_t head = g_ring_head;
    uint32_t tail = g_ring_tail;
    uint32_t used = head - tail;   /* modulo arithmetic; both monotone */
    if (used >= INPUT_RING_CAPACITY) {
        /* Full: drop oldest by advancing tail, then push. */
        g_ring_tail = tail + 1;
        g_stat_dropped++;
    }
    g_ring[head & RING_MASK] = *ev;
    /* compiler barrier so the slot content is visible before head advances */
    __asm__ volatile("" ::: "memory");
    g_ring_head = head + 1;
    g_stat_produced++;
}

int input_poll_event(input_event_t *out) {
    if (!out) return 0;
    uint32_t head = g_ring_head;
    uint32_t tail = g_ring_tail;
    if (head == tail) return 0;
    *out = *(const input_event_t *)&g_ring[tail & RING_MASK];
    __asm__ volatile("" ::: "memory");
    g_ring_tail = tail + 1;
    return 1;
}

/* ---------------- publish ---------------- */

void input_report(const input_event_t *ev) {
    if (!g_initialised) input_core_init();
    if (!ev) return;

    if (ev->dev_id != 0 && ev->dev_id <= INPUT_MAX_DEVICES) {
        g_devs[ev->dev_id].event_count++;
    }

    ring_push(ev);

    for (int i = 0; i < INPUT_MAX_SUBSCRIBERS; ++i) {
        if (g_subs[i].in_use && g_subs[i].fn) {
            g_subs[i].fn(ev, g_subs[i].user);
        }
    }
}

void input_report_key(uint16_t dev_id, uint32_t key, int32_t value, uint32_t ts_ms) {
    input_event_t ev = {0};
    ev.timestamp_ms = ts_ms;
    ev.dev_id       = dev_id;
    ev.type         = INPUT_EV_KEY;
    ev.code         = key;
    ev.value        = value;
    input_report(&ev);
}

void input_report_rel(uint16_t dev_id, uint32_t axis, int32_t delta, uint32_t ts_ms) {
    input_event_t ev = {0};
    ev.timestamp_ms = ts_ms;
    ev.dev_id       = dev_id;
    ev.type         = INPUT_EV_REL;
    ev.code         = axis;
    ev.value        = delta;
    input_report(&ev);
}

void input_report_abs(uint16_t dev_id, int32_t x, int32_t y, int32_t pressure, uint32_t ts_ms) {
    input_event_t ev = {0};
    ev.timestamp_ms = ts_ms;
    ev.dev_id       = dev_id;
    ev.type         = INPUT_EV_ABS;
    ev.code         = INPUT_ABS_X;   /* payload carries both x and y */
    ev.value        = pressure;
    ev.x            = x;
    ev.y            = y;
    input_report(&ev);
}

void input_report_syn(uint16_t dev_id, uint32_t ts_ms) {
    input_event_t ev = {0};
    ev.timestamp_ms = ts_ms;
    ev.dev_id       = dev_id;
    ev.type         = INPUT_EV_SYN;
    input_report(&ev);
}

/* ---------------- subscribers ---------------- */

int input_subscribe(input_listener_fn fn, void *user) {
    if (!fn) return 0;
    if (!g_initialised) input_core_init();
    for (int i = 0; i < INPUT_MAX_SUBSCRIBERS; ++i) {
        if (!g_subs[i].in_use) {
            g_subs[i].fn     = fn;
            g_subs[i].user   = user;
            g_subs[i].in_use = 1;
            return i + 1;
        }
    }
    return 0;
}

void input_unsubscribe(int handle) {
    if (handle < 1 || handle > INPUT_MAX_SUBSCRIBERS) return;
    int i = handle - 1;
    g_subs[i].fn     = NULL;
    g_subs[i].user   = NULL;
    g_subs[i].in_use = 0;
}

/* ---------------- hotplug ---------------- */

/* Hotplug subscriber table. */
#define INPUT_MAX_HOTPLUG_SUBSCRIBERS 8
static struct {
    input_hotplug_cb_t fn;
    void              *user;
    uint8_t            in_use;
} g_hotplug_subs[INPUT_MAX_HOTPLUG_SUBSCRIBERS];

void input_report_hotplug_add(uint16_t dev_id, const char *name, input_dev_class_t klass) {
    if (!g_initialised) input_core_init();
    if (dev_id == 0 || dev_id > INPUT_MAX_DEVICES) return;

    input_device_t *dev = &g_devs[dev_id];
    dev->present = 1;

    /* Post hotplug event onto input bus. */
    input_event_t ev = {0};
    ev.timestamp_ms  = 0;
    ev.dev_id        = dev_id;
    ev.type          = INPUT_EV_HOTPLUG;
    ev.code          = INPUT_HOTPLUG_ADD;
    ev.value         = klass;
    input_report(&ev);

    /* Notify hotplug subscribers. */
    input_hotplug_data_t hp_data = {klass, dev_id, name};
    for (int i = 0; i < INPUT_MAX_HOTPLUG_SUBSCRIBERS; ++i) {
        if (g_hotplug_subs[i].in_use && g_hotplug_subs[i].fn) {
            g_hotplug_subs[i].fn(INPUT_HOTPLUG_ADD, &hp_data, g_hotplug_subs[i].user);
        }
    }
}

void input_report_hotplug_remove(uint16_t dev_id) {
    if (!g_initialised) input_core_init();
    if (dev_id == 0 || dev_id > INPUT_MAX_DEVICES) return;

    input_device_t *dev = &g_devs[dev_id];
    if (!dev->present) return;
    dev->present = 0;

    /* Post hotplug event onto input bus. */
    input_event_t ev = {0};
    ev.timestamp_ms  = 0;
    ev.dev_id        = dev_id;
    ev.type          = INPUT_EV_HOTPLUG;
    ev.code          = INPUT_HOTPLUG_REMOVE;
    ev.value         = dev->klass;
    input_report(&ev);

    /* Notify hotplug subscribers. */
    input_hotplug_data_t hp_data = {dev->klass, dev_id, dev->name};
    for (int i = 0; i < INPUT_MAX_HOTPLUG_SUBSCRIBERS; ++i) {
        if (g_hotplug_subs[i].in_use && g_hotplug_subs[i].fn) {
            g_hotplug_subs[i].fn(INPUT_HOTPLUG_REMOVE, &hp_data, g_hotplug_subs[i].user);
        }
    }
}

/* ---------------- device lookup ---------------- */

uint16_t input_device_find_by_name(const char *name) {
    if (!g_initialised) input_core_init();
    if (!name) return 0;
    for (int i = 1; i <= INPUT_MAX_DEVICES; ++i) {
        if (g_devs[i].dev_id != 0 && g_devs[i].name && str_eq(g_devs[i].name, name)) {
            return g_devs[i].dev_id;
        }
    }
    return 0;
}

uint16_t input_device_find_by_class(input_dev_class_t klass) {
    if (!g_initialised) input_core_init();
    for (int i = 1; i <= INPUT_MAX_DEVICES; ++i) {
        if (g_devs[i].dev_id != 0 && g_devs[i].klass == klass && g_devs[i].present) {
            return g_devs[i].dev_id;
        }
    }
    return 0;
}

int input_device_is_present(uint16_t dev_id) {
    if (!g_initialised) input_core_init();
    if (dev_id == 0 || dev_id > INPUT_MAX_DEVICES) return 0;
    return g_devs[dev_id].present;
}

/* ---------------- hotplug subscription ---------------- */

int input_hotplug_subscribe(input_hotplug_cb_t fn, void *user) {
    if (!fn) return 0;
    if (!g_initialised) input_core_init();
    for (int i = 0; i < INPUT_MAX_HOTPLUG_SUBSCRIBERS; ++i) {
        if (!g_hotplug_subs[i].in_use) {
            g_hotplug_subs[i].fn     = fn;
            g_hotplug_subs[i].user   = user;
            g_hotplug_subs[i].in_use = 1;
            return i + 1;
        }
    }
    return 0;
}

void input_hotplug_unsubscribe(int handle) {
    if (handle < 1 || handle > INPUT_MAX_HOTPLUG_SUBSCRIBERS) return;
    int i = handle - 1;
    g_hotplug_subs[i].fn     = NULL;
    g_hotplug_subs[i].user   = NULL;
    g_hotplug_subs[i].in_use = 0;
}

/* ---------------- stats ---------------- */

uint32_t input_stat_events_produced(void) { return g_stat_produced; }
uint32_t input_stat_events_dropped(void)  { return g_stat_dropped;  }
uint32_t input_stat_ring_depth(void)      { return g_ring_head - g_ring_tail; }
