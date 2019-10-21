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

/* Multi-user networking protocol implementation for local clients on SysV UNIX

 * The protocol for connection establishment works like this:
 *
 *      CLIENT                          SERVER
 *                                      Create SERVER_FIFO (mode 622).
 *                                      Open SERVER_FIFO for reading (O_NDELAY)
 *                                      Wait for input on SERVER_FIFO
 *                                      ...
 *      Create C2S_FIFO (mode 644)
 *        and S2C_FIFO (mode 622) in
 *        some personal directory
 *        (like $HOME).
 *      Open S2C_FIFO for reading
 *        (O_NDELAY).
 *      Open SERVER_FIFO for writing
 *        (no O_NDELAY), write
 *        '\n' C2S_FIFO ' ' S2C_FIFO '\n'
 *        in a single write() call, and
 *        then close SERVER_FIFO.
 *      Open C2S_FIFO for writing       Read C2S_FIFO and S2C_FIFO from
 *        (*no* O_NDELAY).                SERVER_FIFO.
 *                                      Open S2C_FIFO for writing (O_NDELAY)
 *                                        and abort connection on error.
 *                                      Open C2S_FIFO for reading (O_NDELAY)
 *                                        and abort connection on error.
 *      Unlink C2S_FIFO and S2C_FIFO
 *        for privacy; their names
 *        are no longer needed.
 *                                      ...
 *                                      Close SERVER_FIFO and unlink it.
 *
 * It is important that the client do the above actions in the given order;
 * no other sequence (with the sole exception of the timing of the opening
 * and closing of SERVER_FIFO) will accomplish a successful connection.
 *
 * The unlinking of the client's two FIFOs makes it impossible for someone
 * else thereafter to either spoof input or divert output in the name of the
 * client.  There is, of course, a brief window during which the names exist
 * and a spoofer could intervene, but this appears to be unavoidable.
 *
 * The extra newline at the beginning of the client's connection request on
 * SERVER_FIFO is there to make the request parsable even in the presence of
 * random extra garbage already in the FIFO, thus thwarting a denial-of-service
 * attack on the server.
 *
 * The server makes use of information about the client's FIFOs (i.e., their
 * owner) to identify the connection for humans.  For example, the user name
 * appears in the server's log and is available to wizards via the MOO
 * function connection_name(player).  Caveat Emptor.
 */

#include <errno.h>		/* EMFILE */
#include "my-fcntl.h"		/* open(), O_RDONLY, O_WRONLY, NONBLOCK_FLAG */
#include <pwd.h>		/* struct passwd, getpwuid() */
#include "my-stat.h"		/* S_IFIFO, fstat(), mkfifo() */
#include "my-stdio.h"		/* remove() */
#include "my-stdlib.h"		/* exit() */
#include "my-unistd.h"		/* chmod(), close(), pipe(), read(), write() */

#include "config.h"
#include "list.h"
#include "log.h"
#include "net_multi.h"
#include "net_proto.h"
#include "storage.h"
#include "streams.h"
#include "utils.h"

enum state {
    RejectLine, GetC2S, GetS2C, Accepting
};

typedef struct listener {
    struct listener *next;
    int fifo, pseudo_client;
    const char *filename;
    enum state state;
    char c2s[1001], s2c[1001];
    int ptr;			/* current index in c2s or s2c */
} listener;

static listener *all_listeners = 0;

const char *
proto_name(void)
{
    return "FIFO";
}

const char *
proto_usage_string(void)
{
    return "[server-connect-file]";
}

int
proto_initialize(struct proto *proto, Var * desc, int argc, char **argv)
{
    const char *connect_file = DEFAULT_CONNECT_FILE;

    proto->pocket_size = 2;
#if POSIX_NONBLOCKING_WORKS
    /* With POSIX-style nonblocking, we'll win */
    proto->believe_eof = 1;
#else
    proto->believe_eof = 0;
#endif
    proto->eol_out_string = "\n";

    if (argc > 1)
	return 0;
    else if (argc == 1) {
	connect_file = argv[0];
    }
    desc->type = TYPE_STR;
    desc->v.str = str_dup(connect_file);
    return 1;
}

enum error
proto_make_listener(Var desc, int *fd, Var * canon, const char **name)
{
    char buffer[1024];
    const char *connect_file;
    int fifo, pseudo_client;
    listener *l;

    if (desc.type != TYPE_STR)
	return E_TYPE;

    connect_file = desc.v.str;
    if (mkfifo(connect_file, 0600) < 0) {
	sprintf(buffer, "Creating listening FIFO `%s'", connect_file);
	log_perror(buffer);
	return E_QUOTA;
    } else if ((fifo = open(connect_file, O_RDONLY | NONBLOCK_FLAG)) < 0) {
	log_perror("Opening listening FIFO");
	return E_QUOTA;
    } else if ((pseudo_client = open(connect_file, O_WRONLY)) < 0) {
	log_perror("Opening pseudo-client");
	return E_QUOTA;
    } else if (!network_set_nonblocking(fifo)) {
	log_perror("Setting listening FIFO non-blocking");
	return E_QUOTA;
    }
    l = (listener *)mymalloc(sizeof(listener), M_NETWORK);
    l->next = all_listeners;
    all_listeners = l;
    l->filename = str_dup(connect_file);
    l->fifo = fifo;
    l->pseudo_client = pseudo_client;
    l->state = GetC2S;
    l->ptr = 0;

    *fd = fifo;
    *canon = var_ref(desc);
    *name = l->filename;
    return E_NONE;
}

