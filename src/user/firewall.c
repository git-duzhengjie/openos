#include "openos.h"

static int streq(const char *a, const char *b)
{
    return openos_strcmp(a, b) == 0;
}

static unsigned int parse_uint(const char *s, int *ok)
{
    unsigned int v = 0;
    int i = 0;

    *ok = 0;
    if (!s || !s[0]) return 0;
    while (s[i]) {
        if (s[i] < '0' || s[i] > '9') return 0;
        v = v * 10u + (unsigned int)(s[i] - '0');
        i++;
    }
    *ok = 1;
    return v;
}

static const char *proto_name(unsigned int proto)
{
    if (proto == OPENOS_FW_PROTO_TCP) return "tcp";
    if (proto == OPENOS_FW_PROTO_UDP) return "udp";
    if (proto == OPENOS_FW_PROTO_ICMP) return "icmp";
    return "any";
}

static int parse_proto(const char *s, unsigned int *proto)
{
    if (streq(s, "tcp")) *proto = OPENOS_FW_PROTO_TCP;
    else if (streq(s, "udp")) *proto = OPENOS_FW_PROTO_UDP;
    else if (streq(s, "icmp")) *proto = OPENOS_FW_PROTO_ICMP;
    else if (streq(s, "any")) *proto = OPENOS_FW_PROTO_ANY;
    else return -1;
    return 0;
}

static void usage(void)
{
    printf("usage:\n");
    printf("  firewall list\n");
    printf("  firewall allow|deny tcp|udp <port>\n");
    printf("  firewall allow|deny icmp|any\n");
    printf("  firewall delete <index>\n");
    printf("  firewall clear\n");
}

static int list_rules(void)
{
    openos_firewall_rule_t rule;
    int any = 0;
    unsigned int i;

    printf("idx action proto port hits\n");
    for (i = 0; i < 16u; i++) {
        if (firewall(OPENOS_FW_OP_GET, i, &rule) < 0) return 1;
        if (!rule.used) continue;
        any = 1;
        printf("%u   %s   %s   %u   %u\n",
               i,
               rule.action == OPENOS_FW_ACTION_DENY ? "deny" : "allow",
               proto_name(rule.protocol),
               rule.port,
               rule.hits);
    }
    if (!any) printf("<empty> default allow\n");
    return 0;
}

int main(int argc, char **argv)
{
    openos_firewall_rule_t rule;
    unsigned int index;
    int ok;

    if (argc < 2) {
        usage();
        return 1;
    }

    if (streq(argv[1], "list")) {
        return list_rules();
    }

    if (streq(argv[1], "clear")) {
        if (firewall(OPENOS_FW_OP_CLEAR, 0, 0) < 0) {
            printf("firewall: permission denied or failed\n");
            return 1;
        }
        return 0;
    }

    if (streq(argv[1], "delete")) {
        if (argc != 3) {
            usage();
            return 1;
        }
        index = parse_uint(argv[2], &ok);
        if (!ok || firewall(OPENOS_FW_OP_DELETE, index, 0) < 0) {
            printf("firewall: delete failed\n");
            return 1;
        }
        return 0;
    }

    if (streq(argv[1], "allow") || streq(argv[1], "deny")) {
        if (argc < 3 || argc > 4) {
            usage();
            return 1;
        }
        rule.used = 1;
        rule.action = streq(argv[1], "deny") ? OPENOS_FW_ACTION_DENY : OPENOS_FW_ACTION_ALLOW;
        if (parse_proto(argv[2], &rule.protocol) < 0) {
            usage();
            return 1;
        }
        rule.port = 0;
        rule.hits = 0;
        if (rule.protocol == OPENOS_FW_PROTO_TCP || rule.protocol == OPENOS_FW_PROTO_UDP) {
            if (argc != 4) {
                usage();
                return 1;
            }
            rule.port = parse_uint(argv[3], &ok);
            if (!ok || rule.port > 65535u) {
                usage();
                return 1;
            }
        } else if (argc != 3) {
            usage();
            return 1;
        }
        index = (unsigned int)firewall(OPENOS_FW_OP_ADD, 0, &rule);
        if ((int)index < 0) {
            printf("firewall: add failed\n");
            return 1;
        }
        printf("firewall: rule %u added\n", index);
        return 0;
    }

    usage();
    return 1;
}
