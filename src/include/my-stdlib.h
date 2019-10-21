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

#ifndef My_Stdlib_H
#define My_Stdlib_H 1

#include "config.h"

#if HAVE_STDLIB_H

#include <stdlib.h>

#if NEED_MALLOC_H
#include <malloc.h>
#endif

#else				/* !HAVE_STDLIB_H */

#include "my-types.h"

extern void abort(void);
extern int abs(int);
extern void exit(int);
extern void free(void *);
extern void *malloc(size_t);
extern int rand(void);
extern void *realloc(void *, size_t);
extern void srand(unsigned);

#endif				/* !HAVE_STDLIB_H */

#if NDECL_STRTOD
extern double strtod(const char *, char **);
#endif

#if NDECL_STRTOL
extern long strtol(const char *, char **, int);
#endif

#if HAVE_STRTOUL
# if NDECL_STRTOUL
extern unsigned long strtoul(const char *, char **, int);
# endif
#else
# define strtoul		strtol
#endif

#if NDECL_RANDOM
extern int random(void);
#endif

#if NDECL_SRANDOM
extern int srandom(unsigned);
#endif

#endif
