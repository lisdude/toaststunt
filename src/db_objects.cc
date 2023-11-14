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
 * Routines for manipulating DB objects
 *****************************************************************************/

#include <string.h>

#include <ctime>
#include "config.h"
#include "db.h"
#include "db_io.h"
#include "db_private.h"
#include "collection.h"
#include "list.h"
#include "program.h"
#include "server.h"
#include "storage.h"
#include "utils.h"
#include "dependencies/xtrapbits.h"
#include "map.h"
#include <unordered_map>
#include "options.h"
#include "log.h"

static Object **objects;
static Num num_objects = 0;
static Num max_objects = 0;

static unsigned int nonce = 0;

static Var all_users;

#ifdef USE_ANCESTOR_CACHE
static std::unordered_map <int, Var> ancestor_cache;
#endif /* USE_ANCESTOR_CACHE */

/* used in graph traversals */
static unsigned char *bit_array;
static size_t array_size = 0;

/*********** Objects qua objects ***********/

Object *
dbpriv_find_object(Objid oid)
{
    if (oid < 0 || oid >= max_objects)
        return nullptr;
    else
        return objects[oid];
}

int
valid(Objid oid)
{
    return dbpriv_find_object(oid) != nullptr;
}

Objid
db_last_used_objid(void)
{
    return num_objects - 1;
}

void
db_reset_last_used_objid(void)
{
    while (!objects[num_objects - 1])
        num_objects--;
    db_clear_ancestor_cache();
}

void
db_set_last_used_objid(Objid oid)
{
    while (!objects[num_objects - 1] && num_objects > oid)
        num_objects--;
}

static void
extend(unsigned int new_objects)
{
    int size;

    for (size = 128; size <= new_objects; size *= 2)
        ;

    if (max_objects == 0) {
        objects = (Object **)mymalloc(size * sizeof(Object *), M_OBJECT_TABLE);
        memset(objects, 0, size * sizeof(Object *));
        max_objects = size;
    }
    if (size > max_objects) {
        Object **_new = (Object **)mymalloc(size * sizeof(Object *), M_OBJECT_TABLE);
        memcpy(_new, objects, max_objects * sizeof(Object *));
        memset(_new + max_objects, 0, (size - max_objects) * sizeof(Object *));
        myfree(objects, M_OBJECT_TABLE);
        objects = _new;
        max_objects = size;
    }

    for (size = 4096; size <= new_objects; size *= 2)
        ;

    if (array_size == 0) {
        bit_array = (unsigned char *)mymalloc((size / 8) * sizeof(unsigned char), M_ARRAY);
        array_size = size;
    }
    if (size > array_size) {
        myfree(bit_array, M_ARRAY);
        bit_array = (unsigned char *)mymalloc((size / 8) * sizeof(unsigned char), M_ARRAY);
        array_size = size;
    }
}

static void
ensure_new_object(void)
{
    extend(num_objects + 1);
}

void
dbpriv_assign_nonce(Object *o)
{
    o->nonce = nonce++;
}

void
dbpriv_after_load(void)
{
    int i;

    for (i = num_objects; i < max_objects; i++) {
        if (objects[i]) {
            dbpriv_assign_nonce(objects[i]);
            objects[i] = nullptr;
        }
    }
}

/* Both `dbpriv_new_object()' and `dbpriv_new_anonymous_object()'
 * allocate space for an `Object' and put the object into the array of
 * Objects.  The difference is the storage type used.  `M_ANON'
 * includes space for reference counts.
 */
Object *
dbpriv_new_object(Num new_objid)
{
    Object *o;

    if (new_objid <= 0 || new_objid >= num_objects) {
        ensure_new_object();
        new_objid = num_objects;
        num_objects++;
    }

    o = objects[new_objid] = (Object *)mymalloc(sizeof(Object), M_OBJECT);
    o->id = new_objid;
    o->waif_propdefs = nullptr;

    return o;
}

Object *
dbpriv_new_anonymous_object(void)
{
    Object *o;

    ensure_new_object();
    o = objects[num_objects] = (Object *)mymalloc(sizeof(Object), M_ANON);
    o->id = NOTHING;
    num_objects++;

    return o;
}

