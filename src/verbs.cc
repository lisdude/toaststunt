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

#include <string.h>

#include "config.h"
#include "db.h"
#include "execute.h"
#include "functions.h"
#include "list.h"
#include "log.h"
#include "match.h"
#include "parse_cmd.h"
#include "parser.h"
#include "server.h"
#include "storage.h"
#include "structures.h"
#include "unparse.h"
#include "utils.h"
#include "verbs.h"

struct verb_data {
    Var r;
    int i;
};

static int
add_to_list(void *data, const char *verb_name)
{
    struct verb_data *d = (struct verb_data *)data;

    d->i++;
    d->r.v.list[d->i].type = TYPE_STR;
    d->r.v.list[d->i].v.str = str_ref(verb_name);

    return 0;
}

static package
bf_verbs(Var arglist, Byte next, void *vdata, Objid progr)
{   /* (object) */
    Var obj = arglist.v.list[1];

    free_var(arglist);

    if (!obj.is_object())
        return make_error_pack(E_TYPE);
    else if (!is_valid(obj))
        return make_error_pack(E_INVARG);
    else if (!db_object_allows(obj, progr, FLAG_READ))
        return make_error_pack(E_PERM);
    else {
        struct verb_data d;
        d.r = new_list(db_count_verbs(obj));
        d.i = 0;
        db_for_all_verbs(obj, add_to_list, &d);

        return make_var_pack(d.r);
    }
}

static enum error
validate_verb_info(Var v, Objid * owner, unsigned *flags, const char **names)
{
    const char *s;

    if (!(v.type == TYPE_LIST
            && v.v.list[0].v.num == 3
            && v.v.list[1].type == TYPE_OBJ
            && v.v.list[2].type == TYPE_STR
            && v.v.list[3].type == TYPE_STR))
        return E_TYPE;

    *owner = v.v.list[1].v.obj;
    if (!valid(*owner))
        return E_INVARG;

    for (*flags = 0, s = v.v.list[2].v.str; *s; s++) {
        switch (*s) {
            case 'r':
            case 'R':
                *flags |= VF_READ;
                break;
            case 'w':
            case 'W':
                *flags |= VF_WRITE;
                break;
            case 'x':
            case 'X':
                *flags |= VF_EXEC;
                break;
            case 'd':
            case 'D':
                *flags |= VF_DEBUG;
                break;
            default:
                return E_INVARG;
        }
    }

    *names = v.v.list[3].v.str;
    while (**names == ' ')
        (*names)++;
    if (**names == '\0')
        return E_INVARG;

    *names = str_dup(*names);

    return E_NONE;
}

static int
match_arg_spec(const char *s, db_arg_spec * spec)
{
    if (!strcasecmp(s, "none")) {
        *spec = ASPEC_NONE;
        return 1;
    } else if (!strcasecmp(s, "any")) {
        *spec = ASPEC_ANY;
        return 1;
    } else if (!strcasecmp(s, "this")) {
        *spec = ASPEC_THIS;
        return 1;
    } else
        return 0;
}

static int
match_prep_spec(const char *s, db_prep_spec * spec)
{
    if (!strcasecmp(s, "none")) {
        *spec = PREP_NONE;
        return 1;
    } else if (!strcasecmp(s, "any")) {
        *spec = PREP_ANY;
        return 1;
    } else
        return (*spec = db_match_prep(s)) != PREP_NONE;
}

static enum error
validate_verb_args(Var v, db_arg_spec * dobj, db_prep_spec * prep,
                   db_arg_spec * iobj)
{
    if (!(v.type == TYPE_LIST
            && v.v.list[0].v.num == 3
            && v.v.list[1].type == TYPE_STR
            && v.v.list[2].type == TYPE_STR
            && v.v.list[3].type == TYPE_STR))
        return E_TYPE;

    if (!match_arg_spec(v.v.list[1].v.str, dobj)
            || !match_prep_spec(v.v.list[2].v.str, prep)
            || !match_arg_spec(v.v.list[3].v.str, iobj))
        return E_INVARG;

    return E_NONE;
}

