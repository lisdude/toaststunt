/******************************************************************************
  Copyright (c) 1994, 1995, 1996 Xerox Corporation.  All rights reserved.
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

/* This module provides IP host name lookup with timeouts.  Because
 * longjmps out of name lookups corrupt some UNIX name lookup modules, this
 * module uses a subprocess to do the name lookup.  On any failure, the
 * subprocess is restarted.
 */

#include "options.h"

#if NETWORK_PROTOCOL == NP_TCP	/* Skip almost entire file otherwise... */

#include "my-signal.h"
#include "my-stdlib.h"
#include "my-unistd.h"
#include "my-inet.h"		/* inet_addr() */
#include "my-in.h"		/* struct sockaddr_in, INADDR_ANY, htons(),
				   * htonl(), ntohl(), struct in_addr */
#include <netdb.h>		/* struct hostent, gethostbyaddr() */
#include "my-socket.h"		/* AF_INET */
#include "my-wait.h"
#include "my-string.h"
#include <errno.h>

#include "config.h"
#include "log.h"
#include "server.h"
#include "storage.h"
#include "timers.h"

/******************************************************************************
 * Utilities
 *****************************************************************************/

static pid_t
spawn_pipe(void (*child_proc) (int to_parent, int from_parent),
	   int *to_child, int *from_child)
{
    int pipe_to_child[2], pipe_from_child[2];
    pid_t pid;

    if (pipe(pipe_to_child) < 0) {
	log_perror("SPAWNING: Couldn't create first pipe");
    } else if (pipe(pipe_from_child) < 0) {
	log_perror("SPAWNING: Couldn't create second pipe");
	close(pipe_to_child[0]);
	close(pipe_to_child[1]);
    } else if ((pid = fork()) < 0) {
	log_perror("SPAWNING: Couldn't fork middleman");
	close(pipe_to_child[0]);
	close(pipe_to_child[1]);
	close(pipe_from_child[0]);
	close(pipe_from_child[1]);
    } else if (pid != 0) {	/* parent */
	int status;

	close(pipe_to_child[0]);
	close(pipe_from_child[1]);
	*to_child = pipe_to_child[1];
	*from_child = pipe_from_child[0];

	wait(&status);	/* wait for middleman to die */
	if (status != 0) {
	    errlog("SPAWNING: Middleman died with status %d!\n", status);
	    close(pipe_to_child[1]);
	    close(pipe_from_child[0]);
	} else if (read(*from_child, &pid, sizeof(pid)) != sizeof(pid)) {
	    errlog("SPAWNING: Bad read() for pid\n");
	    close(pipe_to_child[1]);
	    close(pipe_from_child[0]);
	} else {
	    return pid;
	}
    } else {			/* middleman */
	close(pipe_to_child[1]);
	close(pipe_from_child[0]);
	if ((pid = fork()) < 0) {
	    log_perror("SPAWNING: Couldn't fork child");
	    exit(1);
	} else if (pid != 0) {	/* still the middleman */
	    write(pipe_from_child[1], &pid, sizeof(pid));
	    exit(0);
	} else {		/* finally, the child */
	    (*child_proc) (pipe_from_child[1], pipe_to_child[0]);
	    exit(0);
	}
    }

    return 0;
}

static void
ensure_buffer(char **buffer, int *buflen, int len)
{
    if (len > *buflen) {
	if (*buffer)
	    myfree(*buffer, M_STRING);
	*buflen = len;
	*buffer = (char *)mymalloc(len, M_STRING);
    }
}

static int
robust_read(int fd, void *buffer, int len)
{
    int count;

    do {
	count = read(fd, buffer, len);
    } while (count == -1 && errno == EINTR);

    return count;
}

/******************************************************************************
 * Data structures and types used by more than one process.
 *****************************************************************************/

struct request {
    enum {
	REQ_NAME_FROM_ADDR, REQ_ADDR_FROM_NAME
    } kind;
    unsigned timeout;
    union {
	unsigned length;
	struct sockaddr_in address;
    } u;
};

