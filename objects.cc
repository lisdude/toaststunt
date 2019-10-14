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

#include <vector>

#include "collection.h"
#include "db.h"
#include "db_io.h"
#include "execute.h"
#include "functions.h"
#include "list.h"
#include "numbers.h"
#include "quota.h"
#include "server.h"
#include "storage.h"
#include "structures.h"
#include "utils.h"
#include "log.h"
#include "background.h"   // Threads

static int
controls(Objid who, Objid what)
{
    return is_wizard(who) || who == db_object_owner(what);
}

static int
controls2(Objid who, Var what)
{
    return is_wizard(who) || who == db_object_owner2(what);
}

static Var
make_arglist(Objid what)
{
    Var r;

    r = new_list(1);
    r.v.list[1].type = TYPE_OBJ;
    r.v.list[1].v.obj = what;

    return r;
}

static bool
all_valid(Var vars)
{
    Var var;
    int i, c;
    FOR_EACH(var, vars, i, c)
	if (!valid(var.v.obj))
	    return false;
    return true;
}

static bool
all_allowed(Var vars, Objid progr, db_object_flag f)
{
    Var var;
    int i, c;
    FOR_EACH(var, vars, i, c)
	if (!db_object_allows(var, progr, f))
	    return false;
    return true;
}

/*
 * Returns true if `_this' is a descendant of `obj'.
 */
static bool
is_a_descendant(Var _this, Var obj)
{
    Var descendants = db_descendants(obj, true);
    int ret = ismember(_this, descendants, 1);
    free_var(descendants);
    return ret ? true : false;
}

/*
 * Returns true if any of `these' are descendants of `obj'.
 */
static bool
any_are_descendants(Var these, Var obj)
{
    Var _this, descendants = db_descendants(obj, true);
    int i, c;
    FOR_EACH(_this, these, i, c) {
	if (is_a_descendant(_this, obj)) {
	    free_var(descendants);
	    return true;
	}
    }
    free_var(descendants);
    return false;
}

struct bf_move_data {
    Objid what, where;
    int position;
};

static package
do_move(Var arglist, Byte next, struct bf_move_data *data, Objid progr)
{
    Objid what = data->what, where = data->where;
    int position = data->position, accepts;
    Objid oid, oldloc = FAILED_MATCH;
    Var args;
    enum error e;

    switch (next) {
    case 1:			/* Check validity and decide `accepts' */
	if (!valid(what) || (!valid(where) && where != NOTHING) || position < 0)
	    return make_error_pack(E_INVARG);
	else if (!controls(progr, what))
	    return make_error_pack(E_PERM);
	else if (where == NOTHING || where == db_object_location(what))
	    accepts = 1;
    else if (data->position > 0 && !controls(progr, where))
        return make_error_pack(E_PERM);
	else {
	    args = make_arglist(what);
	    e = call_verb(where, "accept", Var::new_obj(where), args, 0);
	    /* e will not be E_INVIND */

	    if (e == E_NONE)
		return make_call_pack(2, data);
	    else {
		free_var(args);
		if (e == E_VERBNF)
		    accepts = 0;

		else		/* (e == E_MAXREC) */
		    return make_error_pack(e);
	    }
	}
	goto accepts_decided;

    case 2:			/* Returned from `accepts' call */
	accepts = is_true(arglist);

      accepts_decided:
	if (!is_wizard(progr) && accepts == 0)
	    return make_error_pack(E_NACC);

	if (!valid(what)
	    || (where != NOTHING && !valid(where))
	    || (db_object_location(what) == where && position == 0))
	    return no_var_pack();

	/* Check to see that we're not trying to violate the hierarchy */
	for (oid = where; oid != NOTHING; oid = db_object_location(oid))
	    if (oid == what)
		return make_error_pack(E_RECMOVE);

    oldloc = db_object_location(what);
	db_change_location(what, where, position);

	if (where != oldloc) {
		args = make_arglist(what);
		e = call_verb(oldloc, "exitfunc", Var::new_obj(oldloc), args, 0);

		if (e == E_NONE)
		    return make_call_pack(3, data);
		else {
		    free_var(args);
		    if (e == E_MAXREC)
			return make_error_pack(e);
		}
	/* e == E_INVIND or E_VERBNF, in both cases fall through */
	}

    case 3:			/* Returned from exitfunc call */
	if (valid(where) && valid(what)
	    && where != oldloc && db_object_location(what) == where) {
	    args = make_arglist(what);
	    e = call_verb(where, "enterfunc", Var::new_obj(where), args, 0);
	    /* e != E_INVIND */

	    if (e == E_NONE)
		return make_call_pack(4, data);
	    else {
		free_var(args);
		if (e == E_MAXREC)
		    return make_error_pack(e);
		/* else e == E_VERBNF, fall through */
	    }
	}
    case 4:			/* Returned from enterfunc call */
	return no_var_pack();

    default:
	panic_moo("Unknown PC in DO_MOVE");
	return no_var_pack();	/* Dead code to eliminate compiler warning */
    }
}

