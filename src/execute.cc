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

#include <chrono>
#include <string.h>
#include <stdarg.h>

#include "collection.h"
#include "config.h"
#include "db.h"
#include "db_io.h"
#include "decompile.h"
#include "eval_env.h"
#include "eval_vm.h"
#include "execute.h"
#include "functions.h"
#include "list.h"
#include "log.h"
#include "map.h"
#include "numbers.h"
#include "opcode.h"
#include "options.h"
#include "parse_cmd.h"
#include "server.h"
#include "storage.h"
#include "streams.h"
#include "structures.h"
#include "sym_table.h"
#include "tasks.h"
#include "timers.h"
#include "utils.h"
#include "waif.h"
#include "version.h"
#include "background.h"
#include "unparse.h"

/* the following globals are the guts of the virtual machine: */
static activation *activ_stack = nullptr;
static Num max_stack_size = 0;
static unsigned top_activ_stack;    /* points to top-of-stack
                                       (last-occupied-slot),
                                        not next-empty-slot */
static Num root_activ_vector;   /* root_activ_vector == MAIN_VECTOR
                                   iff root activation is main
                                   vector */

/* these globals are not part of the vm because they get re-initialized after a suspend */
static int ticks_remaining;
int task_timed_out;
static int interpreter_is_running = 0;
static Timer_ID task_alarm_id;

static const char *handler_verb_name;   /* For in-DB traceback handling */
static Var handler_verb_args;

/* used when loading the database to hold values that may reference yet
   unloaded anonymous objects */
static Var temp_vars = new_list(0);

#ifdef WAIF_DICT
/*
 * Jay Carlson's WAIF DICT patch.  These static moo-strings are needed for
 * the new call_verb2 interface which assumes verb is a moo-string rather
 * than a (non reference-counted) C-string.
 */
static char *waif_index_verb;
static char *waif_indexset_verb;
#endif              /* WAIF_DICT */

#ifdef SAVE_FINISHED_TASKS
static Var map_this;
static Var map_player;
static Var map_programmer;
static Var map_receiver;
static Var map_object;
static Var map_verb;
static Var map_fullverb;
static Var map_foreground;
static Var map_suspended;
static Var map_time;
#endif              /* SAVE_FINISHED_TASKS */

/* macros to ease indexing into activation stack */
#define RUN_ACTIV     activ_stack[top_activ_stack]
#define CALLER_ACTIV  activ_stack[top_activ_stack - 1]

/**** error handling ****/

typedef enum {          /* Reasons for executing a FINALLY handler */
    /* These constants are stored in the DB, so don't change the order... */
    FIN_FALL_THRU, FIN_RAISE, FIN_UNCAUGHT, FIN_RETURN,
    FIN_ABORT,          /* This doesn't actually get you into a FINALLY... */
    FIN_EXIT
} Finally_Reason;

/*
 * Keep a pool of the common size rt_stacks around to avoid beating up on
 * malloc.  This doesn't really need tuning.  Most rt_stacks will be less
 * than size 10.  I rounded up to a size which won't waste a lot of space
 * with a powers-of-two malloc (while leaving some room for mymalloc
 * overhead, if any).
 */
static Var *rt_stack_quick;
#define RT_STACK_QUICKSIZE  15

static void
alloc_rt_stack(activation * a, Num size)
{
    Var *res;

    if (size <= RT_STACK_QUICKSIZE && rt_stack_quick) {
        res = rt_stack_quick;
        rt_stack_quick = rt_stack_quick[0].v.list;
    } else {
        res = (Var *)mymalloc(MAX(size, RT_STACK_QUICKSIZE) * sizeof(Var), M_RT_STACK);
    }
    a->base_rt_stack = a->top_rt_stack = res;
    a->rt_stack_size = size;
}

static void
free_rt_stack(activation * a)
{
    Var *stack = a->base_rt_stack;

    if (a->rt_stack_size <= RT_STACK_QUICKSIZE) {
        stack[0].v.list = rt_stack_quick;
        rt_stack_quick = stack;
    } else
        myfree(stack, M_RT_STACK);
}

void
print_error_backtrace(const char *msg, void (*output) (const char *))
{
    int t;
    Stream *str;

    if (!interpreter_is_running)
        return;
    str = new_stream(100);
    for (t = top_activ_stack; t >= 0; t--) {
        if (t != top_activ_stack)
            stream_printf(str, "... called from ");

        if (TYPE_OBJ == activ_stack[t].vloc.type)
            stream_printf(str, "#%" PRIdN ":%s", activ_stack[t].vloc.v.obj,
                          activ_stack[t].verbname);
        else
            stream_printf(str, "*anonymous*:%s",
                          activ_stack[t].verbname);

        if (equality(activ_stack[t].vloc, activ_stack[t]._this, 0)) {
            stream_add_string(str, " (this == ");
            unparse_value(str, activ_stack[t]._this);
            stream_add_string(str, ")");
        }

        stream_printf(str, ", line %" PRIdN,
                      find_line_number(activ_stack[t].prog,
                                       (t == 0 ? root_activ_vector
                                        : MAIN_VECTOR),
                                       activ_stack[t].error_pc));
        if (t == top_activ_stack)
            stream_printf(str, ":  %s", msg);
        output(reset_stream(str));
        if (t > 0 && activ_stack[t].bi_func_pc) {
            stream_printf(str, "... called from built-in function %s()",
                          name_func_by_num(activ_stack[t].bi_func_id));
            output(reset_stream(str));
        }
    }
    output("(End of traceback)");
    free_stream(str);
}

static void
output_to_log(const char *line)
{
    applog(LOG_INFO2, "%s\n", line);
}

static Var backtrace_list;

static void
output_to_list(const char *line)
{
    Var str;

    str.type = TYPE_STR;
    str.v.str = str_dup(line);
    backtrace_list = listappend(backtrace_list, str);
}

static Var
error_backtrace_list(const char *msg)
{
    backtrace_list = new_list(0);
    print_error_backtrace(msg, output_to_list);
    return backtrace_list;
}

static enum error
suspend_task(package p)
{
    vm the_vm = new_vm(current_task_id, var_ref(current_local), top_activ_stack + 1);
    unsigned int i;
    enum error e;

    the_vm->max_stack_size = max_stack_size;
    the_vm->top_activ_stack = top_activ_stack;
    the_vm->root_activ_vector = root_activ_vector;
    the_vm->func_id = 0;    /* shouldn't need func_id; */
    for (i = 0; i <= top_activ_stack; i++)
        the_vm->activ_stack[i] = activ_stack[i];

    e = (*p.u.susp.proc) (the_vm, p.u.susp.data);
    if (e != E_NONE)
        free_vm(the_vm, 0);
    return e;
}

static int raise_error(package p, enum outcome *outcome);
static void abort_task(enum abort_reason reason);

static int
unwind_stack(Finally_Reason why, Var value, enum outcome *outcome)
{
    /* Returns true iff the interpreter should stop,
     * in which case *outcome is set to the correct outcome to return.
     * Interpreter stops either because it was blocked (OUTCOME_BLOCKED)
     * or the entire stack was unwound (OUTCOME_DONE/OUTCOME_ABORTED)
     *
     * why==FIN_EXIT always returns false
     * why==FIN_ABORT always returns true/OUTCOME_ABORTED
     */
    Var code = (why == FIN_RAISE ? value.v.list[1] : zero);

    for (;;) {          /* loop over activations */
        activation *a = &(activ_stack[top_activ_stack]);
        void *bi_func_data = nullptr;
        int bi_func_pc;
        unsigned bi_func_id = 0;
        Var v, *goal = a->base_rt_stack;

        if (why == FIN_EXIT)
            goal += value.v.list[1].v.num;
        while (a->top_rt_stack > goal) {    /* loop over rt stack */
            a->top_rt_stack--;
            v = *(a->top_rt_stack);
            if (why != FIN_ABORT && v.type == TYPE_FINALLY) {
                /* FINALLY handler */
                a->pc = v.v.num;
                v.type = TYPE_INT;
                v.v.num = why;
                *(a->top_rt_stack++) = v;
                *(a->top_rt_stack++) = value;
                return 0;
            } else if (why == FIN_RAISE && v.type == TYPE_CATCH) {
                /* TRY-EXCEPT or `expr ! ...' handler */
                Var *new_top = a->top_rt_stack - 2 * v.v.num;
                Var *vv;
                int found = 0;

                for (vv = new_top; vv < a->top_rt_stack; vv += 2) {
                    if (!found && (vv->type != TYPE_LIST
                                   || ismember(code, *vv, 0))) {
                        found = 1;
                        v = *(vv + 1);
                        if (v.type != TYPE_INT)
                            panic_moo("Non-numeric PC value on stack!");
                        a->pc = v.v.num;
                    }
                    free_var(*vv);
                }

                a->top_rt_stack = new_top;
                if (found) {
                    *(a->top_rt_stack++) = value;
                    /* Another exception has been raised... */
                    return 0;
                }
            } else {
                free_var(v);
            }
        }
        if (why == FIN_EXIT) {
            a->pc = value.v.list[2].v.num;
            free_var(value);
            return 0;
        }
        bi_func_pc = a->bi_func_pc;
        if (bi_func_pc) {
            bi_func_id = a->bi_func_id;
            bi_func_data = a->bi_func_data;
        }
        free_activation(a, 0);  /* 0 == don't free bi_func_data */

        if (top_activ_stack == 0) { /* done */
            if (outcome)
                *outcome = (why == FIN_RETURN
                            ? OUTCOME_DONE
                            : OUTCOME_ABORTED);
            return 1;
        }
        top_activ_stack--;

        if (bi_func_pc != 0) {  /* Must unwind through a built-in function */
            package p;

            if (why == FIN_RETURN) {
                a = &(activ_stack[top_activ_stack]);
                p = call_bi_func(bi_func_id, value, bi_func_pc, a->progr,
                                 bi_func_data);
                switch (p.kind) {
                    case package::BI_RETURN:
                        *(a->top_rt_stack++) = p.u.ret;
                        return 0;
                    case package::BI_RAISE:
                        if (a->debug)
                            return raise_error(p, outcome);
                        else {
                            *(a->top_rt_stack++) = p.u.raise.code;
                            free_str(p.u.raise.msg);
                            free_var(p.u.raise.value);
                            return 0;
                        }
                    case package::BI_SUSPEND:
                    {
                        enum error e = suspend_task(p);

                        if (e == E_NONE) {
                            if (outcome)
                                *outcome = OUTCOME_BLOCKED;
                            return 1;
                        } else {
                            value.type = TYPE_ERR;
                            value.v.err = e;
                            return unwind_stack(FIN_RAISE, value, outcome);
                        }
                    }
                    case package::BI_CALL:
                        a = &(activ_stack[top_activ_stack]);    /* TOS has changed */
                        a->bi_func_id = bi_func_id;
                        a->bi_func_pc = p.u.call.pc;
                        a->bi_func_data = p.u.call.data;
                        return 0;
                    case package::BI_KILL:
                        abort_task((abort_reason)p.u.ret.v.num);
                        if (outcome)
                            *outcome = OUTCOME_ABORTED;
                        return 1;
                }
            } else {
                /* Built-in functions receive zero as a `returned value' on
                 * errors and aborts, all further calls they make are short-
                 * circuited with an immediate return of zero, and any errors
                 * they raise are squelched.  This is compatible with older,
                 * pre-error-handling versions of the server, and thus
                 * acceptible for the existing built-ins.  It is conceivable
                 * that this model will have to be revisited at some point in
                 * the future.
                 */
                do {
                    p = call_bi_func(bi_func_id, zero, bi_func_pc, a->progr,
                                     bi_func_data);
                    switch (p.kind) {
                        case package::BI_RETURN:
                            free_var(p.u.ret);
                            break;
                        case package::BI_RAISE:
                            free_var(p.u.raise.code);
                            free_str(p.u.raise.msg);
                            free_var(p.u.raise.value);
                            break;
                        case package::BI_SUSPEND:
                        case package::BI_KILL:
                            break;
                        case package::BI_CALL:
                            free_activation(&activ_stack[top_activ_stack--], 0);
                            bi_func_pc = p.u.call.pc;
                            bi_func_data = p.u.call.data;
                            break;
                    }
                } while (p.kind == package::BI_CALL && bi_func_pc != 0);        /* !tailcall */
            }
        } else if (why == FIN_RETURN) {     /* Push the value on the stack & go */
            a = &(activ_stack[top_activ_stack]);
            *(a->top_rt_stack++) = value;
            return 0;
        }
    }
}

static int
find_handler_activ(Var code)
{
    /* Returns the index of the hottest activation with an active exception
     * handler for the given code.
     */
    int frame;

    for (frame = top_activ_stack; frame >= 0; frame--) {
        activation *a = &(activ_stack[frame]);
        Var *v, *vv;

        for (v = a->top_rt_stack - 1; v >= a->base_rt_stack; v--)
            if (v->type == TYPE_CATCH) {
                for (vv = v - 2 * v->v.num; vv < v; vv += 2)
                    if (vv->type != TYPE_LIST || ismember(code, *vv, 0))
                        return frame;
                v -= 2 * v->v.num;
            }
    }

    return -1;
}

Var
make_rt_var_map(Var * rt_env, const char **var_names, unsigned size)
{
    Var rt_vars = new_map();
    int i;

    for (i = 0; i < size; ++i) {
        if (rt_env[i].type != TYPE_NONE)
            rt_vars = mapinsert(rt_vars, str_ref_to_var(var_names[i]), var_ref(rt_env[i]));
    }

    return rt_vars;
}

static Var
make_stack_list(activation * stack, int start, int end, int include_end,
                int root_vector, int line_numbers_too, int include_variables, Objid progr)
{
    Var r, rt_vars;
    int count = 0, listlen = 5, i, j, k;

    if (line_numbers_too)
        listlen++;
    if (include_variables)
        listlen++;

    for (i = end; i >= start; i--) {
        if (include_end || i != end)
            count++;
        if (i != start && stack[i].bi_func_pc)
            count++;
    }

    r = new_list(count);
    j = 1;
    for (i = end; i >= start; i--) {
        Var v;

        if (include_end || i != end) {
            v = r.v.list[j++] = new_list(listlen);
            v.v.list[1] = anonymizing_var_ref(stack[i]._this, progr);
            v.v.list[2] = str_ref_to_var(stack[i].verb);
            v.v.list[3] = Var::new_obj(stack[i].progr);
            v.v.list[4] = anonymizing_var_ref(stack[i].vloc, progr);
            v.v.list[5] = Var::new_obj(stack[i].player);
            if (line_numbers_too) {
                v.v.list[include_variables ? listlen - 1 : listlen].type = TYPE_INT;
                v.v.list[include_variables ? listlen - 1 : listlen].v.num = find_line_number(stack[i].prog,
                        (i == 0 ? root_vector
                         : MAIN_VECTOR),
                        stack[i].error_pc);
            }
            if (include_variables) {
                v.v.list[listlen].type = TYPE_MAP;
                v.v.list[listlen] = make_rt_var_map(stack[i].rt_env, stack[i].prog->var_names, stack[i].prog->num_var_names);
            }
        }
        if (i != start && stack[i].bi_func_pc) {
            v = r.v.list[j++] = new_list(listlen);
            v.v.list[1].type = TYPE_OBJ;
            v.v.list[1].v.obj = NOTHING;
            v.v.list[2].type = TYPE_STR;
            v.v.list[2].v.str = str_dup(name_func_by_num(stack[i].bi_func_id));
            v.v.list[3].type = TYPE_OBJ;
            v.v.list[3].v.obj = NOTHING;
            v.v.list[4].type = TYPE_OBJ;
            v.v.list[4].v.obj = NOTHING;
            v.v.list[5].type = TYPE_OBJ;
            v.v.list[5].v.obj = stack[i].player;
            if (line_numbers_too) {
                v.v.list[include_variables ? listlen - 1 : listlen].type = TYPE_INT;
                v.v.list[include_variables ? listlen - 1 : listlen].v.num = stack[i].bi_func_pc;
            }
            if (include_variables) {
                v.v.list[listlen].type = TYPE_MAP;
                v.v.list[listlen] = make_rt_var_map(stack[i].rt_env, stack[i].prog->var_names, stack[i].prog->num_var_names);
            }
        }
    }

    return r;
}

static void
save_handler_info(const char *vname, Var args)
{
    handler_verb_name = vname;
    free_var(handler_verb_args);
    handler_verb_args = args;
}

/* TODO: both `raise_error()' and `abort_task()' should create a stack
 * list from the point of view of the programmer _catching the error_
 * (not the point of view of the programmer who's verb generated the
 * error) however, this is hard, so for now everyone gets invalid
 * anonymous objects!
 */

static int
raise_error(package p, enum outcome *outcome)
{
    /* ASSERT: p.kind == package::BI_RAISE */
    int handler_activ = find_handler_activ(p.u.raise.code);
    int include_vars = server_int_option("INCLUDE_RT_VARS", 0);
    Finally_Reason why;
    Var value;

    if (handler_activ >= 0) {   /* handler found */
        why = FIN_RAISE;
        value = new_list(4);
    } else {            /* uncaught exception */
        why = FIN_UNCAUGHT;
        value = new_list(5);
        value.v.list[5] = error_backtrace_list(p.u.raise.msg);
        handler_activ = 0;  /* get entire stack in list */
    }
    value.v.list[1] = p.u.raise.code;
    value.v.list[2].type = TYPE_STR;
    value.v.list[2].v.str = p.u.raise.msg;
    value.v.list[3] = p.u.raise.value;
    value.v.list[4] = make_stack_list(activ_stack, handler_activ,
                                      top_activ_stack, 1,
                                      root_activ_vector, 1, include_vars,
                                      NOTHING);

    if (why == FIN_UNCAUGHT) {
        save_handler_info("handle_uncaught_error", value);
        value = zero;
    }
    return unwind_stack(why, value, outcome);
}

