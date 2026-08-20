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
 * Routines for manipulating properties on DB objects
 *****************************************************************************/

#include <assert.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "collection.h"
#include "config.h"
#include "db.h"
#include "db_private.h"
#include "list.h"
#include "server.h"
#include "storage.h"
#include "utils.h"
#include "waif.h"
#include "log.h"

Propdef
dbpriv_new_propdef(const char *name)
{
    Propdef newprop;

    newprop.name = str_ref(name);
    newprop.hash = str_hash(name);
    return newprop;
}

/*
 * Finds the offset of the properties defined on `target' in `this'.
 * Returns -1 if `target' is not an ancestor of `this'.
 */
static int
properties_offset(Var target, Var _this)
{
    Var ancestor, ancestors;
    int i, c, offset = 0;
    Object *o;

    ancestors = db_ancestors(_this, true);

    FOR_EACH(ancestor, ancestors, i, c) {
        if (equality(target, ancestor, 0))
            break;
        o = dbpriv_dereference(ancestor);
        offset += o->propdefs.cur_length;
    }

    free_var(ancestors);

    return i <= c ? offset : -1;
}

/*
 * Returns true iff `o' defines a property named `pname'.
 */
static int
property_defined_at(const char *pname, int phash, Object *o)
{
    Proplist *props = &(o->propdefs);
    int length = props->cur_length;
    int i;

    for (i = 0; i < length; i++)
        if (props->l[i].hash == phash
                && !strcasecmp(props->l[i].name, pname))
            return 1;

    return 0;
}

/*
 * Return true iff some descendant of `o' defines a property named
 * `pname'.
 */
static int
property_defined_at_or_below(const char *pname, int phash, Object *o,
                             std::unordered_set<Object *> *seen)
{
    /* A descendant reachable along several paths must only be searched
     * once; otherwise this is exponential in the number of diamonds. */
    if (nullptr == o || !seen->insert(o).second)
        return 0;

    Proplist *props = &(o->propdefs);
    int length = props->cur_length;
    int i;

    for (i = 0; i < length; i++)
        if (props->l[i].hash == phash
                && !strcasecmp(props->l[i].name, pname))
            return 1;

    Var children = o->children;
    for (i = 1; i <= children.v.list[0].v.num; i++) {
        Object *child = dbpriv_dereference(children.v.list[i]);
        if (property_defined_at_or_below(pname, phash, child, seen))
            return 1;
    }

    return 0;
}

static int
property_defined_at_or_below(const char *pname, int phash, Object *o)
{
    std::unordered_set<Object *> seen;

    return property_defined_at_or_below(pname, phash, o, &seen);
}

static void
insert_prop2(Var obj, int pos, Pval pval)
{
    Object *o = dbpriv_dereference(obj);
    Pval *new_propval;
    int i, nprops;

    nprops = ++o->nval;
    new_propval = (Pval *)mymalloc(nprops * sizeof(Pval), M_PVAL);

    free_waif_propdefs((WaifPropdefs*)o->waif_propdefs);
    o->waif_propdefs = nullptr;

    for (i = 0; i < pos; i++)
        new_propval[i] = o->propval[i];

    new_propval[pos] = pval;
    new_propval[pos].var = var_ref(pval.var);
    if (new_propval[pos].perms & PF_CHOWN)
        new_propval[pos].owner = o->owner;

    for (i = pos + 1; i < nprops; i++)
        new_propval[i] = o->propval[i - 1];

    if (o->propval)
        myfree(o->propval, M_PVAL);
    o->propval = new_propval;
}

static void
insert_prop(Objid oid, int pos, Pval pval)
{
    insert_prop2(Var::new_obj(oid), pos, pval);
}

static void
insert_prop_recursively(Objid root, int prop_pos, Pval pv)
{
    insert_prop(root, prop_pos, pv);

    pv.var.type = TYPE_CLEAR;   /* do after initial insert_prop so only
                   children will be TYPE_CLEAR */

    Var descendant, descendants = db_descendants(Var::new_obj(root), false);
    int i, c, offset = 0;

    Num perm_count = listlength(descendants);
    std::unordered_set<Object*> seen;
    dbpriv_append_anon_list(root, &descendants, &seen);
    for (i = 1; i <= perm_count; i++)
        dbpriv_append_anon_list(descendants.v.list[i].v.obj, &descendants, &seen);

    int num_descendants = listlength(descendants);
    int *offsets = (int *)mymalloc(num_descendants * sizeof(int), M_INT);

    FOR_EACH(descendant, descendants, i, c) {
        offset = properties_offset(Var::new_obj(root), descendant);
        offsets[i - 1] = offset;
    }

    FOR_EACH(descendant, descendants, i, c) {
        offset = offsets[i - 1];
        if (descendant.type == TYPE_ANON)
            insert_prop2(descendant, offset + prop_pos, pv);
        else
            insert_prop(descendant.v.obj, offset + prop_pos, pv);
    }

    myfree(offsets, M_INT);
    free_var(descendants);
}

