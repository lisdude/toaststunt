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

#include "config.h"

/* The server no longer uses variable-length argument lists of large (size > 8
 * bytes) structures, so this code is no longer needed and BUGGY_STDARG is
 * never defined in config.h.  I've left the code here for possible future use.
 */

#if BUGGY_STDARG

/*
 * This implementation of the stdarg.h stuff is very simplistic; it ignores all
 * promotion and alignment issues.  This is good enough for LambdaMOO's uses on
 * most machines.  Regardless, this implementation should not be used unless
 * the one on your machine won't work.
 */

#ifndef STDARG_H
#define STDARG_H 1

#ifndef _VA_LIST_
#define _VA_LIST_ 1
typedef void *va_list;
#endif

#define va_start(ptr, arg) \
		(ptr = (void *) ((unsigned) &arg + sizeof(arg)))

#define va_arg(ptr, type) \
		((type *) (ptr = (void *) ((unsigned) ptr + sizeof(type))))[-1]

#define va_end(ptr)

#endif				/* STDARG_H */

#else

#include <stdarg.h>

#endif				/* BUGGY_STDARG */
