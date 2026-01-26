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

#ifndef DB_h
#define DB_h 1

#include <stdbool.h>

#include "config.h"
#include "program.h"
#include "structures.h"

static inline bool
is_list_of_objs(Var v)
{
    int i;

    if (TYPE_LIST != v.type)
	return false;

    for (i = 1; i <= v.v.list[0].v.num; i++)
	if (TYPE_OBJ != v.v.list[i].type)
	    return false;

    return true;
}

static inline bool
is_obj_or_list_of_objs(Var v)
{
    return v.is_obj() || is_list_of_objs(v);
}

/**** file system ****/

extern const char *db_usage_string(void);
				/* Returns a string describing the database
				 * command-line arguments.
				 */

extern int db_initialize(int *pargc, char ***pargv);
				/* (*pargc) and (*pargv) refer to the database
				 * command-line arguments and perhaps others.
				 * Returns true (and sets (*pargc) and (*pargv)
				 * to refer to any extra arguments) if the
				 * database args were valid.
				 */

extern int db_load(void);
				/* Does any necessary long-running preparations
				 * of the database, such as loading significant
				 * amounts of data into memory, returning true
				 * iff this operation is successful.
				 */

enum db_flush_type {
    FLUSH_IF_FULL,		/* Do output only if there's `too much' changed
				 * data in memory now. */
    FLUSH_ONE_SECOND,		/* Do output for up to about one second. */
    FLUSH_ALL_NOW,		/* Do all pending output, forking the server
				 * if necessary. */
    FLUSH_PANIC			/* Do all pending output; the server is going
				 * down in emergency mode. */
};

extern int db_flush(enum db_flush_type);
				/* Flush some amount of the changed portion of
				 * the database to disk, as indicated by the
				 * argument.  Returns true on success.
				 */

extern Num db_disk_size(void);
				/* Return the total size, in bytes, of the most
				 * recent full representation of the database
				 * as one or more disk files.  Returns -1 if,
				 * for some reason, no such on-disk
				 * representation is currently available.
				 */

extern void db_shutdown(void);
				/* Shut down the database module, flushing all
				 * pending database changes to disk and only
				 * returning after this is done.
				 */

/**** objects ****/
extern void db_clear_ancestor_cache();
#ifndef NDEBUG
extern Var db_anon_debug(Var options, Var arguments);
#endif
extern int valid(Objid);
extern int is_valid(Var);

extern Objid db_create_object(Num new_objid, bool anonymous = false);
				/* Creates a new object with parent & location
				 * == #-1.  Returns new object's id number.
				 */

extern Objid db_last_used_objid(void);
extern void db_reset_last_used_objid(void);
extern void db_set_last_used_objid(Objid);
				/* The former returns the highest object number
				 * ever returned by db_create_object().  The
				 * second resets that high-water mark to the
				 * highest object number referring to a
				 * currently-valid object.  The third sets that
				 * high-water mark to the specified object
				 * number, or to the highest object number
				 * referring to a currently-valid object.
				 */

extern void db_destroy_object(Objid);
				/* Destroys object, freeing all associated
				 * storage.  The object must not have parents,
				 * children or contents, and the location
				 * must == #-1.
				 */

extern void db_destroy_anonymous_object(void *);
				/* Destroys object, freeing all associated
				 * storage.
				 */

extern Object *db_make_anonymous(Objid, Objid);
				/* Makes the specified object anonymous by
				 * removing it from the collection of numbered
				 * objects.  Resets the high-water mark to the
				 * last high-water mark.  Returns an opaque
				 * pointer to the object.
				 */

extern Objid db_renumber_object(Objid);
				/* Renumbers object to have the lowest free
				 * object number.  Returns its new number.
				 */

extern int db_object_bytes(Var);
				/* Returns the number of bytes of memory
				 * currently required in order to represent the
				 * given object and all of its verbs and
				 * properties.
				 */

extern Var db_ancestors(Var, bool);
				/* Returns a list of the ancestors of the
				 * given object.  db_ancestors() does not/
				 * can not free the returned list.  The caller
				 * should therefore free it once it has finished
				 * operating on it.
				 */

extern Var db_descendants(Var, bool);
				/* Returns a list of the descendants of the
				 * given object.  db_descendants() does not/
				 * can not free the returned list.  The caller
				 * should therefore free it once it has finished
				 * operating on it.
				 */

