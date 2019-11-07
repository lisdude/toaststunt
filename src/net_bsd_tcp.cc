/******************************************************************************
  Copyright (c) 1992, 1995, 1996 Xerox Corporation.  All rights reserved.
  Portions of this code were written by Stephen White, aka ghond.
  Use and copying of this software and preparation of derivative works based
  upon this software are permitted.  Any distribution of this software or
  derivative works must comply with all applicable United States export
  control laws.  This software is made available AS IS, and Xerox Corporation
  makes no warranty about the software, its performance or its conformity to
  any specification.  Any person obtaining a copy of this software is requested
  to send their name and post office or electronic mail address to:
    Pavel Curtis
    Xerox PARC
    3333 Coyote Hill Rd.
    Palo Alto, CA 94304
    Pavel@Xerox.Com
 *****************************************************************************/

#include <stdexcept>

/* Multi-user networking protocol implementation for TCP/IP on BSD UNIX */

#include <arpa/inet.h>		/* inet_addr() */
#include <errno.h>		/* EMFILE, EADDRNOTAVAIL, ECONNREFUSED,
				   * ENETUNREACH, ETIMEOUT */
#include <netinet/in.h>		/* struct sockaddr_in, INADDR_ANY, htons(),
				   * htonl(), ntohl(), struct in_addr */
#include <sys/socket.h>		/* socket(), AF_INET, SOCK_STREAM,
				   * setsockopt(), SOL_SOCKET, SO_REUSEADDR,
				   * bind(), struct sockaddr, accept(),
				   * connect() */
#include <stdlib.h>		/* strtoul() */
#include <string.h>		/* memcpy() */
#include <unistd.h>		/* close() */

#include "config.h"
#include "list.h"
#include "log.h"
#include "net_proto.h"
#include "options.h"
#include "server.h"
#include "streams.h"
#include "timers.h"
#include "utils.h"

#include "net_tcp.cc"
#include <netinet/tcp.h>
#include <netdb.h>

struct addrinfo tcp_hint;

const char *
proto_name(void)
{
    return "BSD/TCP";
}

int
proto_initialize(struct proto *proto, Var * desc, int argc, char **argv)
{
    int port = DEFAULT_PORT;

    proto->pocket_size = 1;
    proto->believe_eof = 1;
    proto->eol_out_string = "\r\n";

    if (!tcp_arguments(argc, argv, &port))
	return 0;

    memset(&tcp_hint, 0, sizeof tcp_hint);
    tcp_hint.ai_family = AF_UNSPEC;
    tcp_hint.ai_socktype = SOCK_STREAM;

    desc->type = TYPE_INT;
    desc->v.num = port;
    return 1;
}