int
db_add_propdef(Var obj, const char *pname, Var value, Objid owner,
               unsigned flags)
{
    Object *o;
    Pval pval;
    int i;
    db_prop_handle h;

    o = dbpriv_dereference(obj);

    h = db_find_property(obj, pname, nullptr);

    if (h.ptr || property_defined_at_or_below(pname, str_hash(pname), o))
        return 0;

    if (o->propdefs.cur_length == o->propdefs.max_length) {
        Propdef *old_props = o->propdefs.l;
        int new_size = (o->propdefs.max_length == 0
                        ? 8 : 2 * o->propdefs.max_length);

        o->propdefs.l = (Propdef *)mymalloc(new_size * sizeof(Propdef), M_PROPDEF);
        for (i = 0; i < o->propdefs.max_length; i++)
            o->propdefs.l[i] = old_props[i];
        o->propdefs.max_length = new_size;

        if (old_props)
            myfree(old_props, M_PROPDEF);
    }
    o->propdefs.l[o->propdefs.cur_length++] = dbpriv_new_propdef(pname);

    pval.var = value;
    pval.owner = owner;
    pval.perms = flags;

    /* anonymous objects can't have children */
    if (TYPE_OBJ == obj.type)
        insert_prop_recursively(obj.v.obj, o->propdefs.cur_length - 1, pval);
    else
        insert_prop2(obj, o->propdefs.cur_length - 1, pval);

    return 1;
}


static void
rename_waif_prop_recursively(Var root, const char *old, const char *_new)
{
    Object *o = dbpriv_dereference(root);

    if (o && o->waif_propdefs)
        waif_rename_propdef(o, old, _new);

    /* db_descendants() already returns the whole transitive set, so walk it
     * once.  Recursing into each descendant recomputed the same closure over
     * and over, which blew up on any deep or wide hierarchy. */
    Var descendant, descendants = db_descendants(root, false);
    int i, c = 0;

    FOR_EACH(descendant, descendants, i, c) {
        Object *d = dbpriv_dereference(descendant);
        if (d && d->waif_propdefs)
            waif_rename_propdef(d, old, _new);
    }

    free_var(descendants);
}

int
db_rename_propdef(Var obj, const char *old, const char *_new)
{
    Object *o = dbpriv_dereference(obj);
    Proplist *props = &(o->propdefs);
    int hash = str_hash(old);
    int count = props->cur_length;
    int i;
    db_prop_handle h;

    for (i = 0; i < count; i++) {
        Propdef p;

        p = props->l[i];
        if (p.hash == hash && !strcasecmp(p.name, old)) {
            if (strcasecmp(old, _new) != 0) {   /* not changing just the case */
                h = db_find_property(obj, _new, nullptr);
                if (h.ptr || property_defined_at_or_below(_new, str_hash(_new), o))
                    return 0;
            }
            rename_waif_prop_recursively(obj, props->l[i].name, _new);
            free_str(props->l[i].name);
            props->l[i].name = str_ref(_new);
            props->l[i].hash = str_hash(_new);

            return 1;
        }
    }

    return 0;
}

static void
remove_prop2(Var obj, int pos)
{
    Object *o = dbpriv_dereference(obj);
    Pval *new_propval;
    int i, nprops;

    nprops = --o->nval;

    free_waif_propdefs((WaifPropdefs *)o->waif_propdefs);
    o->waif_propdefs = nullptr;

    free_var(o->propval[pos].var);  /* free deleted property */

    if (nprops) {
        new_propval = (Pval *)mymalloc(nprops * sizeof(Pval), M_PVAL);
        for (i = 0; i < pos; i++)
            new_propval[i] = o->propval[i];
        for (i = pos; i < nprops; i++)
            new_propval[i] = o->propval[i + 1];
    } else
        new_propval = nullptr;

    if (o->propval)
        myfree(o->propval, M_PVAL);
    o->propval = new_propval;
}

static void
remove_prop(Objid oid, int pos)
{
    remove_prop2(Var::new_obj(oid), pos);
}

