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

#ifndef My_Time_H
#define My_Time_H 1

#include "config.h"

#if TIME_H_NEEDS_HELP
/* Some systems' time.h does not include time_t or clock_t */
#include "my-types.h"
#endif

#include <time.h>

#if NDECL_TIME
#include "my-types.h"

extern time_t time(time_t *);
#endif

#include <sys/time.h>

#if defined(MACH) && defined(CMU)
/* These clowns blew the declaration of strftime() in their <time.h> */
#undef HAVE_STRFTIME
#endif

#if HAVE_STRFTIME && NDECL_STRFTIME
#include "my-types.h"

extern size_t strftime(char *s, size_t smax, const char *fmt,
		       const struct tm *tp);
#endif

#if HAVE_TZNAME && NDECL_TZNAME
extern char *tzname;
#endif

#endif				/* !My_Time_H */