extern Var db_all_locations(Var, bool);
				/* Returns a list of all objects that
				 * contain the given object.
				 * db_all_locations() does not/can not free the
				 * returned list.  The caller should therefore
				 * free it once it has finished operating on it.
				 */

extern Var db_all_contents(Var, bool);
				/* Returns a list of all objects that are
				 * contained by the given object.
				 * db_all_contents() does not/can not free the
				 * returned list.  The caller should therefore
				 * free it once it has finished operating on it.
				 */

/**** object attributes ****/

extern Objid db_object_owner2(Var);

extern Var db_object_parents2(Var);
extern Var db_object_children2(Var);

extern Objid db_object_owner(Objid);
extern void db_set_object_owner(Objid oid, Objid owner);

extern const char *db_object_name(Objid);
extern void db_set_object_name(Objid oid, const char *name);
				/* These functions do not change the reference
				 * count of the name they accept/return.  Thus,
				 * the caller should str_ref() the name if the
				 * reference is to be persistent.
				 */

extern Var db_object_parents(Objid);
extern Var db_object_children(Objid);
				/* Returns a list of the parents/children of the
				 * given object.  These functions do not change
				 * the reference count of the value they return.
				 * Thus, the caller should var_ref() the value
				 * if the reference is to be persistent.
				 */

extern int db_next_child(Objid oid, Var *ret);
				/* A replacement for db_for_all_children that
				 * also includes anonymous children. 
				 * The outcome is unspecified if any of the
				 * following functions are called during a call
				 * to db_next_child():
				 *      db_create_object()
				 *      db_destroy_object()
				 *      db_renumber_object()
				 *      db_change_parent()
				 */

extern int db_change_parents(Var obj, Var parents, Var anon_kids);
				/* db_change_parents() returns true (and
				 * actually changes the parent of OBJ) iff
				 * neither OBJ nor any of its descendents
				 * defines any properties with the same names
				 * as properties defined on PARENTS or any of
				 * its ancestors.
				 */

extern Objid db_object_location(Objid);
extern int db_count_contents(Objid);
extern int db_for_all_contents(Objid,
			       int (*)(void *, Objid),
			       void *);
				/* The outcome is unspecified if any of the
				 * following functions are called during a call
				 * to db_for_all_contects():
				 *      db_destroy_object()
				 *      db_renumber_object()
				 *      db_change_location()
				 */
extern void db_change_location(Objid oid, Objid location, int position);

typedef enum {
    /* Permanent flags */
    FLAG_USER,
    FLAG_PROGRAMMER,
    FLAG_WIZARD,
    FLAG_OBSOLETE_1,
    FLAG_READ,
    FLAG_WRITE,
    FLAG_OBSOLETE_2,
    FLAG_FERTILE,
    FLAG_ANONYMOUS,
    FLAG_INVALID,	/* `FLAG_INVALID' indicates the anonymous
			 * object is invalid due to parent/ancestor
			 * changes, recycling, etc.  The object may
			 * still be maintaining storage.
			 */
    FLAG_RECYCLED	/* `FLAG_RECYCLED' indicates the anonymous
			 * object has been recycled (the `recycle'
			 * verb has been called on the object, if
			 * defined) and the object is scheduled
			 * to have its internal storage freed.
			 */
    /* NOTE: New permanent flags must always be added here, rather
     *	     than replacing one of the obsolete ones, since old
     *	     databases might have old objects around that still have
     *	     that flag set.
     */
} db_object_flag;

extern int db_object_has_flag2(Var, db_object_flag);
extern void db_set_object_flag2(Var, db_object_flag);
extern void db_clear_object_flag2(Var, db_object_flag);

extern int db_object_has_flag(Objid, db_object_flag);
extern void db_set_object_flag(Objid, db_object_flag);
extern void db_clear_object_flag(Objid, db_object_flag);

extern int db_object_allows(Var obj, Objid progr,
			    db_object_flag flag);
				/* Returns true iff either OBJ has FLAG or
				 * PROGR either is a wizard or owns OBJ; that
				 * is, iff PROGR's authority is sufficient to
				 * be allowed to do the indicated operation on
				 * OBJ.
				 */

extern int is_wizard(Objid oid);
extern int is_programmer(Objid oid);
extern int is_user(Objid oid);