static void
remove_prop_recursively(Objid root, int prop_pos)
{
    remove_prop(root, prop_pos);

    Var descendant, descendants = db_descendants(Var::new_obj(root), false);
    int i, c, offset = 0;

    Num perm_count = listlength(descendants);
    std::unordered_set<Object*> seen;
    dbpriv_append_anon_list(root, &descendants, &seen);
    for (i = 1; i <= perm_count; i++)
        dbpriv_append_anon_list(descendants.v.list[i].v.obj, &descendants, &seen);

    int num_descendants = listlength(descendants);
    int *offsets = (int *)mymalloc(num_descendants * sizeof(int), M_INT);

    FOR_EACH(descendant, descendants, i, c) {
        offset = properties_offset(Var::new_obj(root), descendant);
        offsets[i - 1] = offset;
    }

    FOR_EACH(descendant, descendants, i, c) {
        offset = offsets[i - 1];
        if (descendant.type == TYPE_ANON)
            remove_prop2(descendant, offset + prop_pos);
        else
            remove_prop(descendant.v.obj, offset + prop_pos);
    }

    myfree(offsets, M_INT);
    free_var(descendants);
}

int
db_delete_propdef(Var obj, const char *pname)
{
    Object *o = dbpriv_dereference(obj);
    Proplist *props = &(o->propdefs);
    int hash = str_hash(pname);
    int count = props->cur_length;
    int max = props->max_length;
    int i, j;

    for (i = 0; i < count; i++) {
        Propdef p;

        p = props->l[i];
        if (p.hash == hash && !strcasecmp(p.name, pname)) {
            if (p.name)
                free_str(p.name);

            if (max > 8 && props->cur_length <= ((max * 3) / 8)) {
                int new_size = max / 2;
                Propdef *new_props;

                new_props = (Propdef *)mymalloc(new_size * sizeof(Propdef), M_PROPDEF);

                for (j = 0; j < i; j++)
                    new_props[j] = props->l[j];
                for (j = i + 1; j < count; j++)
                    new_props[j - 1] = props->l[j];

                myfree(props->l, M_PROPDEF);
                props->l = new_props;
                props->max_length = new_size;
            } else
                for (j = i + 1; j < count; j++)
                    props->l[j - 1] = props->l[j];

            props->cur_length--;

            /* anonymous objects can't have children */
            if (TYPE_OBJ == obj.type)
                remove_prop_recursively(obj.v.obj, i);
            else
                remove_prop2(obj, i);

            return 1;
        }
    }

    return 0;
}

int
db_count_propdefs(Var obj)
{
    return dbpriv_dereference(obj)->propdefs.cur_length;
}

int
db_for_all_propdefs(Var obj, int (*func) (void *, const char *), void *data)
{
    int i;
    Object *o = dbpriv_dereference(obj);
    int len = o->propdefs.cur_length;

    for (i = 0; i < len; i++)
        if (func(data, o->propdefs.l[i].name))
            return 1;

    return 0;
}

int
db_for_all_propvals(Var obj, int (*func) (void *, Var), void *data)
{
    int i;
    Object *o = dbpriv_dereference(obj);
    int len = o->nval;

    for (i = 0; i < len; i++)
        if (func(data, o->propval[i].var))
            return 1;

    return 0;
}

struct contents_data {
    Var r;
    int i;
};

static void
get_bi_value(db_prop_handle h, Var * value)
{
    Object *o = (Object *)h.ptr;

    switch (h.built_in) {
        case BP_NAME:
            value->type = TYPE_STR;
            value->v.str = str_ref(dbpriv_object_name(o));
            break;
        case BP_OWNER:
            value->type = TYPE_OBJ;
            value->v.obj = dbpriv_object_owner(o);
            break;
        case BP_PROGRAMMER:
            value->type = TYPE_INT;
            value->v.num = dbpriv_object_has_flag(o, FLAG_PROGRAMMER);
            break;
        case BP_WIZARD:
            value->type = TYPE_INT;
            value->v.num = dbpriv_object_has_flag(o, FLAG_WIZARD);
            break;
        case BP_R:
            value->type = TYPE_INT;
            value->v.num = dbpriv_object_has_flag(o, FLAG_READ);
            break;
        case BP_W:
            value->type = TYPE_INT;
            value->v.num = dbpriv_object_has_flag(o, FLAG_WRITE);
            break;
        case BP_F:
            value->type = TYPE_INT;
            value->v.num = dbpriv_object_has_flag(o, FLAG_FERTILE);
            break;
        case BP_A:
            value->type = TYPE_INT;
            value->v.num = dbpriv_object_has_flag(o, FLAG_ANONYMOUS);
            break;
        case BP_LOCATION:
            *value = var_ref(dbpriv_object_location(o));
            break;
        case BP_LAST_MOVE:
            *value = var_ref(dbpriv_object_last_move(o));
            break;
        case    BP_CONTENTS:
            *value = var_ref(dbpriv_object_contents(o));
            break;
        default:
            panic_moo("Unknown built-in property in GET_BI_VALUE!");
    }
}

