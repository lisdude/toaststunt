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

#include "my-ctype.h"
#include <errno.h>
#include "my-fcntl.h"
#include "my-ioctl.h"
#include "my-signal.h"
#include "my-stdio.h"
#include "my-stdlib.h"
#include "my-string.h"
#include "my-unistd.h"

#include "config.h"
#include "list.h"
#include "log.h"
#include "net_mplex.h"
#include "net_multi.h"
#include "net_proto.h"
#include "network.h"
#include "options.h"
#include "server.h"
#include "streams.h"
#include "structures.h"
#include "storage.h"
#include "timers.h"
#include "utils.h"

static struct proto proto;
static int eol_length;		/* == strlen(proto.eol_out_string) */

#ifdef EAGAIN
static int eagain = EAGAIN;
#else
static int eagain = -1;
#endif

#ifdef EWOULDBLOCK
static int ewouldblock = EWOULDBLOCK;
#else
static int ewouldblock = -1;
#endif

static int *pocket_descriptors = 0;	/* fds we keep around in case we need
					 * one and no others are left... */

typedef struct text_block {
    struct text_block *next;
    int length;
    char *buffer;
    char *start;
} text_block;

typedef struct nhandle {
    struct nhandle *next, **prev;
    server_handle shandle;
    int rfd, wfd;
    char *name;
    Stream *input;
    int last_input_was_CR;
    int input_suspended;
    text_block *output_head;
    text_block **output_tail;
    int output_length;
    int output_lines_flushed;
    int outbound, binary;
#if NETWORK_PROTOCOL == NP_TCP
    int client_echo;
#endif
} nhandle;

static nhandle *all_nhandles = 0;

typedef struct nlistener {
    struct nlistener *next, **prev;
    server_listener slistener;
    int fd;
    const char *name;
} nlistener;

static nlistener *all_nlisteners = 0;


typedef struct {
    int fd;
    network_fd_callback readable;
    network_fd_callback writable;
    void *data;
} fd_reg;

static fd_reg *reg_fds = 0;
static int max_reg_fds = 0;

void
network_register_fd(int fd, network_fd_callback readable,
		    network_fd_callback writable, void *data)
{
    int i;

    if (!reg_fds) {
	max_reg_fds = 5;
	reg_fds = (fd_reg *)mymalloc(max_reg_fds * sizeof(fd_reg), M_NETWORK);
	for (i = 0; i < max_reg_fds; i++)
	    reg_fds[i].fd = -1;
    }
    /* Find an empty slot */
    for (i = 0; i < max_reg_fds; i++)
	if (reg_fds[i].fd == -1)
	    break;
    if (i >= max_reg_fds) {	/* No free slots */
	int new_max = 2 * max_reg_fds;
	fd_reg *_new = (fd_reg *)mymalloc(new_max * sizeof(fd_reg), M_NETWORK);

	for (i = 0; i < new_max; i++)
	    if (i < max_reg_fds)
		_new[i] = reg_fds[i];
	    else
		_new[i].fd = -1;

	myfree(reg_fds, M_NETWORK);
	i = max_reg_fds;	/* first free slot */
	max_reg_fds = new_max;
	reg_fds = _new;
    }
    reg_fds[i].fd = fd;
    reg_fds[i].readable = readable;
    reg_fds[i].writable = writable;
    reg_fds[i].data = data;
}

void
network_unregister_fd(int fd)
{
    int i;

    for (i = 0; i < max_reg_fds; i++)
	if (reg_fds[i].fd == fd)
	    reg_fds[i].fd = -1;
}

static void
add_registered_fds(void)
{
    fd_reg *reg;

    for (reg = reg_fds; reg < reg_fds + max_reg_fds; reg++)
	if (reg->fd != -1) {
	    if (reg->readable)
		mplex_add_reader(reg->fd);
	    if (reg->writable)
		mplex_add_writer(reg->fd);
	}
}

