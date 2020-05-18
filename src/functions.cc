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

#include <stdarg.h>
#include <vector>
#include <functional>

#include "bf_register.h"
#include "config.h"
#include "db_io.h"
#include "functions.h"
#include "list.h"
#include "log.h"
#include "map.h"
#include "server.h"
#include "storage.h"
#include "streams.h"
#include "structures.h"
#include "unparse.h"
#include "utils.h"

typedef std::function<void()> registry;

void
register_bi_functions()
{
/*****************************************************************************
 * This is the table of procedures that register MOO built-in functions.  To
 * add new built-in functions to the server, add to the list below the name of
 * a C function that will register your new MOO built-ins; your C function will
 * be called exactly once, during server initialization.  Also add a
 * declaration of that C function to `bf_register.h' and add the necessary .c
 * files to the `CSRCS' line in the Makefile.
 ****************************************************************************/
const std::vector<registry> registry_callbacks =
{
#ifdef ENABLE_GC
    register_gc,
#endif
    register_collection,
    register_disassemble,
    register_extensions,
    register_execute,
    register_functions,
    register_list,
    register_log,
    register_map,
    register_numbers,
    register_objects,
    register_property,
    register_server,
    register_tasks,
    register_verbs,
    register_yajl,
    register_base64,
    register_fileio,
    register_system,
    register_exec,
    register_crypto,
    register_sqlite,
    register_pcre,
    register_background,
    register_waif,
    register_simplexnoise,
    register_argon2,
    register_spellcheck,
    register_curl
};
	for (const auto& callback: registry_callbacks)
{
	callback();
}
    }

/*** register ***/

struct bft_entry {
    const char *name;
    const char *protect_str;
    const char *verb_str;
    int minargs;
    int maxargs;
    var_type *prototype;
    bf_type func;
    bf_read_type read;
    bf_write_type write;
    int _protected;
};

static std::vector<bft_entry> bf_table;

static void
register_common(const char *name, int minargs, int maxargs, bf_type func,
		bf_read_type read, bf_write_type write, va_list args)
{
    int va_index;
    int num_arg_types = maxargs == -1 ? minargs : maxargs;
    static Stream *s = nullptr;

    if (!s)
	s = new_stream(30);

	bft_entry entry;
    entry.name = str_dup(name);
    stream_printf(s, "protect_%s", name);
    entry.protect_str = str_dup(reset_stream(s));
    stream_printf(s, "bf_%s", name);
    entry.verb_str = str_dup(reset_stream(s));
    entry.minargs = minargs;
    entry.maxargs = maxargs;
    entry.func = func;
    entry.read = read;
    entry.write = write;
    entry._protected = 0;

    if (num_arg_types > 0)
	entry.prototype =
	    (var_type *)mymalloc(num_arg_types * sizeof(var_type), M_PROTOTYPE);
    else
	entry.prototype = nullptr;
    for (va_index = 0; va_index < num_arg_types; va_index++)
	entry.prototype[va_index] = (var_type)va_arg(args, int);
bf_table.push_back(entry);
    }
	
void 
register_function(const char *name, int minargs, int maxargs,
		  bf_type func,...)
{
    va_list args;
    
    va_start(args, func);
    register_common(name, minargs, maxargs, func, nullptr, nullptr, args);
    va_end(args);
}

void
register_function_with_read_write(const char *name, int minargs, int maxargs,
				  bf_type func, bf_read_type read,
				  bf_write_type write,...)
{
    va_list args;


    va_start(args, write);
    register_common(name, minargs, maxargs, func, read, write, args);
    va_end(args);
    }

/*** looking up functions -- by name or num ***/

static const char *func_not_found_msg = "no such function";
const char *
name_func_by_num(unsigned n)
{				/* used by unparse only */
    if (n >= bf_table.size())
	return func_not_found_msg;
    else
	return bf_table[n].name;
}

unsigned
number_func_by_name(const char *name)
{				/* used by parser only */
    
	const auto functionCount = bf_table.size();
    for (size_t i = 0; i < functionCount; ++i)
	if (!strcasecmp(name, bf_table[i].name))
	    return i;

    return FUNC_NOT_FOUND;
}

/*** calling built-in functions ***/

