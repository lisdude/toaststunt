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

#ifndef Unparse_h
#define Unparse_h 1

#include "my-stdio.h"

#include "config.h"
#include "program.h"
#include "structures.h"

typedef void (*Unparser_Receiver) (void *, const char *);

extern void unparse_program(Program *, Unparser_Receiver, void *,
			    int fully_parenthesize,
			    int indent_lines, int f_index);

extern void unparse_to_file(FILE * fp, Program *,
			    int fully_parenthesize,
			    int indent_lines, int f_index);
extern void unparse_to_stderr(Program *, int fully_parenthesize,
			      int indent_lines, int f_index);

extern const char *error_name(enum error);	/* E_NONE -> "E_NONE" */
extern const char *unparse_error(enum error);	/* E_NONE -> "No error" */

extern int parse_error(const char *error);	/* "E_NONE" -> E_NONE */

#endif