void
dbpriv_new_recycled_object(void)
{
    ensure_new_object();
    num_objects++;
}

void
db_init_object(Object *o)
{
    o->name = str_dup("");
    o->flags = 0;

    o->parents = var_ref(nothing);
    o->children = new_list(0);

    o->location = var_ref(nothing);
    o->last_move = (clear_last_move ? var_ref(zero) : new_map());
    o->contents = new_list(0);

    o->propval = nullptr;
    o->nval = 0;

    o->propdefs.max_length = 0;
    o->propdefs.cur_length = 0;
    o->propdefs.l = nullptr;

    o->verbdefs = nullptr;
}

Objid
db_create_object(Num new_objid)
{
    Object *o;

    if (new_objid <= 0 || new_objid > num_objects)
        new_objid = num_objects;

    o = dbpriv_new_object(new_objid);
    db_init_object(o);

    return o->id;
}

void
db_destroy_object(Objid oid)
{
    Object *o = dbpriv_find_object(oid);
    Verbdef *v, *w;
    int i;

    db_priv_affected_callable_verb_lookup();

    if (!o)
        panic_moo("DB_DESTROY_OBJECT: Invalid object!");

    if (o->location.v.obj != NOTHING ||
            o->contents.v.list[0].v.num != 0 ||
            (o->parents.type == TYPE_OBJ && o->parents.v.obj != NOTHING) ||
            (o->parents.type == TYPE_LIST && o->parents.v.list[0].v.num != 0) ||
            o->children.v.list[0].v.num != 0)
        panic_moo("DB_DESTROY_OBJECT: Not a barren orphan!");

    free_var(o->parents);
    free_var(o->children);

    free_var(o->location);
    free_var(o->last_move);
    free_var(o->contents);

    if (is_user(oid)) {
        Var t;

        t.type = TYPE_OBJ;
        t.v.obj = oid;
        all_users = setremove(all_users, t);
    }
    free_str(o->name);

    for (i = 0; i < o->propdefs.cur_length; i++) {
        /* As an orphan, the only properties on this object are the ones
         * defined on it directly, so these two arrays must be the same length.
         */
        free_str(o->propdefs.l[i].name);
        free_var(o->propval[i].var);
    }
    if (o->propdefs.l)
        myfree(o->propdefs.l, M_PROPDEF);
    if (o->propval)
        myfree(o->propval, M_PVAL);
    o->nval = 0;

    for (v = o->verbdefs; v; v = w) {
        if (v->program)
            free_program(v->program);
        free_str(v->name);
        w = v->next;
        myfree(v, M_VERBDEF);
    }

    myfree(objects[oid], M_OBJECT);
    objects[oid] = nullptr;
}

Var
db_read_anonymous()
{
    Var r;
    int oid;

    if ((oid = dbio_read_num()) == NOTHING) {
        r.type = TYPE_ANON;
        r.v.anon = nullptr;
    } else if (max_objects && (oid < max_objects && objects[oid])) {
        r.type = TYPE_ANON;
        r.v.anon = objects[oid];
        addref(r.v.anon);
    }
    else {
        /* Back up the permanent object count, temporarily store the
         * database id of this anonymous object as the new count, and
         * allocate a new object -- this will force the new anonymous
         * object to be allocated at that position -- restore the
         * count when finished.
         */
        int sav_objects = num_objects;
        num_objects = oid;
        dbpriv_new_anonymous_object();
        num_objects = sav_objects;
        r.type = TYPE_ANON;
        r.v.anon = objects[oid];
    }

    return r;
}

void
db_write_anonymous(Var v)
{
    Objid oid;
    Object *o = (Object *)v.v.anon;

    if (!is_valid(v))
        oid = NOTHING;
    else if (o->id != NOTHING)
        oid = o->id;
    else {
        ensure_new_object();
        objects[num_objects] = o;
        oid = o->id = num_objects;
        num_objects++;
    }

    dbio_write_num(oid);
}

