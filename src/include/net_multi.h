/******************************************************************************
  Copyright (c) 1995, 1996 Xerox Corporation.  All rights reserved.
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

#ifndef Net_Multi_H
#define Net_Multi_H 1

/* Extra networking facilities available only for the multi-user networking
 * configurations.
 */

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

#include "streams.h"
#include "network.h"

extern void rewrite_connection_name(network_handle nh, const char *destination, const char *destination_port, const char *source, const char *source_port);
extern int network_name_lookup_rewrite(Objid obj, const char *name);
extern int network_is_localhost(network_handle nh);

#endif				/* !Net_Multi_H */
