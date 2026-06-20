#ifndef OPENOS_DNS_H
#define OPENOS_DNS_H

#include "net.h"

#define DNS_PORT 53
#define DNS_DEFAULT_SERVER NET_IP4(8, 8, 8, 8)

typedef enum dns_state {
    DNS_STATE_IDLE = 0,
    DNS_STATE_QUERYING,
    DNS_STATE_RESOLVED,
    DNS_STATE_FAILED
} dns_state_t;

void dns_init(void);
int dns_query_a(const char *name);
void dns_set_server(uint32_t server_ip);
uint32_t dns_get_server(void);
uint32_t dns_get_last_result(void);
dns_state_t dns_get_state(void);
uint32_t dns_get_cache_hits(void);
uint32_t dns_get_cache_negative_hits(void);
void dns_mark_failed(void);
void dns_print_info(void);

#endif /* OPENOS_DNS_H */
