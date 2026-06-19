#include "openos.h"

static int parse_ip(const char *text, unsigned int *out)
{
    unsigned int parts[4];
    unsigned int count = 0;
    unsigned int part = 0;
    const char *p;

    if (!text || !out)
        return -1;

    for (p = text; ; p++) {
        char c = *p;
        if (c >= '0' && c <= '9') {
            part = part * 10U + (unsigned int)(c - '0');
            if (part > 255U)
                return -1;
        } else if (c == '.' || c == '\0') {
            if (count >= 4U)
                return -1;
            parts[count++] = part;
            part = 0;
            if (c == '\0')
                break;
        } else {
            return -1;
        }
    }

    if (count != 4U)
        return -1;

    *out = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
    return 0;
}

static void print_ip(unsigned int ip)
{
    printf("%u.%u.%u.%u",
           (ip >> 24) & 0xffU,
           (ip >> 16) & 0xffU,
           (ip >> 8) & 0xffU,
           ip & 0xffU);
}

int main(int argc, char **argv)
{
    openos_netinfo_t before;
    openos_netinfo_t after;
    unsigned int target;
    const char *target_name = 0;

    if (netinfo(&before) < 0) {
        printf("ping: no network device\n");
        return 1;
    }

    target = before.ip;
    if (argc > 1) {
        target_name = argv[1];
        if (parse_ip(target_name, &target) < 0) {
            if (dnslookup(target_name, &target) < 0) {
                printf("ping: cannot resolve %s\n", target_name);
                return 1;
            }
            printf("ping: %s resolved to ", target_name);
            print_ip(target);
            printf("\n");
        }
    }

    printf("PING ");
    if (target_name)
        printf("%s (", target_name);
    print_ip(target);
    if (target_name)
        printf(")");
    printf(": 4 data bytes\n");

    if (ping(target) < 0) {
        printf("ping: send failed\n");
        return 1;
    }

    {
        int i;
        for (i = 0; i < 20; i++) {
            openos_sleep(1);
            if (netinfo(&after) < 0)
                return 1;
            if (after.icmp_echo_replies > before.icmp_echo_replies) {
                printf("4 bytes from ");
                print_ip(target);
                printf(": icmp_seq=1 ttl=64\n");
                printf("1 packets transmitted, 1 received, 0%% packet loss\n");
                return 0;
            }
        }
    }

    printf("1 packets transmitted, 0 received, 100%% packet loss\n");
    return 1;
}
