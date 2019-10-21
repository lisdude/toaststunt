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

#if HAVE_UNISTD_H  &&  !NDECL_FORK

#include <unistd.h>

#else

#include "my-types.h"

extern unsigned alarm(unsigned);
extern int chmod(const char *, mode_t);
extern int close(int);
extern int dup(int);
extern void _exit(int);
extern pid_t fork(void);
extern pid_t getpid(void);
extern int link(const char *, const char *);
extern int pause(void);
extern int pipe(int *fds);
extern int read(int, void *, unsigned);
extern unsigned sleep(unsigned);
extern int unlink(const char *);
extern int write(int, const void *, unsigned);
extern int fsync(int);

#endif