enum error
proto_make_listener(Var desc, int *fd, Var * canon, const char **name, bool use_ipv6)
{
    int s, port, yes = 1;
    static Stream *st = nullptr;
    struct addrinfo hints;
    struct addrinfo *servinfo, *p;
    
    memset(&hints, 0, sizeof hints);
    hints.ai_family = (use_ipv6 ? AF_INET6 : AF_INET);
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;        // use all the IPs

    if (!st)
        st = new_stream(20);

    if (desc.type != TYPE_INT)
        return E_TYPE;

    port = desc.v.num;

    int rv = getaddrinfo(use_ipv6 ? bind_ipv6 : bind_ipv4, get_port_str(port), &hints, &servinfo);
    if (rv != 0) {
        log_perror(gai_strerror(rv));
        freeaddrinfo(servinfo);
        return E_QUOTA;
    }

    /* If we have multiple results, we'll bind to the first one we can. */
    for (p = servinfo; p != nullptr; p = p->ai_next) {
        if ((s = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("Creating listening socket");
            continue;
        }

        if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) == -1) {
            perror("Setting listening socket options");
            close(s);
            freeaddrinfo(servinfo);
            return E_QUOTA;
        }

        if (use_ipv6 && setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, &yes, sizeof yes) == -1) {
            perror("Disabling dual-stack mode for IPv6");
            close(s);
            freeaddrinfo(servinfo);
            return E_QUOTA;
        }

        if (port == 0) {
            applog(LOG_INFO2, "Port is 0 in proto_make_listener. Attempting to discover actual local port number...\n");
            if (getsockname(s, p->ai_addr, &(p->ai_addrlen)) < 0) {
                log_perror("... failed to discover local port number");
                close(s);
                freeaddrinfo(servinfo);
                return E_QUOTA;
            } else {
                canon->type = TYPE_INT;
                canon->v.num = get_in_port((struct sockaddr_storage *)p->ai_addr);
                applog(LOG_INFO2, "... port is now %i\n", canon->v.num);
            }
        } else if (bind(s, p->ai_addr, p->ai_addrlen) == -1) {
            close(s);
            perror("Binding listening socket");
            continue;
        } else {
            *canon = var_ref(desc);
        }
        break;
    }

    if (p == nullptr) {
        enum error e = E_QUOTA;

        log_perror("Failed to bind to listening socket");
        if (errno == EACCES)
            e = E_PERM;
        freeaddrinfo(servinfo);
        return e;
    }

    const char *nameinfo = get_ntop((struct sockaddr_storage *)p->ai_addr);
    stream_printf(st, "%s port %" PRIdN, nameinfo, canon->v.num);
    *name = reset_stream(st);
    free_str(nameinfo);
    *fd = s;

    freeaddrinfo(servinfo);

    return E_NONE;
}

int
proto_listen(int fd)
{
    int status = listen(fd, 5);
    if (status != 0)
        log_perror("Failed to listen");
    return status == 0 ? 1 : 0;
}

enum proto_accept_error
proto_accept_connection(int listener_fd, int *read_fd, int *write_fd,
			const char **name, struct sockaddr_storage *ip_addr)
{
    int option = 1;
    int fd;
    struct sockaddr_storage addr;
    socklen_t addr_length = sizeof addr;
    static Stream *s = nullptr;

    if (!s)
        s = new_stream(100);

    fd = accept(listener_fd, (struct sockaddr *)&addr, &addr_length);
    if (fd < 0) {
        if (errno == EMFILE)
            return PA_FULL;
        else {
            log_perror("Accepting new network connection");
            return PA_OTHER;
        }
    }

    // Disable Nagle algorithm on the socket
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char *) &option, sizeof(option)) < 0)
        log_perror("Couldn't set TCP_NODELAY");
#ifdef __linux__
    // Disable delayed ACKs
    if (setsockopt(fd, IPPROTO_TCP, TCP_QUICKACK, (char*) &option, sizeof(option)) < 0)
        log_perror("Couldn't set TCP_QUICKACK");
#endif

    *read_fd = *write_fd = fd;

    const char *nameinfo;
    if (!server_int_option("no_name_lookup", NO_NAME_LOOKUP))
        nameinfo = get_nameinfo((struct sockaddr *)&addr);
    else
        nameinfo = get_ntop(&addr);

    stream_printf(s, "%s, port %" PRIdN,
            nameinfo,
            get_in_port(&addr));
   
    *name = reset_stream(s);
    *ip_addr = addr;
    free_str(nameinfo);
    return PA_OKAY;
}

void
proto_close_connection(int read_fd, int write_fd)
{
    /* read_fd and write_fd are the same, so we only need to deal with one. */
    close(read_fd);
}

void
proto_close_listener(int fd)
{
    close(fd);
}

void *get_in_addr(const struct sockaddr_storage *sa)
{
    switch (sa->ss_family) {
        case AF_INET:
            return &(((struct sockaddr_in*)sa)->sin_addr);
        case AF_INET6:
            return &(((struct sockaddr_in6*)sa)->sin6_addr);
        default:
            return nullptr;
    }
}