Object *
db_make_anonymous(Objid oid, Objid last)
{
    Object *o = objects[oid];
    Var old_parents = o->parents;
    Var me = Var::new_obj(oid);

    Var parent;
    int i, c;

    /* remove me from my old parents' children */
    if (old_parents.type == TYPE_OBJ && old_parents.v.obj != NOTHING)
        objects[old_parents.v.obj]->children = setremove(objects[old_parents.v.obj]->children, me);
    else if (old_parents.type == TYPE_LIST)
        FOR_EACH(parent, old_parents, i, c)
        objects[parent.v.obj]->children = setremove(objects[parent.v.obj]->children, me);

    objects[oid] = nullptr;
    db_set_last_used_objid(last);

    o->id = NOTHING;

    free_var(o->children);
    free_var(o->location);
    free_var(o->last_move);
    free_var(o->contents);

    /* Last step, reallocate the memory and copy -- anonymous objects
     * require space for reference counting.
     */
    Object *t = (Object *)mymalloc(sizeof(Object), M_ANON);
    memcpy(t, o, sizeof(Object));
    myfree(o, M_OBJECT);

    return t;
}

void
db_destroy_anonymous_object(void *obj)
{
    Object *o = (Object *)obj;
    Verbdef *v, *w;
    int i;

    free_str(o->name);
    o->name = nullptr;

    free_var(o->parents);

    for (i = 0; i < o->propdefs.cur_length; i++)
        free_str(o->propdefs.l[i].name);
    if (o->propdefs.l)
        myfree(o->propdefs.l, M_PROPDEF);
    for (i = 0; i < o->nval; i++)
        free_var(o->propval[i].var);
    if (o->propval)
        myfree(o->propval, M_PVAL);
    o->nval = 0;

    for (v = o->verbdefs; v; v = w) {
        if (v->program)
            free_program(v->program);
        free_str(v->name);
        w = v->next;
        myfree(v, M_VERBDEF);
    }

    dbpriv_set_object_flag(o, FLAG_INVALID);

    /* Since this object could possibly be the root of a cycle, final
     * destruction is handled in the garbage collector if garbage
     * collection is enabled.
     */
    /*myfree(o, M_ANON);*/
}

int
parents_ok(Object *o)
{
    if (TYPE_LIST == o->parents.type) {
        Var parent;
        int i, c;
        FOR_EACH(parent, o->parents, i, c) {
            Objid oid = parent.v.obj;
            if (!valid(oid) || objects[oid]->nonce > o->nonce) {
                dbpriv_set_object_flag(o, FLAG_INVALID);
                return 0;
            }
        }
    }
    else {
        Objid oid = o->parents.v.obj;
        if (NOTHING != oid && (!valid(oid) || objects[oid]->nonce > o->nonce)) {
            dbpriv_set_object_flag(o, FLAG_INVALID);
            return 0;
        }
    }

    return 1;
}

int
anon_valid(Object *o)
{
    return o
           && !dbpriv_object_has_flag(o, FLAG_INVALID)
           && parents_ok(o);
}

int
is_valid(Var obj)
{
    return (TYPE_ANON == obj.type) ?
           anon_valid(obj.v.anon) :
           valid(obj.v.obj);
}

