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

#ifndef My_String_H
#define My_String_H 1

#include "config.h"

#if USE_OWN_STRING_H

#include "my-types.h"

extern void bzero(char *, int);
extern void *memset(void *, int, size_t);
extern char *strcpy(char *, const char *);
extern char *strncpy(char *, const char *, size_t);
extern char *strcat(char *, const char *);
extern int strcmp(const char *, const char *);
extern int strncmp(const char *, const char *, size_t);
extern char *strchr(const char *, int);
extern char *strerror(int);

/* We don't need to declare these because we're only using our own string.h
 * due to GCC complaining about conflicting built-in declarations for them.
 */

#if 0
extern void *memcpy(void *, const void *, size_t);
extern int memcmp(const void *, const void *, size_t);
extern size_t strlen(const char *);
#endif

#else

#  include <string.h>

#  if NEED_MEMORY_H
#    include <memory.h>
#  else
#    if NDECL_MEMCPY
#      include "my-types.h"
extern void *memcpy(void *, const void *, size_t);
extern void *memmove(void *, const void *, size_t);
extern int memcmp(const void *, const void *, size_t);
#    endif
#  endif

#endif

#if HAVE_STRERROR
# if NDECL_STRERROR
extern char *strerror(int);
# endif
#else
extern const char *sys_errlist[];
#  define strerror(error_code)	sys_errlist[error_code]
#endif

#if NDECL_BZERO && !defined(bzero)
#  if BZERO_IN_STDLIB_H
#    include "my-stdlib.h"
#  else
extern void bzero(char *, int);
#  endif
#endif

#if NDECL_MEMSET
#include "my-types.h"

extern void *memset(void *, int, size_t);
#endif

#endif				/* !My_String_H */
