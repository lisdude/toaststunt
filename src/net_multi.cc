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

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>

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

static int *pocket_descriptors = nullptr;	/* fds we keep around in case we need
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
	const char *name;						// resolved hostname (or IP if DNS is disabled)
	Stream *input;
	bool last_input_was_CR;
	bool input_suspended;
	text_block *output_head;
	text_block **output_tail;
	int output_length;
	int output_lines_flushed;
	int outbound, binary;
#if NETWORK_PROTOCOL == NP_TCP
	bool client_echo;
	uint16_t source_port;          		// port on server
	const char *source_address;         // interface on server (resolved hostname)
	const char *source_ipaddr;			// interface on server (IP address)
	uint16_t destination_port;     		// local port on connectee
	const char *destination_ipaddr;     // IP address of connection
	sa_family_t protocol_family;    	// AF_INET, AF_INET6
	pthread_mutex_t *name_mutex;
	unsigned int refcount;
#endif
} nhandle;

static nhandle *all_nhandles = nullptr;

typedef struct nlistener {
	struct nlistener *next, **prev;
	server_listener slistener;
	int fd;
	const char *name;				// resolved hostname
	const char *ip_addr;			// 'raw' IP address
	uint16_t port;					// listening port
} nlistener;

static nlistener *all_nlisteners = nullptr;

typedef struct {
	int fd;
	network_fd_callback readable;
	network_fd_callback writable;
	void *data;
} fd_reg;

static fd_reg *reg_fds = nullptr;
static int max_reg_fds = 0;

extern struct addrinfo tcp_hint;

void
network_register_fd(int fd, network_fd_callback readable, network_fd_callback writable, void *data)
{
	int i;

	if (!reg_fds) {
		max_reg_fds = 5;
		reg_fds = (fd_reg *) mymalloc(max_reg_fds * sizeof(fd_reg), M_NETWORK);
		for (i = 0; i < max_reg_fds; i++)
			reg_fds[i].fd = -1;
	}
	/* Find an empty slot */
	for (i = 0; i < max_reg_fds; i++)
		if (reg_fds[i].fd == -1)
			break;
	if (i >= max_reg_fds) {	/* No free slots */
		int new_max = 2 * max_reg_fds;
		fd_reg *_new = (fd_reg *) mymalloc(new_max * sizeof(fd_reg), M_NETWORK);

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

#ifndef HAVE_ACCEPT4
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

	if ((flags = fcntl(fd, F_GETFL, 0)) < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
		return 0;
	else
		return 1;
#endif
}
#endif

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
			h->output_lines_flushed == 1 ? "has" : "have", proto.eol_out_string);
		length = strlen(buf);
		count = write(h->wfd, buf, length);
		if (count == length)
			h->output_lines_flushed = 0;
		else
			return count >= 0 || errno == eagain || errno == ewouldblock;
	}
	while ((b = h->output_head) != nullptr) {
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
	if (h->output_head == nullptr)
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
		errlog("Connection `%s` closed for exceeding MAX_LINE_BYTES! (%" PRIdN " /%" PRIdN ")\n", h->name,
		       stream_length(s), MAX_LINE_BYTES);
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

			free_stream(oob);
		}
		return 1;
	} else
		return (count == 0 && !proto.believe_eof)
			|| (count < 0 && (errno == eagain || errno == ewouldblock));
}

static nhandle *
new_nhandle(const int rfd, const int wfd, const int outbound, uint16_t listen_port, const char *listen_hostname,
            const char *listen_ipaddr, uint16_t local_port, const char *local_hostname,
			const char *local_ipaddr, sa_family_t protocol)
{
	nhandle *h;

#ifndef HAVE_ACCEPT4
	if (!network_set_nonblocking(rfd)
	    || (rfd != wfd && !network_set_nonblocking(wfd)))
		log_perror("Setting connection non-blocking");
#endif

	h = (nhandle *) mymalloc(sizeof(nhandle), M_NETWORK);

	if (all_nhandles)
		all_nhandles->prev = &(h->next);
	h->next = all_nhandles;
	h->prev = &all_nhandles;
	all_nhandles = h;

	h->rfd = rfd;
	h->wfd = wfd;
	h->input = new_stream(100);
	h->last_input_was_CR = false;
	h->input_suspended = false;
	h->output_head = nullptr;
	h->output_tail = &(h->output_head);
	h->output_length = 0;
	h->output_lines_flushed = 0;
	h->outbound = outbound;
	h->binary = 0;
	h->name = local_hostname;	// already malloced by a get_network* function
#if NETWORK_PROTOCOL == NP_TCP
	h->client_echo = 1;
	h->source_port = listen_port;
	h->source_address = str_dup(listen_hostname);
	h->source_ipaddr = str_dup(listen_ipaddr);
	h->destination_port = local_port;
	h->destination_ipaddr = local_ipaddr;  // malloced by a get_network* function
	h->protocol_family = protocol;
	h->name_mutex = (pthread_mutex_t *) malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(h->name_mutex, nullptr);
	h->refcount = 1;
#endif

	return h;
}