static void
check_registered_fds(void)
{
    fd_reg *reg;

    for (reg = reg_fds; reg < reg_fds + max_reg_fds; reg++)
	if (reg->fd != -1) {
	    if (reg->readable && mplex_is_readable(reg->fd))
		(*reg->readable) (reg->fd, reg->data);
	    if (reg->writable && mplex_is_writable(reg->fd))
		(*reg->writable) (reg->fd, reg->data);
	}
}


static void
free_text_block(text_block * b)
{
    myfree(b->buffer, M_NETWORK);
    myfree(b, M_NETWORK);
}

int
network_set_nonblocking(int fd)
{
#ifdef FIONBIO
    /* Prefer this implementation, since the second one fails on some SysV
     * platforms, including HP/UX.
     */
    int yes = 1;

    if (ioctl(fd, FIONBIO, &yes) < 0)
	return 0;
    else
	return 1;
#else
    int flags;

    if ((flags = fcntl(fd, F_GETFL, 0)) < 0
	|| fcntl(fd, F_SETFL, flags | NONBLOCK_FLAG) < 0)
	return 0;
    else
	return 1;
#endif
}

static int
push_output(nhandle * h)
{
    text_block *b;
    int count;

    if (h->output_lines_flushed > 0) {
	char buf[100];
	int length;

	sprintf(buf,
		"%s>> Network buffer overflow: %u line%s of output to you %s been lost <<%s",
		proto.eol_out_string,
		h->output_lines_flushed,
		h->output_lines_flushed == 1 ? "" : "s",
		h->output_lines_flushed == 1 ? "has" : "have",
		proto.eol_out_string);
	length = strlen(buf);
	count = write(h->wfd, buf, length);
	if (count == length)
	    h->output_lines_flushed = 0;
	else
	    return count >= 0 || errno == eagain || errno == ewouldblock;
    }
    while ((b = h->output_head) != 0) {
	count = write(h->wfd, b->start, b->length);
	if (count < 0)
	    return (errno == eagain || errno == ewouldblock);
	h->output_length -= count;
	if (count == b->length) {
	    h->output_head = b->next;
	    free_text_block(b);
	} else {
	    b->start += count;
	    b->length -= count;
	}
    }
    if (h->output_head == 0)
	h->output_tail = &(h->output_head);
    return 1;
}

    static int
pull_input(nhandle * h)
{
#define TN_IAC  255
#define TN_DO   253
#define TN_DONT 254
#define TN_WILL 251
#define TN_WONT 252
#define TN_SE   240

    Stream *s = h->input;

        if (stream_length(s) >= MAX_LINE_BYTES) {
        errlog("Connection `%s` closed for exceeding MAX_LINE_BYTES! (%" PRIdN" /%" PRIdN")\n", h->name, stream_length(s), MAX_LINE_BYTES);
        return 0;
    }

    int count;
    char buffer[1024];
    char *ptr, *end;

    if ((count = read(h->rfd, buffer, sizeof(buffer))) > 0) {
        if (h->binary) {
            stream_add_raw_bytes_to_binary(s, buffer, count);
            server_receive_line(h->shandle, reset_stream(s), false);
            h->last_input_was_CR = 0;
        } else {
            Stream *oob = new_stream(3);
            for (ptr = buffer, end = buffer + count; ptr < end; ptr++) {
                unsigned char c = *ptr;

                if (isgraph(c) || c == ' ' || c == '\t')
                    stream_add_char(s, c);
#ifdef INPUT_APPLY_BACKSPACE
                else if (c == 0x08 || c == 0x7F)
                    stream_delete_char(s);
#endif
                else if (c == TN_IAC && ptr + 2 <= end) {
                    // Pluck a telnet IAC sequence out of the middle of the input
                    int telnet_counter = 1;
                    unsigned char cmd = *(ptr + telnet_counter);
                    if (cmd == TN_WILL || cmd == TN_WONT || cmd == TN_DO || cmd == TN_DONT) {
                        stream_add_raw_bytes_to_binary(oob, ptr, 3);
                        ptr += 2;
                    } else {
                        while (cmd != TN_SE && ptr + telnet_counter <= end)
                            cmd = *(ptr + telnet_counter++);

                        if (cmd == TN_SE) {
                            // We got a complete option sequence.
                            stream_add_raw_bytes_to_binary(oob, ptr, telnet_counter);
                            ptr += --telnet_counter;
                        } else {
                            /* We couldn't find the end of the option sequence, so, unfortunately,
                             * we just consider this IAC wasted. The rest of the out of band commands
                             * will get passed to do_out_of_band_command as gibberish. */
                        }
                    }
                }

                if ((c == '\r' || (c == '\n' && !h->last_input_was_CR)))
                    server_receive_line(h->shandle, reset_stream(s), 0);

                h->last_input_was_CR = (c == '\r');
            }

            if (stream_length(oob) > 0)
                server_receive_line(h->shandle, reset_stream(oob), 1);
            else
                free_stream(oob);
        }
        return 1;
    } else
        return (count == 0 && !proto.believe_eof)
            || (count < 0 && (errno == eagain || errno == ewouldblock));
}

