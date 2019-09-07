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

#ifndef Execute_h
#define Execute_h 1

#include "config.h"
#include "db.h"
#include "opcode.h"
#include "parse_cmd.h"
#include "program.h"
#include "structures.h"

typedef struct {
    Program *prog;
    Var *rt_env;		/* same length as prog.var_names */
    Var *base_rt_stack;
    Var *top_rt_stack;		/* the stack has a fixed size equal to
				   vector.max_stack.  top_rt_stack
				   always points to next empty slot;
				   there is no need to check bounds! */
    int rt_stack_size;		/* size of stack allocated */
    unsigned pc;
    unsigned error_pc;
    Byte bi_func_pc;		/* next == 0 means a normal activation, which just
				   returns to the previous activation (caller verb).
				   next == 1, 2, 3, ... means the returned value should be
				   fed to the bi_func (as specified in bi_func_id) 
				   together with the next code. */
    Byte bi_func_id;
    void *bi_func_data;
    Var temp;			/* VM's temp register */

    /* `this' is the value on which the verb was invoked and 'vloc' is
     * the object on which the verb is actually defined.  `recv' is
     * the object number of the receiver of the verb invocation -- the
     * object number of the handler in the case of primitive values.
     */
    Var _this;
    Objid player;
    Objid progr;
    Objid recv;
    Var vloc;
    const char *verb;
    const char *verbname;
    int debug;
    bool threaded;
} activation;

extern void free_activation(activation *, char data_too);

typedef struct {
    int task_id;
    Var local;
    activation *activ_stack;
    unsigned max_stack_size;
    unsigned top_activ_stack;
    int root_activ_vector;
    /* root_activ_vector == MAIN_VECTOR
       means root activation is main_vector */
    unsigned func_id;
} vmstruct;

typedef vmstruct *vm;

#define alloc_data(size)   mymalloc(size, M_BI_FUNC_DATA)
#define free_data(ptr)     myfree((void *) ptr, M_BI_FUNC_DATA)

/* call_verb will only return E_MAXREC, E_INVIND, E_VERBNF,
   or E_NONE.  the vm will only be changed if E_NONE is returned */
extern enum error call_verb(Objid obj, const char *vname,
			    Var _this, Var args, int do_pass);
/* if your vname is already a moo str (via str_dup) then you can
   use this interface instead */
extern enum error call_verb2(Objid obj, const char *vname,
			     Var _this, Var args, int do_pass, bool should_thread);

extern int setup_activ_for_eval(Program * prog);

enum outcome {
    OUTCOME_DONE,		/* Task ran successfully to completion */
    OUTCOME_ABORTED,		/* Task aborted, either by kill_task() or
				 * by an uncaught error. */
    OUTCOME_BLOCKED		/* Task called a blocking built-in function. */
};

extern enum outcome do_forked_task(Program * prog, Var * rt_env,
				   activation a, int f_id);
extern enum outcome do_input_task(Objid user, Parsed_Command * pc,
				  Objid recv, db_verb_handle vh);
extern enum outcome do_server_verb_task(Var _this, const char *verb,
					Var args, db_verb_handle h,
					Objid player, const char *argstr,
					Var * result, int do_db_tracebacks);
extern enum outcome do_server_program_task(Var _this, const char *verb,
					   Var args, Var vloc,
					   const char *verbname,
					   Program * program, Objid progr,
					   int debug, Objid player,
					   const char *argstr,
					   Var * result,
					   int do_db_tracebacks);
extern enum outcome resume_from_previous_vm(vm the_vm, Var value);

extern int task_timed_out;
extern void abort_running_task(void);
extern void print_error_backtrace(const char *, void (*)(const char *));
extern Var caller(void);

extern void write_activ_as_pi(activation);
extern int read_activ_as_pi(activation *);
void write_rt_env(const char **var_names, Var * rt_env,
		  unsigned size);
int read_rt_env(const char ***old_names, Var ** rt_env,
		int *old_size);
Var *reorder_rt_env(Var * old_rt_env, const char **old_names,
		    int old_size, Program * prog);
extern void free_reordered_rt_env_values(void);
extern void write_activ(activation a);
extern int read_activ(activation * a, int which_vector);

extern bool get_thread_mode();
extern void set_thread_mode(bool mode);

#endif
