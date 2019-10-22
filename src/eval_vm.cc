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

#include "config.h"
#include "db_io.h"
#include "decompile.h"
#include "eval_vm.h"
#include "execute.h"
#include "log.h"
#include "map.h"
#include "options.h"
#include "storage.h"
#include "structures.h"
#include "tasks.h"
#include "utils.h"

/**** external functions ****/

vm
new_vm(int task_id, Var local, int stack_size)
{
    vm the_vm = (vm)mymalloc(sizeof(vmstruct), M_VM);

    the_vm->task_id = task_id;
    the_vm->local = local;
    the_vm->activ_stack = (activation *)mymalloc(sizeof(activation) * stack_size, M_VM);

    return the_vm;
}

void
free_vm(vm the_vm, int stack_too)
{
    int i;

    free_var(the_vm->local);

    if (stack_too)
	for (i = the_vm->top_activ_stack; i >= 0; i--)
	    free_activation(&the_vm->activ_stack[i], 1);
    myfree(the_vm->activ_stack, M_VM);
    myfree(the_vm, M_VM);
}

activation
top_activ(vm the_vm)
{
    return the_vm->activ_stack[the_vm->top_activ_stack];
}

Objid
progr_of_cur_verb(vm the_vm)
{
    return top_activ(the_vm).progr;
}

unsigned
suspended_lineno_of_vm(vm the_vm)
{
    activation top;

    top = top_activ(the_vm);
    return find_line_number(top.prog, (the_vm->top_activ_stack == 0
				       ? the_vm->root_activ_vector
				       : MAIN_VECTOR),
			    top.error_pc);
}

/**** read/write data base ****/

void
write_vm(vm the_vm)
{
    unsigned i;

    dbio_write_var(the_vm->local);

    dbio_printf("%u %d %u %u\n",
		the_vm->top_activ_stack, the_vm->root_activ_vector,
		the_vm->func_id, the_vm->max_stack_size);

    for (i = 0; i <= the_vm->top_activ_stack; i++)
	write_activ(the_vm->activ_stack[i]);
}

vm
read_vm(int task_id)
{
    unsigned i, top, func_id, max;
    int vector;
    char c;
    vm the_vm;

    Var local;
    if (dbio_input_version >= DBV_TaskLocal)
	local = dbio_read_var();
    else
	local = new_map();

    if (dbio_scanf("%u %d %u%c", &top, &vector, &func_id, &c) != 4
	|| (c == ' '
	    ? dbio_scanf("%u%c", &max, &c) != 2 || c != '\n'
	    : (max = DEFAULT_MAX_STACK_DEPTH, c != '\n'))) {
	free_var(local);
	errlog("READ_VM: Bad vm header\n");
	return 0;
    }
    the_vm = new_vm(task_id, local, top + 1);
    the_vm->max_stack_size = max;
    the_vm->top_activ_stack = top;
    the_vm->root_activ_vector = vector;
    the_vm->func_id = func_id;

    for (i = 0; i <= top; i++)
	if (!read_activ(&the_vm->activ_stack[i],
			i == 0 ? vector : MAIN_VECTOR)) {
	    errlog("READ_VM: Bad activ number %d\n", i);
	    return 0;
	}
    return the_vm;
}
