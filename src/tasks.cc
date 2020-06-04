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

#include <sys/socket.h>
#include <stdlib.h>
#include <sys/time.h>
#include <math.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "db.h"
#include "db_io.h"
#include "decompile.h"
#include "eval_env.h"
#include "eval_vm.h"
#include "execute.h"
#include "functions.h"
#include "http_parser.h"
#include "list.h"
#include "log.h"
#include "map.h"
#include "match.h"
#include "options.h"
#include "parse_cmd.h"
#include "parser.h"
#include "random.h"
#include "server.h"
#include "storage.h"
#include "streams.h"
#include "structures.h"
#include "tasks.h"
#include "utils.h"
#include "verbs.h"
#include "version.h"
#include "background.h"
#include "network.h"

#define ROUND(tvp)  ((tvp)->tv_sec + ((tvp)->tv_usec > 500000))

typedef enum {
    /* Input Tasks */
    TASK_INBAND,    /* vanilla in-band */
    TASK_OOB,       /* out-of-band unless disable_oob */
    TASK_QUOTED,    /* in-band; needs unquote unless disable-oob */
    TASK_BINARY,    /* in-band; binary mode string */
    /* Background Tasks */
    TASK_FORKED,
    TASK_SUSPENDED,
} task_kind;

typedef struct forked_task {
    int id;
    Program *program;
    activation a;
    Var *rt_env;
    int f_index;
    struct timeval start_tv;
} forked_task;

typedef struct suspended_task {
    vm the_vm;
    struct timeval start_tv;
    Var value;
} suspended_task;

typedef struct {
    char *string;
    int length;
    struct task *next_itail;    /* see tqueue.first_itail */
} input_task;

typedef struct task {
    struct task *next;
    task_kind kind;
    union {
        input_task input;
        forked_task forked;
        suspended_task suspended;
    } t;
} task;

#define GET_START_TIME(ttt) \
    (ttt->kind == TASK_FORKED \
     ? &ttt->t.forked.start_tv \
     : &ttt->t.suspended.start_tv)

static inline struct timeval
double_to_start_tv(double after_seconds)
{
    struct timeval now, delta, when;

    gettimeofday(&now, nullptr);
    delta.tv_sec = floor(after_seconds);
    delta.tv_usec = 1000000. * (after_seconds - delta.tv_sec);
    timeradd(&now, &delta, &when);
    return when;
}

enum icmd_flag {
    /* fix icmd_index() if you change any of the following numbers: */
    ICMD_SUFFIX       = 1,
    ICMD_OUTPUTSUFFIX = 2,
    ICMD_OUTPUTPREFIX = 3,
    ICMD_PREFIX       = 4,
    ICMD_PROGRAM      = 5,  /* .program */
    /* mask */
    ICMD_ALL_CMDS = ((1 << (ICMD_PROGRAM + 1)) - 2)
};

enum parsing_status {
    READY = 1,
    PARSING = 2,
    DONE = 3
};

struct http_parsing_state {
    struct http_parser parser;
    enum parsing_status status;
    Var uri;
    Var headers;
    Var header_field_under_constr;
    Var header_value_under_constr;
    Var body;
    Var result;
};

typedef struct tqueue {
    /*
     * A task queue can be in one of four possible states, depending upon the
     * values of its `player' and `connected' slots.
     *
     * When people first connect to the MOO, they are assigned negative player
     * object numbers.  Once they log in (either by creating a new player
     * object or by connecting to an existing one), the connection is changed
     * to refer to the appropriate, non-negative, player number.
     *
     * Input tasks for logged-in players (with non-negative `player' slots) are
     * interpreted as normal MOO commands.  Those for anonymous players (with
     * negative `player' slots) are treated specially: they are passed to a
     * particular verb on the system object.  If that verb returns a valid
     * player object number, then that is used as the new player number for the
     * connection.  (Note that none of this applies to tasks that are being
     * read() or that are being handled as out-of-band commands.)
     *
     * The `connected' field is true iff this queue has an associated open
     * network connection.  Queues without such connections may nonetheless
     * contain input tasks, either typed ahead before disconnection or else
     * entered via the `force_input()' function.
     *
     * If an unconnected queue becomes empty, it is destroyed.
     */
    struct tqueue *next, **prev;    /* prev only valid on idle_tqueues */
    Objid player;
    Objid handler;
    int connected;
    task *first_input, **last_input;
    task *first_itail, **last_itail;
    /* The input queue alternates between contiguous sequences of TASK_OOBs
     * and sequences of non-TASK_OOBs; the "itail queue" is the queue of all
     * sequence-ending tasks threaded along the next_itail pointers.
     * first_itail is null iff first_input is null
     * When the queue is nonempty,
     *   last_itail points to the penultimate .next_itail pointer slot
     *   unlike last_input which always points to the final (null)
     *   .next pointer slot
     * For tasks not at the end of a sequence,
     *   the next_itail field is ignored and may be garbage.
     */
    int total_input_length;
    int last_input_task_id;
    int input_suspended;

    task *first_bg, **last_bg;
    int usage;          /* a kind of inverted priority */
    int num_bg_tasks;       /* in either here or waiting_tasks */
    char *output_prefix, *output_suffix;
    const char *flush_cmd;

    /* Used in emergency mode and when handling the `.program'
     * intrinsic command.  `program_object' _could_ be changed to hold
     * a reference to either a permanent or anonymous object, however
     * the reader only handles object numbers, so for now we leave
     * it as an `Objid'.
     */
    Stream *program_stream;
    Objid program_object;
    const char *program_verb;

    /* booleans */
    int hold_input: 1;      /* input tasks must wait for read() */
    int disable_oob: 1;     /* treat all input lines as inband */
    int reading: 1;     /* some task is blocked on read() */
    int parsing: 1;     /* some task is blocked on read_http() */
    int icmds: 8;       /* which of .program/PREFIX/... are enabled */

    /* Once a `http_parsing_state' is allocated and assigned to a task
     * queue, it is not freed until the task queue is freed -- the
     * theory being that once a connection is used for HTTP it's
     * likely to be reused for HTTP.
     */
    struct http_parsing_state *parsing_state;

    vm reading_vm;
} tqueue;

typedef struct ext_queue {
    struct ext_queue *next;
    task_enumerator enumerator;
} ext_queue;

#define INPUT_HIWAT MAX_QUEUED_INPUT
#define INPUT_LOWAT (INPUT_HIWAT / 2)

#define NO_USAGE    -1

Var current_local;
int current_task_id;
static tqueue *idle_tqueues = nullptr, *active_tqueues = nullptr;
static task *waiting_tasks = nullptr;   /* forked and suspended tasks */
static ext_queue *external_queues = nullptr;
#ifdef SAVE_FINISHED_TASKS
Var finished_tasks = new_list(0);
#endif

/*
 * Forward declarations for functions that operate on external queues.
 */
struct qcl_data {
    Objid progr;
    int show_all;
    int i;
    Var tasks;
};

static task_enum_action
counting_closure(vm the_vm, const char *status, void *data);

static task_enum_action
listing_closure(vm the_vm, const char *status, void *data);

static task_enum_action
writing_closure(vm the_vm, const char *status, void *data);

/*
 *  ICMD_FOR_EACH(DEFINE,verb)
 *   expands to a table of intrinsic commands,
 *   each entry of the form
 *
 *  DEFINE(ICMD_NAME,<name>,<matcher>)  where
 *      ICMD_NAME == enumeration constant name to use
 *      <name>    == full verbname
 *      <matcher>(verb) -> true iff verb matches <name>
 */