static void
abort_task(enum abort_reason reason)
{
    Var value;
    const char *msg;
    const char *htag;

    switch (reason) {
        default:
            panic_moo("Bad abort_reason");
        /*NOTREACHED*/

        case ABORT_TICKS:
            msg  = "Task ran out of ticks";
            htag = "ticks";
            goto save_hinfo;

        case ABORT_SECONDS:
            msg = "Task ran out of seconds";
            htag = "seconds";

save_hinfo:
            value = new_list(3);
            value.v.list[1].type = TYPE_STR;
            value.v.list[1].v.str = str_dup(htag);
            value.v.list[2] = make_stack_list(activ_stack, 0, top_activ_stack, 1,
                                              root_activ_vector, 1, server_int_option("INCLUDE_RT_VARS", 0),
                                              NOTHING);
            value.v.list[3] = error_backtrace_list(msg);
            save_handler_info("handle_task_timeout", value);
        /* fall through */

        case ABORT_KILL:
            (void) unwind_stack(FIN_ABORT, zero, nullptr);
    }
}

/**** activation manipulation ****/

static int
push_activation(void)
{
    if (top_activ_stack < max_stack_size - 1) {
        top_activ_stack++;
        return 1;
    } else
        return 0;
}

void
free_activation(activation * ap, char data_too)
{
    Var *i;

    free_rt_env(ap->rt_env, ap->prog->num_var_names);

    for (i = ap->base_rt_stack; i < ap->top_rt_stack; i++)
        free_var(*i);
    free_rt_stack(ap);
    free_var(ap->temp);
    free_var(ap->_this);
    free_var(ap->vloc);
    free_str(ap->verb);
    free_str(ap->verbname);

    free_program(ap->prog);

    if (data_too && ap->bi_func_pc && ap->bi_func_data)
        free_data(ap->bi_func_data);
    /* else bi_func_state will be later freed by bi_function */
}


/** Set up another activation for calling a verb
  does not change the vm in case of any error **/

enum error call_verb2(Objid recv, const char *vname, Var _this, Var args, int do_pass);

/*
 * Historical interface for things which want to call with vname not
 * already in a moo-str.
 */
enum error
call_verb(Objid recv, const char *vname_in, Var _this, Var args, int do_pass)
{
    const char *vname = str_dup(vname_in);
    enum error result;

    result = call_verb2(recv, vname, _this, args, do_pass, DEFAULT_THREAD_MODE);
    /* call_verb2 got any refs it wanted */
    free_str(vname);
    return result;
}

enum error
call_verb2(Objid recv, const char *vname, Var _this, Var args, int do_pass, bool should_thread)
{
    /* if call succeeds, args will be consumed.  If call fails, args
       will NOT be consumed  -- it must therefore be freed by caller */
    /* vname will never be consumed */
    /* vname *must* already be a MOO-string (as in str_ref-able) */
    /* `_this' will never be consumed */

    /* will only return E_MAXREC, E_INVIND, E_VERBNF, or E_NONE */
    /* returns an error if there is one, and does not change the vm in that
       case, else sets up the activ_stack for the verb call and then returns
       E_NONE */

    db_verb_handle h = { };
    Program *program;
    Var *env;
    Var v;

    if (do_pass) {
        Objid where;

        if (!is_valid(RUN_ACTIV.vloc))
            return E_INVIND;

        Var parents = db_object_parents2(RUN_ACTIV.vloc);

        if (TYPE_LIST == parents.type) {
            if (listlength(parents) == 0)
                return E_INVIND;
            /* Loop over each parent, looking for the first parent
             * that defines a suitable verb that we can pass to.
             */
            Var parent;
            int i, c;
            FOR_EACH(parent, parents, i, c) {
                where = parent.v.obj;
                h = db_find_callable_verb(Var::new_obj(where), vname);
                if (h.ptr)
                    break;
            }
        }
        else if (TYPE_OBJ == parents.type) {
            /* Look for a suitable verb on the parent, if the parent
             * is valid.
             */
            where = parents.v.obj;
            if (!valid(where))
                return E_INVIND;
            h = db_find_callable_verb(Var::new_obj(where), vname);
        }
        else {
            return E_VERBNF;
        }
    }
    else {
        if (TYPE_ANON == _this.type && is_valid(_this))
            h = db_find_callable_verb(_this, vname);
        else if (valid(recv))
            h = db_find_callable_verb(Var::new_obj(recv), vname);
        else
            return E_INVIND;
    }

    if (!h.ptr)
        return E_VERBNF;
    else if (!push_activation())
        return E_MAXREC;

    program = db_verb_program(h);
    RUN_ACTIV.prog = program_ref(program);
    RUN_ACTIV._this = var_ref(_this);
    RUN_ACTIV.progr = db_verb_owner(h);
    RUN_ACTIV.recv = recv;
    RUN_ACTIV.vloc = var_ref(db_verb_definer(h));
    RUN_ACTIV.verb = str_ref(vname);
    RUN_ACTIV.verbname = str_ref(db_verb_names(h));
    RUN_ACTIV.debug = (db_verb_flags(h) & VF_DEBUG);
    RUN_ACTIV.threaded = should_thread;

    alloc_rt_stack(&RUN_ACTIV, program->main_vector.max_stack);
    RUN_ACTIV.pc = 0;
    RUN_ACTIV.error_pc = 0;
    RUN_ACTIV.bi_func_pc = 0;
    RUN_ACTIV.temp.type = TYPE_NONE;

    RUN_ACTIV.rt_env = env = new_rt_env(RUN_ACTIV.prog->num_var_names);

    fill_in_rt_consts(env, program->version);

    set_rt_env_var(env, SLOT_THIS, var_ref(RUN_ACTIV._this));
    set_rt_env_var(env, SLOT_CALLER, var_ref(CALLER_ACTIV._this));

#define ENV_COPY(slot) \
    set_rt_env_var(env, slot, var_ref(CALLER_ACTIV.rt_env[slot]))

    ENV_COPY(SLOT_ARGSTR);
    ENV_COPY(SLOT_DOBJ);
    ENV_COPY(SLOT_DOBJSTR);
    ENV_COPY(SLOT_PREPSTR);
    ENV_COPY(SLOT_IOBJ);
    ENV_COPY(SLOT_IOBJSTR);

    if (is_wizard(CALLER_ACTIV.progr) &&
            (CALLER_ACTIV.rt_env[SLOT_PLAYER].type == TYPE_OBJ))
        ENV_COPY(SLOT_PLAYER);
    else
        set_rt_env_obj(env, SLOT_PLAYER, CALLER_ACTIV.player);
    RUN_ACTIV.player = env[SLOT_PLAYER].v.obj;

#undef ENV_COPY

    v.type = TYPE_STR;
    if (vname[0] == WAIF_VERB_PREFIX)
        v.v.str = str_dup(vname + 1);
    else
        v.v.str = str_ref(vname);
    set_rt_env_var(env, SLOT_VERB, v);  /* no var_dup */
    set_rt_env_var(env, SLOT_ARGS, args);   /* no var_dup */

    return E_NONE;
}

#ifdef IGNORE_PROP_PROTECTED
#define bi_prop_protected(prop, progr) (0)
#else
#define bi_prop_protected(prop, progr) ((!is_wizard(progr)) && server_flag_option_cached(prop))
#endif              /* IGNORE_PROP_PROTECTED */

/**
  the main interpreter -- run()
  everything is just an entry point to it
**/

