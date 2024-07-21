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

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>       // getrusage
#include <sys/resource.h>   // getrusage
#if !defined(__FreeBSD__) && !defined(__MACH__)
#include <sys/sysinfo.h>    // CPU usage
#endif
#ifdef __MACH__
#include <mach/mach.h>
#include <sys/sysctl.h>
#endif

#include <string>
#include <sstream>
#include <fstream>
#include <vector>
#include <mutex>
#include <getopt.h>
#include <sys/types.h>      /* must be first on some systems */
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <netinet/in.h>

#include "config.h"
#include "db.h"
#include "db_io.h"
#include "disassemble.h"
#include "exec.h"
#include "execute.h"
#include "functions.h"
#ifdef ENABLE_GC
#include "garbage.h"
#endif
#include "list.h"
#include "log.h"
#include "map.h"
#include <nettle/sha2.h>
#include "network.h"
#include "numbers.h"
#include "options.h"
#include "parser.h"
#include "quota.h"
#include "random.h"
#include "server.h"
#include "storage.h"
#include "streams.h"
#include "structures.h"
#include "tasks.h"
#include "timers.h"
#include "unparse.h"
#include "utils.h"
#include "version.h"
#include "waif.h" /* destroyed_waifs */
#include "curl.h" /* curl shutdown */
#include "background.h"
#include "map.h"
#include "pcre_moo.h" /* pcre shutdown */

#ifdef JEMALLOC_FOUND
#include <jemalloc/jemalloc.h>
#endif

extern "C" {
#include "dependencies/linenoise.h"
}

#define RANDOM_DEVICE "/dev/urandom"

static pid_t parent_pid;
static bool in_child = false;

static const char *this_program;

static std::stringstream shutdown_message;

static std::atomic<bool> shutdown_triggered(false);
static bool in_emergency_mode = false;

static Var checkpointed_connections;

typedef enum {
    CHKPT_OFF, CHKPT_TIMER, CHKPT_SIGNAL, CHKPT_FUNC
} Checkpoint_Reason;
static Checkpoint_Reason checkpoint_requested = CHKPT_OFF;

static int checkpoint_finished = 0; /* 1 = failure, 2 = success */

static bool reopen_logfile_requested = false;

static void handle_user_defined_signal(int sig);

#ifdef OUTBOUND_NETWORK
int outbound_network_enabled = OUTBOUND_NETWORK;
#else
int outbound_network_enabled = false;
#endif

#ifdef USE_TLS
const char *default_certificate_path = DEFAULT_TLS_CERT;
const char *default_key_path = DEFAULT_TLS_KEY;
#endif

int clear_last_move = false;
char *bind_ipv4 = nullptr;
char *bind_ipv6 = nullptr;
char *file_subdir = FILE_SUBDIR;
char *exec_subdir = EXEC_SUBDIR;

typedef struct shandle {
    struct shandle *next, **prev;
    network_handle nhandle;
    time_t connection_time;
    time_t last_activity_time;
    Objid player;
    Objid listener;
    task_queue tasks;
    Objid switched;
    bool outbound, binary;
    bool print_messages;
    std::atomic<bool> disconnect_me;
} shandle;

static shandle *all_shandles = nullptr;
std::recursive_mutex all_shandles_mutex;

typedef struct slistener {
    Var desc;
    struct slistener *next, **prev;
    const char *name;           // resolved hostname
    const char *ip_addr;        // 'raw' IP address
    network_listener nlistener;
    Objid oid;          /* listen(OID, DESC, PRINT_MESSAGES, IPV6) */
    int print_messages;
    uint16_t port;             // listening port
    bool ipv6;
} slistener;

static slistener *all_slisteners = nullptr;

server_listener null_server_listener = {nullptr};

struct pending_recycle {
    Var v;
    struct pending_recycle *next;
};

static struct pending_recycle *pending_free = nullptr;
static struct pending_recycle *pending_head = nullptr;
static struct pending_recycle *pending_tail = nullptr;
static unsigned int pending_count = 0;

/* used once when the server loads the database */
static Var pending_list = new_list(0);

/* maplookup doesn't consume the key, so here are common map keys that
   are used by functions like listen() and open_network_connection() */
static Var ipv6_key = str_dup_to_var("ipv6");
static Var interface_key = str_dup_to_var("interface");
#ifdef USE_TLS
static Var tls_key = str_dup_to_var("TLS");
#endif

static void
free_shandle(shandle * h)
{
    all_shandles_mutex.lock();
    *(h->prev) = h->next;
    if (h->next)
        h->next->prev = h->prev;
    all_shandles_mutex.unlock();

    free_task_queue(h->tasks);

    myfree(h, M_NETWORK);
}

static slistener *
new_slistener(Objid oid, Var desc, int print_messages, enum error *ee, bool use_ipv6, const char *interface USE_TLS_BOOL_DEF TLS_CERT_PATH_DEF)
{
    slistener *listener = (slistener *)mymalloc(sizeof(slistener), M_NETWORK);
    server_listener sl;
    enum error e;
    const char *name, *ip_address;
    uint16_t port;

    sl.ptr = listener;
    e = network_make_listener(sl, desc, &(listener->nlistener), &name, &ip_address, &port, use_ipv6, interface USE_TLS_BOOL TLS_CERT_PATH);

    if (ee)
        *ee = e;

    if (e != E_NONE) {
        myfree(listener, M_NETWORK);
        return nullptr;
    }

    listener->oid = oid;
    listener->print_messages = print_messages;
    listener->name = name;                      // original copy
    listener->ipv6 = use_ipv6;
    listener->ip_addr = ip_address;             // original copy
    listener->port = port;
    listener->desc = var_ref(desc);

    listener->next = all_slisteners;
    listener->prev = &all_slisteners;
    if (all_slisteners)
        all_slisteners->prev = &(listener->next);
    all_slisteners = listener;

    return listener;
}

static int
start_listener(slistener * l)
{
    if (network_listen(l->nlistener)) {
        oklog("LISTEN: #%" PRIdN " now listening on %s [%s], port %i\n", l->oid, l->name, l->ip_addr, l->port);
        return 1;
    } else {
        errlog("LISTEN: Can't start #%" PRIdN " listening on %s [%s], port %i\n", l->oid, l->name, l->ip_addr, l->port);
        return 0;
    }
}

static void
free_slistener(slistener * l)
{
    network_close_listener(l->nlistener);
    oklog("UNLISTEN: #%" PRIdN " no longer listening on %s\n", l->oid, l->name);

    *(l->prev) = l->next;
    if (l->next)
        l->next->prev = l->prev;

    free_var(l->desc);
    free_str(l->name);
    free_str(l->ip_addr);

    myfree(l, M_NETWORK);
}

static void
send_shutdown_message(const char *message)
{
    shandle *h;
    std::stringstream s;

    s << "*** Shutting down: " << message << " ***";

    for (h = all_shandles; h; h = h->next)
        network_send_line(h->nhandle, s.str().c_str(), 1, 1);
}

static void
abort_server(void)
{
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    signal(SIGFPE, SIG_DFL);
    signal(SIGHUP, SIG_DFL);
    signal(SIGILL, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGSEGV, SIG_DFL);
#ifdef SIGBUS
    signal(SIGBUS, SIG_DFL);
#endif
    signal(SIGUSR1, SIG_DFL);
    signal(SIGUSR2, SIG_DFL);
    signal(SIGCHLD, SIG_DFL);

    abort();
}

static void
output_to_log(const char *line)
{
    errlog("%s\n", line);
}

void
panic_moo(const char *message)
{
    static int in_panic = 0;

    errlog("PANIC%s: %s\n", in_child ? " (in child)" : "", message);
    if (in_panic) {
        errlog("RECURSIVE PANIC: aborting\n");
        abort_server();
    }
    in_panic = 1;

    log_command_history();

    if (in_child) {     /* We're a forked checkpointer */
        errlog("Child shutting down parent via INT signal\n");
        kill(parent_pid, SIGINT);
        _exit(1);
    }
    print_error_backtrace("server panic", output_to_log);
    send_shutdown_message("server panic");
    network_shutdown();
    db_flush(FLUSH_PANIC);

    abort_server();
}

enum Fork_Result
fork_server(const char *subtask_name)
{
    pid_t pid;
    std::stringstream s;

    s << "Forking " << subtask_name;

    pid = fork();
    if (pid < 0) {
        log_perror(s.str().c_str());
        return FORK_ERROR;
    } else if (pid == 0) {
        in_child = true;
        return FORK_CHILD;
    } else {
        return FORK_PARENT;
    }
}

static void
panic_signal(int sig)
{
    char message[100];

    sprintf(message, "Caught signal %d", sig);
    panic_moo(message);
}

static void
shutdown_signal(int sig)
{
    shutdown_triggered = true;
    shutdown_message << "shutdown signal received";
}

static void
logfile_signal()
{
    if (get_log_file())
        reopen_logfile_requested = true;
}

static void
checkpoint_signal(int sig)
{
    checkpoint_requested = CHKPT_SIGNAL;

    signal(sig, handle_user_defined_signal);
}

static void
handle_user_defined_signal(int sig)
{
    Var args, result;

    args = new_list(1);
    args.v.list[1].type = TYPE_STR;
    args.v.list[1].v.str = str_dup(sig == SIGUSR1 ? "SIGUSR1" : "SIGUSR2");

    if (run_server_task(-1, Var::new_obj(SYSTEM_OBJECT), "handle_signal", args, "", &result) != OUTCOME_DONE || is_true(result)) {
        /* :handle_signal returned true; do nothing. */
    } else if (sig == SIGUSR1) {    /* reopen logfile */
        logfile_signal();
    } else if (sig == SIGUSR2) {    /* remote checkpoint signal */
        checkpoint_signal(sig);
    }

    free_var(result);
}

static void
call_checkpoint_notifier(int successful)
{
    Var args;

    args = new_list(1);
    args.v.list[1].type = TYPE_INT;
    args.v.list[1].v.num = successful;
    run_server_task(-1, Var::new_obj(SYSTEM_OBJECT), "checkpoint_finished", args, "", nullptr);
}

static void
child_completed_signal(int sig)
{
    int tmp_errno = errno;
    pid_t p;
    pid_t checkpoint_child = 0;
    int status;

    /* Signal every child's completion to the exec subsystem and let
     * it decide if it's relevant.
     */
#if HAVE_WAITPID

    while ((p = waitpid(-1, &status, WNOHANG)) > 0) {
        if (!exec_complete(p, WEXITSTATUS(status)))
            checkpoint_child = p;
    }
#else
#if HAVE_WAIT3
    while ((p = wait3(&status, WNOHANG, 0)) > 0) {
        if (!exec_complete(p, WEXITSTATUS(status)))
            checkpoint_child = p;
    }
#else
#if HAVE_WAIT2
    while ((p = wait2(&status, WNOHANG)) > 0) {
        if (!exec_complete(p, WEXITSTATUS(status)))
            checkpoint_child = p;
    }
#else
    p = wait(&status);
    if (!exec_complete(p, WEXITSTATUS(status)))
        checkpoint_child = p;
#endif
#endif
#endif

    signal(sig, child_completed_signal);

    if (checkpoint_child)
        checkpoint_finished = (status == 0) + 1;    /* 1 = failure, 2 = success */

    errno = tmp_errno;
}

static void
setup_signals(void)
{
    signal(SIGFPE, SIG_IGN);
    if (signal(SIGHUP, panic_signal) == SIG_IGN)
        signal(SIGHUP, SIG_IGN);
    signal(SIGILL, panic_signal);
    signal(SIGQUIT, panic_signal);
    signal(SIGSEGV, panic_signal);
#ifdef SIGBUS
    signal(SIGBUS, panic_signal);
#endif

    signal(SIGINT, shutdown_signal);
    signal(SIGTERM, shutdown_signal);
    signal(SIGUSR1, handle_user_defined_signal);
    signal(SIGUSR2, handle_user_defined_signal);

    signal(SIGCHLD, child_completed_signal);
}

static void
checkpoint_timer(Timer_ID id, Timer_Data data)
{
    checkpoint_requested = CHKPT_TIMER;
}

static void
set_checkpoint_timer(int first_time)
{
    int interval, now = time(nullptr);
    static Timer_ID last_checkpoint_timer;

    interval = server_int_option("dump_interval", 3600);
    if (interval < 60 || now + interval < now) {
        interval = 3600;    /* Once per hour */
    }

    if (!first_time)
        cancel_timer(last_checkpoint_timer);
    last_checkpoint_timer = set_timer(interval, checkpoint_timer, nullptr);
}

static const char *
object_name(Objid oid)
{
    static Stream *s = nullptr;

    if (!s)
        s = new_stream(30);

    if (valid(oid))
        stream_printf(s, "%s (#%" PRIdN ")", db_object_name(oid), oid);
    else
        stream_printf(s, "#%" PRIdN "", oid);

    return reset_stream(s);
}

