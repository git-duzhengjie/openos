#ifndef OPENOS_DEVMGR_H
#define OPENOS_DEVMGR_H

#include "types.h"

#define DEVMGR_NAME_MAX 32
#define DEVMGR_BUS_MAX  16
#define DEVMGR_DEV_MAX  64
#define DEVMGR_EVENT_MAX 64

typedef enum devmgr_type {
    DEVMGR_TYPE_UNKNOWN = 0,
    DEVMGR_TYPE_CHAR = 1,
    DEVMGR_TYPE_BLOCK = 2,
    DEVMGR_TYPE_NET = 3,
    DEVMGR_TYPE_INPUT = 4,
    DEVMGR_TYPE_STORAGE = 5
} devmgr_type_t;

typedef enum devmgr_state {
    DEVMGR_STATE_EMPTY = 0,
    DEVMGR_STATE_PRESENT = 1,
    DEVMGR_STATE_REMOVED = 2
} devmgr_state_t;

typedef enum hotplug_action {
    HOTPLUG_ACTION_ADD = 1,
    HOTPLUG_ACTION_REMOVE = 2,
    HOTPLUG_ACTION_CHANGE = 3
} hotplug_action_t;

typedef struct devmgr_device {
    uint32_t id;
    char name[DEVMGR_NAME_MAX];
    char bus[DEVMGR_BUS_MAX];
    devmgr_type_t type;
    uint32_t major;
    uint32_t minor;
    uint32_t flags;
    devmgr_state_t state;
    void *device_ref;
} devmgr_device_t;

typedef struct hotplug_event {
    uint32_t seq;
    hotplug_action_t action;
    uint32_t device_id;
    char name[DEVMGR_NAME_MAX];
    char bus[DEVMGR_BUS_MAX];
    devmgr_type_t type;
    uint32_t major;
    uint32_t minor;
} hotplug_event_t;

typedef void (*hotplug_notifier_t)(const hotplug_event_t *event);

void devmgr_init(void);
int devmgr_register(const char *name, const char *bus, devmgr_type_t type,
                    uint32_t major, uint32_t minor, uint32_t flags,
                    void *device_ref);
int devmgr_unregister(const char *name);
int devmgr_notify_change(const char *name);
devmgr_device_t *devmgr_find(const char *name);
devmgr_device_t *devmgr_get_by_index(uint32_t index);
uint32_t devmgr_count(void);
uint32_t devmgr_event_count(void);
int devmgr_event_get(uint32_t index, hotplug_event_t *out);
int devmgr_poll_event(hotplug_event_t *out);
void devmgr_set_notifier(hotplug_notifier_t notifier);
void devmgr_print_devices(void);
void devmgr_print_hotplug_events(void);
const char *devmgr_type_name(devmgr_type_t type);
const char *devmgr_action_name(hotplug_action_t action);

#endif /* OPENOS_DEVMGR_H */
