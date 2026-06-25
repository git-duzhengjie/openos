#ifndef OPENOS_INPUT_H
#define OPENOS_INPUT_H

#include "types.h"

#define OPENOS_INPUT_ABI_VERSION_MAJOR 1u
#define OPENOS_INPUT_ABI_VERSION_MINOR 0u
#define OPENOS_INPUT_ABI_VERSION_PATCH 0u
#define OPENOS_INPUT_QUEUE_SIZE 128u

typedef enum input_event_type {
    INPUT_EVENT_NONE = 0,
    INPUT_EVENT_KEY = 1,
    INPUT_EVENT_TEXT = 2,
    INPUT_EVENT_POINTER_MOVE = 3,
    INPUT_EVENT_POINTER_BUTTON = 4,
    INPUT_EVENT_POINTER_WHEEL = 5,
    INPUT_EVENT_TOUCH = 6,
} input_event_type_t;

typedef enum input_device_type {
    INPUT_DEVICE_UNKNOWN = 0,
    INPUT_DEVICE_KEYBOARD = 1,
    INPUT_DEVICE_MOUSE = 2,
    INPUT_DEVICE_TOUCHSCREEN = 3,
    INPUT_DEVICE_TABLET = 4,
    INPUT_DEVICE_VIRTUAL = 5,
} input_device_type_t;

typedef struct input_event {
    input_event_type_t type;
    input_device_type_t device_type;
    uint32_t device_id;
    uint32_t modifiers;
    int32_t x;
    int32_t y;
    int32_t dx;
    int32_t dy;
    int32_t key;
    int32_t button;
    uint32_t codepoint;
    char text[8];
    uint32_t text_len;
} input_event_t;

void input_init(void);
int input_register_device(input_device_type_t type, const char *name);
int input_push_event(const input_event_t *event);
int input_poll_event(input_event_t *event);
int input_has_event(void);
void input_flush_events(void);
int input_push_key(uint32_t device_id, int key, uint32_t modifiers);
int input_push_text(uint32_t device_id, const char *text, uint32_t text_len, uint32_t codepoint);
int input_push_pointer_move(uint32_t device_id, int32_t x, int32_t y, int32_t dx, int32_t dy);
int input_push_pointer_button(uint32_t device_id, int32_t x, int32_t y, int button, uint32_t modifiers);
int input_push_pointer_wheel(uint32_t device_id, int32_t x, int32_t y, int32_t dx, int32_t dy);

#endif /* OPENOS_INPUT_H */
