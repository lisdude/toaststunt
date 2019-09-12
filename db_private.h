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
 * Private interface for internal communication in the DB implementation
 *****************************************************************************/
#ifndef DB_PRIVATE_h
#define DB_PRIVATE_h

#include <stdexcept>

#include "config.h"
#include "program.h"
#include "structures.h"

typedef struct Verbdef Verbdef;

struct Verbdef {
    const char *name;
    Program *program;
    Objid owner;
    short perms;
    short prep;
    Verbdef *next;
};

typedef struct Proplist Proplist;
typedef struct Propdef Propdef;

struct Propdef {
    const char *name;
    int hash;
};

struct Proplist {
    int max_length;
    int cur_length;
    Propdef *l;
};

typedef struct Pval {
    Var var;
    Objid owner;
    short perms;
} Pval;

typedef struct Object {
    Objid id;

    Objid owner;

    const char *name;
    int flags; /* see db.h for `flags' values */

    Var location;
    Var last_move;
    Var contents;
    Var parents;
    Var children;

    Pval *propval;
    unsigned int nval;

    Verbdef *verbdefs;
    Proplist propdefs;

    /* The nonce marks changes to the propval layout caused by changes
     * to parentage, property addition/deletion, etc.  Every value is
     * globally unique.
     */
    unsigned int nonce;

    void *waif_propdefs;
} Object;

/*
 * `parents' can be #-1 (NOTHING), a valid object number, or a list of
 * valid object numbers.  `location' can be #-1 or a valid object
 * number.  `children' and `contents' must be a list of valid object
 * numbers.
 */

/*********** Verb cache support ***********/

#define VERB_CACHE 1

#ifdef VERB_CACHE

/* Whenever anything is modified that could influence callable verb
 * lookup, this function must be called.
 */

#ifdef RONG
#define db_priv_affected_callable_verb_lookup() (db_verb_generation++)
                                 /* The choice of a new generation. */
extern unsigned int db_verb_generation;
#endif

extern void db_priv_affected_callable_verb_lookup(void);

#else /* no cache */
#define db_priv_affected_callable_verb_lookup()
#endif

/*********** Objects ***********/

extern Var db_read_anonymous();
extern void db_write_anonymous(Var);

extern void dbpriv_assign_nonce(Object *);

extern Objid dbpriv_object_owner(Object *);
extern void dbpriv_set_object_owner(Object *, Objid owner);

extern const char *dbpriv_object_name(Object *);
extern void dbpriv_set_object_name(Object *, const char *);
				/* These functions do not change the reference
				 * count of the name they accept/return.  Thus,
				 * the caller should str_ref() the name if the
				 * reference is to be persistent.
				 */

extern int dbpriv_object_has_flag(Object *, db_object_flag);
extern void dbpriv_set_object_flag(Object *, db_object_flag);
extern void dbpriv_clear_object_flag(Object *, db_object_flag);

extern Var dbpriv_object_parents(Object *);
extern Var dbpriv_object_children(Object *);
extern Var dbpriv_object_location(Object *);
extern Var dbpriv_object_last_move(Object *);
extern Var dbpriv_object_contents(Object *);
				/* These functions do not change the reference
				 * count of the list they return.  Thus, the
				 * caller should var_ref() the value if the
				 * reference is to be persistent.
				 */

extern void dbpriv_set_all_users(Var);
				/* Initialize the list returned by
				 * db_all_users().
				 */

extern Object *dbpriv_new_object(Num new_objid);
extern Object *dbpriv_new_anonymous_object(void);
				/* Creates a new object, assigning it a number,
				 * but doesn't fill in any of the fields other
				 * than `id'.
				 */
extern void db_init_object(Object *);
				/* Initializes a new object.
				 */

extern void dbpriv_new_recycled_object(void);
				/* Does the equivalent of creating and
				 * destroying an object, with the net effect of
				 * using up the next available object number.
				 */

extern Object *dbpriv_find_object(Objid);
				/* Returns 0 if given object is not valid.
				 */

extern void dbpriv_after_load(void);

/*********** Properties ***********/

extern Propdef dbpriv_new_propdef(const char *);

extern int dbpriv_check_properties_for_chparent(Var obj,
						Var parents,
						Var anon_kids);
				/* Return true iff PARENTS defines no
				 * properties that are also defined by either
				 * OBJ or any of OBJ's descendants, or by
				 * other parents in PARENTS and their
				 * ancestors.
				 */

extern void dbpriv_fix_properties_after_chparent(Var obj,
						 Var old_ancestors,
						 Var new_ancestors,
						 Var anon_kids);
				/* OBJ has just had its parents changed.
				 * Fix up the properties of OBJ and its
				 * descendants, removing obsolete ones
				 * and adding clear new ones, as
				 * appropriate for its new parents.
				 */

/*********** Verbs ***********/

extern void dbpriv_build_prep_table(void);
				/* Should be called once near the beginning of
				 * the world, to initialize the
				 * prepositional-phrase matching table.
				 */

/*********** DBIO ***********/

class dbpriv_dbio_failed: public std::exception
{
public:

    dbpriv_dbio_failed() throw() {}

    virtual ~dbpriv_dbio_failed() throw() {}

    virtual const char* what() const throw() {
	return "dbio failed";
    }
};

				/* Raised by DBIO in case of failure (e.g.,
				 * running out of disk space for the dump).
				 */

extern void dbpriv_set_dbio_input(FILE *);
extern void dbpriv_set_dbio_output(FILE *);

/****/

static inline Object *
dbpriv_dereference(Var v)
{
    return (TYPE_OBJ == v.type)
           ? dbpriv_find_object(v.v.obj)
           : v.v.anon;
}
#endif      /* !DB_PRIVATE_h */
