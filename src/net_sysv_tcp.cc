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

/* Multi-user networking protocol implementation for TCP/IP on System V */

/* It appears that there may never be a use for this particular protocol
 * implementation because every vendor that provides a TLI interface to TCP
 * also provides a complete BSD-style sockets implementation, so one could
 * simply use net_bsd_tcp.c instead.  I went ahead and provided this code
 * anyway, for two reasons: (a) it gave the server code a kind of pleasing
 * symmetry and completeness (and me a sense of closure), and (b) it can,
 * perhaps, serve as a model for other TLI-based protocol implementations in
 * the future.  For example, there are vendors supplying TLI interfaces to the
 * XNS protocol family without providing a sockets-based interface.  In any
 * case, it was interesting to learn about TLI and may prove useful in the
 * future; I have been given to understand that TLI will probably form the
 * basis for the eventual POSIX standard networking interface.
 */

#include <stdexcept>

#include "my-inet.h"		/* inet_addr() */
#include <errno.h>		/* EMFILE */
#include "my-fcntl.h"		/* O_RDWR */
#include "my-in.h"		/* struct sockaddr_in, INADDR_ANY, htonl(),
				   * htons(), ntohl(), struct in_addr */
#include "my-ioctl.h"		/* ioctl() */
#include "my-socket.h"		/* AF_INET */
#include "my-stdlib.h"		/* strtoul() */
#include "my-string.h"		/* memcpy() */
#include "my-stropts.h"		/* I_POP, I_PUSH */
#include <sys/conf.h>		/* FMNAMESZ */
#include "my-tiuser.h"		/* t_open(), t_bind(), t_alloc(), t_accept() */
#include "my-unistd.h"		/* close() */

#include "config.h"
#include "log.h"
#include "name_lookup.h"
#include "net_proto.h"
#include "options.h"
#include "server.h"
#include "streams.h"
#include "structures.h"
#include "timers.h"

#include "net_tcp.cc"

static struct t_call *call = 0;

static void
log_ti_error(const char *msg)
{
    if (t_errno == TSYSERR)
	log_perror(msg);
    else
	errlog("%s: %s\n", msg, t_errlist[t_errno]);
}

const char *
proto_name(void)
{
    return "SysV/TCP";
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

    initialize_name_lookup();

    desc->type = TYPE_INT;
    desc->v.num = port;
    return 1;
}

enum error
proto_make_listener(Var desc, int *fd, Var * canon, const char **name)
{
    struct sockaddr_in req_addr, rec_addr;
    struct t_bind requested, received;
    int s, port;
    static Stream *st = 0;

    if (!st)
	st = new_stream(20);

    if (desc.type != TYPE_INT)
	return E_TYPE;

    port = desc.v.num;
    s = t_open((void *) "/dev/tcp", O_RDWR, 0);
    if (s < 0) {
	log_ti_error("Creating listening endpoint");
	return E_QUOTA;
    }
    req_addr.sin_family = AF_INET;
    req_addr.sin_addr.s_addr = bind_local_ip;
    req_addr.sin_port = htons(port);

    requested.addr.maxlen = sizeof(req_addr);
    requested.addr.len = sizeof(req_addr);
    requested.addr.buf = (void *) &req_addr;
    requested.qlen = 5;

    received.addr.maxlen = sizeof(rec_addr);
    received.addr.len = sizeof(rec_addr);
    received.addr.buf = (void *) &rec_addr;

    if (t_bind(s, &requested, &received) < 0) {
	enum error e = E_QUOTA;

	log_ti_error("Binding to listening address");
	t_close(s);
	if (t_errno == TACCES || (t_errno == TSYSERR && errno == EACCES))
	    e = E_PERM;
	return e;
    } else if (port != 0 && rec_addr.sin_port != htons(port)) {
	errlog("Can't bind to requested port!\n");
	t_close(s);
	return E_QUOTA;
    }
    if (!call)
	call = (struct t_call *) t_alloc(s, T_CALL, T_ADDR);
    if (!call) {
	log_ti_error("Allocating T_CALL structure");
	t_close(s);
	return E_QUOTA;
    }
    canon->type = TYPE_INT;
    canon->v.num = ntohs(rec_addr.sin_port);

    stream_printf(st, "port %d", canon->v.num);
    *name = reset_stream(st);

    *fd = s;
    return E_NONE;
}

int
proto_listen(int fd)
{
    /* Nothing to do. */
    return 1;
}

static int
set_rw_able(int fd)
{
    char buffer[FMNAMESZ + 1];

    if (ioctl(fd, I_LOOK, buffer) < 0
	|| strcmp(buffer, "timod") != 0
	|| ioctl(fd, I_POP, 0) < 0
	|| ioctl(fd, I_PUSH, (void *) "tirdwr") < 0) {
	log_perror("Setting up read/write behavior for connection");
	return 0;
    } else
	return 1;
}

