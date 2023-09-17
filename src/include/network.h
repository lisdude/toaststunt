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

/*
 * See 'server.h' for the complete set of procedures the network
 * implementation is expected to use from the rest of the server.
 */

#ifndef Network_H
#define Network_H 1

#include "structures.h"
#include "streams.h"
#include <netdb.h>      // sa_family_t

/* These get set by command-line options in server.cc */
extern int outbound_network_enabled;
extern char* bind_ipv4;
extern char* bind_ipv6;
extern const char* default_certificate_path;
extern const char* default_key_path;
/*                                                    */

#ifdef USE_TLS
#include <openssl/ssl.h>
#include <openssl/err.h>

#define TLS_FAIL            -9999
#define TLS_CONNECT_FAIL    -9998

extern SSL_CTX *tls_ctx;
#endif

typedef struct {		/* Network's handle on a connection */
    void *ptr;
} network_handle;

typedef struct {		/* Network's handle on a listening point */
    void *ptr;
} network_listener;

struct nhandle; /* Forward declaration of nhandle. */

#include "server.h"		/* Include this *after* defining the types */

struct proto {
    unsigned pocket_size;	/* Maximum number of file descriptors it might
				 * take to accept a new connection in this
				 * protocol.  The generic multi-user network
				 * code will keep this many descriptors `in its
				 * pocket', ready to be freed up in order to
				 * tell potential users that there's no more
				 * room in the server. */
    int believe_eof;		/* If true, then read() will return 0 on a
				 * connection using this protocol iff the
				 * connection really is closed.  If false, then
				 * read() -> 0 will be interpreted as the
				 * connection still being open, but no data
				 * being available. */
    const char *eol_out_string;	/* The characters to add to the end of each
				 * line of output on connections. */
};

extern enum error make_listener(Var desc, int *fd,
				      const char **name, const char **ip_address,
					  uint16_t *port, const bool use_ipv6, const char *interface);
				/* DESC is the second argument in a call to the
				 * built-in MOO function `listen()'; it should
				 * be used as a specification of a new local
				 * point on which to listen for connections.
				 * If DESC is OK for this protocol and a
				 * listening point is successfully made, then
				 * *FD should be the file descriptor of the new
				 * listening point, *CANON a canonicalized
				 * version of DESC (reflecting any defaulting
				 * or aliasing), *NAME a human-readable name
				 * for the listening point, and E_NONE
				 * returned.  Otherwise, an appropriate error
				 * should be returned.
				 *
				 * NOTE: It is more than okay for the server
				 * still to be refusing connections.  The
				 * server's call to network_listen() marks the
				 * time by which the server must start
				 * accepting connections.
				 */

enum accept_error {
    PA_OKAY, PA_FULL, PA_OTHER
};

extern enum accept_error
 network_accept_connection(int listener_fd,
			 int *read_fd, int *write_fd,
			 const char **name, const char **ip_addr,
			 uint16_t *port, sa_family_t *protocol USE_TLS_BOOL_DEF SSL_CONTEXT_2_DEF TLS_CERT_PATH_DEF);
				/* Accept a new connection on LISTENER_FD,
				 * returning PA_OKAY if successful, PA_FULL if
				 * unsuccessful only because there aren't
				 * enough file descriptors available, and
				 * PA_OTHER for other failures (in which case a
				 * message should have been output to the
				 * server log).  LISTENER_FD was returned by a
				 * call to make_listener().  On
				 * successful return, *READ_FD and *WRITE_FD
				 * should be file descriptors on which input
				 * and output for the new connection can be
				 * done, *NAME should be a human-readable
				 * string identifying this connection, and
                 * *IP_ADDR should be an in_addr struct of
                 * the original IP address.
				 */

#ifdef OUTBOUND_NETWORK

extern enum error open_connection(Var arglist,
					int *read_fd, int *write_fd,
					const char **name, const char **ip_addr,
					uint16_t *port, sa_family_t *protocol, bool use_ipv6 USE_TLS_BOOL_DEF SSL_CONTEXT_2_DEF);
				/* The given MOO arguments should be used as a
				 * specification of a remote network connection
				 * to be opened.  If the arguments are OK for
				 * this protocol and the connection is success-
				 * fully made, then *READ_FD and *WRITE_FD
				 * should be set as network_accept_connection()
				 * does, *LOCAL_NAME a human-readable string
				 * naming the local endpoint of the connection,
				 * *REMOTE_NAME a string naming the remote
				 * endpoint, and E_NONE returned.  Otherwise,
				 * an appropriate error should be returned.
				 */

#endif				/* OUTBOUND_NETWORK */

extern void network_close_connection(int read_fd, int write_fd);
				/* Close the given file descriptors, which were
				 * returned by network_accept_connection(),
				 * performing whatever extra clean-ups are
				 * required by the protocol.
				 */

extern void close_listener(int fd);
				/* Close FD, which was returned by a call to
				 * network_make_listener(), performing whatever
				 * extra clean-ups are required by the
				 * protocol.
				 */

