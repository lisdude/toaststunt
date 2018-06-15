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
#include "my-stdarg.h"
#include "my-string.h"
#include "my-stdio.h"

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

static const char *
itoa(int n, int radix)
{
    if (n == 0)			/* zero produces "" below. */
	return "0";
    else if (n == -2147483647 - 1)	/* min. integer won't work below. */
	switch (radix) {
	case 16:
	    return "-7FFFFFFF";
	case 10:
	    return "-2147483648";
	case 8:
	    return "-17777777777";
	default:
	    errlog("STREAM_PRINTF: Illegal radix %d!\n", radix);
	    return "0";
    } else {
	static char buffer[20];
	char *ptr = buffer + 19;
	int neg = 0;

	if (n < 0) {
	    neg = 1;
	    n = -n;
	}
	*(ptr) = '\0';
	while (n != 0) {
	    int digit = n % radix;
	    *(--ptr) = (digit < 10 ? '0' + digit : 'A' + digit - 10);
	    n /= radix;
	}
	if (neg)
	    *(--ptr) = '-';
	return ptr;
    }
}

static const char *
dbl_fmt(void)
{
    static const char *fmt = 0;
    static char buffer[10];

    if (!fmt) {
	sprintf(buffer, "%%.%dg", DBL_DIG);
	fmt = buffer;
    }
    return fmt;
}

void
stream_printf(Stream * s, const char *fmt,...)
{
    char buffer[40];
    va_list args;

    va_start(args, fmt);
    while (*fmt) {
	char c = *fmt;

	if (c == '%') {
	    char pad = ' ';
	    int width = 0, base;
	    const char *string = "";	/* initialized to silence warning */

	    while ((c = *(++fmt)) != '\0') {
		switch (c) {
		case 's':
		    string = va_arg(args, char *);
		    break;
		case 'x':
		    base = 16;
		    goto finish_number;
		case 'o':
		    base = 8;
		    goto finish_number;
		case 'd':
		    base = 10;
		  finish_number:
		    string = itoa(va_arg(args, int), base);
		    break;
		case 'g':
		    sprintf(buffer, dbl_fmt(), va_arg(args, double));
		    if (!strchr(buffer, '.') && !strchr(buffer, 'e'))
			strcat(buffer, ".0");	/* make it look floating */
		    string = buffer;
		    break;
		case '0':
		    if (width == 0) {
			pad = '0';
			continue;
		    }
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
		    width = width * 10 + c - '0';
		    continue;
		default:
		    errlog("STREAM_PRINTF: Unknown format directive: %%%c\n",
			   c);
		    goto abort;
		}
		break;
	    }

	    if (width && (width -= strlen(string)) > 0)
		while (width--)
		    stream_add_char(s, pad);
	    stream_add_string(s, string);
	} else
	    stream_add_char(s, *fmt);

	fmt++;
    }

  abort:
    va_end(args);
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
