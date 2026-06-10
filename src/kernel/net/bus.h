#ifndef OPENOS_BUS_H
#define OPENOS_BUS_H

#include "types.h"
#include "discovery.h"

#define BUS_PORT 40557
#define BUS_TOPIC_MAX 48
#define BUS_PAYLOAD_MAX 256
#define BUS_MSG_ID_MAX 48
#define BUS_MAX_SUBSCRIBERS 16
#define BUS_SEEN_MAX 32
#define BUS_RELIABLE_MAX 16
#define BUS_RELIABLE_RETRY_INTERVAL 3U
#define BUS_RELIABLE_RETRY_LIMIT 4U

#define BUS_PUBLISH_LOCAL  0x01U
#define BUS_PUBLISH_REMOTE 0x02U
#define BUS_PUBLISH_ALL    (BUS_PUBLISH_LOCAL | BUS_PUBLISH_REMOTE)

typedef struct bus_message {
    char msg_id[BUS_MSG_ID_MAX];
    char from[DISCOVERY_DEVICE_ID_MAX];
    char topic[BUS_TOPIC_MAX];
    char payload[BUS_PAYLOAD_MAX];
    uint32_t src_ip;
    int remote;
} bus_message_t;

typedef void (*bus_handler_t)(const bus_message_t *message, void *context);

void bus_init(void);
int bus_subscribe(const char *topic, bus_handler_t handler, void *context);
int bus_unsubscribe(const char *topic, bus_handler_t handler, void *context);
int bus_publish(const char *topic, const char *payload, uint32_t flags);
int bus_shell_subscribe(const char *topic);
void bus_print_info(void);
void bus_print_subscribers(void);
void bus_print_stats(void);

int bus_reliable_send(uint16_t port, const char *msg_id, const char *packet, uint16_t len, const char *target);
int bus_reliable_send_ack(uint16_t port, const char *proto, const char *ack_for, const char *to);
void bus_reliable_ack(uint16_t port, const char *msg_id, const char *from);
int bus_reliable_seen_before(uint16_t port, const char *from, const char *msg_id);
void bus_reliable_tick(uint32_t ticks);
int bus_reliable_pending_port(uint16_t port);
void bus_reliable_print_port(uint16_t port);

#endif /* OPENOS_BUS_H */