extern Var db_all_users(void);
				/* db_all_users() does not change the reference
				 * count of the `users' list maintained by this
				 * module.  The caller should thus var_ref() it
				 * to make it persistent.
				 */

extern int db_object_isa(Var, Var);


/**** properties *****/

typedef enum {
    PF_READ = 01,
    PF_WRITE = 02,
    PF_CHOWN = 04
} db_prop_flag;

extern int db_add_propdef(Var obj, const char *pname,
			  Var value, Objid owner,
			  unsigned flags);
				/* Returns true (and actually adds the property
				 * to OBJ) iff (1) no property named PNAME
				 * already exists on OBJ or one of its
				 * ancestors or descendants, and (2) PNAME does
				 * not name any built-in property.  This
				 * function does not change the reference count
				 * of VALUE, so the caller should var_ref() it
				 * if the caller's reference is to persist.
				 * FLAGS should be the result of ORing together
				 * zero or more elements of `db_prop_flag'.
				 */

extern int db_rename_propdef(Var obj, const char *old,
			     const char *_new);
				/* Returns true (and actually renames the
				 * propdef on OBJ) iff (1) a propdef with the
				 * name OLD existed on OBJ, (2) no property
				 * named NEW already exists on OBJ or one of
				 * its ancestors or descendants, and (3) NEW
				 * does not name any built-in property.  If
				 * condition (1) holds and OLD == NEW, then
				 * this is a no-op that returns true.
				 */

extern int db_delete_propdef(Var, const char *);
				/* Returns true iff a propdef with the given
				 * name existed on the object (i.e., was there
				 * to be deleted).
				 */

extern int db_count_propdefs(Var);
extern int db_for_all_propdefs(Var,
			       int (*)(void *, const char *),
			       void *);
				/* db_for_all_propdefs() does not change the
				 * reference counts of the property names
				 * passed to the given callback function.
				 * Thus, the caller should str_ref() the names
				 * if the references are to be persistent.
				 */

extern int db_for_all_propvals(Var,
			       int (*)(void *, Var),
			       void *);
				/* db_for_all_propvals() does not change the
				 * reference counts of the values passed to
				 * the given callback function.  Thus, the
				 * caller should var_ref() the values if the
				 * references are to be persistent.
				 */

#define BUILTIN_PROPERTIES(DEFINE)		\
    DEFINE(NAME,name)				\
    DEFINE(OWNER,owner)				\
    DEFINE(PROGRAMMER,programmer)		\
    DEFINE(WIZARD,wizard)			\
    DEFINE(R,r)					\
    DEFINE(W,w)					\
    DEFINE(F,f)					\
    DEFINE(A,a)					\
    DEFINE(LOCATION,location)			\
		  DEFINE(LAST_MOVE,last_move)\
    DEFINE(CONTENTS,contents)

enum bi_prop {
    BP_NONE = 0
#define _BP_DO(P,p) , BP_##P
    BUILTIN_PROPERTIES(_BP_DO)
#undef _BP_DO
};

/* Use the accessors `db_is_property_built_in' and `db_is_property_defined_on'
 * to read the fields `built_in' and `definer'.
 */
typedef struct {
    enum bi_prop built_in;	/* true iff property is a built-in one */
    void *definer;		/* null iff property is a built-in one */
    void *ptr;			/* null iff property not found */
} db_prop_handle;

extern db_prop_handle db_find_property(Var obj, const char *name,
				       Var * value);
				/* Returns a handle on the named property of
				 * the given object.  If `value' is non-null,
				 * then the value of the named property (after
				 * skipping over any `clear' slots up the
				 * ancestor list) will be stored through it.
				 * For non-built-in properties, the reference
				 * count of the value is not changed by this
				 * function, so the caller should var_ref() it
				 * if it is to be persistent; some built-in
				 * properties have freshly generated values, so
				 * the caller should free_var() the value in
				 * the built-in case if it is *not* to be
				 * persistent.  The `ptr' in the result of this
				 * function will be null iff there is no such
				 * property on the object.  The `built_in'
				 * field in the result is true iff the named
				 * property is one built into all objects, in
				 * which case it specifies which such property
				 * it is.  The returned handle is very
				 * volatile; only the routines declared
				 * immediately below as taking a
				 * `db_prop_handle' argument are guaranteed to
				 * leave the handle intact.
				 */