/******************************************************************************
 * Code that runs in the lookup process.
 *****************************************************************************/

static void
timeout_proc(Timer_ID id, Timer_Data data)
{
    _exit(1);
}

static void
lookup(int to_intermediary, int from_intermediary)
{
    struct request req;
    static char *buffer = 0;
    static int buflen = 0;
    Timer_ID id;
    struct hostent *e;
    char hbuf[NI_MAXHOST];

    set_server_cmdline("(MOO name-lookup slave)");
    /* Read requests and do them.  Before each, we set a timer.  If it
       expires, we exit (in timeout_proc, above).  The intermediary will
       restart us in that event. */
    for (;;) {
	if (robust_read(from_intermediary, &req, sizeof(req)) != sizeof(req))
	    _exit(1);
	if (req.kind == request::REQ_ADDR_FROM_NAME) {
	    ensure_buffer(&buffer, &buflen, req.u.length + 1);
	    if (robust_read(from_intermediary, buffer, req.u.length)
		!= req.u.length)
		_exit(1);
	    buffer[req.u.length] = 0;
	    id = set_timer(req.timeout, timeout_proc, 0);
	    e = gethostbyname(buffer);
	    cancel_timer(id);
	    if (e && e->h_length == sizeof(uint32_t))
		write(to_intermediary, e->h_addr_list[0], e->h_length);
	    else {
		uint32_t addr;

		addr = inet_addr(buffer);
		write(to_intermediary, &addr, sizeof(addr));
	    }
    } else {
        const char *host_name;
        int length;
        id = set_timer(req.timeout, timeout_proc, 0);
        if (getnameinfo((struct sockaddr *)&req.u.address, sizeof(req.u.address), hbuf, sizeof(hbuf), NULL, 0, 0) == 0)
            host_name = hbuf;
        else
            host_name = "";
        cancel_timer(id);
        length = strlen(host_name);
        write(to_intermediary, &length, sizeof(length));
        write(to_intermediary, host_name, length);
    }
    }
}

/******************************************************************************
 * Code that runs in the intermediary process.
 *****************************************************************************/

static int to_lookup, from_lookup;
static pid_t lookup_pid;

static void
restart_lookup(void)
{
    if (lookup_pid) {
	kill(lookup_pid, SIGKILL);
	close(to_lookup);
	close(from_lookup);
	oklog("NAME_LOOKUP: Killing old lookup process ...\n");
    }
    lookup_pid = spawn_pipe(lookup, &to_lookup, &from_lookup);
    if (lookup_pid)
	oklog("NAME_LOOKUP: Started new lookup process\n");
    else
	errlog("NAME_LOOKUP: Can't spawn lookup process; "
	       "will try again later...\n");
}

static void
intermediary(int to_server, int from_server)
{
    struct request req;
    static char *buffer = 0;
    static int buflen = 0;
    int len;
    uint32_t addr;

    set_server_cmdline("(MOO name-lookup master)");
    signal(SIGPIPE, SIG_IGN);
    restart_lookup();
    for (;;) {
	if (robust_read(from_server, &req, sizeof(req)) != sizeof(req))
	    _exit(1);
	if (req.kind == request::REQ_ADDR_FROM_NAME) {
	    ensure_buffer(&buffer, &buflen, req.u.length);
	    if (robust_read(from_server, buffer, req.u.length) != req.u.length)
		_exit(1);
	}
	if (!lookup_pid)	/* Restart lookup if it's died */
	    restart_lookup();
	if (lookup_pid) {	/* Only try to deal with lookup if alive */
	    write(to_lookup, &req, sizeof(req));
	    if (req.kind == request::REQ_ADDR_FROM_NAME) {
		write(to_lookup, buffer, req.u.length);
		if (robust_read(from_lookup, &addr, sizeof(addr))
		    != sizeof(addr)) {
		    restart_lookup();
		    addr = 0;
		}
		write(to_server, &addr, sizeof(addr));
	    } else {
		if (robust_read(from_lookup, &len, sizeof(len))
		    != sizeof(len)) {
		    restart_lookup();
		    len = 0;
		} else {
		    ensure_buffer(&buffer, &buflen, len);
		    if (len > 0
			&& robust_read(from_lookup, buffer, len) != len) {
			restart_lookup();
			len = 0;
		    }
		}
		write(to_server, &len, sizeof(len));
		if (len > 0)
		    write(to_server, buffer, len);
	    }
	} else {		/* Lookup dead and wouldn't restart ... */
	    int failure = 0;

	    write(to_server, &failure, sizeof(failure));
	}
    }
}


