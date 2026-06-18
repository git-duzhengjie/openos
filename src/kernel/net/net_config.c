#include "net_config.h"
#include "dhcp.h"
#include "../fs/vfs.h"
#include "string.h"

#define NET_CONFIG_PATH "/etc/network.conf"
#define NET_CONFIG_BUF_SIZE 256

static int net_config_append_char(char *buf, int size, int *pos, char c) {
    if (!buf || !pos || *pos < 0 || *pos >= size - 1) return -1;
    buf[*pos] = c;
    (*pos)++;
    buf[*pos] = '\0';
    return 0;
}

static int net_config_append_text(char *buf, int size, int *pos, const char *text) {
    if (!text) return -1;
    while (*text) {
        if (net_config_append_char(buf, size, pos, *text++) != 0) return -1;
    }
    return 0;
}

static int net_config_append_u8(char *buf, int size, int *pos, uint32_t value) {
    char tmp[3];
    int n = 0;
    if (value > 255u) return -1;
    if (value >= 100u) tmp[n++] = (char)('0' + (value / 100u));
    if (value >= 10u) tmp[n++] = (char)('0' + ((value / 10u) % 10u));
    tmp[n++] = (char)('0' + (value % 10u));
    for (int i = 0; i < n; i++) {
        if (net_config_append_char(buf, size, pos, tmp[i]) != 0) return -1;
    }
    return 0;
}

static void net_config_ipv4_to_string(uint32_t ip, char *out, int out_size) {
    int pos = 0;
    if (!out || out_size <= 0) return;
    out[0] = '\0';
    (void)net_config_append_u8(out, out_size, &pos, (ip >> 24) & 0xff);
    (void)net_config_append_char(out, out_size, &pos, '.');
    (void)net_config_append_u8(out, out_size, &pos, (ip >> 16) & 0xff);
    (void)net_config_append_char(out, out_size, &pos, '.');
    (void)net_config_append_u8(out, out_size, &pos, (ip >> 8) & 0xff);
    (void)net_config_append_char(out, out_size, &pos, '.');
    (void)net_config_append_u8(out, out_size, &pos, ip & 0xff);
}

static int net_config_parse_ipv4(const char *text, uint32_t *out) {
    uint32_t parts[4];
    int part = 0;
    uint32_t value = 0;
    int has_digit = 0;
    const char *p = text;
    if (!text || !out) return -1;
    while (*p && *p != '\n' && *p != '\r') {
        if (*p >= '0' && *p <= '9') {
            value = value * 10u + (uint32_t)(*p - '0');
            if (value > 255u) return -1;
            has_digit = 1;
        } else if (*p == '.') {
            if (!has_digit || part >= 3) return -1;
            parts[part++] = value;
            value = 0;
            has_digit = 0;
        } else {
            return -1;
        }
        p++;
    }
    if (!has_digit || part != 3) return -1;
    parts[part] = value;
    *out = NET_IP4(parts[0], parts[1], parts[2], parts[3]);
    return 0;
}

static int net_config_starts_with(const char *s, const char *prefix) {
    while (*prefix) {
        if (*s++ != *prefix++) return 0;
    }
    return 1;
}

static void net_config_parse_line(net_persist_config_t *cfg, const char *line) {
    if (!cfg || !line) return;
    if (net_config_starts_with(line, "mode=dhcp")) {
        cfg->mode = NET_CONFIG_MODE_DHCP;
    } else if (net_config_starts_with(line, "mode=static")) {
        cfg->mode = NET_CONFIG_MODE_STATIC;
    } else if (net_config_starts_with(line, "ip=")) {
        (void)net_config_parse_ipv4(line + 3, &cfg->ip);
    } else if (net_config_starts_with(line, "netmask=")) {
        (void)net_config_parse_ipv4(line + 8, &cfg->netmask);
    } else if (net_config_starts_with(line, "gateway=")) {
        (void)net_config_parse_ipv4(line + 8, &cfg->gateway);
    } else if (net_config_starts_with(line, "dns=")) {
        (void)net_config_parse_ipv4(line + 4, &cfg->dns);
    }
}

