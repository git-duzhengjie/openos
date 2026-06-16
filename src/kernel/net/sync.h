#ifndef OPENOS_SYNC_H
#define OPENOS_SYNC_H

#include "types.h"

#define SYNC_PORT 40556
#define SYNC_ITEM_KEY_MAX 32
#define SYNC_ITEM_VALUE_MAX 128
#define SYNC_MAX_ITEMS 32
#define SYNC_TASK_ID_MAX 32
#define SYNC_TASK_TEXT_MAX 96
#define SYNC_MAX_TASKS 16
#define SYNC_RETRY_MAX 16


typedef enum sync_task_state {
    SYNC_TASK_EMPTY = 0,
    SYNC_TASK_OFFERED = 1,
    SYNC_TASK_ACCEPTED = 2,
    SYNC_TASK_DONE = 3
} sync_task_state_t;

typedef struct sync_item {
    int used;
    char key[SYNC_ITEM_KEY_MAX];
    char value[SYNC_ITEM_VALUE_MAX];
    char owner[32];
    uint32_t version;
    uint32_t updated_at;
} sync_item_t;

typedef struct sync_task {
    int used;
    char task_id[SYNC_TASK_ID_MAX];
    char title[SYNC_TASK_TEXT_MAX];
    char payload[SYNC_TASK_TEXT_MAX];
    char result[SYNC_TASK_TEXT_MAX];
    char owner[32];
    char assignee[32];
    sync_task_state_t state;
    uint32_t updated_at;
} sync_task_t;

void sync_init(void);
int sync_put(const char *key, const char *value);
int sync_delete(const char *key);
int sync_file_put(const char *path, const char *content);
int sync_file_delete(const char *path);
int sync_clipboard_set(const char *text);
int sync_message_send(const char *channel, const char *message);
int sync_broadcast_key(const char *key);
int sync_broadcast_all(void);
int sync_task_offer(const char *task_id, const char *title, const char *payload, const char *target_device_id);
int sync_task_accept(const char *task_id);
int sync_task_done(const char *task_id, const char *result);
void sync_tick(uint32_t ticks);
void sync_print_info(void);
void sync_print_items(void);
int sync_retry_pending(void);
void sync_print_reliable(void);
void sync_print_tasks(void);

#endif /* OPENOS_SYNC_H */
