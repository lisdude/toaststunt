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

#include <float.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

#include "config.h"
#include "log.h"
#include "storage.h"
#include "streams.h"

Stream *
new_stream(int size)
{
    Stream *s = (Stream *)mymalloc(sizeof(Stream), M_STREAM);

    if (size < 1)
        size = 1;

    s->buffer = (char *)mymalloc(size, M_STREAM);
    s->buflen = size;
    s->current = 0;

    return s;
}

size_t stream_alloc_maximum = 0;

static int allow_stream_exceptions = 0;

void
enable_stream_exceptions()
{
    ++allow_stream_exceptions;
}

void
disable_stream_exceptions()
{
    --allow_stream_exceptions;
}

static void
grow(Stream * s, int newlen, int need)
{
    if (allow_stream_exceptions > 0) {
        if (newlen > stream_alloc_maximum) {
            if (s->current + need < stream_alloc_maximum)
                newlen = stream_alloc_maximum;
            else
                throw stream_too_big();
        }
    }
    s->buffer = (char *)myrealloc(s->buffer, newlen, M_STREAM);
    s->buflen = newlen;
}

void
stream_add_char(Stream * s, char c)
{
    if (s->current + 1 >= s->buflen)
        grow(s, s->buflen * 2, 1);

    s->buffer[s->current++] = c;
}

void
stream_delete_char(Stream * s)
{
    if (s->current > 0)
        s->current--;
}

void
stream_add_string(Stream * s, const char *string)
{
    int len = strlen(string);

    if (s->current + len >= s->buflen) {
        int newlen = s->buflen * 2;

        if (newlen <= s->current + len)
            newlen = s->current + len + 1;
        grow(s, newlen, len);
    }
    strcpy(s->buffer + s->current, string);
    s->current += len;
}

void
stream_printf(Stream * s, const char *fmt, ...)
{
    va_list args, pargs;
    int len;

    va_start(args, fmt);

    va_copy(pargs, args);
    len = vsnprintf(s->buffer + s->current, s->buflen - s->current, fmt, pargs);
    va_end(pargs);

    if (s->current + len >= s->buflen) {
        int newlen = s->buflen * 2;

        if (newlen <= s->current + len)
            newlen = s->current + len + 1;
        grow(s, newlen, len);
        len = vsnprintf(s->buffer + s->current, s->buflen - s->current, fmt, args);
    }

    va_end(args);
    s->current += len;
}

void
free_stream(Stream * s)
{
    myfree(s->buffer, M_STREAM);
    myfree(s, M_STREAM);
}

char *
reset_stream(Stream * s)
{
    s->buffer[s->current] = '\0';
    s->current = 0;
    return s->buffer;
}

char *
stream_contents(Stream * s)
{
    s->buffer[s->current] = '\0';
    return s->buffer;
}

int
stream_length(Stream * s)
{
    return s->current;
}