static package
bf_move(Var arglist, Byte next, void *vdata, Objid progr)
{
    struct bf_move_data *data = (bf_move_data *)vdata;
    package p;

    if (next == 1) {
	data = (bf_move_data *)alloc_data(sizeof(*data));
	data->what = arglist.v.list[1].v.obj;
	data->where = arglist.v.list[2].v.obj;
    data->position = arglist.v.list[0].v.num < 3 ? 0 : arglist.v.list[3].v.num;
    }
    p = do_move(arglist, next, data, progr);
    free_var(arglist);

    if (p.kind != package::BI_CALL)
	free_data(data);

    return p;
}

static void
bf_move_write(void *vdata)
{
    struct bf_move_data *data = (bf_move_data *)vdata;

    dbio_printf("bf_move data: what = %" PRIdN ", where = %" PRIdN ", position = %" PRIdN "\n",
		data->what, data->where, data->position);
}

static void *
bf_move_read()
{
    struct bf_move_data *data = (bf_move_data *)alloc_data(sizeof(*data));

    if (dbio_scanf("bf_move data: what = %" PRIdN ", where = %" PRIdN ", position = %" PRIdN "\n",
		   &data->what, &data->where, &data->position) == 3)
	return data;
    else
	return 0;
}

static package
bf_toobj(Var arglist, Byte next, void *vdata, Objid progr)
{
    Var r;
    Num i;
    enum error e;

    r.type = TYPE_OBJ;
    e = become_integer(arglist.v.list[1], &i, 0);
    r.v.obj = i;

    free_var(arglist);
    if (e != E_NONE)
	return make_error_pack(e);

    return make_var_pack(r);
}

static package
bf_typeof(Var arglist, Byte next, void *vdata, Objid progr)
{
    Var r;
    r.type = TYPE_INT;
    r.v.num = (int) arglist.v.list[1].type & TYPE_DB_MASK;
    free_var(arglist);
    return make_var_pack(r);
}

static package
bf_valid(Var arglist, Byte next, void *vdata, Objid progr)
{				/* (object) */
    Var r;

    if (arglist.v.list[1].is_object()) {
	r.type = TYPE_INT;
	r.v.num = is_valid(arglist.v.list[1]);
    }
    else {
	free_var(arglist);
	return make_error_pack(E_TYPE);
    }

    free_var(arglist);
    return make_var_pack(r);
}

static package
bf_max_object(Var arglist, Byte next, void *vdata, Objid progr)
{				/* () */
    Var r;

    free_var(arglist);
    r.type = TYPE_OBJ;
    r.v.obj = db_last_used_objid();
    return make_var_pack(r);
}

