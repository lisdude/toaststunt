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

#if HAVE_LRAND48
extern long lrand48(void);
extern void srand48(long);
#    define RANDOM	lrand48
#    define SRANDOM	srand48
#else
#  include "my-stdlib.h"
#  if HAVE_RANDOM
#    define RANDOM	random
#    define SRANDOM 	srandom
#  else
#    define RANDOM	rand
#    define SRANDOM	srand
#  endif
#endif