static void
call_notifier(Objid player, Objid handler, const char *verb_name)
{
    Var args;

    args = new_list(1);
    args.v.list[1].type = TYPE_OBJ;
    args.v.list[1].v.obj = player;
    run_server_task(player, Var::new_obj(handler), verb_name, args, "", nullptr);
}

int
get_server_option(Objid oid, const char *name, Var * r)
{
    if (((valid(oid) &&
            db_find_property(Var::new_obj(oid), "server_options", r).ptr)
            || (valid(SYSTEM_OBJECT) &&
                db_find_property(Var::new_obj(SYSTEM_OBJECT), "server_options", r).ptr))
            && r->type == TYPE_OBJ
            && valid(r->v.obj)
            && db_find_property(*r, name, r).ptr)
        return 1;

    return 0;
}

static void
send_message(Objid listener, network_handle nh, const char *msg_name, ...)
{
    va_list args;
    Var msg;
    const char *line;

    va_start(args, msg_name);
    if (get_server_option(listener, msg_name, &msg)) {
        if (msg.type == TYPE_STR)
            network_send_line(nh, msg.v.str, 1, 1);
        else if (msg.type == TYPE_LIST) {
            int i;

            for (i = 1; i <= msg.v.list[0].v.num; i++)
                if (msg.v.list[i].type == TYPE_STR)
                    network_send_line(nh, msg.v.list[i].v.str, 1, 1);
        }
    } else          /* Use default message */
        while ((line = va_arg(args, const char *)) != 0)
            network_send_line(nh, line, 1, 1);

    va_end(args);
}

/* Queue an anonymous object for eventual recycling.  This is the
 * entry-point for anonymous objects that lose all references (see
 * utils.c), and for anonymous objects that the garbage collector
 * schedules for cycle busting (see garbage.c).  Objects added to the
 * queue are var_ref'd to increment the refcount.  This prevents the
 * garbage collector from recycling if the object makes its way onto
 * the list of roots.  After they are recycled, they are freed.
 */
static int
queue_includes(Var v)
{
    struct pending_recycle *head = pending_head;

    while (head) {
        if (head->v.v.anon == v.v.anon)
            return 1;
        head = head->next;
    }

    return 0;
}

void
queue_anonymous_object(Var v)
{
    assert(TYPE_ANON == v.type);
    assert(!db_object_has_flag2(v, FLAG_RECYCLED));
    assert(!db_object_has_flag2(v, FLAG_INVALID));
    assert(!queue_includes(v));

    if (!pending_free) {
        pending_free = (struct pending_recycle *)mymalloc(sizeof(struct pending_recycle), M_STRUCT);
        pending_free->next = nullptr;
    }

    struct pending_recycle *next = pending_free;
    pending_free = next->next;

    next->v = var_ref(v);
    next->next = pending_head;
    pending_head = next;

    if (!pending_tail)
        pending_tail = next;

    pending_count++;
}

static void
recycle_anonymous_objects(void)
{
    if (!pending_head)
        return;

    struct pending_recycle *next, *head = pending_head;
    pending_head = pending_tail = nullptr;
    pending_count = 0;

    while (head) {
        Var v = head->v;

        assert(TYPE_ANON == v.type);

        next = head->next;
        head->next = pending_free;
        pending_free = head;
        head = next;

        assert(!db_object_has_flag2(v, FLAG_RECYCLED));
        assert(!db_object_has_flag2(v, FLAG_INVALID));

        db_set_object_flag2(v, FLAG_RECYCLED);

        /* the best approximation I could think of */
        run_server_task(-1, v, "recycle", new_list(0), "", nullptr);

        /* We'd like to run `db_change_parents()' to be consistent
         * with the pattern laid out in `bf_recycle()', but we can't
         * because the object can be invalid at this point due to
         * changes in parentage.
         */
        /*db_change_parents(v, nothing, none);*/

        incr_quota(db_object_owner2(v));

        db_destroy_anonymous_object(v.v.anon);

        free_var(v);
    }
}

static void
recycle_waifs(void)
{
    /* This seems like a lot of work to go through just to get a destroy verb name.
     * Maybe it should just be a #define in waif.h? Ah well.*/
    static char *waif_recycle_verb = nullptr;
    if (!waif_recycle_verb) {
        waif_recycle_verb = (char *)mymalloc(9, M_STRING);
        waif_recycle_verb[0] = WAIF_VERB_PREFIX;
        strcpy(waif_recycle_verb + 1, "recycle");
    }

    std::vector<Waif*> removals;
    for (auto &x : destroyed_waifs) {
        if (x.second == false) {
            run_server_task(-1, Var::new_waif(x.first), waif_recycle_verb, new_list(0), "", nullptr);
            x.second = true;
            /* Flag it as destroyed. Now we just wait for the refcount to hit zero so we can free it. */
        }
        if (refcount(x.first) <= 0) {
            removals.push_back(x.first);
        }
    }
    for (auto x : removals) {
        destroyed_waifs.erase(x);
        free_waif(x);
    }
}

/* When the server checkpoints, all of the objects pending recycling
 * are written to the database.  It is not safe to simply forget about
 * these objects because recycling them will call their `recycle()'
 * verb (if defined) which may have the effect of making them non-lost
 * again.
 */

void
write_values_pending_finalization(void)
{
    /* In order to get an accurate count, we have to iterate through destroyed_waifs twice.
       The first time to ascertain which waifs haven't already had their recycle verb called,
       the second time to add them to the list. If this proves problematic, we might have to
       trade off speed for some slightly increased memory usage with waif flags. However,
       I really don't see any database having enough waifs pending recycling for this to make
       any impact whatsoever. */

    unsigned int pending_waif_count = 0;
    for (auto &x : destroyed_waifs)
        if (x.second == false)
            pending_waif_count++;
    
    dbio_printf("%" PRIdN " values pending finalization\n", pending_count + pending_waif_count);

    struct pending_recycle *head = pending_head;

    while (head) {
        dbio_write_var(head->v);
        head = head->next;
    }

    for (auto &x : destroyed_waifs) {
        if (x.second == false)
            dbio_write_var(Var::new_waif(x.first));
    }
}

/* When the server loads the database, the objects pending recycling
 * are read in as well.  However, at the point that this function is
 * called, these objects are empty slots that will hold to-be-built
 * anonymous objects.  By the time `main_loop()' is called and they
 * are to be recycled, they are proper anonymous objects.  In between,
 * just track them.
 */

int
read_values_pending_finalization(void)
{
    int i, count;

    if (dbio_scanf("%d values pending finalization\n", &count) != 1) {
        errlog("READ_VALUES_PENDING_FINALIZATION: Bad count.\n");
        return 0;
    }

    free_var(pending_list);
    pending_list = new_list(count);

    for (i = 1; i <= count; i++) {
        pending_list.v.list[i] = dbio_read_var();
    }

    return 1;
}

static void
main_loop(void)
{
    int i;

    /* First, queue anonymous objects and WAIFs */
    for (i = 1; i <= pending_list.v.list[0].v.num; i++) {
        Var v;

        v = pending_list.v.list[i];

        /* in theory this could be any value... */
        /* in practice this will be an anonymous object... */
        /*... until now! It can also be a WAIF. */
        assert(v.type == TYPE_ANON || v.type == TYPE_WAIF);

    switch (v.type) {
        case TYPE_ANON:
            if (v.v.anon != nullptr)
                queue_anonymous_object(var_ref(v));
            break;
        case TYPE_WAIF:
            if (v.v.waif != nullptr && destroyed_waifs.count(v.v.waif) == 0)
                destroyed_waifs[v.v.waif] = false;
        }
    }

    free_var(pending_list);

    /* Second, notify DB of disconnections for all checkpointed connections */
    for (i = 1; i <= checkpointed_connections.v.list[0].v.num; i++) {
        Var v;

        v = checkpointed_connections.v.list[i];
        call_notifier(v.v.list[1].v.obj, v.v.list[2].v.obj,
                      "user_disconnected");
    }
    free_var(checkpointed_connections);

    /* Third, run #0:server_started() */
    run_server_task(-1, Var::new_obj(SYSTEM_OBJECT), "server_started", new_list(0), "", nullptr);
    set_checkpoint_timer(1);

    /* Now, we enter the main server loop */
    while (!shutdown_triggered) {
        /* Check how long we have until the next task will be ready to run.
         * We only care about three cases (== 0, == 1, and > 1), so we can
         * map a `never' result from the task subsystem into 2.
         */
        int task_useconds = next_task_start();
        int useconds_left = task_useconds < 0 ? 1000000 : task_useconds;
        shandle *h, *nexth;

#ifdef ENABLE_GC
        if (gc_run_called || gc_roots_count > GC_ROOTS_LIMIT
                || checkpoint_requested != CHKPT_OFF)
            gc_collect();
#endif

        if (reopen_logfile_requested) {
            reopen_logfile_requested = false;

            FILE *new_log;
            oklog("LOGFILE: Closing due to remote request signal.\n");

            new_log = fopen(get_log_file_name(), "a");
            if (new_log) {
                fclose(get_log_file());
                set_log_file(new_log);
                oklog("LOGFILE: Reopening due to remote request signal.\n");
            } else {
                log_perror("Error reopening log file");
            }
        }

        if (checkpoint_requested != CHKPT_OFF) {
            if (checkpoint_requested == CHKPT_SIGNAL)
                oklog("CHECKPOINTING due to remote request signal.\n");
            checkpoint_requested = CHKPT_OFF;
            run_server_task(-1, Var::new_obj(SYSTEM_OBJECT), "checkpoint_started",
                            new_list(0), "", nullptr);
            network_process_io(0);
#ifdef UNFORKED_CHECKPOINTS
            call_checkpoint_notifier(db_flush(FLUSH_ALL_NOW));
#else
            if (!db_flush(FLUSH_ALL_NOW))
                call_checkpoint_notifier(0);
#endif
            set_checkpoint_timer(0);
        }
#ifndef UNFORKED_CHECKPOINTS
        if (checkpoint_finished) {
            call_checkpoint_notifier(checkpoint_finished - 1);
            checkpoint_finished = 0;
        }
#endif

        recycle_anonymous_objects();
        recycle_waifs();

        network_process_io(useconds_left);

        run_ready_tasks();

        /* If a exec'd child process exited, deal with it here */
        deal_with_child_exit();

        {   /* Get rid of old un-logged-in or useless connections */
            int now = time(nullptr);

            all_shandles_mutex.lock();
            for (h = all_shandles; h; h = nexth) {
                Var v;

                nexth = h->next;

                /* If the nhandle refcount is > 1, a background thread is working with it.
                 * We don't want to mess with it until that thread is finished. */
                if (get_nhandle_refcount(h->nhandle) > 1)
                  continue;

                if (!h->outbound && h->connection_time == 0
                        && (get_server_option(h->listener, "connect_timeout", &v)
                            ? (v.type == TYPE_INT && v.v.num > 0
                               && now - h->last_activity_time > v.v.num)
                            : (now - h->last_activity_time
                               > DEFAULT_CONNECT_TIMEOUT))) {
                    call_notifier(h->player, h->listener, "user_disconnected");
                    lock_connection_name_mutex(h->nhandle);
                    oklog("TIMEOUT: #%" PRIdN " on %s\n",
                          h->player,
                          network_connection_name(h->nhandle));
                    unlock_connection_name_mutex(h->nhandle);
                    if (h->print_messages)
                        send_message(h->listener, h->nhandle, "timeout_msg",
                                     "*** Timed-out waiting for login. ***",
                                     0);
                    network_close(h->nhandle);
                    free_shandle(h);
                } else if (h->connection_time != 0 && !valid(h->player)) {
                    lock_connection_name_mutex(h->nhandle);
                    oklog("RECYCLED: #%" PRIdN " on %s\n",
                          h->player,
                          network_connection_name(h->nhandle));
                    unlock_connection_name_mutex(h->nhandle);
                    if (h->print_messages)
                        send_message(h->listener, h->nhandle,
                                     "recycle_msg", "*** Recycled ***", 0);
                    network_close(h->nhandle);
                    free_shandle(h);
                } else if (h->disconnect_me) {
                    call_notifier(h->player, h->listener,
                                  "user_disconnected");
                    lock_connection_name_mutex(h->nhandle);
                    oklog("DISCONNECTED: %s on %s\n",
                          object_name(h->player),
                          network_connection_name(h->nhandle));
                    unlock_connection_name_mutex(h->nhandle);
                    if (h->print_messages)
                        send_message(h->listener, h->nhandle, "boot_msg",
                                     "*** Disconnected ***", 0);
                    network_close(h->nhandle);
                    free_shandle(h);
                } else if (h->switched) {
                    if (h->switched != h->player && is_user(h->switched))
                        call_notifier(h->switched, h->listener, "user_disconnected");
                    if (is_user(h->player))
                        call_notifier(h->player, h->listener, h->switched == h->player ? "user_reconnected" : "user_connected");
                    h->switched = 0;
                }
            }
            all_shandles_mutex.unlock();
        }
    }

    applog(LOG_WARNING, "SHUTDOWN: %s\n", shutdown_message.str().c_str());
    send_shutdown_message(shutdown_message.str().c_str());
}

