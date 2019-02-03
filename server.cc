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

#include <string>
#include <sstream>
#include <fstream>
#include <vector>

#include "my-types.h"		/* must be first on some systems */
#include "my-signal.h"
#include "my-stdarg.h"
#include "my-stdio.h"
#include "my-stdlib.h"
#include "my-string.h"
#include "my-unistd.h"
#include "my-wait.h"

#include "config.h"
#include "db.h"
#include "db_io.h"
#include "disassemble.h"
#include "exec.h"
#include "execute.h"
#include "functions.h"
#include "garbage.h"
#include "list.h"
#include "log.h"
#include "nettle/sha2.h"
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
#include "net_multi.h"  /* rewrite connection name */
#include "waif.h" /* recycled_waifs */

extern "C" {
#include "linenoise.h"
}

static pid_t parent_pid;
static bool in_child = false;

static std::stringstream shutdown_message;
static bool shutdown_triggered = false;

static bool in_emergency_mode = false;

static Var checkpointed_connections;

typedef enum {
    CHKPT_OFF, CHKPT_TIMER, CHKPT_SIGNAL, CHKPT_FUNC
} Checkpoint_Reason;
static Checkpoint_Reason checkpoint_requested = CHKPT_OFF;

static int checkpoint_finished = 0;	/* 1 = failure, 2 = success */

typedef struct shandle {
    struct shandle *next, **prev;
    network_handle nhandle;
    time_t connection_time;
    time_t last_activity_time;
    Objid player;
    Objid listener;
    task_queue tasks;
    int disconnect_me;
    int outbound, binary;
    int print_messages;
} shandle;

static shandle *all_shandles = 0;

typedef struct slistener {
    struct slistener *next, **prev;
    network_listener nlistener;
    Objid oid;			/* listen(OID, DESC, PRINT_MESSAGES) */
    Var desc;
    int print_messages;
    const char *name;
} slistener;

static slistener *all_slisteners = 0;

server_listener null_server_listener = {0};

struct pending_recycle {
    struct pending_recycle *next;
    Var v;
};

static struct pending_recycle *pending_free = 0;
static struct pending_recycle *pending_head = 0;
static struct pending_recycle *pending_tail = 0;
static unsigned int pending_count = 0;

/* used once when the server loads the database */
static Var pending_list = new_list(0);

static void
free_shandle(shandle * h)
{
    *(h->prev) = h->next;
    if (h->next)
	h->next->prev = h->prev;

    free_task_queue(h->tasks);

    myfree(h, M_NETWORK);
}

static slistener *
new_slistener(Objid oid, Var desc, int print_messages, enum error *ee)
{
    slistener *l = (slistener *)mymalloc(sizeof(slistener), M_NETWORK);
    server_listener sl;
    enum error e;
    const char *name;

    sl.ptr = l;
    e = network_make_listener(sl, desc, &(l->nlistener), &(l->desc), &name);

    if (ee)
	*ee = e;

    if (e != E_NONE) {
	myfree(l, M_NETWORK);
	return 0;
    }
    l->oid = oid;
    l->print_messages = print_messages;
    l->name = str_dup(name);

    l->next = all_slisteners;
    l->prev = &all_slisteners;
    if (all_slisteners)
	all_slisteners->prev = &(l->next);
    all_slisteners = l;

    return l;
}

static int
start_listener(slistener * l)
{
    if (network_listen(l->nlistener)) {
	oklog("LISTEN: #%d now listening on %s\n", l->oid, l->name);
	return 1;
    } else {
	errlog("LISTEN: Can't start #%d listening on %s!\n", l->oid, l->name);
	return 0;
    }
}

