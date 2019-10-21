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
#include "structures.h"
#include "version.h"

struct keyword {
    const char *name;		/* the canonical spelling of the keyword */
    DB_Version version;		/* the DB version when it was introduced */
    int token;			/* the type of token the scanner should use */
    enum error error;		/* for token == ERROR, the value */
};

typedef const struct keyword Keyword;

extern Keyword *find_keyword(const char *);