static int net_config_write_file(const net_persist_config_t *cfg) {
    char ip[16];
    char mask[16];
    char gateway[16];
    char dns[16];
    char buf[NET_CONFIG_BUF_SIZE];
    int len = 0;
    int fd;
    if (!cfg) return -1;
    net_config_ipv4_to_string(cfg->ip, ip, sizeof(ip));
    net_config_ipv4_to_string(cfg->netmask, mask, sizeof(mask));
    net_config_ipv4_to_string(cfg->gateway, gateway, sizeof(gateway));
    net_config_ipv4_to_string(cfg->dns, dns, sizeof(dns));
    buf[0] = '\0';
    if (net_config_append_text(buf, sizeof(buf), &len, "mode=") != 0) return -1;
    if (net_config_append_text(buf, sizeof(buf), &len,
                               cfg->mode == NET_CONFIG_MODE_DHCP ? "dhcp" : "static") != 0) return -1;
    if (net_config_append_text(buf, sizeof(buf), &len, "\nip=") != 0) return -1;
    if (net_config_append_text(buf, sizeof(buf), &len, ip) != 0) return -1;
    if (net_config_append_text(buf, sizeof(buf), &len, "\nnetmask=") != 0) return -1;
    if (net_config_append_text(buf, sizeof(buf), &len, mask) != 0) return -1;
    if (net_config_append_text(buf, sizeof(buf), &len, "\ngateway=") != 0) return -1;
    if (net_config_append_text(buf, sizeof(buf), &len, gateway) != 0) return -1;
    if (net_config_append_text(buf, sizeof(buf), &len, "\ndns=") != 0) return -1;
    if (net_config_append_text(buf, sizeof(buf), &len, dns) != 0) return -1;
    if (net_config_append_char(buf, sizeof(buf), &len, '\n') != 0) return -1;

    (void)vfs_mkdir("/etc", 0755);
    fd = vfs_open(NET_CONFIG_PATH, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) return -1;
    if (vfs_write(fd, buf, len) != len) {
        (void)vfs_close(fd);
        return -1;
    }
    return vfs_close(fd);
}

void net_config_init(void) {
    (void)vfs_mkdir("/etc", 0755);
}

int net_config_load(net_persist_config_t *out) {
    char buf[NET_CONFIG_BUF_SIZE];
    char line[64];
    int fd;
    int n;
    int i;
    int lp = 0;
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    out->mode = NET_CONFIG_MODE_DHCP;
    out->netmask = NET_IP4(255, 255, 255, 0);
    out->dns = NET_IP4(8, 8, 8, 8);

    fd = vfs_open(NET_CONFIG_PATH, O_RDONLY, 0);
    if (fd < 0) return -1;
    n = vfs_read(fd, buf, sizeof(buf) - 1);
    (void)vfs_close(fd);
    if (n <= 0) return -1;
    buf[n] = '\0';

    for (i = 0; i <= n; i++) {
        char c = buf[i];
        if (c == '\n' || c == '\r' || c == '\0') {
            if (lp > 0) {
                line[lp] = '\0';
                net_config_parse_line(out, line);
                lp = 0;
            }
        } else if (lp < (int)sizeof(line) - 1) {
            line[lp++] = c;
        }
    }
    return 0;
}

int net_config_save_dhcp(void) {
    net_persist_config_t cfg;
    net_device_info_t info;
    memset(&cfg, 0, sizeof(cfg));
    cfg.mode = NET_CONFIG_MODE_DHCP;
    cfg.netmask = NET_IP4(255, 255, 255, 0);
    cfg.dns = NET_IP4(8, 8, 8, 8);
    if (net_get_device_info(0, &info) == 0) {
        cfg.ip = info.ip;
        cfg.netmask = info.netmask;
        cfg.gateway = info.gateway;
        cfg.dns = info.dns;
    }
    return net_config_write_file(&cfg);
}

int net_config_save_static(uint32_t ip, uint32_t netmask, uint32_t gateway, uint32_t dns) {
    net_persist_config_t cfg;
    cfg.mode = NET_CONFIG_MODE_STATIC;
    cfg.ip = ip;
    cfg.netmask = netmask;
    cfg.gateway = gateway;
    cfg.dns = dns;
    return net_config_write_file(&cfg);
}

int net_config_apply_saved(void) {
    net_persist_config_t cfg;
    if (net_config_load(&cfg) != 0) return -1;
    if (cfg.mode == NET_CONFIG_MODE_STATIC) {
        net_config_ipv4(cfg.ip, cfg.netmask, cfg.gateway, cfg.dns);
        return 0;
    }
    if (cfg.mode == NET_CONFIG_MODE_DHCP) {
        dhcp_start();
        return 0;
    }
    return -1;
}