static shandle *
find_shandle(Objid player)
{
    shandle *h;

    std::lock_guard<std::recursive_mutex> lock(all_shandles_mutex);

    for (h = all_shandles; h; h = h->next)
        if (h->player == player)
            return h;

    return nullptr;
}

static char *cmdline_buffer;
static int cmdline_buflen;

static void
init_cmdline(int argc, char *argv[])
{
    char *p;
    int i;

    for (p = argv[0], i = 1;;) {
        if (*p++ == '\0' && (i >= argc || p != argv[i++]))
            break;
    }

    cmdline_buffer = argv[0];
    cmdline_buflen = p - argv[0];
}

#define SERVER_CO_TABLE(DEFINE, H, VALUE, _)                             \
    DEFINE(binary, _, TYPE_INT, num,                                     \
           H->binary,                                                    \
           {                                                             \
                H->binary = is_true(VALUE);                              \
                network_set_connection_binary(H->nhandle, H->binary);    \
           })                                                            \

static int
server_set_connection_option(shandle * h, const char *option, Var value)
{
    CONNECTION_OPTION_SET(SERVER_CO_TABLE, h, option, value);
}

static int
server_connection_option(shandle * h, const char *option, Var * value)
{
    CONNECTION_OPTION_GET(SERVER_CO_TABLE, h, option, value);
}

static Var
server_connection_options(shandle * h, Var list)
{
    CONNECTION_OPTION_LIST(SERVER_CO_TABLE, h, list);
}

#undef SERVER_CO_TABLE

static const char *
read_stdin_line(const char *prompt)
{
    static Stream *s = nullptr;

    if (!s)
        s = new_stream(100);

    fflush(stdout);

    char *line;

    if ((line = linenoise(prompt)) && *line) {
        linenoiseHistoryAdd(line);
        stream_add_string(s, line);
        free(line);
        return reset_stream(s);
    }

    return (char *)"";
}

static void
emergency_notify(Objid player, const char *line)
{
    printf("#%" PRIdN " <- %s\n", player, line);
}

static int
emergency_mode()
{
    const char *line;
    Var words;
    int nargs;
    const char *command;
    Stream *s = new_stream(100);
    Objid wizard = -1;
    int debug = 1;
    int start_ok = -1;

    oklog("EMERGENCY_MODE: Entering mode...\n");
    in_emergency_mode = true;

    printf("\nLambdaMOO Emergency Holographic Wizard Mode\n");
    printf("-------------------------------------------\n");
    printf("\"Please state the nature of the wizardly emergency...\"\n");
    printf("(Type `help' for assistance.)\n\n");

    while (start_ok < 0) {
        /* Find/create a wizard to run commands as... */
        if (!is_wizard(wizard)) {
            Objid first_valid = -1;

            if (wizard >= 0)
                printf("** Object #%" PRIdN " is not a wizard...\n", wizard);

            for (wizard = 0; wizard <= db_last_used_objid(); wizard++)
                if (is_wizard(wizard))
                    break;
                else if (valid(wizard) && first_valid < 0)
                    first_valid = wizard;

            if (!is_wizard(wizard)) {
                if (first_valid < 0) {
                    first_valid = db_create_object(-1);
                    db_change_parents(Var::new_obj(first_valid), new_list(0), none);
                    printf("** No objects in database; created #%" PRIdN ".\n",
                           first_valid);
                }
                wizard = first_valid;
                db_set_object_flag(wizard, FLAG_WIZARD);
                printf("** No wizards in database; wizzed #%" PRIdN ".\n", wizard);
            }
            printf("** Now running emergency commands as #%" PRIdN " ...\n\n", wizard);
        }
        char prompt[100];
        sprintf(prompt, "(#%" PRIdN ")%s: ", wizard, debug ? "" : "[!d]");
        line = read_stdin_line(prompt);

        if (!line)
            start_ok = 0;   /* treat EOF as "quit" */
        else if (*line == ';') {    /* eval command */
            Var code, errors;
            Program *program;
            Var str;

            str.type = TYPE_STR;
            code = new_list(0);

            if (*++line == ';')
                line++;
            else {
                str.v.str = str_dup("return");
                code = listappend(code, str);
            }

            while (*line == ' ')
                line++;

            if (*line == '\0') {    /* long form */
                printf("Type one or more lines of code, ending with `.' ");
                printf("alone on a line.\n");
                for (;;) {
                    line = read_stdin_line(" ");
                    if (!strcmp(line, "."))
                        break;
                    else {
                        str.v.str = str_dup(line);
                        code = listappend(code, str);
                    }
                }
            } else {
                str.v.str = str_dup(line);
                code = listappend(code, str);
            }
            str.v.str = str_dup(";");
            code = listappend(code, str);

            program = parse_list_as_program(code, &errors);
            free_var(code);
            if (program) {
                Var result;

                switch (run_server_program_task(NOTHING, "emergency_mode",
                                                new_list(0), NOTHING,
                                                "emergency_mode", program,
                                                wizard, debug, wizard, "",
                                                &result)) {
                    case OUTCOME_DONE:
                        unparse_value(s, result);
                        printf("=> %s\n", reset_stream(s));
                        free_var(result);
                        break;
                    case OUTCOME_ABORTED:
                        printf("=> *Aborted*\n");
                        break;
                    case OUTCOME_BLOCKED:
                        printf("=> *Suspended*\n");
                        break;
                }
                free_program(program);
            } else {
                int i;

                printf("** %" PRIdN " errors during parsing:\n",
                       errors.v.list[0].v.num);
                for (i = 1; i <= errors.v.list[0].v.num; i++)
                    printf("  %s\n", errors.v.list[i].v.str);
            }
            free_var(errors);
        } else {
            words = parse_into_wordlist(line);
            nargs = words.v.list[0].v.num - 1;
            if (nargs < 0)
                continue;
            command = words.v.list[1].v.str;

            if ((!strcasecmp(command, "program")
                    || !strcasecmp(command, ".program"))
                    && nargs == 1) {
                const char *verbref = words.v.list[2].v.str;
                db_verb_handle h;
                const char *message, *vname;

                h = find_verb_for_programming(wizard, verbref,
                                              &message, &vname);
                printf("%s\n", message);
                if (h.ptr) {
                    Var code, str, errors;
                    const char *line;
                    Program *program;

                    code = new_list(0);
                    str.type = TYPE_STR;

                    while (strcmp(line = read_stdin_line(" "), ".")) {
                        str.v.str = str_dup(line);
                        code = listappend(code, str);
                    }

                    program = parse_list_as_program(code, &errors);
                    if (program) {
                        db_set_verb_program(h, program);
                        printf("Verb programmed.\n");
                    } else {
                        int i;

                        printf("** %" PRIdN " errors during parsing:\n",
                               errors.v.list[0].v.num);
                        for (i = 1; i <= errors.v.list[0].v.num; i++)
                            printf("  %s\n", errors.v.list[i].v.str);
                        printf("Verb not programmed.\n");
                    }

                    free_var(code);
                    free_var(errors);
                }
            } else if (!strcasecmp(command, "list") && nargs == 1) {
                const char *verbref = words.v.list[2].v.str;
                db_verb_handle h;
                const char *message, *vname;

                h = find_verb_for_programming(wizard, verbref,
                                              &message, &vname);
                if (h.ptr)
                    unparse_to_file(stdout, db_verb_program(h), 0, 1,
                                    MAIN_VECTOR);
                else
                    printf("%s\n", message);
            } else if (!strcasecmp(command, "disassemble") && nargs == 1) {
                const char *verbref = words.v.list[2].v.str;
                db_verb_handle h;
                const char *message, *vname;

                h = find_verb_for_programming(wizard, verbref,
                                              &message, &vname);
                if (h.ptr)
                    disassemble_to_file(stdout, db_verb_program(h));
                else
                    printf("%s\n", message);
            } else if (!strcasecmp(command, "abort") && nargs == 0) {
                printf("Bye.  (%s)\n\n", "NOT saving database");
                exit(1);
            } else if (!strcasecmp(command, "quit") && nargs == 0) {
                start_ok = 0;
            } else if (!strcasecmp(command, "continue") && nargs == 0) {
                start_ok = 1;
            } else if (!strcasecmp(command, "debug") && nargs == 0) {
                debug = !debug;
            } else if (!strcasecmp(command, "wizard") && nargs == 1
                       && sscanf(words.v.list[2].v.str, "#%" PRIdN, &wizard) == 1) {
                printf("** Switching to wizard #%" PRIdN "...\n", wizard);
            } else if (!strcasecmp(command, "help") || !strcasecmp(command, "?")) {
                printf(";EXPR                 "
                       "Evaluate MOO expression, print result.\n");
                printf(";;CODE                "
                       "Execute whole MOO verb, print result.\n");
                printf("    (For above, omitting EXPR or CODE lets you "
                       "enter several lines\n");
                printf("     of input at once; type a period alone on a "
                       "line to finish.)\n");
                printf("program OBJ:VERB      "
                       "Set the MOO code of an existing verb.\n");
                printf("list OBJ:VERB         "
                       "List the MOO code of an existing verb.\n");
                printf("disassemble OBJ:VERB  "
                       "List the internal form of an existing verb.\n");
                printf("debug                 "
                       "Toggle evaluation with(out) `d' bit.\n");
                printf("wizard #XX            "
                       "Execute future commands as wizard #XX.\n");
                printf("continue              "
                       "End emergency mode, continue start-up.\n");
                printf("quit                  "
                       "Exit server normally, saving database.\n");
                printf("abort                 "
                       "Exit server *without* saving database.\n");
                printf("help, ?               "
                       "Print this text.\n\n");
                printf("NOTE: *NO* forked or suspended tasks will run "
                       "until you exit this mode.\n\n");
            } else {
                printf("** Unknown or malformed command.\n");
            }

            free_var(words);
        }
    }

    printf("Bye.  (%s)\n\n", start_ok ? "continuing" : "saving database");
    fclose(stdout);

    free_stream(s);
    in_emergency_mode = false;
    oklog("EMERGENCY_MODE: Leaving mode; %s continue...\n",
          start_ok ? "will" : "won't");
    return start_ok;
}

static void
run_do_start_script(Var code)
{
    Stream *s = new_stream(100);
    Var result;

    switch (run_server_task(NOTHING,
                            Var::new_obj(SYSTEM_OBJECT), "do_start_script", code, "",
                            &result)) {
        case OUTCOME_DONE:
            unparse_value(s, result);
            oklog("SCRIPT: => %s\n", reset_stream(s));
            free_var(result);
            break;
        case OUTCOME_ABORTED:
            oklog("SCRIPT: *Aborted*\n");
            break;
        case OUTCOME_BLOCKED:
            oklog("SCRIPT: *Suspended*\n");
            break;
    }

    free_stream(s);
}

static void
do_script_line(const char *line)
{
    Var str;
    Var code = new_list(0);

    str = str_dup_to_var(raw_bytes_to_clean(line, strlen(line)));
    code = listappend(code, str);

    run_do_start_script(code);
}

static void
do_script_file(const char *path)
{
    Var str;
    Var code = new_list(0);

    std::ifstream file(path);
    if (!file.is_open()) {
        panic_moo(strerror(errno));
    }

    std::string line;
    while (std::getline(file, line)) {
        str = str_dup_to_var(raw_bytes_to_clean(line.c_str(), line.size()));
        code = listappend(code, str);
    }

    if (!file.eof()) {
        panic_moo(strerror(errno));
    }

    file.close();

    run_do_start_script(code);
}

static void
init_random(void)
{
    long seed;
    unsigned char soskey[32];

    memset(soskey, 0, sizeof(soskey));

#ifndef TEST

    oklog("RANDOM: seeding from " RANDOM_DEVICE "\n");

    int fd;

    if ((fd = open(RANDOM_DEVICE, O_RDONLY)) == -1) {
        errlog("Can't open " RANDOM_DEVICE "!\n");
        exit(1);
    }

    ssize_t count = 0, total = 0;

    while (total < sizeof(soskey)) {
        if ((count = read(fd, soskey + total, sizeof(soskey) - total)) == -1) {
            errlog("Can't read " RANDOM_DEVICE "!\n");
            exit(1);
        }
        total += count;
    }

    close(fd);

#else /* #ifndef TEST */

    oklog("RANDOM: (-DTEST) not seeding!\n");

#endif

    sosemanuk_schedule(&key_context, soskey, sizeof(soskey));

    sosemanuk_init(&run_context, &key_context, nullptr, 0);

    sosemanuk_prng(&run_context, (unsigned char *)&seed, sizeof(seed));

    SRANDOM(seed);
}