static package
bf_create(Var arglist, Byte next, void *vdata, Objid progr)
{			/* (OBJ|LIST parent(s) [, OBJ owner] [, INT anonymous] [, LIST args]) */
    Var *data = (Var *)vdata;
    Var r;

    if (next == 1) {
	// there must be at least one argument, and
	// it must be an object or list of objects
	if (!is_obj_or_list_of_objs(arglist.v.list[1])) {
	    free_var(arglist);
	    return make_error_pack(E_TYPE);
	}

	int nargs = arglist.v.list[0].v.num;
	Objid owner = progr;
	int anon = 0; // position of the anonymous flag argument
	int init = 0; // position of the initializer argument

	if (1 < nargs && TYPE_OBJ == arglist.v.list[2].type)
	    owner = arglist.v.list[2].v.obj;
	else if (1 < nargs && TYPE_INT == arglist.v.list[2].type)
	    anon = 2;
	else if (1 < nargs && TYPE_LIST == arglist.v.list[2].type)
	    init = 2;
	else if (1 < nargs) {
	    free_var(arglist);
	    return make_error_pack(E_TYPE);
	}

	if (2 < nargs && TYPE_INT == arglist.v.list[3].type && !anon)
	    anon = 3;
	else if (2 < nargs && TYPE_LIST == arglist.v.list[3].type && !init)
	    init = 3;
	else if (2 < nargs) {
	    free_var(arglist);
	    return make_error_pack(E_TYPE);
	}

	if (3 < nargs && TYPE_INT == arglist.v.list[4].type && !anon)
	    anon = 4;
	else if (3 < nargs && TYPE_LIST == arglist.v.list[4].type && !init)
	    init = 4;
	else if (3 < nargs) {
	    free_var(arglist);
	    return make_error_pack(E_TYPE);
	}

	bool anonymous = anon > 0 ? arglist.v.list[anon].v.num : false;

	if ((anonymous && owner == NOTHING)
	    || (!valid(owner) && owner != NOTHING)
	    || (arglist.v.list[1].type == TYPE_OBJ
		&& !valid(arglist.v.list[1].v.obj)
		&& arglist.v.list[1].v.obj != NOTHING)
	    || (arglist.v.list[1].type == TYPE_LIST
		&& !all_valid(arglist.v.list[1]))) {
	    free_var(arglist);
	    return make_error_pack(E_INVARG);
	}
	else if ((progr != owner && !is_wizard(progr))
		 || (arglist.v.list[1].type == TYPE_OBJ
		     && valid(arglist.v.list[1].v.obj)
		     && !db_object_allows(arglist.v.list[1], progr,
		                          anonymous ? FLAG_ANONYMOUS : FLAG_FERTILE))
		 || (arglist.v.list[1].type == TYPE_LIST
		     && !all_allowed(arglist.v.list[1], progr,
		                     anonymous ? FLAG_ANONYMOUS : FLAG_FERTILE))) {
	    free_var(arglist);
	    return make_error_pack(E_PERM);
	}

	if (valid(owner) && !decr_quota(owner)) {
	    free_var(arglist);
	    return make_error_pack(E_QUOTA);
	}
	else {
	    enum error e;
	    Objid last = db_last_used_objid();
	    Objid oid = db_create_object(-1);
	    Var args;

	    db_set_object_owner(oid, !valid(owner) ? oid : owner);

	    if (!db_change_parents(Var::new_obj(oid), arglist.v.list[1], none)) {
		db_destroy_object(oid);
		db_set_last_used_objid(last);
		free_var(arglist);
		return make_error_pack(E_INVARG);
	    }

	    /*
	     * If anonymous, clean up the object used to create the
	     * anonymous object; `oid' is invalid after that.
	     */
	    if (anonymous) {
		r.type = TYPE_ANON;
		r.v.anon = db_make_anonymous(oid, last);
	    } else {
		r.type = TYPE_OBJ;
		r.v.obj = oid;
	    }

	    data = (Var *)alloc_data(sizeof(Var));
	    *data = var_ref(r);

	    /* pass in initializer args, if present */
	    args = init > 0 ? var_ref(arglist.v.list[init]) : new_list(0);

	    free_var(arglist);

	    e = call_verb(oid, "initialize", r, args, 0);
	    /* e will not be E_INVIND */

	    if (e == E_NONE) {
		free_var(r);
		return make_call_pack(2, data);
	    }

	    free_var(*data);
	    free_data(data);
	    free_var(args);

	    if (e == E_MAXREC) {
		free_var(r);
		return make_error_pack(e);
	    } else		/* (e == E_VERBNF) do nothing */
		return make_var_pack(r);
	}
    } else {			/* next == 2, returns from initialize verb_call */
	r = var_ref(*data);
	free_var(*data);
	free_data(data);
	return make_var_pack(r);
    }
}

/* Recreate a destroyed object. Mostly a duplicate of bf_create, unfortunately. */
static package
bf_recreate(Var arglist, Byte next, void *vdata, Objid progr)
{			/* (OBJ old_object, OBJ parent [, OBJ owner]) */
    Var *data = (Var *)vdata;
    Var r;

    if (next == 1) {
	if (arglist.v.list[1].v.obj <= 0 || arglist.v.list[1].v.obj > db_last_used_objid() || is_valid(arglist.v.list[1])) {
        free_var(arglist);
        return make_error_pack(E_INVARG);
    }

	Objid owner = progr;
    if (arglist.v.list[0].v.num > 2 && arglist.v.list[3].type == TYPE_OBJ && is_valid(arglist.v.list[3]))
        owner = arglist.v.list[3].v.obj;

    if ((progr != owner && !is_wizard(progr))
		 || (arglist.v.list[2].type == TYPE_OBJ
		     && valid(arglist.v.list[2].v.obj)
		     && !db_object_allows(arglist.v.list[2], progr, FLAG_FERTILE))) {
	    free_var(arglist);
	    return make_error_pack(E_PERM);
	}

	if (valid(owner) && !decr_quota(owner)) {
	    free_var(arglist);
	    return make_error_pack(E_QUOTA);
	}
	else {
	    enum error e;
	    Objid oid = db_create_object(arglist.v.list[1].v.obj);

	    db_set_object_owner(oid, !valid(owner) ? oid : owner);

	    if (!db_change_parents(Var::new_obj(oid), arglist.v.list[2], none)) {
		db_destroy_object(oid);
		free_var(arglist);
		return make_error_pack(E_INVARG);
	    }

	    free_var(arglist);

        r.type = TYPE_OBJ;
        r.v.obj = oid;

        data = (Var *)alloc_data(sizeof(Var));
	    *data = var_ref(r);

	    e = call_verb(oid, "initialize", r, new_list(0), 0);
	    /* e will not be E_INVIND */

	    if (e == E_NONE) {
		free_var(r);
		return make_call_pack(2, data);
	    }

	    free_var(*data);
	    free_data(data);

	    if (e == E_MAXREC) {
		free_var(r);
		return make_error_pack(e);
	    } else		/* (e == E_VERBNF) do nothing */
		return make_var_pack(r);
	}
    } else {			/* next == 2, returns from initialize verb_call */
	r = var_ref(*data);
	free_var(*data);
	free_data(data);
	return make_var_pack(r);
    }
}

