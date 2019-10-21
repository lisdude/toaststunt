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

/* Multi-user networking protocol implementation for local clients on BSD UNIX
 */

#include <errno.h>		/* EMFILE */
#include "my-socket.h"		/* socket(), AF_UNIX, SOCK_STREAM,
				   * bind(), struct sockaddr, accept(),
				   * shutdown(), connect() */
#include "my-stdio.h"		/* remove() */
#include "my-string.h"		/* strcpy() */
#include <sys/un.h>		/* struct sockaddr_un */
#include "my-unistd.h"		/* close() */

#include "config.h"
#include "log.h"
#include "net_proto.h"
#include "storage.h"
#include "structures.h"
#include "utils.h"

typedef struct listener {
    struct listener *next;
    int fd;
    const char *filename;
} listener;

static listener *all_listeners = 0;

const char *
proto_name(void)
{
    return "BSD single-host";
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

    proto->pocket_size = 1;
    proto->believe_eof = 1;
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
    struct sockaddr_un address;
    int s;
    const char *connect_file;
    listener *l;

    if (desc.type != TYPE_STR)
	return E_TYPE;

    connect_file = desc.v.str;
    s = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s < 0) {
	log_perror("Creating listening socket");
	return E_QUOTA;
    }
    address.sun_family = AF_UNIX;
    strcpy(address.sun_path, connect_file);
    if (bind(s, (struct sockaddr *) &address,
	     sizeof(address.sun_family) + strlen(connect_file))) {
	enum error e = E_QUOTA;

	log_perror("Binding listening socket");
	if (errno == EACCES)
	    e = E_PERM;
	close(s);
	return e;
    }
    l = (listener *)mymalloc(sizeof(listener), M_NETWORK);
    l->next = all_listeners;
    all_listeners = l;
    l->filename = str_dup(connect_file);
    l->fd = s;

    *fd = s;
    *canon = var_ref(desc);
    *name = l->filename;
    return E_NONE;
}

int
proto_listen(int fd)
{
    listen(fd, 5);
    return 1;
}

enum proto_accept_error
proto_accept_connection(int listener_fd, int *read_fd, int *write_fd,
			const char **name)
{
    int fd;
    static struct sockaddr_un address;
    socklen_t addr_length = sizeof(address);

    fd = accept(listener_fd, (struct sockaddr *) &address, &addr_length);
    if (fd < 0) {
	if (errno == EMFILE)
	    return PA_FULL;
	else {
	    log_perror("Accepting new network connection");
	    return PA_OTHER;
	}
    }
    *read_fd = *write_fd = fd;
    *name = "??";
    return PA_OKAY;
}

void
proto_close_connection(int read_fd, int write_fd)
{
    /* read_fd and write_fd are the same, so we only need to deal with one. */
    close(read_fd);
}

void
proto_close_listener(int fd)
{
    listener *l, **ll;

    for (l = all_listeners, ll = &all_listeners; l; ll = &(l->next),
	 l = l->next)
	if (l->fd == fd) {
	    close(l->fd);
	    remove(l->filename);

	    *ll = l->next;
	    free_str(l->filename);
	    myfree(l, M_NETWORK);
	    return;
	}
    errlog("Can't find fd in PROTO_CLOSE_LISTENER!\n");
}