/* does NOT consume `obj' and `name' */
db_prop_handle
db_find_property(Var obj, const char *name, Var *value)
{
    Object *o = dbpriv_dereference(obj);
    int hash = str_hash(name);

    static struct {
        const char *name;
        enum bi_prop prop;
        int hash;
    } ptable[] = {
#define _ENTRY(P,p) { #p, BP_##P, 0 },
        BUILTIN_PROPERTIES(_ENTRY)
#undef _ENTRY
    };
    static int ptable_init = 0;
    db_prop_handle h;
    int i, n;

    if (!ptable_init) {
        for (i = 0; i < Arraysize(ptable); i++)
            ptable[i].hash = str_hash(ptable[i].name);
        ptable_init = 1;
    }

    h.definer = nullptr;
    h.ptr = nullptr;

    for (i = 0; i < Arraysize(ptable); i++) {
        if (ptable[i].hash == hash && !strcasecmp(name, ptable[i].name)) {
            h.built_in = ptable[i].prop;
            h.ptr = o;
            if (value)
                get_bi_value(h, value);
            return h;
        }
    }

    h.built_in = BP_NONE;

    Var ancestor, ancestors = db_ancestors(obj, false);

    Proplist *props = &(o->propdefs);
    Propdef *defs = props->l;
    int length = props->cur_length;

    n = 0;

    for (i = 0; i < length; i++, n++) {
        if (defs[i].hash == hash && !strcasecmp(defs[i].name, name)) {
            h.definer = o;
            h.ptr = o->propval + n;
            goto done;
        }
    }

    Object *t;
    int ai, ac;

    FOR_EACH(ancestor, ancestors, ai, ac) {
        if (!is_valid(ancestor))
            continue;

        t = dbpriv_dereference(ancestor);

        props = &(t->propdefs);
        defs = props->l;
        length = props->cur_length;

        for (i = 0; i < length; i++, n++) {
            if (defs[i].hash == hash && !strcasecmp(defs[i].name, name)) {
                h.definer = t;
                h.ptr = o->propval + n;
                goto done;
            }
        }
    }

done:

    free_var(ancestors);

    if (!h.ptr)
        return h;

    if (value) {
        Pval *prop = (Pval *)h.ptr;

        while (prop->var.type == TYPE_CLEAR) {
            /* We take a few liberties at this point.  If a property
             * value on an object is clear, then its `definer' must be
             * a permanent (not an anonymous) object, because
             * anonymous objects can't currently be parents of other
             * objects.  Thus `new_obj()' below is okay.
             */
            if (TYPE_LIST == o->parents.type) {
                Var parent, parents = o->parents;
                int i2, c2, offset = 0;
                FOR_EACH(parent, parents, i2, c2)
                if ((offset = properties_offset(Var::new_obj(((Object *)h.definer)->id), parent)) > -1)
                    break;
                o = dbpriv_find_object(parent.v.obj);
                prop = o->propval + offset + i;
            }
            else if (TYPE_OBJ == o->parents.type && NOTHING != o->parents.v.obj) {
                int offset = properties_offset(Var::new_obj(((Object *)h.definer)->id), o->parents);
                o = dbpriv_find_object(o->parents.v.obj);
                prop = o->propval + offset + i;
            }
        }
        *value = prop->var;
    }

    return h;
}

int
db_is_property_defined_on(db_prop_handle h, Var obj)
{
    return (dbpriv_dereference(obj) == h.definer);
}

int
db_is_property_built_in(db_prop_handle h)
{
    return h.built_in;
}

Var
db_property_value(db_prop_handle h)
{
    Var value;

    if (h.built_in)
        get_bi_value(h, &value);
    else {
        Pval *prop = (Pval *)h.ptr;

        value = prop->var;
    }

    return value;
}