static package
bf_add_verb(Var arglist, Byte next, void *vdata, Objid progr)
{   /* (object, info, args) */
    Var obj = arglist.v.list[1];
    Var info = arglist.v.list[2];
    Var args = arglist.v.list[3];
    Var result;
    Objid owner;
    unsigned flags;
    const char *names;
    db_arg_spec dobj, iobj;
    db_prep_spec prep;
    enum error e;

    if ((e = validate_verb_info(info, &owner, &flags, &names)) != E_NONE)
        ; /* already failed */
    else if ((e = validate_verb_args(args, &dobj, &prep, &iobj)) != E_NONE)
        free_str(names);
    else if (!obj.is_object()) {
        free_str(names);
        e = E_INVARG;
    } else if (!is_valid(obj)) {
        free_str(names);
        e = E_INVARG;
    } else if (!db_object_allows(obj, progr, FLAG_WRITE)
               || (progr != owner && !is_wizard(progr))) {
        free_str(names);
        e = E_PERM;
    } else {
        result.type = TYPE_INT;
        result.v.num = db_add_verb(obj, names, owner, flags, dobj, prep, iobj);
    }

    free_var(arglist);

    if (e == E_NONE)
        return make_var_pack(result);
    else
        return make_error_pack(e);
}

enum error
validate_verb_descriptor(Var desc)
{
    if (desc.type == TYPE_STR)
        return E_NONE;
    else if (desc.type != TYPE_INT)
        return E_TYPE;
    else if (desc.v.num <= 0)
        return E_INVARG;
    else
        return E_NONE;
}

db_verb_handle
find_described_verb(Var obj, Var desc)
{
    if (desc.type == TYPE_INT)
        return db_find_indexed_verb(obj, desc.v.num);
    else {
        int flag = server_flag_option("support_numeric_verbname_strings", 0);
        return db_find_defined_verb(obj, desc.v.str, flag);
    }
}

static package
bf_delete_verb(Var arglist, Byte next, void *vdata, Objid progr)
{   /* (object, verb-desc) */
    Var obj = arglist.v.list[1];
    Var desc = arglist.v.list[2];
    db_verb_handle h;
    enum error e;

    if ((e = validate_verb_descriptor(desc)) != E_NONE)
        ; /* e is already set */
    else if (!obj.is_object())
        e = E_TYPE;
    else if (!is_valid(obj))
        e = E_INVARG;
    else if (!db_object_allows(obj, progr, FLAG_WRITE))
        e = E_PERM;
    else {
        h = find_described_verb(obj, desc);
        if (h.ptr)
            db_delete_verb(h);
        else
            e = E_VERBNF;
    }

    free_var(arglist);

    if (e == E_NONE)
        return no_var_pack();
    else
        return make_error_pack(e);
}

static package
bf_verb_info(Var arglist, Byte next, void *vdata, Objid progr)
{   /* (object, verb-desc) */
    Var obj = arglist.v.list[1];
    Var desc = arglist.v.list[2];
    db_verb_handle h;
    Var r;
    unsigned flags;
    char perms[5], *s;
    enum error e;

    if (!obj.is_object()) {
        free_var(arglist);
        return make_error_pack(E_TYPE);
    } else if ((e = validate_verb_descriptor(desc)) != E_NONE
               || (e = E_INVARG, !is_valid(obj))) {
        free_var(arglist);
        return make_error_pack(e);
    }
    h = find_described_verb(obj, desc);
    free_var(arglist);

    if (!h.ptr)
        return make_error_pack(E_VERBNF);
    else if (!db_verb_allows(h, progr, VF_READ))
        return make_error_pack(E_PERM);

    r = new_list(3);
    r.v.list[1].type = TYPE_OBJ;
    r.v.list[1].v.obj = db_verb_owner(h);
    r.v.list[2].type = TYPE_STR;
    s = perms;
    flags = db_verb_flags(h);
    if (flags & VF_READ)
        *s++ = 'r';
    if (flags & VF_WRITE)
        *s++ = 'w';
    if (flags & VF_EXEC)
        *s++ = 'x';
    if (flags & VF_DEBUG)
        *s++ = 'd';
    *s = '\0';
    r.v.list[2].v.str = str_dup(perms);
    r.v.list[3].type = TYPE_STR;
    r.v.list[3].v.str = str_ref(db_verb_names(h));

    return make_var_pack(r);
}