/*
 * Exported interface
 */

void
set_server_cmdline(const char *line)
{
    /* This technique works for all UNIX systems I've seen on which this is
     * possible to do at all, and it's safe on all systems.  The only systems
     * I know of where this doesn't work run System V Release 4, on which the
     * kernel keeps its own copy of the original cmdline for printing by the
     * `ps' command; on these systems, it would appear that there does not
     * exist a way to get around this at all.  Thus, this works everywhere I
     * know of where it's possible to do the job at all...
     */
    char *p = cmdline_buffer, *e = p + cmdline_buflen - 1;

    while (*line && p < e)
        *p++ = *line++;
    while (p < e)
        *p++ = ' ';     /* Pad with blanks, not nulls; on SunOS and
                 * maybe other systems, nulls would confuse
                 * `ps'.  (*sigh*)
                 */
    *e = '\0';
}

int
server_flag_option(const char *name, int defallt)
{
    Var v;

    if (get_server_option(SYSTEM_OBJECT, name, &v))
        return is_true(v);
    else
        return defallt;
}

int
server_int_option(const char *name, int defallt)
{
    Var v;

    if (get_server_option(SYSTEM_OBJECT, name, &v))
        return (v.type == TYPE_INT ? v.v.num : defallt);
    else
        return defallt;
}

double
server_float_option(const char *name, double defallt)
{
    Var v;

    if (get_server_option(SYSTEM_OBJECT, name, &v))
        return (v.type == TYPE_FLOAT ? v.v.fnum : defallt);
    else
        return defallt;
}

const char *
server_string_option(const char *name, const char *defallt)
{
    Var v;

    if (get_server_option(SYSTEM_OBJECT, name, &v))
        return (v.type == TYPE_STR ? v.v.str : nullptr);
    else
        return defallt;
}

static Objid next_unconnected_player = NOTHING - 1;

server_handle
server_new_connection(server_listener sl, network_handle nh, bool outbound)
{
    slistener *l = (slistener *)sl.ptr;
    shandle *h = (shandle *)mymalloc(sizeof(shandle), M_NETWORK);
    server_handle result;

    all_shandles_mutex.lock();

    h->next = all_shandles;
    h->prev = &all_shandles;
    if (all_shandles)
        all_shandles->prev = &(h->next);
    all_shandles = h;

    h->nhandle = nh;
    h->connection_time = 0;
    h->last_activity_time = time(nullptr);
    h->player = next_unconnected_player--;
    h->switched = 0;
    h->listener = l ? l->oid : SYSTEM_OBJECT;
    h->tasks = new_task_queue(h->player, h->listener);
    h->disconnect_me = false;
    h->outbound = outbound;
    h->binary = false;
    h->print_messages = l ? l->print_messages : !outbound;

    all_shandles_mutex.unlock();

    if (l || !outbound) {
        new_input_task(h->tasks, "", 0, 0);
        /*
         * Suspend input at the network level until the above input task
         * is processed.  At the point when it is dequeued, tasks.c will
         * notice that the queued input size is below the low water mark
         * and resume input.
         */
        task_suspend_input(h->tasks);
    }

    lock_connection_name_mutex(nh);

    if (outbound) {
        oklog("CONNECT: #%" PRIdN " to %s [%s], port %i\n", h->player,
              network_connection_name(nh), network_ip_address(nh), network_port(nh));
    } else {
        oklog("ACCEPT: #%" PRIdN " on %s [%s], port %i from %s [%s], port %i\n", h->player,
              network_source_connection_name(nh), network_source_ip_address(nh),
              network_source_port(nh), network_connection_name(nh),
              network_ip_address(nh), network_port(nh));
    }

    unlock_connection_name_mutex(nh);

    result.ptr = h;
    return result;
}

void
server_refuse_connection(server_listener sl, network_handle nh)
{
    slistener *l = (slistener *)sl.ptr;

    lock_connection_name_mutex(nh);

    if (l->print_messages)
        send_message(l->oid, nh, "server_full_msg",
                     "*** Sorry, but the server cannot accept any more"
                     " connections right now.",
                     "*** Please try again later.",
                     0);

    errlog("SERVER FULL: refusing connection on %s [%s], port %i from %s [%s], port %i\n",
           network_source_connection_name(nh), network_source_ip_address(nh),
           network_source_port(nh), network_connection_name(nh),
           network_ip_address(nh), network_port(nh));

    unlock_connection_name_mutex(nh);
}

void
server_receive_line(server_handle sh, const char *line, bool out_of_band)
{
    shandle *h = (shandle *) sh.ptr;

    h->last_activity_time = time(nullptr);
    new_input_task(h->tasks, line, h->binary, out_of_band);
}

void
server_close(server_handle sh)
{
    shandle *h = (shandle *) sh.ptr;

    lock_connection_name_mutex(h->nhandle);

    oklog("CLIENT DISCONNECTED: %s on %s\n",
          object_name(h->player),
          network_connection_name(h->nhandle));

    unlock_connection_name_mutex(h->nhandle);
    h->disconnect_me = true;
    call_notifier(h->player, h->listener, "user_client_disconnected");
    free_shandle(h);
}

void
server_suspend_input(Objid connection)
{
    shandle *h = find_shandle(connection);

    network_suspend_input(h->nhandle);
}

void
server_resume_input(Objid connection)
{
    shandle *h = find_shandle(connection);

    network_resume_input(h->nhandle);
}

bool
is_trusted_proxy(Objid connection)
{
    shandle *existing_h = find_shandle(connection);
    Var proxies;

    if (!existing_h) {
        return false;
    } else if (!get_server_option(existing_h->listener, "trusted_proxies", &proxies) || proxies.type != TYPE_LIST) {
        return false;
    } else {
        
        int i;
        const char *ip = network_ip_address(existing_h->nhandle);

        for (i = 1; i <= proxies.v.list[0].v.num; i++) {
            if (proxies.v.list[i].type == TYPE_STR && strcmp(ip, proxies.v.list[i].v.str) == 0) {
                return true;
            }
        }
        return false;
    }
}

int
proxy_connected(Objid connection, char *command)
{
    shandle *existing_h = find_shandle(connection);
    int ret = 0;
    if (existing_h) {
        applog(LOG_INFO3, "PROXY: Proxy command detected: %s\n", command);
        char *source, *destination = nullptr;
        char *source_port = nullptr;
        char *destination_port = nullptr;
        char *split = strtok(command, " ");

        int x = 0;
        for (x = 1; x <= 6; x++) {
            // Just in case something goes horribly wrong...
            if (split == nullptr) {
                errlog("PROXY: Proxy command parsing failed!\n");
                return 1;
            }
            switch (x) {
                case 3:
                    source = split;        // local interface
                    break;
                case 4:
                    destination = split;             // incoming connection IP
                    break;
                case 5:
                    destination_port = split;        // incoming connection port
                    break;
                case 6:
                    source_port = split;   // local port
                    break;
                default:
                    break;
            }
            split = strtok(nullptr, " ");
        }
        lock_connection_name_mutex(existing_h->nhandle);
        const char *old_name = str_dup(network_connection_name(existing_h->nhandle));   // rewrite is going to free this
        unlock_connection_name_mutex(existing_h->nhandle);
        
        int rw = rewrite_connection_name(existing_h->nhandle, destination, destination_port, source, source_port);
        if (rw != 0) {
            errlog("PROXY: Proxy rewrite failed.\n");
            ret = 1;
        } else {
            lock_connection_name_mutex(existing_h->nhandle);
            applog(LOG_INFO3, "PROXY: connection_name changed from `%s` to `%s`\n", old_name, network_connection_name(existing_h->nhandle));
            unlock_connection_name_mutex(existing_h->nhandle);
        }
        free_str(old_name);
    } else {
        ret = -1;
    }
    return ret;
}

void
player_connected(Objid old_id, Objid new_id, bool is_newly_created)
{
    shandle *existing_h = find_shandle(new_id);
    shandle *new_h = find_shandle(old_id);

    if (!new_h)
        panic_moo("Non-existent shandle connected");

    new_h->player = new_id;
    new_h->connection_time = time(nullptr);

    if (existing_h) {
        /* we now have two shandles with the same player value while
         * find_shandle assumes there can only be one.  This needs to
         * be remedied before any call_notifier() call; luckily, the
         * latter only needs listener value.
         */
        Objid existing_listener = existing_h->listener;
        
        lock_connection_name_mutex(existing_h->nhandle);
        lock_connection_name_mutex(new_h->nhandle);
        oklog("REDIRECTED: %s, was %s, now %s\n",
              object_name(new_id),
              network_connection_name(existing_h->nhandle),
              network_connection_name(new_h->nhandle));
        unlock_connection_name_mutex(new_h->nhandle);
        unlock_connection_name_mutex(existing_h->nhandle);
        if (existing_h->print_messages)
            send_message(existing_listener, existing_h->nhandle,
                         "redirect_from_msg",
                         "*** Redirecting connection to new port ***", 0);
        if (new_h->print_messages)
            send_message(new_h->listener, new_h->nhandle, "redirect_to_msg",
                         "*** Redirecting old connection to this port ***", 0);
        network_close(existing_h->nhandle);
        free_shandle(existing_h);
        if (existing_listener == new_h->listener)
            call_notifier(new_id, new_h->listener, "user_reconnected");
        else {
            new_h->disconnect_me = true;
            call_notifier(new_id, existing_listener,
                          "user_client_disconnected");
            new_h->disconnect_me = false;
            call_notifier(new_id, new_h->listener, "user_connected");
        }
    } else {
        lock_connection_name_mutex(new_h->nhandle);
        oklog("%s: %s on %s\n",
              is_newly_created ? "CREATED" : "CONNECTED",
              object_name(new_h->player),
              full_network_connection_name(new_h->nhandle));
        unlock_connection_name_mutex(new_h->nhandle);
        if (new_h->print_messages) {
            if (is_newly_created)
                send_message(new_h->listener, new_h->nhandle, "create_msg",
                             "*** Created ***", 0);
            else
                send_message(new_h->listener, new_h->nhandle, "connect_msg",
                             "*** Connected ***", 0);
        }
        call_notifier(new_id, new_h->listener,
                      is_newly_created ? "user_created" : "user_connected");
    }
}

void
player_switched(Objid old_id, Objid new_id, bool silent)
{
    const char *old_name = str_dup(object_name(old_id));
    shandle *existing_h = find_shandle(new_id);
    shandle *new_h = find_shandle(old_id);
    const char *status = nullptr;

    if (!new_h)
        panic_moo("Non-existent shandle connected");

    new_h->switched = old_id;
    new_h->player = new_id;
    new_h->connection_time = time(nullptr);

    if (existing_h) {
        status = "REDIRECTED:";
        new_h->switched = new_id;
        if (!silent && existing_h->print_messages)
            send_message(existing_h->listener, existing_h->nhandle,
                         "redirect_from_msg",
                         "*** Redirecting connection to new port ***", 0);
        if (!silent && new_h->print_messages)
            send_message(new_h->listener, new_h->nhandle, "redirect_to_msg",
                         "*** Redirecting old connection to this port ***", 0);
        network_close(existing_h->nhandle);
        free_shandle(existing_h);
    } else {
        if (!silent && new_h->print_messages)
            send_message(new_h->listener, new_h->nhandle, "connect_msg",
                         "*** Connected ***", 0);
        status = old_id < 0 ? "CONNECTED:" : "SWITCHED:";
    }
    lock_connection_name_mutex(new_h->nhandle);
    oklog("%s %s is now %s on %s\n",
          status,
          old_name,
          object_name(new_h->player),
          network_connection_name(new_h->nhandle));
    unlock_connection_name_mutex(new_h->nhandle);
    free_str(old_name);
}

int
is_player_connected(Objid player)
{
    shandle *h = find_shandle(player);
    return !h || h->disconnect_me.load() ? 0 : 1;
}

void
notify(Objid player, const char *message)
{
    shandle *h = find_shandle(player);

    if (h && !h->disconnect_me.load())
        network_send_line(h->nhandle, message, 1, 1);
    else if (in_emergency_mode)
        emergency_notify(player, message);
}

void
boot_player(Objid player)
{
    shandle *h = find_shandle(player);

    if (h)
        h->disconnect_me = true;
}

void
write_active_connections(void)
{
    int count = 0;
    shandle *h;

    all_shandles_mutex.lock();

    for (h = all_shandles; h; h = h->next)
        count++;

    dbio_printf("%" PRIdN " active connections with listeners\n", count);

    for (h = all_shandles; h; h = h->next)
        dbio_printf("%" PRIdN " %" PRIdN "\n", h->player, h->listener);
        
    all_shandles_mutex.unlock();
}

