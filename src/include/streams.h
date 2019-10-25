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

#ifndef Stream_h
#define Stream_h 1

#include <stdexcept>

#include "config.h"

typedef struct {
    char *buffer;
    int buflen;
    int current;
} Stream;

extern Stream *new_stream(int size);
extern void stream_add_char(Stream *, char);
extern void stream_delete_char(Stream *);
extern void stream_add_string(Stream *, const char *);
extern void stream_printf(Stream *, const char *,...);
extern void free_stream(Stream *);
extern char *stream_contents(Stream *);
extern char *reset_stream(Stream *);
extern int stream_length(Stream *);

class stream_too_big: public std::exception
{
public:

    stream_too_big() throw() {}

    ~stream_too_big() throw() override {}

    const char* what() const throw() override {
        return "stream too big";
    }
};

/*
 * Calls to enable_stream_exceptions() and disable_stream_exceptions()
 * must be paired and nest properly.
 *
 * If enable_stream_exceptions() is in effect, then, upon any attempt
 * to grow a stream beyond stream_alloc_maximum bytes, a
 * stream_too_big exception will be raised.
 */
extern void enable_stream_exceptions();
extern void disable_stream_exceptions();

extern size_t stream_alloc_maximum;

#endif
