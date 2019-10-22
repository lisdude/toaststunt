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

#ifndef Version_H
#define Version_H 1

#include "config.h"
#include "structures.h"

/*****************************************************************
 * Server Executable Version
 */

extern const char *server_version;
extern Var server_version_full(Var);


/*****************************************************************
 * Language / Database-Format Version 
 */

/* The following list must never be reordered, only appended to.  There is one
 * element per version of the database format (including incompatible changes
 * to the language, such as the addition of new keywords).  The integer value
 * of each element is used in the DB header on disk to identify the format
 * version in use in that file.
 */
typedef enum {
    DBV_Prehistory,		/* Before format versions */
    DBV_Exceptions,		/* Addition of the `try', `except', `finally',
				 * and `endtry' keywords.
				 */
    DBV_BreakCont,		/* Addition of the `break' and `continue'
				 * keywords.
				 */
    DBV_Float,			/* Addition of `FLOAT' and `INT' variables and
				 * the `E_FLOAT' keyword, along with version
				 * numbers on each frame of a suspended task.
				 */
    DBV_BFBugFixed,		/* Bug in built-in function overrides fixed by
				 * making it use tail-calling.  This DB_Version
				 * change exists solely to turn off special
				 * bug handling in read_bi_func_data().
				 */
    DBV_NextGen,		/* Introduced the next-generation database
				 * format which fixes the data locality
				 * problems in the v4 format.
				 */
    DBV_TaskLocal,		/* Addition of task local value.
				 */
    DBV_Map,			/* Addition of `MAP' variables
				 */
    DBV_FileIO,			/* Includes addition of the 'E_FILE' keyword.
				 */
    DBV_Exec,			/* Includes addition of the 'E_EXEC' keyword.
				 */
    DBV_Interrupt,		/* Includes addition of the 'E_INTRPT' keyword.
				 */
    DBV_This,			/* Varification of `this'.
				 */
    DBV_Iter,			/* Addition of map iterator
				 */
    DBV_Anon,			/* Addition of anonymous objects
				 */
    DBV_Waif,           /* Addition of waifs
                 */
    DBV_Last_Move,      /* Addition of the 'last_move' built-in property
                 */
    DBV_Threaded,       /* Store threading information
                 */
    Num_DB_Versions		/* Special: the current version is this - 1. */
} DB_Version;

#define current_db_version	((DB_Version) (Num_DB_Versions - 1))

extern int check_db_version(DB_Version);
				/* Returns true iff given version is within the
				 * known range.
				 */

#endif				/* !Version_H */