int
read_active_connections(void)
{
    int count, i, have_listeners = 0;
    char c;

    i = dbio_scanf("%d active connections%c", &count, &c);
    if (i == EOF) {     /* older database format */
        checkpointed_connections = new_list(0);
        return 1;
    } else if (i != 2) {
        errlog("READ_ACTIVE_CONNECTIONS: Bad active connections count.\n");
        return 0;
    } else if (c == ' ') {
        if (strcmp(dbio_read_string(), "with listeners") != 0) {
            errlog("READ_ACTIVE_CONNECTIONS: Bad listeners tag.\n");
            return 0;
        } else
            have_listeners = 1;
    } else if (c != '\n') {
        errlog("READ_ACTIVE_CONNECTIONS: Bad EOL.\n");
        return 0;
    }
    checkpointed_connections = new_list(count);
    for (i = 1; i <= count; i++) {
        Objid who, listener;
        Var v;

        if (have_listeners) {
            if (dbio_scanf("%" SCNdN "%" SCNdN "\n", &who, &listener) != 2) {
                errlog("READ_ACTIVE_CONNECTIONS: Bad conn/listener pair.\n");
                return 0;
            }
        } else {
            who = dbio_read_num();
            listener = SYSTEM_OBJECT;
        }
        checkpointed_connections.v.list[i] = v = new_list(2);
        v.v.list[1].type = v.v.list[2].type = TYPE_OBJ;
        v.v.list[1].v.obj = who;
        v.v.list[2].v.obj = listener;
    }

    return 1;
}

int
find_network_handle(Objid obj, network_handle **handle)
{
    shandle *h = find_shandle(obj);

    if (!h || h->disconnect_me.load())
        return -1;
    else
        *handle = &(h->nhandle);

    return 0;
}

static void
set_system_object_integer_limits()
{
    if (!valid(SYSTEM_OBJECT))
        return;

    Var value;
    db_prop_handle h;

    h = db_find_property(Var::new_obj(SYSTEM_OBJECT), "maxint", &value);
    if (h.ptr)
        db_set_property_value(h, Var::new_int(MAXINT));

    h = db_find_property(Var::new_obj(SYSTEM_OBJECT), "minint", &value);
    if (h.ptr)
        db_set_property_value(h, Var::new_int(MININT));

}

void
print_usage()
{
    fprintf(stderr, "Usage:\n  %s [-e] [-f script-file] [-c script-line] [-l log-file] [-m] [-w waif-type] [-O|-o] [-4 ipv4-address] [-6 ipv6-address] [-r certificate-path] [-k key-path] [-i files-path] [-x executables-path] %s [-t|-p port-number]\n",
            this_program, db_usage_string());
    fprintf(stderr, "\nMETA OPTIONS\n");
    fprintf(stderr, "  %-20s %s\n", "-v, --version", "current version");
    fprintf(stderr, "  %-20s %s\n", "-h, --help", "show usage information and command-line options");
    fprintf(stderr, "\nSERVER OPTIONS\n");
    fprintf(stderr, "  %-20s %s\n", "-e, --emergency", "emergency wizard mode");
    fprintf(stderr, "  %-20s %s\n", "-l, --log", "redirect standard output to log file");
    fprintf(stderr, "\nDATABASE OPTIONS\n");
    fprintf(stderr, "  %-20s %s\n", "-m, --clear-move", "clear the `last_move' builtin property on all objects");
    fprintf(stderr, "  %-20s %s\n", "-w, --waif-type", "convert waifs from the specified type (check with typeof(waif) in your old MOO)");
    fprintf(stderr, "  %-20s %s\n", "-f, --start-script", "file to load and pass to `#0:do_start_script()'");
    fprintf(stderr, "  %-20s %s\n", "-c, --start-line", "line to pass to `#0:do_start_script()'");
    fprintf(stderr, "\nDIRECTORY OPTIONS\n");
    fprintf(stderr, "  %-20s %s\n", "-i, --file-dir", "directory to look for files for use with FileIO functions");
    fprintf(stderr, "  %-20s %s\n", "-x, --exec-dir", "directory to look for executables for use with the exec() function");
    fprintf(stderr, "\nNETWORKING OPTIONS\n");
    fprintf(stderr, "  %-20s %s\n", "-o, --outbound", "enable outbound network connections");
    fprintf(stderr, "  %-20s %s\n", "-O, --no-outbound", "disable outbound network connections");
    fprintf(stderr, "  %-20s %s\n", "    --no-ipv6", "don't listen on IPv6 for default ports");
    fprintf(stderr, "  %-20s %s\n", "-4, --ipv4", "restrict IPv4 listeners to a specific address");
    fprintf(stderr, "  %-20s %s\n", "-6, --ipv6", "restrict IPv6 listeners to a specific address");
    fprintf(stderr, "  %-20s %s\n", "-r, --tls-cert", "TLS certificate to use");
    fprintf(stderr, "  %-20s %s\n", "-k, --tls-key", "TLS key to use");
    fprintf(stderr, "  %-20s %s\n", "-t, --tls-port", "port to listen for TLS connections on (can be used multiple times)");
    fprintf(stderr, "  %-20s %s\n", "-p, --port", "port to listen for connections on (can be used multiple times)");
    fprintf(stderr, "\nThe emergency mode switch (-e) may not be used with either the file (-f) or line (-c) options.\n\n");
    fprintf(stderr, "Both the file and line options may be specified. Their order on the command line determines the order of their invocation.\n\n");
    fprintf(stderr, "Examples:\n");
    fprintf(stderr, "%s -c '$enable_debugging();' -f development.moo Minimal.db Minimal.db.new 7777\n", this_program);
    fprintf(stderr, "%s Minimal.db Minimal.db.new\n", this_program);
}

int waif_conversion_type = _TYPE_WAIF;    /* For shame. We can remove this someday. */