static nhandle *
new_nhandle(int rfd, int wfd, const char *local_name, const char *remote_name,
	    int outbound)
{
    nhandle *h;
    static Stream *s = 0;

    if (s == 0)
	s = new_stream(100);

    if (!network_set_nonblocking(rfd)
	|| (rfd != wfd && !network_set_nonblocking(wfd)))
	log_perror("Setting connection non-blocking");

    h = (nhandle *)mymalloc(sizeof(nhandle), M_NETWORK);

    if (all_nhandles)
	all_nhandles->prev = &(h->next);
    h->next = all_nhandles;
    h->prev = &all_nhandles;
    all_nhandles = h;

    h->rfd = rfd;
    h->wfd = wfd;
    h->input = new_stream(100);
    h->last_input_was_CR = 0;
    h->input_suspended = 0;
    h->output_head = 0;
    h->output_tail = &(h->output_head);
    h->output_length = 0;
    h->output_lines_flushed = 0;
    h->outbound = outbound;
    h->binary = 0;
#if NETWORK_PROTOCOL == NP_TCP
    h->client_echo = 1;
#endif

    stream_printf(s, "%s %s %s",
		  local_name, outbound ? "to" : "from", remote_name);
    h->name = str_dup(reset_stream(s));

    return h;
}

static void
close_nhandle(nhandle * h)
{
    text_block *b, *bb;

    (void) push_output(h);
    *(h->prev) = h->next;
    if (h->next)
	h->next->prev = h->prev;
    b = h->output_head;
    while (b) {
	bb = b->next;
	free_text_block(b);
	b = bb;
    }
    free_stream(h->input);
    proto_close_connection(h->rfd, h->wfd);
    free_str(h->name);
    myfree(h, M_NETWORK);
}

static void
close_nlistener(nlistener * l)
{
    *(l->prev) = l->next;
    if (l->next)
	l->next->prev = l->prev;
    proto_close_listener(l->fd);
    free_str(l->name);
    myfree(l, M_NETWORK);
}

static void
make_new_connection(server_listener sl, int rfd, int wfd,
		    const char *local_name, const char *remote_name,
		    int outbound)
{
    nhandle *h;
    network_handle nh;

    nh.ptr = h = new_nhandle(rfd, wfd, local_name, remote_name, outbound);
    h->shandle = server_new_connection(sl, nh, outbound);
}

static void
get_pocket_descriptors()
{
    unsigned int i;

    if (!pocket_descriptors)
	pocket_descriptors =
	    (int *) mymalloc(proto.pocket_size * sizeof(int), M_NETWORK);

    for (i = 0; i < proto.pocket_size; i++) {
	pocket_descriptors[i] = dup(0);
	if (!pocket_descriptors[i]) {
	    log_perror("Can't get a pocket descriptor");
	    panic_moo("Need pocket descriptors to continue");
	}
    }
}

