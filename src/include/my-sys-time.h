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
#include "options.h"

#include <sys/time.h>

#if NEED_SELECT_H
#  include "my-types.h"
#  include <sys/select.h>
#endif

#if NDECL_GETITIMER
extern int getitimer(int, struct itimerval *);
#endif

#if NDECL_SETITIMER
extern int setitimer(int, struct itimerval *, struct itimerval *);
#endif

#if NDECL_SELECT  &&  MPLEX_STYLE == MP_SELECT
#include "my-types.h"

extern int select(int, fd_set *, fd_set *, fd_set *, struct timeval *);
#endif