int
main(int argc, char **argv)
{
    this_program = str_dup(argv[0]);
    const char *log_file = nullptr;
    const char *script_file = nullptr;
    const char *script_line = nullptr;
    int script_file_first = 0;
    int emergency = 0;
    Var desc = Var::new_int(0);

#ifdef USE_TLS
    const char *certificate_path = nullptr;
    const char *key_path = nullptr;
#endif

    init_cmdline(argc, argv);

    // Keep track of which options were changed for logging purposes later.
    bool cmdline_outbound = false;
    bool cmdline_ipv4 = false;
    bool cmdline_ipv6 = false;
    bool cmdline_cert = false;
    bool cmdline_key = false;
    bool cmdline_filedir = false;
    bool cmdline_execdir = false;
    bool cmdline_noipv6 = false;
    //

    std::vector<uint16_t> initial_ports;
#ifdef USE_TLS
    std::vector<uint16_t> initial_tls_ports;
#endif

    int option_index = 0;
    int c = 0;
    static struct option long_options[] =
    {
        {"version",         no_argument,        nullptr,            'v'},
        {"emergency",       no_argument,        nullptr,            'e'},
        {"log",             required_argument,  nullptr,            'l'},
        {"start-script",    required_argument,  nullptr,            'f'},
        {"start-line",      required_argument,  nullptr,            'c'},
        {"waif-type",       required_argument,  nullptr,            'w'},
        {"clear-move",      no_argument,        nullptr,            'm'},
        {"outbound",        no_argument,        nullptr,            'o'},
        {"no-outbound",     no_argument,        nullptr,            'O'},
        {"no-ipv6",         no_argument,        nullptr,            '3'},
        {"tls-port",        no_argument,        nullptr,            't'},
        {"ipv4",            required_argument,  nullptr,            '4'},
        {"ipv6",            required_argument,  nullptr,            '6'},
        {"port",            required_argument,  nullptr,            'p'},
        {"tls-cert",        required_argument,  nullptr,            'r'},
        {"tls-key",         required_argument,  nullptr,            'k'},
        {"file-dir",        required_argument,  nullptr,            'i'},
        {"exec-dir",        required_argument,  nullptr,            'x'},
        {"help",            no_argument,        nullptr,            'h'},
        {nullptr,           0,                  nullptr,              0}
    };

    while ((c = getopt_long(argc, argv, "vel:f:c:w:moOt:4:6:p:r:k:i:x:h", long_options, &option_index)) != -1)
    {
        switch (c)
        {
            case 'v':                   /* --version; print version and exit */
            {
                fprintf(stderr, "ToastStunt version %s\n", server_version);
                exit(1);
            }
            break;

            case 'e':                   /* --emergency; emergency wizard mode */
                emergency = 1;
                break;

            case 'l':                   /* --log; specify log file */
            {
                log_file = optarg;
                set_log_file_name(log_file);
            }
            break;

            case 'f':                   /* --start-script; file of code to pass to :do_start_script */
            {
                if (!script_line)
                    script_file_first = 1;
                script_file = optarg;
            }
            break;

            case 'c':                   /* --start-line; line of code to pass to :do_start_script */
            {
                if (!script_file)
                    script_file_first = 0;
                script_line = optarg;
            }
            break;

            case 'w':                   /* --waif-type; old waif type to use for conversion */
                waif_conversion_type = atoi(optarg);
                break;

            case 'm':                   /* --clear-move; clear all last_move properties and don't set new ones */
                clear_last_move = true;
                break;

            case 'o':                   /* --outbound; enable outbound network connections */
            {
#ifndef OUTBOUND_NETWORK
                fprintf(stderr, "Outbound networking is disabled. The '--outbound' option is invalid.\n");
                exit(1);
#else
                cmdline_outbound = true;
                outbound_network_enabled = true;
#endif
            }
            break;

            case 'O':                   /* --no-outbound; disable outbound network connections */
            {
#ifdef OUTBOUND_NETWORK
                cmdline_outbound = true;
                outbound_network_enabled = false;
#endif
            }
            break;
            
            case '3':                   /* --no-ipv6; disable initial IPv6 listeners */
            {
                cmdline_noipv6 = true;
            }
            break;

            case '4':                   /* --ipv4; restrict ipv4 listener */
            {
                cmdline_ipv4 = true;
                bind_ipv4 = str_dup(optarg);
            }
            break;

            case '6':                   /* --ipv6; restrict ipv6 listener */
            {
                cmdline_ipv6 = true;
                bind_ipv6 = str_dup(optarg);
            }
            break;

            case 'p':                   /* --port; standard listening port */
            {
                char *p = nullptr;
                initial_ports.push_back(strtoul(optarg, &p, 10));
            }
            break;

            case 't':                   /* --tls-port; TLS listening port */
            {
#ifndef USE_TLS
                fprintf(stderr, "TLS is disabled or not supported. The '--tls-port' option is invalid.\n");
                exit(1);
#else
                char *p = nullptr;
                initial_tls_ports.push_back(strtoul(optarg, &p, 10));
#endif
            }
            break;

            case 'r':                   /* --tls-cert; TLS certificate path */
            {
#ifndef USE_TLS
                fprintf(stderr, "TLS is disabled or not supported. The '--tls' option is invalid.\n");
                exit(1);
#else
                cmdline_cert = true;
                default_certificate_path = optarg;
#endif
            }
            break;

            case 'k':                   /* --tls-key; TLS key path */
            {
#ifndef USE_TLS
                fprintf(stderr, "TLS is disabled or not supported. The '--tls' option is invalid.\n");
                exit(1);
#else
                cmdline_key = true;
                default_key_path = optarg;
#endif
            }
            break;

            case 'i':                   /* --file-dir; the directory to store files in */
            {
                cmdline_filedir = true;
                file_subdir = optarg;
            }
            break;

            case 'x':                   /* --exec-dir; the directory to store executables in */
            {
                cmdline_execdir = true;
                exec_subdir = optarg;
            }
            break;

            case 'h':                   /* --help; show usage instructions */
                print_usage();
                exit(1);

            default:
                // Should we print usage here? It's pretty spammy...
                exit(1);
        }
    }

    argv += optind;
    argc -= optind;

    if (log_file) {
        FILE *f = fopen(log_file, "a");

        if (f)
            set_log_file(f);
        else {
            perror("Error opening specified log file");
            exit(1);
        }
    } else {
        set_log_file(stderr);
    }

    if ((emergency && (script_file || script_line))
            || !db_initialize(&argc, &argv)
            || !network_initialize(argc, argv, &desc)) {
        print_usage();
        exit(1);
    }

    if (initial_ports.empty()
#ifdef USE_TLS
            && initial_tls_ports.empty()
#endif
            && desc.v.num == 0)
        desc.v.num = DEFAULT_PORT;

    // If we caught a port at the end of the arglist, add it to the rest.
    if (desc.v.num != 0)
        initial_ports.push_back(desc.v.num);

    /* Now that it's so easy to change file / exec directories, it's easy to forget the last '/'
       We'll helpfully add it back to avoid confusion. */
    if (file_subdir[strlen(file_subdir) - 1] != '/')
        asprintf(&file_subdir, "%s/", file_subdir);
    if (exec_subdir[strlen(exec_subdir) - 1] != '/')
        asprintf(&exec_subdir, "%s/", exec_subdir);

    applog(LOG_INFO1, " _   __           _____                ______\n");
    applog(LOG_INFO1, "( `^` ))  ___________  /_____  _________ __  /_\n");
    applog(LOG_INFO1, "|     ||   __  ___/_  __/_  / / /__  __ \\_  __/\n");
    applog(LOG_INFO1, "|     ||   _(__  ) / /_  / /_/ / _  / / // /_\n");
    applog(LOG_INFO1, "'-----'`   /____/  \\__/  \\__,_/  /_/ /_/ \\__/   v%s\n", server_version);
    applog(LOG_INFO1, "\n");

    if (!emergency)
        fclose(stdout);

    if (log_file)
        fclose(stderr);

    parent_pid = getpid();

    enum PortType {PORT_STANDARD = 0, PORT_TLS};
    enum IPProtocol {PROTO_IPv4 = 0, PROTO_IPv6};

    applog(LOG_INFO1, "STARTING: Version %s (%" PRIdN "-bit) of the ToastStunt/LambdaMOO server\n", server_version, SERVER_BITS);
    applog(LOG_INFO1, "          (Task timeouts measured in %s seconds.)\n",
           virtual_timer_available() ? "server CPU" : "wall-clock");
#ifdef JEMALLOC_FOUND
    applog(LOG_INFO1, "          (Using jemalloc)\n");
#endif
    applog(LOG_INFO1, "          (Process id %" PRIdN ")\n", parent_pid);
    if (waif_conversion_type != _TYPE_WAIF)
        applog(LOG_WARNING, "(Using type '%i' for waifs; will convert to '%i' at next checkpoint)\n", waif_conversion_type, _TYPE_WAIF);
    if (clear_last_move)
        applog(LOG_WARNING, "(last_move properties will all be cleared and no movement activity will be saved)\n");

    std::string port_string;
#ifdef USE_TLS
    for (int port_type = 0; port_type < 2; port_type++)
    {
        auto *ports = (port_type == PORT_STANDARD ? &initial_ports : &initial_tls_ports);
#else
    {
        auto port_type = PORT_STANDARD;
        auto *ports = &initial_ports;
#endif
        if (!ports->empty())
        {
            for (auto the_port : *ports)
                port_string += std::to_string(the_port) += ", ";
            port_string.resize(port_string.size() - 2);
            applog(LOG_NOTICE, "CMDLINE: Initial %sport%s = %s\n", port_type == PORT_TLS ? "TLS " : "", ports->size() > 1 ? "s" : "", port_string.c_str());
            port_string.clear();
        }
    }

    applog(LOG_NOTICE, "NETWORK: Outbound network connections %s.\n", outbound_network_enabled ? "enabled" : "disabled");
    if (cmdline_noipv6)
        applog(LOG_INFO2, "CMDLINE: Not listening for IPv6 connections on default ports.\n");
    if (cmdline_ipv4)
        applog(LOG_INFO2, "CMDLINE: IPv4 source address restricted to: %s.\n", bind_ipv4);
    if (cmdline_ipv6)
        applog(LOG_INFO2, "CMDLINE: IPv6 source address restricted to: %s.\n", bind_ipv6);
#ifdef USE_TLS
    if (cmdline_cert)
        applog(LOG_INFO2, "CMDLINE: Using TLS certificate path: %s\n", default_certificate_path);
    if (cmdline_key)
        applog(LOG_INFO2, "CMDLINE: Using TLS key path: %s\n", default_key_path);
#endif
    if (cmdline_filedir)
        applog(LOG_INFO2, "CMDLINE: Using file directory path: %s\n", file_subdir);
    if (cmdline_execdir)
        applog(LOG_INFO2, "CMDLINE: Using executables directory path: %s\n", exec_subdir);
#ifdef USE_TLS
    oklog("REGISTER_NETWORK: Using %s\n", SSLeay_version(SSLEAY_VERSION));
#endif

    register_bi_functions();

    std::vector<slistener*> initial_listeners;


    slistener *new_listener;

#ifdef USE_TLS
    for (int port_type = 0; port_type < 2; port_type++)
    {
        auto *ports = (port_type == PORT_STANDARD ? &initial_ports : &initial_tls_ports);
#else
    {
        auto port_type = PORT_STANDARD;
        auto *ports = &initial_ports;
#endif
        for (auto &the_port : *ports)
        {
            desc.v.num = the_port;
            for (int ip_type = 0; ip_type < (cmdline_noipv6 ? 1 : 2); ip_type++)
            {
                if ((new_listener = new_slistener(SYSTEM_OBJECT, desc, 1, nullptr, ip_type, nullptr TLS_PORT_TYPE TLS_CERT_PATH)) == nullptr)
                    errlog("Error creating %s%s listener on port %i.\n", port_type == PORT_TLS ? "TLS " : "", ip_type == PROTO_IPv6 ? "IPv6" : "IPv4", the_port);
                else
                    initial_listeners.push_back(new_listener);
            }
        }
    }

    if (initial_listeners.size() < 1) {
        errlog("Can't create initial connection point!\n");
        exit(1);
    }
    free_var(desc);
    // I doubt anybody will have enough listeners for this to be worthwhile, but why not.
    initial_ports.clear();
    initial_ports.shrink_to_fit();
#ifdef USE_TLS
    initial_tls_ports.clear();
    initial_tls_ports.shrink_to_fit();
#endif

    if (!db_load())
        exit(1);

    free_reordered_rt_env_values();

    load_server_options();
    set_system_object_integer_limits();

    init_random();

    setup_signals();
    reset_command_history();

    if (script_file_first) {
        if (script_file)
            do_script_file(script_file);
        if (script_line)
            do_script_line(script_line);
    }
    else {
        if (script_line)
            do_script_line(script_line);
        if (script_file)
            do_script_file(script_file);
    }

    if (!emergency || emergency_mode()) {
        int status = 0;
        int listen_failures = 0;

        for (auto &l : initial_listeners)
        {
            status = start_listener(l);
            if (!status)
            {
                errlog("Failed to listen on port %i\n", l->port);
                free_slistener(l);
                listen_failures++;
            }
        }

        if (listen_failures >= initial_listeners.size())
            exit(1);

        initial_listeners.clear();
        initial_listeners.shrink_to_fit();

        main_loop();

        background_shutdown();

        network_shutdown();
    }

#ifdef ENABLE_GC
    gc_collect();
#endif
    db_shutdown();
    db_clear_ancestor_cache();
    sqlite_shutdown();
    curl_shutdown();
    pcre_shutdown();

    free_str(this_program);

    return 0;
}


/**** built in functions ****/

static package
bf_server_version(Var arglist, Byte next, void *vdata, Objid progr)
{
    Var r;
    if (arglist.v.list[0].v.num > 0) {
        r = server_version_full(arglist.v.list[1]);
    }
    else {
        r.type = TYPE_STR;
        r.v.str = str_dup(server_version);
    }
    free_var(arglist);
    if (r.type == TYPE_ERR)
        return make_error_pack(r.v.err);
    else
        return make_var_pack(r);
}

static package
bf_renumber(Var arglist, Byte next, void *vdata, Objid progr)
{
    Var r;
    Objid o = arglist.v.list[1].v.obj;
    free_var(arglist);

    if (!valid(o))
        return make_error_pack(E_INVARG);
    else if (!is_wizard(progr))
        return make_error_pack(E_PERM);

    r.type = TYPE_OBJ;
    r.v.obj = db_renumber_object(o);
    return make_var_pack(r);
}

static package
bf_reset_max_object(Var arglist, Byte next, void *vdata, Objid progr)
{
    free_var(arglist);

    if (!is_wizard(progr))
        return make_error_pack(E_PERM);

    db_reset_last_used_objid();
    return no_var_pack();
}

/* Returns total memory usage, resident set size, shared pages, text/code, and data + stack. */
static package
bf_memory_usage(Var arglist, Byte next, void *vdata, Objid progr)
{
    // LINUX: Values are returned in pages. To get KB, multiply by 4.
    // macOS: The only value available is the resident set size, which is returned in bytes.
    free_var(arglist);

    long double size = 0.0, resident = 0.0, share = 0.0, text = 0.0, lib = 0.0, data = 0.0, dt = 0.0;

#ifdef __MACH__
    // macOS doesn't have /proc, so we have to search elsewhere.
    struct mach_task_basic_info info;
    mach_msg_type_number_t infoCount = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &infoCount) == KERN_SUCCESS)
        resident = (size_t)info.resident_size;
    else
        return make_error_pack(E_FILE);
#else
    FILE *f = fopen("/proc/self/statm", "r");

    if (!f)
        return make_error_pack(E_FILE);

    if (fscanf(f, "%Lf %Lf %Lf %Lf %Lf %Lf %Lf",
               &size, &resident, &share, &text, &lib, &data, &dt) != 7)
    {
        fclose(f);
        return make_error_pack(E_NACC);
    }

    fclose(f);
#endif

    Var s = new_list(5);
    s.v.list[1].type = TYPE_FLOAT;
    s.v.list[2].type = TYPE_FLOAT;
    s.v.list[3].type = TYPE_FLOAT;
    s.v.list[4].type = TYPE_FLOAT;
    s.v.list[5].type = TYPE_FLOAT;
    s.v.list[1].v.fnum = size;           // Total program size
    s.v.list[2].v.fnum = resident;       // Resident set size
    s.v.list[3].v.fnum = share;          // Shared pages from shared mappings
    s.v.list[4].v.fnum = text;           // Text (code)
    s.v.list[5].v.fnum = data;           // Data + stack

    return make_var_pack(s);
}

#ifdef JEMALLOC_FOUND
/* Returns a LIST of stats from jemalloc about memory usage.
 * NOTE: jemalloc must have been compiled with stats enabled for this to work.
 *       Otherwise it will just return all 0's.
 */
static package
bf_malloc_stats(Var arglist, Byte next, void *vdata, Objid progr)
{
    free_var(arglist);

    size_t sz;

    // Update the cached statistics.
    uint64_t epoch = 1;
    sz = sizeof(uint64_t);
    mallctl("epoch", &epoch, &sz, &epoch, sz);

    size_t allocated, active, resident, metadata, mapped, allocated_large, active_large;
    sz = sizeof(size_t);

    if (mallctl("stats.allocated", &allocated, &sz, NULL, 0) != 0)
        allocated = 0;

    if (mallctl("stats.active", &active, &sz, NULL, 0) != 0)
        active = 0;

    if (mallctl("stats.resident", &resident, &sz, NULL, 0) != 0)
        resident = 0;

    if (mallctl("stats.metadata", &metadata, &sz, NULL, 0) != 0)
        metadata = 0;

    if (mallctl("stats.mapped", &mapped, &sz, NULL, 0) != 0)
        mapped = 0;

    if (mallctl("stats.allocated_large", &allocated_large, &sz, NULL, 0) != 0)
        allocated_large = 0;

    if (mallctl("stats.active_large", &active_large, &sz, NULL, 0) != 0)
        active_large = 0;

    Var s = new_list(7);
    s.v.list[1] = Var::new_int(allocated);
    s.v.list[2] = Var::new_int(active);
    s.v.list[3] = Var::new_int(resident);
    s.v.list[4] = Var::new_int(metadata);
    s.v.list[5] = Var::new_int(mapped);
    s.v.list[6] = Var::new_int(allocated_large);
    s.v.list[7] = Var::new_int(active_large);

    return make_var_pack(s);
}
#endif


/* Return resource usage information from the operating system.
 * Values returned: {{load averages}, user time, system time, page reclaims, page faults, block input ops, block output ops, voluntary context switches, involuntary context switches, signals received
 * Divide load averages by 65536. */