Objid
db_renumber_object(Objid old)
{
    Objid _new;
    Object *o;

#ifdef USE_ANCESTOR_CACHE
    db_clear_ancestor_cache();
#endif /* USE_ANCESTOR_CACHE */

    for (_new = 0; _new < old; _new++) {
        if (objects[_new] == nullptr) {
            /* Change the identity of the object. */
            o = objects[_new] = objects[old];
            objects[old] = nullptr;
            objects[_new]->id = _new;

            /* Fix up the parents/children hierarchy and the
             * location/contents hierarchy.
             */
            int i1, c1, i2, c2;
            Var obj1, obj2;

#define     FIX(up, down)                           \
    if (TYPE_LIST == o->up.type) {                  \
        FOR_EACH(obj1, o->up, i1, c1) {                 \
            FOR_EACH(obj2, objects[obj1.v.obj]->down, i2, c2)       \
            if (obj2.v.obj == old)                  \
                break;                      \
            objects[obj1.v.obj]->down.v.list[i2].v.obj = _new;      \
        }                               \
    }                                   \
    else if (TYPE_OBJ == o->up.type && NOTHING != o->up.v.obj) {    \
        FOR_EACH(obj1, objects[o->up.v.obj]->down, i2, c2)      \
        if (obj1.v.obj == old)                      \
            break;                          \
        objects[o->up.v.obj]->down.v.list[i2].v.obj = _new;     \
    }                                   \
    FOR_EACH(obj1, o->down, i1, c1) {                   \
        if (TYPE_LIST == objects[obj1.v.obj]->up.type) {        \
            FOR_EACH(obj2, objects[obj1.v.obj]->up, i2, c2)     \
            if (obj2.v.obj == old)                  \
                break;                      \
            objects[obj1.v.obj]->up.v.list[i2].v.obj = _new;        \
        }                               \
        else {                              \
            objects[obj1.v.obj]->up.v.obj = _new;           \
        }                               \
    }

            FIX(parents, children);
            FIX(location, contents);

#undef      FIX

            /* Fix up the list of users, if necessary */
            if (is_user(_new)) {
                int i;

                for (i = 1; i <= all_users.v.list[0].v.num; i++)
                    if (all_users.v.list[i].v.obj == old) {
                        all_users.v.list[i].v.obj = _new;
                        break;
                    }
            }
            /* Fix the owners of verbs, properties and objects */
            {
                Objid oid;

                for (oid = 0; oid < num_objects; oid++) {
                    Object *o = objects[oid];
                    Verbdef *v;
                    Pval *p;
                    int i, count;

                    if (!o)
                        continue;

                    if (o->owner == _new)
                        o->owner = NOTHING;
                    else if (o->owner == old)
                        o->owner = _new;

                    for (v = o->verbdefs; v; v = v->next)
                        if (v->owner == _new)
                            v->owner = NOTHING;
                        else if (v->owner == old)
                            v->owner = _new;

                    p = o->propval;
                    count = o->nval;
                    for (i = 0; i < count; i++)
                        if (p[i].owner == _new)
                            p[i].owner = NOTHING;
                        else if (p[i].owner == old)
                            p[i].owner = _new;
                }
            }

            return _new;
        }
    }

    /* There are no recycled objects less than `old', so keep its number. */
    return old;
}

int
db_object_bytes(Var obj)
{
    Object *o = dbpriv_dereference(obj);
    int i, len, count;
    Verbdef *v;

    count = sizeof(Object) + sizeof(Object *);
    count += memo_strlen(o->name) + 1;

    for (v = o->verbdefs; v; v = v->next) {
        count += sizeof(Verbdef);
        count += memo_strlen(v->name) + 1;
        if (v->program)
            count += program_bytes(v->program);
    }

    count += sizeof(Propdef) * o->propdefs.cur_length;
    for (i = 0; i < o->propdefs.cur_length; i++)
        count += memo_strlen(o->propdefs.l[i].name) + 1;

    len = o->nval;
    count += (sizeof(Pval) - sizeof(Var)) * len;
    for (i = 0; i < len; i++)
        count += value_bytes(o->propval[i].var);

    return count;
}


/* Traverse the tree/graph twice.  First to count the maximal number
 * of members, and then to copy the members.  Use the bit array to
 * mark objects that have been copied, to prevent double copies.
 * Note: the final step forcibly sets the length of the list, which
 * may be less than the allocated length.  It's possible to calculate
 * the correct length, but that would require one more round of bit
 * array bookkeeping.
 */
/* The following implementations depend on the fact that an object
 * cannot appear in its own ancestors, descendants, location/contents
 * hierarchy.  It also depends on the fact that an anonymous object
 * cannot appear in any other object's hierarchies.  Consequently,
 * we don't set a bit for the root object (which may not have an id).
 */

#define ARRAY_SIZE_IN_BYTES (array_size / 8)
#define CLEAR_BIT_ARRAY() memset(bit_array, 0, ARRAY_SIZE_IN_BYTES)

