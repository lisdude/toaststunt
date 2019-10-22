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

#include "db.h"
#include "functions.h"
#include "list.h"
#include "storage.h"
#include "utils.h"

struct prop_data {
    Var r;
    int i;
};

static int
add_to_list(void *data, const char *prop_name)
{
    struct prop_data *d = (prop_data *)data;

    d->i++;
    d->r.v.list[d->i].type = TYPE_STR;
    d->r.v.list[d->i].v.str = str_ref(prop_name);

    return 0;
}

static package
bf_properties(Var arglist, Byte next, void *vdata, Objid progr)
{				/* (object) */
    Var obj = arglist.v.list[1];

    free_var(arglist);

    if (!obj.is_object())
	return make_error_pack(E_TYPE);
    else if (!is_valid(obj))
	return make_error_pack(E_INVARG);
    else if (!db_object_allows(obj, progr, FLAG_READ))
	return make_error_pack(E_PERM);
    else {
	struct prop_data d;
	d.r = new_list(db_count_propdefs(obj));
	d.i = 0;
	db_for_all_propdefs(obj, add_to_list, &d);

	return make_var_pack(d.r);
    }
}

static package
bf_prop_info(Var arglist, Byte next, void *vdata, Objid progr)
{				/* (object, prop-name) */
    Var obj = arglist.v.list[1];
    const char *pname = arglist.v.list[2].v.str;
    db_prop_handle h;
    Var r;
    unsigned flags;
    char perms[4], *s;

    if (!obj.is_object()) {
	free_var(arglist);
	return make_error_pack(E_TYPE);
    }
    else if (!is_valid(obj)) {
	free_var(arglist);
	return make_error_pack(E_INVARG);
    }
    h = db_find_property(obj, pname, 0);
    free_var(arglist);

    if (!h.ptr || db_is_property_built_in(h))
	return make_error_pack(E_PROPNF);
    else if (!db_property_allows(h, progr, PF_READ))
	return make_error_pack(E_PERM);

    r = new_list(2);
    r.v.list[1].type = TYPE_OBJ;
    r.v.list[1].v.obj = db_property_owner(h);
    r.v.list[2].type = TYPE_STR;
    s = perms;
    flags = db_property_flags(h);
    if (flags & PF_READ)
	*s++ = 'r';
    if (flags & PF_WRITE)
	*s++ = 'w';
    if (flags & PF_CHOWN)
	*s++ = 'c';
    *s = '\0';
    r.v.list[2].v.str = str_dup(perms);

    return make_var_pack(r);
}

static enum error
validate_prop_info(Var v, Objid * owner, unsigned *flags, const char **name)
{
    const char *s;
    int len = (v.type == TYPE_LIST ? v.v.list[0].v.num : 0);

    if (!((len == 2 || len == 3)
	  && v.v.list[1].type == TYPE_OBJ
	  && v.v.list[2].type == TYPE_STR
	  && (len == 2 || v.v.list[3].type == TYPE_STR)))
	return E_TYPE;

    *owner = v.v.list[1].v.obj;
    if (!valid(*owner))
	return E_INVARG;

    for (*flags = 0, s = v.v.list[2].v.str; *s; s++) {
	switch (*s) {
	case 'r':
	case 'R':
	    *flags |= PF_READ;
	    break;
	case 'w':
	case 'W':
	    *flags |= PF_WRITE;
	    break;
	case 'c':
	case 'C':
	    *flags |= PF_CHOWN;
	    break;
	default:
	    return E_INVARG;
	}
    }

    if (len == 2)
	*name = 0;
    else
	*name = v.v.list[3].v.str;

    return E_NONE;
}

static enum error
set_prop_info(Var obj, const char *pname, Var info, Objid progr)
{
    Objid new_owner;
    unsigned new_flags;
    const char *new_name;
    enum error e;
    db_prop_handle h;

    if (!obj.is_object())
	e = E_TYPE;
    else if (!is_valid(obj))
	e = E_INVARG;
    else
	e = validate_prop_info(info, &new_owner, &new_flags, &new_name);

    if (e != E_NONE)
	return e;

    h = db_find_property(obj, pname, 0);

    if (!h.ptr || db_is_property_built_in(h))
	return E_PROPNF;
    else if (!db_property_allows(h, progr, PF_WRITE)
	     || (!is_wizard(progr) && db_property_owner(h) != new_owner))
	return E_PERM;

    if (new_name) {
	if (!db_rename_propdef(obj, pname, new_name))
	    return E_INVARG;

	h = db_find_property(obj, new_name, 0);
    }
    db_set_property_owner(h, new_owner);
    db_set_property_flags(h, new_flags);

    return E_NONE;
}