static package
bf_set_verb_info(Var arglist, Byte next, void *vdata, Objid progr)
{   /* (object, verb-desc, {owner, flags, names}) */
    Var obj = arglist.v.list[1];
    Var desc = arglist.v.list[2];
    Var info = arglist.v.list[3];
    Objid new_owner;
    unsigned new_flags;
    const char *new_names;
    enum error e;
    db_verb_handle h;

    if ((e = validate_verb_descriptor(desc)) != E_NONE)
        ; /* e is already set */
    else if (!obj.is_object())
        e = E_TYPE;
    else if (!is_valid(obj))
        e = E_INVARG;
    else
        e = validate_verb_info(info, &new_owner, &new_flags, &new_names);

    if (e != E_NONE) {
        free_var(arglist);
        return make_error_pack(e);
    }

    h = find_described_verb(obj, desc);

    free_var(arglist);

    if (!h.ptr) {
        free_str(new_names);
        return make_error_pack(E_VERBNF);
    } else if (!db_verb_allows(h, progr, VF_WRITE)
               || (!is_wizard(progr) && db_verb_owner(h) != new_owner)) {
        free_str(new_names);
        return make_error_pack(E_PERM);
    }
    db_set_verb_owner(h, new_owner);
    db_set_verb_flags(h, new_flags);
    db_set_verb_names(h, new_names);

    return no_var_pack();
}

static const char *
unparse_arg_spec(db_arg_spec spec)
{
    switch (spec) {
        case ASPEC_NONE:
            return str_dup("none");
        case ASPEC_ANY:
            return str_dup("any");
        case ASPEC_THIS:
            return str_dup("this");
        default:
            panic_moo("UNPARSE_ARG_SPEC: Unknown db_arg_spec!");
            return "";
    }
}

static package
bf_verb_args(Var arglist, Byte next, void *vdata, Objid progr)
{   /* (object, verb-desc) */
    Var obj = arglist.v.list[1];
    Var desc = arglist.v.list[2];
    db_verb_handle h;
    db_arg_spec dobj, iobj;
    db_prep_spec prep;
    Var r;
    enum error e;

    if (!obj.is_object()) {
        free_var(arglist);
        return make_error_pack(E_TYPE);
    } if ((e = validate_verb_descriptor(desc)) != E_NONE
            || (e = E_INVARG, !is_valid(obj))) {
        free_var(arglist);
        return make_error_pack(e);
    }
    h = find_described_verb(obj, desc);
    free_var(arglist);

    if (!h.ptr)
        return make_error_pack(E_VERBNF);
    else if (!db_verb_allows(h, progr, VF_READ))
        return make_error_pack(E_PERM);

    db_verb_arg_specs(h, &dobj, &prep, &iobj);
    r = new_list(3);
    r.v.list[1].type = TYPE_STR;
    r.v.list[1].v.str = unparse_arg_spec(dobj);
    r.v.list[2].type = TYPE_STR;
    r.v.list[2].v.str = str_dup(db_unparse_prep(prep));
    r.v.list[3].type = TYPE_STR;
    r.v.list[3].v.str = unparse_arg_spec(iobj);

    return make_var_pack(r);
}