static void
bf_create_write(void *vdata)
{
    dbio_printf("bf_create data: oid = %" PRIdN "\n", *((Objid *) vdata));
}

static void *
bf_create_read(void)
{
    Objid *data = (Objid *)alloc_data(sizeof(Objid));

    if (dbio_scanf("bf_create data: oid = %" PRIdN "\n", data) == 1)
	return data;
    else
	return 0;
}

static package
bf_chparent_chparents(Var arglist, Byte next, void *vdata, Objid progr)
{				/* (OBJ obj, OBJ|LIST what, LIST anon) */
    Var obj = arglist.v.list[1];
    Var what = arglist.v.list[2];
    int n = listlength(arglist);
    Var anon_kids = nothing;

    if (!obj.is_object() || !is_obj_or_list_of_objs(what)) {
	free_var(arglist);
	return make_error_pack(E_TYPE);
    }

    if (n > 2 && !is_wizard(progr)) {
	free_var(arglist);
	return make_error_pack(E_PERM);
    }
    else if (n > 2) {
	anon_kids = arglist.v.list[3];
    }

    if (!is_valid(obj)
	|| (what.type == TYPE_OBJ && !valid(what.v.obj)
	    && what.v.obj != NOTHING)
	|| (what.type == TYPE_LIST
	    && !all_valid(what))) {
	free_var(arglist);
	return make_error_pack(E_INVARG);
    }
    else if (!controls2(progr, obj)
	     || (what.type == TYPE_OBJ && valid(what.v.obj)
		 && !db_object_allows(what, progr, FLAG_FERTILE))
	     || (what.type == TYPE_LIST
		 && !all_allowed(what, progr, FLAG_FERTILE))) {
	free_var(arglist);
	return make_error_pack(E_PERM);
    }
    else if ((what.type == TYPE_OBJ && is_a_descendant(what, obj))
	     || (what.type == TYPE_LIST && any_are_descendants(what, obj))) {
	free_var(arglist);
	return make_error_pack(E_RECMOVE);
    }
    else {
	if (!db_change_parents(obj, what, anon_kids)) {
	    free_var(arglist);
	    return make_error_pack(E_INVARG);
	}
	else {
	    free_var(arglist);
	    return no_var_pack();
	}
    }
}

/* bf_parent is DEPRECATED!  It returns only the first parent in the
 * set of parents.  Use bf_parents!
 */
static package
bf_parent(Var arglist, Byte next, void *vdata, Objid progr)
{				/* (OBJ object) */
    Var r;

    if (!arglist.v.list[1].is_object()) {
	free_var(arglist);
	return make_error_pack(E_TYPE);
    } else if (!is_valid(arglist.v.list[1])) {
	free_var(arglist);
	return make_error_pack(E_INVARG);
    } else {
	r = var_ref(db_object_parents2(arglist.v.list[1]));
	free_var(arglist);
    }

    if (TYPE_OBJ == r.type)
	return make_var_pack(r);

    if (0 == listlength(r)) {
	free_var(r);
	return make_var_pack(Var::new_obj(NOTHING));
    } else {
	Var t = var_ref(r.v.list[1]);
	free_var(r);
	return make_var_pack(t);
    }
}

static package
bf_parents(Var arglist, Byte next, void *vdata, Objid progr)
{				/* (OBJ object) */
    Var r;

    if (!arglist.v.list[1].is_object()) {
	free_var(arglist);
	return make_error_pack(E_TYPE);
    }  else if (!is_valid(arglist.v.list[1])) {
	free_var(arglist);
	return make_error_pack(E_INVARG);
    } else {
	r = var_ref(db_object_parents2(arglist.v.list[1]));
	free_var(arglist);
    }

    if (TYPE_LIST == r.type)
	return make_var_pack(r);

    if (NOTHING == r.v.obj) {
	free_var(r);
	return make_var_pack(new_list(0));
    } else {
	Var t = new_list(1);
	t.v.list[1] = r;
	return make_var_pack(t);
    }
}

static package
bf_children(Var arglist, Byte next, void *vdata, Objid progr)
{				/* (object) */
    Var obj = arglist.v.list[1];

    if (!obj.is_object()) {
	free_var(arglist);
	return make_error_pack(E_TYPE);
    } else if (!is_valid(obj)) {
	free_var(arglist);
	return make_error_pack(E_INVARG);
    } else {
	Var r = var_ref(db_object_children2(obj));
	free_var(arglist);
	return make_var_pack(r);
    }
}