static package
bf_usage(Var arglist, Byte next, void *vdata, Objid progr)
{
    free_var(arglist);
    if (!is_wizard(progr))
        return make_error_pack(E_PERM);

    Var r = new_list(9);
    Var cpu = new_list(3);

    // Setup all of our types ahead of time.
    int x = 0;
    for (x = 3; x <= r.v.list[0].v.num; x++)
        r.v.list[x].type = TYPE_INT;

    for (x = 1; x <= 3; x++)
        cpu.v.list[x] = Var::new_int(0); //initialize to all 0

#if !defined(__FreeBSD__) && !defined(__MACH__)
    struct sysinfo sys_info;
    int info_ret = sysinfo(&sys_info);

    for (x = 0; x < 3; x++)
        cpu.v.list[x + 1].v.num = (info_ret != 0 ? 0 : sys_info.loads[x]);
#else
    /*** Begin CPU load averages ***/
#ifdef __MACH__
    struct loadavg load;
    size_t size = sizeof(load);
    if (sysctlbyname("vm.loadavg", &load, &size, 0, 0) != -1) {
        for (x = 0; x < 3; x++)
            cpu.v.list[x + 1].v.num = load.ldavg[x];
    }
#endif
#endif

    /*** Now rusage ***/
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);

    r.v.list[1].type = TYPE_FLOAT;
    r.v.list[2].type = TYPE_FLOAT;
    r.v.list[1].v.fnum = (double)usage.ru_utime.tv_sec + (double)usage.ru_utime.tv_usec / CLOCKS_PER_SEC;
    r.v.list[2].v.fnum = (double)usage.ru_stime.tv_sec + (double)usage.ru_stime.tv_usec / CLOCKS_PER_SEC;
    r.v.list[3].v.num = usage.ru_minflt;
    r.v.list[4].v.num = usage.ru_majflt;
    r.v.list[5].v.num = usage.ru_inblock;
    r.v.list[6].v.num = usage.ru_oublock;
    r.v.list[7].v.num = usage.ru_nvcsw;
    r.v.list[8].v.num = usage.ru_nivcsw;
    r.v.list[9].v.num = usage.ru_nsignals;

    // Add in our load averages.
    r = listinsert(r, cpu, 1);
    return make_var_pack(r);
}

/* Unceremoniously exit the server, creating a panic dump of the database. */
static package
bf_panic(Var arglist, Byte next, void *vdata, Objid progr)
{
    const char *msg;

    if (!is_wizard(progr)) {
        free_var(arglist);
        return make_error_pack(E_PERM);
    }

    if (arglist.v.list[0].v.num) {
        msg = str_dup(arglist.v.list[1].v.str);
    } else {
        msg = "";
    }

    free_var(arglist);
    panic_moo(msg);

    return make_error_pack(E_NONE);
}


static package
bf_shutdown(Var arglist, Byte next, void *vdata, Objid progr)
{
    int nargs = arglist.v.list[0].v.num;
    const char *message = (nargs >= 1 ? arglist.v.list[1].v.str : nullptr);

    if (!is_wizard(progr)) {
        free_var(arglist);
        return make_error_pack(E_PERM);
    }

    shutdown_triggered = true;
    shutdown_message << "shutdown() called by " << object_name(progr);
    if (message)
        shutdown_message << ": " << message;

    free_var(arglist);
    return no_var_pack();
}

static package
bf_dump_database(Var arglist, Byte next, void *vdata, Objid progr)
{
    free_var(arglist);
    if (!is_wizard(progr))
        return make_error_pack(E_PERM);

    checkpoint_requested = CHKPT_FUNC;
    return no_var_pack();
}

static package
bf_db_disk_size(Var arglist, Byte next, void *vdata, Objid progr)
{
    Var v;

    free_var(arglist);
    v.type = TYPE_INT;
    if ((v.v.num = db_disk_size()) < 0)
        return make_raise_pack(E_QUOTA, "No database file(s) available", zero);
    else
        return make_var_pack(v);
}

#ifdef OUTBOUND_NETWORK
static slistener *
find_slistener_by_oid(Objid obj)
{
    slistener *l;

    for (l = all_slisteners; l; l = l->next)
        if (l->oid == obj)
            return l;

    return nullptr;
}
#endif /* OUTBOUND_NETWORK */

static package
bf_open_network_connection(Var arglist, Byte next, void *vdata, Objid progr)
{
#ifdef OUTBOUND_NETWORK
    /* STR <host>, INT <port>[, MAP <options>]
    Options: ipv6 -> INT, listener -> OBJ, tls -> INT, tls_verify -> INT */

    Var r;
    package e;
    server_listener sl;
    slistener l;
    bool use_ipv6 = false;
#ifdef USE_TLS
    bool use_tls = false;
    bool verify_tls = false;
#endif

    if (!is_wizard(progr)) {
        free_var(arglist);
        return make_error_pack(E_PERM);
    }

    /* maplookup doesn't consume the key, so we may as well make these static instead
       of recreating and freeing them every time this function gets run...
       Additional shared keys exist at the top of server.cc */
    static Var listener_key = str_dup_to_var("listener");
#ifdef USE_TLS
    static Var tls_verify_key = str_dup_to_var("tls_verify");
#endif

    sl.ptr = nullptr;

    if (arglist.v.list[0].v.num >= 3) {
        Var options = arglist.v.list[3];
        Var value;

#ifdef USE_TLS
        if (maplookup(options, tls_key, &value, 0) != nullptr && is_true(value)) {
            if (!tls_ctx) {
                var_ref(value);
                free_var(arglist);
                return make_raise_pack(E_PERM, "TLS is not enabled", value);
            }
            use_tls = true;
        }

        if (maplookup(options, tls_verify_key, &value, 0) != nullptr && is_true(value))
            verify_tls = true;
#endif /* USE_TLS */

        if (maplookup(options, ipv6_key, &value, 0) != nullptr && is_true(value))
            use_ipv6 = true;

        if (maplookup(options, listener_key, &value, 0) != nullptr) {
            if (value.type != TYPE_OBJ) {
                var_ref(value);
                free_var(arglist);
                return make_raise_pack(E_TYPE, "listener should be an object", value);
            }

            sl.ptr = find_slistener_by_oid(value.v.obj);
            if (!sl.ptr) {
                /* Create a temporary */
                l.print_messages = 0;
                l.name = "open_network_connection";
                l.desc = zero;
                l.oid = value.v.obj;
                sl.ptr = &l;
            }
        }
    }

    e = network_open_connection(arglist, sl, use_ipv6 USE_TLS_BOOL);
    free_var(arglist);
    if (e.u.raise.code.v.err == E_NONE) {
        /* The connection was successfully opened, implying that
         * server_new_connection was called, implying and a new negative
         * player number was allocated for the connection.  Thus, the old
         * value of next_unconnected_player is the number of our connection.
         */
        r.type = TYPE_OBJ;
        r.v.obj = next_unconnected_player + 1;
        return make_var_pack(r);
    } else {
        return e;
    }

#else               /* !OUTBOUND_NETWORK */

    /* This function is disabled in this server. */

    free_var(arglist);
    return make_error_pack(E_PERM);

#endif
}

static package
bf_connected_players(Var arglist, Byte next, void *vdata, Objid progr)
{
    shandle *h;
    int nargs = arglist.v.list[0].v.num;
    int show_all = (nargs >= 1 && is_true(arglist.v.list[1]));
    int count = 0;
    Var result;

    free_var(arglist);

    all_shandles_mutex.lock();
    for (h = all_shandles; h; h = h->next)
        if ((show_all || h->connection_time != 0) && !h->disconnect_me.load())
            count++;

    result = new_list(count);
    count = 0;

    for (h = all_shandles; h; h = h->next) {
        if ((show_all || h->connection_time != 0) && !h->disconnect_me.load()) {
            count++;
            result.v.list[count].type = TYPE_OBJ;
            result.v.list[count].v.obj = h->player;
        }
    }
    all_shandles_mutex.unlock();

    return make_var_pack(result);
}

static package
bf_connected_seconds(Var arglist, Byte next, void *vdata, Objid progr)
{   /* (player) */
    Var r;
    shandle *h = find_shandle(arglist.v.list[1].v.obj);

    r.type = TYPE_INT;
    if (h && h->connection_time != 0 && !h->disconnect_me.load())
        r.v.num = time(nullptr) - h->connection_time;
    else
        r.v.num = -1;
    free_var(arglist);
    if (r.v.num < 0)
        return make_error_pack(E_INVARG);
    else
        return make_var_pack(r);
}

static package
bf_idle_seconds(Var arglist, Byte next, void *vdata, Objid progr)
{   /* (player) */
    Var r;
    shandle *h = find_shandle(arglist.v.list[1].v.obj);

    r.type = TYPE_INT;
    if (h && !h->disconnect_me.load())
        r.v.num = time(nullptr) - h->last_activity_time;
    else
        r.v.num = -1;
    free_var(arglist);
    if (r.v.num < 0)
        return make_error_pack(E_INVARG);
    else
        return make_var_pack(r);
}

static package
bf_connection_name(Var arglist, Byte next, void *vdata, Objid progr)
{   /* (player [, IP | LEGACY]) */
    Objid who = arglist.v.list[1].v.obj;
    shandle *h = find_shandle(who);
    Var r;

    r.type = TYPE_STR;
    r.v.str = nullptr;

    if (h && !h->disconnect_me) {
        lock_connection_name_mutex(h->nhandle);
        if (arglist.v.list[0].v.num == 1) {
            r.v.str = str_dup(network_connection_name(h->nhandle));
        } else if (arglist.v.list[2].v.num == 1)
            r.v.str = str_dup(network_ip_address(h->nhandle));
        else {
            char *full_conn_name = full_network_connection_name(h->nhandle, true);
            r.v.str = str_dup(full_conn_name);
            free(full_conn_name);
        }
        unlock_connection_name_mutex(h->nhandle);
    }

    free_var(arglist);
    if (!is_wizard(progr) && progr != who)
        return make_error_pack(E_PERM);
    else if (!r.v.str)
        return make_error_pack(E_INVARG);
    else {
        return make_var_pack(r);
    }
}

void
name_lookup_cleanup(void *extra_data)
{
    network_handle nh;
    nh.ptr = extra_data;

    decrement_nhandle_refcount(nh);
}

void
name_lookup_callback(Var arglist, Var *ret, void *extra_data)
{
    int nargs = arglist.v.list[0].v.num;
    Objid who = arglist.v.list[1].v.obj;
    shandle *h = find_shandle(who);
    bool rewrite_connect_name = nargs > 1 && is_true(arglist.v.list[2]);

    network_handle nh;
    nh.ptr = extra_data;

    if (!h || h->disconnect_me)
        make_error_map(E_INVARG, "Invalid connection", ret);
    else
    {
        const char *name;
        int status = lookup_network_connection_name(h->nhandle, &name);

        /* If the server is shutting down, this is meaningless and creates
         * a bit of a mess anyway. So don't bother continuing. */
        if (!shutdown_triggered.load()) {
            ret->type = TYPE_STR;
            ret->v.str = name;

            if (rewrite_connect_name && status == 0)
                if (network_name_lookup_rewrite(who, name, nh) != 0)
                    make_error_map(E_INVARG, "Failed to rewrite connection name.", ret);
        }
    }
}

static package
bf_name_lookup(Var arglist, Byte next, void *vdata, Objid progr)
{
    if (!is_wizard(progr) && progr != arglist.v.list[1].v.obj)
        return make_error_pack(E_PERM);

    /* The main thread should keep track of nhandle refcounts to
       ensure that close_nhandle doesn't pull the rug out from
       under the other threads. */
    shandle *h = find_shandle(arglist.v.list[1].v.obj);

    if (!h || h->disconnect_me) {
        free_var(arglist);
        return make_error_pack(E_INVARG);
    }

    increment_nhandle_refcount(h->nhandle);

    return background_thread(name_lookup_callback, &arglist, (void*)h->nhandle.ptr, name_lookup_cleanup);
}

static package
bf_notify(Var arglist, Byte next, void *vdata, Objid progr)
{   /* (player, string [, no_flush]) */
    Objid conn = arglist.v.list[1].v.obj;
    const char *line = arglist.v.list[2].v.str;
    int no_flush = (arglist.v.list[0].v.num > 2
                    ? is_true(arglist.v.list[3])
                    : 0);
    int no_newline = (arglist.v.list[0].v.num > 3
                      ? is_true(arglist.v.list[4]) : 0);

    shandle *h = find_shandle(conn);
    Var r;

    if (!is_wizard(progr) && progr != conn) {
        free_var(arglist);
        return make_error_pack(E_PERM);
    }
    r.type = TYPE_INT;
    if (h && !h->disconnect_me.load()) {
        if (h->binary) {
            int length;

            line = binary_to_raw_bytes(line, &length);
            if (!line) {
                free_var(arglist);
                return make_error_pack(E_INVARG);
            }
            r.v.num = network_send_bytes(h->nhandle, line, length, !no_flush);
        } else
            r.v.num = network_send_line(h->nhandle, line, !no_flush, !no_newline);
    } else {
        if (in_emergency_mode)
            emergency_notify(conn, line);
        r.v.num = 1;
    }
    free_var(arglist);
    return make_var_pack(r);
}

static package
bf_boot_player(Var arglist, Byte next, void *vdata, Objid progr)
{   /* (object) */
    Objid oid = arglist.v.list[1].v.obj;

    free_var(arglist);

    if (oid != progr && !is_wizard(progr))
        return make_error_pack(E_PERM);

    boot_player(oid);
    return no_var_pack();
}

