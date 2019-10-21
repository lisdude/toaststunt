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

#if SYS_STAT_H_NEEDS_HELP
#  include "my-types.h"
#endif

#include <sys/stat.h>

#if NDECL_FSTAT
#include "my-types.h"

extern int stat(const char *, struct stat *);
extern int fstat(int, struct stat *);
extern int mkfifo(const char *, mode_t);
#endif

#if !HAVE_MKFIFO
extern int mknod(const char *file, mode_t mode, dev_t dev);
#define mkfifo(path, mode)	mknod(path, S_IFIFO | (mode), 0)
#endif