static package
bf_ancestors(Var arglist, Byte next, void *vdata, Objid progr)
{				/* (OBJ object) */
    Var obj = arglist.v.list[1];
    bool full = (listlength(arglist) > 1 && is_true(arglist.v.list[2])) ? true : false;

    if (!obj.is_object()) {
	free_var(arglist);
	return make_error_pack(E_TYPE);
    } else if (!is_valid(obj)) {
	free_var(arglist);
	return make_error_pack(E_INVARG);
    } else {
	Var r = db_ancestors(obj, full);
	free_var(arglist);
	return make_var_pack(r);
    }
}

static package
bf_descendants(Var arglist, Byte next, void *vdata, Objid progr)
{				/* (OBJ object) */
    Var obj = arglist.v.list[1];
    bool full = (listlength(arglist) > 1 && is_true(arglist.v.list[2])) ? true : false;

    if (!obj.is_object()) {
	free_var(arglist);
	return make_error_pack(E_TYPE);
    } else if (!is_valid(obj)) {
	free_var(arglist);
	return make_error_pack(E_INVARG);
    } else {
	Var r = db_descendants(obj, full);
	free_var(arglist);
	return make_var_pack(r);
    }
}

static int
move_to_nothing(Objid oid)
{
    /* All we need to do is change the location and run the exitfunc. */
    Objid oldloc = db_object_location(oid);
    Var args;
    enum error e;

    db_change_location(oid, NOTHING, 0);

    args = make_arglist(oid);
    e = call_verb(oldloc, "exitfunc", Var::new_obj(oldloc), args, 0);

    if (e == E_NONE)
	return 1;

    free_var(args);
    return 0;
}

static int
first_proc(void *data, Objid oid)
{
    Objid *oidp = (Objid *)data;

    *oidp = oid;
    return 1;
}

static Objid
get_first(Objid oid, int (*for_all) (Objid, int (*)(void *, Objid), void *))
{
    Objid result = NOTHING;

    for_all(oid, first_proc, &result);

    return result;
}

static package
bf_recycle(Var arglist, Byte func_pc, void *vdata, Objid progr)
{				/* (OBJ|ANON object) */
    Var *data = (Var *)vdata;
    enum error e;
    Var obj;
    Var args;

    switch (func_pc) {
    case 1:
	obj = var_ref(arglist.v.list[1]);
	free_var(arglist);

	if (!obj.is_object()) {
	    free_var(obj);
	    return make_error_pack(E_TYPE);
	} else if (!is_valid(obj) || db_object_has_flag2(obj, FLAG_RECYCLED)) {
	    free_var(obj);
	    return make_error_pack(E_INVARG);
	} else if (!controls2(progr, obj)) {
	    free_var(obj);
	    return make_error_pack(E_PERM);
	}

	db_set_object_flag2(obj, FLAG_RECYCLED);

	/* Recycle permanent and anonymous objects.
	 *
	 * At this point in time, an anonymous object may be in the
	 * root buffer and may be any color (purple, if the last
	 * operation was a decrement, black, if the last operation was
	 * an increment) (it _will_ have a reference, however -- a
	 * reference to itself, at least).
	 */

	data = (Var *)alloc_data(sizeof(Var));
	*data = var_ref(obj);
	args = new_list(0);
	e = call_verb(obj.is_obj() ? obj.v.obj : NOTHING, "recycle", obj, args, 0);
	/* e != E_INVIND */

	if (e == E_NONE) {
	    free_var(obj);
	    return make_call_pack(2, data);
	}
	/* else e == E_VERBNF or E_MAXREC; fall through */

	free_var(args);

	goto moving_contents;

    case 2:
	obj = var_ref(*data);
	free_var(arglist);

      moving_contents:

	if (!is_valid(obj)) {
	    free_var(obj);
	    free_var(*data);
	    free_data(data);
	    return no_var_pack();
	}

	if (TYPE_OBJ == obj.type) {
	    Objid c, oid = obj.v.obj;

	    while ((c = get_first(oid, db_for_all_contents)) != NOTHING)
		if (move_to_nothing(c))
		    return make_call_pack(2, data);

	    if (db_object_location(oid) != NOTHING && move_to_nothing(oid))
		/* Return to same case because this :exitfunc might add new */
		/* contents to OID or even move OID right back in. */
		return make_call_pack(2, data);

	    /* we can now be confident that OID has no contents and no location */

	    /* do the same thing for the inheritance hierarchy */
	    while ((c = get_first(oid, db_for_all_children)) != NOTHING) {
		Var cp = db_object_parents(c);
		Var op = db_object_parents(oid);
		if (cp.is_obj()) {
		    db_change_parents(Var::new_obj(c), op, none);
		}
		else {
		    int i = 1;
		    int j = 1;
		    Var _new = new_list(0);
		    while (i <= cp.v.list[0].v.num && cp.v.list[i].v.obj != oid) {
			_new = setadd(_new, var_ref(cp.v.list[i]));
			i++;
		    }
		    if (op.is_obj()) {
			if (valid(op.v.obj))
			    _new = setadd(_new, var_ref(op));
		    }
		    else {
			while (j <= op.v.list[0].v.num) {
			    _new = setadd(_new, var_ref(op.v.list[j]));
			    j++;
			}
		    }
		    i++;
		    while (i <= cp.v.list[0].v.num) {
			_new = setadd(_new, var_ref(cp.v.list[i]));
			i++;
		    }
		    db_change_parents(Var::new_obj(c), _new, none);
		    free_var(_new);
		}
	    }

	    db_change_parents(obj, nothing, none);

	    incr_quota(db_object_owner(oid));

	    db_destroy_object(oid);

	    free_var(obj);
	    free_var(*data);
	    free_data(data);
	    return no_var_pack();
	}
	else if (TYPE_ANON == obj.type) {
	    /* We'd like to run `db_change_parents()' to be consistent
	     * with the pattern laid out for permanent objects, but we
	     * can't because the object can be invalid at this point
	     * due to changes in parentage.
	     */
	    /*db_change_parents(obj, nothing, none);*/

	    incr_quota(db_object_owner2(obj));

	    db_destroy_anonymous_object(obj.v.anon);

	    free_var(obj);
	    free_var(*data);
	    free_data(data);
	    return no_var_pack();
	}
    }

    panic_moo("Can't happen in bf_recycle");
    return no_var_pack();
}

