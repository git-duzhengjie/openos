#ifndef OPENOS_DISCOVERY_H
#define OPENOS_DISCOVERY_H

#include "types.h"
#include "net.h"

#define DISCOVERY_DEVICE_ID_MAX 32
#define DISCOVERY_NAME_MAX 32
#define DISCOVERY_OS_MAX 16
#define DISCOVERY_CAPS_MAX 64
#define DISCOVERY_MAX_PEERS 16
#define DISCOVERY_PORT 40555
#define DISCOVERY_AUTH_SECRET_MAX 64
#define DISCOVERY_AUTH_NONCE_HEX_LEN 16
#define DISCOVERY_AUTH_PROOF_HEX_LEN 64

typedef enum discovery_message_type {
    DISCOVERY_MSG_HELLO = 1,
    DISCOVERY_MSG_QUERY = 2,
    DISCOVERY_MSG_BYE = 3,
    DISCOVERY_MSG_AUTH_CHALLENGE = 4,
    DISCOVERY_MSG_AUTH_RESPONSE = 5,
    DISCOVERY_MSG_AUTH_OK = 6
} discovery_message_type_t;

typedef enum discovery_auth_status {
    DISCOVERY_AUTH_NONE = 0,
    DISCOVERY_AUTH_CHALLENGE_SENT = 1,
    DISCOVERY_AUTH_RESPONSE_SENT = 2,
    DISCOVERY_AUTH_TRUSTED = 3,
    DISCOVERY_AUTH_FAILED = 4
} discovery_auth_status_t;

typedef struct discovery_peer {
    int used;
    char device_id[DISCOVERY_DEVICE_ID_MAX];
    char name[DISCOVERY_NAME_MAX];
    char os[DISCOVERY_OS_MAX];
    char capabilities[DISCOVERY_CAPS_MAX];
    uint32_t ip;
    uint32_t last_seen;
    uint32_t ttl;
    discovery_auth_status_t auth_status;
    char auth_nonce[DISCOVERY_AUTH_NONCE_HEX_LEN + 1];
    uint32_t auth_last_seen;
    uint32_t auth_failures;
} discovery_peer_t;

void discovery_init(void);
int discovery_set_local_name(const char *name);
int discovery_set_local_capabilities(const char *capabilities);
int discovery_announce(void);
int discovery_query(void);
int discovery_goodbye(void);
int discovery_set_auth_secret(const char *secret);
int discovery_auth_peer(const char *device_id);
void discovery_tick(uint32_t ticks);
uint32_t discovery_peer_count(void);
const discovery_peer_t *discovery_peer_get(uint32_t index);
void discovery_get_local_device_id(char *out, uint32_t out_size);
void discovery_print_info(void);
void discovery_print_peers(void);
void discovery_print_auth(void);

#endif /* OPENOS_DISCOVERY_H */