#define DEFUNC(name, field)                                                  \
                                                                             \
    static int                                                               \
    db1_count_##name(Object *o)                                              \
    {                                                                        \
        int i, c, n = 0;                                                     \
        Var tmp, field = enlist_var(var_ref(o->field));                      \
        Object *o2;                                                          \
        Objid oid;                                                           \
                                                                             \
        FOR_EACH(tmp, field, i, c) {                                         \
            if (valid(oid = tmp.v.obj)) {                                    \
                if (bit_is_false(bit_array, oid)) {                          \
                    bit_true(bit_array, oid);                                \
                    o2 = dbpriv_find_object(oid);                            \
                    n += db1_count_##name(o2) + 1;                           \
                }                                                            \
            }                                                                \
        }                                                                    \
                                                                             \
        free_var(field);                                                     \
                                                                             \
        return n;                                                            \
    }                                                                        \
                                                                             \
    static void                                                              \
    db2_add_##name(Object *o, Var *plist, int *px)                           \
    {                                                                        \
        int i, c;                                                            \
        Var tmp, field = enlist_var(var_ref(o->field));                      \
        Object *o2;                                                          \
        Objid oid;                                                           \
                                                                             \
        FOR_EACH(tmp, field, i, c) {                                         \
            if (valid(oid = tmp.v.obj)) {                                    \
                if (bit_is_false(bit_array, oid)) {                          \
                    bit_true(bit_array, oid);                                \
                    ++(*px);                                                 \
                    plist->v.list[*px].type = TYPE_OBJ;                      \
                    plist->v.list[*px].v.obj = oid;                          \
                    o2 = dbpriv_find_object(oid);                            \
                    db2_add_##name(o2, plist, px);                           \
                }                                                            \
            }                                                                \
        }                                                                    \
                                                                             \
        free_var(field);                                                     \
    }                                                                        \
                                                                             \
    Var                                                                      \
    db_##name(Var obj, bool full)                                            \
    {                                                                        \
        Object *o;                                                           \
        int n, i = 0;                                                        \
        Var list;                                                            \
                                                                             \
        o = dbpriv_dereference(obj);                                         \
        if ((o->field.type == TYPE_OBJ && o->field.v.obj == NOTHING) ||      \
                (o->field.type == TYPE_LIST && listlength(o->field) == 0))   \
            return full ? enlist_var(var_ref(obj)) : new_list(0);            \
                                                                             \
        CLEAR_BIT_ARRAY();                                                   \
                                                                             \
        n = db1_count_##name(o) + (full ? 1 : 0);                            \
                                                                             \
        list = new_list(n);                                                  \
                                                                             \
        if (full)                                                            \
            list.v.list[++i] = var_ref(obj);                                 \
                                                                             \
        CLEAR_BIT_ARRAY();                                                   \
                                                                             \
        db2_add_##name(o, &list, &i);                                        \
                                                                             \
        list.v.list[0].v.num = i; /* sketchy */                              \
                                                                             \
        return list;                                                         \
    }

DEFUNC(descendants, children);

/* the following two could be replace by better/more specific implementations */
DEFUNC(all_locations, location);
DEFUNC(all_contents, contents);

#ifdef USE_ANCESTOR_CACHE
DEFUNC(find_ancestors, parents);

Var db_ancestors(Var obj, bool full) {
    Object *o = dbpriv_dereference(obj);

    if (obj.type != TYPE_OBJ || !is_valid(obj) || o->parents.v.obj == NOTHING)
        return db_find_ancestors(obj, full);

    if (ancestor_cache.count(o->id) == 0)
        ancestor_cache[o->id] = db_find_ancestors(obj, false);

    Var ancestors = var_ref(ancestor_cache[o->id]);

    /* The 'full' refcount only needs to be 1 because listinsert will be creating a new list and consuming
     * the second reference to the cached copy (which we just created directly above this). This leaves us
     * with a refcount of 1, which is the new list that we WANT to get destroyed.
     * The non-full list should have a refcount of 2 so that when it gets consumed, the copy in the cache
     * won't be freed. */
    if (full)
        return listinsert(ancestors, var_ref(obj), 1); /* listinsert consumes the list */
    else
        return ancestors;
}
#else
DEFUNC(ancestors, parents);
#endif /* USE_ANCESTOR_CACHE */

#undef DEFUNC

/*********** Object attributes ***********/

Objid
db_object_owner2(Var obj)
{
    return (TYPE_ANON == obj.type) ?
           dbpriv_object_owner(obj.v.anon) :
           db_object_owner(obj.v.obj);
}