void
db_set_property_value(db_prop_handle h, Var value)
{
    if (!h.built_in) {
        Pval *prop = (Pval *)h.ptr;

        free_var(prop->var);
        prop->var = value;
    } else {
        Object *o = (Object *)h.ptr;
        db_object_flag flag;

        switch (h.built_in) {
            case BP_NAME:
                if (value.type != TYPE_STR)
                    goto complain;
                dbpriv_set_object_name(o, value.v.str);
                break;
            case BP_OWNER:
                if (value.type != TYPE_OBJ)
                    goto complain;
                dbpriv_set_object_owner(o, value.v.obj);
                break;
            case BP_PROGRAMMER:
                flag = FLAG_PROGRAMMER;
                goto finish_flag;
            case BP_WIZARD:
                flag = FLAG_WIZARD;
                goto finish_flag;
            case BP_R:
                flag = FLAG_READ;
                goto finish_flag;
            case BP_W:
                flag = FLAG_WRITE;
                goto finish_flag;
            case BP_F:
                flag = FLAG_FERTILE;
                goto finish_flag;
            case BP_A:
                flag = FLAG_ANONYMOUS;
finish_flag:
                if (is_true(value))
                    dbpriv_set_object_flag(o, flag);
                else
                    dbpriv_clear_object_flag(o, flag);
                free_var(value);
                break;
            case BP_LOCATION:
            case BP_LAST_MOVE:
            case BP_CONTENTS:
complain:
                panic_moo("Inappropriate value in DB_SET_PROPERTY_VALUE!");
                break;
            default:
                panic_moo("Unknown built-in property in DB_SET_PROPERTY_VALUE!");
        }
    }
}

Objid
db_property_owner(db_prop_handle h)
{
    if (h.built_in) {
        panic_moo("Built-in property in DB_PROPERTY_OWNER!");
        return NOTHING;
    } else {
        Pval *prop = (Pval *)h.ptr;

        return prop->owner;
    }
}

void
db_set_property_owner(db_prop_handle h, Objid oid)
{
    if (h.built_in)
        panic_moo("Built-in property in DB_SET_PROPERTY_OWNER!");
    else {
        Pval *prop = (Pval *)h.ptr;

        prop->owner = oid;
    }
}

unsigned
db_property_flags(db_prop_handle h)
{
    if (h.built_in) {
        panic_moo("Built-in property in DB_PROPERTY_FLAGS!");
        return 0;
    } else {
        Pval *prop = (Pval *)h.ptr;

        return prop->perms;
    }
}

void
db_set_property_flags(db_prop_handle h, unsigned flags)
{
    if (h.built_in)
        panic_moo("Built-in property in DB_SET_PROPERTY_FLAGS!");
    else {
        Pval *prop = (Pval *)h.ptr;

        prop->perms = flags;
    }
}

int
db_property_allows(db_prop_handle h, Objid progr, db_prop_flag flag)
{
    return ((db_property_flags(h) & flag)
            || progr == db_property_owner(h)
            || is_wizard(progr));
}

/*
 * `parents' is the proposed set of new parents.  Ensure that object
 * `obj' or one of its descendants (or anonymous children) does not
 * define a property with the same name as one defined on `parents' or
 * any of their ancestors.  Ensure that none of the `parents' nor
 * their ancestors define a property with the same name.
 */
int
dbpriv_check_properties_for_chparent(Var obj, Var parents, Var anon_kids)
{
    /* build a hypothetical list of ancestors from the supplied parents */
    /* `obj' must not be in any of `parents' ancestors */

    Var ancestors = new_list(0);
    Var stack = enlist_var(var_dup(parents));
    Var top;

    /* `seen' is what keeps this linear.  Without it the walk follows every
     * distinct path through the inheritance DAG rather than every distinct
     * node, which is exponential in the number of stacked diamonds -- and
     * this runs inside a builtin, where the task timeout cannot preempt it.
     * (`setadd' below de-duplicates the *result*, not the *traversal*.) */
    std::unordered_set<Object *> seen;

    while (listlength(stack) > 0) {
        POP_TOP(top, stack);
        if (is_valid(top)) {
            Object *o = dbpriv_dereference(top);
            if (!seen.insert(o).second) {
                free_var(top);
                continue;
            }
            Var tmp = o->parents;
            tmp = enlist_var(var_ref(tmp));
            stack = listconcat(tmp, stack);
            ancestors = setadd(ancestors, top);
        }
        else {
            /* POP_TOP took a reference; setadd() would have consumed it. */
            free_var(top);
        }
    }

    free_var(stack);

    int has_kids = (TYPE_LIST == anon_kids.type && listlength(anon_kids) > 0);
    Object *o2, *o3, *o = dbpriv_dereference(obj);
    Proplist *props;
    Var ancestor;
    int i, c, x;
    int i2, c2;

    /* check props in descendants & anonymous children */

    FOR_EACH(ancestor, ancestors, i, c) {
        o2 = dbpriv_dereference(ancestor);
        props = &o2->propdefs;

        for (x = 0; x < props->cur_length; x++) {
            if (property_defined_at_or_below(props->l[x].name,
                                             props->l[x].hash,
                                             o)) {
                free_var(ancestors);
                return 0;
            }

            if (has_kids) {
                FOR_EACH(obj, anon_kids, i2, c2) {
                    o3 = dbpriv_dereference(obj);
                    if (property_defined_at(props->l[x].name,
                                            props->l[x].hash,
                                            o3)) {
                        free_var(ancestors);
                        return 0;
                    }
                }
            }
        }
    }

    /* check props in parents */

    FOR_EACH(ancestor, ancestors, i, c) {
        o2 = dbpriv_dereference(ancestor);
        props = &o2->propdefs;

        FOR_EACH(obj, ancestors, i2, c2) {
            if (equality(obj, ancestor, 0))
                continue;
            o3 = dbpriv_dereference(obj);
            for (x = 0; x < props->cur_length; x++) {
                if (property_defined_at(props->l[x].name,
                                        props->l[x].hash,
                                        o3)) {
                    free_var(ancestors);
                    return 0;
                }
            }
        }
    }

    free_var(ancestors);
    return 1;
}