extern int network_initialize(int argc, char **argv,
			      Var * desc);
				/* ARGC and ARGV refer to just the protocol-
				 * specific command-line arguments, if any,
				 * which always come after any protocol-
				 * independent args.  Returns true iff those
				 * arguments were valid.  On success, all of
				 * the fields of PROTO should be filled in with
				 * values appropriate for the protocol, and
				 * *DESC should be a MOO value to pass to
				 * network_make_listener() in order to create the
				 * server's initial listening point.
				 */

extern enum error network_make_listener(server_listener sl, Var desc,
					network_listener * nl, 
					const char **name, const char **ip_address,
					uint16_t *port, bool use_ipv6, const char *interface 
					USE_TLS_BOOL_DEF TLS_CERT_PATH_DEF);
				/* DESC is the second argument in a call to the
				 * built-in MOO function `listen()'; it should
				 * be used as a specification of a new local
				 * point on which to listen for connections.
				 * If DESC is OK and a listening point is
				 * successfully made, then *NL should be the
				 * network's handle on the new listening point,
				 * *CANON a canonicalized version of DESC
				 * (reflecting any defaulting or aliasing),
				 * *NAME a human-readable name for the
				 * listening point, and E_NONE returned.
				 * Otherwise, an appropriate error should be
				 * returned.  By this call, the network and
				 * server exchange tokens representing the
				 * listening point for use in later calls on
				 * each other.
				 *
				 * NOTE: It is more than okay for the server
				 * still to be refusing connections.  The
				 * server's call to network_listen() marks the
				 * time by which the server must start
				 * accepting connections.
				 */

extern int network_listen(network_listener nl);
				/* Prepare for accepting connections on the
				 * given file descriptor, returning true if
				 * successful.  FD was returned by a call to
				 * network_make_listener().
				 */

extern int network_send_line(network_handle nh, const char *line,
			     int flush_ok, bool send_newline);
				/* The given line should be queued for output
				 * on the specified connection.  'line' does
				 * NOT end in a newline; it is up to the
				 * network code to add the appropriate bytes,
				 * if any.  If FLUSH_OK is true, then the
				 * network module may, if necessary, discard
				 * some other pending output to make room for
				 * this new output.  If FLUSH_OK is false, then
				 * no output may be discarded in this way.
				 * Returns true iff the given line was
				 * successfully queued for output; it can only
				 * fail if FLUSH_OK is false. If send_newline
                 * is false, the network code is compelled to
                 * suppress the newline.
				 */

extern int network_send_bytes(network_handle nh,
			      const char *buffer, int buflen,
			      int flush_ok);
				/* The first BUFLEN bytes in the given BUFFER
				 * should be queued for output on the specified
				 * connection.  If FLUSH_OK is true, then the
				 * network module may, if necessary, discard
				 * some other pending output to make room for
				 * this new output.  If FLUSH_OK is false, then
				 * no output may be discarded in this way.
				 * Returns true iff the given bytes were
				 * successfully queued for output; it can only
				 * fail if FLUSH_OK is false.
				 */

extern int network_buffered_output_length(network_handle nh);
				/* Returns the number of bytes of output
				 * currently queued up on the given connection.
				 */

extern void network_suspend_input(network_handle nh);
				/* The network module is strongly encouraged,
				 * though not strictly required, to temporarily
				 * stop calling `server_receive_line()' for
				 * the given connection.
				 */

extern void network_resume_input(network_handle nh);
				/* The network module may once again feel free
				 * to call `server_receive_line()' for the
				 * given connection.
				 */

extern void network_set_connection_binary(network_handle, bool);
				/* Set the given connection into or out of
				 * `binary input mode'.
				 */

extern int network_process_io(int timeout);
				/* This is called at intervals to allow the
				 * network to flush pending output, receive
				 * pending input, and handle requests for new
				 * connections.  It is acceptable for the
				 * network to block for up to 'timeout'
				 * seconds.  Returns true iff it found some I/O
				 * to do (i.e., it didn't use up all of the
				 * timeout).
				 */

extern const char *network_connection_name(network_handle nh);
				/* Return some human-readable identification
				 * for the specified connection.  It should fit
				 * into the phrase 'Connection accepted: %s'.
				 */

extern int lookup_network_connection_name(const network_handle nh, const char **name);
				/* Similar to network_connection_name, except this function
				   will perform a DNS name lookup and fallback to the
				   stored value if it fails. Returns 0 if the DNS lookup
				   was successful or -1  if it failed.
				*/

extern char *full_network_connection_name(const network_handle nh, bool legacy = false);
				/* Returns a 'legacy' style connection name string in the form:
				   INTERFACE HOST NAME [IP] port PORT from HOSTNAME [IP], port PORT
				   If legacy is true, the interface name and IP address are not included. */

extern const char *network_ip_address(network_handle nh);
				/* Return the numeric IP address for
                 * the connection.
				 */