static void
free_slistener(slistener * l)
{
    network_close_listener(l->nlistener);
    oklog("UNLISTEN: #%d no longer listening on %s\n", l->oid, l->name);

    *(l->prev) = l->next;
    if (l->next)
	l->next->prev = l->prev;

    free_var(l->desc);
    free_str(l->name);

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
panic(const char *message)
{
    static int in_panic = 0;

    errlog("PANIC%s: %s\n", in_child ? " (in child)" : "", message);
    if (in_panic) {
	errlog("RECURSIVE PANIC: aborting\n");
	abort_server();
    }
    in_panic = 1;

    log_command_history();

    if (in_child) {		/* We're a forked checkpointer */
	errlog("Child shutting down parent via USR1 signal\n");
	kill(parent_pid, SIGUSR1);
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
    panic(message);
}

static void
shutdown_signal(int sig)
{
    shutdown_triggered = true;
    shutdown_message << "shutdown signal received";
}

static void
checkpoint_signal(int sig)
{
    checkpoint_requested = CHKPT_SIGNAL;

    signal(sig, checkpoint_signal);
}

static void
call_checkpoint_notifier(int successful)
{
    Var args;

    args = new_list(1);
    args.v.list[1].type = TYPE_INT;
    args.v.list[1].v.num = successful;
    run_server_task(-1, Var::new_obj(SYSTEM_OBJECT), "checkpoint_finished", args, "", 0);
}

static void
child_completed_signal(int sig)
{
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
	checkpoint_finished = (status == 0) + 1;	/* 1 = failure, 2 = success */
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
    signal(SIGUSR1, shutdown_signal);	/* remote shutdown signal */
    signal(SIGUSR2, checkpoint_signal);		/* remote checkpoint signal */

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
    int interval, now = time(0);
    static Timer_ID last_checkpoint_timer;

   interval = server_int_option("dump_interval", 3600);
    if (interval < 60 || now + interval < now) {
	interval = 3600;	/* Once per hour */
    }

    if (!first_time)
	cancel_timer(last_checkpoint_timer);
    last_checkpoint_timer = set_timer(interval, checkpoint_timer, 0);
}

static const char *
object_name(Objid oid)
{
    static Stream *s = 0;

    if (!s)
	s = new_stream(30);

    if (valid(oid))
	stream_printf(s, "%s (#%d)", db_object_name(oid), oid);
    else
	stream_printf(s, "#%d", oid);

    return reset_stream(s);
}

static void
call_notifier(Objid player, Objid handler, const char *verb_name)
{
    Var args;

    args = new_list(1);
    args.v.list[1].type = TYPE_OBJ;
    args.v.list[1].v.obj = player;
    run_server_task(player, Var::new_obj(handler), verb_name, args, "", 0);
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
send_message(Objid listener, network_handle nh, const char *msg_name,...)
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
    } else			/* Use default message */
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
	pending_free->next = NULL;
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
    pending_head = pending_tail = NULL;
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
	run_server_task(-1, v, "recycle", new_list(0), "", 0);

	/* We'd like to run `db_change_parents()' to be consistent
	 * with the pattern laid out in `bf_destroy()', but we can't
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
    /* This seems like a lot of work to go through just to get a recycle verb name.
     * Maybe it should just be a #define in waif.h? Ah well.*/
    static char *waif_recycle_verb = NULL;
    if (!waif_recycle_verb) {
        waif_recycle_verb = (char *)mymalloc(9, M_STRING);
        waif_recycle_verb[0] = WAIF_VERB_PREFIX;
        strcpy(waif_recycle_verb + 1, "recycle");
    }

    std::vector<Waif*> removals;
    for (auto &x: recycled_waifs) {
        if (recycled_waifs[x.first] == false) {
            run_server_task(-1, Var::new_waif(x.first), waif_recycle_verb, new_list(0), "", 0);
            recycled_waifs[x.first] = true;
            /* Flag it as recycled. Now we just wait for the refcount to hit zero so we can free it. */
        }
        if (refcount(x.first) == 0) {
            removals.push_back(x.first);
        }
      }  
      for (auto x:removals) {
        free_waif(x);
        recycled_waifs.erase(x);
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
    dbio_printf("%d values pending finalization\n", pending_count);

    struct pending_recycle *head = pending_head;

    while (head) {
	dbio_write_var(head->v);
	head = head->next;
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
    int i, res;

    /* First, queue anonymous objects */
    for (i = 1; i <= pending_list.v.list[0].v.num; i++) {
	Var v;

	v = pending_list.v.list[i];

	/* in theory this could be any value... */
	/* in practice this will be an anonymous object... */
	assert(TYPE_ANON == v.type);

	if (v.v.anon != NULL)
	    queue_anonymous_object(var_ref(v));
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
    run_server_task(-1, Var::new_obj(SYSTEM_OBJECT), "server_started", new_list(0), "", 0);
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

	if (checkpoint_requested != CHKPT_OFF) {
	    if (checkpoint_requested == CHKPT_SIGNAL)
		oklog("CHECKPOINTING due to remote request signal.\n");
	    checkpoint_requested = CHKPT_OFF;
	    run_server_task(-1, Var::new_obj(SYSTEM_OBJECT), "checkpoint_started",
			    new_list(0), "", 0);
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

	res = network_process_io(useconds_left);

	run_ready_tasks();

	/* If a exec'd child process exited, deal with it here */
	deal_with_child_exit();

	{			/* Get rid of old un-logged-in or useless connections */
	    int now = time(0);

	    for (h = all_shandles; h; h = nexth) {
		Var v;

		nexth = h->next;

		if (!h->outbound && h->connection_time == 0
		    && (get_server_option(h->listener, "connect_timeout", &v)
			? (v.type == TYPE_INT && v.v.num > 0
			   && now - h->last_activity_time > v.v.num)
			: (now - h->last_activity_time
			   > DEFAULT_CONNECT_TIMEOUT))) {
		    call_notifier(h->player, h->listener, "user_disconnected");
		    oklog("TIMEOUT: #%d on %s\n",
			  h->player,
			  network_connection_name(h->nhandle));
		    if (h->print_messages)
			send_message(h->listener, h->nhandle, "timeout_msg",
				     "*** Timed-out waiting for login. ***",
				     0);
		    network_close(h->nhandle);
		    free_shandle(h);
		} else if (h->connection_time != 0 && !valid(h->player)) {
		    oklog("RECYCLED: #%d on %s\n",
			  h->player,
			  network_connection_name(h->nhandle));
		    if (h->print_messages)
			send_message(h->listener, h->nhandle,
				     "recycle_msg", "*** Recycled ***", 0);
		    network_close(h->nhandle);
		    free_shandle(h);
		} else if (h->disconnect_me) {
		    call_notifier(h->player, h->listener,
				  "user_disconnected");
		    oklog("DISCONNECTED: %s on %s\n",
			  object_name(h->player),
			  network_connection_name(h->nhandle));
		    if (h->print_messages)
			send_message(h->listener, h->nhandle, "boot_msg",
				     "*** Disconnected ***", 0);
		    network_close(h->nhandle);
		    free_shandle(h);
		}
	    }
	}
    }

    applog(LOG_WARNING, "SHUTDOWN: %s\n", shutdown_message.str().c_str());
    send_shutdown_message(shutdown_message.str().c_str());
}

static shandle *
find_shandle(Objid player)
{
    shandle *h;

    for (h = all_shandles; h; h = h->next)
	if (h->player == player)
	    return h;

    return 0;
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

#define SERVER_CO_TABLE(DEFINE, H, VALUE, _)				\
    DEFINE(binary, _, TYPE_INT, num,					\
	   H->binary,							\
	   {								\
	       H->binary = is_true(VALUE);				\
	       network_set_connection_binary(H->nhandle, H->binary);	\
	   })								\

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
    static Stream *s = 0;

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
    printf("#%d <- %s\n", player, line);
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
		printf("** Object #%d is not a wizard...\n", wizard);

	    for (wizard = 0; wizard <= db_last_used_objid(); wizard++)
		if (is_wizard(wizard))
		    break;
		else if (valid(wizard) && first_valid < 0)
		    first_valid = wizard;

	    if (!is_wizard(wizard)) {
		if (first_valid < 0) {
		    first_valid = db_create_object();
		    db_change_parents(Var::new_obj(first_valid), new_list(0), none);
		    printf("** No objects in database; created #%d.\n",
			   first_valid);
		}
		wizard = first_valid;
		db_set_object_flag(wizard, FLAG_WIZARD);
		printf("** No wizards in database; wizzed #%d.\n", wizard);
	    }
	    printf("** Now running emergency commands as #%d ...\n\n", wizard);
	}
        char prompt[100];
        sprintf(prompt, "(#%d)%s: ", wizard, debug ? "" : "[!d]");
	line = read_stdin_line(prompt);

	if (!line)
	    start_ok = 0;	/* treat EOF as "quit" */
	else if (*line == ';') {	/* eval command */
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

	    if (*line == '\0') {	/* long form */
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

		printf("** %d errors during parsing:\n",
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

			printf("** %d errors during parsing:\n",
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
		       && sscanf(words.v.list[2].v.str, "#%d", &wizard) == 1) {
		printf("** Switching to wizard #%d...\n", wizard);
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
#if NETWORK_PROTOCOL != NP_SINGLE
    fclose(stdout);
#endif

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
    std::string line;

    if (!file.is_open()) {
	panic(strerror(errno));
    }
    while (std::getline(file, line)) {
	str = str_dup_to_var(raw_bytes_to_clean(line.c_str(), line.size()));
	code = listappend(code, str);
    }
    if (errno) {
	panic(strerror(errno));
    }

    run_do_start_script(code);
}

static void
init_random(void)
{
    long seed;

    sha256_ctx context;
    unsigned char input[32];
    unsigned char output[32];

    memset(input, 0, sizeof(input));
    memset(output, 0, sizeof(output));

    sha256_init(&context);

#ifndef TEST
#ifdef HAVE_RANDOM_DEVICE

    oklog("RANDOM: seeding from " RANDOM_DEVICE "\n");

    int fd;

    if ((fd = open(RANDOM_DEVICE, O_RDONLY)) == -1) {
	errlog("Can't open " RANDOM_DEVICE "!\n");
	exit(1);
    }

    ssize_t count = 0, total = 0;
    ssize_t required = MIN(MINIMUM_SEED_ENTROPY, sizeof(input));

    while (total < required) {
	if (total)
	    oklog("RANDOM: seeding ... (more bytes required)\n");
	if ((count = read(fd, input + total, sizeof(input) - total)) == -1) {
	    errlog("Can't read " RANDOM_DEVICE "!\n");
	    exit(1);
	}
        total += count;
    }

    sha256_update(&context, sizeof(input), input);

    close(fd);

#else

    oklog("RANDOM: seeding from internal sources\n");

    time_t time_now = time(0);

    sha256_update(&context, sizeof(time_now), (const unsigned char *)&time_now);
    sha256_update(&context, sizeof(parent_pid), (const unsigned char *)&parent_pid);

#endif
#else /* #ifndef TEST */

    oklog("RANDOM: (-DTEST) not seeding!\n");

#endif

    sha256_digest(&context, sizeof(output), output);

    sosemanuk_schedule(&key_context, output, sizeof(output));

    sosemanuk_init(&run_context, &key_context, NULL, 0);

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
	*p++ = ' ';		/* Pad with blanks, not nulls; on SunOS and
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

const char *
server_string_option(const char *name, const char *defallt)
{
    Var v;

    if (get_server_option(SYSTEM_OBJECT, name, &v))
	return (v.type == TYPE_STR ? v.v.str : 0);
    else
	return defallt;
}

static Objid next_unconnected_player = NOTHING - 1;

server_handle
server_new_connection(server_listener sl, network_handle nh, int outbound)
{
    slistener *l = (slistener *)sl.ptr;
    shandle *h = (shandle *)mymalloc(sizeof(shandle), M_NETWORK);
    server_handle result;

    h->next = all_shandles;
    h->prev = &all_shandles;
    if (all_shandles)
	all_shandles->prev = &(h->next);
    all_shandles = h;

    h->nhandle = nh;
    h->connection_time = 0;
    h->last_activity_time = time(0);
    h->player = next_unconnected_player--;
    h->listener = l ? l->oid : SYSTEM_OBJECT;
    h->tasks = new_task_queue(h->player, h->listener);
    h->disconnect_me = 0;
    h->outbound = outbound;
    h->binary = 0;
    h->print_messages = l ? l->print_messages : !outbound;

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

    oklog("%s: #%d on %s\n",
	  outbound ? "CONNECT" : "ACCEPT",
	  h->player, network_connection_name(nh));

    result.ptr = h;
    return result;
}

void
server_refuse_connection(server_listener sl, network_handle nh)
{
    slistener *l = (slistener *)sl.ptr;

    if (l->print_messages)
	send_message(l->oid, nh, "server_full_msg",
		     "*** Sorry, but the server cannot accept any more"
		     " connections right now.",
		     "*** Please try again later.",
		     0);
    errlog("SERVER FULL: refusing connection on %s from %s\n",
	   l->name, network_connection_name(nh));
}

void
server_receive_line(server_handle sh, const char *line, bool out_of_band)
{
    shandle *h = (shandle *) sh.ptr;

    h->last_activity_time = time(0);
    new_input_task(h->tasks, line, h->binary, out_of_band);
}

void
server_close(server_handle sh)
{
    shandle *h = (shandle *) sh.ptr;

    oklog("CLIENT DISCONNECTED: %s on %s\n",
	  object_name(h->player),
	  network_connection_name(h->nhandle));
    h->disconnect_me = 1;
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

void
player_connected_silent(Objid old_id, Objid new_id)
{
    const char *old_name = str_dup(object_name(old_id));
    shandle *existing_h = find_shandle(new_id);
    shandle *new_h = find_shandle(old_id);

    if (!new_h)
	panic("Non-existent shandle connected");

    new_h->player = new_id;
    new_h->connection_time = time(0);

    if (existing_h) {
	/* network_connection_name is allowed to reuse the same string
	 * storage, so we have to copy one of them.
	 */
	char *name1 = str_dup(network_connection_name(existing_h->nhandle));
	oklog("REDIRECTED: %s, was %s, now %s\n",
	      object_name(new_id),
	      name1,
	      network_connection_name(new_h->nhandle));
	free_str(name1);
	network_close(existing_h->nhandle);
	free_shandle(existing_h);
    } else {
	oklog("SWITCHED: %s is now %s on %s\n",
	      old_name,
          object_name(new_h->player),
	      network_connection_name(new_h->nhandle));
    }
    free_str(old_name);
}

char
is_localhost(Objid connection)
{
    shandle *existing_h = find_shandle(connection);
    if (existing_h && strstr(network_connection_name(existing_h->nhandle), "[127.0.0.1]") != NULL)
        return 1;
    else
        return 0;
}

void
proxy_connected(Objid connection, Stream *new_connection_name)
{
    shandle *existing_h = find_shandle(connection);
    if (existing_h)
        rewrite_connection_name(existing_h->nhandle, new_connection_name);
}

void
player_connected(Objid old_id, Objid new_id, int is_newly_created)
{
    shandle *existing_h = find_shandle(new_id);
    shandle *new_h = find_shandle(old_id);

    if (!new_h)
	panic("Non-existent shandle connected");

    new_h->player = new_id;
    new_h->connection_time = time(0);

    if (existing_h) {
	/* we now have two shandles with the same player value while
	 * find_shandle assumes there can only be one.  This needs to
	 * be remedied before any call_notifier() call; luckily, the
	 * latter only needs listener value.
	 */
	Objid existing_listener = existing_h->listener;
	/* network_connection_name is allowed to reuse the same string
	 * storage, so we have to copy one of them.
	 */
	char *name1 = str_dup(network_connection_name(existing_h->nhandle));
	oklog("REDIRECTED: %s, was %s, now %s\n",
	      object_name(new_id),
	      name1,
	      network_connection_name(new_h->nhandle));
	free_str(name1);
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
	    new_h->disconnect_me = 1;
	    call_notifier(new_id, existing_listener,
			  "user_client_disconnected");
	    new_h->disconnect_me = 0;
	    call_notifier(new_id, new_h->listener, "user_connected");
	}
    } else {
	oklog("%s: %s on %s\n",
	      is_newly_created ? "CREATED" : "CONNECTED",
	      object_name(new_h->player),
	      network_connection_name(new_h->nhandle));
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

int
is_player_connected(Objid player)
{
    shandle *h = find_shandle(player);
    return !h || h->disconnect_me ? 0 : 1;
}

void
notify(Objid player, const char *message)
{
    shandle *h = find_shandle(player);

    if (h && !h->disconnect_me)
	network_send_line(h->nhandle, message, 1, 1);
    else if (in_emergency_mode)
	emergency_notify(player, message);
}

void
boot_player(Objid player)
{
    shandle *h = find_shandle(player);

    if (h)
	h->disconnect_me = 1;
}

void
write_active_connections(void)
{
    int count = 0;
    shandle *h;

    for (h = all_shandles; h; h = h->next)
	count++;

    dbio_printf("%d active connections with listeners\n", count);

    for (h = all_shandles; h; h = h->next)
	dbio_printf("%d %d\n", h->player, h->listener);
}

int
read_active_connections(void)
{
    int count, i, have_listeners = 0;
    char c;

    i = dbio_scanf("%d active connections%c", &count, &c);
    if (i == EOF) {		/* older database format */
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
	    if (dbio_scanf("%d %d\n", &who, &listener) != 2) {
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

    int waif_conversion_type = _TYPE_WAIF;    /* For shame. We can remove this someday. */

int
main(int argc, char **argv)
{
    char *this_program = str_dup(argv[0]);
    const char *log_file = 0;
    const char *script_file = 0;
    const char *script_line = 0;
    int script_file_first = 0;
    int emergency = 0;
    Var desc;
    slistener *l;

    init_cmdline(argc, argv);

    argc--;
    argv++;
    while (argc > 0 && argv[0][0] == '-') {
	/* Deal with any command-line options */
	switch (argv[0][1]) {
	case 'e':		/* Emergency wizard mode */
	    emergency = 1;
	    break;
	case 'l':		/* Specified log file */
	    if (argc > 1) {
		log_file = argv[1];
		argc--;
		argv++;
	    } else
		argc = 0;
	    break;
	case 'f':		/* Specified file of code */
	    if (argc > 1) {
		if (!script_line)
		    script_file_first = 1;
		script_file = argv[1];
		argc--;
		argv++;
	    } else
		argc = 0;
	    break;
	case 'c':		/* Specified line of code */
	    if (argc > 1) {
		if (!script_file)
		    script_file_first = 0;
		script_line = argv[1];
		argc--;
		argv++;
	    } else
		argc = 0;
	    break;
    case 'w':       /* Specified an old waif type for conversion */
        if (argc > 1) {
            waif_conversion_type = atoi(argv[1]);
            argc--;
            argv++;
        } else
            argc = 0;
        break;
    default:
	    argc = 0;		/* Provoke usage message below */
	}
	argc--;
	argv++;
    }

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

    applog(LOG_INFO1, "           _____                ______\n");
    applog(LOG_INFO1, "  ___________  /_____  _________ __  /_\n");
    applog(LOG_INFO1, "   __  ___/_  __/_  / / /__  __ \\_  __/\n");
    applog(LOG_INFO1, "   _(__  ) / /_  / /_/ / _  / / // /_\n");
    applog(LOG_INFO1, "   /____/  \\__/  \\__,_/  /_/ /_/ \\__/\n");
    applog(LOG_INFO1, "\n");

    if ((emergency && (script_file || script_line))
	|| !db_initialize(&argc, &argv)
	|| !network_initialize(argc, argv, &desc)) {
	fprintf(stderr, "Usage: %s [-e] [-f script-file] [-c script-line] [-l log-file] [-w waif-type] %s %s\n",
		this_program, db_usage_string(), network_usage_string());
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "\t-e\t\temergency wizard mode\n");
	fprintf(stderr, "\t-f\t\tfile to load and pass to `#0:do_start_script()'\n");
	fprintf(stderr, "\t-c\t\tline to pass to `#0:do_start_script()'\n");
	fprintf(stderr, "\t-l\t\toptional log file\n");
    fprintf(stderr, "\t-w\t\tconvert waifs from the specified type to the proper type (check with typeof(waif) in your MOO)\n\n");
	fprintf(stderr, "The emergency mode switch (-e) may not be used with either the file (-f) or line (-c) options.\n\n");
	fprintf(stderr, "Both the file and line options may be specified. Their order on the command line determines the order of their invocation.\n\n");
	fprintf(stderr, "Examples: \n");
	fprintf(stderr, "\t%s -c '$enable_debugging();' -f development.moo Minimal.db Minimal.db.new 7777\n", this_program);
	fprintf(stderr, "\t%s Minimal.db Minimal.db.new\n", this_program);
	exit(1);
    }
#if NETWORK_PROTOCOL != NP_SINGLE
    if (!emergency)
	fclose(stdout);
#endif
    if (log_file)
	fclose(stderr);

    parent_pid = getpid();

    applog(LOG_INFO1, "STARTING: Version %s of the Stunt/LambdaMOO server\n", server_version);
    oklog("          (Using %s protocol)\n", network_protocol_name());
    oklog("          (Task timeouts measured in %s seconds.)\n",
	  virtual_timer_available()? "server CPU" : "wall-clock");
    oklog("          (Process id %d)\n", parent_pid);
    if (waif_conversion_type != _TYPE_WAIF)
        applog(LOG_WARNING, "(Using type '%i' for waifs; will convert to '%i' at next checkpoint)\n", waif_conversion_type, _TYPE_WAIF);

    register_bi_functions();

    l = new_slistener(SYSTEM_OBJECT, desc, 1, 0);
    if (!l) {
	errlog("Can't create initial connection point!\n");
	exit(1);
    }
    free_var(desc);

    if (!db_load())
	exit(1);

    free_reordered_rt_env_values();

    load_server_options();

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
	if (!start_listener(l))
	    exit(1);

	main_loop();

	network_shutdown();
    }

    gc_collect();

    db_shutdown();

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

static package
bf_shutdown(Var arglist, Byte next, void *vdata, Objid progr)
{
    int nargs = arglist.v.list[0].v.num;
    const char *message = (nargs >= 1 ? arglist.v.list[1].v.str : 0);

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

static package
bf_clear_ancestor_cache(Var arglist, Byte next, void *vdata, Objid progr)
{
    free_var(arglist);

    if (!is_wizard(progr))
	return make_error_pack(E_PERM);

    db_clear_ancestor_cache();
    return no_var_pack();
}

#ifdef OUTBOUND_NETWORK
static slistener *
find_slistener_by_oid(Objid obj)
{
    slistener *l;

    for (l = all_slisteners; l; l = l->next)
	if (l->oid == obj)
	    return l;

    return 0;
}
#endif /* OUTBOUND_NETWORK */

static package
bf_open_network_connection(Var arglist, Byte next, void *vdata, Objid progr)
{
#ifdef OUTBOUND_NETWORK

    Var r;
    enum error e;
    server_listener sl;
    slistener l;

    if (!is_wizard(progr)) {
        free_var(arglist);
        return make_error_pack(E_PERM);
    }

    if (arglist.v.list[0].v.num == 3) {
	Objid oid;

	if (arglist.v.list[3].type != TYPE_OBJ) {
	    return make_error_pack(E_TYPE);
	}
	oid = arglist.v.list[3].v.obj;
	arglist = listdelete(arglist, 3);

	sl.ptr = find_slistener_by_oid(oid);
	if (!sl.ptr) {
	    /* Create a temporary */
	    l.print_messages = 0;
	    l.name = "open_network_connection";
	    l.desc = zero;
	    l.oid = oid;
	    sl.ptr = &l;
	}
    } else {
	sl.ptr = NULL;
    }

    e = network_open_connection(arglist, sl);
    free_var(arglist);
    if (e == E_NONE) {
	/* The connection was successfully opened, implying that
	 * server_new_connection was called, implying and a new negative
	 * player number was allocated for the connection.  Thus, the old
	 * value of next_unconnected_player is the number of our connection.
	 */
	r.type = TYPE_OBJ;
	r.v.obj = next_unconnected_player + 1;
    } else {
	r.type = TYPE_ERR;
	r.v.err = e;
    }
    if (r.type == TYPE_ERR)
	return make_error_pack(r.v.err);
    else
	return make_var_pack(r);

#else				/* !OUTBOUND_NETWORK */

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
    for (h = all_shandles; h; h = h->next)
	if ((show_all || h->connection_time != 0) && !h->disconnect_me)
	    count++;

    result = new_list(count);
    count = 0;

    for (h = all_shandles; h; h = h->next) {
	if ((show_all || h->connection_time != 0) && !h->disconnect_me) {
	    count++;
	    result.v.list[count].type = TYPE_OBJ;
	    result.v.list[count].v.obj = h->player;
	}
    }

    return make_var_pack(result);
}

static package
bf_connected_seconds(Var arglist, Byte next, void *vdata, Objid progr)
{				/* (player) */
    Var r;
    shandle *h = find_shandle(arglist.v.list[1].v.obj);

    r.type = TYPE_INT;
    if (h && h->connection_time != 0 && !h->disconnect_me)
	r.v.num = time(0) - h->connection_time;
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
{				/* (player) */
    Var r;
    shandle *h = find_shandle(arglist.v.list[1].v.obj);

    r.type = TYPE_INT;
    if (h && !h->disconnect_me)
	r.v.num = time(0) - h->last_activity_time;
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
{				/* (player) */
    Objid who = arglist.v.list[1].v.obj;
    shandle *h = find_shandle(who);
    const char *conn_name;
    Var r;

    if (h && !h->disconnect_me)
	conn_name = network_connection_name(h->nhandle);
    else
	conn_name = 0;

    free_var(arglist);
    if (!is_wizard(progr) && progr != who)
	return make_error_pack(E_PERM);
    else if (!conn_name)
	return make_error_pack(E_INVARG);
    else {
	r.type = TYPE_STR;
	r.v.str = str_dup(conn_name);
	return make_var_pack(r);
    }
}

static package
bf_notify(Var arglist, Byte next, void *vdata, Objid progr)
{				/* (player, string [, no_flush]) */
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
    if (h && !h->disconnect_me) {
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
{				/* (object) */
    Objid oid = arglist.v.list[1].v.obj;

    free_var(arglist);

    if (oid != progr && !is_wizard(progr))
	return make_error_pack(E_PERM);

    boot_player(oid);
    return no_var_pack();
}

static package
bf_set_connection_option(Var arglist, Byte next, void *vdata, Objid progr)
{				/* (conn, option, value) */
    Objid oid = arglist.v.list[1].v.obj;
    const char *option = arglist.v.list[2].v.str;
    Var value = arglist.v.list[3];
    shandle *h = find_shandle(oid);
    enum error e = E_NONE;

    if (oid != progr && !is_wizard(progr))
	e = E_PERM;
    else if (!h || h->disconnect_me
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
{				/* (conn [, opt-name]) */
    Objid oid = arglist.v.list[1].v.obj;
    int nargs = arglist.v.list[0].v.num;
    const char *oname = (nargs >= 2 ? arglist.v.list[2].v.str : 0);
    shandle *h = find_shandle(oid);
    Var ans;

    if (!h || h->disconnect_me) {
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

static slistener *
find_slistener(Var desc)
{
    slistener *l;

    for (l = all_slisteners; l; l = l->next)
	if (equality(desc, l->desc, 0))
	    return l;

    return 0;
}

static package
bf_listen(Var arglist, Byte next, void *vdata, Objid progr)
{				/* (oid, desc) */
    Objid oid = arglist.v.list[1].v.obj;
    Var desc = arglist.v.list[2];
    int nargs = arglist.v.list[0].v.num;
    int print_messages = nargs >= 3 && is_true(arglist.v.list[3]);
    enum error e;
    slistener *l = 0;

    if (!is_wizard(progr))
	e = E_PERM;
    else if (!valid(oid) || find_slistener(desc))
	e = E_INVARG;
    else if (!(l = new_slistener(oid, desc, print_messages, &e)));	/* Do nothing; e is already set */
    else if (!start_listener(l))
	e = E_QUOTA;

    free_var(arglist);
    if (e == E_NONE)
	return make_var_pack(var_ref(l->desc));
    else
	return make_error_pack(e);
}

static package
bf_unlisten(Var arglist, Byte next, void *vdata, Objid progr)
{				/* (desc) */
    Var desc = arglist.v.list[1];
    enum error e = E_NONE;
    slistener *l = 0;

    if (!is_wizard(progr))
	e = E_PERM;
    else if (!(l = find_slistener(desc)))
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
{				/* () */
    int i, count = 0;
    Var list, entry;
    slistener *l;

    free_var(arglist);
    for (l = all_slisteners; l; l = l->next)
	count++;
    list = new_list(count);
    for (i = 1, l = all_slisteners; l; i++, l = l->next) {
	list.v.list[i] = entry = new_list(3);
	entry.v.list[1].type = TYPE_OBJ;
	entry.v.list[1].v.obj = l->oid;
	entry.v.list[2] = var_ref(l->desc);
	entry.v.list[3].type = TYPE_INT;
	entry.v.list[3].v.num = l->print_messages;
    }

    return make_var_pack(list);
}

static package
bf_buffered_output_length(Var arglist, Byte next, void *vdata, Objid progr)
{				/* ([connection]) */
    int nargs = arglist.v.list[0].v.num;
    Objid conn = nargs >= 1 ? arglist.v.list[1].v.obj : 0;
    Var r;

    free_var(arglist);
    r.type = TYPE_INT;
    if (nargs == 0)
	r.v.num = MAX_QUEUED_OUTPUT;
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

static package
bf_process_id(Var arglist, Byte next, void *vdata, Objid progr)
{
    free_var(arglist);

    Var taskid;
    taskid.type = TYPE_INT;
    taskid.v.num = getpid();

    return make_var_pack(taskid);
}

void
register_server(void)
{
    register_function("server_version", 0, 1, bf_server_version, TYPE_ANY);
    register_function("renumber", 1, 1, bf_renumber, TYPE_OBJ);
    register_function("reset_max_object", 0, 0, bf_reset_max_object);
    register_function("process_id", 0, 0, bf_process_id);
    register_function("shutdown", 0, 1, bf_shutdown, TYPE_STR);
    register_function("dump_database", 0, 0, bf_dump_database);
    register_function("db_disk_size", 0, 0, bf_db_disk_size);
    register_function("open_network_connection", 0, -1,
		      bf_open_network_connection);
    register_function("connected_players", 0, 1, bf_connected_players,
		      TYPE_ANY);
    register_function("connected_seconds", 1, 1, bf_connected_seconds,
		      TYPE_OBJ);
    register_function("idle_seconds", 1, 1, bf_idle_seconds, TYPE_OBJ);
    register_function("connection_name", 1, 1, bf_connection_name, TYPE_OBJ);
    register_function("notify", 2, 4, bf_notify, TYPE_OBJ, TYPE_STR, TYPE_ANY, TYPE_ANY);
    register_function("boot_player", 1, 1, bf_boot_player, TYPE_OBJ);
    register_function("set_connection_option", 3, 3, bf_set_connection_option,
		      TYPE_OBJ, TYPE_STR, TYPE_ANY);
    register_function("connection_option", 2, 2, bf_connection_options,
		      TYPE_OBJ, TYPE_STR);
    register_function("connection_options", 1, 1, bf_connection_options,
		      TYPE_OBJ);
    register_function("listen", 2, 3, bf_listen, TYPE_OBJ, TYPE_ANY, TYPE_ANY);
    register_function("unlisten", 1, 1, bf_unlisten, TYPE_ANY);
    register_function("listeners", 0, 0, bf_listeners);
    register_function("buffered_output_length", 0, 1,
		      bf_buffered_output_length, TYPE_OBJ);
#ifdef USE_ANCESTOR_CACHE
    register_function("clear_ancestor_cache", 0, 0, bf_clear_ancestor_cache);
#endif
}