/*
 * Given lists of `old_ancestors' and `new_ancestors', fix the
 * properties of `obj' by 1) preserving properties whose definition is
 * present in both `old_ancestors' and `new_ancestors', 2) removing
 * all properties whose definition is not present in `new_ancestors',
 * and 3) adding new clear properties for properties whose definition
 * was added in `new_ancestors'.
 *
 * Consider the following graph.  The challenge is to figure out what
 * properties we must preserve when we chparent `a' to `c' (bypassing
 * `b' which is still a parent of `f').
 *
 *      x - m
 *     /     \
 *    z       a ----- b - c
 *     \     /       /
 *      y - n       /
 *           \     /
 *            e - f
 *
 * Ancestors Before
 *  m - a, b, c
 *  x - m, a, b, c
 *  n - a, b, c, e, f
 *  y - n, a, b, c, e, f
 *  z - x, m, a, b, c, y, n, e, f
 *
 * Ancestors After
 *  m - a, c
 *  x - m, a, c
 *  n - a, c, e, f, b
 *  y - n, a, c, e, f, b
 *  z - x, m, a, c, y, n, e, f, b
 *
 * Note that for `z', `b' is still an ancestor, however the path
 * changed from _through `x'_ to _through `y'_.  If `z' changed the
 * value of a property on `b', then that value should be preserved.
 * If the value of a property on `b' was clear, then access through
 * `x' will pick up the non-clear value from an ancestor based on the
 * new in inheritance path.  This applies to `y' and `n', as well.
 *
 * In short, in the case of `z', `y' and `n', the properties of `b'
 * should be preserved, even though `b' moved in the inheritance path.
 * The only thing that should change is the apparent value from clear
 * properties.
 *
 * Note!  Ownership of 'c' properties is a sticky issue -- however
 * ickyness exists in the single-inheritance implementation, too --
 * once ownership is established in a child via the `c' flag, it is
 * never revoked, even if the parent changes or the parent changes the
 * `c' flag!
 *
 * Implementation: for this object and all of its descendants,
 * calculate the set of old ancestors, the set of new ancestors, and
 * find their intersection.  The layout may change, but as we're
 * laying out the properties in memory, consult the intersection, and
 * preserve information about those properties.
 */
/*
 * Re-lay-out a single object's property values, given the ancestor lists it
 * had before and after the chparent.  Does not touch anything else.
 */
