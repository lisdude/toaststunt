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

#ifndef Parse_Cmd_H
#define Parse_Cmd_H 1

#include "config.h"
#include "db.h"
#include "structures.h"

typedef struct {
    const char *verb;		/* verb (as typed by player) */
    const char *argstr;		/* arguments to verb */
    Var args;			/* arguments to the verb */

    const char *dobjstr;	/* direct object string */
    Objid dobj;			/* direct object */

    const char *prepstr;	/* preposition string */
    db_prep_spec prep;		/* preposition identifier */

    const char *iobjstr;	/* indirect object string */
    Objid iobj;			/* indirect object */
} Parsed_Command;

extern char **parse_into_words(char *input, int *nwords);
extern Var parse_into_wordlist(const char *command);
extern Parsed_Command *parse_command(const char *command, Objid user);
extern void free_parsed_command(Parsed_Command *);

#endif				/* !Parse_Cmd_H */