static void
close_nhandle(nhandle * h)
{
	text_block *b, *bb;

	(void)push_output(h);
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
#if NETWORK_PROTOCOL == NP_TCP
	free_str(h->source_address);
	free_str(h->source_ipaddr);
	free_str(h->destination_ipaddr);
    pthread_mutex_destroy(h->name_mutex);
	free(h->name_mutex);
#endif
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
	free_str(l->ip_addr);
	myfree(l, M_NETWORK);
}

static void
make_new_connection(server_listener sl, int rfd, int wfd, int outbound,
					uint16_t listen_port, const char *listen_hostname,
					const char *listen_ipaddr, uint16_t local_port,
					const char *local_hostname, const char *local_ipaddr,
					sa_family_t protocol)
{
	nhandle *h;
	network_handle nh;

	nh.ptr = h = new_nhandle(rfd, wfd, outbound, listen_port, listen_hostname, 
							listen_ipaddr, local_port, local_hostname, local_ipaddr, protocol);
	h->shandle = server_new_connection(sl, nh, outbound);
}

static void
get_pocket_descriptors()
{
	unsigned int i;

	if (!pocket_descriptors)
		pocket_descriptors = (int *)mymalloc(proto.pocket_size * sizeof(int), M_NETWORK);

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
	int rfd, wfd;
	unsigned int i;
	const char *name;
	const char *ip_addr;
	uint16_t port;
	sa_family_t protocol;

	switch (proto_accept_connection(l->fd, &rfd, &wfd, &name, &ip_addr, &port, &protocol)) {
	    case PA_OKAY:
		    make_new_connection(l->slistener, rfd, wfd, 0, l->port, l->name, l->ip_addr, port, name, ip_addr, protocol);
		    break;
	    case PA_FULL:
		    for (i = 0; i < proto.pocket_size; i++)
			    close(pocket_descriptors[i]);
		    if (proto_accept_connection(l->fd, &rfd, &wfd, &name, &ip_addr, &port, &protocol) != PA_OKAY) {
			    errlog("Can't accept connection even by emptying pockets!\n");
		    } else {
			    nh.ptr = h = new_nhandle(rfd, wfd, 0, l->port, l->name, l->ip_addr, port, name, ip_addr, protocol);
			    server_refuse_connection(l->slistener, nh);
				decrement_nhandle_refcount(nh);
		    }
		    get_pocket_descriptors();
		    break;
	    case PA_OTHER:
		    /* Do nothing.  The protocol implementation has already logged it. */
		    break;
	}
}

static int
enqueue_output(network_handle nh, const char *line, int line_length, int add_eol, int flush_ok)
{
	nhandle *h = (nhandle *) nh.ptr;
	int length = line_length + (add_eol ? eol_length : 0);
	char *buffer;
	text_block *block;

	if (h->output_length != 0 && h->output_length + length > MAX_QUEUED_OUTPUT) {	/* must flush... */
		int to_flush;
		text_block *b;

		(void)push_output(h);
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
		if (h->output_head == nullptr)
			h->output_tail = &(h->output_head);
	}
	buffer = (char *)mymalloc(length * sizeof(char), M_NETWORK);
	block = (text_block *) mymalloc(sizeof(text_block), M_NETWORK);
	memcpy(buffer, line, line_length);
	if (add_eol)
		memcpy(buffer + line_length, proto.eol_out_string, eol_length);
	block->buffer = block->start = buffer;
	block->length = length;
	block->next = nullptr;
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
network_make_listener(server_listener sl, Var desc, network_listener * nl,
					  const char **name, const char **ip_address,
					  uint16_t *port, bool use_ipv6)
{
	int fd;
	enum error e = proto_make_listener(desc, &fd, name, ip_address, port, use_ipv6);
	nlistener *listener;

	if (e == E_NONE) {
		nl->ptr = listener = (nlistener *) mymalloc(sizeof(nlistener), M_NETWORK);
		listener->fd = fd;
		listener->slistener = sl;
		listener->name = str_dup(*name);
		listener->ip_addr = str_dup(*ip_address);
		listener->port = *port;
		if (all_nlisteners)
			all_nlisteners->prev = &(listener->next);
		listener->next = all_nlisteners;
		listener->prev = &all_nlisteners;
		all_nlisteners = listener;
	}
	return e;
}

int
network_listen(network_listener nl)
{
	if (!nl.ptr)
		return 0;

	nlistener *l = (nlistener *) nl.ptr;

	return proto_listen(l->fd);
}

int
network_send_line(network_handle nh, const char *line, int flush_ok, bool send_newline)
{
	return enqueue_output(nh, line, strlen(line), send_newline, flush_ok);
}

int
network_send_bytes(network_handle nh, const char *buffer, int buflen, int flush_ok)
{
	return enqueue_output(nh, buffer, buflen, 0, flush_ok);
}

int
network_buffered_output_length(network_handle nh)
{
	nhandle *h = (nhandle *) nh.ptr;

	return h->output_length;
}

void
network_suspend_input(network_handle nh)
{
	nhandle *h = (nhandle *) nh.ptr;

	h->input_suspended = 1;
}

void
network_resume_input(network_handle nh)
{
	nhandle *h = (nhandle *) nh.ptr;

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
			if (((mplex_is_readable(h->rfd) && !pull_input(h))
			    || (mplex_is_writable(h->wfd) && !push_output(h))) && h->refcount == 1) {
				server_close(h->shandle);
				network_handle nh;
				nh.ptr = h;
				decrement_nhandle_refcount(nh);
			}
		}
		check_registered_fds();
		return 1;
	}
}

