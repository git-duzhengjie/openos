#include "include/devmgr.h"
#include "include/string.h"
#include "include/vga.h"

static devmgr_device_t device_table[DEVMGR_DEV_MAX];
static uint32_t device_table_count;
static hotplug_event_t event_ring[DEVMGR_EVENT_MAX];
static uint32_t event_head;
static uint32_t event_count;
static uint32_t next_device_id;
static uint32_t next_event_seq;
static hotplug_notifier_t global_notifier;

static void copy_text(char *dst, uint32_t dst_size, const char *src) {
    uint32_t i = 0;
    if (!dst || dst_size == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    for (; i + 1 < dst_size && src[i]; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

static void print_dec(uint32_t value) {
    char buf[11];
    int i = 10;
    buf[i] = '\0';
    if (value == 0) {
        vga_putc('0');
        return;
    }
    while (value > 0 && i > 0) {
        buf[--i] = (char)('0' + (value % 10));
        value /= 10;
    }
    vga_write(&buf[i]);
}

const char *devmgr_type_name(devmgr_type_t type) {
    switch (type) {
    case DEVMGR_TYPE_CHAR: return "char";
    case DEVMGR_TYPE_BLOCK: return "block";
    case DEVMGR_TYPE_NET: return "net";
    case DEVMGR_TYPE_INPUT: return "input";
    case DEVMGR_TYPE_STORAGE: return "storage";
    default: return "unknown";
    }
}

const char *devmgr_action_name(hotplug_action_t action) {
    switch (action) {
    case HOTPLUG_ACTION_ADD: return "add";
    case HOTPLUG_ACTION_REMOVE: return "remove";
    case HOTPLUG_ACTION_CHANGE: return "change";
    default: return "unknown";
    }
}

static void push_event(hotplug_action_t action, const devmgr_device_t *dev) {
    hotplug_event_t *event;
    uint32_t idx;
    if (!dev) return;

    idx = (event_head + event_count) % DEVMGR_EVENT_MAX;
    if (event_count == DEVMGR_EVENT_MAX) {
        event_head = (event_head + 1) % DEVMGR_EVENT_MAX;
        idx = (event_head + event_count - 1) % DEVMGR_EVENT_MAX;
    } else {
        event_count++;
    }

    event = &event_ring[idx];
    memset(event, 0, sizeof(hotplug_event_t));
    event->seq = next_event_seq++;
    event->action = action;
    event->device_id = dev->id;
    copy_text(event->name, DEVMGR_NAME_MAX, dev->name);
    copy_text(event->bus, DEVMGR_BUS_MAX, dev->bus);
    event->type = dev->type;
    event->major = dev->major;
    event->minor = dev->minor;

    if (global_notifier) {
        global_notifier(event);
    }
}

void devmgr_init(void) {
    memset(device_table, 0, sizeof(device_table));
    memset(event_ring, 0, sizeof(event_ring));
    device_table_count = 0;
    event_head = 0;
    event_count = 0;
    next_device_id = 1;
    next_event_seq = 1;
    global_notifier = 0;
}

int devmgr_register(const char *name, const char *bus, devmgr_type_t type,
                    uint32_t major, uint32_t minor, uint32_t flags,
                    void *device_ref) {
    devmgr_device_t *dev;
    if (!name || !bus) return -1;
    if (device_table_count >= DEVMGR_DEV_MAX) return -1;
    if (devmgr_find(name)) return -1;

    dev = &device_table[device_table_count++];
    memset(dev, 0, sizeof(devmgr_device_t));
    dev->id = next_device_id++;
    copy_text(dev->name, DEVMGR_NAME_MAX, name);
    copy_text(dev->bus, DEVMGR_BUS_MAX, bus);
    dev->type = type;
    dev->major = major;
    dev->minor = minor;
    dev->flags = flags;
    dev->state = DEVMGR_STATE_PRESENT;
    dev->device_ref = device_ref;
    push_event(HOTPLUG_ACTION_ADD, dev);
    return 0;
}

int devmgr_unregister(const char *name) {
    uint32_t i;
    devmgr_device_t removed;
    if (!name) return -1;

    for (i = 0; i < device_table_count; i++) {
        if (strcmp(device_table[i].name, name) == 0) {
            memcpy(&removed, &device_table[i], sizeof(devmgr_device_t));
            removed.state = DEVMGR_STATE_REMOVED;
            push_event(HOTPLUG_ACTION_REMOVE, &removed);
            if (i + 1 < device_table_count) {
                memcpy(&device_table[i], &device_table[device_table_count - 1], sizeof(devmgr_device_t));
            }
            memset(&device_table[device_table_count - 1], 0, sizeof(devmgr_device_t));
            device_table_count--;
            return 0;
        }
    }
    return -1;
}

int devmgr_notify_change(const char *name) {
    devmgr_device_t *dev = devmgr_find(name);
    if (!dev) return -1;
    push_event(HOTPLUG_ACTION_CHANGE, dev);
    return 0;
}

devmgr_device_t *devmgr_find(const char *name) {
    uint32_t i;
    if (!name) return 0;
    for (i = 0; i < device_table_count; i++) {
        if (strcmp(device_table[i].name, name) == 0) {
            return &device_table[i];
        }
    }
    return 0;
}

devmgr_device_t *devmgr_get_by_index(uint32_t index) {
    if (index >= device_table_count) return 0;
    return &device_table[index];
}

uint32_t devmgr_count(void) {
    return device_table_count;
}

uint32_t devmgr_event_count(void) {
    return event_count;
}

int devmgr_event_get(uint32_t index, hotplug_event_t *out) {
    uint32_t idx;
    if (!out || index >= event_count) return -1;
    idx = (event_head + index) % DEVMGR_EVENT_MAX;
    memcpy(out, &event_ring[idx], sizeof(hotplug_event_t));
    return 0;
}

int devmgr_poll_event(hotplug_event_t *out) {
    if (!out || event_count == 0) return -1;
    memcpy(out, &event_ring[event_head], sizeof(hotplug_event_t));
    memset(&event_ring[event_head], 0, sizeof(hotplug_event_t));
    event_head = (event_head + 1) % DEVMGR_EVENT_MAX;
    event_count--;
    return 0;
}

void devmgr_set_notifier(hotplug_notifier_t notifier) {
    global_notifier = notifier;
}

void devmgr_print_devices(void) {
    uint32_t i;
    vga_write("devices:\n");
    if (device_table_count == 0) {
        vga_write("  <none>\n");
        return;
    }
    for (i = 0; i < device_table_count; i++) {
        devmgr_device_t *dev = &device_table[i];
        vga_write("  #");
        print_dec(dev->id);
        vga_write(" ");
        vga_write(dev->name);
        vga_write(" bus=");
        vga_write(dev->bus);
        vga_write(" type=");
        vga_write(devmgr_type_name(dev->type));
        vga_write(" dev=");
        print_dec(dev->major);
        vga_write(":");
        print_dec(dev->minor);
        vga_write("\n");
    }
}

void devmgr_print_hotplug_events(void) {
    uint32_t i;
    hotplug_event_t event;
    vga_write("hotplug events:\n");
    if (event_count == 0) {
        vga_write("  <none>\n");
        return;
    }
    for (i = 0; i < event_count; i++) {
        if (devmgr_event_get(i, &event) == 0) {
            vga_write("  seq=");
            print_dec(event.seq);
            vga_write(" action=");
            vga_write(devmgr_action_name(event.action));
            vga_write(" name=");
            vga_write(event.name);
            vga_write(" bus=");
            vga_write(event.bus);
            vga_write(" type=");
            vga_write(devmgr_type_name(event.type));
            vga_write(" dev=");
            print_dec(event.major);
            vga_write(":");
            print_dec(event.minor);
            vga_write("\n");
        }
    }
}