static package
bf_set_connection_option(Var arglist, Byte next, void *vdata, Objid progr)
{   /* (conn, option, value) */
    Objid oid = arglist.v.list[1].v.obj;
    const char *option = arglist.v.list[2].v.str;
    Var value = arglist.v.list[3];
    shandle *h = find_shandle(oid);
    enum error e = E_NONE;

    if (oid != progr && !is_wizard(progr))
        e = E_PERM;
    else if (!h || h->disconnect_me.load()
             || (!server_set_connection_option(h, option, value)
                 && !tasks_set_connection_option(h->tasks, option, value)
                 && !network_set_connection_option(h->nhandle, option, value)))
        e = E_INVARG;

    free_var(arglist);
    if (e == E_NONE)
        return no_var_pack();
    else
        return make_error_pack(e);
}

static package
bf_connection_options(Var arglist, Byte next, void *vdata, Objid progr)
{   /* (conn [, opt-name]) */
    Objid oid = arglist.v.list[1].v.obj;
    int nargs = arglist.v.list[0].v.num;
    const char *oname = (nargs >= 2 ? arglist.v.list[2].v.str : nullptr);
    shandle *h = find_shandle(oid);
    Var ans;

    if (!h || h->disconnect_me.load()) {
        free_var(arglist);
        return make_error_pack(E_INVARG);
    } else if (oid != progr && !is_wizard(progr)) {
        free_var(arglist);
        return make_error_pack(E_PERM);
    }
    if (oname) {
        if (!server_connection_option(h, oname, &ans)
                && !tasks_connection_option(h->tasks, oname, &ans)
                && !network_connection_option(h->nhandle, oname, &ans)) {
            free_var(arglist);
            return make_error_pack(E_INVARG);
        }
    } else {
        ans = new_list(0);
        ans = server_connection_options(h, ans);
        ans = tasks_connection_options(h->tasks, ans);
        ans = network_connection_options(h->nhandle, ans);
    }

    free_var(arglist);
    return make_var_pack(ans);
}

static package
bf_connection_info(Var arglist, Byte next, void *vdata, Objid progr)
{   /* (conn) */
    Objid oid = arglist.v.list[1].v.obj;
    shandle *h = find_shandle(oid);

    if (!h || h->disconnect_me.load()) {
        free_var(arglist);
        return make_error_pack(E_INVARG);
    } else if (oid != progr && !is_wizard(progr)) {
        free_var(arglist);
        return make_error_pack(E_PERM);
    }

    // Avoid some mallocs
    static Var src_addr =   str_dup_to_var("source_address");
    static Var src_ip =   str_dup_to_var("source_ip");
    static Var src_port =   str_dup_to_var("source_port");
    static Var dest_addr =  str_dup_to_var("destination_address");
    static Var dest_ip =  str_dup_to_var("destination_ip");
    static Var dest_port =  str_dup_to_var("destination_port");
    static Var protocol =   str_dup_to_var("protocol");
    static Var is_outbound = str_dup_to_var("outbound");

    network_handle nh = h->nhandle;

    lock_connection_name_mutex(nh);

    Var ret = new_map();
    ret = mapinsert(ret, var_ref(src_addr), str_dup_to_var(network_source_connection_name(nh)));
    ret = mapinsert(ret, var_ref(src_port), Var::new_int(network_source_port(nh)));
    ret = mapinsert(ret, var_ref(src_ip), str_dup_to_var(network_source_ip_address(nh)));
    ret = mapinsert(ret, var_ref(dest_addr), str_dup_to_var(network_connection_name(nh)));
    ret = mapinsert(ret, var_ref(dest_port), Var::new_int(network_port(nh)));
    ret = mapinsert(ret, var_ref(dest_ip), str_dup_to_var(network_ip_address(nh)));
    ret = mapinsert(ret, var_ref(protocol), str_dup_to_var(network_protocol(nh)));
    ret = mapinsert(ret, var_ref(is_outbound), Var::new_int(h->outbound));
#ifdef USE_TLS
    ret = mapinsert(ret, var_ref(tls_key), tls_connection_info(nh));
#endif

    unlock_connection_name_mutex(nh);
    free_var(arglist);
    return make_var_pack(ret);
}

static slistener *
find_slistener(Var desc, bool use_ipv6)
{
    slistener *l;

    for (l = all_slisteners; l; l = l->next)
        if (equality(desc, l->desc, 0) && l->ipv6 == use_ipv6)
            return l;

    return nullptr;
}

static package
bf_listen(Var arglist, Byte next, void *vdata, Objid progr)
{   /* (oid, desc) */
    Objid oid = arglist.v.list[1].v.obj;
    Var desc = arglist.v.list[2];
    int print_messages = 0;
    bool ipv6 = false;
    enum error e = E_NONE;
    slistener *l = nullptr;
    char error_msg[100];
    const char *interface = nullptr;
#ifdef USE_TLS
    bool use_tls = false;
    const char *certificate_path = nullptr;
    const char *key_path = nullptr;
#endif

    /* maplookup doesn't consume the key, so we make some static values to save recreation every time
       Additional shared keys exist at the top of server.cc */
    static Var print_messages_key = str_dup_to_var("print-messages");
#ifdef USE_TLS
    static Var tls_cert = str_dup_to_var("certificate");
    static Var tls_key_key = str_dup_to_var("key");
#endif

    if (arglist.v.list[0].v.num >= 3) {
        Var options = arglist.v.list[3];
        Var value;

#ifdef USE_TLS
        if (maplookup(options, tls_key, &value, 0) != nullptr && is_true(value)) {
            if (!tls_ctx) {
                e = E_INVARG;
                sprintf(error_msg, "TLS is not enabled");
            } else {
                use_tls = true;
            }
        }

        if (maplookup(options, tls_cert, &value, 0) != nullptr) {
            if (value.type != TYPE_STR) {
                e = E_INVARG;
                sprintf(error_msg, "Certificate path should be a string");
            } else {
                certificate_path = str_dup(value.v.str);
            }
        }

        if (maplookup(options, tls_key_key, &value, 0) != nullptr) {
            if (value.type != TYPE_STR) {
                e = E_INVARG;
                sprintf(error_msg, "Private key path should be a string");
            } else {
                key_path = str_dup(value.v.str);
            }
        }
#endif

        if (maplookup(options, ipv6_key, &value, 0) != nullptr && is_true(value))
            ipv6 = true;

        if (maplookup(options, print_messages_key, &value, 0) != nullptr && is_true(value))
            print_messages = 1;

        if (maplookup(options, interface_key, &value, 0) != nullptr && value.type == TYPE_STR)
            interface = value.v.str;
    }

    if (e == E_NONE) {
        if (!is_wizard(progr)) {
            e = E_PERM;
            sprintf(error_msg, "Permission denied");
        } else if (!valid(oid) || find_slistener(desc, ipv6)) {
            e = E_INVARG;
            sprintf(error_msg, "Invalid argument");
        } else if (!(l = new_slistener(oid, desc, print_messages, &e, ipv6, interface USE_TLS_BOOL TLS_CERT_PATH))) {
            sprintf(error_msg, unparse_error(e));
            /* Do nothing; e is already set */
        } else if (!start_listener(l)) {
            e = E_QUOTA;
            sprintf(error_msg, "Failed to listen on port");
        }
    }

    free_var(arglist);

    if (e == E_NONE)
        return make_var_pack(var_ref(l->desc));
    else {
#ifdef USE_TLS
        if (certificate_path)
            free_str(certificate_path);
        if (key_path)
            free_str(key_path);
#endif
        return make_raise_pack(e, error_msg, var_ref(zero));
    }
}

static package
bf_unlisten(Var arglist, Byte next, void *vdata, Objid progr)
{   /* (desc) */
    Var desc = arglist.v.list[1];
    bool ipv6 = arglist.v.list[0].v.num >= 2 && is_true(arglist.v.list[2]);
    enum error e = E_NONE;
    slistener *l = nullptr;

    if (!is_wizard(progr))
        e = E_PERM;
    else if (!(l = find_slistener(desc, ipv6)))
        e = E_INVARG;

    free_var(arglist);
    if (e == E_NONE) {
        free_slistener(l);
        return no_var_pack();
    } else
        return make_error_pack(e);
}

static package
bf_listeners(Var arglist, Byte next, void *vdata, Objid progr)
{   /* (find) */
    const int nargs = arglist.v.list[0].v.num;
    Var entry, list = new_list(0);
    bool find_listener = nargs == 1 ? true : false;
    const Var find = find_listener ? arglist.v.list[1] : var_ref(zero);
    slistener *l;

// Save the keys for later
    static const Var object = str_dup_to_var("object");
    static const Var port = str_dup_to_var("port");
    static const Var print = str_dup_to_var("print-messages");

    for (l = all_slisteners; l; l = l->next) {
        if (!find_listener || equality(find, (find.type == TYPE_OBJ) ? Var::new_obj(l->oid) : l->desc, 0)) {
            entry = new_map();
            entry = mapinsert(entry, var_ref(object), Var::new_obj(l->oid));
            entry = mapinsert(entry, var_ref(port), var_ref(l->desc));
            entry = mapinsert(entry, var_ref(print), Var::new_int(l->print_messages));
            entry = mapinsert(entry, var_ref(ipv6_key), Var::new_int(l->ipv6));
            entry = mapinsert(entry, var_ref(interface_key), str_dup_to_var(l->name));
#ifdef USE_TLS
            entry = mapinsert(entry, var_ref(tls_key), Var::new_int(nlistener_is_tls(l->nlistener.ptr)));
#endif
            list = listappend(list, entry);
        }
    }

    free_var(arglist);
    return make_var_pack(list);
}

static package
bf_buffered_output_length(Var arglist, Byte next, void *vdata, Objid progr)
{   /* ([connection]) */
    int nargs = arglist.v.list[0].v.num;
    Objid conn = nargs >= 1 ? arglist.v.list[1].v.obj : 0;
    Var r;

    free_var(arglist);
    r.type = TYPE_INT;
    if (nargs == 0)
        r.v.num = server_flag_option_cached(SVO_MAX_QUEUED_OUTPUT);
    else {
        shandle *h = find_shandle(conn);

        if (!h)
            return make_error_pack(E_INVARG);
        else if (progr != conn && !is_wizard(progr))
            return make_error_pack(E_PERM);

        r.v.num = network_buffered_output_length(h->nhandle);
    }

    return make_var_pack(r);
}

bool is_shutdown_triggered()
{
    return shutdown_triggered;
}

void
register_server(void)
{
    register_function("server_version", 0, 1, bf_server_version, TYPE_ANY);
    register_function("renumber", 1, 1, bf_renumber, TYPE_OBJ);
    register_function("reset_max_object", 0, 0, bf_reset_max_object);
    register_function("memory_usage", 0, 0, bf_memory_usage);
#ifdef JEMALLOC_FOUND
    register_function("malloc_stats", 0, 0, bf_malloc_stats);
#endif
    register_function("usage", 0, 0, bf_usage);
    register_function("panic", 0, 1, bf_panic, TYPE_STR);
    register_function("shutdown", 0, 1, bf_shutdown, TYPE_STR);
    register_function("dump_database", 0, 0, bf_dump_database);
    register_function("db_disk_size", 0, 0, bf_db_disk_size);
    register_function("open_network_connection", 2, 3, bf_open_network_connection,
                      TYPE_STR, TYPE_INT, TYPE_MAP);
    register_function("connected_players", 0, 1, bf_connected_players,
                      TYPE_ANY);
    register_function("connected_seconds", 1, 1, bf_connected_seconds,
                      TYPE_OBJ);
    register_function("idle_seconds", 1, 1, bf_idle_seconds, TYPE_OBJ);
    register_function("connection_name", 1, 2, bf_connection_name, TYPE_OBJ, TYPE_INT);
    register_function("notify", 2, 4, bf_notify, TYPE_OBJ, TYPE_STR, TYPE_ANY, TYPE_ANY);
    register_function("boot_player", 1, 1, bf_boot_player, TYPE_OBJ);
    register_function("set_connection_option", 3, 3, bf_set_connection_option,
                      TYPE_OBJ, TYPE_STR, TYPE_ANY);
    register_function("connection_options", 1, 2, bf_connection_options,
                      TYPE_OBJ, TYPE_STR);
    register_function("connection_info", 1, 1, bf_connection_info, TYPE_OBJ);
    register_function("connection_name_lookup", 1, 2, bf_name_lookup, TYPE_OBJ, TYPE_ANY);
    register_function("listen", 2, 3, bf_listen, TYPE_OBJ, TYPE_ANY, TYPE_MAP);
    register_function("unlisten", 1, 2, bf_unlisten, TYPE_ANY, TYPE_ANY);
    register_function("listeners", 0, 1, bf_listeners, TYPE_ANY);
    register_function("buffered_output_length", 0, 1,
                      bf_buffered_output_length, TYPE_OBJ);
}
