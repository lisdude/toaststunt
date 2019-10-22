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

/* This describes the complete set of procedures that a multi-user network
 * protocol implementation must provide.
 */

#ifndef Net_Proto_H
#define Net_Proto_H 1

#include "config.h"
#include "options.h"
#include "structures.h"

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

extern const char *proto_name(void);
				/* Returns a string naming the protocol. */

extern const char *proto_usage_string(void);
				/* Returns a string giving the syntax of any
				 * extra, protocol-specific command-line
				 * arguments, such as a port number.
				 */

extern int proto_initialize(struct proto *proto, Var * desc,
			    int argc, char **argv);
				/* ARGC and ARGV refer to just the protocol-
				 * specific command-line arguments, if any,
				 * which always come after any protocol-
				 * independent args.  Returns true iff those
				 * arguments were valid.  On success, all of
				 * the fields of PROTO should be filled in with
				 * values appropriate for the protocol, and
				 * *DESC should be a MOO value to pass to
				 * proto_make_listener() in order to create the
				 * server's initial listening point.
				 */

extern enum error proto_make_listener(Var desc, int *fd, Var * canon,
				      const char **name);
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
				 * server's call to proto_listen() marks the
				 * time by which the server must start
				 * accepting connections.
				 */

extern int proto_listen(int fd);
				/* Prepare for accepting connections on the
				 * given file descriptor, returning true if
				 * successful.  FD was returned by a call to
				 * proto_make_listener().
				 */


enum proto_accept_error {
    PA_OKAY, PA_FULL, PA_OTHER
};

extern enum proto_accept_error
 proto_accept_connection(int listener_fd,
			 int *read_fd, int *write_fd,
			 const char **name);
				/* Accept a new connection on LISTENER_FD,
				 * returning PA_OKAY if successful, PA_FULL if
				 * unsuccessful only because there aren't
				 * enough file descriptors available, and
				 * PA_OTHER for other failures (in which case a
				 * message should have been output to the
				 * server log).  LISTENER_FD was returned by a
				 * call to proto_make_listener().  On
				 * successful return, *READ_FD and *WRITE_FD
				 * should be file descriptors on which input
				 * and output for the new connection can be
				 * done, and *NAME should be a human-readable
				 * string identifying this connection.
				 */

#ifdef OUTBOUND_NETWORK

extern enum error proto_open_connection(Var arglist,
					int *read_fd, int *write_fd,
					const char **local_name,
					const char **remote_name);
				/* The given MOO arguments should be used as a
				 * specification of a remote network connection
				 * to be opened.  If the arguments are OK for
				 * this protocol and the connection is success-
				 * fully made, then *READ_FD and *WRITE_FD
				 * should be set as proto_accept_connection()
				 * does, *LOCAL_NAME a human-readable string
				 * naming the local endpoint of the connection,
				 * *REMOTE_NAME a string naming the remote
				 * endpoint, and E_NONE returned.  Otherwise,
				 * an appropriate error should be returned.
				 */

#endif				/* OUTBOUND_NETWORK */

extern void proto_close_connection(int read_fd, int write_fd);
				/* Close the given file descriptors, which were
				 * returned by proto_accept_connection(),
				 * performing whatever extra clean-ups are
				 * required by the protocol.
				 */

extern void proto_close_listener(int fd);
				/* Close FD, which was returned by a call to
				 * proto_make_listener(), performing whatever
				 * extra clean-ups are required by the
				 * protocol.
				 */

#endif				/* !Net_Proto_H */