int
network_is_localhost(const network_handle nh)
{
	const nhandle *h = (nhandle *) nh.ptr;
	const char *ip = h->destination_ipaddr;
	int ret = 0;

	if (strstr(ip, "127.0.0.1") != nullptr || strstr(ip, "::1") != nullptr)
		ret = 1;

	return ret;
}

void
rewrite_connection_name(network_handle nh, const char *destination, const char *destination_ip, const char *source, const char *source_port)
{
    const char *nameinfo;
    struct addrinfo *address;
    getaddrinfo(source, source_port, &tcp_hint, &address);

    const char *ip_addr = get_ntop((struct sockaddr_storage *)address->ai_addr);

    if (!server_int_option("no_name_lookup", NO_NAME_LOOKUP))
        nameinfo = get_nameinfo(address->ai_addr);
    else
        nameinfo = str_dup(ip_addr);

    freeaddrinfo(address);
	
	nhandle *h = (nhandle *) nh.ptr;

	pthread_mutex_lock(h->name_mutex);
	free_str(h->name);
	h->name = nameinfo;
	free_str(h->destination_ipaddr);
	h->destination_ipaddr = ip_addr;
	h->source_port = atoi(source_port);
	pthread_mutex_unlock(h->name_mutex);
}

int
network_name_lookup_rewrite(Objid obj, const char *name)
{
	network_handle *nh;
	if (find_network_handle(obj, &nh) < 0)
		return -1;

	nhandle *h = (nhandle *) nh->ptr;

	pthread_mutex_lock(h->name_mutex);	
	applog(LOG_INFO3, "NAME_LOOKUP: connection_name for #%" PRIdN " changed from `%s` to `%s`\n", obj, h->name, name);
	free_str(h->name);
	h->name = str_dup(name);
	pthread_mutex_unlock(h->name_mutex);	

	return 0;
}

void
lock_connection_name_mutex(const network_handle nh)
{
	const nhandle *h = (nhandle *) nh.ptr;
	pthread_mutex_lock(h->name_mutex);
}

void
unlock_connection_name_mutex(const network_handle nh)
{
	const nhandle *h = (nhandle *) nh.ptr;
	pthread_mutex_unlock(h->name_mutex);
}

void
increment_nhandle_refcount(const network_handle nh)
{
	nhandle *h = (nhandle *)nh.ptr;
	h->refcount++;
}

void
decrement_nhandle_refcount(const network_handle nh)
{
	nhandle *h = (nhandle *)nh.ptr;
	h->refcount--;

	if (h->refcount <= 0)
		close_nhandle(h);
}

int
nhandle_refcount(const network_handle nh)
{
	return ((nhandle*)nh.ptr)->refcount;
}

const char *
network_connection_name(const network_handle nh)
{
	const nhandle *h = (nhandle *) nh.ptr;
	return h->name;
}

int
lookup_network_connection_name(const network_handle nh, const char **name)
{
    const nhandle *h = (nhandle *) nh.ptr;
    int retval = 0;

    pthread_mutex_lock(h->name_mutex);	

    struct addrinfo *address;
    int status = getaddrinfo(h->destination_ipaddr, nullptr, &tcp_hint, &address);
    if (status < 0) {
        // Better luck next time.
        *name = str_dup(h->name);
        retval = -1;
    } else {
        *name = get_nameinfo(address->ai_addr);
    }
    freeaddrinfo(address);
    pthread_mutex_unlock(h->name_mutex);	
    return retval;
}

