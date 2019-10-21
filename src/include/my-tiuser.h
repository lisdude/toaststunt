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

#include <tiuser.h>

#if NDECL_T_OPEN
extern int t_accept(int, int, struct t_call *);
extern void *t_alloc(int, int, int);
extern int t_bind(int, struct t_bind *, struct t_bind *);
extern int t_close(int);
extern int t_connect(int, struct t_call *, struct t_call *);
extern int t_listen(int, struct t_call *);
extern int t_open(const char *, int, struct t_info *);

extern int t_errno;
#endif

#if NDECL_T_ERRLIST
extern char *t_errlist[];
#endif