static void
bf_recycle_write(void *vdata)
{
    Objid *data = (Objid *)vdata;

    dbio_printf("bf_recycle data: oid = %d, cont = 0\n", *data);
}

static void *
bf_recycle_read(void)
{
    Objid *data = (Objid *)alloc_data(sizeof(*data));
    int dummy;

    /* I use a `dummy' variable here and elsewhere instead of the `*'
     * assignment-suppression syntax of `scanf' because it allows more
     * straightforward error checking; unfortunately, the standard says that
     * suppressed assignments are not counted in determining the returned value
     * of `scanf'...
     */
    if (dbio_scanf("bf_recycle data: oid = %" PRIdN ", cont = %" PRIdN "\n",
		   data, &dummy) == 2)
	return data;
    else
	return 0;
}

static package
bf_players(Var arglist, Byte next, void *vdata, Objid progr)
{				/* () */
    free_var(arglist);
    return make_var_pack(var_ref(db_all_users()));
}

static package
bf_is_player(Var arglist, Byte next, void *vdata, Objid progr)
{				/* (object) */
    Var r;
    Objid oid = arglist.v.list[1].v.obj;

    free_var(arglist);

    if (!valid(oid))
	return make_error_pack(E_INVARG);

    r.type = TYPE_INT;
    r.v.num = is_user(oid);
    return make_var_pack(r);
}

static package
bf_set_player_flag(Var arglist, Byte next, void *vdata, Objid progr)
{				/* (object, yes/no) */
    Var obj;
    char flag;

    obj = arglist.v.list[1];
    flag = is_true(arglist.v.list[2]);

    free_var(arglist);

    if (!valid(obj.v.obj))
	return make_error_pack(E_INVARG);
    else if (!is_wizard(progr))
	return make_error_pack(E_PERM);

    if (flag) {
	db_set_object_flag(obj.v.obj, FLAG_USER);
    } else {
	boot_player(obj.v.obj);
	db_clear_object_flag(obj.v.obj, FLAG_USER);
    }
    return no_var_pack();
}

static package
bf_object_bytes(Var arglist, Byte next, void *vdata, Objid progr)
{
    Var obj = arglist.v.list[1];

    if (!obj.is_object()) {
	free_var(arglist);
	return make_error_pack(E_TYPE);
    } else if (!is_valid(obj)) {
	free_var(arglist);
	return make_error_pack(E_INVIND);
    } else if (!is_wizard(progr)) {
	free_var(arglist);
	return make_error_pack(E_PERM);
    } else {
	Var v;

	v.type = TYPE_INT;
	v.v.num = db_object_bytes(obj);

	free_var(arglist);

	return make_var_pack(v);
    }
}

static package
bf_isa(Var arglist, Byte next, void *vdata, Objid progr)
{				/* (object, parent, return_object) */
    Var object = arglist.v.list[1];
    Var parent = arglist.v.list[2];
    bool return_obj = (arglist.v.list[0].v.num > 2 && is_true(arglist.v.list[3]));

    if (!object.is_object() || (!is_obj_or_list_of_objs(parent) && parent.type != TYPE_ANON)) {
        free_var(arglist);
        return make_error_pack(E_TYPE);
    }

    package ret = make_var_pack(return_obj ? Var::new_obj(NOTHING) : Var::new_int(0));

    if (!is_valid(object)) {
        // Do nothing
    } else if (parent.type == TYPE_LIST) {
        int parent_length = parent.v.list[0].v.num;
        for (int x = 1; x <= parent_length; x++) {
            if (db_object_isa(object, parent.v.list[x])) {
                ret = make_var_pack(return_obj ? Var::new_obj(parent.v.list[x].v.obj) : Var::new_int(1));
                break;
            }
        }
    } else if (db_object_isa(object, parent)) {
        ret = make_var_pack(return_obj ? Var::new_obj(parent.v.obj) : Var::new_int(1));
    }

    free_var(arglist);
    return ret;
}