char *
full_network_connection_name(const network_handle nh, bool legacy)
{
	const nhandle *h = (nhandle *)nh.ptr;
	char *ret = nullptr;

	pthread_mutex_lock(h->name_mutex);	

	if (legacy) {
		asprintf(&ret, "port %i from %s [%s], port %i", h->source_port, h->name, h->destination_ipaddr, h->destination_port);
	} else {
		asprintf(&ret, "%s [%s], port %i from %s [%s], port %i", h->source_address, h->source_ipaddr, h->source_port, h->name, h->destination_ipaddr, h->destination_port);
	}

	pthread_mutex_unlock(h->name_mutex);	

	return ret;
}

const char *
network_ip_address(const network_handle nh)
{
	const nhandle *h = (nhandle *) nh.ptr;

	pthread_mutex_lock(h->name_mutex);	
	const char *ret = h->destination_ipaddr;
	pthread_mutex_unlock(h->name_mutex);	
	return ret;
}

const char *
network_source_connection_name(const network_handle nh)
{
	const nhandle *h = (nhandle *)nh.ptr;

	return h->source_address;
}

const char *
network_source_ip_address(const network_handle nh)
{
	const nhandle *h = (nhandle *)nh.ptr;

	return h->source_ipaddr;
}

uint16_t
network_port(const network_handle nh)
{
	const nhandle *h = (nhandle *)nh.ptr;

	return h->destination_port;
}

uint16_t
network_source_port(const network_handle nh)
{
	const nhandle *h = (nhandle *)nh.ptr;
	pthread_mutex_lock(h->name_mutex);
	uint16_t port = h->source_port;
	pthread_mutex_unlock(h->name_mutex);

	return port;
}

const char *
network_protocol(const network_handle nh)
{
	const nhandle *h = (nhandle *)nh.ptr;
	switch (h->protocol_family)
	{
		case AF_INET:
		return "IPv4";
		case AF_INET6:
		return "IPv6";
		default:
		return "unknown";
	}
}

void
network_set_connection_binary(network_handle nh, int do_binary)
{
	nhandle *h = (nhandle *) nh.ptr;

	h->binary = do_binary;
}

#if NETWORK_PROTOCOL == NP_TCP
#    define NETWORK_CO_TABLE(DEFINE, nh, value, _)		\
       DEFINE(client-echo, _, TYPE_INT, num,			\
	      ((nhandle *)nh.ptr)->client_echo,			\
	      network_set_client_echo(nh, is_true(value));)	\

void
network_set_client_echo(network_handle nh, int is_on)
{
	nhandle *h = (nhandle *) nh.ptr;

	/* These values taken from RFC 854 and RFC 857. */
#    define TN_IAC	255	/* Interpret As Command */
#    define TN_WILL	251
#    define TN_WONT	252
#    define TN_ECHO	1

	static char telnet_cmd[4] = { (char)TN_IAC, (char)0, (char)TN_ECHO, (char)0 };

	h->client_echo = is_on;
	if (is_on)
		telnet_cmd[1] = (char)TN_WONT;
	else
		telnet_cmd[1] = (char)TN_WILL;
	enqueue_output(nh, telnet_cmd, 3, 0, 1);
}

#else /* NETWORK_PROTOCOL == NP_SINGLE */

#    error "NP_SINGLE ???"

#endif /* NETWORK_PROTOCOL */


#ifdef OUTBOUND_NETWORK

enum error
network_open_connection(Var arglist, server_listener sl, bool use_ipv6)
{
	int rfd, wfd;
	const char *name;
	const char *ip_addr;
	uint16_t port;
	sa_family_t protocol;
	enum error e;
	
	e = proto_open_connection(arglist, &rfd, &wfd, &name, &ip_addr, &port, &protocol, use_ipv6);
	if (e == E_NONE)
		make_new_connection(sl, rfd, wfd, 1, 0, nullptr, nullptr, port, name, ip_addr, protocol);

	return e;
}
#endif

void
network_close(network_handle h)
{
	decrement_nhandle_refcount(h);
}

void
network_close_listener(network_listener nl)
{
	close_nlistener((nlistener *) nl.ptr);
}

void
network_shutdown(void)
{
	while (all_nhandles)
		close_nhandle(all_nhandles);
	while (all_nlisteners)
		close_nlistener(all_nlisteners);
}