package
call_bi_func(unsigned n, Var arglist, Byte func_pc,
	     Objid progr, void *vdata)
     /* requires arglist.type == TYPE_LIST
        call_bi_func will free arglist */
{
	const auto functionCount = bf_table.size();
    
    if (n >= functionCount) {
	errlog("CALL_BI_FUNC: Unknown function number: %d\n", n);
	free_var(arglist);
	return no_var_pack();
    }
    const auto f = bf_table[n];

    if (func_pc == 1) {		/* check arg types and count *ONLY* for first entry */
	int k, max;
	Var *args = arglist.v.list;

	/*
	 * Check permissions, if protected
	 */
	if ((!caller().is_obj() || caller().v.obj != SYSTEM_OBJECT) && f._protected) {
	    /* Try calling #0:bf_FUNCNAME(@ARGS) instead */
	    enum error e = call_verb2(SYSTEM_OBJECT, f.verb_str, Var::new_obj(SYSTEM_OBJECT), arglist, 0, get_thread_mode());

	    if (e == E_NONE)
		return tail_call_pack();

	    if (e == E_MAXREC || !is_wizard(progr)) {
		free_var(arglist);
		return make_error_pack(e == E_MAXREC ? e : E_PERM);
	    }
	}
	/*
	 * Check argument count
	 * (Can't always check in the compiler, because of @)
	 */
	if (args[0].v.num < f.minargs
	    || (f.maxargs != -1 && args[0].v.num > f.maxargs)) {
	    free_var(arglist);
	    return make_error_pack(E_ARGS);
	}
	/*
	 * Check argument types
	 */
	max = (f.maxargs == -1) ? f.minargs : args[0].v.num;

	for (k = 0; k < max; k++) {
	    var_type proto = f.prototype[k];
	    var_type arg = args[k + 1].type;

	    if (!(proto == TYPE_ANY
		  || (proto == TYPE_NUMERIC && (arg == TYPE_INT
						|| arg == TYPE_FLOAT))
		  || proto == arg)) {
		free_var(arglist);
		return make_error_pack(E_TYPE);
	    }
	}
    } else if (func_pc == 2 && vdata == &call_bi_func) {
	/* This is a return from calling #0:bf_FUNCNAME(@ARGS); return what
	 * it returned.  If it errored, what we do will be ignored.
	 */
	return make_var_pack(arglist);
    }
    /*
     * do the function
     */
    return (*(f.func)) (arglist, func_pc, vdata, progr);
    /* f->func is responsible for freeing/using up arglist. */
}

void
write_bi_func_data(void *vdata, Byte f_id)
{
	const auto functionCount = bf_table.size();
    if (f_id >= functionCount)
	errlog("WRITE_BI_FUNC_DATA: Unknown function number: %d\n", f_id);
    else if (bf_table[f_id].write)
	(*(bf_table[f_id].write)) (vdata);
}

static Byte *pc_for_bi_func_data_being_read;

Byte *
pc_for_bi_func_data(void)
{
    return pc_for_bi_func_data_being_read;
}

int
read_bi_func_data(Byte f_id, void **bi_func_state, Byte * bi_func_pc)
{
    pc_for_bi_func_data_being_read = bi_func_pc;
const auto functionCount = bf_table.size();
    if (f_id >= functionCount) {
	errlog("READ_BI_FUNC_DATA: Unknown function number: %d\n", f_id);
	*bi_func_state = nullptr;
	return 0;
    } else if (bf_table[f_id].read) {
	*bi_func_state = (*(bf_table[f_id].read)) ();
	if (*bi_func_state == nullptr) {
	    errlog("READ_BI_FUNC_DATA: Can't read data for %s()\n",
		   bf_table[f_id].name);
	    return 0;
	}
    } else {
	*bi_func_state = nullptr;
	/* The following code checks for the easily-detectable case of the
	 * bug described in the Version 1.8.0p4 entry in ChangeLog.txt.
	 */
	if (*bi_func_pc == 2 && dbio_input_version == DBV_Float
	    && strcmp(bf_table[f_id].name, "eval") != 0) {
	    oklog("LOADING: Warning: patching bogus return to `%s()'\n",
		  bf_table[f_id].name);
	    oklog("         (See 1.8.0p4 ChangeLog.txt entry for details.)\n");
	    *bi_func_pc = 0;
	}
    }
    return 1;
}


package
make_abort_pack(enum abort_reason reason)
{
    package p;

    p.kind = package::BI_KILL;
    p.u.ret.type = TYPE_INT;
    p.u.ret.v.num = reason;
    return p;
}

package
make_error_pack(enum error err)
{
    return make_raise_pack(err, unparse_error(err), zero);
}

package
make_raise_pack(enum error err, const char *msg, Var value)
{
    package p;

    p.kind = package::BI_RAISE;
    p.u.raise.code.type = TYPE_ERR;
    p.u.raise.code.v.err = err;
    p.u.raise.msg = str_dup(msg);
    p.u.raise.value = value;

    return p;
}

