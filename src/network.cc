/******************************************************************************
  Copyright (c) 1992 Xerox Corporation.  All rights reserved.
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
#include <fcntl.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <stdio.h>
#include <pthread.h>
#include <arpa/inet.h>      /* inet_addr() */
#include <errno.h>          /* EMFILE, EADDRNOTAVAIL, ECONNREFUSED,
                             * ENETUNREACH, ETIMEOUT */
#include <netinet/in.h>     /* struct sockaddr_in, INADDR_ANY, htons(),
                             * htonl(), ntohl(), struct in_addr */
#include <sys/socket.h>     /* socket(), AF_INET, SOCK_STREAM,
                             * setsockopt(), SOL_SOCKET, SO_REUSEADDR,
                             * bind(), struct sockaddr, accept(),
                             * connect() */
#include <stdlib.h>         /* strtoul() */
#include <string.h>         /* memcpy() */
#include <unistd.h>         /* close() */
#include <netinet/tcp.h>
#include <atomic>
#include <vector>

#include "options.h"
#include "config.h"
#include "list.h"
#include "log.h"
#include "net_mplex.h"
#include "network.h"
#include "server.h"
#include "storage.h"
#include "timers.h"
#include "utils.h"
#include "map.h"

static struct proto proto;
static int eol_length;      /* == strlen(proto.eol_out_string) */

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

static int *pocket_descriptors = nullptr;   /* fds we keep around in case we need
                                             * one and no others are left... */

#ifdef USE_TLS
SSL_CTX *tls_ctx;
#endif

typedef struct text_block {
    struct text_block *next;
    char *buffer;
    char *start;
    int length;
} text_block;

typedef struct nhandle {
    struct nhandle *next, **prev;
    server_handle shandle;
    const char *name;                       // resolved hostname (or IP if DNS is disabled)
    Stream *input;
    text_block *output_head;
    text_block **output_tail;
    const char *source_address;             // interface on server (resolved hostname)
    const char *source_ipaddr;              // interface on server (IP address)
    const char *destination_ipaddr;         // IP address of connection
    pthread_mutex_t *name_mutex;
    std::atomic<uint32_t> refcount;
    int rfd, wfd;
    int output_length;
    int output_lines_flushed;
    uint16_t source_port;                   // port on server
    uint16_t destination_port;              // local port on connectee
    uint16_t keep_alive_idle;
    uint16_t keep_alive_interval;
    uint8_t keep_alive_count;
    sa_family_t protocol_family;            // AF_INET, AF_INET6
    bool last_input_was_CR;
    bool input_suspended;
    bool outbound, binary;
    bool client_echo;
    bool keep_alive;
#ifdef USE_TLS
    SSL *tls;                               // TLS context; not TLS if null
    bool connected;
    bool want_write;
#endif
} nhandle;

static nhandle *all_nhandles = nullptr;

typedef struct nlistener {
    struct nlistener *next, **prev;
    server_listener slistener;
    const char *name;                       // resolved hostname
    const char *ip_addr;                    // 'raw' IP address
#ifdef USE_TLS
    const char *tls_certificate_path;
    const char *tls_key_path;
#endif
    int fd;
    uint16_t port;                          // listening port
#ifdef USE_TLS
    bool use_tls;
#endif
} nlistener;

static nlistener *all_nlisteners = nullptr;

typedef struct {
    void *data;
    network_fd_callback readable;
    network_fd_callback writable;
    int fd;
} fd_reg;

static fd_reg *reg_fds = nullptr;
static int max_reg_fds = 0;

struct addrinfo tcp_hint;