/* Locate an object in the database by name more quickly than is possible in-DB.
 * To avoid numerous list reallocations, we put everything in a vector and then
 * transfer it over to a list when we know how many values we have. */
void locate_by_name_thread_callback(Var arglist, Var *ret)
{
    Var name, object;
    object.type = TYPE_OBJ;
    std::vector<int> tmp;

    const int case_matters = arglist.v.list[0].v.num < 2 ? 0 : is_true(arglist.v.list[2]);
    const int string_length = memo_strlen(arglist.v.list[1].v.str);

    const Objid last_objid = db_last_used_objid();
    for (int x = 1; x < last_objid; x++)
    {
        if (!valid(x))
            continue;

        object.v.obj = x;
        db_find_property(object, "name", &name);
        if (strindex(name.v.str, memo_strlen(name.v.str), arglist.v.list[1].v.str, string_length, case_matters))
            tmp.push_back(x);
    }

    *ret = new_list(tmp.size());
    const auto vector_size = tmp.size();
    for (size_t x = 0; x < vector_size; x++) {
        ret->v.list[x+1] = Var::new_obj(tmp[x]);
    }
}

    static package
bf_locate_by_name(Var arglist, Byte next, void *vdata, Objid progr)
{
    if (!is_wizard(progr))
    {
        free_var(arglist);
        return make_error_pack(E_PERM);
    }

    char *human_string = nullptr;
    asprintf(&human_string, "locate_by_name: \"%s\"", arglist.v.list[1].v.str);

    return background_thread(locate_by_name_thread_callback, &arglist, human_string);
}

static bool multi_parent_isa(const Var *object, const Var *parents)
{
    if (parents->type == TYPE_OBJ)
        return db_object_isa(*object, *parents);

    for (int y = 1; y <= parents->v.list[0].v.num; y++)
        if (db_object_isa(*object, parents->v.list[y]))
            return true;

    return false;
}

/* Return a list of objects of parent, optionally with a player flag set.
 * With only one argument, player flag is assumed to be the only condition.
 * With two arguments, parent is the only condition.
 * With three arguments, parent is checked first and then the player flag is checked.
 * occupants(LIST objects, OBJ | LIST parent, ?INT player flag set)
 */
    static package
bf_occupants(Var arglist, Byte next, void *vdata, Objid progr)
{				/* (object) */
    Var ret = new_list(0);
    int nargs = arglist.v.list[0].v.num;
    Var contents = arglist.v.list[1];
    int content_length = contents.v.list[0].v.num;
    bool check_parent = nargs == 1 ? false : true;
    Var parent = check_parent ? arglist.v.list[2] : nothing;
    bool check_player_flag = (nargs == 1 || (nargs > 2 && is_true(arglist.v.list[3])));

    if (check_parent && !is_obj_or_list_of_objs(parent)) {
        free_var(arglist);
        return make_error_pack(E_TYPE);
    }

    for (int x = 1; x <= content_length; x++) {
        Objid oid = contents.v.list[x].v.obj;
        if (valid(oid)
                && (!check_parent ? 1 : multi_parent_isa(&contents.v.list[x], &parent))
                && (!check_player_flag || (check_player_flag && is_user(oid))))
        {
            ret = setadd(ret, contents.v.list[x]);
        }
    }

    free_var(arglist);
    return make_var_pack(ret);
}

/* Return a list of nested locations for an object.
 * If base_object is specified, locations will stop at that object. Otherwise,
 *   stop at $nothing (#-1).
 * If check_parent is true, the base_object is assumed to be a parent and an
 *   isa() check is performed.
 * For objects in base_parent, this returns an empty list.
 * locations(OBJ object, ?base_object, ?check_parent)
 */
    static package
bf_locations(Var arglist, Byte next, void *vdata, Objid progr)
{
    Objid what = arglist.v.list[1].v.obj;
    int nargs = arglist.v.list[0].v.num;
    Objid base_obj = (nargs > 1 ? arglist.v.list[2].v.obj : 0);
    Var base_obj_var = Var::new_obj(base_obj);
    bool check_parent = (nargs > 2 ? is_true(arglist.v.list[3]) : false);

    free_var(arglist);

    if (!valid(what))
        return make_error_pack(E_INVIND);

    Var locs = new_list(0);

    Objid loc = db_object_location(what);

    while (valid(loc)) {
        Var loc_var = Var::new_obj(loc);
        if (base_obj && (check_parent ? db_object_isa(loc_var, base_obj_var) : loc == base_obj))
            break;
        locs = setadd(locs, loc_var);
        loc = db_object_location(loc);
    }

    return make_var_pack(locs);
}

    static package