package
make_raise_x_not_found_pack(enum error err, const char *msg)
{
	Var missing;
	missing.type = TYPE_STR;
	missing.v.str = str_dup(msg);
	char *error_msg = nullptr;
	asprintf(&error_msg, "%s: %s", unparse_error(err), msg);

    return make_raise_pack(err, error_msg, missing);
}

package
make_var_pack(Var v)
{
    package p;

    p.kind = package::BI_RETURN;
    p.u.ret = v;

    return p;
}

package
no_var_pack(void)
{
    return make_var_pack(zero);
}

package
make_call_pack(Byte pc, void *data)
{
    package p;

    p.kind = package::BI_CALL;
    p.u.call.pc = pc;
    p.u.call.data = data;

    return p;
}

package
tail_call_pack(void)
{
    return make_call_pack(0, nullptr);
}

package
make_suspend_pack(enum error(*proc) (vm, void *), void *data)
{
    package p;

    p.kind = package::BI_SUSPEND;
    p.u.susp.proc = proc;
    p.u.susp.data = data;

    return p;
}

package
make_int_pack(Num v)
{
    package p;

    p.kind = package::BI_RETURN;
    p.u.ret.type = TYPE_INT;
    p.u.ret.v.num = v;

    return p;
}

package
make_float_pack(double v)
{
    package p;

    p.kind = package::BI_RETURN;
    p.u.ret.type = TYPE_FLOAT;
    p.u.ret.v.fnum = v;

    return p;
}


static Var
function_description(int i)
{
    struct bft_entry entry;
    Var v, vv;
    int j, nargs;

    entry = bf_table[i];
    v = new_list(4);
    v.v.list[1].type = TYPE_STR;
    v.v.list[1].v.str = str_ref(entry.name);
    v.v.list[2].type = TYPE_INT;
    v.v.list[2].v.num = entry.minargs;
    v.v.list[3].type = TYPE_INT;
    v.v.list[3].v.num = entry.maxargs;
    nargs = entry.maxargs == -1 ? entry.minargs : entry.maxargs;
    vv = v.v.list[4] = new_list(nargs);
    for (j = 0; j < nargs; j++) {
	int proto = entry.prototype[j];
	vv.v.list[j + 1].type = TYPE_INT;
	vv.v.list[j + 1].v.num = proto < 0 ? proto : (proto & TYPE_DB_MASK);
    }

    return v;
}

static package
bf_function_info(Var arglist, Byte next, void *vdata, Objid progr)
{
    Var r;

    if (arglist.v.list[0].v.num == 1) {
	const auto i = number_func_by_name(arglist.v.list[1].v.str);
	if (i == FUNC_NOT_FOUND) {
	    free_var(arglist);
	    return make_error_pack(E_INVARG);
	}
	r = function_description(i);
    } else {
    const auto functionCount = bf_table.size();
	r = new_list(functionCount);
	for (size_t i = 0; i < functionCount; i++)
	    r.v.list[i + 1] = function_description(i);
    }

    free_var(arglist);
    return make_var_pack(r);
}

static void
load_server_protect_function_flags(void)
{
    const auto functionCount = bf_table.size();
    for (size_t i = 0; i < functionCount; i++) {
	bf_table[i]._protected
	    = server_flag_option(bf_table[i].protect_str, 0);
    }
    oklog("Loaded protect cache for %d builtin functions\n", functionCount);
}

Num _server_int_option_cache[SVO__CACHE_SIZE];

void
load_server_options(void)
{
    int value;

    load_server_protect_function_flags();

# define _BP_DO(PROPERTY, property)				\
      _server_int_option_cache[SVO_PROTECT_##PROPERTY]		\
	  = server_flag_option("protect_" #property, 0);	\

    BUILTIN_PROPERTIES(_BP_DO);

# undef _BP_DO

# define _SVO_DO(SVO_MISC_OPTION, misc_option,			\
		 kind, DEFAULT, CANONICALIZE)			\
      value = server_##kind##_option(#misc_option, DEFAULT);	\
      CANONICALIZE;						\
      _server_int_option_cache[SVO_MISC_OPTION] = value;	\

    SERVER_OPTIONS_CACHED_MISC(_SVO_DO, value);

# undef _SVO_DO
}

static package
bf_load_server_options(Var arglist, Byte next, void *vdata, Objid progr)
{
    free_var(arglist);

    if (!is_wizard(progr)) {
	return make_error_pack(E_PERM);
    }
    load_server_options();

    return no_var_pack();
}

void
register_functions(void)
{
    register_function("function_info", 0, 1, bf_function_info, TYPE_STR);
    register_function("load_server_options", 0, 0, bf_load_server_options);
}