Var
db_object_parents2(Var obj)
{
    return (TYPE_ANON == obj.type) ?
           dbpriv_object_parents(obj.v.anon) :
           db_object_parents(obj.v.obj);
}

Var
db_object_children2(Var obj)
{
    return (TYPE_ANON == obj.type) ?
           dbpriv_object_children(obj.v.anon) :
           db_object_children(obj.v.obj);
}

int
db_object_has_flag2(Var obj, db_object_flag f)
{
    return (TYPE_ANON == obj.type) ?
           dbpriv_object_has_flag(obj.v.anon, f) :
           db_object_has_flag(obj.v.obj, f);
}

void
db_set_object_flag2(Var obj, db_object_flag f)
{
    (TYPE_ANON == obj.type) ?
    dbpriv_set_object_flag(obj.v.anon, f) :
    db_set_object_flag(obj.v.obj, f);
}

void
db_clear_object_flag2(Var obj, db_object_flag f)
{
    (TYPE_ANON == obj.type) ?
    dbpriv_clear_object_flag(obj.v.anon, f) :
    db_clear_object_flag(obj.v.obj, f);
}

Objid
dbpriv_object_owner(Object *o)
{
    return o->owner;
}

void
dbpriv_set_object_owner(Object *o, Objid owner)
{
    o->owner = owner;
}

Objid
db_object_owner(Objid oid)
{
    return dbpriv_object_owner(objects[oid]);
}

void
db_set_object_owner(Objid oid, Objid owner)
{
    dbpriv_set_object_owner(objects[oid], owner);
}

const char *
dbpriv_object_name(Object *o)
{
    return o->name;
}

void
dbpriv_set_object_name(Object *o, const char *name)
{
    if (o->name)
        free_str(o->name);
    o->name = name;
}

const char *
db_object_name(Objid oid)
{
    return dbpriv_object_name(objects[oid]);
}

void
db_set_object_name(Objid oid, const char *name)
{
    dbpriv_set_object_name(objects[oid], name);
}

Var
dbpriv_object_parents(Object *o)
{
    return o->parents;
}

Var
dbpriv_object_children(Object *o)
{
    return o->children;
}

Var
db_object_parents(Objid oid)
{
    return dbpriv_object_parents(objects[oid]);
}

Var
db_object_children(Objid oid)
{
    return dbpriv_object_children(objects[oid]);
}

int
db_count_children(Objid oid)
{
    return listlength(objects[oid]->children);
}

int
db_for_all_children(Objid oid, int (*func) (void *, Objid), void *data)
{
    int i, c = db_count_children(oid);

    for (i = 1; i <= c; i++)
        if (func(data, objects[oid]->children.v.list[i].v.obj))
            return 1;

    return 0;
}

static int
check_for_duplicates(Var list)
{
    int i, j, c;

    if (TYPE_LIST != list.type || (c = listlength(list)) < 1)
        return 1;

    for (i = 1; i <= c; i++)
        for (j = i + i; j <= c; j++)
            if (equality(list.v.list[i], list.v.list[j], 1))
                return 0;

    return 1;
}

static int
check_children_of_object(Var obj, Var anon_kids)
{
    Var kid;
    int i, c;

    if (TYPE_LIST != anon_kids.type || listlength(anon_kids) < 1)
        return 1;

    FOR_EACH (kid, anon_kids, i, c) {
        Var parents = db_object_parents2(kid);
        if (TYPE_ANON != kid.type
                || ((TYPE_OBJ == parents.type && !equality(obj, parents, 0))
                    || (TYPE_LIST == parents.type && !ismember(obj, parents, 0))))
            return 0;
    }

    return 1;
}