static void
relayout_properties(Var obj, Var old_ancestors, Var new_ancestors)
{
    Object *o;
    Var ancestor;

    /*
     * Property values are laid corresponding to the order of
     * ancestors in the inheritance hierarchy.  The offsets arrays
     * hold the starting point of each sub-array.
     */
    int offset;
    int old_count, *old_offsets = (int *)mymalloc((listlength(old_ancestors) + 1) * sizeof(int), M_INT);
    int new_count, *new_offsets = (int *)mymalloc((listlength(new_ancestors) + 1) * sizeof(int), M_INT);
    int i1, c1;

    /* C arrays start at index 0, MOO arrays start at index 1 */

    offset = 0;
    FOR_EACH(ancestor, old_ancestors, i1, c1) {
        o = dbpriv_dereference(ancestor);
        old_offsets[i1 - 1] = offset;
        offset += o->propdefs.cur_length;
    }
    old_count = old_offsets[i1 - 1] = offset;

    offset = 0;
    FOR_EACH(ancestor, new_ancestors, i1, c1) {
        o = dbpriv_dereference(ancestor);
        new_offsets[i1 - 1] = offset;
        offset += o->propdefs.cur_length;
        free_waif_propdefs((WaifPropdefs*)o->waif_propdefs);
        o->waif_propdefs = nullptr;
    }
    new_count = new_offsets[i1 - 1] = offset;

    /*
     * Iterate through the new ancestors.  Copy property values that
     * are in both the new ancestors and the old ancestors.
     * Otherwise, add new clear property values and delete any
     * remaining property values.
     */
    Object *me = dbpriv_dereference(obj);
    Pval *new_propval = nullptr;

    /* The ancestry snapshot taken before the graph changed should make this
     * hold.  Keep the checks below anyway: a violation must degrade into
     * clear properties, never into a walk off the end of `propval'. */
    const int have = (int)me->nval;
    if (old_count != have)
        errlog("PROPERTY LAYOUT: %s expected %d old properties but has %d\n",
               (TYPE_OBJ == obj.type) ? "object" : "anonymous object",
               old_count, have);

    if (new_count != 0) {
        new_propval = (Pval *)mymalloc(new_count * sizeof(Pval), M_PVAL);
        int i2, c2, i3, c3;
        FOR_EACH(ancestor, new_ancestors, i2, c2) {
            int n1 = new_offsets[i2 - 1];
            int n2 = new_offsets[i2];
            int l, x;
            if ((l = ismember(ancestor, old_ancestors, 1)) > 0) {
                int o1 = old_offsets[l - 1];
                int o2 = old_offsets[l];
                for (x = o1; x < o2; x++, n1++) {
                    if (nullptr == me->propval || x < 0 || x >= have) {
                        new_propval[n1].var = clear;
                        new_propval[n1].owner = me->owner;
                        new_propval[n1].perms = 0;
                        continue;
                    }
                    new_propval[n1].var = var_ref(me->propval[x].var);
                    new_propval[n1].owner = me->propval[x].owner;
                    new_propval[n1].perms = me->propval[x].perms;
                }
            }
            else {
                /* This ancestor is newly inherited.  Copy the owner/perms of
                 * its property slots from whichever parent it arrived
                 * through.  `po' stays null if no parent yields it, which
                 * should not happen -- but the old code then indexed a stale
                 * `offset' off an arbitrary object. */
                Var parent, parents = enlist_var(var_ref(me->parents));
                Object *po = nullptr;
                int poffset = -1;
                FOR_EACH(parent, parents, i3, c3) {
                    if (!valid(parent.v.obj))
                        continue;
                    if ((poffset = properties_offset(ancestor, parent)) > -1) {
                        po = dbpriv_find_object(parent.v.obj);
                        break;
                    }
                }
                free_var(parents);
                for (x = 0; n1 < n2; x++, n1++) {
                    new_propval[n1].var = clear;
                    if (nullptr == po || nullptr == po->propval || poffset < 0
                            || poffset + x < 0 || poffset + x >= (int)po->nval) {
                        new_propval[n1].owner = me->owner;
                        new_propval[n1].perms = 0;
                        continue;
                    }
                    Pval pv = po->propval[poffset + x];
                    new_propval[n1].owner = pv.perms & PF_CHOWN ? me->owner : pv.owner;
                    new_propval[n1].perms = pv.perms;
                }
            }
        }
    }

    /*
     * Clean up.
     */
    int c;
    if (me->propval) {
        for (c = 0; c < have; c++)
            free_var(me->propval[c].var);
        myfree(me->propval, M_PVAL);
    }
    me->propval = new_propval;
    me->nval = new_count;

    myfree(old_offsets, M_INT);
    myfree(new_offsets, M_INT);
}

/*
 * Multiple inheritance makes the fixup after a chparent more delicate than a
 * recursive descent over `children'.
 *
 *   - An object can be reachable from the reparented object along more than
 *     one path (a "diamond"), so a plain recursion visits it more than once
 *     and re-lays-out data an earlier visit already rewrote.
 *
 *   - Worse, an affected object's *old* ancestor list cannot be recovered
 *     once the graph has been mutated.  The old code rebuilt it by calling
 *     db_ancestors() on the object's other parents -- but if one of those is
 *     itself a descendant of the reparented object, that call returns the
 *     *new* ancestry, which silently inflates the old layout and sends the
 *     copy and free loops past the end of `propval'.
 *
 * So the old ancestry of every affected object is snapshotted *before* the
 * graph changes, and afterwards each of them is re-laid-out exactly once,
 * parents before children.
 */

struct AncestrySnapshot {
    std::vector<Var> objs;
    std::vector<Var> old_ancestors;
};