enum proto_accept_error
proto_accept_connection(int listener_fd, int *read_fd, int *write_fd,
			const char **name)
{
    int timeout = server_int_option("name_lookup_timeout", 5);
    int fd;
    struct sockaddr_in *addr = (struct sockaddr_in *) call->addr.buf;
    static Stream *s = 0;

    if (!s)
	s = new_stream(100);

    fd = t_open((void *) "/dev/tcp", O_RDWR, 0);
    if (fd < 0) {
	if (t_errno == TSYSERR && errno == EMFILE)
	    return PA_FULL;
	else {
	    log_ti_error("Opening endpoint for new connection");
	    return PA_OTHER;
	}
    }
    if (t_bind(fd, 0, 0) < 0) {
	log_ti_error("Binding endpoint for new connection");
	t_close(fd);
	return PA_OTHER;
    }
    if (t_listen(listener_fd, call) < 0) {
	log_ti_error("Accepting new network connection");
	t_close(fd);
	return PA_OTHER;
    }
    if (t_accept(listener_fd, fd, call) < 0) {
	log_ti_error("Accepting new network connection");
	t_close(fd);
	return PA_OTHER;
    }
    if (!set_rw_able(fd)) {
	t_close(fd);
	return PA_OTHER;
    }
    *read_fd = *write_fd = fd;
    stream_printf(s, "%s, port %d",
		  lookup_name_from_addr(addr, timeout),
		  (int) ntohs(addr->sin_port));
    *name = reset_stream(s);
    return PA_OKAY;
}

void
proto_close_connection(int read_fd, int write_fd)
{
    /* read_fd and write_fd are the same, so we only need to deal with one. */
    t_close(read_fd);
}

void
proto_close_listener(int fd)
{
    t_close(fd);
}

#ifdef OUTBOUND_NETWORK

//static Exception timeout_exception;

class timeout_exception: public std::exception
{
public:
    timeout_exception() throw() {}

    virtual ~timeout_exception() throw() {}

    virtual const char* what() const throw() {
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
		      const char **local_name, const char **remote_name)
{
    /* These are `static' rather than `volatile' because I can't cope with
     * getting all those nasty little parameter-passing rules right.  This
     * function isn't recursive anyway, so it doesn't matter.
     */
    struct sockaddr_in rec_addr, req_addr;
    struct t_bind received, requested, *p_requested;
    static const char *host_name;
    static int port;
    static Timer_ID id;
    int fd, result;
    int timeout = server_int_option("name_lookup_timeout", 5);
    static struct sockaddr_in addr;
    static Stream *st1 = 0, *st2 = 0;

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

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = lookup_addr_from_name(host_name, timeout);
    if (addr.sin_addr.s_addr == 0)
	return E_INVARG;

    /* Cast to (void *) here to workaround const-less decls on some systems. */
    fd = t_open((void *) "/dev/tcp", O_RDWR, 0);
    if (fd < 0) {
	if (t_errno != TSYSERR || errno != EMFILE)
	    log_ti_error("Making endpoint in proto_open_connection");
	return E_QUOTA;
    }

    if (bind_local_ip == INADDR_ANY) {
	p_requested = 0;
    }
    else {
	req_addr.sin_family = AF_INET;
	req_addr.sin_addr.s_addr = bind_local_ip;
	req_addr.sin_port = 0;

	requested.addr.maxlen = sizeof(req_addr);
	requested.addr.len = sizeof(req_addr);
	requested.addr.buf = (void *) &req_addr;
	p_requested = &requested;
    }

    received.addr.maxlen = sizeof(rec_addr);
    received.addr.len = sizeof(rec_addr);
    received.addr.buf = (void *) &rec_addr;

    if (t_bind(fd, p_requested, &received) < 0) {
	log_ti_error("Binding outbound endpoint");
	t_close(fd);
	return E_QUOTA;
    }
    call->addr.maxlen = sizeof(addr);
    call->addr.len = sizeof(addr);
    call->addr.buf = (void *) &addr;

    try {
	id = set_timer(server_int_option("outbound_connect_timeout", 5),
		       timeout_proc, 0);
	result = t_connect(fd, call, 0);
	cancel_timer(id);
    }
    catch(timeout_exception& exception) {
	result = -1;
	errno = ETIMEDOUT;
	t_errno = TSYSERR;
	reenable_timers();
    }

    if (result < 0) {
	t_close(fd);
	log_ti_error("Connecting in proto_open_connection");
	return E_QUOTA;
    }
    if (!set_rw_able(fd)) {
	t_close(fd);
	return E_QUOTA;
    }
    *read_fd = *write_fd = fd;

    stream_printf(st1, "port %d", (int) ntohs(rec_addr.sin_port));
    *local_name = reset_stream(st1);

    stream_printf(st2, "%s, port %d", host_name, port);
    *remote_name = reset_stream(st2);

    return E_NONE;
}
#endif				/* OUTBOUND_NETWORK */