/******************************************************************************
 * Code that runs in the server process.
 *****************************************************************************/

static int to_intermediary, from_intermediary;
static int dead_intermediary = 0;


int
initialize_name_lookup(void)
{
    return spawn_pipe(intermediary, &to_intermediary, &from_intermediary);
}

static void
abandon_intermediary(const char *prefix)
{
    errlog("LOOKUP_NAME: %s; presumed dead...\n", prefix);
    dead_intermediary = 1;
    close(to_intermediary);
    close(from_intermediary);
}

const char *
lookup_name_from_addr(struct sockaddr_in *addr, unsigned timeout)
{
    struct request req;
    static char *buffer = 0;
    static int buflen = 0;
    int len;

    if (!dead_intermediary) {
	req.kind = request::REQ_NAME_FROM_ADDR;
	req.timeout = timeout;
	req.u.address = *addr;
	if (write(to_intermediary, &req, sizeof(req)) != sizeof(req))
	    abandon_intermediary("LOOKUP_NAME: Write to intermediary failed");
	else if (robust_read(from_intermediary, &len, sizeof(len))
		 != sizeof(len))
	    abandon_intermediary("LOOKUP_NAME: Read from intermediary failed");
	else if (len != 0) {
	    ensure_buffer(&buffer, &buflen, len + 21);  // 21 to ensure enough space for the numeric IP
	    if (robust_read(from_intermediary, buffer, len) != len)
		abandon_intermediary("LOOKUP_NAME: "
				   "Data-read from intermediary failed");
	    else {
        uint32_t a = ntohl(addr->sin_addr.s_addr);
        sprintf((char *)buffer+len, " [%u.%u.%u.%u]", (unsigned) (a >> 24) & 0xff, (unsigned) (a >> 16) & 0xff, (unsigned) (a >> 8) & 0xff, (unsigned) a & 0xff);

//        buffer[len] = '\0';
		return buffer;
	    }
	}
    }
    /* Either the intermediary is presumed dead, or else it failed to produce
     * a name; in either case, we must fall back on a the default, dotted-
     * decimal notation.
     */

    {
	static char decimal[20];
	uint32_t a = ntohl(addr->sin_addr.s_addr);

	sprintf(decimal, "%u.%u.%u.%u",
		(unsigned) (a >> 24) & 0xff, (unsigned) (a >> 16) & 0xff,
		(unsigned) (a >> 8) & 0xff, (unsigned) a & 0xff);
	return decimal;
    }
}

uint32_t
lookup_addr_from_name(const char *name, unsigned timeout)
{
    struct request req;
    uint32_t addr = 0;

    if (dead_intermediary) {
	/* Numeric addresses should always work... */
	addr = inet_addr(name);
    } else {
	req.kind = request::REQ_ADDR_FROM_NAME;
	req.timeout = timeout;
	req.u.length = strlen(name);
	if (write(to_intermediary, &req, sizeof(req)) != sizeof(req)
	    || write(to_intermediary, name, req.u.length) != req.u.length)
	    abandon_intermediary("LOOKUP_ADDR: Write to intermediary failed");
	else if (robust_read(from_intermediary, &addr, sizeof(addr))
		 != sizeof(addr))
	    abandon_intermediary("LOOKUP_ADDR: Read from intermediary failed");
    }

    return addr == 0xffffffff ? 0 : addr;
}

#endif				/* NETWORK_PROTOCOL == NP_TCP */