static listener *
find_listener(int fd)
{
    listener *l;

    for (l = all_listeners; l; l = l->next)
	if (l->fifo == fd)
	    return l;

    return 0;
}

int
proto_listen(int fd)
{
    listener *l = find_listener(fd);

    if (l) {
	if (chmod(l->filename, 0622) < 0) {
	    log_perror("Making listening FIFO writable");
	    return 0;
	}
	return 1;
    }
    errlog("Can't find FIFO in PROTO_LISTEN!");
    return 0;
}

enum proto_accept_error
proto_accept_connection(int listener_fd, int *read_fd, int *write_fd,
			const char **name)
{
    /* There is input available on listener_fd; read up to 1K of it and try
     * to parse a line like this from it:
     *          <c2s-path-name> <space> <s2c-path-name> <newline>
     * Because it's impossible in System V and can be difficult in POSIX to do
     * otherwise, we assume that the maximum length of a path-name is 1000
     * bytes.  In fact, because System V and POSIX only guarantee that 512
     * bytes can be written atomically on a pipe or FIFO, and since the client
     * has to get two path-names, two newlines, and a space atomically into the
     * FIFO, it really ought not use names longer than about 250 bytes each.
     * Of course, in practice, the names will likely never exceed 100 bytes...
     */
    listener *l = find_listener(listener_fd);
    struct stat st1, st2;
    struct passwd *pw;

    if (l->state != Accepting) {
	int got_one = 0;

	while (!got_one) {
	    char c;

	    if (read(l->fifo, &c, 1) != 1)
		break;

	    switch (l->state) {
	    case RejectLine:
		if (c == '\n') {
		    l->state = GetC2S;
		    l->ptr = 0;
		}
		break;

	    case GetC2S:
		if (c == ' ') {
		    l->c2s[l->ptr] = '\0';
		    l->state = GetS2C;
		    l->ptr = 0;
		} else if (c == '\n') {
		    if (l->ptr != 0)
			errlog("Missing FIFO name(s) on listening FIFO\n");
		    l->ptr = 0;
		} else if (l->ptr == 1000) {
		    errlog("Overlong line on server FIFO\n");
		    l->state = RejectLine;
		} else
		    l->c2s[l->ptr++] = c;
		break;

	    case GetS2C:
		if (c == '\n') {
		    l->s2c[l->ptr] = '\0';
		    l->state = Accepting;
		    l->ptr = 0;
		    got_one = 1;
		} else if (c == ' ' || l->ptr == 1000) {
		    errlog("Overlong or malformed line on listening FIFO\n");
		    l->state = RejectLine;
		} else
		    l->s2c[l->ptr++] = c;
		break;

	    default:
		panic_moo("Can't happen in proto_accept_connection()");
	    }
	}

	if (!got_one) {
	    errlog("Unproductive call to proto_accept_connection()\n");
	    return PA_OTHER;
	}
    }
    if ((*write_fd = open(l->s2c, O_WRONLY | NONBLOCK_FLAG)) < 0) {
	log_perror("Failed to open server->client FIFO");
	if (errno == EMFILE)
	    return PA_FULL;
	else {
	    l->state = GetC2S;
	    return PA_OTHER;
	}
    }
    if ((*read_fd = open(l->c2s, O_RDONLY | NONBLOCK_FLAG)) < 0) {
	log_perror("Failed to open server->client FIFO");
	close(*write_fd);
	if (errno == EMFILE)
	    return PA_FULL;
	else {
	    l->state = GetC2S;
	    return PA_OTHER;
	}
    }
    l->state = GetC2S;

    if (fstat(*read_fd, &st1) < 0 || fstat(*write_fd, &st2) < 0) {
	log_perror("Statting client FIFOs");
	return PA_OTHER;
    }
    if (st1.st_mode & S_IFMT != S_IFIFO
	|| st2.st_mode & S_IFMT != S_IFIFO
	|| st1.st_uid != st2.st_uid) {
	close(*read_fd);
	close(*write_fd);
	errlog("Bogus FIFO names: \"%s\" and \"%s\"\n", l->c2s, l->s2c);
	return PA_OTHER;
    }
    pw = getpwuid(st1.st_uid);
    if (pw)
	*name = pw->pw_name;
    else {
	static char buffer[20];

	sprintf(buffer, "User #%d", (int) st1.st_uid);
	*name = buffer;
    }

    return PA_OKAY;
}

void
proto_close_connection(int read_fd, int write_fd)
{
    write(write_fd, "\0", 1);	/* Tell client we're shutting down. */
    close(read_fd);
    close(write_fd);
}

void
proto_close_listener(int fd)
{
    listener *l, **ll;

    for (l = all_listeners, ll = &all_listeners; l; ll = &(l->next),
	 l = l->next)
	if (l->fifo == fd) {
	    remove(l->filename);
	    close(l->fifo);
	    close(l->pseudo_client);

	    *ll = l->next;
	    free_str(l->filename);
	    myfree(l, M_NETWORK);
	    return;
	}
    errlog("Can't find fd in PROTO_CLOSE_LISTENER!\n");
}
