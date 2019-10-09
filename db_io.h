/******************************************************************************
  Copyright (c) 1995, 1996 Xerox Corporation.  All rights reserved.
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

/*****************************************************************************
 * Routines for use by non-DB modules with persistent state stored in the DB
 *****************************************************************************/

#include "program.h"
#include "structures.h"
#include "version.h"

extern int waif_conversion_type;

extern bool clear_last_move;

/*********** Input ***********/

extern DB_Version dbio_input_version;
				/* What DB-format version are we reading? */

extern void dbio_read_line(char *s, int n);
				/* Reads at most N-1 characters through the
				 * next newline into S, terminating S with a
				 * null.  (Like the `fgets()' function.)
				 */

extern int dbio_scanf(const char *format,...);

extern Num dbio_read_num(void);
extern Objid dbio_read_objid(void);
extern double dbio_read_float(void);

extern const char *dbio_read_string(void);
				/* The returned string is in private storage of
				 * the DBIO module, so the caller should
				 * str_dup() it if it is to persist.
				 */

extern const char *dbio_read_string_intern(void);
				/* The returned string is duplicated
				 * and possibly interned in a db-load
				 * string intern table.
				 */

extern Var dbio_read_var(void);
				/* The DBIO module retains no references to
				 * the returned value, so freeing it is
				 * entirely the responsibility of the caller.
				 */

extern Program *dbio_read_program(DB_Version version,
				  const char *(*fmtr) (void *),
				  void *data);
				/* FMTR is called with DATA to produce a human-
				 * understandable identifier for the program
				 * being read, for use in any error/warning
				 * messages.  If FMTR is null, then DATA should
				 * be the required string.
				 */


/*********** Output ***********/

/* NOTE: All output routines can raise a (private) exception if they are unable
 * to write all of the requested output (e.g., because there is no more space
 * on disk).  The DB module catches this exception and retries the DB dump
 * after performing appropriate notifications, waiting, and/or fixes.  Callers
 * should thus be prepared for any call to these routines to fail to return
 * normally, using TRY ... FINALLY ... if necessary to recover from such an
 * event.
 */

extern void dbio_printf(const char *format,...);

extern void dbio_write_num(Num);
extern void dbio_write_objid(Objid);
extern void dbio_write_float(double);

extern void dbio_write_string(const char *);
				/* The given string should not contain any
				 * newline characters.
				 */

extern void dbio_write_var(Var);

extern void dbio_write_program(Program *);
extern void dbio_write_forked_program(Program * prog, int f_index);