static package
bf_set_verb_args(Var arglist, Byte next, void *vdata, Objid progr)
{   /* (object, verb-desc, {dobj, prep, iobj}) */
    Var obj = arglist.v.list[1];
    Var desc = arglist.v.list[2];
    Var args = arglist.v.list[3];
    enum error e;
    db_verb_handle h;
    db_arg_spec dobj, iobj;
    db_prep_spec prep;

    if ((e = validate_verb_descriptor(desc)) != E_NONE)
        ; /* e is already set */
    else if (!obj.is_object())
        e = E_TYPE;
    else if (!is_valid(obj))
        e = E_INVARG;
    else
        e = validate_verb_args(args, &dobj, &prep, &iobj);

    if (e != E_NONE) {
        free_var(arglist);
        return make_error_pack(e);
    }
    h = find_described_verb(obj, desc);
    free_var(arglist);

    if (!h.ptr)
        return make_error_pack(E_VERBNF);
    else if (!db_verb_allows(h, progr, VF_WRITE))
        return make_error_pack(E_PERM);

    db_set_verb_arg_specs(h, dobj, prep, iobj);

    return no_var_pack();
}

static void
lister(void *data, const char *line)
{
    Var *r = (Var *) data;
    Var v;

    v.type = TYPE_STR;
    v.v.str = str_dup(line);
    *r = listappend(*r, v);
}

static package
bf_verb_code(Var arglist, Byte next, void *vdata, Objid progr)
{   /* (object, verb-desc [, fully-paren [, indent]]) */
    int nargs = arglist.v.list[0].v.num;
    Var obj = arglist.v.list[1];
    Var desc = arglist.v.list[2];
    int parens = nargs >= 3 && is_true(arglist.v.list[3]);
    int indent = nargs < 4 || is_true(arglist.v.list[4]);
    db_verb_handle h;
    Var code;
    enum error e;

    if (!obj.is_object()) {
        free_var(arglist);
        return make_error_pack(E_TYPE);
    } else if ((e = validate_verb_descriptor(desc)) != E_NONE
               || (e = E_INVARG, !is_valid(obj))) {
        free_var(arglist);
        return make_error_pack(e);
    }
    h = find_described_verb(obj, desc);
    free_var(arglist);

    if (!h.ptr)
        return make_error_pack(E_VERBNF);
    else if (!db_verb_allows(h, progr, VF_READ))
        return make_error_pack(E_PERM);

    code = new_list(0);
    unparse_program(db_verb_program(h), lister, &code, parens, indent,
                    MAIN_VECTOR);

    return make_var_pack(code);
}

static package
bf_set_verb_code(Var arglist, Byte next, void *vdata, Objid progr)
{   /* (object, verb-desc, code) */
    Var obj = arglist.v.list[1];
    Var desc = arglist.v.list[2];
    Var code = arglist.v.list[3];
    int i;
    Program *program;
    db_verb_handle h;
    Var errors;
    enum error e;

    for (i = 1; i <= code.v.list[0].v.num; i++)
        if (code.v.list[i].type != TYPE_STR) {
            free_var(arglist);
            return make_error_pack(E_TYPE);
        }
    if (!obj.is_object()) {
        free_var(arglist);
        return make_error_pack(E_TYPE);
    } else if ((e = validate_verb_descriptor(desc)) != E_NONE
               || (e = E_INVARG, !is_valid(obj))) {
        free_var(arglist);
        return make_error_pack(e);
    }
    h = find_described_verb(obj, desc);
    if (!h.ptr) {
        free_var(arglist);
        return make_error_pack(E_VERBNF);
    } else if (!is_programmer(progr) || !db_verb_allows(h, progr, VF_WRITE)) {
        free_var(arglist);
        return make_error_pack(E_PERM);
    }
    program = parse_list_as_program(code, &errors);
    if (program) {
        if (task_timed_out)
            free_program(program);
        else
        {
#ifdef LOG_CODE_CHANGES
            oklog("CODE_CHANGE: %s (#%" PRIdN ") set verb #%" PRIdN ":%s\n", db_object_name(progr), progr, obj.v.obj, db_verb_names(h));
#endif
            db_set_verb_program(h, program);
        }
    }
    free_var(arglist);
    return make_var_pack(errors);
}