unsigned short int get_in_port(const struct sockaddr_storage *sa)
{
    switch (sa->ss_family) {
        case AF_INET:
            return ntohs(((struct sockaddr_in*)sa)->sin_port);
        case AF_INET6:
            return ntohs(((struct sockaddr_in6*)sa)->sin6_port);
        default:
            return 0;
    }
}

const char *get_ntop(const struct sockaddr_storage *sa)
{
    switch (sa->ss_family) {
        case AF_INET:
            char ip4[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &(((struct sockaddr_in *)sa)->sin_addr), ip4, INET_ADDRSTRLEN);
            return str_dup(ip4);
        case AF_INET6:
            char ip6[INET6_ADDRSTRLEN];
            inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)sa)->sin6_addr), ip6, INET6_ADDRSTRLEN);
            return str_dup(ip6);
        default:
            return str_dup(">>unknown address<<");
        }
}

const char *get_nameinfo(const struct sockaddr *sa)
{
    char hostname[NI_MAXHOST] = "";
    socklen_t sa_length = (sa->sa_family == AF_INET6 ? sizeof(sockaddr_in6) : sizeof(sockaddr_in));
    
    int status = getnameinfo(sa, sa_length, hostname, sizeof hostname, nullptr, 0, 0);

    if (status != 0) {
        /* Don't bother reporting unrecognized family errors.
           More than likely it's because it's IPv6 '::' */
        if (status != EAI_FAMILY)
            errlog("getnameinfo failed: %s\n", gai_strerror(status));
        return get_ntop((sockaddr_storage *)sa);
    }

    return str_dup(hostname);
}

const char *get_nameinfo_port(const struct sockaddr *sa)
{
    char service[NI_MAXSERV];
    int status = getnameinfo(sa, sizeof *sa, nullptr, 0, service, sizeof service, NI_NUMERICSERV);

    if (status != 0) {
        errlog("getnameinfo_port failed: %s\n", gai_strerror(status));
        return nullptr;
    }

    return str_dup(service);
}

const char *get_ipver(const struct sockaddr_storage *sa)
{
    switch (sa->ss_family) {
    case AF_INET:
        return "IPv4";
    case AF_INET6:
        return "IPv6";
    default:
        return ">>unknown protocol<<";
    }
}

const char *get_port_str(int port)
{
    char *port_str = nullptr;
    asprintf(&port_str, "%d", port);
    return port_str;
}

#ifdef OUTBOUND_NETWORK

#include "structures.h"

class timeout_exception: public std::exception
{
public:
    timeout_exception() throw() {}

    ~timeout_exception() throw() override {}

    const char* what() const throw() override {
	return "timeout";
    }
};

static void
timeout_proc(Timer_ID id, Timer_Data data)
{
    throw timeout_exception();
}