static enum outcome
run(char raise, enum error resumption_error, Var * result)
{   /* runs the_vm */
    /* If the returned value is OUTCOME_DONE and RESULT is non-NULL, then
     * *RESULT is the value returned by the top frame.
     */

    /* bc, bv, rts are distinguished as the state variables of run()
       their value capture the state of the running between OP_ cases */
    Bytecodes bc;
    Byte * bv, *error_bv;
    Var *rts;           /* next empty slot */
    enum Opcode op;
    Var error_var;
    enum outcome outcome;

    /** a bunch of macros that work *ONLY* inside run() **/

    /* helping macros about the runtime_stack. */
#define POP()         (*(--rts))
#define PUSH(v)       (*(rts++) = v)
#define PUSH_REF(v)   PUSH(var_ref(v))
#define TOP_RT_VALUE           (*(rts - 1))
#define NEXT_TOP_RT_VALUE      (*(rts - 2))

#define READ_BYTES(bv, nb)                      \
    ( bv += nb,                                 \
      (nb == 1                                  \
       ? bv[-1]                                 \
       : (nb == 2                               \
          ? ((unsigned) bv[-2] << 8) + bv[-1]   \
          : (((unsigned) bv[-4] << 24)          \
             + ((unsigned) bv[-3] << 16)        \
             + ((unsigned) bv[-2] << 8)         \
             + bv[-1]))))

#define SKIP_BYTES(bv, nb)  ((void)(bv += nb))

#define LOAD_STATE_VARIABLES()                                            \
    do {                                                                  \
        bc = ( (top_activ_stack != 0 || root_activ_vector == MAIN_VECTOR) \
               ? RUN_ACTIV.prog->main_vector                              \
               : RUN_ACTIV.prog->fork_vectors[root_activ_vector]);        \
        bv = bc.vector + RUN_ACTIV.pc;                                    \
        error_bv = bc.vector + RUN_ACTIV.error_pc;                        \
        rts = RUN_ACTIV.top_rt_stack; /* next empty slot */               \
    } while (0)

#define STORE_STATE_VARIABLES()                     \
    do {                                            \
        RUN_ACTIV.pc = bv - bc.vector;              \
        RUN_ACTIV.error_pc = error_bv - bc.vector;  \
        RUN_ACTIV.top_rt_stack = rts;               \
    } while (0)

#define RAISE_ERROR(the_err)                                \
    do {                                                    \
        if (RUN_ACTIV.debug) {                              \
            STORE_STATE_VARIABLES();                        \
            if (raise_error(make_error_pack(the_err), 0))   \
                return OUTCOME_ABORTED;                     \
            else {                                          \
                LOAD_STATE_VARIABLES();                     \
                goto next_opcode;                           \
            }                                               \
        }                                                   \
    } while (0)

#define RAISE_ERROR_WITH_VALUE(the_err, the_msg, the_value)                     \
    do {                                                                        \
        if (RUN_ACTIV.debug) {                                                  \
            STORE_STATE_VARIABLES();                                            \
            if (raise_error(make_raise_pack(the_err, the_msg, the_value), 0)) { \
                /* No try-except */                                             \
                free_str(the_msg);                                              \
                return OUTCOME_ABORTED;                                         \
            } else {                                                            \
                LOAD_STATE_VARIABLES();                                         \
                free_str(the_msg);                                              \
                goto next_opcode;                                               \
            } \
        } \
    } while (0)

#define PUSH_ERROR(the_err)                                 \
    do {                                                    \
        RAISE_ERROR(the_err);   /* may not return!! */      \
        error_var.type = TYPE_ERR;                          \
        error_var.v.err = the_err;                          \
        PUSH(error_var);                                    \
    } while (0)

    /* NOTE: the_msg will be freed */
#define PUSH_ERROR_WITH_VALUE(the_err, the_msg, the_value)                        \
    do {                                                                          \
        RAISE_ERROR_WITH_VALUE(the_err, the_msg, the_value); /* may not return */ \
        free_str(the_msg);                                                        \
        free_var(the_value);                                                      \
        error_var.type = TYPE_ERR;                                                \
        error_var.v.err = the_err;                                                \
        PUSH(error_var);                                                          \
    } while (0)

#define PUSH_ERROR_UNLESS_QUOTA(the_err)                                \
    do {                                                                \
        if (E_QUOTA == (the_err) &&                                     \
                !server_flag_option_cached(SVO_MAX_CONCAT_CATCHABLE))   \
        {                                                               \
            /* simulate out-of-seconds abort resulting */               \
            /* from monster malloc+copy taking too long */              \
            STORE_STATE_VARIABLES();                                    \
            abort_task(ABORT_SECONDS);                                  \
            return OUTCOME_ABORTED;                                     \
        }                                                               \
        else                                                            \
            PUSH_ERROR(the_err);                                        \
    } while (0)

    /* error the_err: an error type (e.g. E_PROPNF)
       Var the_missing: TYPE_STR representing what is missing (e.g. the property name) [gets freed]
       Var the_object: TYPE_OBJ representing the object that is missing the thing [gets freed]
                       the_object can be 'nothing' to indicate that object is not applicable */
#define PUSH_X_NOT_FOUND(the_err, the_missing, the_object)                                       \
    do {                                                                                         \
        static Stream *error_stream = nullptr;                                                   \
        if (error_stream == nullptr) {                                                           \
            error_stream = new_stream(20);                                                       \
        }                                                                                        \
        if (the_object.v.obj == NOTHING) {                                                       \
            stream_printf(error_stream, "%s: %s", unparse_error(the_err), the_missing.v.str);    \
        } else {                                                                                 \
            char separator;                                                                      \
            if (the_err == E_PROPNF)                                                             \
                separator = '.';                                                                 \
            else if (the_err == E_VERBNF)                                                        \
                separator = ':';                                                                 \
            stream_printf(error_stream, "%s: #%" PRIdN "%c%s%s", unparse_error(the_err),         \
                          the_object.v.obj, separator, the_missing.v.str,                        \
                          the_err == E_VERBNF ? "()" : "");                                      \
        }                                                                                        \
        char *error_message = str_dup(reset_stream(error_stream));                               \
        Var value = new_list(2);                                                                 \
        value.v.list[1] = the_object;                                                            \
        value.v.list[2] = the_missing;                                                           \
        PUSH_ERROR_WITH_VALUE(the_err, error_message, value);                                    \
    } while (0)

#define RAISE_TYPE_MISMATCH(...)                               \
    do {                                                       \
        char *error_msg = type_mismatch_string(__VA_ARGS__);   \
        Var value = type_mismatch_value(__VA_ARGS__);          \
        RAISE_ERROR_WITH_VALUE(E_TYPE, error_msg, value);      \
        free_str(error_msg);                                   \
        free_var(value);                                       \
    } while (0)

#define PUSH_TYPE_MISMATCH(...)                                \
    do {                                                       \
        char *error_msg = type_mismatch_string(__VA_ARGS__);   \
        Var value = type_mismatch_value(__VA_ARGS__);          \
        PUSH_ERROR_WITH_VALUE(E_TYPE, error_msg, value);       \
    } while (0)

#define JUMP(label)     (bv = bc.vector + label)

    /* end of major run() macros */

    LOAD_STATE_VARIABLES();

    if (raise) {
        error_bv = bv;
        PUSH_ERROR(resumption_error);
    }
    for (;;) {
next_opcode:
        error_bv = bv;
        op = (Opcode)(*bv++);

        if (COUNT_TICK(op)) {
            if (--ticks_remaining <= 0) {
                STORE_STATE_VARIABLES();
                abort_task(ABORT_TICKS);
                return OUTCOME_ABORTED;
            }
            if (task_timed_out) {
                STORE_STATE_VARIABLES();
                abort_task(ABORT_SECONDS);
                return OUTCOME_ABORTED;
            }
        }
        switch (op) {

            case OP_IF_QUES:
            case OP_IF:
            case OP_WHILE:
            case OP_EIF:
do_test:
                {
                    Var cond;

                    cond = POP();
                    if (!is_true(cond)) {   /* jump if false */
                        unsigned lab = READ_BYTES(bv, bc.numbytes_label);
                        JUMP(lab);
                    }
                    else {
                        SKIP_BYTES(bv, bc.numbytes_label);
                    }
                    free_var(cond);
                }
                break;

            case OP_JUMP:
            {
                unsigned lab = READ_BYTES(bv, bc.numbytes_label);
                JUMP(lab);
            }
            break;

            case OP_FOR_RANGE:
            {
                unsigned id = READ_BYTES(bv, bc.numbytes_var_name);
                unsigned lab = READ_BYTES(bv, bc.numbytes_label);
                Var from, to;

                to = TOP_RT_VALUE;
                from = NEXT_TOP_RT_VALUE;

                if ((to.type != TYPE_INT && to.type != TYPE_OBJ)
                        || to.type != from.type) {
                    RAISE_TYPE_MISMATCH(2,
                                        to.type != TYPE_INT && to.type != TYPE_OBJ ? to.type : from.type,
                                        TYPE_INT, TYPE_OBJ);
                    free_var(POP());
                    free_var(POP());
                    JUMP(lab);
                } else if (to.type == TYPE_INT
                           ? from.v.num > to.v.num
                           : from.v.obj > to.v.obj) {
                    free_var(POP());
                    free_var(POP());
                    JUMP(lab);
                } else {
                    free_var(RUN_ACTIV.rt_env[id]);
                    RUN_ACTIV.rt_env[id] = var_ref(from);
                    if (to.type == TYPE_INT) {
                        if (from.v.num < MAXINT) {
                            from.v.num++;
                            NEXT_TOP_RT_VALUE = from;
                        } else {
                            to.v.num--;
                            TOP_RT_VALUE = to;
                        }
                    } else {
                        if (from.v.obj < MAXOBJ) {
                            from.v.obj++;
                            NEXT_TOP_RT_VALUE = from;
                        } else {
                            to.v.obj--;
                            TOP_RT_VALUE = to;
                        }
                    }
                }
            }
            break;

            case OP_POP:
                free_var(POP());
                break;

            case OP_IMM:
            {
                int slot;

                /* If we'd just throw it away anyway (eg verbdocs),
                   skip both OPs.  This accounts for most executions
                   of OP_IMM in my tests.
                 */
                if (bv[bc.numbytes_literal] == OP_POP) {
                    bv += bc.numbytes_literal + 1;
                    break;
                }
                slot = READ_BYTES(bv, bc.numbytes_literal);
                PUSH_REF(RUN_ACTIV.prog->literals[slot]);
            }
            break;

            case OP_MAP_CREATE:
            {
                Var map;

                map = new_map();
                PUSH(map);
            }
            break;

            case OP_MAP_INSERT:
            {
                Var r, map, key, value;
                key = POP(); /* any except list or map */
                value = POP(); /* any */
                map = POP(); /* should be map */
                if (map.type != TYPE_MAP || (key.is_collection() && TYPE_ANON != key.type)) {
                    free_var(key);
                    free_var(value);
                    free_var(map);
                    PUSH_TYPE_MISMATCH(8, key.type, TYPE_STR, TYPE_INT, TYPE_OBJ, TYPE_ERR, TYPE_FLOAT, TYPE_ANON, TYPE_WAIF, TYPE_BOOL);
                } else {
                    r = mapinsert(map, key, value);
                    if (value_bytes(r) <= server_int_option_cached(SVO_MAX_MAP_VALUE_BYTES))
                        PUSH(r);
                    else {
                        free_var(r);
                        PUSH_ERROR_UNLESS_QUOTA(E_QUOTA);
                    }
                }
            }
            break;

            case OP_MAKE_EMPTY_LIST:
            {
                Var list;

                list = new_list(0);
                PUSH(list);
            }
            break;

            case OP_LIST_ADD_TAIL:
            {
                Var r, tail, list;

                tail = POP();   /* whatever */
                list = POP();   /* should be list */
                if (list.type != TYPE_LIST) {
                    free_var(list);
                    free_var(tail);
                    PUSH_TYPE_MISMATCH(1, list.type, TYPE_LIST);
                } else {
                    r = listappend(list, tail);
                    if (value_bytes(r) <= server_int_option_cached(SVO_MAX_LIST_VALUE_BYTES))
                        PUSH(r);
                    else {
                        free_var(r);
                        PUSH_ERROR_UNLESS_QUOTA(E_QUOTA);
                    }
                }
            }
            break;

            case OP_LIST_APPEND:
            {
                Var r, tail, list;

                tail = POP();   /* second, should be list */
                list = POP();   /* first, should be list */
                if (tail.type != TYPE_LIST || list.type != TYPE_LIST) {
                    free_var(list);
                    free_var(tail);
                    PUSH_TYPE_MISMATCH(1, tail.type != TYPE_LIST ? tail.type : list.type, TYPE_LIST);
                } else {
                    r = listconcat(list, tail);
                    if (value_bytes(r) <= server_int_option_cached(SVO_MAX_LIST_VALUE_BYTES))
                        PUSH(r);
                    else {
                        free_var(r);
                        PUSH_ERROR_UNLESS_QUOTA(E_QUOTA);
                    }
                }
            }
            break;

            /* This opcode will not increase the length of a string
             * but it may increase the size of a list or map, thus the
             * check.
             */
            case OP_INDEXSET:
            {
                Var value, index, list;

                value = POP();  /* rhs value */
                index = POP();  /* index, should be integer for list or str */
                list = POP();   /* lhs except last index, should be list or str */
                /* whole thing should mean list[index] = value OR
                 * map[key] = value */
#ifdef WAIF_DICT
                if (list.type == TYPE_WAIF) {
                    Objid _class;
                    Var args;
                    enum error err = E_NONE;

                    args = new_list(2);
                    args.v.list[1] = var_ref(index);
                    args.v.list[2] = var_ref(value);

                    _class = list.v.waif->_class;
                    if (!valid(_class)) {
                        err = E_INVIND;
                    } else if (!is_wizard(db_object_owner(_class))) {
                        err = E_TYPE;
                    } else {
                        STORE_STATE_VARIABLES();
                        err = call_verb2(_class, waif_indexset_verb, list, args, 0, DEFAULT_THREAD_MODE);
                        if (err == E_VERBNF) {
                            err = E_TYPE;
                        }
                        LOAD_STATE_VARIABLES();
                    }
                    free_var(index);
                    free_var(value);
                    free_var(list);
                    if (err != E_NONE) {
                        free_var(args);
                        PUSH_ERROR(err);
                    }
                } else
#endif              /* WAIF_DICT */
                    if ((list.type != TYPE_LIST && list.type != TYPE_STR &&
                            list.type != TYPE_MAP)
                            || ((list.type == TYPE_LIST || list.type == TYPE_STR) &&
                                index.type != TYPE_INT)
                            || (list.type == TYPE_MAP && (index.is_collection() && TYPE_ANON != index.type))
                            || (list.type == TYPE_STR && value.type != TYPE_STR)) {
                        free_var(value);
                        free_var(index);
                        free_var(list);
                        PUSH_ERROR(E_TYPE);
                    } else if ((list.type == TYPE_LIST
                                && (index.v.num < 1 || index.v.num > list.v.list[0].v.num /* size */))
                               || (list.type == TYPE_STR
                                   && (index.v.num < 1 || index.v.num > (int) memo_strlen(list.v.str)))) {
                        free_var(value);
                        free_var(index);
                        free_var(list);
                        PUSH_ERROR(E_RANGE);
                    } else if (list.type == TYPE_STR
                               && memo_strlen(value.v.str) != 1) {
                        free_var(value);
                        free_var(index);
                        free_var(list);
                        PUSH_ERROR(E_INVARG);
                    } else if (list.type == TYPE_LIST) {
                        Var res = listset(list, value, index.v.num);
                        if (value_bytes(res) <= server_int_option_cached(SVO_MAX_LIST_VALUE_BYTES))
                            PUSH(res);
                        else {
                            free_var(res);
                            PUSH_ERROR_UNLESS_QUOTA(E_QUOTA);
                        }
                    } else if (list.type == TYPE_MAP) {
                        Var res = mapinsert(list, index, value);
                        if (value_bytes(res) <= server_int_option_cached(SVO_MAX_MAP_VALUE_BYTES))
                            PUSH(res);
                        else {
                            free_var(res);
                            PUSH_ERROR_UNLESS_QUOTA(E_QUOTA);
                        }
                    } else {    /* TYPE_STR */
                        char *tmp_str = str_dup(list.v.str);
                        free_str(list.v.str);
                        tmp_str[index.v.num - 1] = value.v.str[0];
                        list.v.str = tmp_str;
                        free_var(value);
                        PUSH(list);
                    }
            }
            break;

            case OP_MAKE_SINGLETON_LIST:
            {
                Var list;

                list = new_list(1);
                list.v.list[1] = POP();
                PUSH(list);
            }
            break;

            case OP_CHECK_LIST_FOR_SPLICE:
                if (TOP_RT_VALUE.type != TYPE_LIST) {
                    var_type rt_value_type = TOP_RT_VALUE.type;
                    free_var(POP());
                    PUSH_TYPE_MISMATCH(1, rt_value_type, TYPE_LIST);
                }
                /* no op if top-rt-stack is a list */
                break;

            case OP_PUT_TEMP:
                RUN_ACTIV.temp = var_ref(TOP_RT_VALUE);
                break;

            case OP_PUSH_TEMP:
                PUSH(RUN_ACTIV.temp);
                RUN_ACTIV.temp.type = TYPE_NONE;
                break;

            case OP_EQ:
            case OP_NE:
            {
                Var rhs, lhs, ans;

                rhs = POP();
                lhs = POP();
                ans.type = TYPE_INT;
                ans.v.num = (op == OP_EQ
                             ? equality(rhs, lhs, 0)
                             : !equality(rhs, lhs, 0));
                PUSH(ans);
                free_var(rhs);
                free_var(lhs);
            }
            break;

            case OP_GT:
            case OP_LT:
            case OP_GE:
            case OP_LE:
            {
                Var rhs, lhs, ans;
                int comparison;

                rhs = POP();
                lhs = POP();
                if ((lhs.type == TYPE_INT || lhs.type == TYPE_FLOAT)
                        && (rhs.type == TYPE_INT || rhs.type == TYPE_FLOAT)) {
                    ans = compare_numbers(lhs, rhs);
                    if (ans.type == TYPE_ERR) {
                        free_var(rhs);
                        free_var(lhs);
                        PUSH_ERROR(ans.v.err);
                    } else {
                        comparison = ans.v.num;
                        goto finish_comparison;
                    }
                } else if (rhs.type != lhs.type || rhs.type == TYPE_LIST || rhs.type == TYPE_MAP) {
                    free_var(rhs);
                    free_var(lhs);
                    PUSH_TYPE_MISMATCH(1, rhs.type, lhs.type);
                } else {
                    switch (rhs.type) {
                        case TYPE_INT:
                            comparison = compare_integers(lhs.v.num, rhs.v.num);
                            break;
                        case TYPE_OBJ:
                            comparison = compare_integers(lhs.v.obj, rhs.v.obj);
                            break;
                        case TYPE_ERR:
                            comparison = ((int) lhs.v.err) - ((int) rhs.v.err);
                            break;
                        case TYPE_STR:
                            comparison = strcasecmp(lhs.v.str, rhs.v.str);
                            break;
                        default:
                            errlog("RUN: Impossible type in comparison: %d\n",
                                   rhs.type);
                            comparison = 0;
                    }

finish_comparison:
                    ans.type = TYPE_INT;
                    switch (op) {
                        case OP_LT:
                            ans.v.num = (comparison < 0);
                            break;
                        case OP_LE:
                            ans.v.num = (comparison <= 0);
                            break;
                        case OP_GT:
                            ans.v.num = (comparison > 0);
                            break;
                        case OP_GE:
                            ans.v.num = (comparison >= 0);
                            break;
                        default:
                            errlog("RUN: Imposible opcode in comparison: %d\n", op);
                            break;
                    }
                    PUSH(ans);
                    free_var(rhs);
                    free_var(lhs);
                }
            }
            break;

            case OP_IN:
            {
                Var lhs, rhs, ans;

                rhs = POP();    /* should be list or map */
                lhs = POP();    /* lhs, any type */
                if (lhs.type == TYPE_STR && rhs.type == TYPE_STR) {
                    ans.type = TYPE_INT;
                    ans.v.num = strindex(rhs.v.str, memo_strlen(rhs.v.str), lhs.v.str, memo_strlen(lhs.v.str), 0);
                    PUSH(ans);
                    free_var(lhs);
                    free_var(rhs);
                } else if (rhs.type != TYPE_LIST && rhs.type != TYPE_MAP) {
                    var_type rhs_type = rhs.type;
                    var_type lhs_type = lhs.type;
                    free_var(rhs);
                    free_var(lhs);
                    PUSH_TYPE_MISMATCH(2, rhs.type, TYPE_LIST, TYPE_MAP);
                } else {
                    ans.type = TYPE_INT;
                    ans.v.num = ismember(lhs, rhs, 0);
                    PUSH(ans);
                    free_var(rhs);
                    free_var(lhs);
                }
            }
            break;

            case OP_MULT:
            case OP_MINUS:
            case OP_DIV:
            case OP_MOD:
            {
                Var lhs, rhs, ans;
                var_type lhs_type, rhs_type;

                rhs = POP();    /* should be number */
                lhs = POP();    /* should be number */
                if ((lhs.type == TYPE_INT || lhs.type == TYPE_FLOAT)
                        && (rhs.type == TYPE_INT || rhs.type == TYPE_FLOAT)) {
                    switch (op) {
                        case OP_MULT:
                            ans = do_multiply(lhs, rhs);
                            break;
                        case OP_MINUS:
                            ans = do_subtract(lhs, rhs);
                            break;
                        case OP_DIV:
                            ans = do_divide(lhs, rhs);
                            break;
                        case OP_MOD:
                            ans = do_modulus(lhs, rhs);
                            break;
                        default:
                            errlog("RUN: Impossible opcode in arith ops: %d\n", op);
                            break;
                    }
                } else {
                    ans.type = TYPE_ERR;
                    ans.v.err = E_TYPE;
                    lhs_type = lhs.type;
                    rhs_type = rhs.type;
                }
                free_var(rhs);
                free_var(lhs);
                if (ans.type == TYPE_ERR) {
                    if (ans.v.err == E_TYPE)
                        PUSH_TYPE_MISMATCH(2,
                                           lhs_type != TYPE_INT && lhs_type != TYPE_FLOAT ? lhs_type : rhs_type,
                                           TYPE_INT, TYPE_FLOAT);
                    else
                        PUSH_ERROR(ans.v.err);
                } else {
                    PUSH(ans);
                }
            }
            break;

            case OP_ADD:
            {
                Var rhs, lhs, ans;

                rhs = POP();
                lhs = POP();

                var_type lhs_type = lhs.type;
                var_type rhs_type = rhs.type;

                if ((lhs_type == TYPE_INT || lhs_type == TYPE_FLOAT)
                        && (rhs_type == TYPE_INT || rhs_type == TYPE_FLOAT))
                    ans = do_add(lhs, rhs);
                else if (lhs_type == TYPE_STR && rhs_type == TYPE_STR) {
                    char *str;
                    int llen = memo_strlen(lhs.v.str);
                    int flen = llen + memo_strlen(rhs.v.str);

                    if (server_int_option_cached(SVO_MAX_STRING_CONCAT) < flen) {
                        ans.type = TYPE_ERR;
                        ans.v.err = E_QUOTA;
                    } else {
                        str = (char *)mymalloc(flen + 1, M_STRING);
                        strcpy(str, lhs.v.str);
                        strcpy(str + llen, rhs.v.str);
                        ans.type = TYPE_STR;
                        ans.v.str = str;
                    }
                } else if (lhs_type == TYPE_LIST) {
                    if (rhs_type == TYPE_LIST) {
                        ans = listconcat(var_ref(lhs), var_ref(rhs));
                    } else {
                        ans = listappend(var_ref(lhs), var_ref(rhs));
                    }
                } else {
                    ans.type = TYPE_ERR;
                    ans.v.err = E_TYPE;
                }
                free_var(rhs);
                free_var(lhs);

                if (ans.type == TYPE_ERR) {
                    /* This is maybe slightly unwieldy, but it works. It assumes lhs is the type you really wanted. */
                    if (ans.v.err == E_TYPE) {
                        if (lhs_type == TYPE_STR)
                            PUSH_TYPE_MISMATCH(1, lhs_type != TYPE_STR ? lhs_type : rhs_type, TYPE_STR);
                        else if (rhs_type == TYPE_STR && lhs_type != TYPE_INT && lhs_type != TYPE_FLOAT)
                            PUSH_TYPE_MISMATCH(1, lhs_type, TYPE_STR);
                        else if (lhs_type == TYPE_INT)
                            PUSH_TYPE_MISMATCH(1, rhs_type, TYPE_INT);
                        else if (lhs_type == TYPE_FLOAT)
                            PUSH_TYPE_MISMATCH(1, rhs_type, TYPE_FLOAT);
                        else if (lhs_type != TYPE_INT && lhs_type != TYPE_FLOAT && (rhs_type == TYPE_INT || rhs_type == TYPE_FLOAT))
                            PUSH_TYPE_MISMATCH(1, lhs_type, rhs.type);
                        else
                            PUSH_TYPE_MISMATCH(3, lhs.type, TYPE_INT, TYPE_FLOAT, TYPE_STR);
                    } else {
                        PUSH_ERROR_UNLESS_QUOTA(ans.v.err);
                    }
                } else {
                    PUSH(ans);
                }
            }
            break;

            case OP_AND:
            case OP_OR:
            {
                Var lhs;
                unsigned lab = READ_BYTES(bv, bc.numbytes_label);

                lhs = TOP_RT_VALUE;
                if ((op == OP_AND && !is_true(lhs))
                        || (op == OP_OR && is_true(lhs)))   /* short-circuit */
                    JUMP(lab);
                else {
                    free_var(POP());
                }
            }
            break;

            case OP_NOT:
            {
                Var arg, ans;

                arg = POP();
                ans.type = TYPE_INT;
                ans.v.num = !is_true(arg);
                PUSH(ans);
                free_var(arg);
            }
            break;

            case OP_UNARY_MINUS:
            {
                Var arg, ans;
                var_type arg_type;

                arg = POP();
                if (arg.type == TYPE_INT) {
                    ans.type = TYPE_INT;
                    ans.v.num = -arg.v.num;
                } else if (arg.type == TYPE_FLOAT) {
                    ans.type = TYPE_FLOAT;
                    ans.v.fnum = -arg.v.fnum;
                } else {
                    arg_type = arg.type;
                    free_var(arg);
                    PUSH_TYPE_MISMATCH(2, arg_type, TYPE_INT, TYPE_FLOAT);
                    break;
                }

                PUSH(ans);
                free_var(arg);
            }
            break;

            case OP_REF:
            {
                Var index, list;

                index = POP();
                list = POP();   /* should be list, string, or map */

#ifdef WAIF_DICT
                if (list.type == TYPE_WAIF) {
                    Objid _class;
                    Var args;
                    enum error err = E_NONE;

                    args = new_list(1);
                    args.v.list[1] = var_ref(index);

                    _class = list.v.waif->_class;
                    if (!valid(_class)) {
                        err = E_INVIND;
                    } else if (!is_wizard(db_object_owner(_class))) {
                        err = E_TYPE;
                    } else {
                        STORE_STATE_VARIABLES();
                        err = call_verb2(_class, waif_index_verb, list, args, 0, DEFAULT_THREAD_MODE);
                        if (err == E_VERBNF) {
                            err = E_TYPE;
                        }
                        LOAD_STATE_VARIABLES();
                    }
                    free_var(index);
                    free_var(list);
                    if (err != E_NONE) {
                        free_var(args);
                        PUSH_ERROR(err);
                    }
                } else
#endif              /* WAIF_DICT */
                    if ((list.type != TYPE_LIST && list.type != TYPE_STR &&
                            list.type != TYPE_MAP) ||
                            ((list.type == TYPE_LIST || list.type == TYPE_STR) &&
                             index.type != TYPE_INT) ||
                            (list.type == TYPE_MAP && (index.is_collection() && TYPE_ANON != index.type))) {
                        free_var(index);
                        free_var(list);
                        PUSH_ERROR(E_TYPE);
                    } else if (list.type == TYPE_MAP) {
                        Var value;
                        if (maplookup(list, index, &value, 0) == nullptr) {
                            free_var(index);
                            free_var(list);
                            PUSH_ERROR(E_RANGE);
                        } else {
                            PUSH_REF(value);
                            free_var(index);
                            free_var(list);
                        }
                    } else if (list.type == TYPE_LIST) {
                        if (index.v.num <= 0 || index.v.num > list.v.list[0].v.num) {
                            free_var(index);
                            free_var(list);
                            PUSH_ERROR(E_RANGE);
                        } else {
                            PUSH(var_ref(list.v.list[index.v.num]));
                            free_var(index);
                            free_var(list);
                        }
                    } else {    /* list.type == TYPE_STR */
                        if (index.v.num <= 0
                                || index.v.num > (int) memo_strlen(list.v.str)) {
                            free_var(index);
                            free_var(list);
                            PUSH_ERROR(E_RANGE);
                        } else {
                            PUSH(strget(list, index.v.num));
                            free_var(index);
                            free_var(list);
                        }
                    }
            }
            break;

            case OP_PUSH_REF:
            {
                /* This is about the sketchiest manoeuvre I can
                 * imagine.  The goal is to mutate a nested list/map
                 * in place when nothing else is hanging on to a
                 * reference to the list/map.  The original code
                 * defensively called `var_ref' here and `OP_INDEXSET'
                 * obligingly let the value be duplicated.  We try to
                 * be a bit smarter about it by duplicating the
                 * list/map itself when there's more than one
                 * reference to it, thereby ensuring a captive copy.
                 * We then mutate it in place -- we can safely clear
                 * values below because we know that `OP_INDEXSET'
                 * will fill them back in for us.
                 */
                if (var_refcount(NEXT_TOP_RT_VALUE) > 1) {
                    Var temp = var_dup(NEXT_TOP_RT_VALUE);
                    free_var(NEXT_TOP_RT_VALUE);
                    NEXT_TOP_RT_VALUE = temp;
                }

                /* Two cases are possible: list[index] or map[key] */
                Var list, index;

                index = TOP_RT_VALUE;
                list = NEXT_TOP_RT_VALUE;
                if (list.type == TYPE_MAP) {
                    Var value;
                    const rbnode *node;
                    if (index.is_collection() && TYPE_ANON != index.type) {
                        PUSH_TYPE_MISMATCH(8, index.type, TYPE_STR, TYPE_INT, TYPE_OBJ, TYPE_ERR, TYPE_FLOAT, TYPE_ANON, TYPE_WAIF, TYPE_BOOL);
                    } else if (!(node = maplookup(list, index, &value, 0))) {
                        PUSH_ERROR(E_RANGE);
                    } else {
                        PUSH(value);
                        clear_node_value(node);
                    }
                } else if (list.type == TYPE_LIST) {
                    if (index.type != TYPE_INT) {
                        PUSH_TYPE_MISMATCH(1, index.type, TYPE_INT);
                    } else if (index.v.num <= 0 ||
                               index.v.num > list.v.list[0].v.num) {
                        PUSH_ERROR(E_RANGE);
                    } else {
                        PUSH(list.v.list[index.v.num]);
                        list.v.list[index.v.num].type = TYPE_NONE;
                    }
                } else {
                    PUSH_TYPE_MISMATCH(2, list.type, TYPE_LIST, TYPE_MAP);
                }
            }
            break;

            case OP_RANGE_REF:
            {
                Var base, from, to;

                to = POP();
                from = POP();
                base = POP();   /* should be map, list or string */

                if (base.type != TYPE_MAP && base.type != TYPE_LIST
                        && base.type != TYPE_STR) {
                    var_type base_type = base.type;
                    free_var(to);
                    free_var(from);
                    free_var(base);
                    PUSH_TYPE_MISMATCH(3, base_type, TYPE_LIST, TYPE_MAP, TYPE_STR);
                } else if (base.type == TYPE_MAP
                           && ((to.is_collection() && TYPE_ANON != to.type) || (from.is_collection() && TYPE_ANON != from.type))) {
                    var_type to_from_type = to.is_collection() ? to.type : from.type;
                    free_var(to);
                    free_var(from);
                    free_var(base);
                    PUSH_TYPE_MISMATCH(8, to_from_type, TYPE_STR, TYPE_INT, TYPE_OBJ, TYPE_ERR, TYPE_FLOAT, TYPE_ANON, TYPE_WAIF, TYPE_BOOL);
                } else if ((base.type == TYPE_LIST || base.type == TYPE_STR)
                           && (to.type != TYPE_INT || from.type != TYPE_INT)) {
                    var_type to_from_type = to.type != TYPE_INT ? to.type : from.type;
                    free_var(to);
                    free_var(from);
                    free_var(base);
                    PUSH_TYPE_MISMATCH(1, to_from_type, TYPE_INT);
                } else if (base.type == TYPE_MAP) {
                    Var iterfrom, iterto;
                    int rel = compare(from, to, 0);
                    mapseek(base, from, &iterfrom, 0);
                    mapseek(base, to, &iterto, 0);
                    if ((rel <= 0) && (iterfrom.is_none() || iterto.is_none())) {
                        free_var(to);
                        free_var(from);
                        free_var(iterto);
                        free_var(iterfrom);
                        free_var(base);
                        PUSH_ERROR(E_RANGE);
                    } else if (rel > 0) {
                        PUSH(new_map());
                        free_var(to);
                        free_var(from);
                        free_var(iterto);
                        free_var(iterfrom);
                        free_var(base);
                    } else {
                        PUSH(maprange(base, iterfrom.v.trav, iterto.v.trav));
                        free_var(from);
                        free_var(to);
                        free_var(iterto);
                        free_var(iterfrom);
                    }
                } else {
                    int len = (base.type == TYPE_STR ? memo_strlen(base.v.str)
                               : base.v.list[0].v.num);
                    if (from.v.num <= to.v.num
                            && (from.v.num <= 0 || from.v.num > len
                                || to.v.num <= 0 || to.v.num > len)) {
                        free_var(to);
                        free_var(from);
                        free_var(base);
                        PUSH_ERROR(E_RANGE);
                    } else {
                        PUSH((base.type == TYPE_STR
                              ? substr(base, from.v.num, to.v.num)
                              : sublist(base, from.v.num, to.v.num)));
                        /* base freed by substr/sublist */
                        free_var(from);
                        free_var(to);
                    }
                }
            }
            break;

            case OP_G_PUT:
            {
                unsigned id = READ_BYTES(bv, bc.numbytes_var_name);
                free_var(RUN_ACTIV.rt_env[id]);
                RUN_ACTIV.rt_env[id] = var_ref(TOP_RT_VALUE);
            }
            break;

            case OP_G_PUSH:
            {
                Var value;

                int var_pos = READ_BYTES(bv, bc.numbytes_var_name);
                value = RUN_ACTIV.rt_env[var_pos];
                if (value.type == TYPE_NONE) {
                    Var not_found = str_ref_to_var(*(&RUN_ACTIV.prog->var_names[var_pos]));
                    PUSH_X_NOT_FOUND(E_VARNF, not_found, var_ref(nothing));
                }
                else
                    PUSH_REF(value);
            }
            break;

            case OP_GET_PROP:
            {
                Var propname, obj, prop;

                propname = POP();   /* should be string */
                obj = POP();        /* should be an object */
                if (obj.type == TYPE_WAIF && propname.type == TYPE_STR) {
                    enum error err;

                    err = waif_get_prop(obj.v.waif, propname.v.str, &prop, RUN_ACTIV.progr);
                    free_var(propname);
                    free_var(obj);
                    if (err == E_NONE)
                        PUSH(prop);
                    else
                        PUSH_ERROR(err);
                } else if (!obj.is_object() || propname.type != TYPE_STR) {
                    var_type incorrect_type = propname.type != TYPE_STR ? propname.type : obj.type;
                    free_var(propname);
                    free_var(obj);
                    PUSH_TYPE_MISMATCH(1, incorrect_type, incorrect_type == TYPE_STR ? TYPE_OBJ : TYPE_STR);
                } else if (!is_valid(obj)) {
                    free_var(propname);
                    free_var(obj);
                    PUSH_ERROR(E_INVIND);
                } else {
                    db_prop_handle h;
                    int built_in;

                    h = db_find_property(obj, propname.v.str, &prop);
                    built_in = db_is_property_built_in(h);

                    if (!h.ptr) {
                        PUSH_X_NOT_FOUND(E_PROPNF, propname, obj);
                    } else {
                        free_var(propname);
                        free_var(obj);

                        if (built_in
                                ? bi_prop_protected(built_in, RUN_ACTIV.progr)
                                : !db_property_allows(h, RUN_ACTIV.progr, PF_READ))
                            PUSH_ERROR(E_PERM);
                        else if (built_in)
                            PUSH(prop); /* it's already freshly allocated */
                        else
                            PUSH_REF(prop);
                    }
                }
            }
            break;

            case OP_PUSH_GET_PROP:
            {
                Var propname, obj, prop;

                propname = TOP_RT_VALUE;    /* should be string */
                obj = NEXT_TOP_RT_VALUE;    /* should be an object */
                if (obj.type == TYPE_WAIF && propname.type == TYPE_STR) {
                    enum error err;

                    err = waif_get_prop(obj.v.waif, propname.v.str, &prop, RUN_ACTIV.progr);
                    if (err == E_NONE)
                        PUSH(prop);
                    else
                        PUSH_ERROR(err);
                } else if (!obj.is_object() || propname.type != TYPE_STR) {
                    var_type incorrect_type = propname.type != TYPE_STR ? propname.type : obj.type;
                    PUSH_TYPE_MISMATCH(1, incorrect_type, incorrect_type == TYPE_STR ? TYPE_OBJ : TYPE_STR);
                } else if (!is_valid(obj))
                    PUSH_ERROR(E_INVIND);
                else {
                    db_prop_handle h;
                    int built_in;

                    h = db_find_property(obj, propname.v.str, &prop);
                    built_in = db_is_property_built_in(h);
                    if (!h.ptr) {
                        Var not_found = var_ref(propname);
                        PUSH_X_NOT_FOUND(E_PROPNF, not_found, var_ref(obj));
                    } else if (built_in
                               ? bi_prop_protected(built_in, RUN_ACTIV.progr)
                               : !db_property_allows(h, RUN_ACTIV.progr, PF_READ))
                        PUSH_ERROR(E_PERM);
                    else if (built_in)
                        PUSH(prop); /* it's already freshly allocated */
                    else
                        PUSH_REF(prop);
                }
            }
            break;

            case OP_PUT_PROP:
            {
                Var obj, propname, rhs;

                rhs = POP();        /* any type */
                propname = POP();   /* should be string */
                obj = POP();        /* should be an object */
                if (obj.type == TYPE_WAIF && propname.type == TYPE_STR) {
                    enum error err;

                    err = waif_put_prop(obj.v.waif, propname.v.str, rhs, RUN_ACTIV.progr);
                    free_var(obj);
                    if (err == E_NONE) {
                        free_var(propname);
                        PUSH(rhs);
                    } else {
                        free_var(rhs);
                        if (err == E_PROPNF) {
                            PUSH_X_NOT_FOUND(E_PROPNF, propname, var_ref(obj));
                        } else {
                            free_var(propname);
                            PUSH_ERROR(err);
                        }
                    }
                } else if (!obj.is_object() || propname.type != TYPE_STR) {
                    var_type incorrect_type = propname.type != TYPE_STR ? propname.type : obj.type;
                    free_var(rhs);
                    free_var(propname);
                    free_var(obj);
                    PUSH_TYPE_MISMATCH(1, incorrect_type, incorrect_type == TYPE_STR ? TYPE_OBJ : TYPE_STR);
                } else if (!is_valid(obj)) {
                    free_var(rhs);
                    free_var(propname);
                    free_var(obj);
                    PUSH_ERROR(E_INVIND);
                } else {
                    db_prop_handle h;
                    int built_in;
                    enum error err = E_NONE;
                    Objid progr = RUN_ACTIV.progr;

                    h = db_find_property(obj, propname.v.str, nullptr);
                    built_in = db_is_property_built_in(h);
                    if (!h.ptr)
                        err = E_PROPNF;
                    else {
                        switch (built_in) {
                            case BP_NONE:   /* not a built-in property */
                                if (!db_property_allows(h, progr, PF_WRITE))
                                    err = E_PERM;
                                break;
                            case BP_NAME:
                                if (rhs.type != TYPE_STR)
                                    err = E_TYPE;
                                else if (!is_wizard(progr) &&
                                         ((obj.is_obj() && is_user(obj.v.obj)) ||
                                          bi_prop_protected(built_in, progr) ||
                                          progr != db_object_owner2(obj)))
                                    err = E_PERM;
                                break;
                            case BP_OWNER:
                                if (rhs.type != TYPE_OBJ)
                                    err = E_TYPE;
                                else if (!is_wizard(progr))
                                    err = E_PERM;
                                break;
                            case BP_PROGRAMMER:
                            case BP_WIZARD:
                                if (!is_wizard(progr))
                                    err = E_PERM;
                                else if (!obj.is_obj())
                                    err = E_INVARG;
                                else if (built_in == BP_WIZARD
                                         && !is_true(rhs) != !is_wizard(obj.v.obj)) {
                                    /* Notify only on changes in state; the !'s above
                                     * serve to canonicalize the truth values.
                                     */
                                    /* First make sure traceback will be accurate. */
                                    STORE_STATE_VARIABLES();
                                    applog(LOG_WARNING, "%sWIZARDED: #%" PRIdN " by programmer #%" PRIdN "\n",
                                           is_wizard(obj.v.obj) ? "DE" : "",
                                           obj.v.obj, progr);
                                    print_error_backtrace(is_wizard(obj.v.obj)
                                                          ? "Wizard bit unset."
                                                          : "Wizard bit set.",
                                                          output_to_log);
                                }
                                break;
                            case BP_R:
                            case BP_W:
                            case BP_F:
                            case BP_A:
                                if (!is_wizard(progr) &&
                                        (bi_prop_protected(built_in, progr) ||
                                         progr != db_object_owner2(obj)))
                                    err = E_PERM;
                                break;
                            case BP_LOCATION:
                            case BP_LAST_MOVE:
                            case BP_CONTENTS:
                                err = E_PERM;
                                break;
                            default:
                                panic_moo("Unknown built-in property in OP_PUT_PROP!");
                        }
                    }

                    if (err == E_PROPNF) {
                        PUSH_X_NOT_FOUND(E_PROPNF, propname, obj);
                    } else {
                        free_var(propname);
                        free_var(obj);
                        if (err == E_NONE) {
                            db_set_property_value(h, var_ref(rhs));
                            PUSH(rhs);
                        } else {
                            free_var(rhs);
                            PUSH_ERROR(err);
                        }
                    }
                }
            }
            break;

            case OP_FORK:
            case OP_FORK_WITH_ID:
            {
                Var time;
                unsigned id = 0, f_index;
                double when;

                time = POP();
                f_index = READ_BYTES(bv, bc.numbytes_fork);
                if (op == OP_FORK_WITH_ID)
                    id = READ_BYTES(bv, bc.numbytes_var_name);
                if (time.type != TYPE_INT && time.type != TYPE_FLOAT) {
                    var_type time_type = time.type;
                    free_var(time);
                    RAISE_TYPE_MISMATCH(2, time_type, TYPE_INT, TYPE_FLOAT);
                }
                when = time.type == TYPE_INT ? time.v.num : time.v.fnum;
                free_var(time);
                if (when < 0) {
                    RAISE_ERROR(E_INVARG);
                } else {
                    enum error e;

                    e = enqueue_forked_task2(RUN_ACTIV, f_index, when,
                                             op == OP_FORK_WITH_ID ? id : -1);
                    if (e != E_NONE)
                        RAISE_ERROR(e);
                }
            }
            break;

            case OP_CALL_VERB:
            {
                enum error err;
                Var args, verb, obj;
                Objid _class;

                args = POP();   /* args, should be list */
                verb = POP();   /* verbname, should be string */
                obj = POP();    /* could be anything */

                if (args.type != TYPE_LIST || verb.type != TYPE_STR)
                    err = E_TYPE;
                else if (obj.type == TYPE_WAIF) {
                    char *str = (char *)mymalloc(strlen(verb.v.str) + 2, M_STRING);

                    _class = obj.v.waif->_class;
                    str[0] = WAIF_VERB_PREFIX;
                    strcpy(str + 1, verb.v.str);
                    free_str(verb.v.str);
                    verb.v.str = str;
                    STORE_STATE_VARIABLES();
                    err = call_verb2(_class, verb.v.str, obj, args, 0, DEFAULT_THREAD_MODE);
                    LOAD_STATE_VARIABLES();
                } else {
                    Objid recv = NOTHING;
                    db_prop_handle h;
                    Var p;

                    /* If it's an object, we're good.
                     * Otherwise, look for a property on the system
                     * object that points us to the prototype/handler
                     * for the primitive type.
                     */
                    Var system = Var::new_obj(SYSTEM_OBJECT);

#define         MATCH_TYPE(t1, t2)                      \
else if (obj.type == TYPE_##t1) {           \
    h = db_find_property(system, #t2 "_proto", &p); \
    if (h.ptr && p.type == TYPE_OBJ && valid(p.v.obj))  \
        recv = p.v.obj;                 \
}
                    if (obj.type == TYPE_ANON)
                        recv = NOTHING;
                    else if (obj.is_object() && is_valid(obj))
                        recv = obj.v.obj;
                    MATCH_TYPE(INT, int)
                    MATCH_TYPE(OBJ, obj)
                    MATCH_TYPE(FLOAT, float)
                    MATCH_TYPE(STR, str)
                    MATCH_TYPE(ERR, err)
                    MATCH_TYPE(LIST, list)
                    MATCH_TYPE(MAP, map)
#undef          MATCH_TYPE

                    free_var(system);

                    if (obj.is_object() || recv != NOTHING) {
                        STORE_STATE_VARIABLES();
                        err = call_verb2(recv, verb.v.str, obj, args, 0, DEFAULT_THREAD_MODE);
                        /* if there is no error, RUN_ACTIV is now the CALLEE's.
                           args will be consumed in the new rt_env */
                        /* if there is an error, then RUN_ACTIV is unchanged, and
                           args is not consumed in this case */
                        LOAD_STATE_VARIABLES();
                    }
                    else
                        err = E_TYPE;
                }

                if (err != E_NONE) {
                    /* there is an error, RUN_ACTIV unchanged, args must be freed */
                    free_var(args);
                    if (err == E_VERBNF) {
                        PUSH_X_NOT_FOUND(err, verb, obj);
                    } else {
                        free_var(verb);
                        free_var(obj);
                        PUSH_ERROR(err);
                    }
                } else {
                    free_var(verb);
                    free_var(obj);
                }
            }
            break;

            case OP_RETURN:
            case OP_RETURN0:
            case OP_DONE:
            {
                Var ret_val;

                if (op == OP_RETURN)
                    ret_val = POP();
                else
                    ret_val = zero;

                STORE_STATE_VARIABLES();
                if (unwind_stack(FIN_RETURN, ret_val, &outcome)) {
                    if (result && outcome == OUTCOME_DONE)
                        *result = ret_val;
                    else
                        free_var(ret_val);
                    return outcome;
                }
                LOAD_STATE_VARIABLES();
            }
            break;

            case OP_BI_FUNC_CALL:
            {
                unsigned func_id;
                Var args;

                func_id = READ_BYTES(bv, 1);    /* 1 == numbytes of func_id */
                args = POP();   /* should be list */
                if (args.type != TYPE_LIST) {
                    free_var(args);
                    PUSH_ERROR(E_TYPE);
                } else {
                    package p;

                    STORE_STATE_VARIABLES();
                    p = call_bi_func(func_id, args, 1, RUN_ACTIV.progr, nullptr);
                    LOAD_STATE_VARIABLES();

                    switch (p.kind) {
                        case package::BI_RETURN:
                            PUSH(p.u.ret);
                            break;
                        case package::BI_RAISE:
                            if (RUN_ACTIV.debug) {
                                if (raise_error(p, nullptr))
                                    return OUTCOME_ABORTED;
                                else
                                    LOAD_STATE_VARIABLES();
                            } else {
                                PUSH(p.u.raise.code);
                                free_str(p.u.raise.msg);
                                free_var(p.u.raise.value);
                            }
                            break;
                        case package::BI_CALL:
                            /* another activ has been pushed onto activ_stack */
                            RUN_ACTIV.bi_func_id = func_id;
                            RUN_ACTIV.bi_func_data = p.u.call.data;
                            RUN_ACTIV.bi_func_pc = p.u.call.pc;
                            break;
                        case package::BI_SUSPEND:
                        {
                            enum error e = suspend_task(p);

                            if (e == E_NONE)
                                return OUTCOME_BLOCKED;
                            else
                                PUSH_ERROR(e);
                        }
                        break;
                        case package::BI_KILL:
                            STORE_STATE_VARIABLES();
                            abort_task((abort_reason)p.u.ret.v.num);
                            return OUTCOME_ABORTED;
                            /* NOTREACHED */
                    }
                }
            }
            break;

            case OP_EXTENDED:
            {
                enum Extended_Opcode eop = (Extended_Opcode)(*bv);
                bv++;
                if (COUNT_EOP_TICK(eop))
                    ticks_remaining--;
                switch (eop) {
                    case EOP_RANGESET:
                    {
                        Var base, from, to, value;

                        value = POP();
                        to = POP();
                        from = POP();
                        base = POP();   /* map, list or string */

                        if ((base.type != TYPE_MAP && base.type != TYPE_LIST
                                && base.type != TYPE_STR)
                                || (base.type != value.type)) {
                            var_type base_type = base.type;
                            var_type value_type = value.type;
                            free_var(to);
                            free_var(from);
                            free_var(base);
                            free_var(value);
                            if (base_type != value_type)
                                PUSH_TYPE_MISMATCH(1, value_type, base.type);
                            else
                                PUSH_TYPE_MISMATCH(3, base_type, TYPE_LIST, TYPE_MAP, TYPE_STR);
                        } else if (base.type == TYPE_MAP
                                   && ((to.is_collection() && TYPE_ANON != to.type) || (from.is_collection() && TYPE_ANON != from.type))) {
                            var_type to_from_type = to.is_collection() ? to.type : from.type;
                            free_var(to);
                            free_var(from);
                            free_var(base);
                            free_var(value);
                            PUSH_TYPE_MISMATCH(8, to_from_type, TYPE_STR, TYPE_INT, TYPE_OBJ, TYPE_ERR, TYPE_FLOAT, TYPE_ANON, TYPE_WAIF, TYPE_BOOL);
                        } else if ((base.type == TYPE_LIST || base.type == TYPE_STR)
                                   && (to.type != TYPE_INT || from.type != TYPE_INT)) {
                            var_type to_from_type = to.type != TYPE_INT ? to.type : from.type;
                            free_var(to);
                            free_var(from);
                            free_var(base);
                            free_var(value);
                            PUSH_TYPE_MISMATCH(1, to_from_type, TYPE_INT);
                        } else if (base.type == TYPE_MAP) {
                            Var res = none;
                            Var iterfrom, iterto;
                            mapseek(base, from, &iterfrom, 0);
                            mapseek(base, to, &iterto, 0);
                            if (iterfrom.is_none() || iterto.is_none()) {
                                free_var(to);
                                free_var(from);
                                free_var(iterto);
                                free_var(iterfrom);
                                free_var(base);
                                free_var(value);
                                PUSH_ERROR(E_RANGE);
                            } else {
                                maprangeset(base, iterfrom.v.trav, iterto.v.trav, value, &res);
                                free_var(to);
                                free_var(from);
                                free_var(iterto);
                                free_var(iterfrom);
                                if (value_bytes(res) <= server_int_option_cached(SVO_MAX_MAP_VALUE_BYTES))
                                    PUSH(res);
                                else {
                                    free_var(res);
                                    PUSH_ERROR_UNLESS_QUOTA(E_QUOTA);
                                }
                            }
                        } else if (base.type == TYPE_LIST) {
                            Var res;
                            if (from.v.num > base.v.list[0].v.num + 1 || to.v.num < 0) {
                                free_var(to);
                                free_var(from);
                                free_var(base);
                                free_var(value);
                                PUSH_ERROR(E_RANGE);
                            } else {
                                res = listrangeset(base, from.v.num, to.v.num, value);
                                if (value_bytes(res) <= server_int_option_cached(SVO_MAX_LIST_VALUE_BYTES))
                                    PUSH(res);
                                else {
                                    free_var(res);
                                    PUSH_ERROR_UNLESS_QUOTA(E_QUOTA);
                                }
                            }
                        } else {    /* TYPE_STR */
                            Var res;
                            if (from.v.num > memo_strlen(base.v.str) + 1 || to.v.num < 0) {
                                free_var(to);
                                free_var(from);
                                free_var(base);
                                free_var(value);
                                PUSH_ERROR(E_RANGE);
                            } else {
                                res = strrangeset(base, from.v.num, to.v.num, value);
                                if (memo_strlen(res.v.str) <= server_int_option_cached(SVO_MAX_STRING_CONCAT))
                                    PUSH(res);
                                else {
                                    free_var(res);
                                    PUSH_ERROR_UNLESS_QUOTA(E_QUOTA);
                                }
                            }
                        }
                    }
                    break;

                    case EOP_FIRST:
                    {
                        unsigned i = READ_BYTES(bv, bc.numbytes_stack);
                        Var item, v;

                        item = RUN_ACTIV.base_rt_stack[i];
                        if (item.type == TYPE_STR) {
                            v.type = TYPE_INT;
                            v.v.num = memo_strlen(item.v.str) > 0 ? 1 : 0;
                            PUSH(v);
                        } else if (item.type == TYPE_LIST) {
                            v.type = TYPE_INT;
                            v.v.num = item.v.list[0].v.num > 0 ? 1 : 0;
                            PUSH(v);
                        } else if (item.type == TYPE_MAP) {
                            var_pair pair;
                            v = mapfirst(item, &pair)
                                ? var_ref(pair.a)
                                : var_ref(none);
                            PUSH(v);
                        } else
                            PUSH_TYPE_MISMATCH(3, item.type, TYPE_STR, TYPE_LIST, TYPE_MAP);
                    }
                    break;

                    case EOP_LAST:
                    {
                        unsigned i = READ_BYTES(bv, bc.numbytes_stack);
                        Var item, v;

                        item = RUN_ACTIV.base_rt_stack[i];
                        if (item.type == TYPE_STR) {
                            v.type = TYPE_INT;
                            v.v.num = memo_strlen(item.v.str);
                            PUSH(v);
                        } else if (item.type == TYPE_LIST) {
                            v.type = TYPE_INT;
                            v.v.num = item.v.list[0].v.num;
                            PUSH(v);
                        } else if (item.type == TYPE_MAP) {
                            var_pair pair;
                            v = maplast(item, &pair)
                                ? var_ref(pair.a)
                                : var_ref(none);
                            PUSH(v);
                        } else
                            PUSH_TYPE_MISMATCH(3, item.type, TYPE_STR, TYPE_LIST, TYPE_MAP);
                    }
                    break;

                    case EOP_EXP:
                    {
                        Var lhs, rhs, ans;

                        rhs = POP();
                        lhs = POP();
                        ans = do_power(lhs, rhs);
                        free_var(lhs);
                        free_var(rhs);
                        if (ans.type == TYPE_ERR)
                            PUSH_ERROR(ans.v.err);
                        else
                            PUSH(ans);
                    }
                    break;

                    case EOP_SCATTER:
                    {
                        int nargs = READ_BYTES(bv, 1);
                        int nreq = READ_BYTES(bv, 1);
                        int rest = READ_BYTES(bv, 1);
                        int have_rest = (rest > nargs ? 0 : 1);
                        Var list;
                        int len = 0, nopt_avail, nrest, i, offset;
                        int done, where = 0;
                        enum error e = E_NONE;

                        list = TOP_RT_VALUE;
                        if (list.type != TYPE_LIST)
                            e = E_TYPE;
                        else if ((len = list.v.list[0].v.num) < nreq
                                 || (!have_rest && len > nargs))
                            e = E_ARGS;

                        if (e != E_NONE) {  /* skip rest of operands */
                            free_var(POP());    /* replace list with error code */
                            if (e == E_TYPE)
                                PUSH_TYPE_MISMATCH(1, list.type, TYPE_LIST);
                            else
                                PUSH_ERROR(e);
                            for (i = 1; i <= nargs; i++) {
                                SKIP_BYTES(bv, bc.numbytes_var_name);
                                SKIP_BYTES(bv, bc.numbytes_label);
                            }
                        } else {
                            nopt_avail = len - nreq;
                            nrest = (have_rest && len >= nargs ? len - nargs + 1
                                     : 0);
                            for (offset = 0, i = 1; i <= nargs; i++) {
                                int id = READ_BYTES(bv, bc.numbytes_var_name);
                                int label = READ_BYTES(bv, bc.numbytes_label);

                                if (i == rest) {    /* rest */
                                    free_var(RUN_ACTIV.rt_env[id]);
                                    RUN_ACTIV.rt_env[id] = sublist(var_ref(list),
                                                                   i,
                                                                   i + nrest - 1);
                                    offset += nrest - 1;
                                } else if (label == 0) {    /* required */
                                    free_var(RUN_ACTIV.rt_env[id]);
                                    RUN_ACTIV.rt_env[id] =
                                        var_ref(list.v.list[i + offset]);
                                } else {    /* optional */
                                    if (nopt_avail > 0) {
                                        nopt_avail--;
                                        free_var(RUN_ACTIV.rt_env[id]);
                                        RUN_ACTIV.rt_env[id] =
                                            var_ref(list.v.list[i + offset]);
                                    } else {
                                        offset--;
                                        if (where == 0 && label != 1)
                                            where = label;
                                    }
                                }
                            }
                        }

                        done = READ_BYTES(bv, bc.numbytes_label);
                        if (where == 0)
                            JUMP(done);
                        else
                            JUMP(where);
                    }
                    break;

                    case EOP_PUSH_LABEL:
                    case EOP_TRY_FINALLY:
                    {
                        Var v;

                        v.type = (eop == EOP_PUSH_LABEL ? TYPE_INT : TYPE_FINALLY);
                        v.v.num = READ_BYTES(bv, bc.numbytes_label);
                        PUSH(v);
                    }
                    break;

                    case EOP_CATCH:
                    case EOP_TRY_EXCEPT:
                    {
                        Var v;

                        v.type = TYPE_CATCH;
                        v.v.num = (eop == EOP_CATCH ? 1 : READ_BYTES(bv, 1));
                        PUSH(v);
                    }
                    break;

                    case EOP_END_CATCH:
                    case EOP_END_EXCEPT:
                    {
                        Var v, marker;
                        int i;
                        unsigned lab;

                        if (eop == EOP_END_CATCH)
                            v = POP();

                        marker = POP();
                        if (marker.type != TYPE_CATCH) {
                            errlog("Marker is type %i with value %i\n", marker.type, marker.v.num);
                            panic_moo("Stack marker is not TYPE_CATCH!");
                        }
                        for (i = 0; i < marker.v.num; i++) {
                            (void) POP();   /* handler PC */
                            free_var(POP());    /* code list */
                        }

                        if (eop == EOP_END_CATCH)
                            PUSH(v);

                        lab = READ_BYTES(bv, bc.numbytes_label);
                        JUMP(lab);
                    }
                    break;

                    case EOP_END_FINALLY:
                    {
                        Var v, why;

                        v = POP();
                        if (v.type != TYPE_FINALLY)
                            panic_moo("Stack marker is not TYPE_FINALLY!");
                        why.type = TYPE_INT;
                        why.v.num = FIN_FALL_THRU;
                        PUSH(why);
                        PUSH(zero);
                    }
                    break;

                    case EOP_CONTINUE:
                    {
                        Var v, why;

                        v = POP();
                        why = POP();
                        switch (why.type == TYPE_INT ? why.v.num : -1) {
                            case FIN_FALL_THRU:
                                /* Do nothing; normal case. */
                                break;
                            case FIN_EXIT:
                            case FIN_RAISE:
                            case FIN_RETURN:
                            case FIN_UNCAUGHT:
                                STORE_STATE_VARIABLES();
                                if (unwind_stack((Finally_Reason)why.v.num, v, &outcome))
                                    return outcome;
                                LOAD_STATE_VARIABLES();
                                break;
                            default:
                                panic_moo("Unknown FINALLY reason!");
                        }
                    }
                    break;

                    case EOP_WHILE_ID:
                    {
                        unsigned id = READ_BYTES(bv, bc.numbytes_var_name);
                        free_var(RUN_ACTIV.rt_env[id]);
                        RUN_ACTIV.rt_env[id] = var_ref(TOP_RT_VALUE);
                    }
                    goto do_test;

                    case EOP_EXIT_ID:
                        SKIP_BYTES(bv, bc.numbytes_var_name);   /* ignore id */
                    /* fall thru */
                    case EOP_EXIT:
                    {
                        Var v;

                        v = new_list(2);
                        v.v.list[1].type = TYPE_INT;
                        v.v.list[1].v.num = READ_BYTES(bv, bc.numbytes_stack);
                        v.v.list[2].type = TYPE_INT;
                        v.v.list[2].v.num = READ_BYTES(bv, bc.numbytes_label);
                        STORE_STATE_VARIABLES();
                        (void) unwind_stack(FIN_EXIT, v, nullptr);
                        LOAD_STATE_VARIABLES();
                    }
                    break;

                    case EOP_FOR_LIST_1:
                    {
#           define ITER TOP_RT_VALUE
#           define BASE NEXT_TOP_RT_VALUE

                        unsigned id = READ_BYTES(bv, bc.numbytes_var_name);
                        unsigned lab = READ_BYTES(bv, bc.numbytes_label);

                        if (BASE.type != TYPE_STR && BASE.type != TYPE_LIST
                                && BASE.type != TYPE_MAP) {
                            RAISE_TYPE_MISMATCH(3, BASE.type, TYPE_STR, TYPE_LIST, TYPE_MAP);
                            free_var(POP());
                            free_var(POP());
                            JUMP(lab);
                        } else if (BASE.type == TYPE_STR || BASE.type == TYPE_LIST) {
                            int len = (BASE.type == TYPE_STR
                                       ? memo_strlen(BASE.v.str)
                                       : BASE.v.list[0].v.num);
                            if (ITER.type == TYPE_NONE) {
                                free_var(ITER);
                                ITER = Var::new_int(1);
                            }
                            if (ITER.v.num > len) {
                                free_var(POP());
                                free_var(POP());
                                JUMP(lab);
                            } else {
                                free_var(RUN_ACTIV.rt_env[id]);
                                RUN_ACTIV.rt_env[id] = (BASE.type == TYPE_STR)
                                                       ? strget(BASE, ITER.v.num)
                                                       : var_ref(BASE.v.list[ITER.v.num]);
                                ITER.v.num++;   /* increment iter */
                            }
                        } else if (BASE.type == TYPE_MAP) {
                            if (ITER.type == TYPE_NONE) {
                                /* starting iteration */
                                free_var(ITER);
                                ITER = new_iter(BASE);
                            } else if (ITER.type != TYPE_ITER) {
                                /* resuming an iteration after a db load */
                                Var iter;
                                mapseek(BASE, ITER, &iter, 0);
                                free_var(ITER);
                                ITER = iter;
                            }
                            var_pair pair;
                            if (ITER.type == TYPE_NONE || !iterget(ITER, &pair)) {
                                free_var(POP());
                                free_var(POP());
                                JUMP(lab);
                            } else {
                                free_var(RUN_ACTIV.rt_env[id]);
                                RUN_ACTIV.rt_env[id] = var_ref(pair.b);
                                iternext(ITER); /* increment iter */
                            }
                        }
#           undef ITER
#           undef BASE
                    }
                    break;

                    case EOP_FOR_LIST_2:
                    {
#           define ITER TOP_RT_VALUE
#           define BASE NEXT_TOP_RT_VALUE

                        unsigned id = READ_BYTES(bv, bc.numbytes_var_name);
                        unsigned index = READ_BYTES(bv, bc.numbytes_var_name);
                        unsigned lab = READ_BYTES(bv, bc.numbytes_label);

                        if (BASE.type != TYPE_STR && BASE.type != TYPE_LIST
                                && BASE.type != TYPE_MAP) {
                            RAISE_TYPE_MISMATCH(3, BASE.type, TYPE_STR, TYPE_LIST, TYPE_MAP);
                            free_var(POP());
                            free_var(POP());
                            JUMP(lab);
                        } else if (BASE.type == TYPE_STR || BASE.type == TYPE_LIST) {
                            int len = (BASE.type == TYPE_STR
                                       ? memo_strlen(BASE.v.str)
                                       : BASE.v.list[0].v.num);
                            if (ITER.type == TYPE_NONE) {
                                free_var(ITER);
                                ITER = Var::new_int(1);
                            }
                            if (ITER.v.num > len) {
                                free_var(POP());
                                free_var(POP());
                                JUMP(lab);
                            } else {
                                free_var(RUN_ACTIV.rt_env[id]);
                                RUN_ACTIV.rt_env[id] = (BASE.type == TYPE_STR)
                                                       ? strget(BASE, ITER.v.num)
                                                       : var_ref(BASE.v.list[ITER.v.num]);
                                free_var(RUN_ACTIV.rt_env[index]);
                                RUN_ACTIV.rt_env[index] = var_ref(ITER);
                                ITER.v.num++;   /* increment iter */
                            }
                        } else if (BASE.type == TYPE_MAP) {
                            if (ITER.type == TYPE_NONE) {
                                free_var(ITER);
                                ITER = new_iter(BASE);
                            } else if (ITER.type != TYPE_ITER) {
                                Var iter;
                                mapseek(BASE, ITER, &iter, 0);
                                free_var(ITER);
                                ITER = iter;
                            }
                            var_pair pair;
                            if (ITER.type == TYPE_NONE || !iterget(ITER, &pair)) {
                                free_var(POP());
                                free_var(POP());
                                JUMP(lab);
                            } else {
                                free_var(RUN_ACTIV.rt_env[id]);
                                RUN_ACTIV.rt_env[id] = var_ref(pair.b);
                                free_var(RUN_ACTIV.rt_env[index]);
                                RUN_ACTIV.rt_env[index] = var_ref(pair.a);
                                iternext(ITER); /* increment iter */
                            }
                        }
#           undef ITER
#           undef BASE
                    }
                    break;

                    case EOP_BITOR:
                    case EOP_BITAND:
                    case EOP_BITXOR:
                    {
                        Var rhs, lhs, ans;
                        var_type lhs_type, rhs_type;

                        rhs = POP();
                        lhs = POP();
                        if (lhs.type == TYPE_INT && rhs.type == TYPE_INT) {
                            ans.type = TYPE_INT;
                            if (eop == EOP_BITXOR)
                                ans.v.num = lhs.v.num ^ rhs.v.num;
                            else if (eop == EOP_BITAND)
                                ans.v.num = lhs.v.num & rhs.v.num;
                            else if (eop == EOP_BITOR)
                                ans.v.num = lhs.v.num | rhs.v.num;
                            else
                                errlog("RUN: Impossible opcode in bitwise ops: %d\n", eop);
                        } else {
                            ans.type = TYPE_ERR;
                            ans.v.err = E_TYPE;
                            lhs_type = lhs.type;
                            rhs_type = rhs.type;
                        }

                        free_var(lhs);
                        free_var(rhs);
                        if (ans.type == TYPE_ERR)
                            if (ans.v.err == E_TYPE)
                                PUSH_TYPE_MISMATCH(1, lhs_type != TYPE_INT ? lhs_type : rhs_type, TYPE_INT);
                            else
                                PUSH_ERROR(ans.v.err);
                        else
                            PUSH(ans);
                    }
                    break;

                    case EOP_BITSHL:
                    case EOP_BITSHR:
                    {
                        Var rhs, lhs, ans;
                        var_type lhs_type, rhs_type;

                        rhs = POP();
                        lhs = POP();
                        if (lhs.type != TYPE_INT || rhs.type != TYPE_INT) {
                            ans.type = TYPE_ERR;
                            ans.v.err = E_TYPE;
                            lhs_type = lhs.type;
                            rhs_type = rhs.type;
                        } else if (rhs.v.num > sizeof(Num) * CHAR_BIT || rhs.v.num < 0) {
                            ans.type = TYPE_ERR;
                            ans.v.err = E_INVARG;
                        } else if (rhs.v.num == sizeof(Num) * CHAR_BIT) {
                            ans.type = TYPE_INT;
                            ans.v.num = 0;
                        } else if (rhs.v.num == 0) {
                            ans.type = TYPE_INT;
                            ans.v.num = lhs.v.num;
                        } else {
                            ans.type = TYPE_INT;
                            if (eop == EOP_BITSHL)
                                ans.v.num = lhs.v.num << rhs.v.num;
                            else if (eop == EOP_BITSHR)
                                ans.v.num = (UNum)lhs.v.num >> rhs.v.num;
                            else
                                errlog("RUN: Impossible opcode in bitwise ops: %d\n", eop);
                        }

                        free_var(lhs);
                        free_var(rhs);
                        if (ans.type == TYPE_ERR)
                            if (ans.v.err == E_TYPE)
                                PUSH_TYPE_MISMATCH(1, lhs_type != TYPE_INT ? lhs_type : rhs_type, TYPE_INT);
                            else
                                PUSH_ERROR(ans.v.err);
                        else
                            PUSH(ans);
                    }
                    break;

                    case EOP_COMPLEMENT:
                    {
                        Var arg, ans;
                        var_type arg_type;

                        arg = POP();
                        if (arg.type == TYPE_INT) {
                            ans.type = TYPE_INT;
                            ans.v.num = ~arg.v.num;
                        } else {
                            ans.type = TYPE_ERR;
                            ans.v.err = E_TYPE;
                            arg_type = arg.type;
                        }

                        free_var(arg);
                        if (ans.type == TYPE_ERR)
                            if (ans.v.err == E_TYPE)
                                PUSH_TYPE_MISMATCH(1, arg_type, TYPE_INT);
                            else
                                PUSH_ERROR(ans.v.err);
                        else
                            PUSH(ans);
                    }
                    break;

                    default:
                        panic_moo("Unknown extended opcode!");
                }
            }
            break;

                /* These opcodes account for about 20% of all opcodes executed, so
                   let's split out the case stmt so the compiler can help us out.
                   If you're here because the #error below got tripped, just change
                   the set of case stmts below for OP_PUSH and OP_PUT to
                   be 0..NUM_READY_VARS-1.
                 */
#if NUM_READY_VARS != 32
#error NUM_READY_VARS expected to be 32
#endif
            case OP_PUSH:
            case OP_PUSH + 1:
            case OP_PUSH + 2:
            case OP_PUSH + 3:
            case OP_PUSH + 4:
            case OP_PUSH + 5:
            case OP_PUSH + 6:
            case OP_PUSH + 7:
            case OP_PUSH + 8:
            case OP_PUSH + 9:
            case OP_PUSH + 10:
            case OP_PUSH + 11:
            case OP_PUSH + 12:
            case OP_PUSH + 13:
            case OP_PUSH + 14:
            case OP_PUSH + 15:
            case OP_PUSH + 16:
            case OP_PUSH + 17:
            case OP_PUSH + 18:
            case OP_PUSH + 19:
            case OP_PUSH + 20:
            case OP_PUSH + 21:
            case OP_PUSH + 22:
            case OP_PUSH + 23:
            case OP_PUSH + 24:
            case OP_PUSH + 25:
            case OP_PUSH + 26:
            case OP_PUSH + 27:
            case OP_PUSH + 28:
            case OP_PUSH + 29:
            case OP_PUSH + 30:
            case OP_PUSH + 31:
            {
                Var value;
                value = RUN_ACTIV.rt_env[PUSH_n_INDEX(op)];
                if (value.type == TYPE_NONE) {
                    free_var(value);
                    Var not_found = str_ref_to_var(*(&RUN_ACTIV.prog->var_names[PUSH_n_INDEX(op)]));
                    PUSH_X_NOT_FOUND(E_VARNF, not_found, var_ref(nothing));
                } else
                    PUSH_REF(value);
            }
            break;

#ifdef BYTECODE_REDUCE_REF
            case OP_PUSH_CLEAR:
            case OP_PUSH_CLEAR + 1:
            case OP_PUSH_CLEAR + 2:
            case OP_PUSH_CLEAR + 3:
            case OP_PUSH_CLEAR + 4:
            case OP_PUSH_CLEAR + 5:
            case OP_PUSH_CLEAR + 6:
            case OP_PUSH_CLEAR + 7:
            case OP_PUSH_CLEAR + 8:
            case OP_PUSH_CLEAR + 9:
            case OP_PUSH_CLEAR + 10:
            case OP_PUSH_CLEAR + 11:
            case OP_PUSH_CLEAR + 12:
            case OP_PUSH_CLEAR + 13:
            case OP_PUSH_CLEAR + 14:
            case OP_PUSH_CLEAR + 15:
            case OP_PUSH_CLEAR + 16:
            case OP_PUSH_CLEAR + 17:
            case OP_PUSH_CLEAR + 18:
            case OP_PUSH_CLEAR + 19:
            case OP_PUSH_CLEAR + 20:
            case OP_PUSH_CLEAR + 21:
            case OP_PUSH_CLEAR + 22:
            case OP_PUSH_CLEAR + 23:
            case OP_PUSH_CLEAR + 24:
            case OP_PUSH_CLEAR + 25:
            case OP_PUSH_CLEAR + 26:
            case OP_PUSH_CLEAR + 27:
            case OP_PUSH_CLEAR + 28:
            case OP_PUSH_CLEAR + 29:
            case OP_PUSH_CLEAR + 30:
            case OP_PUSH_CLEAR + 31:
            {
                Var *vp;
                vp = &RUN_ACTIV.rt_env[PUSH_CLEAR_n_INDEX(op)];
                if (vp->type == TYPE_NONE) {
                    Var not_found = str_ref_to_var(*(&RUN_ACTIV.prog->var_names[PUSH_CLEAR_n_INDEX(op)]));
                    PUSH_X_NOT_FOUND(E_VARNF, not_found, var_ref(nothing));
                } else {
                    PUSH(*vp);
                    vp->type = TYPE_NONE;
                }
            }
            break;
#endif              /* BYTECODE_REDUCE_REF */

            case OP_PUT:
            case OP_PUT + 1:
            case OP_PUT + 2:
            case OP_PUT + 3:
            case OP_PUT + 4:
            case OP_PUT + 5:
            case OP_PUT + 6:
            case OP_PUT + 7:
            case OP_PUT + 8:
            case OP_PUT + 9:
            case OP_PUT + 10:
            case OP_PUT + 11:
            case OP_PUT + 12:
            case OP_PUT + 13:
            case OP_PUT + 14:
            case OP_PUT + 15:
            case OP_PUT + 16:
            case OP_PUT + 17:
            case OP_PUT + 18:
            case OP_PUT + 19:
            case OP_PUT + 20:
            case OP_PUT + 21:
            case OP_PUT + 22:
            case OP_PUT + 23:
            case OP_PUT + 24:
            case OP_PUT + 25:
            case OP_PUT + 26:
            case OP_PUT + 27:
            case OP_PUT + 28:
            case OP_PUT + 29:
            case OP_PUT + 30:
            case OP_PUT + 31:
            {
                Var *varp = &RUN_ACTIV.rt_env[PUT_n_INDEX(op)];
                free_var(*varp);
                if (bv[0] == OP_POP) {
                    *varp = POP();
                    ++bv;
                } else
                    *varp = var_ref(TOP_RT_VALUE);
            }
            break;

            default:
                if (IS_OPTIM_NUM_OPCODE(op)) {
                    Var value;
                    value.type = TYPE_INT;
                    value.v.num = OPCODE_TO_OPTIM_NUM(op);
                    PUSH(value);
                } else
                    panic_moo("Unknown opcode!");
                break;
        }
    }
}


/**** manipulating data of task ****/

static int timeouts_enabled = 1;    /* set to 0 in debugger to disable
                       timeouts */

static void
task_timeout(Timer_ID id, Timer_Data data)
{
    task_timed_out = timeouts_enabled;
}

static Timer_ID
setup_task_execution_limits(int seconds, int ticks)
{
    task_alarm_id = set_virtual_timer(seconds < 1 ? 1 : seconds,
                                      task_timeout, nullptr);
    task_timed_out = 0;
    ticks_remaining = (ticks < 100 ? 100 : ticks);
    return task_alarm_id;
}

enum outcome
run_interpreter(char raise, enum error e,
                Var *result, int is_fg, int do_db_tracebacks)
/* raise is boolean, true iff an error should be raised.
       e is the specific error to be raised if so.
       (in earlier versions, an error was raised iff e != E_NONE,
       but now it's possible to raise E_NONE on resumption from
       suspend().) */
{
    enum outcome ret;
    Var args;

    setup_task_execution_limits(is_fg ? server_int_option("fg_seconds",
                                DEFAULT_FG_SECONDS)
                                : server_int_option("bg_seconds",
                                        DEFAULT_BG_SECONDS),
                                is_fg ? server_int_option("fg_ticks",
                                        DEFAULT_FG_TICKS)
                                : server_int_option("bg_ticks",
                                        DEFAULT_BG_TICKS));

    /* handler_verb_* is garbage/unreferenced outside of run()
     * and this is the only place run() is called. */
    handler_verb_args = zero;
    handler_verb_name = nullptr;

#ifdef SAVE_FINISHED_TASKS
    Var postmortem = new_map();

    postmortem = mapinsert(postmortem, var_ref(map_this), Var::new_obj(RUN_ACTIV._this.v.obj));
    postmortem = mapinsert(postmortem, var_ref(map_player), Var::new_obj(RUN_ACTIV.player));
    postmortem = mapinsert(postmortem, var_ref(map_programmer), Var::new_obj(RUN_ACTIV.progr));
    postmortem = mapinsert(postmortem, var_ref(map_receiver), Var::new_obj(RUN_ACTIV.recv));
    postmortem = mapinsert(postmortem, var_ref(map_object), Var::new_obj(RUN_ACTIV.vloc.v.obj));
    postmortem = mapinsert(postmortem, var_ref(map_verb), strcmp(RUN_ACTIV.verb, "") == 0 ? str_dup_to_var("n/a") : str_dup_to_var(RUN_ACTIV.verb));
    postmortem = mapinsert(postmortem, var_ref(map_fullverb), strcmp(RUN_ACTIV.verbname, "") == 0 ? str_dup_to_var("n/a") : str_dup_to_var(RUN_ACTIV.verbname));
    postmortem = mapinsert(postmortem, var_ref(map_foreground), Var::new_int(is_fg));
#endif  /* SAVE_FINISHED_TASKS */

    Var total_cputime;
    total_cputime.type = TYPE_FLOAT;

    interpreter_is_running = 1;
    std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();
    ret = run(raise, e, result);
    std::chrono::duration<double> elapsed = std::chrono::high_resolution_clock::now() - start;
    total_cputime.v.fnum = elapsed.count();
    interpreter_is_running = 0;

    args = handler_verb_args;

    cancel_timer(task_alarm_id);
    task_timed_out = 0;

    double lag_threshold = server_float_option("task_lag_threshold", DEFAULT_LAG_THRESHOLD);
    if (total_cputime.v.fnum >= lag_threshold && lag_threshold >= 0.1)
    {
        errlog("LAG: %f seconds\n", total_cputime.v.fnum);
        db_verb_handle handle = db_find_callable_verb(Var::new_obj(SYSTEM_OBJECT), "handle_lagging_task");
        if (handle.ptr)
        {
            Var lag_info = new_list(2);
            lag_info.v.list[1] = make_stack_list(activ_stack, 0, top_activ_stack, 0, root_activ_vector, 1, server_int_option("INCLUDE_RT_VARS", 0), RUN_ACTIV.progr);
            lag_info.v.list[2] = total_cputime;
            do_server_verb_task(Var::new_obj(SYSTEM_OBJECT), "handle_lagging_task", lag_info, handle, activ_stack[0].player, "", nullptr, 0);
        }
    }

#ifdef SAVE_FINISHED_TASKS
    postmortem = mapinsert(postmortem, var_ref(map_suspended), (ret == OUTCOME_BLOCKED ? Var::new_int(1) : Var::new_int(0))); // Task suspended?
    postmortem = mapinsert(postmortem, var_ref(map_time), total_cputime);
    finished_tasks = listappend(finished_tasks, postmortem);

    while (finished_tasks.v.list[0].v.num > server_int_option("finished_tasks_limit", SAVE_FINISHED_TASKS) && finished_tasks.v.list[0].v.num > 1)
        finished_tasks = listdelete(finished_tasks, 1);
#endif /* SAVE_FINISHED_TASKS */

    if (ret == OUTCOME_ABORTED && handler_verb_name)
    {
        db_verb_handle h;
        enum outcome hret;
        Var handled, traceback;
        int i;

        h = db_find_callable_verb(Var::new_obj(SYSTEM_OBJECT), handler_verb_name);
        if (do_db_tracebacks && h.ptr)
        {
            hret = do_server_verb_task(Var::new_obj(SYSTEM_OBJECT), handler_verb_name,
                                       var_ref(args), h,
                                       activ_stack[0].player, "", &handled,
                                       0 /*no-traceback*/);
            if ((hret == OUTCOME_DONE && is_true(handled)) || hret == OUTCOME_BLOCKED)
            {
                /* Assume the in-DB code handled it */
                free_var(args);
                return OUTCOME_ABORTED; /* original ret value */
            }
        }
        i = args.v.list[0].v.num;
        traceback = args.v.list[i]; /* traceback is always the last argument */
        for (i = 1; i <= traceback.v.list[0].v.num; i++)
            notify(activ_stack[0].player, traceback.v.list[i].v.str);
    }

    free_var(args);
    return ret;
}

Var
caller()
{
    return RUN_ACTIV._this;
}

static void
check_activ_stack_size(int max)
{
    if (max_stack_size != max) {
        if (activ_stack)
            myfree(activ_stack, M_VM);

        activ_stack = (activation *)mymalloc(sizeof(activation) * max, M_VM);
        max_stack_size = max;
    }
}

static int
current_max_stack_size(void)
{
    int max = server_int_option("max_stack_depth", DEFAULT_MAX_STACK_DEPTH);

    if (max < DEFAULT_MAX_STACK_DEPTH)
        max = DEFAULT_MAX_STACK_DEPTH;

    return max;
}

/**** There are two methods of starting a new task:
   (1) Create a new one
   (2) Resume an old one  */

/* procedure to create a new task */

static enum outcome
do_task(Program * prog, int which_vector, Var * result, int is_fg, int do_db_tracebacks)
{   /* which vector determines the vector for the root_activ.
       a forked task can also have which_vector == MAIN_VECTOR.
       this happens iff it is recovered from a read from disk,
       because in that case the forked statement is parsed as
       the main vector */

    RUN_ACTIV.prog = program_ref(prog);

    root_activ_vector = which_vector;   /* main or which of the forked */
    alloc_rt_stack(&RUN_ACTIV, (which_vector == MAIN_VECTOR
                                ? prog->main_vector.max_stack
                                : prog->fork_vectors[which_vector].max_stack));

    RUN_ACTIV.pc = 0;
    RUN_ACTIV.error_pc = 0;
    RUN_ACTIV.bi_func_pc = 0;
    RUN_ACTIV.temp.type = TYPE_NONE;

    return run_interpreter(0, E_NONE, result, is_fg, do_db_tracebacks);
}

/* procedure to resume an old task */

enum outcome
resume_from_previous_vm(vm the_vm, Var v)
{
    unsigned int i;

    check_activ_stack_size(the_vm->max_stack_size);
    top_activ_stack = the_vm->top_activ_stack;
    root_activ_vector = the_vm->root_activ_vector;
    for (i = 0; i <= top_activ_stack; i++)
        activ_stack[i] = the_vm->activ_stack[i];

    free_vm(the_vm, 0);

    if (v.type == TYPE_ERR)
        return run_interpreter(1, v.v.err, nullptr, 0/*bg*/, 1/*traceback*/);
    else {
        /* PUSH_REF(v) */
        *(RUN_ACTIV.top_rt_stack++) = var_ref(v);

        return run_interpreter(0, E_NONE, nullptr, 0/*bg*/, 1/*traceback*/);
    }
}


/*** external functions ***/

enum outcome
do_server_verb_task(Var _this, const char *verb, Var args, db_verb_handle h,
                    Objid player, const char *argstr, Var *result,
                    int do_db_tracebacks)
{
    return do_server_program_task(_this, verb, args, db_verb_definer(h),
                                  db_verb_names(h), db_verb_program(h),
                                  db_verb_owner(h), db_verb_flags(h) & VF_DEBUG,
                                  player, argstr, result, do_db_tracebacks);
}

enum outcome
do_server_program_task(Var _this, const char *verb, Var args, Var vloc,
                       const char *verbname, Program * program, Objid progr,
                       int debug, Objid player, const char *argstr,
                       Var * result, int do_db_tracebacks)
{
    Var *env;

    Objid receiver;
    switch (_this.type) {
        case TYPE_OBJ:
            receiver = _this.v.obj;
            break;
        case TYPE_WAIF:
            receiver = _this.v.waif->_class;
            break;
        default:
            receiver = NOTHING;
            break;
    }

    check_activ_stack_size(current_max_stack_size());
    top_activ_stack = 0;

    RUN_ACTIV.rt_env = env = new_rt_env(program->num_var_names);
    RUN_ACTIV._this = var_ref(_this);
    RUN_ACTIV.player = player;
    RUN_ACTIV.progr = progr;
    RUN_ACTIV.recv = receiver;
    RUN_ACTIV.vloc = var_ref(vloc);
    RUN_ACTIV.verb = str_dup(verb);
    RUN_ACTIV.verbname = str_dup(verbname);
    RUN_ACTIV.debug = debug;
    RUN_ACTIV.threaded = DEFAULT_THREAD_MODE;
    fill_in_rt_consts(env, program->version);
    set_rt_env_obj(env, SLOT_PLAYER, player);
    set_rt_env_obj(env, SLOT_CALLER, -1);
    set_rt_env_var(env, SLOT_THIS, var_ref(RUN_ACTIV._this));
    set_rt_env_obj(env, SLOT_DOBJ, NOTHING);
    set_rt_env_obj(env, SLOT_IOBJ, NOTHING);
    set_rt_env_str(env, SLOT_DOBJSTR, str_dup(""));
    set_rt_env_str(env, SLOT_IOBJSTR, str_dup(""));
    set_rt_env_str(env, SLOT_ARGSTR, str_dup(argstr));
    set_rt_env_str(env, SLOT_PREPSTR, str_dup(""));
    set_rt_env_str(env, SLOT_VERB, str_ref(RUN_ACTIV.verb));
    set_rt_env_var(env, SLOT_ARGS, args);

    return do_task(program, MAIN_VECTOR, result, 1/*fg*/, do_db_tracebacks);
}

enum outcome
do_input_task(Objid user, Parsed_Command * pc, Objid recv, db_verb_handle vh)
{
    Program *prog = db_verb_program(vh);
    Var *env;

    check_activ_stack_size(current_max_stack_size());
    top_activ_stack = 0;

    RUN_ACTIV.rt_env = env = new_rt_env(prog->num_var_names);
    RUN_ACTIV._this = Var::new_obj(recv);
    RUN_ACTIV.player = user;
    RUN_ACTIV.progr = db_verb_owner(vh);
    RUN_ACTIV.recv = recv;
    RUN_ACTIV.vloc = var_ref(db_verb_definer(vh));
    RUN_ACTIV.verb = str_ref(pc->verb);
    RUN_ACTIV.verbname = str_ref(db_verb_names(vh));
    RUN_ACTIV.debug = (db_verb_flags(vh) & VF_DEBUG);
    RUN_ACTIV.threaded = DEFAULT_THREAD_MODE;
    fill_in_rt_consts(env, prog->version);
    set_rt_env_obj(env, SLOT_PLAYER, user);
    set_rt_env_obj(env, SLOT_CALLER, user);
    set_rt_env_var(env, SLOT_THIS, var_ref(RUN_ACTIV._this));
    set_rt_env_obj(env, SLOT_DOBJ, pc->dobj);
    set_rt_env_obj(env, SLOT_IOBJ, pc->iobj);
    set_rt_env_str(env, SLOT_DOBJSTR, str_ref(pc->dobjstr));
    set_rt_env_str(env, SLOT_IOBJSTR, str_ref(pc->iobjstr));
    set_rt_env_str(env, SLOT_ARGSTR, str_ref(pc->argstr));
    set_rt_env_str(env, SLOT_PREPSTR, str_ref(pc->prepstr));
    set_rt_env_str(env, SLOT_VERB, str_ref(pc->verb));
    set_rt_env_var(env, SLOT_ARGS, var_ref(pc->args));

    return do_task(prog, MAIN_VECTOR, nullptr, 1/*fg*/, 1/*traceback*/);
}

enum outcome
do_forked_task(Program * prog, Var * rt_env, activation a, int f_id)
{
    check_activ_stack_size(current_max_stack_size());
    top_activ_stack = 0;

    RUN_ACTIV = a;
    RUN_ACTIV.rt_env = rt_env;

    return do_task(prog, f_id, nullptr, 0/*bg*/, 1/*traceback*/);
}

/* this is called from bf_eval to set up stack for an eval call */

int
setup_activ_for_eval(Program * prog)
{
    Var *env;
    if (!push_activation())
        return 0;

    RUN_ACTIV.prog = prog;

    RUN_ACTIV.rt_env = env = new_rt_env(prog->num_var_names);
    fill_in_rt_consts(env, prog->version);
    set_rt_env_obj(env, SLOT_PLAYER, CALLER_ACTIV.player);
    set_rt_env_var(env, SLOT_CALLER, var_ref(CALLER_ACTIV._this));
    set_rt_env_obj(env, SLOT_THIS, NOTHING);
    set_rt_env_obj(env, SLOT_DOBJ, NOTHING);
    set_rt_env_obj(env, SLOT_IOBJ, NOTHING);
    set_rt_env_str(env, SLOT_DOBJSTR, str_dup(""));
    set_rt_env_str(env, SLOT_IOBJSTR, str_dup(""));
    set_rt_env_str(env, SLOT_ARGSTR, str_dup(""));
    set_rt_env_str(env, SLOT_PREPSTR, str_dup(""));
    set_rt_env_str(env, SLOT_VERB, str_dup(""));
    set_rt_env_var(env, SLOT_ARGS, new_list(0));

    RUN_ACTIV._this = var_ref(nothing);
    RUN_ACTIV.player = CALLER_ACTIV.player;
    RUN_ACTIV.progr = CALLER_ACTIV.progr;
    RUN_ACTIV.recv = NOTHING;
    RUN_ACTIV.vloc = var_ref(nothing);
    RUN_ACTIV.verb = str_dup("");
    RUN_ACTIV.verbname = str_dup("Input to EVAL");
    RUN_ACTIV.debug = 1;
    RUN_ACTIV.threaded = DEFAULT_THREAD_MODE;
    alloc_rt_stack(&RUN_ACTIV, RUN_ACTIV.prog->main_vector.max_stack);
    RUN_ACTIV.pc = 0;
    RUN_ACTIV.error_pc = 0;
    RUN_ACTIV.temp.type = TYPE_NONE;

    return 1;
}

/**** built in functions ****/

struct cf_state {
    unsigned fnum;
    void *data;
};

static package
bf_call_function(Var arglist, Byte next, void *vdata, Objid progr)
{
    package p;
    unsigned fnum;
    struct cf_state *s;

    if (next == 1) {        /* first call */
        const char *fname = arglist.v.list[1].v.str;

        fnum = number_func_by_name(fname);
        if (fnum == FUNC_NOT_FOUND) {
            p = make_raise_pack(E_INVARG, "Unknown built-in function",
                                var_ref(arglist.v.list[1]));
            free_var(arglist);
        } else {
            arglist = listdelete(arglist, 1);
            p = call_bi_func(fnum, arglist, next, progr, vdata);
        }
    } else {            /* return to function */
        s = (struct cf_state *)vdata;
        fnum = s->fnum;
        p = call_bi_func(fnum, arglist, next, progr, s->data);
        free_data(s);
    }

    if (p.kind == package::BI_CALL) {
        s = (struct cf_state *)alloc_data(sizeof(struct cf_state));
        s->fnum = fnum;
        s->data = p.u.call.data;
        p.u.call.data = s;
    }
    return p;
}

static void
bf_call_function_write(void *data)
{
    struct cf_state *s = (struct cf_state *)data;

    dbio_printf("bf_call_function data: fname = %s\n",
                name_func_by_num(s->fnum));
    write_bi_func_data(s->data, s->fnum);
}

static void *
bf_call_function_read(void)
{
    struct cf_state *s = (struct cf_state *)alloc_data(sizeof(struct cf_state));
    const char *line = dbio_read_string();
    const char *hdr = "bf_call_function data: fname = ";
    int hlen = strlen(hdr);

    if (!strncmp(line, hdr, hlen)) {
        line += hlen;
        if ((s->fnum = number_func_by_name(line)) == FUNC_NOT_FOUND)
            errlog("CALL_FUNCTION: Unknown built-in function: %s\n", line);
        else if (read_bi_func_data(s->fnum, &s->data, pc_for_bi_func_data()))
            return s;
    }
    return nullptr;
}

static package
bf_raise(Var arglist, Byte next, void *vdata, Objid progr)
{
    package p;
    int nargs = arglist.v.list[0].v.num;
    Var code = var_ref(arglist.v.list[1]);
    const char *msg = (nargs >= 2
                       ? str_ref(arglist.v.list[2].v.str)
                       : value2str(code));
    Var value;

    value = (nargs >= 3 ? var_ref(arglist.v.list[3]) : zero);
    free_var(arglist);
    p.kind = package::BI_RAISE;
    p.u.raise.code = code;
    p.u.raise.msg = msg;
    p.u.raise.value = value;

    return p;
}

static package
bf_suspend(Var arglist, Byte next, void *vdata, Objid progr)
{
    static double seconds, *secondsp = nullptr;
    int nargs = arglist.v.list[0].v.num;

    if (nargs >= 1) {
        seconds = arglist.v.list[1].type == TYPE_INT ?
                  arglist.v.list[1].v.num :
                  arglist.v.list[1].v.fnum;
        secondsp = &seconds;
    } else {
        secondsp = nullptr;
    }
    free_var(arglist);

    if (nargs >= 1 && seconds < 0)
        return make_error_pack(E_INVARG);
    else {
        return make_suspend_pack(enqueue_suspended_task, secondsp);
    }
}

/* An in-server replacement for the LambdaCore suspend_if_needed verb.
 * Arguments:
 *  time to suspend (default 0)
 *  ticks left before suspending (default 2000)
 *  seconds left before suspending (default 2)
 */
static package
bf_yield_if_needed(Var arglist, Byte next, void *vdata, Objid progr)
{
    static double seconds, *secondsp = nullptr;
    int min_ticks, min_seconds;
    int nargs = arglist.v.list[0].v.num;

    if (nargs >= 1)
        seconds = arglist.v.list[1].type == TYPE_INT ?
                  arglist.v.list[1].v.num :
                  arglist.v.list[1].v.fnum;
    else
        seconds = 0;

    secondsp = &seconds;

    min_ticks = (nargs >= 2 ? arglist.v.list[2].v.num : 2000);
    min_seconds = (nargs >= 3 ? arglist.v.list[3].v.num : 2);

    free_var(arglist);

    if (nargs >= 1 && (seconds < 0 || min_ticks <= 0 || min_seconds <= 0
                       || min_ticks >= server_int_option("fg_ticks", DEFAULT_FG_TICKS)
                       || min_seconds >= server_int_option("fg_seconds", DEFAULT_FG_SECONDS)))
        return make_error_pack(E_INVARG);

    if (ticks_remaining < min_ticks || timer_wakeup_interval(task_alarm_id) < min_seconds)
        return make_suspend_pack(enqueue_suspended_task, secondsp);
    else
        return no_var_pack();
}

static package
bf_read(Var arglist, Byte next, void *vdata, Objid progr)
{   /* ([object [, non_blocking]]) */
    int argc = arglist.v.list[0].v.num;
    static Objid connection;
    int non_blocking = (argc >= 2
                        && is_true(arglist.v.list[2]));

    if (argc >= 1)
        connection = arglist.v.list[1].v.obj;
    else
        connection = activ_stack[0].player;
    free_var(arglist);

    /* Permissions checking */
    if (argc >= 1) {
        if (!is_wizard(progr)
                && (!valid(connection)
                    || progr != db_object_owner(connection)))
            return make_error_pack(E_PERM);
    } else {
        if (!is_wizard(progr)
                || last_input_task_id(connection) != current_task_id)
            return make_error_pack(E_PERM);
    }

    if (non_blocking) {
        Var r;

        r = read_input_now(connection);
        if (r.type == TYPE_ERR)
            return make_error_pack(r.v.err);
        else
            return make_var_pack(r);
    }
    return make_suspend_pack(make_reading_task, &connection);
}

static package
bf_read_http(Var arglist, Byte next, void *vdata, Objid progr)
{   /* ("request" | "response" [, object]) */
    int argc = arglist.v.list[0].v.num;
    static Objid connection;
    int request;

    if (!strcasecmp(arglist.v.list[1].v.str, "request"))
        request = 1;
    else if (!strcasecmp(arglist.v.list[1].v.str, "response"))
        request = 0;
    else {
        free_var(arglist);
        return make_error_pack(E_INVARG);
    }

    if (argc > 1)
        connection = arglist.v.list[2].v.obj;
    else
        connection = activ_stack[0].player;

    free_var(arglist);

    /* Permissions checking */
    if (argc > 1) {
        if (!is_wizard(progr)
                && (!valid(connection)
                    || progr != db_object_owner(connection)))
            return make_error_pack(E_PERM);
    } else {
        if (!is_wizard(progr)
                || last_input_task_id(connection) != current_task_id)
            return make_error_pack(E_PERM);
    }

    return make_suspend_pack(request ? make_parsing_http_request_task
                             : make_parsing_http_response_task,
                             &connection);
}

static package
bf_seconds_left(Var arglist, Byte next, void *vdata, Objid progr)
{
    Var r;
    r.type = TYPE_INT;
    r.v.num = timer_wakeup_interval(task_alarm_id);
    free_var(arglist);
    return make_var_pack(r);
}

static package
bf_ticks_left(Var arglist, Byte next, void *vdata, Objid progr)
{
    Var r;
    r.type = TYPE_INT;
    r.v.num = ticks_remaining;
    free_var(arglist);
    return make_var_pack(r);
}

static package
bf_pass(Var arglist, Byte next, void *vdata, Objid progr)
{
    enum error e = call_verb2(RUN_ACTIV.recv, RUN_ACTIV.verb, RUN_ACTIV._this, arglist, 1, DEFAULT_THREAD_MODE);

    if (e == E_NONE)
        return tail_call_pack();

    free_var(arglist);

    if (e == E_VERBNF)
        return make_x_not_found_pack(e, RUN_ACTIV.verb, RUN_ACTIV.recv);
    else
        return make_error_pack(e);
}

static package
bf_set_task_perms(Var arglist, Byte next, void *vdata, Objid progr)
{   /* (player) */
    /* warning!!  modifies top activation */
    Objid oid = arglist.v.list[1].v.obj;

    free_var(arglist);

    if (progr != oid && !is_wizard(progr))
        return make_error_pack(E_PERM);

    RUN_ACTIV.progr = oid;
    return no_var_pack();
}

static package
bf_task_perms(Var arglist, Byte next, void *vdata, Objid progr)
{   /* () */
    Var r;
    r.type = TYPE_OBJ;
    r.v.obj = RUN_ACTIV.progr;
    free_var(arglist);
    return make_var_pack(r);
}

static package
bf_caller_perms(Var arglist, Byte next, void *vdata, Objid progr)
{   /* () */
    Var r;
    r.type = TYPE_OBJ;
    if (top_activ_stack == 0)
        r.v.obj = NOTHING;
    else
        r.v.obj = activ_stack[top_activ_stack - 1].progr;
    free_var(arglist);
    return make_var_pack(r);
}

static package
bf_callers(Var arglist, Byte next, void *vdata, Objid progr)
{
    int line_numbers_too = 0;

    if (arglist.v.list[0].v.num >= 1)
        line_numbers_too = is_true(arglist.v.list[1]);
    free_var(arglist);

    return make_var_pack(make_stack_list(activ_stack, 0, top_activ_stack, 0,
                                         root_activ_vector, line_numbers_too, 0,
                                         progr));
}

static package
bf_task_stack(Var arglist, Byte next, void *vdata, Objid progr)
{
    int nargs = arglist.v.list[0].v.num;
    int id = arglist.v.list[1].v.num;
    int line_numbers_too = (nargs >= 2 && is_true(arglist.v.list[2]));
    int stack_vars = (nargs >= 3 && is_true(arglist.v.list[3]));
    vm the_vm = find_suspended_task(id);
    Objid owner = (the_vm ? progr_of_cur_verb(the_vm) : NOTHING);

    free_var(arglist);
    if (!the_vm)
        return make_error_pack(E_INVARG);
    if (!is_wizard(progr) && progr != owner)
        return make_error_pack(E_PERM);

    return make_var_pack(make_stack_list(the_vm->activ_stack, 0,
                                         the_vm->top_activ_stack, 1,
                                         the_vm->root_activ_vector,
                                         line_numbers_too,
                                         stack_vars,
                                         progr));
}

void
register_execute(void)
{
    register_function_with_read_write("call_function", 1, -1, bf_call_function,
                                      bf_call_function_read,
                                      bf_call_function_write,
                                      TYPE_STR);
    register_function("raise", 1, 3, bf_raise, TYPE_ANY, TYPE_STR, TYPE_ANY);
    register_function("suspend", 0, 1, bf_suspend, TYPE_NUMERIC);
    register_function("yin", 0, 3, bf_yield_if_needed, TYPE_NUMERIC, TYPE_INT, TYPE_INT);
    register_function("read", 0, 2, bf_read, TYPE_OBJ, TYPE_ANY);
    register_function("read_http", 1, 2, bf_read_http, TYPE_STR, TYPE_OBJ);

    register_function("seconds_left", 0, 0, bf_seconds_left);
    register_function("ticks_left", 0, 0, bf_ticks_left);
    register_function("pass", 0, -1, bf_pass);
    register_function("set_task_perms", 1, 1, bf_set_task_perms, TYPE_OBJ);
    register_function("task_perms", 0, 0, bf_task_perms);
    register_function("caller_perms", 0, 0, bf_caller_perms);
    register_function("callers", 0, 1, bf_callers, TYPE_ANY);
    register_function("task_stack", 1, 3, bf_task_stack, TYPE_INT, TYPE_ANY, TYPE_ANY);

#ifdef WAIF_DICT
    waif_index_verb = str_dup(WAIF_INDEX_VERB);
    waif_indexset_verb = str_dup(WAIF_INDEXSET_VERB);
#endif              /* WAIF_DICT */

#ifdef SAVE_FINISHED_TASKS
    map_this = str_dup_to_var("this");
    map_player = str_dup_to_var("player");
    map_programmer = str_dup_to_var("programmer");
    map_receiver = str_dup_to_var("receiver");
    map_object = str_dup_to_var("object");
    map_verb = str_dup_to_var("verb");
    map_fullverb = str_dup_to_var("fullverb");
    map_foreground = str_dup_to_var("foreground");
    map_suspended = str_dup_to_var("suspended");
    map_time = str_dup_to_var("time");
#endif          /* SAVE_FINISHED_TASKS */
}


/**** storing to/loading from database ****/

void
write_activ_as_pi(activation a)
{
    Var dummy;

    dummy.type = TYPE_INT;
    dummy.v.num = -111;
    dbio_write_var(dummy);

    dbio_write_var(a._this);
    dbio_write_var(a.vloc);
    dbio_write_num(a.threaded); // Apparently adding things to the beginning is easier for backward compatibility...
    dbio_printf("%" PRIdN " -7 -8 %" PRIdN " -9 %" PRIdN " %" PRIdN " -10 %d\n", a.recv, a.player, a.progr, a.vloc, a.debug);
    dbio_write_string("No");
    dbio_write_string("More");
    dbio_write_string("Parse");
    dbio_write_string("Infos");
    dbio_write_string(a.verb);
    dbio_write_string(a.verbname);
}

int
read_activ_as_pi(activation * a)
{
    Objid dummy, vloc_oid;
    char c;

    free_var(dbio_read_var());

    Var _this, vloc;
    if (dbio_input_version >= DBV_This)
        _this = dbio_read_var();
    if (dbio_input_version >= DBV_Anon)
        vloc = dbio_read_var();
    if (dbio_input_version >= DBV_Threaded)
        a->threaded = dbio_read_num();
    else
        a->threaded = DEFAULT_THREAD_MODE;

    /* I use a `dummy' variable here and elsewhere instead of the `*'
     * assignment-suppression syntax of `scanf' because it allows more
     * straightforward error checking; unfortunately, the standard says that
     * suppressed assignments are not counted in determining the returned value
     * of `scanf'...
     */
    if (dbio_scanf("%" SCNdN " %" SCNdN " %" SCNdN " %" SCNdN " %" SCNdN " %" SCNdN " %" SCNdN " %" SCNdN " %d%c",
                   &a->recv, &dummy, &dummy, &a->player, &dummy, &a->progr,
                   &vloc_oid, &dummy, &a->debug, &c) != 10
            || c != '\n') {
        if (dbio_input_version >= DBV_This)
            free_var(_this);
        if (dbio_input_version >= DBV_Anon)
            free_var(vloc);
        errlog("READ_A: Bad numbers.\n");
        return 0;
    }

    /* Earlier versions of the database use `recv' for `this' and
     * write `vloc' as an object number.
     */
    if (dbio_input_version < DBV_This)
        a->_this = Var::new_obj(a->recv);
    else
        a->_this = _this;
    if (dbio_input_version < DBV_Anon)
        a->vloc = Var::new_obj(vloc_oid);
    else
        a->vloc = vloc;

    dbio_read_string();     /* was argstr */
    dbio_read_string();     /* was dobjstr */
    dbio_read_string();     /* was iobjstr */
    dbio_read_string();     /* was prepstr */
    a->verb = dbio_read_string_intern();
    a->verbname = dbio_read_string_intern();
    return 1;
}

void
write_rt_env(const char **var_names, Var * rt_env, unsigned size)
{
    unsigned i;

    dbio_printf("%d variables\n", size);
    for (i = 0; i < size; i++) {
        dbio_write_string(var_names[i]);
        dbio_write_var(rt_env[i]);
    }
}

int
read_rt_env(const char ***old_names, Var ** rt_env, int *old_size)
{
    unsigned i;

    if (dbio_scanf("%d variables\n", old_size) != 1) {
        errlog("READ_RT_ENV: Bad count.\n");
        return 0;
    }
    *old_names = (const char **) mymalloc((*old_size) * sizeof(char *),
                                          M_NAMES);
    *rt_env = new_rt_env(*old_size);

    for (i = 0; i < *old_size; i++) {
        (*old_names)[i] = dbio_read_string_intern();
        (*rt_env)[i] = dbio_read_var();
    }
    return 1;
}

Var *
reorder_rt_env(Var * old_rt_env, const char **old_names,
               int old_size, Program * prog)
{
    /* reorder old_rt_env, which is aligned according to old_names,
       to align to prog->var_names -- return the new rt_env
       after freeing old_rt_env and old_names */
    /* while the database is being loaded, a rt_env may directly or indirectly
       reference yet unloaded anonymous objects.  defer freeing these until
       loading is complete. see `free_reordered_rt_env_values()' */

    unsigned size = prog->num_var_names;
    Var *rt_env = new_rt_env(size);

    unsigned i;

    for (i = 0; i < size; i++) {
        int slot;

        for (slot = 0; slot < old_size; slot++) {
            if (strcasecmp(old_names[slot], prog->var_names[i]) == 0)
                break;
        }

        if (slot < old_size)
            rt_env[i] = var_ref(old_rt_env[slot]);
    }

    for (i = 0; i < old_size; i++) {
        temp_vars = listappend(temp_vars, old_rt_env[i]);
        old_rt_env[i] = var_ref(none);
    }

    free_rt_env(old_rt_env, old_size);
    for (i = 0; i < old_size; i++)
        free_str(old_names[i]);
    myfree((void *) old_names, M_NAMES);

    return rt_env;
}

void
free_reordered_rt_env_values(void)
{
    free_var(temp_vars);
}

void
write_activ(activation a)
{
    Var *v;

    dbio_printf("language version %u\n", a.prog->version);
    dbio_write_program(a.prog);
    write_rt_env(a.prog->var_names, a.rt_env, a.prog->num_var_names);

    dbio_printf("%d rt_stack slots in use\n",
                a.top_rt_stack - a.base_rt_stack);

    for (v = a.base_rt_stack; v != a.top_rt_stack; v++)
        dbio_write_var(*v);

    write_activ_as_pi(a);
    dbio_write_var(a.temp);

    dbio_printf("%u %u %u\n", a.pc, a.bi_func_pc, a.error_pc);
    if (a.bi_func_pc != 0) {
        dbio_write_string(name_func_by_num(a.bi_func_id));
        write_bi_func_data(a.bi_func_data, a.bi_func_id);
    }
}

static int
check_pc_validity(Program * prog, int which_vector, unsigned pc)
{
    Bytecodes *bc = (which_vector == -1
                     ? &prog->main_vector
                     : &prog->fork_vectors[which_vector]);

    /* Current insn must be call to verb or built-in function like eval(),
     * move(), pass(), or suspend().
     */
    return (pc < bc->size
            && (bc->vector[pc - 1] == OP_CALL_VERB
                || bc->vector[pc - 2] == OP_BI_FUNC_CALL));
}

int
read_activ(activation * a, int which_vector)
{
    DB_Version version;
    unsigned int v;
    Var *old_rt_env;
    const char **old_names;
    int old_size, stack_in_use;
    unsigned i;
    const char *func_name;
    int max_stack;
    char c;

    if (dbio_input_version < DBV_Float)
        version = dbio_input_version;
    else if (dbio_scanf("language version %u\n", &v) != 1) {
        errlog("READ_ACTIV: Malformed language version\n");
        return 0;
    } else if (version = (DB_Version)v, !check_db_version(version)) {
        errlog("READ_ACTIV: Unrecognized language version: %d\n",
               version);
        return 0;
    }
    if (!(a->prog = dbio_read_program(version,
                                      nullptr, (void *) "suspended task"))) {
        errlog("READ_ACTIV: Malformed program\n");
        return 0;
    }
    if (!read_rt_env(&old_names, &old_rt_env, &old_size)) {
        errlog("READ_ACTIV: Malformed runtime environment\n");
        return 0;
    }
    a->rt_env = reorder_rt_env(old_rt_env, old_names, old_size, a->prog);

    max_stack = (which_vector == MAIN_VECTOR
                 ? a->prog->main_vector.max_stack
                 : a->prog->fork_vectors[which_vector].max_stack);
    alloc_rt_stack(a, max_stack);

    if (dbio_scanf("%d rt_stack slots in use\n", &stack_in_use) != 1) {
        errlog("READ_ACTIV: Bad stack_in_use number\n");
        return 0;
    }
    a->top_rt_stack = a->base_rt_stack;
    for (i = 0; i < stack_in_use; i++)
        *(a->top_rt_stack++) = dbio_read_var();

    if (!read_activ_as_pi(a)) {
        errlog("READ_ACTIV: Bad activ. stack_in_use = %d\n", stack_in_use);
        return 0;
    }
    a->temp = dbio_read_var();

    if (dbio_scanf("%u %u%c", &a->pc, &i, &c) != 3) {
        errlog("READ_ACTIV: bad pc, next. stack_in_use = %d\n", stack_in_use);
        return 0;
    }
    a->bi_func_pc = i;

    if (c == '\n')
        a->error_pc = a->pc;
    else if (dbio_scanf("%u\n", &a->error_pc) != 1) {
        errlog("READ_ACTIV: no error pc.\n");
        return 0;
    }
    if (!check_pc_validity(a->prog, which_vector, a->pc)) {
        errlog("READ_ACTIV: Bad PC for suspended task.\n");
        return 0;
    }
    if (a->bi_func_pc != 0) {
        func_name = dbio_read_string();
        if ((i = number_func_by_name(func_name)) == FUNC_NOT_FOUND) {
            errlog("READ_ACTIV: Unknown built-in function `%s'\n", func_name);
            return 0;
        }
        a->bi_func_id = i;
        if (!read_bi_func_data(a->bi_func_id, &a->bi_func_data,
                               &a->bi_func_pc)) {
            errlog("READ_ACTIV: Bad saved state for built-in function `%s'\n",
                   func_name);
            return 0;
        }
    }
    return 1;
}

bool get_thread_mode()
{
    return RUN_ACTIV.threaded;
}

void set_thread_mode(bool mode)
{
    RUN_ACTIV.threaded = mode;
}

/* Create a type mismatch traceback and try-except return value.
   The first argument is the mismatched type that was received.
   The remaining arguments are the types that were expected. */
char*
type_mismatch_string(int n_args, ...)
{
    static Stream *error_msg = nullptr;
    if (error_msg == nullptr)
        error_msg = new_stream(40);

    stream_printf(error_msg, "%s (expected", unparse_error(E_TYPE));
    var_type the_var = TYPE_CLEAR;
    va_list args;
    va_start(args, n_args);

    var_type mismatch = (var_type)va_arg(args, int);

    for (int x = 1; x <= n_args; x++) {
        var_type the_var = (var_type)va_arg(args, int);
        stream_printf(error_msg, " %s,", parse_type((var_type)the_var));
        if (x == n_args - 1) {
            stream_delete_char(error_msg);
            stream_add_string(error_msg, " or");
        }
    }
    va_end(args);

    stream_delete_char(error_msg); // delete the comma
    stream_printf(error_msg, "; got %s)", parse_type(mismatch));

    return str_dup(reset_stream(error_msg));
}

Var
type_mismatch_value(int n_args, ...)
{
    /* Since -d verbs can't use the value anyway,
       we won't waste time and money setting it up. */
    if (!RUN_ACTIV.debug)
        return var_ref(zero);

    Var value_var = new_list(2);
    Var expected_types = new_list(n_args);
    va_list args;
    va_start(args, n_args);

    var_type mismatch = (var_type)va_arg(args, int);

    for (int x = 1; x <= n_args; x++) {
        var_type the_var = (var_type)va_arg(args, int);
        expected_types.v.list[x] = Var::new_int(the_var);
    }
    va_end(args);

    value_var.v.list[1] = expected_types;
    value_var.v.list[2] = Var::new_int(mismatch & TYPE_DB_MASK);

    return value_var;
}