static void
accept_new_connection(nlistener * l)
{
    network_handle nh;
    nhandle *h;
    int rfd, wfd, i;
    const char *host_name;

    switch (proto_accept_connection(l->fd, &rfd, &wfd, &host_name)) {
    case PA_OKAY:
	make_new_connection(l->slistener, rfd, wfd, l->name, host_name, 0);
	break;

    case PA_FULL:
	for (i = 0; i < proto.pocket_size; i++)
	    close(pocket_descriptors[i]);
	if (proto_accept_connection(l->fd, &rfd, &wfd, &host_name) != PA_OKAY)
	    errlog("Can't accept connection even by emptying pockets!\n");
	else {
	    nh.ptr = h = new_nhandle(rfd, wfd, l->name, host_name, 0);
	    server_refuse_connection(l->slistener, nh);
	    close_nhandle(h);
	}
	get_pocket_descriptors();
	break;

    case PA_OTHER:
	/* Do nothing.  The protocol implementation has already logged it. */
	break;
    }
}

static int
enqueue_output(network_handle nh, const char *line, int line_length,
	       int add_eol, int flush_ok)
{
    nhandle *h = (nhandle *)nh.ptr;
    int length = line_length + (add_eol ? eol_length : 0);
    char *buffer;
    text_block *block;

    if (h->output_length != 0
	&& h->output_length + length > MAX_QUEUED_OUTPUT) {	/* must flush... */
	int to_flush;
	text_block *b;

	(void) push_output(h);
	to_flush = h->output_length + length - MAX_QUEUED_OUTPUT;
	if (to_flush > 0 && !flush_ok)
	    return 0;
	while (to_flush > 0 && (b = h->output_head)) {
	    h->output_length -= b->length;
	    to_flush -= b->length;
	    h->output_lines_flushed++;
	    h->output_head = b->next;
	    free_text_block(b);
	}
	if (h->output_head == 0)
	    h->output_tail = &(h->output_head);
    }
    buffer = (char *) mymalloc(length * sizeof(char), M_NETWORK);
    block = (text_block *) mymalloc(sizeof(text_block), M_NETWORK);
    memcpy(buffer, line, line_length);
    if (add_eol)
	memcpy(buffer + line_length, proto.eol_out_string, eol_length);
    block->buffer = block->start = buffer;
    block->length = length;
    block->next = 0;
    *(h->output_tail) = block;
    h->output_tail = &(block->next);
    h->output_length += length;

    return 1;
}


/*************************
 * External entry points *
 *************************/

const char *
network_protocol_name(void)
{
    return proto_name();
}

const char *
network_usage_string(void)
{
    return proto_usage_string();
}

int
network_initialize(int argc, char **argv, Var * desc)
{
    if (!proto_initialize(&proto, desc, argc, argv))
	return 0;

    eol_length = strlen(proto.eol_out_string);
    get_pocket_descriptors();

    /* we don't care about SIGPIPE, we notice it in mplex_wait() and write() */
    signal(SIGPIPE, SIG_IGN);

    return 1;
}

enum error
network_make_listener(server_listener sl, Var desc,
		   network_listener * nl, Var * canon, const char **name)
{
    int fd;
    enum error e = proto_make_listener(desc, &fd, canon, name);
    nlistener *l;

    if (e == E_NONE) {
	nl->ptr = l = (nlistener *)mymalloc(sizeof(nlistener), M_NETWORK);
	l->fd = fd;
	l->slistener = sl;
	l->name = str_dup(*name);
	if (all_nlisteners)
	    all_nlisteners->prev = &(l->next);
	l->next = all_nlisteners;
	l->prev = &all_nlisteners;
	all_nlisteners = l;
    }
    return e;
}

int
network_listen(network_listener nl)
{
    nlistener *l = (nlistener *)nl.ptr;

    return proto_listen(l->fd);
}

int
network_send_line(network_handle nh, const char *line, int flush_ok, bool send_newline)
{
    return enqueue_output(nh, line, strlen(line), send_newline, flush_ok);
}

int
network_send_bytes(network_handle nh, const char *buffer, int buflen,
		   int flush_ok)
{
    return enqueue_output(nh, buffer, buflen, 0, flush_ok);
}

int
network_buffered_output_length(network_handle nh)
{
    nhandle *h = (nhandle *)nh.ptr;

    return h->output_length;
}

void
network_suspend_input(network_handle nh)
{
    nhandle *h = (nhandle *)nh.ptr;

    h->input_suspended = 1;
}

