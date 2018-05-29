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

/* Multiplexing wait implementation using fstat (only works for FIFOs) */

#include "my-types.h"
#include "my-stat.h"
#include "my-unistd.h"		/* sleep() */

#include "net_mplex.h"
#include "options.h"
#include "storage.h"

#if (NETWORK_PROTOCOL != NP_LOCAL) || (NETWORK_STYLE != NS_SYSV)
 #error Configuration Error: this code can only be used with the FIFO protocol
#endif

typedef enum {
    Read, Write
} Direction;

typedef struct {
    int fd;
    Direction dir;
} Port;

static Port *ports = 0;
static int num_ports = 0;
static int max_ports = 0;

static char *readable = 0;
static char *writable = 0;
static int rw_size = 0;

void
mplex_clear(void)
{
    int i;

    num_ports = 0;
    for (i = 0; i < rw_size; i++)
	readable[i] = writable[i] = 0;
}

static void
add_common(int fd, Direction dir)
{
    if (fd >= rw_size) {	/* Grow readable/writable arrays */
	int new_size = (fd + 9) / 10 * 10 + 1;
	char *new_readable = (char *) mymalloc(new_size * sizeof(char),
					       M_NETWORK);
	char *new_writable = (char *) mymalloc(new_size * sizeof(char),
					       M_NETWORK);
	int i;

	for (i = 0; i < new_size; i++)
	    new_readable[i] = new_writable[i] = 0;

	if (readable != 0) {
	    myfree(readable, M_NETWORK);
	    myfree(writable, M_NETWORK);
	}
	readable = new_readable;
	writable = new_writable;
	rw_size = new_size;
    }
    if (num_ports == max_ports) {	/* Grow ports array */
	int new_max = max_ports + 10;
	Port *new_ports = (Port *)mymalloc(new_max * sizeof(Port), M_NETWORK);
	int i;

	for (i = 0; i < max_ports; i++)
	    new_ports[i] = ports[i];

	if (ports != 0)
	    myfree(ports, M_NETWORK);

	ports = new_ports;
	max_ports = new_max;
    }
    ports[num_ports].fd = fd;
    ports[num_ports++].dir = dir;
}

void
mplex_add_reader(int fd)
{
    add_common(fd, Read);
}

void
mplex_add_writer(int fd)
{
    add_common(fd, Write);
}

int
mplex_wait(unsigned timeout)
{
    struct stat st;
    int i, got_one = 0;

    while (1) {
	for (i = 0; i < num_ports; i++) {
	    fstat(ports[i].fd, &st);
	    if (ports[i].dir == Read) {
		if (st.st_size > 0) {
		    readable[ports[i].fd] = 1;
		    got_one = 1;
		}
	    } else {
		/* The following is a bit of unavoidable crockery.  Since we
		 * don't know how much the server wants to write, and since
		 * writes of less than the size of a FIFO buffer are atomic,
		 * we can't really tell whether or not a write() will be
		 * successful.  This approximation keeps us from ever
		 * busy-waiting.
		 */
		if (st.st_size == 0) {
		    writable[ports[i].fd] = 1;
		    got_one = 1;
		}
	    }
	}

	timeout -= 1000000;
	if (got_one)
	    break;
	else if (timeout > 0)
	    sleep(1);
    }

    return !got_one;
}

int
mplex_is_readable(int fd)
{
    return (fd < rw_size) ? readable[fd] : 0;
}

int
mplex_is_writable(int fd)
{
    return (fd < rw_size) ? writable[fd] : 0;
}