#define __IDLM(DEFINE,DELIMITER,verb)           \
    DEFINE(ICMD_##DELIMITER,  DELIMITER,      \
           (strcmp(verb, #DELIMITER) == 0))       \

#define ICMD_FOR_EACH(DEFINE,verb)          \
    DEFINE(ICMD_PROGRAM, .program,            \
           (verbcasecmp(".pr*ogram", (verb))))    \
    __IDLM(DEFINE,PREFIX,      (verb))        \
    __IDLM(DEFINE,SUFFIX,      (verb))        \
    __IDLM(DEFINE,OUTPUTPREFIX,(verb))        \
    __IDLM(DEFINE,OUTPUTSUFFIX,(verb))        \

static int
icmd_index(const char * verb) {
    /* evil, poor-man's minimal perfect hash */
    int len = strlen(verb);
    char c2 = len > 2 ? verb[2] : 0;
    char c8 = len > 8 ? verb[8] : 0;
    switch (((c2 & 7) ^ 6) + !(c8 & 2)) {
        default:
            break;
#define _ICMD_IX(ICMD_PREFIX,_,MATCH)       \
case ICMD_PREFIX:           \
    if (MATCH) return ICMD_PREFIX;  \
    break;              \

            ICMD_FOR_EACH(_ICMD_IX, verb);
    }
    return 0;
}
#undef _ICMD_IX

static Var
icmd_list(int icmd_flags)
{
    Var s;
    Var list = new_list(0);
    s.type = TYPE_STR;
#define _ICMD_MKSTR(ICMD_PREFIX,PREFIX,_)   \
    if (icmd_flags & (1<<ICMD_PREFIX)) {    \
        s.v.str = str_dup(#PREFIX);     \
        list = listappend(list, s);     \
    }                   \

    ICMD_FOR_EACH(_ICMD_MKSTR, @);
    return list;
}
#undef _ICMD_MKSTR

static int
icmd_set_flags(tqueue * tq, Var list)
{
    int i;
    int newflags;
    if (list.type == TYPE_INT) {
        newflags = is_true(list) ? ICMD_ALL_CMDS : 0;
    }
    else if (list.type != TYPE_LIST)
        return 0;
    else {
        newflags = 0;
        for (i = 1; i <= list.v.list[0].v.num; ++i) {
            int icmd;
            if (list.v.list[i].type != TYPE_STR)
                return 0;
            icmd = icmd_index(list.v.list[i].v.str);
            if (!icmd)
                return 0;
            newflags |= (1 << icmd);
        }
    }
    tq->icmds = newflags;
    return 1;
}

static void
init_http_parsing_state(struct http_parsing_state *state)
{
    state->status = READY;
#define INIT_VAR(XX)        \
    {               \
        (XX).type = TYPE_NONE;  \
    }
    INIT_VAR(state->uri);
    INIT_VAR(state->header_field_under_constr);
    INIT_VAR(state->header_value_under_constr);
    INIT_VAR(state->headers);
    INIT_VAR(state->body);
    INIT_VAR(state->result);
#undef INIT_VAR
}

static void
reset_http_parsing_state(struct http_parsing_state *state)
{
    state->status = READY;
#define RESET_VAR(XX)       \
    {               \
        free_var(XX);       \
        (XX).type = TYPE_NONE;  \
    }
    RESET_VAR(state->uri);
    RESET_VAR(state->header_field_under_constr);
    RESET_VAR(state->header_value_under_constr);
    RESET_VAR(state->headers);
    RESET_VAR(state->body);
    RESET_VAR(state->result);
#undef RESET_VAR
}

static void
deactivate_tqueue(tqueue * tq)
{
    tq->usage = NO_USAGE;

    tq->next = idle_tqueues;
    tq->prev = &idle_tqueues;
    if (idle_tqueues)
        idle_tqueues->prev = &(tq->next);
    idle_tqueues = tq;
}

static void
activate_tqueue(tqueue * tq)
{
    tqueue **qq = &active_tqueues;

    while (*qq && (*qq)->usage <= tq->usage)
        qq = &((*qq)->next);

    tq->next = *qq;
    tq->prev = nullptr;
    *qq = tq;
}

static void
ensure_usage(tqueue * tq)
{
    if (tq->usage == NO_USAGE) {
        tq->usage = active_tqueues ? active_tqueues->usage : 0;

        /* Remove tq from idle_tqueues... */
        *(tq->prev) = tq->next;
        if (tq->next)
            tq->next->prev = tq->prev;

        /* ...and put it on active_tqueues. */
        activate_tqueue(tq);
    }
}

char *
default_flush_command(void)
{
    const char *str = server_string_option("default_flush_command", ".flush");

    return (str && str[0] != '\0') ? str_dup(str) : nullptr;
}

static tqueue *
find_tqueue(Objid player, int create_if_not_found)
{
    tqueue *tq;

    for (tq = active_tqueues; tq; tq = tq->next)
        if (tq->player == player)
            return tq;

    for (tq = idle_tqueues; tq; tq = tq->next)
        if (tq->player == player)
            return tq;

    if (!create_if_not_found)
        return nullptr;

    tq = (tqueue *)mymalloc(sizeof(tqueue), M_TASK);

    deactivate_tqueue(tq);

    tq->player = player;
    tq->handler = 0;
    tq->connected = 0;

    tq->first_input = tq->first_itail = tq->first_bg = nullptr;
    tq->last_input = &(tq->first_input);
    tq->last_itail = &(tq->first_itail);
    tq->last_bg = &(tq->first_bg);
    tq->total_input_length = tq->input_suspended = 0;

    tq->output_prefix = tq->output_suffix = nullptr;
    tq->flush_cmd = default_flush_command();
    tq->program_stream = nullptr;

    tq->reading = 0;
    tq->parsing = 0;
    tq->hold_input = 0;
    tq->disable_oob = 0;
    tq->icmds = ICMD_ALL_CMDS;
    tq->num_bg_tasks = 0;
    tq->last_input_task_id = 0;

    tq->parsing_state = nullptr;

    return tq;
}

static void
free_tqueue(tqueue * tq)
{
    /* Precondition: tq is on idle_tqueues */

    if (tq->output_prefix)
        free_str(tq->output_prefix);
    if (tq->output_suffix)
        free_str(tq->output_suffix);
    if (tq->flush_cmd)
        free_str(tq->flush_cmd);
    if (tq->program_stream)
        free_stream(tq->program_stream);
    if (tq->reading)
        free_vm(tq->reading_vm, 1);
    if (tq->parsing_state) {
        reset_http_parsing_state(tq->parsing_state);
        myfree(tq->parsing_state, M_STRUCT);
    }

    *(tq->prev) = tq->next;
    if (tq->next)
        tq->next->prev = tq->prev;

    myfree(tq, M_TASK);
}

static void
enqueue_bg_task(tqueue * tq, task * t)
{
    *(tq->last_bg) = t;
    tq->last_bg = &(t->next);
    t->next = nullptr;
}

static task *
dequeue_bg_task(tqueue * tq)
{
    task *t = tq->first_bg;

    if (t) {
        tq->first_bg = t->next;
        if (t->next == nullptr)
            tq->last_bg = &(tq->first_bg);
        else
            t->next = nullptr;
        tq->num_bg_tasks--;
    }
    return t;
}

static char oob_quote_prefix[] = OUT_OF_BAND_QUOTE_PREFIX;
#define oob_quote_prefix_length (sizeof(oob_quote_prefix) - 1)

enum dequeue_how { DQ_FIRST = -1, DQ_OOB = 0, DQ_INBAND = 1 };

static task *
dequeue_input_task(tqueue * tq, enum dequeue_how how)
{
    task *t;
    task **pt, **pitail;

    if (tq->disable_oob) {
        if (how == DQ_OOB)
            return nullptr;
        how = DQ_FIRST;
    }

    if (!tq->first_input)
        return nullptr;
    else if (how == (tq->first_input->kind == TASK_OOB)) {
        pt     = &(tq->first_itail->next);
        pitail = &(tq->first_itail->t.input.next_itail);
    }
    else {
        pt     = &(tq->first_input);
        pitail = &(tq->first_itail);
    }
    t = *pt;

    if (t) {
        *pt = t->next;
        if (t->next == nullptr)
            tq->last_input = pt;
        else
            t->next = nullptr;

        if (t == *pitail) {
            *pitail = nullptr;
            if (t->t.input.next_itail) {
                tq->first_itail = t->t.input.next_itail;
                t->t.input.next_itail = nullptr;
            }
            if (*(tq->last_itail) == nullptr)
                tq->last_itail = &(tq->first_itail);
        }

        tq->total_input_length -= t->t.input.length;
        if (tq->input_suspended
                && tq->connected
                && tq->total_input_length < INPUT_LOWAT) {
            server_resume_input(tq->player);
            tq->input_suspended = 0;
        }

        if (t->kind == TASK_OOB) {
            if (tq->disable_oob)
                t->kind = TASK_INBAND;
        }
        else if (t->kind == TASK_QUOTED) {
            if (!tq->disable_oob)
                memmove(t->t.input.string,
                        t->t.input.string + oob_quote_prefix_length,
                        1 + strlen(t->t.input.string + oob_quote_prefix_length));
            t->kind = TASK_INBAND;
        }
    }
    return t;
}

static void
free_task(task * t, int strong)
{   /* for FORKED tasks, strong == 1 means free the rt_env also.
       for SUSPENDED tasks, strong == 1 means free the vm also. */
    switch (t->kind) {
        default:
            panic_moo("Unknown task kind in free_task()");
            break;
        case TASK_BINARY:
        case TASK_INBAND:
        case TASK_QUOTED:
        case TASK_OOB:
            free_str(t->t.input.string);
            break;
        case TASK_FORKED:
            if (strong) {
                free_rt_env(t->t.forked.rt_env,
                            t->t.forked.program->num_var_names);
                free_str(t->t.forked.a.verb);
                free_str(t->t.forked.a.verbname);
            }
            free_program(t->t.forked.program);
            break;
        case TASK_SUSPENDED:
            if (strong)
                free_vm(t->t.suspended.the_vm, 1);
            break;
    }
    myfree(t, M_TASK);
}

static int
new_task_id(void)
{
    int i;

    do {
        i = RANDOM();
    } while (i == 0);

    return i;
}

static void
start_programming(tqueue * tq, char *argstr)
{
    db_verb_handle h;
    const char *message, *vname;

    /* `find_verb_for_programming' only finds verbs on permanent
     * objects (objects identified in the command by object number
     * literals), thus the assumption below that `db_verb_definer'
     * will return a reference to a permanent object.
     */
    h = find_verb_for_programming(tq->player, argstr, &message, &vname);
    notify(tq->player, message);

    if (h.ptr) {
        tq->program_stream = new_stream(100);
        tq->program_object = db_verb_definer(h).v.obj;
        tq->program_verb = str_dup(vname);
#ifdef LOG_CODE_CHANGES
        oklog("CODE_CHANGE: %s (#%" PRIdN ") set verb #%" PRIdN ":%s via .PROGRAM\n", db_object_name(tq->player), tq->player, tq->program_object, tq->program_verb);
#endif
    }
}

struct task_state {
    Objid player;
    int nerrors;
    char *input;
};

static void
my_error(void *data, const char *msg)
{
    struct task_state *s = (task_state *)data;

    notify(s->player, msg);
    s->nerrors++;
}

static int
my_getc(void *data)
{
    struct task_state *s = (task_state *)data;

    if (*(s->input) != '\0')
        return *(s->input++);
    else
        return EOF;
}

static Parser_Client client =
{my_error, nullptr, my_getc};

static void
end_programming(tqueue * tq)
{
    Objid player = tq->player;

    if (!valid(tq->program_object))
        notify(player, "That object appears to have disappeared ...");
    else {
        db_verb_handle h;
        Var desc;

        desc.type = TYPE_STR;
        desc.v.str = tq->program_verb;
        h = find_described_verb(Var::new_obj(tq->program_object), desc);

        if (!h.ptr)
            notify(player, "That verb appears to have disappeared ...");
        else {
            struct task_state s;
            Program *program;
            char buf[30];

            s.player = tq->player;
            s.nerrors = 0;
            s.input = stream_contents(tq->program_stream);

            program = parse_program(current_db_version, client, &s);

            sprintf(buf, "%d error(s).", s.nerrors);
            notify(player, buf);

            if (program) {
                db_set_verb_program(h, program);
                notify(player, "Verb programmed.");
            } else
                notify(player, "Verb not programmed.");
        }
    }

    free_str(tq->program_verb);
    free_stream(tq->program_stream);
    tq->program_stream = nullptr;
}

static void
set_delimiter(char **slot, const char *string)
{
    if (*slot)
        free_str(*slot);
    if (*string == '\0')
        *slot = nullptr;
    else
        *slot = str_dup(string);
}

static int
find_verb_on(Objid oid, Parsed_Command * pc, db_verb_handle * vh)
{
    if (!valid(oid))
        return 0;

    *vh = db_find_command_verb(oid, pc->verb,
                               (pc->dobj == oid ? ASPEC_THIS
                                : pc->dobj == NOTHING ? ASPEC_NONE
                                : ASPEC_ANY),
                               pc->prep,
                               (pc->iobj == oid ? ASPEC_THIS
                                : pc->iobj == NOTHING ? ASPEC_NONE
                                : ASPEC_ANY));

    return vh->ptr != nullptr;
}

static int
do_intrinsic_command(tqueue * tq, Parsed_Command * pc)
{
    int icmd = icmd_index(pc->verb);
    if (!(icmd && (tq->icmds & (1 << icmd))))
        return 0;
    switch (icmd) {
        default:
            panic_moo("Bad return value from icmd_index()");
            break;
        case ICMD_PROGRAM:
            if (!is_programmer(tq->player))
                return 0;
            if (pc->args.v.list[0].v.num != 1)
                notify(tq->player, "Usage:  .program object:verb");
            else
                start_programming(tq, (char *) pc->args.v.list[1].v.str);
            break;
        case ICMD_PREFIX:
        case ICMD_OUTPUTPREFIX:
            set_delimiter(&(tq->output_prefix), pc->argstr);
            break;
        case ICMD_SUFFIX:
        case ICMD_OUTPUTSUFFIX:
            set_delimiter(&(tq->output_suffix), pc->argstr);
            break;
    }
    return 1;
}

/* I made `run_server_task_setting_id' static and removed it from
 * the header to better control access to a central point of task
 * creation.  Functions that want to run tasks should call one of the
 * externally visible entry points -- probably `run_server_task' --
 * which ensure things get cleaned up properly.
 */
static
enum outcome
run_server_task_setting_id(Objid player, Var what, const char *verb,
                           Var args, const char *argstr, Var *result,
                           int *task_id);

static int
do_command_task(tqueue * tq, char *command)
{
    if (tq->program_stream) {   /* We're programming */
        if (strcmp(command, ".") == 0)  /* Done programming */
            end_programming(tq);
        else
            stream_printf(tq->program_stream, "%s\n", command);
    } else {
        Parsed_Command *pc = parse_command(command, tq->player);

        if (!pc)
            return 0;

        if (!do_intrinsic_command(tq, pc)) {
            Objid location = (valid(tq->player)
                              ? db_object_location(tq->player)
                              : NOTHING);
            Objid _this;
            db_verb_handle vh;
            Var result, args;

            result.type = TYPE_INT; /* for free_var() if task isn't DONE */
            if (tq->output_prefix)
                notify(tq->player, tq->output_prefix);

            args = parse_into_wordlist(command);
            if (run_server_task_setting_id(tq->player, Var::new_obj(tq->handler),
                                           "do_command", args, command,
                                           &result, &(tq->last_input_task_id))
                    != OUTCOME_DONE
                    || is_true(result)) {
                /* Do nothing more; we assume :do_command handled it. */
            } else if (find_verb_on(_this = tq->player, pc, &vh)
                       || find_verb_on(_this = location, pc, &vh)
                       || find_verb_on(_this = pc->dobj, pc, &vh)
                       || find_verb_on(_this = pc->iobj, pc, &vh)
                       || (valid(location)
                           && !server_int_option("player_huh", PLAYER_HUH)
                           && (vh = db_find_callable_verb(Var::new_obj(_this = location), "huh"),
                               vh.ptr))
                       || (valid(tq->player)
                           && server_int_option("player_huh", PLAYER_HUH)
                           && (vh = db_find_callable_verb(Var::new_obj(_this = tq->player), "huh"),
                               vh.ptr))) {
                do_input_task(tq->player, pc, _this, vh);
            } else {
                notify(tq->player, "I couldn't understand that.");
                tq->last_input_task_id = 0;
            }

            if (tq->output_suffix)
                notify(tq->player, tq->output_suffix);

            /* clean up after `run_server_task_setting_id' */
            current_task_id = -1;
            free_var(current_local);
            free_var(result);
        }

        free_parsed_command(pc);
    }

    return 1;
}

static int
do_login_task(tqueue * tq, char *command)
{
    Var result;
    Var args;
    Objid old_max_object = db_last_used_objid();

    result.type = TYPE_INT; /* In case #0:do_login_command does not exist
                 * or does not immediately return.
                 */

    bool clear_command = false; /* A flag that determines whether or not do_login_command
                                   will fall through with a blank command */

    if (server_int_option("proxy_rewrite", 1) && is_localhost(tq->player))
    {
        /* To avoid printing the login screen (and allow redlists to work), ignore blank lines coming from localhost.
         * For special circumstances, the user can override this behavior with a 'do_blank_command' verb on the listening object.
         * If the verb returns a true value, the server will not ignore the blank line. */
        if (command[0] == '\0') {
            Var do_blank_command_result;
            run_server_task_setting_id(tq->player, Var::new_obj(tq->handler), "do_blank_command",
                                       parse_into_wordlist(command), command, &do_blank_command_result, &(tq->last_input_task_id));
            if (!is_true(do_blank_command_result))
                return 1;
        }

        /* Detect and parse incoming localhost proxies. This allows us to have an SSL presence and keep the originating IP. */
        if (strlen(command) >= 5 && strncmp(command, "PROXY", 5) == 0) {
            int status = proxy_connected(tq->player, command);
            if (status == 0)
                /* Clear the command so that we don't get an `I don't understand that.` from the proxy command. */
                clear_command = true;
        }
    }

    args = parse_into_wordlist(clear_command ? "\0" : command);
    run_server_task_setting_id(tq->player, Var::new_obj(tq->handler),
                               "do_login_command", args, clear_command ? "\0" : command,
                               &result, &(tq->last_input_task_id));
    /* The connected player (tq->player) may be non-negative if
     * `do_login_command' already called the `switch_player' built-in
     * to log the connection in to a player.
     */
    if (tq->connected && tq->player < 0 && result.type == TYPE_OBJ && is_user(result.v.obj)) {
        Objid new_player = result.v.obj;
        Objid old_player = tq->player;
        tqueue *dead_tq = find_tqueue(new_player, 0);
        task *t;

        tq->player = new_player;
        if (tq->num_bg_tasks) {
            /* Cute; this un-logged-in connection has some queued tasks!
             * Must copy them over to their own tqueue for accounting...
             */
            tqueue *old_tq = find_tqueue(old_player, 1);

            old_tq->num_bg_tasks = tq->num_bg_tasks;
            while ((t = dequeue_bg_task(tq)) != nullptr)
                enqueue_bg_task(old_tq, t);
            tq->num_bg_tasks = 0;
        }
        if (dead_tq) {
            /* Copy over tasks from old queue for player */
            tq->num_bg_tasks = dead_tq->num_bg_tasks;
            while ((t = dequeue_input_task(dead_tq, DQ_FIRST)) != nullptr) {
                free_task(t, 0);
            }
            while ((t = dequeue_bg_task(dead_tq)) != nullptr) {
                enqueue_bg_task(tq, t);
            }
            dead_tq->player = NOTHING;  /* it'll be freed by run_ready_tasks */
            dead_tq->num_bg_tasks = 0;
        }
        /* clean up after `run_server_task_setting_id' before calling
         * `player_connected' because `player_connected' may kick off
         * another task
         */
        current_task_id = -1;
        free_var(current_local);
        free_var(result);
        player_connected(old_player, new_player, new_player > old_max_object);
    } else {
        /* clean up after `run_server_task_setting_id'
         */
        current_task_id = -1;
        free_var(current_local);
        free_var(result);
    }
    return 1;
}

static void
do_out_of_band_command(tqueue * tq, char *command)
{
    run_server_task(tq->player, Var::new_obj(tq->handler), "do_out_of_band_command",
                    parse_into_wordlist(command), command, nullptr);
}

static int
is_out_of_input(tqueue * tq)
{
    return !tq->connected && !tq->first_input;
}

/*
 * Exported interface
 */

task_queue
new_task_queue(Objid player, Objid handler)
{
    tqueue *tq = find_tqueue(player, 1);
    task_queue result;

    result.ptr = tq;

    tq->connected = 1;
    tq->handler = handler;

    return result;
}

void
free_task_queue(task_queue q)
{
    tqueue *tq = (tqueue *) q.ptr;

    tq->connected = 0;

    /* Activate this queue to ensure that the pending read()ing task will
     * eventually get resumed, even if there's no more input for it.
     */

    if (tq->reading)
        ensure_usage(tq);
}

#define TASK_CO_TABLE(DEFINE, tq, value, _)             \
    DEFINE(flush-command, _, TYPE_STR, str,             \
           tq->flush_cmd ? str_ref(tq->flush_cmd) : str_dup(""),    \
           {                                \
                                            if (tq->flush_cmd)                   \
                                            free_str(tq->flush_cmd);             \
                                            if (value.type == TYPE_STR && value.v.str[0] != '\0')    \
                                                tq->flush_cmd = str_ref(value.v.str);        \
                                                else                         \
                                                    tq->flush_cmd = 0;                   \
                   })                               \
                \
                DEFINE(hold-input, _, TYPE_INT, num,                \
                       tq->hold_input,                      \
                       {                                \
                                                        tq->hold_input = is_true(value);             \
                                                        /* Anything to be done? */               \
                                                        if (!tq->hold_input && tq->first_input)          \
                                                        ensure_usage(tq);                    \
                       })                               \
                    \
                    DEFINE(disable-oob, _, TYPE_INT, num,               \
                           tq->disable_oob,                     \
                           {                                \
                                                            tq->disable_oob = is_true(value);            \
                                                            /* Anything to be done? */               \
                                                            if (!tq->disable_oob && tq->first_input          \
                                                                    && (tq->first_itail->next                \
                                                                            || tq->first_input->kind == TASK_OOB))       \
                                                            ensure_usage(tq);                    \
                           })                               \
                        \
                        DEFINE(intrinsic-commands, _, TYPE_LIST, list,          \
                               icmd_list(tq->icmds).v.list,                 \
                               {                                \
                                                                if (!icmd_set_flags(tq, value))              \
                                                                return 0;                        \
                               })                               \

int
tasks_set_connection_option(task_queue q, const char *option, Var value)
{
    CONNECTION_OPTION_SET(TASK_CO_TABLE, (tqueue *)q.ptr, option, value);
}

int
tasks_connection_option(task_queue q, const char *option, Var * value)
{
    CONNECTION_OPTION_GET(TASK_CO_TABLE, (tqueue *)q.ptr, option, value);
}

Var
tasks_connection_options(task_queue q, Var list)
{
    CONNECTION_OPTION_LIST(TASK_CO_TABLE, (tqueue *)q.ptr, list);
}

#undef TASK_CO_TABLE

static void
enqueue_input_task(tqueue * tq, const char *input, int at_front, int binary, bool is_telnet)
{
    static char oob_prefix[] = OUT_OF_BAND_PREFIX;
    task *t;

    t = (task *)mymalloc(sizeof(task), M_TASK);
    if (binary)
        t->kind = TASK_BINARY;
    else if (is_telnet)
        t->kind = TASK_OOB;
    else if (oob_quote_prefix_length > 0
             && strncmp(oob_quote_prefix, input, oob_quote_prefix_length) == 0)
        t->kind = TASK_QUOTED;
    else if (sizeof(oob_prefix) > 1
             && strncmp(oob_prefix, input, sizeof(oob_prefix) - 1) == 0)
        t->kind = TASK_OOB;
    else
        t->kind = TASK_INBAND;

    t->t.input.string = str_dup(input);
    tq->total_input_length += (t->t.input.length = strlen(input));

    t->t.input.next_itail = nullptr;
    if (at_front && tq->first_input) {  /* if nothing there, front == back */
        if ((tq->first_input->kind == TASK_OOB) != (t->kind == TASK_OOB)) {
            t->t.input.next_itail = tq->first_itail;
            tq->first_itail = t;
            if (tq->last_itail == &(tq->first_itail))
                tq->last_itail = &(t->t.input.next_itail);
        }
        t->next = tq->first_input;
        tq->first_input = t;
    }
    else {
        if (tq->first_input && (((*(tq->last_itail))->kind == TASK_OOB)
                                != (t->kind == TASK_OOB)))
            tq->last_itail = &((*(tq->last_itail))->t.input.next_itail);
        *(tq->last_itail) = t;

        *(tq->last_input) = t;
        tq->last_input = &(t->next);
        t->next = nullptr;
    }

    /* Anything to do with this line? */
    if (!tq->hold_input || tq->reading
            || (!tq->disable_oob && t->kind == TASK_OOB))
        ensure_usage(tq);

    if (!tq->input_suspended
            && tq->connected
            && tq->total_input_length > INPUT_HIWAT) {
        server_suspend_input(tq->player);
        tq->input_suspended = 1;
    }
}

void
task_suspend_input(task_queue q)
{
    tqueue *tq = (tqueue *)q.ptr;

    if (!tq->input_suspended && tq->connected) {
        server_suspend_input(tq->player);
        tq->input_suspended = 1;
    }
}

static void
flush_input(tqueue * tq, int show_messages)
{
    if (tq->first_input) {
        Stream *s = nullptr;
        task *t;

        if (show_messages) {
            notify(tq->player, ">> Flushing the following pending input:");
            s = new_stream(100);
        }
        while ((t = dequeue_input_task(tq, DQ_FIRST)) != nullptr) {
            /* TODO*** flush only non-TASK_OOB tasks ??? */
            if (show_messages) {
                stream_printf(s, ">>     %s", t->t.input.string);
                notify(tq->player, reset_stream(s));
            }
            free_task(t, 1);
        }
        if (show_messages) {
            notify(tq->player, ">> (Done flushing)");
            free_stream(s);
        }
    } else if (show_messages)
        notify(tq->player, ">> No pending input to flush...");
}

void
new_input_task(task_queue q, const char *input, int binary, bool out_of_band)
{
    tqueue *tq = (tqueue *)q.ptr;

    if (tq->flush_cmd && strcasecmp(input, tq->flush_cmd) == 0) {
        flush_input(tq, 1);
        return;
    }
    enqueue_input_task(tq, input, 0/*at-rear*/, binary, out_of_band);
}

static void
enqueue_waiting(task * t)
{   /* either FORKED or SUSPENDED */

    struct timeval *start_tvp = GET_START_TIME(t);
    Objid progr = (t->kind == TASK_FORKED
                   ? t->t.forked.a.progr
                   : progr_of_cur_verb(t->t.suspended.the_vm));
    tqueue *tq = find_tqueue(progr, 1);

    tq->num_bg_tasks++;
    if (!waiting_tasks || timercmp(start_tvp, GET_START_TIME(waiting_tasks), < )) {
        t->next = waiting_tasks;
        waiting_tasks = t;
    } else {
        task *tt;

        for (tt = waiting_tasks; tt->next; tt = tt->next)
            if (timercmp(start_tvp, GET_START_TIME(tt->next), < ))
                break;
        t->next = tt->next;
        tt->next = t;
    }
}

static void
enqueue_forked(Program * program, activation a, Var * rt_env,
               int f_index, struct timeval start_tv, int id)
{
    task *t = (task *)mymalloc(sizeof(task), M_TASK);

    t->kind = TASK_FORKED;
    t->t.forked.program = program;
    /* The next two lines were not present before 1.8.2.  a.rt_env/prog
     * were never accessed and were eventually overwritten by
     * forked.rt_env/program in do_forked_task().  Makes no sense to store
     * it two places, but here we are.
     * Setting it in the activation simplifies forked_task_bytes()
     */
    a.rt_env = rt_env;
    a.prog = program;
    a.base_rt_stack = nullptr;
    a.top_rt_stack = nullptr;
    t->t.forked.a = a;
    t->t.forked.rt_env = rt_env;
    t->t.forked.f_index = f_index;
    t->t.forked.start_tv = start_tv;
    t->t.forked.id = id;

    enqueue_waiting(t);
}

int
check_user_task_limit(Objid user)
{
    tqueue *tq = find_tqueue(user, 0);
    int limit = -1;
    Var v;

    if (valid(user)
            && db_find_property(Var::new_obj(user), "queued_task_limit", &v).ptr
            && v.type == TYPE_INT)
        limit = v.v.num;

    if (limit < 0)
        limit = server_int_option("queued_task_limit", -1);

    if (limit < 0)
        return 1;
    else if ((tq ? tq->num_bg_tasks : 0) >= limit)
        return 0;
    else
        return 1;
}

enum error
enqueue_forked_task2(activation a, int f_index, double after_seconds, int vid)
{
    struct timeval when;
    int id;
    Var *rt_env;

    if (!check_user_task_limit(a.progr))
        return E_QUOTA;

    /* We eschew the usual pattern of generating the task local value
     * when we generate the task id to avoid having to store it --
     * it's sufficient to generate it immediately before a forked task
     * becomes a real task (see `run_ready_tasks()').
     */
    id = new_task_id();
    /* The following code is crap.  It cost me long hours of debugging
     * to track this down when adding support for verb calls on
     * primitive types and a few minutes of head scratching to figure
     * out why it's assigning back a ref'd copy of each field.
     */
    a._this = var_ref(a._this);
    a.vloc = var_ref(a.vloc);
    a.verb = str_ref(a.verb);
    a.verbname = str_ref(a.verbname);
    a.prog = program_ref(a.prog);
    a.threaded = DEFAULT_THREAD_MODE;
    if (vid >= 0) {
        free_var(a.rt_env[vid]);
        a.rt_env[vid].type = TYPE_INT;
        a.rt_env[vid].v.num = id;
    }
    rt_env = copy_rt_env(a.rt_env, a.prog->num_var_names);
    when = double_to_start_tv(after_seconds);
    enqueue_forked(a.prog, a, rt_env, f_index, when, id);
    return E_NONE;
}

enum error
enqueue_suspended_task(vm the_vm, void *data)
{
    struct timeval when;
    task *t;

    if (data) {
        double after_seconds = *((double *) data);

        when = double_to_start_tv(after_seconds);
    } else {
        when.tv_sec = INTNUM_MAX;
        when.tv_usec = 0;
    }

    if (check_user_task_limit(progr_of_cur_verb(the_vm))) {
        t = (task *)mymalloc(sizeof(task), M_TASK);
        t->kind = TASK_SUSPENDED;
        t->t.suspended.the_vm = the_vm;
        t->t.suspended.start_tv = when;
        t->t.suspended.value = zero;

        enqueue_waiting(t);
        return E_NONE;
    } else
        return E_QUOTA;
}

void
resume_task(vm the_vm, Var value)
{
    task *t = (task *)mymalloc(sizeof(task), M_TASK);
    Objid progr = progr_of_cur_verb(the_vm);
    tqueue *tq = find_tqueue(progr, 1);

    t->kind = TASK_SUSPENDED;
    t->t.suspended.the_vm = the_vm;
    t->t.suspended.start_tv.tv_sec = 0; /* ready now */
    t->t.suspended.start_tv.tv_usec = 0;
    t->t.suspended.value = value;

    ensure_usage(tq);
    enqueue_waiting(t);
}

Var
read_input_now(Objid connection)
{
    tqueue *tq = find_tqueue(connection, 0);
    task *t;
    Var r;

    if (!tq || is_out_of_input(tq)) {
        r.type = TYPE_ERR;
        r.v.err = E_INVARG;
    } else if (!(t = dequeue_input_task(tq, DQ_INBAND))) {
        r.type = TYPE_INT;
        r.v.num = 0;
    } else {
        r.type = TYPE_STR;
        r.v.str = t->t.input.string;
        myfree(t, M_TASK);
    }

    return r;
}

enum error
make_reading_task(vm the_vm, void *data)
{
    Objid player = *((Objid *) data);
    tqueue *tq = find_tqueue(player, 0);

    if (!tq || tq->reading || is_out_of_input(tq))
        return E_INVARG;
    else {
        tq->reading = 1;
        tq->parsing = 0;
        tq->reading_vm = the_vm;
        if (tq->first_input)    /* Anything to read? */
            ensure_usage(tq);
        return E_NONE;
    }
}

/* A connection that's used to read/parse HTTP will probably be used
 * to read/parse HTTP again -- so we allocate a struct for holding
 * parsing state once and reuse it.
 */
static enum error
make_http_task(vm the_vm, Objid player, int request)
{
    tqueue *tq = find_tqueue(player, 0);

    if (!tq || tq->reading || is_out_of_input(tq))
        return E_INVARG;
    else {
        tq->reading = 1;
        tq->parsing = 1;
        tq->reading_vm = the_vm;
        if (tq->parsing_state == nullptr) {
            tq->parsing_state =
            (http_parsing_state *)mymalloc(sizeof(struct http_parsing_state), M_STRUCT);
            init_http_parsing_state(tq->parsing_state);
        }
        tq->parsing_state->status = PARSING;
        tq->parsing_state->parser.data = tq;
        http_parser_init(&tq->parsing_state->parser,
                         request ? HTTP_REQUEST : HTTP_RESPONSE);
        if (tq->first_input)    /* Anything to read? */
            ensure_usage(tq);
        return E_NONE;
    }
}

enum error
make_parsing_http_request_task(vm the_vm, void *data)
{
    Objid player = *((Objid *) data);
    return make_http_task(the_vm, player, 1);
}

enum error
make_parsing_http_response_task(vm the_vm, void *data)
{
    Objid player = *((Objid *) data);
    return make_http_task(the_vm, player, 0);
}

int
last_input_task_id(Objid player)
{
    tqueue *tq = find_tqueue(player, 0);

    return tq ? tq->last_input_task_id : 0;
}

int
next_task_start(void)
{
    tqueue *tq;

    for (tq = active_tqueues; tq; tq = tq->next)
        if (tq->first_input != nullptr || tq->first_bg != nullptr)
            return 0;

    if (waiting_tasks != nullptr) {
        struct timeval *tvp, now, delta;

        gettimeofday(&now, nullptr);
        tvp = GET_START_TIME(waiting_tasks);
        timersub(tvp, &now, &delta);
        if (delta.tv_sec < 0 || delta.tv_usec < 0)
            return 0;
        if (delta.tv_sec > 9)
            delta.tv_sec = 9;
        return delta.tv_usec + delta.tv_sec * 1000000;
    }
    return -1;      /* never */
}

static Var
create_or_extend(Var in, const char *_new, int newlen)
{
    static Stream *s = nullptr;
    if (!s)
        s = new_stream(100);

    Var out;

    if (in.type == TYPE_STR) {
        stream_add_string(s, in.v.str);
        stream_add_raw_bytes_to_binary(s, _new, newlen);
        free_var(in);
        out.type = TYPE_STR;
        out.v.str = str_dup(reset_stream(s));
    }
    else {
        stream_add_raw_bytes_to_binary(s, _new, newlen);
        free_var(in);
        out.type = TYPE_STR;
        out.v.str = str_dup(reset_stream(s));
    }

    return out;
}

static int
on_message_begin_callback(http_parser *parser)
{
    struct http_parsing_state *state = (struct http_parsing_state *)parser;
    state->result = new_map();
    return 0;
}

static int
on_url_callback(http_parser *parser, const char *url, size_t length)
{
    struct http_parsing_state *state = (struct http_parsing_state *)parser;
    state->uri = create_or_extend(state->uri, url, length);
    return 0;
}

static void
maybe_complete_header(struct http_parsing_state *state)
{
    if (state->headers.type != TYPE_MAP) {
        free_var(state->headers);
        state->headers = new_map();
    }

    if (state->header_value_under_constr.type == TYPE_STR) {
        state->headers = mapinsert(state->headers,
                                   state->header_field_under_constr,
                                   state->header_value_under_constr);
        state->header_field_under_constr.type = TYPE_NONE;
        state->header_value_under_constr.type = TYPE_NONE;
    }
}

static int
on_header_field_callback(http_parser *parser, const char *field, size_t length)
{
    struct http_parsing_state *state = (struct http_parsing_state *)parser;

    maybe_complete_header(state);

    state->header_field_under_constr = create_or_extend(state->header_field_under_constr, field, length);

    return 0;
}

static int
on_header_value_callback(http_parser *parser, const char *value, size_t length)
{
    struct http_parsing_state *state = (struct http_parsing_state *)parser;

    state->header_value_under_constr = create_or_extend(state->header_value_under_constr, value, length);

    return 0;
}

static int
on_headers_complete_callback(http_parser *parser)
{
    struct http_parsing_state *state = (struct http_parsing_state *)parser;

    maybe_complete_header(state);

    return 0;
}

static int
on_body_callback(http_parser *parser, const char *body, size_t length)
{
    struct http_parsing_state *state = (struct http_parsing_state *)parser;

    state->body = create_or_extend(state->body, body, length);

    return 0;
}

static int
on_message_complete_callback(http_parser *parser)
{
    static Var URI, METHOD, HEADERS, BODY, STATUS;
    static int init = 0;
    if (!init) {
        init = 1;

#define INIT_KEY(var, val)      \
    var.type = TYPE_STR;        \
    var.v.str = str_dup(val)

        INIT_KEY(URI, "uri");
        INIT_KEY(METHOD, "method");
        INIT_KEY(HEADERS, "headers");
        INIT_KEY(BODY, "body");
        INIT_KEY(STATUS, "status");

#undef INIT_KEY
    }

    struct http_parsing_state *state = (struct http_parsing_state *)parser;

    if (parser->type == HTTP_REQUEST) {
        Var method;
        method.type = TYPE_STR;
        method.v.str = str_dup(http_method_str((http_method)state->parser.method));
        state->result = mapinsert(state->result, var_dup(METHOD), method);
    }
    else { /* HTTP_RESPONSE */
        Var status;
        status.type = TYPE_INT;
        status.v.num = parser->status_code;
        state->result = mapinsert(state->result, var_dup(STATUS), status);
    }

    if (state->uri.type == TYPE_STR)
        state->result = mapinsert(state->result, var_dup(URI), var_dup(state->uri));

    if (state->headers.type == TYPE_MAP)
        state->result = mapinsert(state->result, var_dup(HEADERS), var_dup(state->headers));

    if (state->body.type == TYPE_STR)
        state->result = mapinsert(state->result, var_dup(BODY), var_dup(state->body));

    state->status = DONE;

    return 0;
}

static http_parser_settings settings = {on_message_begin_callback,
                                        on_url_callback,
                                        on_header_field_callback,
                                        on_header_value_callback,
                                        on_headers_complete_callback,
                                        on_body_callback,
                                        on_message_complete_callback
                                       };

/* There is surprisingness in how tasks actually get created in
 * response to player input, so I'm documenting it here.
 * `run_ready_tasks' turns player input into tasks (and verb calls).
 * `run_server_task_setting_id' is the central point for task
 * creation -- in the case of command verbs it's important to
 * understand that correct behavior *depends on side-effects*.  In
 * particular, `run_server_task_setting_id' must be called *before*
 * `do_input_task' (see `do_command_task') in order to ensure the task
 * is set up.  Basically, the server tries to run "do_command" -- if
 * that fails, it handles the input command.  The same "task" is used
 * for both.  Messy.
 */
void
run_ready_tasks(void)
{
    task *t, *next_t;
    struct timeval now;
    tqueue *tq, *next_tq;

    gettimeofday(&now, nullptr);
    for (t = waiting_tasks; t && timercmp(GET_START_TIME(t), &now, <= ); t = next_t) {
        Objid progr = (t->kind == TASK_FORKED
                       ? t->t.forked.a.progr
                       : progr_of_cur_verb(t->t.suspended.the_vm));
        tqueue *tq = find_tqueue(progr, 1);

        next_t = t->next;
        ensure_usage(tq);
        enqueue_bg_task(tq, t);
    }
    waiting_tasks = t;

    {
        int did_one = 0;
        time_t start = time(nullptr);

        /* Loop over tqueues, looking for a task */
        while (active_tqueues && !did_one) {
            tq = active_tqueues;

            if (tq->reading && is_out_of_input(tq)) {
                Var v;

                tq->reading = 0;
                tq->parsing = 0;
                if (tq->parsing_state != nullptr)
                    reset_http_parsing_state(tq->parsing_state);
                current_task_id = tq->reading_vm->task_id;
                current_local = var_ref(tq->reading_vm->local);
                v.type = TYPE_ERR;
                v.v.err = E_INVARG;
                resume_from_previous_vm(tq->reading_vm, v);
                current_task_id = -1;
                free_var(current_local);
                did_one = 1;
            }

            /* Loop over tasks, looking for runnable one */
            while (!did_one) {
                t = dequeue_input_task(tq, ((tq->hold_input && !tq->reading)
                                            ? DQ_OOB
                                            : DQ_FIRST));
                if (!t)
                    t = dequeue_bg_task(tq);
                if (!t)
                    break;

                switch (t->kind) {
                    default:
                        panic_moo("Unexpected task kind in run_ready_tasks()");
                        break;
                    case TASK_OOB:
                        do_out_of_band_command(tq, t->t.input.string);
                        did_one = 1;
                        break;
                    case TASK_BINARY:
                    case TASK_INBAND:
                        if (tq->reading && tq->parsing) {
                            int done = 0;
                            int len;
                            const char *binary = binary_to_raw_bytes(t->t.input.string, &len);
                            if (binary == nullptr) {
                                /* This can happen if someone forces an
                                 * invalid binary string as input on this
                                 * connection!
                                 */
                                /* It can happen even before the
                                 * `on_message_begin_callback()' is
                                 * called.
                                 */
                                if (tq->parsing_state->status == PARSING)
                                    free_var(tq->parsing_state->result);
                                tq->parsing_state->result = var_ref(zero);
                                done = 1;
                            }
                            else {
                                http_parser_execute(&tq->parsing_state->parser, &settings, binary, len);
                                if (tq->parsing_state->parser.http_errno != HPE_OK) {
                                    Var key, value;
                                    key.type = TYPE_STR;
                                    key.v.str = str_dup("error");
                                    value = new_list(2);
                                    value.v.list[1].type = TYPE_STR;
                                    value.v.list[1].v.str = str_dup(http_errno_name((http_errno)tq->parsing_state->parser.http_errno));
                                    value.v.list[2].type = TYPE_STR;
                                    value.v.list[2].v.str = str_dup(http_errno_description((http_errno)tq->parsing_state->parser.http_errno));
                                    tq->parsing_state->result = mapinsert(tq->parsing_state->result, key, value);
                                    done = 1;
                                }
                                else if (tq->parsing_state->parser.upgrade) {
                                    Var key;
                                    key.type = TYPE_STR;
                                    key.v.str = str_dup("upgrade");
                                    tq->parsing_state->result = mapinsert(tq->parsing_state->result, key, Var::new_int(1));
                                    done = 1;
                                }
                                else if (tq->parsing_state->status == DONE)
                                    done = 1;
                            }
                            if (done) {
                                Var v = var_ref(tq->parsing_state->result);
                                tq->reading = 0;
                                tq->parsing = 0;
                                reset_http_parsing_state(tq->parsing_state);
                                current_task_id = tq->reading_vm->task_id;
                                current_local = var_ref(tq->reading_vm->local);
                                resume_from_previous_vm(tq->reading_vm, v);
                                free_var(v);
                                current_task_id = -1;
                                free_var(current_local);
                            }
                            did_one = 1;
                        }
                        else if (tq->reading) {
                            Var v;
                            tq->reading = 0;
                            tq->parsing = 0;
                            current_task_id = tq->reading_vm->task_id;
                            current_local = var_ref(tq->reading_vm->local);
                            v.type = TYPE_STR;
                            v.v.str = t->t.input.string;
                            resume_from_previous_vm(tq->reading_vm, v);
                            current_task_id = -1;
                            free_var(current_local);
                            did_one = 1;
                        } else {
                            /* Used to insist on tq->connected here, but Pavel
                             * couldn't come up with a good reason to keep that
                             * restriction.
                             */
                            add_command_to_history(tq->player, t->t.input.string);
                            did_one = (tq->player >= 0
                                       ? do_command_task
                                       : do_login_task) (tq, t->t.input.string);
                        }
                        break;
                    case TASK_FORKED:
                    {
                        forked_task ft;
                        ft = t->t.forked;
                        current_task_id = ft.id;
                        current_local = new_map();
                        ft.a.threaded = DEFAULT_THREAD_MODE;
                        do_forked_task(ft.program, ft.rt_env, ft.a,
                                       ft.f_index);
                        current_task_id = -1;
                        free_var(current_local);
                        did_one = 1;
                    }
                    break;
                    case TASK_SUSPENDED:
                        current_task_id = t->t.suspended.the_vm->task_id;
                        current_local = var_ref(t->t.suspended.the_vm->local);
                        resume_from_previous_vm(t->t.suspended.the_vm,
                                                t->t.suspended.value);
                        /* must free value passed in to resume_task() and do_resume() */
                        free_var(t->t.suspended.value);
                        current_task_id = -1;
                        free_var(current_local);
                        did_one = 1;
                        break;
                }
                free_task(t, 0);
            }

            active_tqueues = tq->next;

            if (did_one) {
                /* Bump the usage level of this tqueue */
                time_t end = time(nullptr);

                tq->usage += end - start;
                activate_tqueue(tq);
            } else {
                /* There was nothing to do on this tqueue, so deactivate it */
                deactivate_tqueue(tq);
            }
        }
    }

    /* Free any unconnected and empty tqueues */
    for (tq = idle_tqueues; tq; tq = next_tq) {
        next_tq = tq->next;

        if (!tq->connected && !tq->first_input && tq->num_bg_tasks == 0)
            free_tqueue(tq);
    }
}

enum outcome
run_server_task(Objid player, Var what, const char *verb, Var args,
                const char *argstr, Var *result)
{
    enum outcome ret = run_server_task_setting_id(player, what, verb,
                       args, argstr,
                       result, nullptr);

    current_task_id = -1;
    free_var(current_local);

    return ret;
}

/* This is the usual entry point for a new task (the other being the
 * creation of a forked task).  It allocates the current task local
 * value -- make sure it gets cleaned up properly when the task
 * finishes.
 */
static
enum outcome
run_server_task_setting_id(Objid player, Var what, const char *verb,
                           Var args, const char *argstr, Var *result,
                           int *task_id)
{
    db_verb_handle h;

    current_task_id = new_task_id();
    current_local = new_map();

    if (task_id)
        *task_id = current_task_id;

    h = db_find_callable_verb(what.type == TYPE_WAIF ? Var::new_obj(what.v.waif->_class) : what, verb);
    if (h.ptr)
        return do_server_verb_task(what, verb, args, h, player, argstr,
                                   result, 1/*traceback*/);
    else {
        /* simulate an empty verb */
        if (result) {
            result->type = TYPE_INT;
            result->v.num = 0;
        }
        free_var(args);
        return OUTCOME_DONE;
    }
}

/* for emergency mode */
enum outcome
run_server_program_task(Objid _this, const char *verb, Var args, Objid vloc,
                        const char *verbname, Program * program, Objid progr,
                        int debug, Objid player, const char *argstr,
                        Var *result)
{
    current_task_id = new_task_id();
    current_local = new_map();

    enum outcome ret = do_server_program_task(Var::new_obj(_this), verb, args, Var::new_obj(vloc), verbname, program,
                       progr, debug, player, argstr,
                       result, 1/*traceback*/);

    current_task_id = -1;
    free_var(current_local);

    return ret;
}

void
register_task_queue(task_enumerator enumerator)
{
    ext_queue *eq = (ext_queue *)mymalloc(sizeof(ext_queue), M_TASK);

    eq->enumerator = enumerator;
    eq->next = external_queues;
    external_queues = eq;
}

static void
write_forked_task(forked_task ft)
{
    unsigned lineno = find_line_number(ft.program, ft.f_index, 0);

    /* saving rounds to the nearest second.  restart is slow anyway */
    dbio_printf("0 %d %d %d\n", lineno, ROUND(&ft.start_tv), ft.id);
    write_activ_as_pi(ft.a);
    write_rt_env(ft.program->var_names, ft.rt_env, ft.program->num_var_names);
    dbio_write_forked_program(ft.program, ft.f_index);
}

static void
write_suspended_task(suspended_task st)
{
    /* saving rounds down to the nearest second.  restart is slow anyway */
    dbio_printf("%d %d ", ROUND(&st.start_tv), st.the_vm->task_id);
    dbio_write_var(st.value);
    write_vm(st.the_vm);
}

void
write_task_queue(void)
{
    int forked_count = 0;
    int suspended_count = 0;
    task *t;
    tqueue *tq;

    dbio_printf("0 clocks\n");  /* for compatibility's sake */

    for (t = waiting_tasks; t; t = t->next)
        if (t->kind == TASK_FORKED)
            forked_count++;
        else            /* t->kind == TASK_SUSPENDED */
            suspended_count++;

    for (tq = active_tqueues; tq; tq = tq->next)
        for (t = tq->first_bg; t; t = t->next)
            if (t->kind == TASK_FORKED)
                forked_count++;
            else        /* t->kind == TASK_SUSPENDED */
                suspended_count++;

    dbio_printf("%d queued tasks\n", forked_count);

    for (t = waiting_tasks; t; t = t->next)
        if (t->kind == TASK_FORKED)
            write_forked_task(t->t.forked);

    for (tq = active_tqueues; tq; tq = tq->next)
        for (t = tq->first_bg; t; t = t->next)
            if (t->kind == TASK_FORKED)
                write_forked_task(t->t.forked);

    dbio_printf("%d suspended tasks\n", suspended_count);

    for (t = waiting_tasks; t; t = t->next)
        if (t->kind == TASK_SUSPENDED)
            write_suspended_task(t->t.suspended);

    for (tq = active_tqueues; tq; tq = tq->next)
        for (t = tq->first_bg; t; t = t->next)
            if (t->kind == TASK_SUSPENDED)
                write_suspended_task(t->t.suspended);

    /* All tasks held in external queues are interrupted -- this
     * currently comprises tasks that are waiting on a fork/exec that
     * has not completed.
     */
    int interrupted_count = 0;
    struct qcl_data qdata;
    ext_queue *eq;

    qdata.progr = NOTHING;
    qdata.show_all = 1;
    qdata.i = 0;
    for (eq = external_queues; eq; eq = eq->next)
        (*eq->enumerator) (counting_closure, &qdata);
    interrupted_count = qdata.i;

    /* All tasks that are reading are interrupted -- this includes
       tasks that called both `read()' and `read_http()'.
     */
    for (tq = idle_tqueues; tq; tq = tq->next)
        if (tq->reading)
            interrupted_count++;

    dbio_printf("%d interrupted tasks\n", interrupted_count);

    qdata.progr = NOTHING;
    qdata.show_all = 1;
    qdata.i = 0;
    for (eq = external_queues; eq; eq = eq->next)
        (*eq->enumerator) (writing_closure, &qdata);

    for (tq = idle_tqueues; tq; tq = tq->next) {
        if (tq->reading) {
            dbio_printf("%d %s\n", tq->reading_vm->task_id, "interrupted reading task");
            write_vm(tq->reading_vm);
        }
    }
}

int
read_task_queue(void)
{
    int count, dummy;
    int suspended_count, suspended_task_header;
    int interrupted_count, interrupted_task_header;

    /* Skip obsolete clock stuff */
    if (dbio_scanf("%d clocks\n", &count) != 1) {
        errlog("READ_TASK_QUEUE: Bad clock count.\n");
        return 0;
    }
    for (; count > 0; count--)
        /* I use a `dummy' variable here and elsewhere instead of the `*'
         * assignment-suppression syntax of `scanf' because it allows more
         * straightforward error checking; unfortunately, the standard says
         * that suppressed assignments are not counted in determining the
         * returned value of `scanf'...
         */
        if (dbio_scanf("%d %d %d\n", &dummy, &dummy, &dummy) != 3) {
            errlog("READ_TASK_QUEUE: Bad clock; count = %d\n", count);
            return 0;
        }
    if (dbio_scanf("%d queued tasks\n", &count) != 1) {
        errlog("READ_TASK_QUEUE: Bad task count.\n");
        return 0;
    }
    for (; count > 0; count--) {
        int first_lineno, id, old_size, st;
        char c;
        struct timeval start_tv;
        Program *program;
        Var *rt_env, *old_rt_env;
        const char **old_names;
        activation a;

        if (dbio_scanf("%d %d %d %d%c",
                       &dummy, &first_lineno, &st, &id, &c) != 5
                || c != '\n') {
            errlog("READ_TASK_QUEUE: Bad numbers, count = %d.\n", count);
            return 0;
        }
        start_tv.tv_sec = st;
        start_tv.tv_usec = 0;
        if (!read_activ_as_pi(&a)) {
            errlog("READ_TASK_QUEUE: Bad activation, count = %d.\n", count);
            return 0;
        }
        a.temp.type = TYPE_NONE;
        if (!read_rt_env(&old_names, &old_rt_env, &old_size)) {
            errlog("READ_TASK_QUEUE: Bad env, count = %d.\n", count);
            return 0;
        }
        if (!(program = dbio_read_program(dbio_input_version,
                                          nullptr, (void *) "forked task"))) {
            errlog("READ_TASK_QUEUE: Bad program, count = %d.\n", count);
            return 0;
        }
        rt_env = reorder_rt_env(old_rt_env, old_names, old_size, program);
        program->first_lineno = first_lineno;

        enqueue_forked(program, a, rt_env, MAIN_VECTOR, start_tv, id);
    }

    suspended_task_header = dbio_scanf("%d suspended tasks\n",
                                       &suspended_count);
    if (suspended_task_header == EOF)
        return 1;       /* old version */
    if (suspended_task_header != 1) {
        errlog("READ_TASK_QUEUE: Bad suspended task count.\n");
        return 0;
    }
    for (; suspended_count > 0; suspended_count--) {
        task *t = (task *)mymalloc(sizeof(task), M_TASK);
        int task_id, st;
        char c;

        t->kind = TASK_SUSPENDED;
        if (dbio_scanf("%d %d%c", &st, &task_id, &c) != 3) {
            errlog("READ_TASK_QUEUE: Bad suspended task header, count = %d\n",
                   suspended_count);
            return 0;
        }
        t->t.suspended.start_tv.tv_sec = st;
        t->t.suspended.start_tv.tv_usec = 0;
        if (c == ' ')
            t->t.suspended.value = dbio_read_var();
        else if (c == '\n')
            t->t.suspended.value = zero;
        else {
            errlog("READ_TASK_QUEUE: Bad suspended task value, count = %d\n",
                   suspended_count);
            return 0;
        }

        if (!(t->t.suspended.the_vm = read_vm(task_id))) {
            errlog("READ_TASK_QUEUE: Bad suspended task vm, count = %d\n",
                   suspended_count);
            return 0;
        }
        enqueue_waiting(t);
    }

    if (dbio_input_version < DBV_Interrupt)
        return 1;

    interrupted_task_header = dbio_scanf("%d interrupted tasks\n",
                                         &interrupted_count);
    if (interrupted_task_header == EOF)
        return 1;       /* old version */
    if (interrupted_task_header != 1) {
        errlog("READ_TASK_QUEUE: Bad interrupted task count.\n");
        return 0;
    }
    for (; interrupted_count > 0; interrupted_count--) {
        int task_id;
        const char *status;
        vm the_vm;

        if (dbio_scanf("%d ", &task_id) != 1) {
            errlog("READ_TASK_QUEUE: Bad interrupted task header, count = %d\n",
                   interrupted_count);
            return 0;
        }
        if ((status = dbio_read_string()) == nullptr) {
            errlog("READ_TASK_QUEUE: Bad interrupted task status, count = %d\n",
                   interrupted_count);
            return 0;
        }

        if (!(the_vm = read_vm(task_id))) {
            errlog("READ_TASK_QUEUE: Bad interrupted task vm, count = %d\n",
                   interrupted_count);
            return 0;
        }

        task *t = (task *)mymalloc(sizeof(task), M_TASK);
        t->kind = TASK_SUSPENDED;
        t->t.suspended.start_tv.tv_sec = 0;
        t->t.suspended.start_tv.tv_usec = 0;
        t->t.suspended.value.type = TYPE_ERR;
        t->t.suspended.value.v.err = E_INTRPT;
        t->t.suspended.the_vm = the_vm;
        enqueue_waiting(t);
    }

    return 1;
}

/* Used in emergency mode and when handling the `.program' intrinsic
 * command.  Is only capable of finding verbs defined on permanent
 * objects (relies on `Objid' internally).
 */
db_verb_handle
find_verb_for_programming(Objid player, const char *verbref,
                          const char **message, const char **vname)
{
    char *copy = str_dup(verbref);
    char *colon = strchr(copy, ':');
    char *obj;
    Objid oid;
    db_verb_handle h;
    static Stream *str = nullptr;
    Var desc;

    if (!str)
        str = new_stream(100);

    h.ptr = nullptr;

    if (!colon || colon[1] == '\0') {
        free_str(copy);
        *message = "You must specify a verb; use the format object:verb.";
        return h;
    }
    *colon = '\0';
    obj = copy;
    *vname = verbref + (colon - copy) + 1;

    if (obj[0] == '$')
        oid = get_system_object(obj + 1);
    else
        oid = match_object(player, obj);

    if (!valid(oid)) {
        switch (oid) {
            case FAILED_MATCH:
                stream_printf(str, "I don't see \"%s\" here.", obj);
                break;
            case AMBIGUOUS:
                stream_printf(str, "I don't know which \"%s\" you mean.", obj);
                break;
            default:
                stream_printf(str, "\"%s\" is not a valid object.", obj);
                break;
        }
        *message = reset_stream(str);
        free_str(copy);
        return h;
    }
    desc.type = TYPE_STR;
    desc.v.str = *vname;
    h = find_described_verb(Var::new_obj(oid), desc);
    free_str(copy);

    if (!h.ptr)
        *message = "That object does not have that verb definition.";
    else if (!db_verb_allows(h, player, VF_WRITE)
             || (server_flag_option("protect_set_verb_code", 0)
                 && !is_wizard(player))) {
        *message = "Permission denied.";
        h.ptr = nullptr;
    } else {
        stream_printf(str, "Now programming %s:%s.  Use \".\" to end.",
                      db_object_name(oid), db_verb_names(h));
        *message = reset_stream(str);
    }

    return h;
}

static package
bf_queue_info(Var arglist, Byte next, void *vdata, Objid progr)
{
    int nargs = arglist.v.list[0].v.num;
    Var res;

    if (nargs == 0) {
        int count = 0;
        tqueue *tq;

        for (tq = active_tqueues; tq; tq = tq->next)
            count++;
        for (tq = idle_tqueues; tq; tq = tq->next)
            count++;

        res = new_list(count);
        for (tq = active_tqueues; tq; tq = tq->next) {
            res.v.list[count].type = TYPE_OBJ;
            res.v.list[count].v.obj = tq->player;
            count--;
        }
        for (tq = idle_tqueues; tq; tq = tq->next) {
            res.v.list[count].type = TYPE_OBJ;
            res.v.list[count].v.obj = tq->player;
            count--;
        }
    } else if (is_wizard(progr)) {
        /* Currently keeping the map key names forever. Is this called often enough to warrant that? */
        static Var queue_pname = str_dup_to_var("player");
        static Var queue_handler = str_dup_to_var("handler");
        static Var queue_connected = str_dup_to_var("connected");
        static Var queue_total_input_length = str_dup_to_var("total_input_length");
        static Var queue_last_input_task_id = str_dup_to_var("last_input_task_id");
        static Var queue_suspended = str_dup_to_var("input_suspended");
        static Var queue_usage = str_dup_to_var("usage");
        static Var queue_num_bg_tasks = str_dup_to_var("num_bg_tasks");
        static Var queue_hold_input = str_dup_to_var("hold_input");
        static Var queue_disable_oob = str_dup_to_var("disable_oob");
        static Var queue_reading = str_dup_to_var("reading");
        static Var queue_parsing = str_dup_to_var("parsing");
        static Var queue_vm = str_dup_to_var("reading_task_id");

        Objid who = arglist.v.list[1].v.obj;
        tqueue *tq = find_tqueue(who, 0);

        if (!tq) {
            res.type = TYPE_INT;
            res.v.num = 0;
        } else {
            res = new_map();
            res = mapinsert(res, var_ref(queue_pname), Var::new_obj(tq->player));
            res = mapinsert(res, var_ref(queue_handler), Var::new_obj(tq->handler));
            res = mapinsert(res, var_ref(queue_connected), Var::new_bool(tq->connected));
            res = mapinsert(res, var_ref(queue_total_input_length), Var::new_int(tq->total_input_length));
            res = mapinsert(res, var_ref(queue_last_input_task_id), Var::new_int(tq->last_input_task_id));
            res = mapinsert(res, var_ref(queue_suspended), Var::new_bool(tq->input_suspended));
            res = mapinsert(res, var_ref(queue_usage), Var::new_int(tq->usage));
            res = mapinsert(res, var_ref(queue_num_bg_tasks), Var::new_int(tq->num_bg_tasks));
            res = mapinsert(res, var_ref(queue_hold_input), Var::new_bool(tq->usage));
            res = mapinsert(res, var_ref(queue_disable_oob), Var::new_bool(tq->disable_oob));
            res = mapinsert(res, var_ref(queue_reading), Var::new_bool(tq->reading));
            res = mapinsert(res, var_ref(queue_parsing), Var::new_bool(tq->parsing));
            res = mapinsert(res, var_ref(queue_vm), tq->reading ?
                            Var::new_int(tq->reading_vm->task_id) :
                            Var::new_int(0));
        }

    } else {
        Objid who = arglist.v.list[1].v.obj;
        tqueue *tq = find_tqueue(who, 0);

        res.type = TYPE_INT;
        res.v.num = (tq ? tq->num_bg_tasks : 0);
    }

    free_var(arglist);
    return make_var_pack(res);
}

static package
bf_task_id(Var arglist, Byte next, void *vdata, Objid progr)
{
    Var r;
    r.type = TYPE_INT;
    r.v.num = current_task_id;
    free_var(arglist);
    return make_var_pack(r);
}

static int
activation_bytes(activation * ap)
{
    int total = sizeof(activation);
    Var *v;
    unsigned int i;

    /* The MOO Way [tm] is double-billing to avoid the possibility
     * of not billing at all, so the size of the prog is counted here
     * even though it will be shared unless the verb was reprogrammed.
     */
    total += program_bytes(ap->prog);
    for (i = 0; i < ap->prog->num_var_names; ++i)
        total += value_bytes(ap->rt_env[i]);
    if (ap->top_rt_stack) {
        for (v = ap->top_rt_stack - 1; v >= ap->base_rt_stack; v--)
            total += value_bytes(*v);
    }
    /* XXX ignore bi_func_data, it's an opaque type. */
    total += value_bytes(ap->temp) - sizeof(Var);
    total += memo_strlen(ap->verb) + 1;
    total += memo_strlen(ap->verbname) + 1;
    return total;
}

static int
forked_task_bytes(forked_task ft)
{
    int total = sizeof(forked_task);

    /* ft.program is duplicated in ft.a */
    total += activation_bytes(&ft.a) - sizeof(activation);
    /* ft.rt_env is properly inside ft.a now */

    return total;
}

static Var
list_for_forked_task(forked_task ft, Objid progr, int include_variables)
{
    Var list;

    list = new_list(include_variables ? 11 : 10);

    list.v.list[1].type = TYPE_INT;
    list.v.list[1].v.num = ft.id;
    list.v.list[2].type = TYPE_INT;
    list.v.list[2].v.num = ROUND(&ft.start_tv);
    list.v.list[3].type = TYPE_INT;
    list.v.list[3].v.num = 0;           /* OBSOLETE: was clock ID */
    list.v.list[4].type = TYPE_INT;
    list.v.list[4].v.num = DEFAULT_BG_TICKS;    /* OBSOLETE: was clock ticks */
    list.v.list[5].type = TYPE_OBJ;
    list.v.list[5].v.obj = ft.a.progr;
    list.v.list[6] = anonymizing_var_ref(ft.a.vloc, progr);
    list.v.list[7].type = TYPE_STR;
    list.v.list[7].v.str = str_ref(ft.a.verbname);
    list.v.list[8].type = TYPE_INT;
    list.v.list[8].v.num = find_line_number(ft.program, ft.f_index, 0);
    list.v.list[9] = anonymizing_var_ref(ft.a._this, progr);
    list.v.list[10].type = TYPE_INT;
    list.v.list[10].v.num = forked_task_bytes(ft);

    if (include_variables)
        list.v.list[11] = make_rt_var_map(ft.a.rt_env, ft.a.prog->var_names, ft.a.prog->num_var_names);

    return list;
}

static int
suspended_task_bytes(vm the_vm)
{
    int total = sizeof(vmstruct);
    unsigned int i;

    for (i = 0; i <= the_vm->top_activ_stack; i++)
        total += activation_bytes(the_vm->activ_stack + i);

    return total;
}

static Var
list_for_vm(vm the_vm, Objid progr, int include_variables)
{
    Var list;

    list = new_list(include_variables ? 11 : 10);

    list.v.list[1].type = TYPE_INT;
    list.v.list[1].v.num = the_vm->task_id;

    list.v.list[3].type = TYPE_INT;
    list.v.list[3].v.num = 0;           /* OBSOLETE: was clock ID */
    list.v.list[4].type = TYPE_INT;
    list.v.list[4].v.num = DEFAULT_BG_TICKS;    /* OBSOLETE: was clock ticks */
    list.v.list[5].type = TYPE_OBJ;
    list.v.list[5].v.obj = progr_of_cur_verb(the_vm);
    list.v.list[6] = anonymizing_var_ref(top_activ(the_vm).vloc, progr);
    list.v.list[7].type = TYPE_STR;
    list.v.list[7].v.str = str_ref(top_activ(the_vm).verbname);
    list.v.list[8].type = TYPE_INT;
    list.v.list[8].v.num = suspended_lineno_of_vm(the_vm);
    list.v.list[9] = anonymizing_var_ref(top_activ(the_vm)._this, progr);
    list.v.list[10].type = TYPE_INT;
    list.v.list[10].v.num = suspended_task_bytes(the_vm);

    if (include_variables) {
        list.v.list[11] = make_rt_var_map(top_activ(the_vm).rt_env, top_activ(the_vm).prog->var_names, top_activ(the_vm).prog->num_var_names);
    }
    return list;
}

static Var
list_for_suspended_task(suspended_task st, Objid progr, int include_variables)
{
    Var list;

    list = list_for_vm(st.the_vm, progr, include_variables);
    list.v.list[2].type = TYPE_INT;
    list.v.list[2].v.num = ROUND(&st.start_tv);

    return list;
}

static Var
list_for_reading_task(Objid player, vm the_vm, Objid progr, int include_variables)
{
    Var list;

    list = list_for_vm(the_vm, progr, include_variables);
    list.v.list[2].type = TYPE_INT;
    list.v.list[2].v.num = -1;  /* conventional value */

    list.v.list[5].v.obj = player;

    return list;
}

static task_enum_action
counting_closure(vm the_vm, const char *status, void *data)
{
    struct qcl_data *qdata = (struct qcl_data *)data;

    if (qdata->show_all || qdata->progr == progr_of_cur_verb(the_vm))
        qdata->i++;

    return TEA_CONTINUE;
}

static task_enum_action
listing_closure(vm the_vm, const char *status, void *data)
{
    struct qcl_data *qdata = (struct qcl_data *)data;
    Var list;

    if (qdata->show_all || qdata->progr == progr_of_cur_verb(the_vm)) {
        list = list_for_vm(the_vm, qdata->progr, 0);
        list.v.list[2].type = TYPE_STR;
        list.v.list[2].v.str = str_dup(status);
        qdata->tasks.v.list[qdata->i++] = list;
    }

    return TEA_CONTINUE;
}

static task_enum_action
writing_closure(vm the_vm, const char *status, void *data)
{
    struct qcl_data *qdata = (struct qcl_data *)data;

    if (qdata->show_all || qdata->progr == progr_of_cur_verb(the_vm)) {
        dbio_printf("%d %s\n", the_vm->task_id, status);
        write_vm(the_vm);
    }

    return TEA_CONTINUE;
}

static package
bf_queued_tasks(Var arglist, Byte next, void *vdata, Objid progr)
{
    Var tasks;
    int nargs = arglist.v.list[0].v.num;
    int include_variables = (nargs == 1 && is_true(arglist.v.list[1]));
    int show_all = is_wizard(progr);
    tqueue *tq;
    task *t;
    int i, count = 0;
    ext_queue *eq;
    struct qcl_data qdata;

    for (tq = idle_tqueues; tq; tq = tq->next) {
        if (tq->reading && (show_all || tq->player == progr))
            count++;
    }

    for (tq = active_tqueues; tq; tq = tq->next) {
        if (tq->reading && (show_all || tq->player == progr))
            count++;

        for (t = tq->first_bg; t; t = t->next)
            if (show_all
                    || (t->kind == TASK_FORKED
                        ? t->t.forked.a.progr == progr
                        : progr_of_cur_verb(t->t.suspended.the_vm) == progr))
                count++;
    }

    for (t = waiting_tasks; t; t = t->next)
        if (show_all
                || (t->kind == TASK_FORKED
                    ? t->t.forked.a.progr == progr
                    : progr_of_cur_verb(t->t.suspended.the_vm) == progr))
            count++;

    qdata.progr = progr;
    qdata.show_all = show_all;
    qdata.i = count;
    for (eq = external_queues; eq; eq = eq->next)
        (*eq->enumerator) (counting_closure, &qdata);
    count = qdata.i;

    tasks = new_list(count);
    i = 1;

    for (tq = idle_tqueues; tq; tq = tq->next) {
        if (tq->reading && (show_all || tq->player == progr))
            tasks.v.list[i++] = list_for_reading_task(tq->player,
                                tq->reading_vm,
                                progr, include_variables);
    }

    for (tq = active_tqueues; tq; tq = tq->next) {
        if (tq->reading && (show_all || tq->player == progr))
            tasks.v.list[i++] = list_for_reading_task(tq->player,
                                tq->reading_vm,
                                progr, include_variables);

        for (t = tq->first_bg; t; t = t->next)
            if (t->kind == TASK_FORKED && (show_all
                                           || t->t.forked.a.progr == progr))
                tasks.v.list[i++] = list_for_forked_task(t->t.forked,
                                    progr, include_variables);
            else if (t->kind == TASK_SUSPENDED
                     && (show_all
                         || progr_of_cur_verb(t->t.suspended.the_vm) == progr))
                tasks.v.list[i++] = list_for_suspended_task(t->t.suspended,
                                    progr, include_variables);
    }

    for (t = waiting_tasks; t; t = t->next) {
        if (t->kind == TASK_FORKED && (show_all ||
                                       t->t.forked.a.progr == progr))
            tasks.v.list[i++] = list_for_forked_task(t->t.forked,
                                progr, include_variables);
        else if (t->kind == TASK_SUSPENDED
                 && (progr_of_cur_verb(t->t.suspended.the_vm) == progr
                     || show_all))
            tasks.v.list[i++] = list_for_suspended_task(t->t.suspended,
                                progr, include_variables);
    }

    qdata.tasks = tasks;
    qdata.i = i;
    for (eq = external_queues; eq; eq = eq->next)
        (*eq->enumerator) (listing_closure, &qdata);

    free_var(arglist);
    return make_var_pack(tasks);
}

struct fcl_data {
    int id;
    vm the_vm;
};

static task_enum_action
finding_closure(vm the_vm, const char *status, void *data)
{
    struct fcl_data *fdata = (struct fcl_data *)data;

    if (the_vm->task_id == fdata->id) {
        fdata->the_vm = the_vm;
        return TEA_STOP;
    }
    return TEA_CONTINUE;
}

vm
find_suspended_task(int id)
{
    tqueue *tq;
    task *t;
    ext_queue *eq;
    struct fcl_data fdata;

    for (t = waiting_tasks; t; t = t->next)
        if (t->kind == TASK_SUSPENDED && t->t.suspended.the_vm->task_id == id)
            return t->t.suspended.the_vm;

    for (tq = idle_tqueues; tq; tq = tq->next)
        if (tq->reading && tq->reading_vm->task_id == id)
            return tq->reading_vm;

    for (tq = active_tqueues; tq; tq = tq->next) {
        if (tq->reading && tq->reading_vm->task_id == id)
            return tq->reading_vm;

        for (t = tq->first_bg; t; t = t->next)
            if (t->kind == TASK_SUSPENDED
                    && t->t.suspended.the_vm->task_id == id)
                return t->t.suspended.the_vm;
    }

    fdata.id = id;

    for (eq = external_queues; eq; eq = eq->next)
        switch ((*eq->enumerator) (finding_closure, &fdata)) {
            case TEA_CONTINUE:
                /* Do nothing; continue searching other queues */
                break;
            case TEA_KILL:
                panic_moo("Can't happen in FIND_SUSPENDED_TASK!");
            case TEA_STOP:
                return fdata.the_vm;
        }

    return nullptr;
}

struct kcl_data {
    int id;
    Objid owner;
};

static task_enum_action
killing_closure(vm the_vm, const char *status, void *data)
{
    struct kcl_data *kdata = (struct kcl_data *)data;

    if (the_vm->task_id == kdata->id) {
        if (is_wizard(kdata->owner)
                || progr_of_cur_verb(the_vm) == kdata->owner) {
            free_vm(the_vm, 1);
            return TEA_KILL;
        } else
            return TEA_STOP;
    }
    return TEA_CONTINUE;
}

static enum error
kill_task(int id, Objid owner)
{
    task **tt;
    tqueue *tq;

    if (id == current_task_id) {
        return E_NONE;
    }
    for (tt = &waiting_tasks; *tt; tt = &((*tt)->next)) {
        task *t = *tt;
        Objid progr;

        if (t->kind == TASK_FORKED && t->t.forked.id == id)
            progr = t->t.forked.a.progr;
        else if (t->kind == TASK_SUSPENDED
                 && t->t.suspended.the_vm->task_id == id)
            progr = progr_of_cur_verb(t->t.suspended.the_vm);
        else
            continue;

        if (!is_wizard(owner) && owner != progr)
            return E_PERM;
        tq = find_tqueue(progr, 0);
        if (tq)
            tq->num_bg_tasks--;
        *tt = t->next;
        free_task(t, 1);
        return E_NONE;
    }

    for (tq = idle_tqueues; tq; tq = tq->next) {
        if (tq->reading && tq->reading_vm->task_id == id) {
            if (!is_wizard(owner) && owner != tq->player)
                return E_PERM;
            free_vm(tq->reading_vm, 1);
            tq->reading = 0;
            tq->parsing = 0;
            if (tq->parsing_state != nullptr)
                reset_http_parsing_state(tq->parsing_state);
            return E_NONE;
        }
    }

    for (tq = active_tqueues; tq; tq = tq->next) {

        if (tq->reading && tq->reading_vm->task_id == id) {
            if (!is_wizard(owner) && owner != tq->player)
                return E_PERM;
            free_vm(tq->reading_vm, 1);
            tq->reading = 0;
            tq->parsing = 0;
            if (tq->parsing_state != nullptr)
                reset_http_parsing_state(tq->parsing_state);
            return E_NONE;
        }
        for (tt = &(tq->first_bg); *tt; tt = &((*tt)->next)) {
            task *t = *tt;

            if ((t->kind == TASK_FORKED && t->t.forked.id == id)
                    || (t->kind == TASK_SUSPENDED
                        && t->t.suspended.the_vm->task_id == id)) {
                if (!is_wizard(owner) && owner != tq->player)
                    return E_PERM;
                *tt = t->next;
                if (t->next == nullptr)
                    tq->last_bg = tt;
                tq->num_bg_tasks--;
                free_task(t, 1);
                return E_NONE;
            }
        }
    }

    {
        struct kcl_data kdata;
        ext_queue *eq;

        kdata.id = id;
        kdata.owner = owner;
        for (eq = external_queues; eq; eq = eq->next)
            switch ((*eq->enumerator) (killing_closure, &kdata)) {
                case TEA_CONTINUE:
                    /* Do nothing; continue searching other queues */
                    break;
                case TEA_KILL:
                    return E_NONE;
                case TEA_STOP:
                    return E_PERM;
            }
    }

    return E_INVARG;
}

static package
bf_kill_task(Var arglist, Byte next, void *vdata, Objid progr)
{
    int id = arglist.v.list[1].v.num;
    enum error e = kill_task(id, progr);

    free_var(arglist);
    if (e != E_NONE)
        return make_error_pack(e);
    else if (id == current_task_id)
        return make_abort_pack(ABORT_KILL);

    return no_var_pack();
}

static enum error
do_resume(int id, Var value, Objid progr)
{
    task **tt;
    tqueue *tq;

    for (tt = &waiting_tasks; *tt; tt = &((*tt)->next)) {
        task *t = *tt;
        Objid owner;

        if (t->kind == TASK_SUSPENDED && t->t.suspended.the_vm->task_id == id)
            owner = progr_of_cur_verb(t->t.suspended.the_vm);
        else
            continue;

        if (!is_wizard(progr) && progr != owner)
            return E_PERM;
        gettimeofday(&t->t.suspended.start_tv, nullptr);    /* runnable now */
        free_var(t->t.suspended.value);
        t->t.suspended.value = value;
        tq = find_tqueue(owner, 1);
        *tt = t->next;
        ensure_usage(tq);
        enqueue_bg_task(tq, t);
        return E_NONE;
    }

    for (tq = active_tqueues; tq; tq = tq->next) {
        for (tt = &(tq->first_bg); *tt; tt = &((*tt)->next)) {
            task *t = *tt;

            if (t->kind == TASK_SUSPENDED
                    && t->t.suspended.the_vm->task_id == id) {
                if (!is_wizard(progr) && progr != tq->player)
                    return E_PERM;
                /* already resumed, but we have a new value for it */
                free_var(t->t.suspended.value);
                t->t.suspended.value = value;
                return E_NONE;
            }
        }
    }

    return E_INVARG;
}

static package
bf_resume(Var arglist, Byte next, void *vdata, Objid progr)
{
    int nargs = arglist.v.list[0].v.num;
    Var value;
    enum error e;

    value = (nargs >= 2 ? var_ref(arglist.v.list[2]) : zero);
    e = do_resume(arglist.v.list[1].v.num, value, progr);
    free_var(arglist);
    if (e != E_NONE) {
        free_var(value);
        return make_error_pack(e);
    }
    return no_var_pack();
}

static package
bf_output_delimiters(Var arglist, Byte next, void *vdata, Objid progr)
{
    Var r;
    Objid player = arglist.v.list[1].v.obj;

    free_var(arglist);

    if (!is_wizard(progr) && progr != player)
        return make_error_pack(E_PERM);
    else {
        const char *prefix, *suffix;
        tqueue *tq = find_tqueue(player, 0);

        if (!tq || !tq->connected)
            return make_error_pack(E_INVARG);

        if (tq->output_prefix)
            prefix = tq->output_prefix;
        else
            prefix = "";

        if (tq->output_suffix)
            suffix = tq->output_suffix;
        else
            suffix = "";

        r = new_list(2);
        r.v.list[1].type = r.v.list[2].type = TYPE_STR;
        r.v.list[1].v.str = str_dup(prefix);
        r.v.list[2].v.str = str_dup(suffix);
    }
    return make_var_pack(r);
}

static package
bf_force_input(Var arglist, Byte next, void *vdata, Objid progr)
{   /* (conn, string [, at_front]) */
    Objid conn = arglist.v.list[1].v.obj;
    const char *line = arglist.v.list[2].v.str;
    int at_front = (arglist.v.list[0].v.num > 2
                    && is_true(arglist.v.list[3]));
    tqueue *tq;

    if (!is_wizard(progr) && progr != conn) {
        free_var(arglist);
        return make_error_pack(E_PERM);
    }
    tq = find_tqueue(conn, 1);
    enqueue_input_task(tq, line, at_front, 0/*non-binary*/, 0 /*non-telnet*/);
    free_var(arglist);
    return no_var_pack();
}

static package
bf_flush_input(Var arglist, Byte next, void *vdata, Objid progr)
{   /* (conn [, show_messages]) */
    Objid conn = arglist.v.list[1].v.obj;
    int show_messages = (arglist.v.list[0].v.num > 1
                         && is_true(arglist.v.list[2]));
    tqueue *tq;

    if (!is_wizard(progr) && progr != conn) {
        free_var(arglist);
        return make_error_pack(E_PERM);
    }
    tq = find_tqueue(conn, 1);
    flush_input(tq, show_messages);
    free_var(arglist);
    return no_var_pack();
}

static package
bf_set_task_local(Var arglist, Byte next, void *vdata, Objid progr)
{   /* (ANY value) */
    if (!is_wizard(progr)) {
        free_var(arglist);
        return make_error_pack(E_PERM);
    }

    Var v = var_ref(arglist.v.list[1]);

    free_var(current_local);
    current_local = v;

    free_var(arglist);
    return no_var_pack();
}

static package
bf_task_local(Var arglist, Byte next, void *vdata, Objid progr)
{
    if (!is_wizard(progr)) {
        free_var(arglist);
        return make_error_pack(E_PERM);
    }

    Var v = var_ref(current_local);

    free_var(arglist);
    return make_var_pack(v);
}

/* Concept courtesy of Ryan Smith (http://zanosoft.net/rsgames/moo-switchcon/).
 */
static package
bf_switch_player(Var arglist, Byte next, void *vdata, Objid progr)
{
    Objid old_player = arglist.v.list[1].v.obj;
    Objid new_player = arglist.v.list[2].v.obj;

    free_var(arglist);

    if (!is_wizard(progr))
        return make_error_pack(E_PERM);

    if (old_player == new_player)
        return make_error_pack(E_INVARG);

    if (is_player_connected(old_player) == 0)
        return make_error_pack(E_INVARG);

    if (is_user(new_player) == 0)
        return make_error_pack(E_INVARG);

    tqueue *tq = find_tqueue(old_player, 0);
    if (!tq)
        return make_error_pack(E_INVARG);

    tqueue *dead_tq = find_tqueue(new_player, 0);
    task *t;

    tq->player = new_player;
    if (tq->num_bg_tasks) {
        /* Cute; this un-logged-in connection has some queued tasks!
         * Must copy them over to their own tqueue for accounting...
         */
        tqueue *old_tq = find_tqueue(old_player, 1);

        old_tq->num_bg_tasks = tq->num_bg_tasks;
        while ((t = dequeue_bg_task(tq)) != nullptr)
            enqueue_bg_task(old_tq, t);
        tq->num_bg_tasks = 0;
    }
    if (dead_tq) {
        /* Copy over tasks from old queue for player */
        tq->num_bg_tasks = dead_tq->num_bg_tasks;
        while ((t = dequeue_input_task(dead_tq, DQ_FIRST)) != nullptr) {
            free_task(t, 0);
        }
        while ((t = dequeue_bg_task(dead_tq)) != nullptr) {
            enqueue_bg_task(tq, t);
        }
        dead_tq->player = NOTHING;  /* it'll be freed by run_ready_tasks */
        dead_tq->num_bg_tasks = 0;
    }

    player_connected_silent(old_player, new_player);

    boot_player(old_player);

    return no_var_pack();
}

#ifdef SAVE_FINISHED_TASKS
static package
bf_finished_tasks(Var arglist, Byte next, void *vdata, Objid progr)
{
    free_var(arglist);

    if (!is_wizard(progr))
        return make_error_pack(E_PERM);

    return make_var_pack(var_ref(finished_tasks));
}
#endif

static package
bf_set_thread_mode(Var arglist, Byte next, void *vdata, Objid progr)
{
    if (arglist.v.list[0].v.num == 0) {
        Var ret;
        ret.type = TYPE_INT;
        ret.v.num = get_thread_mode();
        free_var(arglist);
        return make_var_pack(ret);
    } else {
        set_thread_mode(is_true(arglist.v.list[1]));
        free_var(arglist);
        return no_var_pack();
    }
}

void
register_tasks(void)
{
    register_function("task_id", 0, 0, bf_task_id);
    register_function("queued_tasks", 0, 1, bf_queued_tasks, TYPE_INT);
#ifdef SAVE_FINISHED_TASKS
    register_function("finished_tasks", 0, 0, bf_finished_tasks);
#endif
    register_function("kill_task", 1, 1, bf_kill_task, TYPE_INT);
    register_function("output_delimiters", 1, 1, bf_output_delimiters,
                      TYPE_OBJ);
    register_function("queue_info", 0, 1, bf_queue_info, TYPE_OBJ);
    register_function("resume", 1, 2, bf_resume, TYPE_INT, TYPE_ANY);
    register_function("force_input", 2, 3, bf_force_input,
                      TYPE_OBJ, TYPE_STR, TYPE_ANY);
    register_function("flush_input", 1, 2, bf_flush_input, TYPE_OBJ, TYPE_ANY);
    register_function("set_task_local", 1, 1, bf_set_task_local, TYPE_ANY);
    register_function("task_local", 0, 0, bf_task_local);
    register_function("switch_player", 2, 2, bf_switch_player,
                      TYPE_OBJ, TYPE_OBJ);
    register_function("set_thread_mode", 0, 1, bf_set_thread_mode, TYPE_INT);
}