static const char *get_ntop(const struct sockaddr_storage *sa);
static const char *get_nameinfo(const struct sockaddr *sa);
static unsigned short int get_in_port(const struct sockaddr_storage *sa);
static char *get_port_str(int port);

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
    if (i >= max_reg_fds) { /* No free slots */
        int new_max = 2 * max_reg_fds;
        fd_reg *_new = (fd_reg *) mymalloc(new_max * sizeof(fd_reg), M_NETWORK);

        for (i = 0; i < new_max; i++)
            if (i < max_reg_fds)
                _new[i] = reg_fds[i];
            else
                _new[i].fd = -1;

        myfree(reg_fds, M_NETWORK);
        i = max_reg_fds;    /* first free slot */
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
#else /* FIONBIO */
    int flags;

    if ((flags = fcntl(fd, F_GETFL, 0)) < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        return 0;
    else
        return 1;
#endif /* FIONBIO */
}

static int
push_network_buffer_overflow(nhandle *h)
{
    int count;
    char buf[100];
    int length;

    sprintf(buf,
            "%s>> Network buffer overflow: %i line%s of output to you %s been lost <<%s",
            proto.eol_out_string,
            h->output_lines_flushed,
            h->output_lines_flushed == 1 ? "" : "s",
            h->output_lines_flushed == 1 ? "has" : "have", proto.eol_out_string);
    length = strlen(buf);

#ifdef USE_TLS
    if (h->tls)
        count = SSL_write(h->tls, buf, length);
    else
#endif
        count = write(h->wfd, buf, length);

    if (count == length) {
        h->output_lines_flushed = 0;
        return 1;
    }

#ifdef USE_TLS
    if (h->tls)
    {
        int error = SSL_get_error(h->tls, count);
        if (error == SSL_ERROR_WANT_WRITE || error == SSL_ERROR_WANT_READ || errno == eagain || errno == ewouldblock)
            h->want_write = true;
        else {
            pthread_mutex_lock(h->name_mutex);
            errlog("TLS: Error pushing output (error %i) (errno %i) from %s: %s\n", error, errno, h->name, ERR_error_string(ERR_get_error(), nullptr));
            pthread_mutex_unlock(h->name_mutex);
        }

        ERR_clear_error();
        return count > 0 || error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE || errno == eagain || errno == ewouldblock;
    }
#endif

    return count >= 0 || errno == eagain || errno == ewouldblock;
}

static int
push_output(nhandle * h)
{
#ifdef USE_TLS
    if (h->tls && !h->connected)
        return 1;
#endif

    text_block *b;
    int count;

    if (h->output_lines_flushed > 0)
#ifdef USE_TLS
        /* If this is a TLS connection, we want to skip printing the overflow message for now.
           This is because SSL_write() demands the same data as before when an SSL_ERROR_WANT_WRITE occurs.
           So before we can print the overflow, we have to resend the old data. */
        if (!h->tls)
#endif
            if (!push_network_buffer_overflow(h))
                return 0;

    while ((b = h->output_head) != nullptr) {
#ifdef USE_TLS
        if (h->tls)
            count = SSL_write(h->tls, b->start, b->length);
        else
#endif
            count = write(h->wfd, b->start, b->length);
#ifdef USE_TLS
        if (count <= 0) {
            if (h->tls) {
                int error = SSL_get_error(h->tls, count);
                if (error == SSL_ERROR_WANT_WRITE || error == SSL_ERROR_WANT_READ || errno == eagain || errno == ewouldblock)
                    h->want_write = true;
                else {
                    pthread_mutex_lock(h->name_mutex);
                    errlog("TLS: Error pushing output (error %i) (errno %i) from %s: %s\n", error, errno, h->name, ERR_error_string(ERR_get_error(), nullptr));
                    pthread_mutex_unlock(h->name_mutex);
                }
                ERR_clear_error();
                return (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE || errno == eagain || errno == ewouldblock);
            }
#else
        if (count < 0) {
#endif
            return (errno == eagain || errno == ewouldblock);
        } // end of count checks
        h->output_length -= count;
        if (count == b->length) {
            h->output_head = b->next;
            free_text_block(b);
        } else {
            b->start += count;
            b->length -= count;
        }

#ifdef USE_TLS
        if (h->want_write) {
            h->want_write = false;
            if (h->output_lines_flushed > 0 && !push_network_buffer_overflow(h))
                break;
        }
#endif
    } // endwhile

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

#ifdef USE_TLS
    if (h->tls) {
        int error = 0;
        if (!h->connected) {
            int tls_success = SSL_accept(h->tls);
            error = SSL_get_error(h->tls, tls_success);
            ERR_clear_error();
            switch (error) {
                case SSL_ERROR_WANT_READ:
                case SSL_ERROR_WANT_WRITE:
                    return 1;
                    break;
                case SSL_ERROR_SYSCALL:
                    return 0;
                    break;
                case SSL_ERROR_NONE:
                    h->connected = true;
                    break;
                default: {
                    pthread_mutex_lock(h->name_mutex);
                    errlog("TLS: Accept failed (%i) from %s: %s\n", error, h->name, ERR_error_string(ERR_get_error(), nullptr));
                    pthread_mutex_unlock(h->name_mutex);
                    return 0;
                }
            }
#ifdef LOG_TLS_CONNECTIONS
            pthread_mutex_lock(h->name_mutex);
            oklog("TLS: %s for %s. Cipher: %s\n", SSL_state_string_long(h->tls), h->name, SSL_get_cipher(h->tls));
            pthread_mutex_unlock(h->name_mutex);
#endif
            return 1;
        } else {
            count = SSL_read(h->tls, buffer, sizeof(buffer));

            if (count <= 0) {
                error = SSL_get_error(h->tls, count);
                ERR_clear_error();
                switch (error) {
                    case SSL_ERROR_WANT_READ:
                    case SSL_ERROR_WANT_WRITE:
                    case SSL_ERROR_SSL:
                        return 1;
                        break;
                    case SSL_ERROR_SYSCALL:
                    case SSL_ERROR_ZERO_RETURN:
                        return 0;
                        break;
                    default: {
                        pthread_mutex_lock(h->name_mutex);
                        errlog("TLS: Error pulling input (%i) from %s: %s\n", error, h->name, ERR_error_string(ERR_get_error(), nullptr));
                        pthread_mutex_unlock(h->name_mutex);
                        return 0;
                    }
                }
            }
        }
    } else
#endif
        count = read(h->rfd, buffer, sizeof(buffer));

    if (count > 0) {
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
    } else {
        return (count == 0 && !proto.believe_eof)
               || (count < 0 && (errno == eagain || errno == ewouldblock));
    }
}

static nhandle *
new_nhandle(const int rfd, const int wfd, const bool outbound, uint16_t listen_port, const char *listen_hostname,
            const char *listen_ipaddr, uint16_t local_port, const char *local_hostname,
            const char *local_ipaddr, sa_family_t protocol SSL_CONTEXT_1_DEF)
{
    nhandle *h;

#ifdef HAVE_ACCEPT4
    if (outbound) {
#else
    {
#endif
        if (!network_set_nonblocking(rfd)
                || (rfd != wfd && !network_set_nonblocking(wfd)))
            log_perror("Setting connection non-blocking");
    }

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
    h->binary = false;
    h->name = local_hostname;   // already malloced by a get_network* function
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
    h->keep_alive = KEEP_ALIVE_DEFAULT;
    h->keep_alive_count = KEEP_ALIVE_COUNT;
    h->keep_alive_idle = KEEP_ALIVE_IDLE;
    h->keep_alive_interval = KEEP_ALIVE_INTERVAL;
#ifdef USE_TLS
    h->tls = tls;
    h->connected = false;
    h->want_write = false;
#endif

    if (h->keep_alive) {
        network_handle nh;
        nh.ptr = h;
        network_set_client_keep_alive(nh, Var::new_int(1));
    }

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
    network_close_connection(h->rfd, h->wfd);
    free_str(h->name);
    free_str(h->source_address);
    free_str(h->source_ipaddr);
    free_str(h->destination_ipaddr);
    pthread_mutex_destroy(h->name_mutex);
    free(h->name_mutex);
#ifdef USE_TLS
    if (h->tls) {
        SSL_shutdown(h->tls);
        SSL_free(h->tls);
    }
#endif
    myfree(h, M_NETWORK);
}

static void
close_nlistener(nlistener * l)
{
    *(l->prev) = l->next;
    if (l->next)
        l->next->prev = l->prev;
    close_listener(l->fd);
    free_str(l->name);
    free_str(l->ip_addr);
#ifdef USE_TLS
    if (l->tls_key_path)
        free_str(l->tls_certificate_path);
    if (l->tls_key_path)
        free_str(l->tls_key_path);
#endif
    myfree(l, M_NETWORK);
}

void
network_close_connection(int read_fd, int write_fd)
{
    /* read_fd and write_fd are the same, so we only need to deal with one. */
    close(read_fd);
}

void
close_listener(int fd)
{
    close(fd);
}

static nhandle *
make_new_connection(server_listener sl, int rfd, int wfd, bool outbound,
                    uint16_t listen_port, const char *listen_hostname,
                    const char *listen_ipaddr, uint16_t local_port,
                    const char *local_hostname, const char *local_ipaddr,
                    sa_family_t protocol SSL_CONTEXT_1_DEF)
{
    nhandle *h;
    network_handle nh;

    nh.ptr = h = new_nhandle(rfd, wfd, outbound, listen_port, listen_hostname,
                             listen_ipaddr, local_port, local_hostname, local_ipaddr, protocol SSL_CONTEXT_1_ARG);
    h->shandle = server_new_connection(sl, nh, outbound);

    return h;
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
#ifdef USE_TLS
    SSL *tls = nullptr;
    bool use_tls = l->use_tls;
    const char *certificate_path = l->tls_certificate_path;
    const char *key_path = l->tls_key_path;
#endif

    switch (network_accept_connection(l->fd, &rfd, &wfd, &name, &ip_addr, &port, &protocol USE_TLS_BOOL SSL_CONTEXT_2_ARG TLS_CERT_PATH)) {
        case PA_OKAY:
            make_new_connection(l->slistener, rfd, wfd, 0, l->port, l->name, l->ip_addr, port, name, ip_addr, protocol SSL_CONTEXT_1_ARG);
            break;
        case PA_FULL:
            for (i = 0; i < proto.pocket_size; i++)
                close(pocket_descriptors[i]);
            if (network_accept_connection(l->fd, &rfd, &wfd, &name, &ip_addr, &port, &protocol USE_TLS_BOOL SSL_CONTEXT_2_ARG TLS_CERT_PATH) != PA_OKAY) {
                errlog("Can't accept connection even by emptying pockets!\n");
            } else {
                nh.ptr = h = new_nhandle(rfd, wfd, 0, l->port, l->name, l->ip_addr, port, name, ip_addr, protocol SSL_CONTEXT_1_ARG);
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

enum accept_error
network_accept_connection(int listener_fd, int *read_fd, int *write_fd,
                          const char **name, const char **ip_addr, uint16_t *port,
                          sa_family_t *protocol USE_TLS_BOOL_DEF SSL_CONTEXT_2_DEF TLS_CERT_PATH_DEF)
{
    int option = 1;
    int fd;
    struct sockaddr_storage addr;
    socklen_t addr_length = sizeof addr;

#if HAVE_ACCEPT4
    fd = accept4(listener_fd, (struct sockaddr *)&addr, &addr_length, SOCK_NONBLOCK);
#else
    fd = accept(listener_fd, (struct sockaddr *)&addr, &addr_length);
#endif
    if (fd < 0) {
        if (errno == EMFILE)
            return PA_FULL;
        else {
            log_perror("Accepting new network connection");
            return PA_OTHER;
        }
    }

#ifdef USE_TLS
    if (use_tls) {
        if (!tls_ctx || !(*tls = SSL_new(tls_ctx))) {
            errlog("TLS: Error creating context\n");
            close_listener(fd);
            return PA_OTHER;
        } else {
            SSL_set_fd(*tls, fd);
            SSL_set_accept_state(*tls);
            bool cert_success = true;
            int tls_success = 0;
            if (certificate_path != nullptr)
                if ((tls_success = SSL_use_certificate_chain_file(*tls, certificate_path)) <= 0) {
                    SSL_get_error(*tls, tls_success);
                    errlog("TLS: Error loading certificate (%s) from argument: %s\n", certificate_path, ERR_error_string(ERR_get_error(), nullptr));
                    cert_success = false;
                    ERR_clear_error();
                }
            if (cert_success && key_path != nullptr)
                if ((tls_success = SSL_use_PrivateKey_file(*tls, key_path, SSL_FILETYPE_PEM)) <= 0) {
                    SSL_get_error(*tls, tls_success);
                    errlog("TLS: Error loading private key (%s) from argument: %s\n", key_path, ERR_error_string(ERR_get_error(), nullptr));
                    cert_success = false;
                    ERR_clear_error();
                }
            if (cert_success && certificate_path != nullptr && key_path != nullptr && !SSL_check_private_key(*tls)) {
                errlog("TLS: Private key (%s) does not match certificate (%s)!\n", key_path, certificate_path);
                cert_success = false;
            }

            if (!cert_success) {
                close_listener(fd);
                SSL_shutdown(*tls);
                SSL_free(*tls);
                return PA_OTHER;
            }
        }
    }
#endif

    // Disable Nagle algorithm on the socket
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char *) &option, sizeof(option)) < 0)
        log_perror("Couldn't set TCP_NODELAY");
#ifdef __linux__
    // Disable delayed ACKs
    if (setsockopt(fd, IPPROTO_TCP, TCP_QUICKACK, (char*) &option, sizeof(option)) < 0)
        log_perror("Couldn't set TCP_QUICKACK");
#endif

    *read_fd = *write_fd = fd;

    *ip_addr = get_ntop(&addr);
    if (!server_int_option("no_name_lookup", NO_NAME_LOOKUP))
        *name = get_nameinfo((struct sockaddr *)&addr);
    else
        *name = str_dup(*ip_addr);

    *port = get_in_port(&addr);
    *protocol = addr.ss_family;

    return PA_OKAY;
}

enum error
make_listener(Var desc, int *fd, const char **name, const char **ip_address,
              uint16_t *port, const bool use_ipv6, const char *interface)
{
    int s, yes = 1;
    struct addrinfo hints;
    struct addrinfo * servinfo, *p;
    const char *default_interface = use_ipv6 ? bind_ipv6 : bind_ipv4;

    if (desc.type != TYPE_INT)
        return E_TYPE;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = (use_ipv6 ? AF_INET6 : AF_INET);
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;        // use all the IPs

    char *port_string = get_port_str(desc.v.num);
    int rv = getaddrinfo(interface ? interface : default_interface, port_string, &hints, &servinfo);
    free(port_string);
    if (rv != 0) {
        log_perror(gai_strerror(rv));
        return E_QUOTA;
    }

    /* If we have multiple results, we'll bind to the first one we can. */
    for (p = servinfo; p != nullptr; p = p->ai_next) {
        if ((s = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0) {
            log_perror("Error creating listening socket");
            continue;
        }

        if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) < 0) {
            log_perror("Error setting listening socket reuseaddr");
            close(s);
            freeaddrinfo(servinfo);
            return E_QUOTA;
        }

        if (use_ipv6 && setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, &yes, sizeof yes) < 0) {
            log_perror("Error disabling listening socket dual-stack mode for IPv6");
            close(s);
            freeaddrinfo(servinfo);
            return E_QUOTA;
        }

        if (bind(s, p->ai_addr, p->ai_addrlen) < 0) {
            log_perror("Error binding listening socket");
            close(s);
            continue;
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

    *ip_address = get_ntop((struct sockaddr_storage *)p->ai_addr);
    if (!server_int_option("no_name_lookup", NO_NAME_LOOKUP))
        *name = get_nameinfo((struct sockaddr *)p->ai_addr);
    else
        *name = str_dup(*ip_address);
    *port = desc.v.num;
    *fd = s;

    freeaddrinfo(servinfo);

    return E_NONE;
}

static void *get_in_addr(const struct sockaddr_storage *sa)
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

static unsigned short int get_in_port(const struct sockaddr_storage *sa)
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

static const char *get_ntop(const struct sockaddr_storage *sa)
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

static const char *get_nameinfo(const struct sockaddr *sa)
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

static const char *get_nameinfo_port(const struct sockaddr *sa)
{
    char service[NI_MAXSERV];
    int status = getnameinfo(sa, sizeof * sa, nullptr, 0, service, sizeof service, NI_NUMERICSERV);

    if (status != 0) {
        errlog("getnameinfo_port failed: %s\n", gai_strerror(status));
        return nullptr;
    }

    return str_dup(service);
}

static const char *get_ipver(const struct sockaddr_storage *sa)
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

static char *get_port_str(int port)
{
    char *port_str = nullptr;
    asprintf(&port_str, "%d", port);
    return port_str;
}

#ifdef OUTBOUND_NETWORK
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
open_connection(Var arglist, int *read_fd, int *write_fd,
                const char **name, const char **ip_addr,
                uint16_t *port, sa_family_t *protocol,
                bool use_ipv6 USE_TLS_BOOL_DEF SSL_CONTEXT_2_DEF)
{
    static Timer_ID id;
    int s, result;
    struct addrinfo * servinfo, *p, hint;
    int yes = 1;

    if (!outbound_network_enabled)
        return E_PERM;

    if (arglist.v.list[0].v.num < 2)
        return E_ARGS;
    else if (arglist.v.list[1].type != TYPE_STR ||
             arglist.v.list[2].type != TYPE_INT)
        return E_TYPE;

    const char *host_name = arglist.v.list[1].v.str;
    int host_port = arglist.v.list[2].v.num;

    memset(&hint, 0, sizeof hint);
    hint.ai_family = use_ipv6 ? AF_INET6 : AF_INET;
    hint.ai_socktype = SOCK_STREAM;
    hint.ai_flags = AI_PASSIVE;

    char *port_string = get_port_str(host_port);
    int rv = getaddrinfo(host_name, port_string, &hint, &servinfo);
    free(port_string);
    if (rv != 0) {
        errlog("open_connection getaddrinfo error: %s\n", gai_strerror(rv));
        return E_INVARG;
    }

    /* If we have multiple results, we'll bind to the first one we can. */
    for (p = servinfo; p != nullptr; p = p->ai_next) {
        if ((s = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            if (errno != EMFILE)
                log_perror("Error making socket in open_connection");
            continue;
        }

        if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
            log_perror("Error setting listening socket options");
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
#ifdef USE_TLS
        if (use_tls) {
            if (!tls_ctx) {
                result = -1;
                errno = TLS_FAIL;
            } else if (!(*tls = SSL_new(tls_ctx))) {
                errlog("TLS: Error creating context\n");
                result = -1;
                errno = TLS_FAIL;
            } else {
                SSL_set_fd(*tls, s);
                SSL_set_connect_state(*tls);
                int tls_success = SSL_connect(*tls);
                if (tls_success <= 0) {
                    SSL_get_error(*tls, tls_success);
                    errlog("TLS: Connect failed: %s\n", ERR_error_string(ERR_get_error(), nullptr));
                    result = -1;
                    errno = TLS_CONNECT_FAIL;
                    ERR_clear_error();
                } else {
#ifdef LOG_TLS_CONNECTIONS
                    oklog("TLS: %s. Cipher: %s\n", SSL_state_string_long(*tls), SSL_get_cipher(*tls));
#endif
                }
            }
        }
#endif
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
                errno == ETIMEDOUT) {
            log_perror("open_network_connection error");
            return E_INVARG;
#ifdef USE_TLS
        } else if (errno == TLS_FAIL) {
            return E_INVARG;
        } else if (errno == TLS_CONNECT_FAIL) {
            if (*tls) {
                SSL_shutdown(*tls);
                SSL_free(*tls);
            }
            return E_INVARG;
#endif
        }

        log_perror("Connecting in open_connection");
        return E_QUOTA;
    }

    *read_fd = *write_fd = s;

    *ip_addr = get_ntop((struct sockaddr_storage *)p->ai_addr);
    if (!server_int_option("no_name_lookup", NO_NAME_LOOKUP))
        *name = get_nameinfo((struct sockaddr *)p->ai_addr);
    else
        *name = str_dup(*ip_addr);

    *port = get_in_port((struct sockaddr_storage *)p->ai_addr);
    *protocol = servinfo->ai_family;

    freeaddrinfo(servinfo);

    return E_NONE;
}
#endif              /* OUTBOUND_NETWORK */

/* At this stage in the game, this function only looks for a port floating
   at the end of the command-line arguments. */
static void
tcp_arguments(int argc, char **argv, uint16_t *pport)
{
    char *p = nullptr;

    for ( ; argc > 0; argc--, argv++) {
            if (p != nullptr) /* strtoul always sets p */
                return;
            *pport = strtoul(argv[0], &p, 10);
            if (*p != '\0')
                return;
        }
    return;
}

/*************************
 * External entry points *
 *************************/

int
network_initialize(int argc, char **argv, Var *desc)
{
    uint16_t port = 0;

    proto.pocket_size = 1;
    proto.believe_eof = 1;
    proto.eol_out_string = "\r\n";

    /* Look for a stray port that wasn't specified with -p or -t */
    tcp_arguments(argc, argv, &port);

    memset(&tcp_hint, 0, sizeof tcp_hint);
    tcp_hint.ai_family = AF_UNSPEC;
    tcp_hint.ai_socktype = SOCK_STREAM;

    desc->type = TYPE_INT;
    desc->v.num = port;

#ifdef USE_TLS
    SSL_load_error_strings();
    SSL_library_init();

    tls_ctx = SSL_CTX_new(TLS_method());
    if (!tls_ctx) {
        errlog("NETWORK: Failed to initialize OpenSSL context. TLS is unavailable.\n");
        ERR_print_errors_fp(stderr);
    } else {
        char error_msg[256];
        if (SSL_CTX_use_certificate_chain_file(tls_ctx, default_certificate_path) <= 0) {
            ERR_error_string_n(ERR_get_error(), error_msg, 256);
            errlog("TLS: Failed to load default certificate: %s\n", error_msg);
        }

        if (SSL_CTX_use_PrivateKey_file(tls_ctx, default_key_path, SSL_FILETYPE_PEM) <= 0) {
            ERR_error_string_n(ERR_get_error(), error_msg, 256);
            errlog("TLS: Failed to load default private key: %s\n", error_msg);
        }

        if (!SSL_CTX_check_private_key(tls_ctx)) {
            ERR_error_string_n(ERR_get_error(), error_msg, 256);
            errlog("TLS: Private key does not match the certificate: %s\n", error_msg);
        }

        SSL_CTX_set_session_id_context(tls_ctx, (const unsigned char*)"ToastStunt", 10);
        SSL_CTX_set_mode(tls_ctx, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_RELEASE_BUFFERS);

#ifdef VERIFY_TLS_PEERS
        if (!SSL_CTX_set_default_verify_paths(tls_ctx))
            errlog("TLS: Unable to load CA! Peer verification will likely fail.\n");

        SSL_CTX_set_verify(tls_ctx, SSL_VERIFY_PEER, nullptr);
#endif
    }
#endif /* USE_TLS */

    eol_length = strlen(proto.eol_out_string);
    get_pocket_descriptors();

    /* we don't care about SIGPIPE, we notice it in mplex_wait() and write() */
    signal(SIGPIPE, SIG_IGN);

    return 1;
}

enum error
network_make_listener(server_listener sl, Var desc, network_listener * nl,
                      const char **name, const char **ip_address,
                      uint16_t *port, bool use_ipv6, const char *interface 
                      USE_TLS_BOOL_DEF TLS_CERT_PATH_DEF)
{
    int fd;
    enum error e = make_listener(desc, &fd, name, ip_address, port, use_ipv6, interface);
    nlistener *listener;

    if (e == E_NONE) {
        nl->ptr = listener = (nlistener *) mymalloc(sizeof(nlistener), M_NETWORK);
        listener->fd = fd;
        listener->slistener = sl;
        listener->name = str_dup(*name);
        listener->ip_addr = str_dup(*ip_address);
        listener->port = *port;
#ifdef USE_TLS
        listener->use_tls = use_tls;
        listener->tls_certificate_path = certificate_path;
        listener->tls_key_path = key_path;
#endif
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

    int status = listen(l->fd, 5);
    if (status < 0)
        log_perror("Failed to listen");
    return status < 0 ? 0 : 1;
}

static int
enqueue_output(network_handle nh, const char *line, int line_length, int add_eol, int flush_ok)
{
    nhandle *h = (nhandle *) nh.ptr;
    int length = line_length + (add_eol ? eol_length : 0);
    char *buffer;
    text_block *block;
    bool move_output_head = true;
    /* If SSL_ERROR_WANT_WRITE, we need to preserve the first output_head. This flag indicates that the while loop below won't
       change the output head and will instead move 'next' around. */

    if (h->output_length != 0 && h->output_length + length > server_flag_option_cached(SVO_MAX_QUEUED_OUTPUT)) {   /* must flush... */
        int to_flush;
        text_block *b, *next;

        (void)push_output(h);
        to_flush = h->output_length + length - server_flag_option_cached(SVO_MAX_QUEUED_OUTPUT);
        if (to_flush > 0 && !flush_ok)
            return 0;

        next = h->output_head;
#ifdef USE_TLS
        if (h->want_write) {
            if (h->output_head->next == nullptr) {
                /* Not much we can do here. OpenSSL expects the exact same data as before,
                   so output_head must remain intact. But we have nothing else to flush... */
                return 1;
            }
            next = h->output_head->next;
            move_output_head = false;
        }
#endif
        while (to_flush > 0 && (b = next)) {
            h->output_length -= b->length;
            to_flush -= b->length;
            h->output_lines_flushed++;
            next = b->next;
            if (move_output_head)
                h->output_head = next;
            else
                h->output_head->next = next;
            free_text_block(b);
        }
        if (h->output_head == nullptr)
            h->output_tail = &(h->output_head);
        else if (h->output_head->next == nullptr)
            h->output_tail = &(h->output_head->next);
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
    bool pending_tls = false;

    mplex_clear();
    for (l = all_nlisteners; l; l = l->next)
        mplex_add_reader(l->fd);
    for (h = all_nhandles; h; h = h->next) {
        if (!h->input_suspended)
        {
            mplex_add_reader(h->rfd);
#ifdef USE_TLS
            if (h->tls && h->connected && SSL_has_pending(h->tls)) {
                pending_tls = true;
                timeout = 0;
            }
#endif
        }
        if (h->output_head)
            mplex_add_writer(h->wfd);
    }
    add_registered_fds();

    if (mplex_wait(timeout) && !pending_tls)
        return 0;
    else {
        for (l = all_nlisteners; l; l = l->next)
            if (mplex_is_readable(l->fd))
                accept_new_connection(l);
        for (h = all_nhandles; h; h = hnext) {
            hnext = h->next;
            if (((mplex_is_readable(h->rfd) && !pull_input(h))
                    || (mplex_is_writable(h->wfd) && !push_output(h))) && get_nhandle_refcount(h) == 1) {
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

bool
network_is_localhost(const network_handle nh)
{
    const nhandle *h = (nhandle *) nh.ptr;
    const char *ip = h->destination_ipaddr;
    int ret = 0;

    if (strcmp(ip, "127.0.0.1") == 0 || strcmp(ip, "::1") == 0)
        ret = 1;

    return ret;
}

int
rewrite_connection_name(network_handle nh, const char *destination, const char *destination_ip, const char *source, const char *source_port)
{
    const char *nameinfo;
    struct addrinfo *address;
    int ret = 0, status = getaddrinfo(source, source_port, &tcp_hint, &address);

    if (status < 0) {
        errlog("getaddrinfo failed while rewriting connection_name: %s\n", gai_strerror(status));
        ret = -1;
    } else {
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
    return ret;
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

uint32_t
get_nhandle_refcount(const network_handle nh)
{
    nhandle *h = (nhandle *)nh.ptr;
    return h->refcount;
}

uint32_t
get_nhandle_refcount(nhandle *h)
{
    return h->refcount;
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

    if (--h->refcount <= 0)
        close_nhandle(h);
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

    struct addrinfo *address = 0;
    int status = getaddrinfo(h->destination_ipaddr, nullptr, &tcp_hint, &address);
    if (status < 0) {
        // Better luck next time.
        *name = str_dup(h->name);
        retval = -1;
    } else {
        *name = get_nameinfo(address->ai_addr);
    }
    if (address)
        freeaddrinfo(address);

    pthread_mutex_unlock(h->name_mutex);

    return retval;
}

char *
full_network_connection_name(const network_handle nh, bool legacy)
{
    const nhandle *h = (nhandle *)nh.ptr;
    char *ret = nullptr;

    if (legacy) {
        asprintf(&ret, "port %i %s %s [%s], port %i", h->source_port, h->outbound ? "to" : "from", h->name, h->destination_ipaddr, h->destination_port);
    } else {
        asprintf(&ret, "%s [%s], port %i %s %s [%s], port %i", h->source_address, h->source_ipaddr, h->source_port, h->outbound ? "to" : "from", h->name, h->destination_ipaddr, h->destination_port);
    }

    return ret;
}

const char *
network_ip_address(const network_handle nh)
{
    const nhandle *h = (nhandle *) nh.ptr;

    const char *ret = h->destination_ipaddr;
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
    uint16_t port = h->source_port;

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

#ifdef USE_TLS
int
network_handle_is_tls(const network_handle nh)
{
    const nhandle *h = (nhandle *)nh.ptr;

    return h->tls != nullptr;
}

int
nlistener_is_tls(const void *sl)
{
    const nlistener *l = (nlistener *)sl;

    return l->use_tls;
}

Var
tls_connection_info(const network_handle nh)
{
    static Var cyphersuite_key_name = str_dup_to_var("cyphersuite");
    static Var active_key_name = str_dup_to_var("active");
    static Var tls_version = str_dup_to_var("version");
    const nhandle *h = (nhandle *)nh.ptr;
    Var ret = new_map();

    ret = mapinsert(ret, var_ref(active_key_name), Var::new_int(h->tls != nullptr));
    if (h->tls) {
        ret = mapinsert(ret, var_ref(cyphersuite_key_name), str_dup_to_var(SSL_get_cipher(h->tls)));
        ret = mapinsert(ret, var_ref(tls_version), str_dup_to_var(SSL_get_version(h->tls)));
    }

    return ret;
}
#endif /* USE_TLS */

void
network_set_connection_binary(network_handle nh, bool do_binary)
{
    nhandle *h = (nhandle *) nh.ptr;

    h->binary = do_binary;
}

#define NETWORK_CO_TABLE(DEFINE, nh, value, _)                  \
    DEFINE(client-echo, _, TYPE_INT, num,                       \
           ((nhandle *)nh.ptr)->client_echo,                    \
           network_set_client_echo(nh, is_true(value));         \
           )                                                    \
                                                                \
    DEFINE(keep-alive, _, TYPE_MAP, tree,                       \
            network_keep_alive_map(nh).v.tree,                  \
            {                                                   \
                if (!network_set_client_keep_alive(nh, value))  \
                    return 0;                                   \
            })                                                  \

void
network_set_client_echo(network_handle nh, int is_on)
{
    nhandle *h = (nhandle *) nh.ptr;

    /* These values taken from RFC 854 and RFC 857. */
#    define TN_IAC  255 /* Interpret As Command */
#    define TN_WILL 251
#    define TN_WONT 252
#    define TN_ECHO 1

    static char telnet_cmd[4] = { (char)TN_IAC, (char)0, (char)TN_ECHO, (char)0 };

    h->client_echo = is_on;
    if (is_on)
        telnet_cmd[1] = (char)TN_WONT;
    else
        telnet_cmd[1] = (char)TN_WILL;
    enqueue_output(nh, telnet_cmd, 3, 0, 1);
}

static Var
network_keep_alive_map(network_handle nh)
{
    nhandle *h = (nhandle*)nh.ptr;

    Var ret = new_map();
    ret = mapinsert(ret, str_dup_to_var("enabled"), Var::new_int(h->keep_alive));
    ret = mapinsert(ret, str_dup_to_var("idle"), Var::new_int(h->keep_alive_idle));
    ret = mapinsert(ret, str_dup_to_var("interval"), Var::new_int(h->keep_alive_interval));
    ret = mapinsert(ret, str_dup_to_var("count"), Var::new_int(h->keep_alive_count));

    return ret;
}

/* Set keep-alive options for a connection. The arguments can either be an int or a map.
    INT: Enable or disable. If enabling, default options are used. (See below.)
    MAP: A MAP containing the various options that can be set. It's assumed that a 
        non-empty MAP being provided means you want to enable TCP keepalive on the connection.
    Defaults can be found in options.h.
*/
int
network_set_client_keep_alive(network_handle nh, Var map)
{
    if (map.type != TYPE_INT && map.type != TYPE_MAP)
        return 0;

    nhandle *h = (nhandle*)nh.ptr;
    int keep_alive = h->keep_alive;
    int idle = h->keep_alive_idle;
    int interval = h->keep_alive_interval;
    int count = h->keep_alive_count;
    Var value;

    keep_alive = is_true(map);

    if (map.type == TYPE_MAP) {
        if (mapstrlookup(map, "idle", &value, 0) != nullptr && value.type == TYPE_INT && value.v.num > 0)
            idle = value.v.num;
        if (mapstrlookup(map, "interval", &value, 0) != nullptr && value.type == TYPE_INT && value.v.num > 0)
            interval = value.v.num;
        if (mapstrlookup(map, "count", &value, 0) != nullptr && value.type == TYPE_INT && value.v.num > 0)
            count = value.v.num;        
    }

    if (setsockopt(h->rfd, SOL_SOCKET, SO_KEEPALIVE, &keep_alive, sizeof(keep_alive)) < 0 ||
#ifndef __MACH__
setsockopt(h->rfd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle)) < 0 ||
#else
setsockopt(h->rfd, IPPROTO_TCP, TCP_KEEPALIVE, &idle, sizeof(idle)) < 0 ||
#endif
        setsockopt(h->rfd, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval)) < 0 ||
        setsockopt(h->rfd, IPPROTO_TCP, TCP_KEEPCNT, &count, sizeof(count)) < 0) 
    {
        log_perror("TCP keepalive setsockopt failed");
        return 0;
    } else {
        h->keep_alive = keep_alive;
        h->keep_alive_idle = idle;
        h->keep_alive_interval = interval;
        h->keep_alive_count = count;
        return 1;
    }
}

#ifdef OUTBOUND_NETWORK
enum error
network_open_connection(Var arglist, server_listener sl, bool use_ipv6 USE_TLS_BOOL_DEF)
{
    int rfd, wfd;
    const char *name;
    const char *ip_addr;
    uint16_t port;
    sa_family_t protocol;
    enum error e;
    nhandle *h;
#ifdef USE_TLS
    SSL *tls = nullptr;
#endif

    e = open_connection(arglist, &rfd, &wfd, &name, &ip_addr, &port, &protocol, use_ipv6 USE_TLS_BOOL SSL_CONTEXT_2_ARG);
    if (e == E_NONE) {
        h = make_new_connection(sl, rfd, wfd, 1, 0, nullptr, nullptr, port, name, ip_addr, protocol SSL_CONTEXT_1_ARG);
#ifdef USE_TLS
        h->connected = true;
#endif
    }

    return e;
}
#endif /* OUTBOUND_NETWORK */

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
    /* This would be a good candidate for deferred deletion buuuut...
     * we're shutting down anyway, may as well do it the lazy way. */
    std::vector<network_handle> handles;

    for (nhandle *h = all_nhandles; h; h = h->next) {
        network_handle nh;
        nh.ptr = h;
        handles.push_back(nh);
    }

    for (network_handle nh : handles)
        decrement_nhandle_refcount(nh);

    while (all_nlisteners)
        close_nlistener(all_nlisteners);
}

Var
network_connection_options(network_handle nh, Var list)
{
    CONNECTION_OPTION_LIST(NETWORK_CO_TABLE, nh, list);
}

int
network_connection_option(network_handle nh, const char *option, Var * value)
{
    CONNECTION_OPTION_GET(NETWORK_CO_TABLE, nh, option, value);
}

int
network_set_connection_option(network_handle nh, const char *option, Var value)
{
    CONNECTION_OPTION_SET(NETWORK_CO_TABLE, nh, option, value);
}