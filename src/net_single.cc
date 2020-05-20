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

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include "config.h"
#include "log.h"
#include "network.h"
#include "server.h"
#include "streams.h"
#include "structures.h"
#include "utils.h"

static enum {
    STATE_OPEN, STATE_CLOSED
} state = STATE_CLOSED;
static int listening = 0;
static server_listener slistener;
static int binary = 0;

const char *
network_protocol_name(void)
{
    return "single-user";
}

const char *
network_usage_string(void)
{
    return "";
}

int
network_initialize(int argc, char **argv, Var * desc)
{
    *desc = zero;
    if (argc != 0)
        return 0;
    else
        return 1;
}

enum error
network_make_listener(server_listener sl, Var desc, network_listener * nl, const char **name)
{
    if (listening)
        return E_PERM;

    listening = 1;
    slistener = sl;
    nl->ptr = 0;
    *name = "standard input";
    return E_NONE;
}

int
network_listen(network_listener nl)
{
    return 1;
}

int
network_send_line(network_handle nh, const char *line, int flush_ok, bool send_newline)
{
    printf(send_newline ? "%s\n" : "%s", line);
    fflush(stdout);

    return 1;
}

int
network_send_bytes(network_handle nh, const char *buffer, int buflen,
                   int flush_ok)
{
    /* Cast to (void *) to discard `const' on some systems */
    fwrite((void *) buffer, sizeof(char), buflen, stdout);
    fflush(stdout);

    return 1;
}

int
network_buffered_output_length(network_handle nh)
{
    return 0;
}

const char *
network_connection_name(network_handle nh)
{
    return "standard input";
}

void
network_set_connection_binary(network_handle nh, int do_binary)
{
    binary = do_binary;
}

#define NETWORK_CO_TABLE(DEFINE, nh, value, _)
/* No network-specific connection options */


void
network_close(network_handle nh)
{
    state = STATE_CLOSED;
}

void
network_close_listener(network_listener nl)
{
    listening = 0;
}

void
network_shutdown(void)
{
    int flags;

    if ((flags = fcntl(0, F_GETFL)) < 0
            || fcntl(0, F_SETFL, flags & ~NONBLOCK_FLAG) < 0)
        log_perror("Setting standard input blocking again");
}

static int input_suspended = 0;

void
network_suspend_input(network_handle nh)
{
    input_suspended = 1;
}

void
network_resume_input(network_handle nh)
{
    input_suspended = 0;
}

int
network_process_io(int timeout)
{
    network_handle nh;
    static server_handle sh;
    static Stream *s = 0;
    char buffer[1024];
    int count;
    char *ptr, *end;
    int got_some = 0;

    if (s == 0) {
        int flags;

        s = new_stream(1000);

        if ((flags = fcntl(0, F_GETFL)) < 0
                || fcntl(0, F_SETFL, flags | NONBLOCK_FLAG) < 0) {
            log_perror("Setting standard input non-blocking");
            return 0;
        }
    }
    switch (state) {
        case STATE_CLOSED:
            if (listening) {
                sh = server_new_connection(slistener, nh, 0);
                state = STATE_OPEN;
                got_some = 1;
            } else if (timeout > 0)
                sleep(timeout / 1000000);
            break;

        case STATE_OPEN:
            for (;;) {
                while (!input_suspended
                        && (count = read(0, buffer, sizeof(buffer))) > 0) {
                    got_some = 1;
                    if (binary) {
                        stream_add_raw_bytes_to_binary(s, buffer, count);
                        server_receive_line(sh, reset_stream(s), 0);
                    } else
                        for (ptr = buffer, end = buffer + count;
                                ptr < end;
                                ptr++) {
                            unsigned char c = *ptr;

                            if (isgraph(c) || c == ' ' || c == '\t')
                                stream_add_char(s, c);
#ifdef INPUT_APPLY_BACKSPACE
                            else if (c == 0x08 || c == 0x7F)
                                stream_delete_char(s);
#endif
                            else if (c == '\n')
                                server_receive_line(sh, reset_stream(s), 0);
                        }
                }

                if (got_some || timeout <= 0)
                    goto done;

                sleep(1);
                timeout -= 1000000;
            }
    }

done:
    return got_some;
}