void *
dbpriv_snapshot_ancestry(Var obj, Var anon_kids)
{
    AncestrySnapshot *snap = new AncestrySnapshot();

    /* `obj' and every permanent object below it: exactly the set whose
     * ancestry -- and therefore property layout -- a chparent can change. */
    Var affected = (TYPE_OBJ == obj.type)
                   ? db_descendants(obj, true)
                   : enlist_var(var_ref(obj));

    /* ...plus their anonymous children.  Anonymous objects cannot be parents,
     * so they are always leaves of the affected set. */
    std::unordered_set<Object *> seen;
    Var anons = new_list(0);
    if (TYPE_OBJ == obj.type) {
        Var d;
        int i, c;
        FOR_EACH(d, affected, i, c)
            dbpriv_append_anon_list(d.v.obj, &anons, &seen);
    }
    if (TYPE_LIST == anon_kids.type) {
        Var k;
        int i, c;
        FOR_EACH(k, anon_kids, i, c)
            if (TYPE_ANON == k.type && seen.insert(k.v.anon).second)
                anons = listappend(anons, var_ref(k));
    }
    affected = listconcat(affected, anons);

    Var a;
    int i, c;
    FOR_EACH(a, affected, i, c) {
        snap->objs.push_back(var_ref(a));
        snap->old_ancestors.push_back(db_ancestors(a, true));
    }

    free_var(affected);

    return snap;
}

void
dbpriv_free_ancestry_snapshot(void *snapshot)
{
    AncestrySnapshot *snap = (AncestrySnapshot *)snapshot;
    size_t n = snap->objs.size();

    for (size_t k = 0; k < n; k++) {
        free_var(snap->objs[k]);
        free_var(snap->old_ancestors[k]);
    }

    delete snap;
}

void
dbpriv_fix_properties_after_chparent(void *snapshot)
{
    AncestrySnapshot *snap = (AncestrySnapshot *)snapshot;
    size_t n = snap->objs.size();
    size_t k;

    /* By far the most common case: a create(), or a chparent of an object
     * with nothing below it.  There is only one object to re-lay-out, so
     * skip the ordering machinery entirely rather than allocating a map,
     * two vectors and a set to sort a single element. */
    if (1 == n) {
        if (nullptr != dbpriv_dereference(snap->objs[0])) {
            Var new_ancestors = db_ancestors(snap->objs[0], true);
            relayout_properties(snap->objs[0], snap->old_ancestors[0],
                                new_ancestors);
            free_var(new_ancestors);
        }
        dbpriv_free_ancestry_snapshot(snapshot);
        return;
    }

    std::unordered_map<Object *, size_t> index;
    for (k = 0; k < n; k++) {
        Object *o = dbpriv_dereference(snap->objs[k]);
        if (nullptr != o)
            index[o] = k;
    }

    /* Order the affected objects so that a parent is always re-laid-out
     * before its children: relayout_properties() reads a parent's `propval'
     * using new-layout offsets when a property is newly inherited. */
    std::vector<int> indegree(n, 0);
    std::vector<std::vector<size_t> > children(n);

    for (k = 0; k < n; k++) {
        Object *o = dbpriv_dereference(snap->objs[k]);
        if (nullptr == o)
            continue;
        Var parent, parents = enlist_var(var_ref(o->parents));
        std::unordered_set<size_t> counted;
        int i, c;

        FOR_EACH(parent, parents, i, c) {
            if (TYPE_OBJ != parent.type || NOTHING == parent.v.obj)
                continue;

            Object *po = dbpriv_find_object(parent.v.obj);
            if (nullptr == po)
                continue;

            auto it = index.find(po);
            if (it == index.end() || it->second == k)
                continue;
            /* A parents list should not contain duplicates, but tolerate
             * one rather than deadlocking the ordering below. */
            if (!counted.insert(it->second).second)
                continue;

            children[it->second].push_back(k);
            indegree[k]++;
        }

        free_var(parents);
    }

    std::vector<size_t> order;
    order.reserve(n);
    for (k = 0; k < n; k++)
        if (0 == indegree[k])
            order.push_back(k);
    for (size_t q = 0; q < order.size(); q++) {
        std::vector<size_t> &kids = children[order[q]];
        for (size_t j = 0; j < kids.size(); j++)
            if (0 == --indegree[kids[j]])
                order.push_back(kids[j]);
    }
    if (order.size() < n) {
        /* Only reachable if the hierarchy is somehow cyclic.  Don't leave
         * anybody with a stale layout. */
        std::vector<bool> queued(n, false);
        for (size_t q = 0; q < order.size(); q++)
            queued[order[q]] = true;
        for (k = 0; k < n; k++)
            if (!queued[k])
                order.push_back(k);
    }

    for (size_t q = 0; q < order.size(); q++) {
        k = order[q];
        if (nullptr == dbpriv_dereference(snap->objs[k]))
            continue;
        Var new_ancestors = db_ancestors(snap->objs[k], true);
        relayout_properties(snap->objs[k], snap->old_ancestors[k], new_ancestors);
        free_var(new_ancestors);
    }

    dbpriv_free_ancestry_snapshot(snapshot);
}