static package
bf_respond_to(Var arglist, Byte next, void *data, Objid progr)
{
    Var object = arglist.v.list[1];
    const char *verb = arglist.v.list[2].v.str;

    if (!object.is_object()) {
        free_var(arglist);
        return make_error_pack(E_TYPE);
    } else if (!is_valid(object)) {
        free_var(arglist);
        return make_error_pack(E_INVARG);
    }

    db_verb_handle h = db_find_callable_verb(object, verb);

    free_var(arglist);

    Var r;

    if (h.ptr) {
        if (db_object_allows(object, progr, FLAG_READ)) {
            r = new_list(2);
            r.v.list[1] = var_ref(db_verb_definer(h));
            r.v.list[2].type = TYPE_STR;
            r.v.list[2].v.str = str_ref(db_verb_names(h));
        }
        else {
            r = Var::new_int(1);
        }
    }
    else {
        r = Var::new_int(0);
    }

    return make_var_pack(r);
}

static int
all_strings(Var arglist)
{
    Var arg;
    int i, c;

    FOR_EACH (arg, arglist, i, c)
    if (!arg.is_str())
        return 0;

    return 1;
}

static package
bf_eval(Var arglist, Byte next, void *data, Objid progr)
{
    package p;
    if (next == 1) {

        if (!is_programmer(progr)) {
            free_var(arglist);
            p = make_error_pack(E_PERM);
        } else if (!all_strings(arglist)) {
            free_var(arglist);
            p = make_error_pack(E_TYPE);
        } else {
            Var errors;
            Program *program = parse_list_as_program(arglist, &errors);

#ifdef LOG_EVALS
            oklog("CODE_EVAL: %s (#%" PRIdN ") evaluated: %s\n", db_object_name(progr), progr, arglist.v.list[1]);
#endif

            free_var(arglist);

            if (program) {
                free_var(errors);
                if (setup_activ_for_eval(program))
                    p = make_call_pack(2, nullptr);
                else {
                    free_program(program);
                    p = make_error_pack(E_MAXREC);
                }
            } else {
                Var r;

                r = new_list(2);
                r.v.list[1].type = TYPE_INT;
                r.v.list[1].v.num = 0;
                r.v.list[2] = errors;
                p = make_var_pack(r);
            }
        }
    } else {            /* next == 2 */
        Var r;

        r = new_list(2);
        r.v.list[1].type = TYPE_INT;
        r.v.list[1].v.num = 1;
        r.v.list[2] = arglist;
        p = make_var_pack(r);
    }
    return p;
}

void
register_verbs(void)
{
    register_function("verbs", 1, 1, bf_verbs,
                      TYPE_ANY);
    register_function("verb_info", 2, 2, bf_verb_info,
                      TYPE_ANY, TYPE_ANY);
    register_function("set_verb_info", 3, 3, bf_set_verb_info,
                      TYPE_ANY, TYPE_ANY, TYPE_LIST);
    register_function("verb_args", 2, 2, bf_verb_args,
                      TYPE_ANY, TYPE_ANY);
    register_function("set_verb_args", 3, 3, bf_set_verb_args,
                      TYPE_ANY, TYPE_ANY, TYPE_LIST);
    register_function("add_verb", 3, 3, bf_add_verb,
                      TYPE_ANY, TYPE_LIST, TYPE_LIST);
    register_function("delete_verb", 2, 2, bf_delete_verb,
                      TYPE_ANY, TYPE_ANY);
    register_function("verb_code", 2, 4, bf_verb_code,
                      TYPE_ANY, TYPE_ANY, TYPE_ANY, TYPE_ANY);
    register_function("set_verb_code", 3, 3, bf_set_verb_code,
                      TYPE_ANY, TYPE_ANY, TYPE_LIST);
    register_function("respond_to", 2, 2, bf_respond_to,
                      TYPE_ANY, TYPE_STR);
    register_function("eval", 1, -1, bf_eval, TYPE_STR);
}
