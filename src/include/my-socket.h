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

#include "config.h"

#if SYS_SOCKET_H_NEEDS_HELP
/* Some systems aren't sufficiently careful about headers including the other
 * headers that they depend upon...
 */
#include "my-types.h"
#endif

#include <sys/socket.h>

#if NDECL_ACCEPT
extern int accept(int, struct sockaddr *, int *);
extern int listen(int, int);
extern int setsockopt(int, int, int, const char *, int);
extern int socket(int, int, int);
#endif

#if NDECL_BIND
extern int bind(int, struct sockaddr *, int);
extern int getsockname(int, struct sockaddr *, int *);
extern int connect(int, struct sockaddr *, int);
#endif

#if NDECL_SHUTDOWN
extern int shutdown(int, int);
#endif