bf_clear_ancestor_cache(Var arglist, Byte next, void *vdata, Objid progr)
{
    free_var(arglist);

    if (!is_wizard(progr))
	return make_error_pack(E_PERM);

    db_clear_ancestor_cache();
    return no_var_pack();
}

    static package
bf_recycled_objects(Var arglist, Byte next, void *vdata, Objid progr)
{
    free_var(arglist);
    std::vector<Objid> tmp;
    Objid max_obj = db_last_used_objid();

    for (Objid x = 0; x <= max_obj; x++) {
        if (!valid(x))
            tmp.push_back(x);
    }

    Var ret = new_list(tmp.size());
    for (size_t x = 1; x <= tmp.size(); x++) {
        ret.v.list[x].type = TYPE_OBJ;
        ret.v.list[x].v.obj = tmp[x-1];
    }

    return make_var_pack(ret);
}

    static package
bf_next_recycled_object(Var arglist, Byte next, void *vdata, Objid progr)
{
    Objid i_obj = (arglist.v.list[0].v.num == 1 ? arglist.v.list[1].v.obj : 0);
    Objid max_obj = db_last_used_objid();
    free_var(arglist);

    if (i_obj > max_obj || i_obj < 0)
	return make_error_pack(E_INVARG);

    package ret = make_var_pack(Var::new_int(0));

    for (; i_obj < max_obj; i_obj++) {
	if (!valid(i_obj)) {
		ret = make_var_pack(Var::new_obj(i_obj));
		break;
	}
    }

    return ret;
}

Var nothing;		/* useful constant */
Var clear;			/* useful constant */
Var none;			/* useful constant */

void
register_objects(void)
{
    nothing.type = TYPE_OBJ;
    nothing.v.obj = NOTHING;
    clear.type = TYPE_CLEAR;
    none.type = TYPE_NONE;

    register_function("toobj", 1, 1, bf_toobj, TYPE_ANY);
    register_function("typeof", 1, 1, bf_typeof, TYPE_ANY);
    register_function_with_read_write("create", 1, 4, bf_create,
				      bf_create_read, bf_create_write,
				      TYPE_ANY, TYPE_ANY, TYPE_ANY, TYPE_ANY);
    register_function_with_read_write("recreate", 2, 3, bf_recreate,
				      bf_create_read, bf_create_write,
				      TYPE_OBJ, TYPE_OBJ, TYPE_OBJ);
    register_function_with_read_write("recycle", 1, 1, bf_recycle,
				      bf_recycle_read, bf_recycle_write,
				      TYPE_ANY);
    register_function("object_bytes", 1, 1, bf_object_bytes, TYPE_ANY);
    register_function("valid", 1, 1, bf_valid, TYPE_ANY);
    register_function("chparents", 2, 3, bf_chparent_chparents,
		      TYPE_ANY, TYPE_LIST, TYPE_LIST);
    register_function("chparent", 2, 3, bf_chparent_chparents,
		      TYPE_ANY, TYPE_OBJ, TYPE_LIST);
    register_function("parents", 1, 1, bf_parents, TYPE_ANY);
    register_function("parent", 1, 1, bf_parent, TYPE_ANY);
    register_function("children", 1, 1, bf_children, TYPE_ANY);
    register_function("ancestors", 1, 2, bf_ancestors,
		      TYPE_ANY, TYPE_ANY);
    register_function("descendants", 1, 2, bf_descendants,
		      TYPE_ANY, TYPE_ANY);
    register_function("max_object", 0, 0, bf_max_object);
    register_function("players", 0, 0, bf_players);
    register_function("is_player", 1, 1, bf_is_player, TYPE_OBJ);
    register_function("set_player_flag", 2, 2, bf_set_player_flag,
		      TYPE_OBJ, TYPE_ANY);
    register_function_with_read_write("move", 2, 3, bf_move,
				      bf_move_read, bf_move_write,
				      TYPE_OBJ, TYPE_OBJ, TYPE_INT);
    register_function("isa", 2, 3, bf_isa, TYPE_ANY, TYPE_ANY, TYPE_INT);
    register_function("locate_by_name", 1, 2, bf_locate_by_name, TYPE_STR, TYPE_INT);
    register_function("occupants", 1, 3, bf_occupants, TYPE_LIST, TYPE_ANY, TYPE_INT);
    register_function("locations", 1, 3, bf_locations, TYPE_OBJ, TYPE_OBJ, TYPE_INT);
#ifdef USE_ANCESTOR_CACHE
    register_function("clear_ancestor_cache", 0, 0, bf_clear_ancestor_cache);
#endif
    register_function("recycled_objects", 0, 0, bf_recycled_objects);
    register_function("next_recycled_object", 0, 1, bf_next_recycled_object, TYPE_OBJ);
}
