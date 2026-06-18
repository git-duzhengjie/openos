#ifndef OPENOS_DHCP_H
#define OPENOS_DHCP_H

#include "net.h"

#define DHCP_CLIENT_PORT 68
#define DHCP_SERVER_PORT 67

typedef enum dhcp_state {
    DHCP_STATE_INIT = 0,
    DHCP_STATE_SELECTING,
    DHCP_STATE_REQUESTING,
    DHCP_STATE_BOUND,
    DHCP_STATE_FAILED
} dhcp_state_t;

void dhcp_init(void);
int dhcp_start(void);
int dhcp_renew(void);
int dhcp_release(void);
void dhcp_print_info(void);
uint32_t dhcp_get_dns_server(void);
dhcp_state_t dhcp_get_state(void);

#endif /* OPENOS_DHCP_H */