void
network_resume_input(network_handle nh)
{
    nhandle *h = (nhandle *)nh.ptr;

    h->input_suspended = 0;
}

int
network_process_io(int timeout)
{
    nhandle *h, *hnext;
    nlistener *l;

    mplex_clear();
    for (l = all_nlisteners; l; l = l->next)
	mplex_add_reader(l->fd);
    for (h = all_nhandles; h; h = h->next) {
	if (!h->input_suspended)
	    mplex_add_reader(h->rfd);
	if (h->output_head)
	    mplex_add_writer(h->wfd);
    }
    add_registered_fds();

    if (mplex_wait(timeout))
	return 0;
    else {
	for (l = all_nlisteners; l; l = l->next)
	    if (mplex_is_readable(l->fd))
		accept_new_connection(l);
	for (h = all_nhandles; h; h = hnext) {
	    hnext = h->next;
	    if ((mplex_is_readable(h->rfd) && !pull_input(h))
		|| (mplex_is_writable(h->wfd) && !push_output(h))) {
		server_close(h->shandle);
		close_nhandle(h);
	    }
	}
	check_registered_fds();
	return 1;
    }
}

extern void
rewrite_connection_name(network_handle nh, Stream *new_connection_name)
{
    nhandle *h = (nhandle *) nh.ptr;

    char *oldname = h->name;
    h->name = str_dup(reset_stream(new_connection_name));
    applog(LOG_INFO3, "PROXY: connection_name changed from `%s` to `%s`\n", oldname, h->name);
    free_str(oldname);
}

const char *
network_connection_name(network_handle nh)
{
    nhandle *h = (nhandle *) nh.ptr;

    return h->name;
}

void
network_set_connection_binary(network_handle nh, int do_binary)
{
    nhandle *h = (nhandle *)nh.ptr;

    h->binary = do_binary;
}

#if NETWORK_PROTOCOL == NP_LOCAL
#  define NETWORK_CO_TABLE(DEFINE, nh, value, _)
       /* No network-specific connection options */

#elif NETWORK_PROTOCOL == NP_TCP
#  define NETWORK_CO_TABLE(DEFINE, nh, value, _)		\
       DEFINE(client-echo, _, TYPE_INT, num,			\
	      ((nhandle *)nh.ptr)->client_echo,			\
	      network_set_client_echo(nh, is_true(value));)	\

void
network_set_client_echo(network_handle nh, int is_on)
{
    nhandle *h = (nhandle *)nh.ptr;

    /* These values taken from RFC 854 and RFC 857. */
#define TN_IAC	255		/* Interpret As Command */
#define TN_WILL	251
#define TN_WONT	252
#define TN_ECHO	1

    static char telnet_cmd[4] =
	{(char)TN_IAC, (char)0, (char)TN_ECHO, (char)0};

    h->client_echo = is_on;
    if (is_on)
	telnet_cmd[1] = (char)TN_WONT;
    else
	telnet_cmd[1] = (char)TN_WILL;
    enqueue_output(nh, telnet_cmd, 3, 0, 1);
}

#else /* NETWORK_PROTOCOL == NP_SINGLE */

#  error "NP_SINGLE ???"

#endif /* NETWORK_PROTOCOL */


#ifdef OUTBOUND_NETWORK

enum error
network_open_connection(Var arglist, server_listener sl)
{
    int rfd, wfd;
    const char *local_name, *remote_name;
    enum error e;

    e = proto_open_connection(arglist, &rfd, &wfd, &local_name, &remote_name);
    if (e == E_NONE)
	make_new_connection(sl, rfd, wfd,
			    local_name, remote_name, 1);

    return e;
}
#endif

void
network_close(network_handle h)
{
    close_nhandle((nhandle *)h.ptr);
}

void
network_close_listener(network_listener nl)
{
    close_nlistener((nlistener *)nl.ptr);
}

void
network_shutdown(void)
{
    while (all_nhandles)
	close_nhandle(all_nhandles);
    while (all_nlisteners)
	close_nlistener(all_nlisteners);
}