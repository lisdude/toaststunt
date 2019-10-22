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

#ifndef Parser_h
#define Parser_h 1

#include "config.h"
#include "program.h"
#include "version.h"

typedef struct {
    void (*error) (void *, const char *);
    void (*warning) (void *, const char *);
    int (*getch) (void *);
} Parser_Client;

extern Program *parse_program(DB_Version, Parser_Client, void *);
extern Program *parse_list_as_program(Var code, Var * errors);

#endif