int
db_change_parents(Var obj, Var new_parents, Var anon_kids)
{
    if (!check_for_duplicates(new_parents))
        return 0;
    if (!check_for_duplicates(anon_kids))
        return 0;
    if (!check_children_of_object(obj, anon_kids))
        return 0;
    if (!dbpriv_check_properties_for_chparent(obj, new_parents, anon_kids))
        return 0;

    Object *o = dbpriv_dereference(obj);

    if (o->verbdefs == nullptr
            && listlength(o->children) == 0
            && (TYPE_LIST != anon_kids.type || listlength(anon_kids) == 0)) {
        /* Since this object has no children and no verbs, we know that it
           can't have had any part in affecting verb lookup, since we use first
           parent with verbs as a key in the verb lookup cache. */
        /* The "no kids" rule is necessary because potentially one of the
           children could have verbs on it--and that child could have cache
           entries for THIS object's parentage. */
        /* In any case, don't clear the cache. */
        ;
    } else {
        db_priv_affected_callable_verb_lookup();
    }

    Var old_parents = o->parents;

    /* save this; we need it later */
    Var old_ancestors = db_ancestors(obj, true);

    /* only adjust the parent's children for permanent objects */
    if (TYPE_OBJ == obj.type) {
        Var parent;
        int i, c;

        /* remove me/obj from my old parents' children */
        if (old_parents.type == TYPE_OBJ && old_parents.v.obj != NOTHING)
            objects[old_parents.v.obj]->children = setremove(objects[old_parents.v.obj]->children, obj);
        else if (old_parents.type == TYPE_LIST)
            FOR_EACH(parent, old_parents, i, c)
            objects[parent.v.obj]->children = setremove(objects[parent.v.obj]->children, obj);

        /* add me/obj to my new parents' children */
        if (new_parents.type == TYPE_OBJ && new_parents.v.obj != NOTHING)
            objects[new_parents.v.obj]->children = setadd(objects[new_parents.v.obj]->children, obj);
        else if (new_parents.type == TYPE_LIST)
            FOR_EACH(parent, new_parents, i, c)
            objects[parent.v.obj]->children = setadd(objects[parent.v.obj]->children, obj);
    }

    free_var(o->parents);
    o->parents = var_dup(new_parents);

    /* Nothing between this point and the completion of
     * `dbpriv_fix_properties_after_chparent' may call `anon_valid'
     * because `o' is currently invalid (the nonce is out of date and
     * the aforementioned call will fix that).
     */

#ifdef USE_ANCESTOR_CACHE
    /* Invalidate the cache for all descendants of the object that is changing parents. */
    Var desc;
    int i, c;

    Var descendants = db_descendants(obj, true);
    FOR_EACH(desc, descendants, i, c) {
        if (ancestor_cache.count(desc.v.obj) > 0) {
            if (var_refcount(ancestor_cache[desc.v.obj]) > 1)
                applog(LOG_ERROR, "Refcount too high for ancestor cache invalidation of #%i. Refcount = %i\n", desc.v.obj, var_refcount(ancestor_cache[desc.v.obj]));
            free_var(ancestor_cache[desc.v.obj]);
            ancestor_cache.erase(desc.v.obj);
        }
    }
    free_var(descendants);
#endif /* USE_ANCESTOR_CACHE */

    Var new_ancestors = db_ancestors(obj, true);

    dbpriv_fix_properties_after_chparent(obj, old_ancestors, new_ancestors, anon_kids);

    free_var(old_ancestors);
    free_var(new_ancestors);

    return 1;
}

Var
dbpriv_object_location(Object *o)
{
    return o->location;
}

Var
dbpriv_object_last_move(Object *o)
{
    return o->last_move;
}

Objid
db_object_location(Objid oid)
{
    return dbpriv_object_location(objects[oid]).v.obj;
}

Var
dbpriv_object_contents(Object *o)
{
    return o->contents;
}

int
db_count_contents(Objid oid)
{
    return listlength(objects[oid]->contents);
}

int
db_for_all_contents(Objid oid, int (*func) (void *, Objid), void *data)
{
    int i, c = db_count_contents(oid);

    for (i = 1; i <= c; i++)
        if (func(data, objects[oid]->contents.v.list[i].v.obj))
            return 1;

    return 0;
}

