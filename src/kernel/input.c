#include "input.h"

static input_event_t input_events[OPENOS_INPUT_QUEUE_SIZE];
static volatile uint32_t input_head;
static volatile uint32_t input_tail;
static volatile uint32_t input_count;
static uint32_t next_input_device_id = 1u;

static uint32_t input_irq_save(void) {
    uint32_t flags;
    __asm__ volatile("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static void input_irq_restore(uint32_t flags) {
    __asm__ volatile("pushl %0; popfl" : : "r"(flags) : "memory", "cc");
}

static uint32_t input_copy_text(char dst[8], const char *src, uint32_t len) {
    uint32_t i = 0;
    if (!dst) {
        return 0;
    }

    for (i = 0; i < 8u; ++i) {
        dst[i] = 0;
    }

    if (!src) {
        return 0;
    }

    i = 0;
    while (i < len && i < 8u) {
        dst[i] = src[i];
        ++i;
    }
    return i;
}

void input_init(void) {
    uint32_t flags = input_irq_save();
    input_head = 0;
    input_tail = 0;
    input_count = 0;
    next_input_device_id = 1u;
    input_irq_restore(flags);
}

int input_register_device(input_device_type_t type, const char *name) {
    uint32_t id;
    (void)type;
    (void)name;

    id = next_input_device_id++;
    if (id == 0u) {
        id = next_input_device_id++;
    }
    return (int)id;
}

int input_push_event(const input_event_t *event) {
    uint32_t flags;
    if (!event || event->type == INPUT_EVENT_NONE) {
        return -1;
    }

    flags = input_irq_save();
    if (input_count >= OPENOS_INPUT_QUEUE_SIZE) {
        input_irq_restore(flags);
        return -2;
    }

    input_events[input_head] = *event;
    input_head = (input_head + 1u) % OPENOS_INPUT_QUEUE_SIZE;
    ++input_count;
    input_irq_restore(flags);
    return 0;
}

int input_poll_event(input_event_t *event) {
    uint32_t flags;
    if (!event) {
        return -1;
    }

    flags = input_irq_save();
    if (input_count == 0u) {
        input_irq_restore(flags);
        return 0;
    }

    *event = input_events[input_tail];
    input_tail = (input_tail + 1u) % OPENOS_INPUT_QUEUE_SIZE;
    --input_count;
    input_irq_restore(flags);
    return 1;
}

int input_has_event(void) {
    int has;
    uint32_t flags = input_irq_save();
    has = input_count > 0u;
    input_irq_restore(flags);
    return has;
}

void input_flush_events(void) {
    uint32_t flags = input_irq_save();
    input_head = 0;
    input_tail = 0;
    input_count = 0;
    input_irq_restore(flags);
}

int input_push_key(uint32_t device_id, int key, uint32_t modifiers) {
    input_event_t event;
    event.type = INPUT_EVENT_KEY;
    event.device_type = INPUT_DEVICE_KEYBOARD;
    event.device_id = device_id;
    event.modifiers = modifiers;
    event.x = 0;
    event.y = 0;
    event.dx = 0;
    event.dy = 0;
    event.key = key;
    event.button = 0;
    event.codepoint = 0;
    event.text_len = 0;
    input_copy_text(event.text, 0, 0);
    return input_push_event(&event);
}

int input_push_text(uint32_t device_id, const char *text, uint32_t text_len, uint32_t codepoint) {
    input_event_t event;
    event.type = INPUT_EVENT_TEXT;
    event.device_type = INPUT_DEVICE_KEYBOARD;
    event.device_id = device_id;
    event.modifiers = 0;
    event.x = 0;
    event.y = 0;
    event.dx = 0;
    event.dy = 0;
    event.key = 0;
    event.button = 0;
    event.codepoint = codepoint;
    event.text_len = input_copy_text(event.text, text, text_len);
    return input_push_event(&event);
}

int input_push_pointer_move(uint32_t device_id, int32_t x, int32_t y, int32_t dx, int32_t dy) {
    input_event_t event;
    event.type = INPUT_EVENT_POINTER_MOVE;
    event.device_type = INPUT_DEVICE_MOUSE;
    event.device_id = device_id;
    event.modifiers = 0;
    event.x = x;
    event.y = y;
    event.dx = dx;
    event.dy = dy;
    event.key = 0;
    event.button = 0;
    event.codepoint = 0;
    event.text_len = 0;
    input_copy_text(event.text, 0, 0);
    return input_push_event(&event);
}

int input_push_pointer_button(uint32_t device_id, int32_t x, int32_t y, int button, uint32_t modifiers) {
    input_event_t event;
    event.type = INPUT_EVENT_POINTER_BUTTON;
    event.device_type = INPUT_DEVICE_MOUSE;
    event.device_id = device_id;
    event.modifiers = modifiers;
    event.x = x;
    event.y = y;
    event.dx = 0;
    event.dy = 0;
    event.key = 0;
    event.button = button;
    event.codepoint = 0;
    event.text_len = 0;
    input_copy_text(event.text, 0, 0);
    return input_push_event(&event);
}

int input_push_pointer_wheel(uint32_t device_id, int32_t x, int32_t y, int32_t dx, int32_t dy) {
    input_event_t event;
    event.type = INPUT_EVENT_POINTER_WHEEL;
    event.device_type = INPUT_DEVICE_MOUSE;
    event.device_id = device_id;
    event.modifiers = 0;
    event.x = x;
    event.y = y;
    event.dx = dx;
    event.dy = dy;
    event.key = 0;
    event.button = 0;
    event.codepoint = 0;
    event.text_len = 0;
    input_copy_text(event.text, 0, 0);
    return input_push_event(&event);
}
