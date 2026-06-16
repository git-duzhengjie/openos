#include "openos.h"

static const char *proto_name(unsigned int proto)
{
    if (proto == OPENOS_FW_PROTO_ICMP) return "icmp";
    if (proto == OPENOS_FW_PROTO_TCP) return "tcp";
    if (proto == OPENOS_FW_PROTO_UDP) return "udp";
    return "any";
}

static const char *action_name(unsigned int action)
{
    return action == OPENOS_FW_ACTION_DENY ? "deny" : "allow";
}

static int parse_proto(const char *s, unsigned int *proto)
{
    if (!s || !proto) return -1;
    if (strcmp(s, "any") == 0) *proto = OPENOS_FW_PROTO_ANY;
    else if (strcmp(s, "icmp") == 0) *proto = OPENOS_FW_PROTO_ICMP;
    else if (strcmp(s, "tcp") == 0) *proto = OPENOS_FW_PROTO_TCP;
    else if (strcmp(s, "udp") == 0) *proto = OPENOS_FW_PROTO_UDP;
    else return -1;
    return 0;
}

static int parse_action(const char *s, unsigned int *action)
{
    if (!s || !action) return -1;
    if (strcmp(s, "allow") == 0) *action = OPENOS_FW_ACTION_ALLOW;
    else if (strcmp(s, "deny") == 0) *action = OPENOS_FW_ACTION_DENY;
    else return -1;
    return 0;
}

static int parse_uint(const char *s, unsigned int *out)
{
    unsigned int value = 0;
    int i = 0;
    if (!s || !out || s[0] == '\0') return -1;
    while (s[i]) {
        if (s[i] < '0' || s[i] > '9') return -1;
        value = value * 10u + (unsigned int)(s[i] - '0');
        if (value > 65535u) return -1;
        i++;
    }
    *out = value;
    return 0;
}

static void usage(void)
{
    printf("usage:\n");
    printf("  firewall list\n");
    printf("  firewall add <allow|deny> <any|icmp|tcp|udp> [port]\n");
    printf("  firewall del <index>\n");
    printf("  firewall clear\n");
}

static int list_rules(void)
{
    openos_firewall_rule_t rule;
    unsigned int i;
    int any = 0;

    printf("Idx Action Proto Port Hits\n");
    for (i = 0; i < 16u; i++) {
        if (firewall(OPENOS_FW_OP_GET, i, &rule) < 0) return 1;
        if (!rule.used) continue;
        printf("%3u %-6s %-5s %4u %4u\n", i, action_name(rule.action),
               proto_name(rule.protocol), rule.port, rule.hits);
        any = 1;
    }
    if (!any) printf("(no firewall rules)\n");
    return 0;
}

int main(int argc, char **argv)
{
    openos_firewall_rule_t rule;
    unsigned int value;

    if (argc < 2) {
        usage();
        return 1;
    }

    if (strcmp(argv[1], "list") == 0) {
        return list_rules();
    }

    if (strcmp(argv[1], "clear") == 0) {
        if (firewall(OPENOS_FW_OP_CLEAR, 0, 0) < 0) {
            printf("firewall: clear failed\n");
            return 1;
        }
        return 0;
    }

    if (strcmp(argv[1], "del") == 0) {
        if (argc != 3 || parse_uint(argv[2], &value) < 0 || value >= 16u) {
            usage();
            return 1;
        }
        if (firewall(OPENOS_FW_OP_DELETE, value, 0) < 0) {
            printf("firewall: delete failed\n");
            return 1;
        }
        return 0;
    }

    if (strcmp(argv[1], "add") == 0) {
        if (argc < 4 || argc > 5) {
            usage();
            return 1;
        }
        memset(&rule, 0, sizeof(rule));
        if (parse_action(argv[2], &rule.action) < 0 || parse_proto(argv[3], &rule.protocol) < 0) {
            usage();
            return 1;
        }
        if (argc == 5) {
            if (parse_uint(argv[4], &rule.port) < 0) {
                usage();
                return 1;
            }
        }
        if (firewall(OPENOS_FW_OP_ADD, 0, &rule) < 0) {
            printf("firewall: add failed\n");
            return 1;
        }
        return 0;
    }

    usage();
    return 1;
}
