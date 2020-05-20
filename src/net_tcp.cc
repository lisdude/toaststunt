/************
 * net_tcp.c
 *
 * common code for
 * multi-user networking protocol implementations for TCP/IP
 *
 */

#ifdef OUTBOUND_NETWORK
static char outbound_network_enabled = OUTBOUND_NETWORK;
#endif

static const char *bind_ipv4 = nullptr;
static const char *bind_ipv6 = nullptr;

const char *
proto_usage_string(void)
{
    return "[+O|-O] [-4 ipv4_address] [-6 ipv6_address] [[-p] port]";
}


static int
tcp_arguments(int argc, char **argv, int *pport)
{
    char *p = nullptr;

    for ( ; argc > 0; argc--, argv++) {
        if (argc > 0
                && (argv[0][0] == '-' || argv[0][0] == '+')
                && argv[0][1] == 'O'
                && argv[0][2] == 0
           ) {
#ifdef OUTBOUND_NETWORK
            outbound_network_enabled = (argv[0][0] == '+');
#else
            if (argv[0][0] == '+') {
                fprintf(stderr, "Outbound network not supported.\n");
                oklog("CMDLINE: *** Ignoring %s (outbound network not supported)\n", argv[0]);
            }
#endif
        }
        else if (0 == strcmp(argv[0], "-4")) {
            if (argc <= 1)
                return 0;
            argc--;
            argv++;
            bind_ipv4 = str_dup(argv[0]);
            oklog("CMDLINE: IPv4 source address restricted to %s\n", argv[0]);
        } else if (0 == strcmp(argv[0], "-6")) {
            if (argc <= 1)
                return 0;
            argc--;
            argv++;
            bind_ipv6 = str_dup(argv[0]);
            oklog("CMDLINE: IPv6 source address restricted to %s\n", argv[0]);
        } else {
            if (p != nullptr) /* strtoul always sets p */
                return 0;
            if (0 == strcmp(argv[0], "-p")) {
                if (argc <= 1)
                    return 0;
                argc--;
                argv++;
            }
            *pport = strtoul(argv[0], &p, 10);
            if (*p != '\0')
                return 0;
            oklog("CMDLINE: Initial port = %d\n", *pport);
        }
    }
#ifdef OUTBOUND_NETWORK
    oklog("CMDLINE: Outbound network connections %s.\n",
          outbound_network_enabled ? "enabled" : "disabled");
#endif
    return 1;
}