static package
bf_set_prop_info(Var arglist, Byte next, void *vdata, Objid progr)
{				/* (object, prop-name, {owner, perms [, new-name]}) */
    Var obj = arglist.v.list[1];
    const char *pname = arglist.v.list[2].v.str;
    Var info = arglist.v.list[3];
    enum error e = set_prop_info(obj, pname, info, progr);

    free_var(arglist);

    if (e == E_NONE)
	return no_var_pack();
    else
	return make_error_pack(e);
}

static package
bf_add_prop(Var arglist, Byte next, void *vdata, Objid progr)
{				/* (object, prop-name, initial-value, initial-info) */
    Var obj = arglist.v.list[1];
    const char *pname = arglist.v.list[2].v.str;
    Var value = arglist.v.list[3];
    Var info = arglist.v.list[4];
    Objid owner;
    unsigned flags;
    const char *new_name;
    enum error e;

    if ((e = validate_prop_info(info, &owner, &flags, &new_name)) != E_NONE)
	; /* already failed */
    else if (new_name || !obj.is_object())
	e = E_TYPE;
    else if (!is_valid(obj))
	e = E_INVARG;
    else if (!db_object_allows(obj, progr, FLAG_WRITE)
	     || (progr != owner && !is_wizard(progr)))
	e = E_PERM;
    else if (!db_add_propdef(obj, pname, value, owner, flags))
	e = E_INVARG;

    free_var(arglist);

    if (e == E_NONE)
	return no_var_pack();
    else
	return make_error_pack(e);
}

static package
bf_delete_prop(Var arglist, Byte next, void *vdata, Objid progr)
{				/* (object, prop-name) */
    Var obj = arglist.v.list[1];
    const char *pname = arglist.v.list[2].v.str;
    enum error e = E_NONE;

    if (!obj.is_object())
	e = E_TYPE;
    if (!is_valid(obj))
	e = E_INVARG;
    else if (!db_object_allows(obj, progr, FLAG_WRITE))
	e = E_PERM;
    else if (!db_delete_propdef(obj, pname))
	e = E_PROPNF;

    free_var(arglist);

    if (e == E_NONE)
	return no_var_pack();
    else
	return make_error_pack(e);
}

static package
bf_clear_prop(Var arglist, Byte next, void *vdata, Objid progr)
{				/* (object, prop-name) */
    Var obj = arglist.v.list[1];
    const char *pname = arglist.v.list[2].v.str;
    db_prop_handle h;
    Var value;
    enum error e;

    if (!obj.is_object())
	e = E_TYPE;
    else if (!is_valid(obj))
	e = E_INVARG;
    else {
	h = db_find_property(obj, pname, 0);
	if (!h.ptr)
	    e = E_PROPNF;
	else if (db_is_property_built_in(h) || !db_property_allows(h, progr, PF_WRITE))
	    e = E_PERM;
	else if (db_is_property_defined_on(h, obj))
	    e = E_INVARG;
	else {
	    value.type = TYPE_CLEAR;
	    db_set_property_value(h, value);
	    e = E_NONE;
	}
    }

    free_var(arglist);

    if (e == E_NONE)
	return no_var_pack();
    else
	return make_error_pack(e);
}

static package
bf_is_clear_prop(Var arglist, Byte next, void *vdata, Objid progr)
{				/* (object, prop-name) */
    Var obj = arglist.v.list[1];
    const char *pname = arglist.v.list[2].v.str;
    db_prop_handle h;
    Var r;
    enum error e;

    if (!obj.is_object())
	e = E_INVARG;
    else if (!is_valid(obj))
	e = E_INVARG;
    else {
	h = db_find_property(obj, pname, 0);
	if (!h.ptr)
	    e = E_PROPNF;
	else if (!db_is_property_built_in(h) && !db_property_allows(h, progr, PF_READ))
	    e = E_PERM;
	else {
	    r.type = TYPE_INT;
	    r.v.num = (!db_is_property_built_in(h) && db_property_value(h).type == TYPE_CLEAR);
	    e = E_NONE;
	}
    }

    free_var(arglist);

    if (e == E_NONE)
	return make_var_pack(r);
    else
	return make_error_pack(e);
}

void
register_property(void)
{
    (void) register_function("properties", 1, 1, bf_properties,
			     TYPE_ANY);
    (void) register_function("property_info", 2, 2, bf_prop_info,
			     TYPE_ANY, TYPE_STR);
    (void) register_function("set_property_info", 3, 3, bf_set_prop_info,
			     TYPE_ANY, TYPE_STR, TYPE_LIST);
    (void) register_function("add_property", 4, 4, bf_add_prop,
			     TYPE_ANY, TYPE_STR, TYPE_ANY, TYPE_LIST);
    (void) register_function("delete_property", 2, 2, bf_delete_prop,
			     TYPE_ANY, TYPE_STR);
    (void) register_function("clear_property", 2, 2, bf_clear_prop,
			     TYPE_ANY, TYPE_STR);
    (void) register_function("is_clear_property", 2, 2, bf_is_clear_prop,
			     TYPE_ANY, TYPE_STR);
}