extern const char *network_source_connection_name(const network_handle nh);
				/* Return the resolved hostname (if available) of
				   the local interface a connection is connected to.
				 */

extern const char *network_source_ip_address(const network_handle nh);
				/* Return the unresolved IP address of the local interface 
				   a connection is connected to.
				 */

extern uint16_t network_port(const network_handle nh);
				/* Return the local port of a network connection. */

extern uint16_t network_source_port(const network_handle nh);
				/* Return the port of the local interface a connection is connected to. */

extern const char *network_protocol(const network_handle nh);
				/* Return a string indicating the protocol (IPv4, IPv6) of the connection. */

extern Var network_connection_options(network_handle nh,
				      Var list);
				/* Add the current option settings for the
				 * given connection onto the end of LIST and
				 * return the new list.  Each entry on LIST
				 * should be a {NAME, VALUE} pair.
				 */

extern int network_connection_option(network_handle nh,
				     const char *option,
				     Var * value);
				/* Return true iff the given option name
				 * is valid for the given connection, storing
				 * the current setting into *VALUE if valid.
				 */

extern int network_set_connection_option(network_handle nh,
					 const char *option,
					 Var value);
				/* Return true iff the given option/value pair
				 * is valid for the given connection, applying
				 * the given setting if valid.
				 */

int network_set_client_keep_alive(network_handle nh, Var map);

#ifdef OUTBOUND_NETWORK
extern enum error network_open_connection(Var arglist, server_listener sl, bool use_ipv6 USE_TLS_BOOL_DEF);
				/* The given MOO arguments should be used as a
				 * specification of a remote network connection
				 * to be made.  If the arguments are OK and the
				 * connection is successfully made, it should
				 * be treated as if it were a normal connection
				 * accepted by the server (e.g., a network
				 * handle should be created for it, the
				 * function server_new_connection should be
				 * called, etc.) and E_NONE should be returned.
				 * Otherwise, some other appropriate error
				 * value should be returned.  The caller of
				 * this function is responsible for freeing the
				 * MOO value in `arglist'.  This function need
				 * not be supplied if OUTBOUND_NETWORK is not
				 * defined.
				 */

#endif

extern void network_close(network_handle nh);
				/* The specified connection should be closed
				 * immediately, after flushing as much pending
				 * output as possible.  Effective immediately,
				 * the given network_handle will not be used
				 * by the server and the corresponding
				 * server_handle should not be used by the
				 * network.
				 */

extern void network_close_listener(network_listener nl);
				/* The specified listening point should be
				 * closed immediately.  Effective immediately,
				 * the given network_listener will not be used
				 * by the server and the corresponding
				 * server_listener should not be used by the
				 * network.
				 */

extern void network_shutdown(void);
				/* All network connections should be closed
				 * after flushing as much pending output as
				 * possible, and all listening points should be
				 * closed.  Effective immediately, no
				 * server_handles or server_listeners should be
				 * used by the network and no network_handles
				 * or network_listeners will be used by the
				 * server.  After this call, the server will
				 * never make another call on the network.
				 */

int network_parse_proxy_string(char *command, Stream *new_connection_name, struct sockaddr_storage *new_ai_addr);
                /* Take an HAProxy connection string and parse
                 * it into a new connection_name and sockaddr_storage
                 * for the connection. */

#ifdef USE_TLS
extern int network_handle_is_tls(network_handle);
extern int nlistener_is_tls(const void *);
extern Var tls_connection_info(network_handle);
#endif

typedef void (*network_fd_callback) (int fd, void *data);

extern void network_register_fd(int fd, network_fd_callback readable,
				network_fd_callback writable, void *data);
				/* The file descriptor FD will be selected for
				 * at intervals (whenever the networking module
				 * is doing its own I/O processing).  If FD
				 * selects true for reading and READABLE is
				 * non-zero, then READABLE will be called,
				 * passing FD and DATA.  Similarly for
				 * WRITABLE.
				 */

extern void network_unregister_fd(int fd);
				/* Any existing registration for FD is
				 * forgotten.
				 */

#ifndef HAVE_ACCEPT4
extern int network_set_nonblocking(int fd);
				/* Enable nonblocking I/O on the file
				 * descriptor FD.  Return true iff successful.
				 */
#endif

extern int rewrite_connection_name(network_handle nh, const char *destination, const char *destination_port, const char *source, const char *source_port);
extern int network_name_lookup_rewrite(Objid obj, const char *name);
extern bool network_is_localhost(network_handle nh);
				/* Return true if the network handle's destination IP address
				   is coming from 127.0.0.1 or ::1 */
extern void lock_connection_name_mutex(const network_handle nh);
extern void unlock_connection_name_mutex(const network_handle nh);
extern void increment_nhandle_refcount(const network_handle nh);
extern void decrement_nhandle_refcount(const network_handle nh);
extern uint32_t get_nhandle_refcount(const network_handle nh);
extern uint32_t get_nhandle_refcount(nhandle *h);
extern uint32_t nhandle_refcount(const network_handle nh);

#endif				/* Network_H */
