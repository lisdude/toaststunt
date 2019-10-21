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

#ifndef My_In_H
#define My_In_H 1

#include "config.h"

#include "my-types.h"
#include <netinet/in.h>

#if HAVE_MACHINE_ENDIAN_H

#  include <machine/endian.h>

#else

#  if !defined(htonl)  &&  NDECL_HTONL
extern unsigned short htons();
extern unsigned32 htonl();
extern unsigned short ntohs();
extern unsigned32 ntohl();
#  endif

#endif

#  if NDECL_IN_ADDR_T
typedef unsigned32 in_addr_t;
#  endif

#  ifndef INADDR_NONE
#    define INADDR_NONE ((in_addr_t)-1)
#  endif

#endif