extern Var db_property_value(db_prop_handle);
extern void db_set_property_value(db_prop_handle, Var);
				/* For non-built-in properties, these functions
				 * do not change the reference count of the
				 * value they accept/return, so the caller
				 * should var_ref() the value if it is to be
				 * persistent.  Some built-in properties have
				 * freshly generated values, so the caller
				 * should free_var() the value in the built-in
				 * case if it is *not* to be persistent.
				 * The server will panic if
				 * `db_set_property_value()' is called with an
				 * illegal value for a built-in property.
				 */

extern Objid db_property_owner(db_prop_handle);
extern void db_set_property_owner(db_prop_handle, Objid);
				/* These functions may not be called for
				 * built-in properties.
				 */

extern unsigned db_property_flags(db_prop_handle);
extern void db_set_property_flags(db_prop_handle, unsigned);
				/* The property flags are the result of ORing
				 * together zero or more elements of
				 * `db_prop_flag', defined above.  These
				 * functions may not be called for built-in
				 * properties.
				 */

extern int db_property_allows(db_prop_handle, Objid,
			      db_prop_flag);
				/* Returns true iff either the property has the
				 * flag or the object either is a wizard or
				 * owns the property; that is, iff the object's
				 * authority is sufficient to be allowed to do
				 * the indicated operation on the property.
				 * This function may not be called for built-in
				 * properties.
				 */

extern int db_is_property_defined_on(db_prop_handle, Var);
				/* Returns true iff the property is defined on
				 * the specified object.  The object value must
				 * be either an object number or an anonymous
				 * object.
				 */

extern int db_is_property_built_in(db_prop_handle);
				/* Returns true iff the property is a built-in
				 * property.
				 */


/**** verbs ****/

typedef enum {
    VF_READ = 01,
    VF_WRITE = 02,
    VF_EXEC = 04,
    VF_DEBUG = 010
} db_verb_flag;

typedef enum {
    ASPEC_NONE, ASPEC_ANY, ASPEC_THIS
} db_arg_spec;

typedef enum {
    PREP_ANY = -2,
    PREP_NONE = -1,
    Other_Preps = 0		/* Others are indices into DB-internal table */
} db_prep_spec;

extern db_prep_spec db_find_prep(int argc, char *argv[],
				 int *first, int *last);
				/* Look for a prepositional phrase in the
				 * ARGC-element sequence ARGV, returning
				 * PREP_NONE if none was found.  If FIRST and
				 * LAST are both non-null, then the indices
				 * of the first and last word in the phrase
				 * are stored through them.  If either FIRST
				 * or LAST is null, then the match will fail
				 * unless the entire sequence is a phrase.
				 */

extern db_prep_spec db_match_prep(const char *);
				/* Try to recognize the given string as a
				 * prepositional phrase, returning the
				 * appropriate db_prep_spec on success and
				 * PREP_NONE on failure.  The string may be of
				 * the form
				 *      [#]<digit>+
				 * (an optional hash mark followed by one or
				 * more decimal digits).  If the number so
				 * represented is a legitimate index for a
				 * prepositional phrase in the DB-internal
				 * table, the corresponding db_prep_spec is
				 * returned, or PREP_NONE otherwise.
				 */

extern const char *db_unparse_prep(db_prep_spec);
				/* Returns a string giving the human-readable
				 * name(s) for the given preposition
				 * specification.  The returned string may not
				 * be dynamically allocated, so the caller
				 * should str_dup() it if it is to be
				 * persistent.
				 */

extern int  db_add_verb(Var obj, const char *vnames,
			Objid owner, unsigned flags,
			db_arg_spec dobj, db_prep_spec prep,
			db_arg_spec iobj);
				/* This function does not change the reference
				 * count of the NAMES string it accepts.  Thus,
				 * the caller should str_ref() it if it is to
				 * be persistent.
				 */

extern int db_count_verbs(Var);
extern int db_for_all_verbs(Var,
			    int (*)(void *, const char *),
			    void *);
				/* db_for_all_verbs() does not change the
				 * reference counts of the verb names
				 * passed to the given callback function.
				 * Thus, the caller should str_ref() the names
				 * if the references are to be persistent.
				 */

typedef struct {
    void *ptr;
} db_verb_handle;

