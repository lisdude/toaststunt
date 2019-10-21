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

#ifndef My_Stdio_H
#define My_Stdio_H 1

#include "config.h"

#include <stdarg.h>
#include <stdio.h>

#if NDECL_FCLOSE
#include "my-types.h"

extern int fclose(FILE *);
extern int fflush(FILE *);
extern int fileno(FILE *);
extern size_t fwrite(const void *, size_t, size_t, FILE *);
extern int fgetc(FILE *);
extern int fprintf(FILE *, const char *,...);
extern int fscanf(FILE *, const char *,...);
extern int sscanf(const char *, const char *,...);
extern int printf(const char *,...);
extern int ungetc(int, FILE *);
#endif

#if NDECL_PERROR
extern void perror(const char *);
#endif

#if NDECL_REMOVE
extern int remove(const char *);
extern int rename(const char *, const char *);
#endif

#if NDECL_VFPRINTF
#include "my-stdarg.h"

extern int vfprintf(FILE *, const char *, va_list);
extern int vfscanf(FILE *, const char *, va_list);
#endif

#if !HAVE_REMOVE
#  include "my-unistd.h"
#  define remove(x)	unlink(x)
#endif

#if !HAVE_RENAME
#  include "my-unistd.h"
#  define rename(old, new)	(link(old, new) && unlink(old))
#endif

#endif				/* !My_Stdio_H */
