#ifndef OPENOS_NET_CONFIG_H
#define OPENOS_NET_CONFIG_H

#include "types.h"
#include "net.h"

typedef struct net_persist_config {
    net_config_mode_t mode;
    uint32_t ip;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t dns;
} net_persist_config_t;

void net_config_init(void);
int net_config_load(net_persist_config_t *out);
int net_config_save_dhcp(void);
int net_config_save_static(uint32_t ip, uint32_t netmask, uint32_t gateway, uint32_t dns);
int net_config_apply_saved(void);

#endif /* OPENOS_NET_CONFIG_H */