void
db_change_location(Objid oid, Objid new_location, int position)
{
    static Var time_key = str_dup_to_var("time");
    static Var source_key = str_dup_to_var("source");

    Var me = Var::new_obj(oid);

    Objid old_location = objects[oid]->location.v.obj;

    if (valid(old_location))
        objects[old_location]->contents = setremove(objects[old_location]->contents, var_dup(me));

    if (valid(new_location)) {
        if (position <= 0)
            position = objects[new_location]->contents.v.list[0].v.num + 1;

        objects[new_location]->contents = listinsert(objects[new_location]->contents, me, position);
    }

    free_var(objects[oid]->location);
    objects[oid]->location = Var::new_obj(new_location);
    if (!clear_last_move) {
        if (objects[oid]->last_move.type != TYPE_MAP) {
            free_var(objects[oid]->last_move);
            objects[oid]->last_move = new_map();
        }

        Var last_move = objects[oid]->last_move;
        last_move = mapinsert(last_move, var_ref(time_key), Var::new_int(time(nullptr)));
        last_move = mapinsert(last_move, var_ref(source_key), Var::new_obj(old_location));

        objects[oid]->last_move = last_move;
    }

}

int
dbpriv_object_has_flag(Object *o, db_object_flag f)
{
    return (o->flags & (1 << f)) != 0;
}

void
dbpriv_set_object_flag(Object *o, db_object_flag f)
{
    o->flags |= (1 << f);
}

void
dbpriv_clear_object_flag(Object *o, db_object_flag f)
{
    o->flags &= ~(1 << f);
}

int
db_object_has_flag(Objid oid, db_object_flag f)
{
    return dbpriv_object_has_flag(objects[oid], f);
}

void
db_set_object_flag(Objid oid, db_object_flag f)
{
    dbpriv_set_object_flag(objects[oid], f);

    if (f == FLAG_USER)
        all_users = setadd(all_users, Var::new_obj(oid));
}

void
db_clear_object_flag(Objid oid, db_object_flag f)
{
    dbpriv_clear_object_flag(objects[oid], f);
    if (f == FLAG_USER)
        all_users = setremove(all_users, Var::new_obj(oid));
}

int
db_object_allows(Var obj, Objid progr, db_object_flag f)
{
    return (is_wizard(progr)
            || progr == db_object_owner2(obj)
            || db_object_has_flag2(obj, f));
}

int
is_wizard(Objid oid)
{
    return valid(oid) && db_object_has_flag(oid, FLAG_WIZARD);
}

int
is_programmer(Objid oid)
{
    return valid(oid) && db_object_has_flag(oid, FLAG_PROGRAMMER);
}

int
is_user(Objid oid)
{
    return valid(oid) && db_object_has_flag(oid, FLAG_USER);
}

Var
db_all_users(void)
{
    return all_users;
}

void
dbpriv_set_all_users(Var v)
{
    all_users = v;
}

int
db_object_isa(Var object, Var parent)
{
    if (equality(object, parent, 0))
        return 1;

    Object *o, *t;

    o = (TYPE_OBJ == object.type) ?
        dbpriv_find_object(object.v.obj) :
        object.v.anon;

    Var ancestor, ancestors = enlist_var(var_ref(o->parents));

    while (listlength(ancestors) > 0) {
        POP_TOP(ancestor, ancestors);

        if (ancestor.v.obj == NOTHING)
            continue;

        if (equality(ancestor, parent, 0)) {
            free_var(ancestors);
            return 1;
        }

        t = dbpriv_find_object(ancestor.v.obj);

        ancestors = listconcat(ancestors, enlist_var(var_ref(t->parents)));
    }

    return 0;
}

void
db_fixup_owners(const Objid obj)
{
    for (Objid oid = 0; oid < num_objects; oid++) {
        Object *o = objects[oid];
        Pval *p;

        if (!o)
            continue;

        if (o->owner == obj)
            o->owner = NOTHING;

        for (Verbdef *v = o->verbdefs; v; v = v->next)
            if (v->owner == obj)
                v->owner = NOTHING;

        p = o->propval;
        for (int i = 0, count = o->nval; i < count; i++)
            if (p[i].owner == obj)
                p[i].owner = NOTHING;
    }
}

void
db_clear_ancestor_cache(void)
{
#ifdef USE_ANCESTOR_CACHE /*Just in case */
    for (auto const& x : ancestor_cache)
        free_var(x.second);
    ancestor_cache.clear();
#endif
}