enum error
proto_open_connection(Var arglist, int *read_fd, int *write_fd,
		      const char **local_name, const char **remote_name, struct sockaddr_storage *ip_addr)
{
    /* These are `static' rather than `volatile' because I can't cope with
     * getting all those nasty little parameter-passing rules right.  This
     * function isn't recursive anyway, so it doesn't matter.
     */
    static const char *host_name;
    static int port;
    static Timer_ID id;
    int s, result;
    static Stream *st1 = nullptr, *st2 = nullptr;
    struct addrinfo *servinfo, *p;
    int yes = 1;

    if (!outbound_network_enabled)
	return E_PERM;

    if (!st1) {
	st1 = new_stream(20);
	st2 = new_stream(50);
    }
    if (arglist.v.list[0].v.num != 2)
	return E_ARGS;
    else if (arglist.v.list[1].type != TYPE_STR ||
	     arglist.v.list[2].type != TYPE_INT)
	return E_TYPE;

    host_name = arglist.v.list[1].v.str;
    port = arglist.v.list[2].v.num;

    int rv = getaddrinfo(host_name, get_port_str(port), &tcp_hint, &servinfo);
    if (rv != 0) {
        errlog("proto_open_connection getaddrinfo error: %s\n", gai_strerror(rv));
        return E_INVARG;
    }

    /* If we have multiple results, we'll bind to the first one we can. */
    for (p = servinfo; p != nullptr; p = p->ai_next) {
        if ((s = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
        if (errno != EMFILE)
            log_perror("Making socket in proto_open_connection");
        continue;
        }

        if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
            perror("Setting listening socket options");
            close(s);
            freeaddrinfo(servinfo);
            return E_QUOTA;
        }

        break;
    }

    if (p == nullptr) {
        enum error e = E_QUOTA;

        log_perror("Failed to bind to listening socket");
        if (errno == EACCES)
            e = E_PERM;
        freeaddrinfo(servinfo);
        return e;
    }
    
    try {
	id = set_timer(server_int_option("outbound_connect_timeout", 5),
		       timeout_proc, nullptr);
	result = connect(s, p->ai_addr, p->ai_addrlen);
	cancel_timer(id);
    }
    catch (timeout_exception& exception) {
	result = -1;
	errno = ETIMEDOUT;
	reenable_timers();
    }

    if (result < 0) {
	close(s);
    freeaddrinfo(servinfo);
	if (errno == EADDRNOTAVAIL ||
	    errno == ECONNREFUSED ||
	    errno == ENETUNREACH ||
	    errno == ETIMEDOUT)
	    return E_INVARG;
	log_perror("Connecting in proto_open_connection");
	return E_QUOTA;
    }
    if (getsockname(s, p->ai_addr, &(p->ai_addrlen)) < 0) {
	close(s);
    freeaddrinfo(servinfo);
	log_perror("Getting local name in proto_open_connection");
	return E_QUOTA;
    }
    *read_fd = *write_fd = s;

    stream_printf(st1, "port %" PRIdN, get_in_port((struct sockaddr_storage *)p->ai_addr));
    *local_name = reset_stream(st1);

    stream_printf(st2, "%s, port %" PRIdN, host_name, port);
    *remote_name = reset_stream(st2);

    memcpy(ip_addr, (struct sockaddr_storage *)(p->ai_addr), sizeof p->ai_addr);

    freeaddrinfo(servinfo);

    return E_NONE;
}
#endif				/* OUTBOUND_NETWORK */

int
network_parse_proxy_string(char *command, Stream *new_connection_name, struct sockaddr_storage *new_ai_addr)
{
    applog(LOG_INFO3, "PROXY: Proxy command detected: %s\n", command);
    char *source = nullptr;
    char *source_port = nullptr;
    char *destination_port = nullptr;
    char *split = strtok(command, " ");

    int x = 0;
    for (x = 1; x <= 6; x++) {
        // Just in case something goes horribly wrong...
        if (split == nullptr) {
            errlog("PROXY: Proxy command parsing failed!\n");
            return 1;
        }
        switch (x) {
            case 3:
                source = split;
                break;
            case 5:
                source_port = split;
                break;
            case 6:
                destination_port = split;
                break;
            default:
                break;
        }
        split = strtok(nullptr, " ");
    }

    struct addrinfo *address;
    getaddrinfo(source, source_port, &tcp_hint, &address);

    const char *nameinfo;
    if (!server_int_option("no_name_lookup", NO_NAME_LOOKUP))
        nameinfo = get_nameinfo(address->ai_addr);
    else
        nameinfo = get_ntop((struct sockaddr_storage *)address->ai_addr);

    stream_printf(new_connection_name, "port %s from %s, port %s",
            destination_port,
            nameinfo,
            source_port);

    free_str(nameinfo);

    memcpy(new_ai_addr, (struct sockaddr_storage *)address->ai_addr, address->ai_addrlen);

    freeaddrinfo(address);

    return 0;
}