extern db_verb_handle db_find_command_verb(Objid oid, const char *verb,
					 db_arg_spec dobj, unsigned prep,
					   db_arg_spec iobj);
				/* Returns a handle on the first matching
				 * verb found defined on OID or one of its
				 * ancestors.  A matching verb has a name
				 * matching VERB, a dobj spec equal either to
				 * ASPEC_ANY or DOBJ, a prep spec equal either
				 * to PREP_ANY or PREP, and an iobj spec equal
				 * either to ASPEC_ANY or IOBJ.  The `ptr' in
				 * the result is null iff there is no such
				 * verb.  The returned handle is very volatile;
				 * only the routines declared below as taking a
				 * `db_verb_handle' argument are guaranteed to
				 * leave the handle intact.
				 */

extern db_verb_handle db_find_callable_verb(Var recv, const char *verb);
				/* Returns a handle on the first verb found
				 * defined on OID or one of its ancestors with
				 * a name matching VERB (and, for now, the
				 * VF_EXEC flag set).  The `ptr' in the result
				 * is null iff there is no such verb.  The
				 * returned handle is very volatile; only the
				 * routines declared below as taking a
				 * `db_verb_handle' argument are guaranteed to
				 * leave the handle intact.
				 */

extern db_verb_handle db_find_defined_verb(Var obj, const char *verb,
					   int allow_numbers);
				/* Returns a handle on the first verb found
				 * defined on OBJ with a name matching VERB
				 * (or, if ALLOW_NUMBERS is true and VERB has
				 * the form of a decimal natural number, the
				 * zero-based VERB'th verb defined on OBJ,
				 * whichever comes first).  The `ptr' in the
				 * result is null iff there is no such verb.
				 * The returned handle is very volatile; only
				 * the routines declared below as taking a
				 * `db_verb_handle' argument are guaranteed to
				 * leave the handle intact.
				 */

extern db_verb_handle db_find_indexed_verb(Var obj, unsigned idx);
				/* Returns a handle on the 1-based INDEX'th
				 * verb defined on OBJ.  The `ptr' in the
				 * result is null iff there is no such verb.
				 * The returned handle is very volatile; only
				 * the routines declared below as taking a
				 * `db_verb_handle' argument are guaranteed to
				 * leave the handle intact.
				 */

extern Var db_verb_definer(db_verb_handle);
				/* Returns the object on which the given verb
				 * is defined.  The caller must var_ref(),
				 * var_dup() the value if it is to be
				 * persistent.
				 */

extern const char *db_verb_names(db_verb_handle);
extern void db_set_verb_names(db_verb_handle, const char *);
				/* These functions do not change the reference
				 * count of the string they accept/return.
				 * Thus, the caller should str_ref() it if it
				 * is to be persistent.
				 */

extern Objid db_verb_owner(db_verb_handle);
extern void db_set_verb_owner(db_verb_handle, Objid);

extern unsigned db_verb_flags(db_verb_handle);
extern void db_set_verb_flags(db_verb_handle, unsigned);

extern Program *db_verb_program(db_verb_handle);
extern void db_set_verb_program(db_verb_handle, Program *);
				/* These functions do not change the reference
				 * count of the program they accept/return.
				 * Thus, the caller should program_ref() it if
				 * it is to be persistent.
				 */

extern void db_verb_arg_specs(db_verb_handle h,
			      db_arg_spec * dobj,
			      db_prep_spec * prep,
			      db_arg_spec * iobj);
extern void db_set_verb_arg_specs(db_verb_handle h,
				  db_arg_spec dobj,
				  db_prep_spec prep,
				  db_arg_spec iobj);

extern int db_verb_allows(db_verb_handle, Objid, db_verb_flag);
				/* Returns true iff either the verb has the
				 * flag or the object either is a wizard or
				 * owns the verb; that is, iff the object's
				 * authority is sufficient to be allowed to do
				 * the indicated operation on the verb.
				 */

extern void db_delete_verb(db_verb_handle);
				/* Deletes the given verb entirely.  This
				 * db_verb_handle may not be used again after
				 * this call returns.
				 */

extern void db_fixup_owners(const Objid obj);
                /* Find all objects, verbs, and properties
                 * owned by obj and set them to NOTHING. */

#endif				/* !DB_h */
