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

/* Multiplexing wait implementation using the BSD UNIX select() system call */

#include <errno.h>		/* errno */
#include "my-string.h"		/* bzero() or memset(), used in FD_ZERO */
#include "my-sys-time.h"	/* select(), struct timeval */
#include "my-types.h"		/* fd_set, FD_ZERO(), FD_SET(), FD_ISSET() */

#include "log.h"
#include "net_mplex.h"

static fd_set input, output;
static int max_descriptor;

void
mplex_clear(void)
{
    FD_ZERO(&input);
    FD_ZERO(&output);
    max_descriptor = -1;
}

void
mplex_add_reader(int fd)
{
    FD_SET(fd, &input);
    if (fd > max_descriptor)
	max_descriptor = fd;
}

void
mplex_add_writer(int fd)
{
    FD_SET(fd, &output);
    if (fd > max_descriptor)
	max_descriptor = fd;
}

int
mplex_wait(unsigned timeout)
{
    struct timeval tv;
    int n;

    tv.tv_sec = timeout / 1000000;
    tv.tv_usec = timeout % 1000000;

    n = select(max_descriptor + 1, &input, &output, 0, &tv);

    if (n < 0) {
	if (errno != EINTR)
	    log_perror("Waiting for network I/O");
	return 1;
    } else
	return (n == 0);
}

int
mplex_is_readable(int fd)
{
    return FD_ISSET(fd, &input);
}

int
mplex_is_writable(int fd)
{
    return FD_ISSET(fd, &output);
}
