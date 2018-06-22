#include <math.h>           // sqrt, atan, round, etc
#include "functions.h"      // register builtins
#include "log.h"            // oklog()
#include "utils.h"          // streams
#include "numbers.h"        // new_float()
#include "list.h"           // listappend and friends
#include "my-stdlib.h"      // rand()
#include "random.h"         // random() (nowai)
#include "server.h"         // panic()
#include <sys/time.h>       // getrusage
#include <sys/resource.h>   // getrusage
#include <sys/sysinfo.h>    // CPU usage
#include "extension-background.h"   // Threads
#ifdef __MACH__
#include <mach/clock.h>     // Millisecond time for OS X
#endif

/* Returns a float of the time (including milliseconds)
   Optional arguments specify monotonic time; 1: Monotonic. 2. Monotonic raw.
   (seconds since an arbitrary period of time. More useful for timing
   since its not affected by NTP or other time changes.) */
    static package
bf_ftime(Var arglist, Byte next, void *vdata, Objid progr)
{
#ifdef __MACH__
    // OS X only provides SYSTEM_CLOCK for monotonic time, so our arguments don't matter.
    clock_id_t clock_type = (arglist.v.list[0].v.num == 0 ? CALENDAR_CLOCK : SYSTEM_CLOCK);
#else
    // Other OSes provide MONOTONIC_RAW and MONOTONIC, so we'll check args for 2(raw) or 1.
    clockid_t clock_type = 0;
    if (arglist.v.list[0].v.num == 0)
        clock_type = CLOCK_REALTIME;
    else
        clock_type = arglist.v.list[1].v.num == 2 ? CLOCK_MONOTONIC_RAW : CLOCK_MONOTONIC;
#endif

    struct timespec ts;

#ifdef __MACH__
    // OS X lacks clock_gettime, use clock_get_time instead
    clock_serv_t cclock;
    mach_timespec_t mts;
    host_get_clock_service(mach_host_self(), clock_type, &cclock);
    clock_get_time(cclock, &mts);
    mach_port_deallocate(mach_task_self(), cclock);
    ts.tv_sec = mts.tv_sec;
    ts.tv_nsec = mts.tv_nsec;
#else
    clock_gettime(clock_type, &ts);
#endif

    Var r = new_float((double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0);

    free_var(arglist);
    return make_var_pack(r);
}

    static package
bf_locate_by_name(Var arglist, Byte next, void *vdata, Objid progr)
{

    if(!is_wizard(progr))
    {
        free_var(arglist);
        return make_error_pack(E_PERM);
    }

    int x = 1, case_matters = 0;
    Var ret = new_list(0), tmp, name;

    tmp.type = TYPE_OBJ;

    if (arglist.v.list[0].v.num == 2)
        case_matters = is_true(arglist.v.list[2]);


    for (x = 1; x < db_last_used_objid(); x++)
    {
        if (valid(x))
        {
            db_find_property(Var::new_obj(x), "name", &name);
            if (strindex(name.v.str, memo_strlen(name.v.str), arglist.v.list[1].v.str, memo_strlen(arglist.v.list[1].v.str), case_matters))
            {
                tmp.v.obj = x;
                ret = listappend(ret, tmp);
            }
        }
    }

    free_var(tmp);
    free_var(arglist);

    return make_var_pack(ret);
}

/* Calculates the distance between two n-dimensional sets of coordinates. */
    static package
bf_distance(Var arglist, Byte next, void *vdata, Objid progr)
{
    double ret = 0.0, tmp = 0.0;
    int count;

    for (count = 1; count <= arglist.v.list[1].v.list[0].v.num; count++)
    {
        if ((arglist.v.list[1].v.list[count].type != TYPE_INT && arglist.v.list[1].v.list[count].type != TYPE_FLOAT) || (arglist.v.list[2].v.list[count].type != TYPE_INT && arglist.v.list[2].v.list[count].type != TYPE_FLOAT))
        {
            free_var(arglist);
            return make_error_pack(E_TYPE);
        }
        else
        {
            tmp = (arglist.v.list[2].v.list[count].type == TYPE_INT ? (double)arglist.v.list[2].v.list[count].v.num : *arglist.v.list[2].v.list[count].v.fnum) - (arglist.v.list[1].v.list[count].type == TYPE_INT ? (double)arglist.v.list[1].v.list[count].v.num : *arglist.v.list[1].v.list[count].v.fnum);
            ret = ret + (tmp * tmp);
        }
    }

    free_var(arglist);

    Var s;
    s = new_float(sqrt(ret));

    return make_var_pack(s);
}

/* Calculates the bearing between two sets of three dimensional floating point coordinates. */
    static package
bf_relative_heading(Var arglist, Byte next, void *vdata, Objid progr)
{
    if (arglist.v.list[1].v.list[1].type != TYPE_FLOAT || arglist.v.list[1].v.list[2].type != TYPE_FLOAT || arglist.v.list[1].v.list[3].type != TYPE_FLOAT || arglist.v.list[2].v.list[1].type != TYPE_FLOAT || arglist.v.list[2].v.list[2].type != TYPE_FLOAT || arglist.v.list[2].v.list[3].type != TYPE_FLOAT) {
        free_var(arglist);
        return make_error_pack(E_TYPE);
    }

    double dx = *arglist.v.list[2].v.list[1].v.fnum - *arglist.v.list[1].v.list[1].v.fnum;
    double dy = *arglist.v.list[2].v.list[2].v.fnum - *arglist.v.list[1].v.list[2].v.fnum;
    double dz = *arglist.v.list[2].v.list[3].v.fnum - *arglist.v.list[1].v.list[3].v.fnum;

    double xy = 0.0;
    double z = 0.0;

    xy = atan2(dy, dx) * 57.2957795130823;

    if (xy < 0.0)
        xy = xy + 360.0;

    z = atan2(dz, sqrt((dx * dx) + (dy * dy))) * 57.2957795130823;

    Var s = new_list(2);
    s.v.list[1].type = TYPE_INT;
    s.v.list[1].v.num = (int)xy;
    s.v.list[2].type = TYPE_INT;
    s.v.list[2].v.num = (int)z;

    free_var(arglist);

    return make_var_pack(s);
}

/* Returns total memory usage, resident set size, shared pages, text/code, and data + stack. */
    static package
bf_memory_usage(Var arglist, Byte next, void *vdata, Objid progr)
{
    // Values are returned in pages. To get KB, multiply by 4.
    free_var(arglist);

    long double size, resident, share, text, lib, data, dt;

    FILE *f = fopen("/proc/self/statm", "r");

    if (!f)
        return make_error_pack(E_NACC);

    if (fscanf(f, "%Lf %Lf %Lf %Lf %Lf %Lf %Lf",
                &size, &resident, &share, &text, &lib, &data, &dt) != 7)
    {
        fclose(f);
        return make_error_pack(E_NACC);
    }

    fclose(f);

    Var s = new_list(5);
    s.v.list[1] = new_float(size);           // Total program size
    s.v.list[2] = new_float(resident);       // Resident set size
    s.v.list[3] = new_float(share);          // Shared pages from shared mappings
    s.v.list[4] = new_float(text);           // Text (code)
    s.v.list[5] = new_float(data);           // Data + stack

    return make_var_pack(s);
}

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
        cpu.v.list[x].type = TYPE_INT;

    /*** Begin CPU load averages ***/
    struct sysinfo sys_info;
    int info_ret = sysinfo(&sys_info);

    for (x = 0; x < 3; x++)
        cpu.v.list[x+1].v.num = (info_ret != 0 ? 0 : sys_info.loads[x]);

    /*** Now rusage ***/
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);

    r.v.list[1] = new_float((double)usage.ru_utime.tv_sec + (double)usage.ru_utime.tv_usec / CLOCKS_PER_SEC);
    r.v.list[2] = new_float((double)usage.ru_stime.tv_sec + (double)usage.ru_stime.tv_usec / CLOCKS_PER_SEC);
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

    if(!is_wizard(progr)) {
        free_var(arglist);
        return make_error_pack(E_PERM);
    }

    if(arglist.v.list[0].v.num) {
        msg=str_dup(arglist.v.list[1].v.str);
    } else {
        msg="";
    }

    free_var(arglist);
    panic(msg);

    return make_error_pack(E_NONE);
}

/* Return a random floating point value between 0.0..args[1] or args[1]..args[2] */
    static package
bf_frandom(Var arglist, Byte next, void *vdata, Objid progr)
{
    double fmin = (arglist.v.list[0].v.num > 1 ? *arglist.v.list[1].v.fnum : 0.0);
    double fmax = (arglist.v.list[0].v.num > 1 ? *arglist.v.list[2].v.fnum : *arglist.v.list[1].v.fnum);

    free_var(arglist);

    double f = (double)rand() / RAND_MAX;
    f = fmin + f * (fmax - fmin);

    return make_var_pack(new_float(f));

}

/* Round numbers to the nearest integer value to args[1] */
    static package
bf_round(Var arglist, Byte next, void *vdata, Objid progr)
{
    double r = round((double)*arglist.v.list[1].v.fnum);

    free_var(arglist);

    return make_var_pack(new_float(r));
}

/* Return a list of substrings of an argument separated by a break. */
    static package
bf_explode(Var arglist, Byte next, void *vdata, Objid progr)
{
    int nargs = arglist.v.list[0].v.num;
    Stream *brk = new_stream(2);
    stream_add_string(brk, (nargs > 1) ? arglist.v.list[2].v.str : " ");
    Var r;

    if (strcmp(stream_contents(brk), "") == 0) {
        // Do we want to break it into letters here?
        r.type = TYPE_ERR;
        r.v.err = E_INVARG;
    } else {
        r = new_list(0);
        int i, l = stream_length(brk);
        Stream *tmp = new_stream(memo_strlen(arglist.v.list[1].v.str)+1);
        stream_add_string(tmp, arglist.v.list[1].v.str);
        stream_add_string(tmp, stream_contents(brk));

        Var subject;
        subject.type = TYPE_STR;
        subject.v.str = str_dup(reset_stream(tmp));
        free_stream(tmp);

        while (memo_strlen(subject.v.str)) {
            if ((i = strindex(subject.v.str, memo_strlen(subject.v.str), stream_contents(brk), stream_length(brk), 0)) > 1) {
                r = listappend(r, substr(var_dup(subject), 1, i - 1));
            }
            subject = substr(subject, i + l, memo_strlen(subject.v.str));
        }
        free_var(subject);
    }
    free_var(arglist);
    free_stream(brk);
    return make_var_pack(r);
}

/* Return a list of objects of parent, optionally with a player flag set.
 * With only one argument, player flag is assumed to be the only condition.
 * With two arguments, parent is the only condition.
 * With three arguments, parent is checked first and then the player flag is checked.
 * get_players(LIST objects, OBJ parent, ?INT player flag set)
 */
static package
bf_occupants(Var arglist, Byte next, void *vdata, Objid progr)
{				/* (object) */
    Var ret = new_list(0);
    int nargs = arglist.v.list[0].v.num;
    Var contents = arglist.v.list[1];
    int content_length = contents.v.list[0].v.num;
    bool check_parent = nargs == 1 ? false : true;
    Var parent = check_parent ? arglist.v.list[2] : nothing;
    bool check_player_flag = (nargs == 1 || (nargs > 2 && is_true(arglist.v.list[3])));

    for (int x = 1; x <= content_length; x++) {
        Objid oid = contents.v.list[x].v.obj;
        if (valid(oid)
            && (!check_parent ? 1 : db_object_isa(Var::new_obj(oid), parent))
            && (!check_player_flag || (check_player_flag && is_user(oid))))
        {
            ret = setadd(ret, Var::new_obj(oid));
        }
    }

    free_var(arglist);
    return make_var_pack(ret);
}

int str_cut(char *str, int begin, int len)
{
    int l = strlen(str);

    if (len < 0) len = l - begin;
    if (begin + len > l) len = l - begin;
    memmove(str + begin, str + begin + len, l - len + 1);

    return len;
}

/*
static package
bf_from_list(Var arglist, Byte next, void *vdata, Objid progr)
{
    const char *str = arglist.v.list[1].v.str;
    int str_length = memo_strlen(str);
    free_var(arglist);

    if (str[0] != '{' || str[str_length-1] != '}')
        return make_error_pack(E_INVARG);

    str_length = str_cut(str, 1, str_length - 2);

    Var r = new_list(0):

    while (1) {
    int ind = strindex(str, str_length, ",", 1);
    if (ind == 0)
        break;

    const char *value =
    }

    return no_var_pack();
}
*/

/* Direct stack access extensions, v1.0
 * (c) 1996, Nick Ingolia
 * Distribute or modify freely, but please retain this and the above notice.
 * No warranty, express or implied, is made as to the performance or safety of this code.
 *
 * Nick Ingolia
 * ningolia@neon.ci.lexington.ma.us
 */

/* Access to the stack frames of MOO tasks is provided by the following extension functions.
 * A stack frame is the list of variables within a given function on the run-time stack.  For
 * example, each non-builtin element of the callers() list and the task_stack() list is a frame
 * on the run-time stack.  The size of the frame for a function is fixed at compile-time, so
 * all variables are in the stack frame, whether or not they are currently defined.  It is
 * possible to determine from the information returned whether or not they are currently defined.
 * I have not considered security issues raised by the ability to change or undefine a variable
 * from outside a suspending function, so these are currently wiz-only when run on any task but
 * the currently-running task.  Permission checks for task_frames() are the same as those on
 * task_stack().
 */

#include "my-string.h"

#include "bf_register.h"
#include "execute.h"
#include "eval_vm.h"
#include "eval_env.h"
#include "functions.h"
#include "list.h"
#include "storage.h"
#include "tasks.h"
#include "utils.h"

/* Part of the hack on execute.c */
/* Simply remove the 'static' from the declaration of these two variables in execute.c */
extern activation *activ_stack;
extern unsigned top_activ_stack;

/* Functions that perform the indicated action on an arbitrary array of activations */
static Var MakeFrameList(activation *, unsigned);
static enum error AlterFrame(activation *, unsigned, unsigned, Var);

/* The builtin-function interfaces provided for those functions */
static package bfTaskFrames(Var, Byte, void *, Objid);
static package bfAlterVariable(Var, Byte, void *, Objid);
static package bfUndefVariable(Var, Byte, void *, Objid);

/* This function creates a list  of stack frames.  Each frame is a list of three-element lists,
 * with the first element containing the name of the variable, the second element the type of the
 * variable, and the third element is the value of the variable, turned into TYPE_INT if it was
 * TYPE_NONE to prevent some unknown but doubtlessly hideous result
 */
static Var MakeFrameList(activation *theStack, unsigned theTop){
	Var				out;
	int				varCount, i, j, n, type;
	activation		a;

	out = new_list(theTop + 1);
	for(j=theTop, n = 1; j>=0; j--, n++){
		a = theStack[j];

		varCount = a.prog->num_var_names;

		out.v.list[n] = new_list(varCount);
		for(i = 0; i<varCount; i++){
			out.v.list[n].v.list[i+1] = new_list(3);

			out.v.list[n].v.list[i+1].v.list[1].type = TYPE_STR;
			out.v.list[n].v.list[i+1].v.list[1].v.str = str_dup(a.prog->var_names[i]);

			out.v.list[n].v.list[i+1].v.list[2].type = TYPE_INT;
			type = out.v.list[n].v.list[i+1].v.list[2].v.num = a.rt_env[i].type;

			out.v.list[n].v.list[i+1].v.list[3] = var_dup(a.rt_env[i]);
			if(type == TYPE_NONE)
				out.v.list[n].v.list[i+1].v.list[3].type = TYPE_INT;	/* Avoid raising E_VARNF */
		}
	}
	return out;
}

/* This function alters a variable in a frame of an array of activations, obtained either from the
 * globals in execute.c or from a vm.  You *MUST NOT* call this function without performing
 * range checking on the 'frame' passed to it.
 */
static enum error AlterFrame(activation *theActivStack, unsigned frame, unsigned address, Var value){
	enum error	out;
	activation	a;

	a = theActivStack[frame];	/* No range checking performed! */
	if(address < 0 || address >= a.prog->num_var_names)
		return E_VARNF;

	free_var(a.rt_env[address]);
	a.rt_env[address] = var_dup(value);
	return E_NONE;
}

/* Returns a list of stack frames in the MOO virtual machine, for the specified task.
 * The list is a list ordered as task_stack() or callers() is, with all non-builtin
 * stack frames corresponding to a list reported.  The first value in each element of
 * the stack frame list is a string, the name of the variable.  The second is the actual type of the
 * variable in-server, with 6 == TYPE_NONE, the type of an undefined function.  This test may be
 * used to determine whether a given variable is defined yet.
 */
static package bfTaskFrames(Var arglist, Byte next, void *vdata, Objid progr){
	vm				theVM;
	int				taskID;

	if(arglist.v.list[0].v.num == 1){	/* Another task... */
		taskID = arglist.v.list[1].v.num;
		free_var(arglist);

		theVM = find_suspended_task(taskID);
		if(!theVM)
			return make_error_pack(E_INVARG);
		if((!is_wizard(progr)) && (progr != progr_of_cur_verb(theVM)))
			return make_error_pack(E_PERM);

		return make_var_pack(MakeFrameList(theVM->activ_stack, theVM->top_activ_stack));
	}
	else	/* This task... */
		return make_var_pack(MakeFrameList(activ_stack, top_activ_stack));
}

/* This function takes 4 arguments.  The first is the task ID on which it should operate.  The
 * second is a 1-based index indicating which frame of that task it should operate on.  This is
 * the index of the list element returned by task_heap() on which the function will act.  The third
 * is either an integer that is a 1-based index indicating which variable is to be altered, or a
 * string that is the variable's name.  The final argument is the value to be assigned to the
 * variable.  If the task ID does not exist, E_INVARG is raised.  If the programmer is not a wizard,
 * E_PERM is raised (I have not fully thought this through w.r.t. security holes).  If the specified
 * frame does not exist, E_RANGE is raised.  If the specified integer-index variable does not exist
 * or the string specified does not name a variable in that stack frame, E_VARNF is raised.
 * It operates on the currently running task if the passed task ID is 0 (an ID which should never
 * arise for an acutal task).
 */
static package bfAlterVariable(Var arglist, Byte next, void *vdata, Objid progr){
	vm			theVM;
	int			taskID, uz, address, frame, i;
	Var			value;
	enum error	out;
	char		*varname = 0;
	activation	a, *theActivStack;
	unsigned	theTopActivStack;

	taskID = arglist.v.list[1].v.num;
	uz = arglist.v.list[2].v.num;

	if(taskID != 0){
		theVM = find_suspended_task(taskID);
		if(!theVM){
			free_var(arglist);
			return make_error_pack(E_INVARG);
		}
		if(!is_wizard(progr)){
			free_var(arglist);
			return make_error_pack(E_PERM);
		}
		theActivStack = theVM->activ_stack;
		theTopActivStack = theVM->top_activ_stack;
	}
	else{
		theActivStack = activ_stack;
		theTopActivStack = top_activ_stack;
	}

	frame = theTopActivStack + 1 - uz;
	if(frame>theTopActivStack || frame < 0){
		free_var(arglist);
		return make_error_pack(E_RANGE);
	}

	if(arglist.v.list[3].type == TYPE_STR){
		varname = str_dup(arglist.v.list[3].v.str);

		a = theActivStack[frame];
		for(i=0; i<a.prog->num_var_names; i++){
			if(!strcmp(varname, a.prog->var_names[i]))
				break;
		}
		free_str(varname);

		if((address = i) == a.prog->num_var_names){
			free_var(arglist);
			return make_error_pack(E_VARNF);
		}
	}
	else if(arglist.v.list[3].type == TYPE_INT)
		address = arglist.v.list[3].v.num;
	else{
		free_var(arglist);
		return make_error_pack(E_TYPE);
	}

	value = var_dup(arglist.v.list[4]);
	out = AlterFrame(theActivStack, frame, address, value);
	free_var(value);
	free_var(arglist);

	if(out != E_NONE)
		return make_error_pack(out);
	else
		return no_var_pack();
}

/* This function takes 3 arguments.  The first is the task ID on which it should operate.  The
 * second is a 1-based index indicating which frame of that task it should operate on.  This is
 * the index of the list element returned by task_frames() on which the function will act.  The third
 * is either an integer that is a 1-based index indicating which variable is to be altered, or a
 * string that is the variable's name.  If the task ID does not exist, E_INVARG is raised.  If the
 * programmer is not a wizard, E_PERM is raised (I have not fully thought this through w.r.t.
 * security holes).  If the specified frame does not exist, E_RANGE is raised.  If the specified
 * integer-index variable does not exist or the string specified does not name a variable in that
 * stack frame, E_VARNF is raised.
 * It operates on the currently running task if the passed task ID is 0 (an ID which should never
 * arise for an acutal task).
 */
static package bfUndefVariable(Var arglist, Byte next, void *vdata, Objid progr){
	vm			theVM;
	int			taskID, uz, address, frame, i;
	Var			value;
	enum error	out;
	char		*varname = 0;
	activation	a, *theActivStack;
	unsigned	theTopActivStack;

	taskID = arglist.v.list[1].v.num;
	uz = arglist.v.list[2].v.num;

	if(taskID != 0){
		theVM = find_suspended_task(taskID);
		if(!theVM){
			free_var(arglist);
			return make_error_pack(E_INVARG);
		}
		if(!is_wizard(progr)){
			free_var(arglist);
			return make_error_pack(E_PERM);
		}
		theActivStack = theVM->activ_stack;
		theTopActivStack = theVM->top_activ_stack;
	}
	else{
		theActivStack = activ_stack;
		theTopActivStack = top_activ_stack;
	}

	frame = theTopActivStack + 1 - uz;
	if(frame>theTopActivStack || frame < 0){
		free_var(arglist);
		return make_error_pack(E_RANGE);
	}

	if(arglist.v.list[3].type == TYPE_STR){
		varname = str_dup(arglist.v.list[3].v.str);

		a = theActivStack[frame];
		for(i=0; i<a.prog->num_var_names; i++){
			if(!strcmp(varname, a.prog->var_names[i]))
				break;
		}
		free_str(varname);

		if((address = i) == a.prog->num_var_names){
			free_var(arglist);
			return make_error_pack(E_VARNF);
		}
	}
	else if(arglist.v.list[3].type == TYPE_INT)
		address = arglist.v.list[3].v.num;
	else{
		free_var(arglist);
		return make_error_pack(E_TYPE);
	}

	value.type = TYPE_NONE;
	out = AlterFrame(theActivStack, frame, address, value);
	free_var(arglist);

	if(out != E_NONE)
		return make_error_pack(out);
	else
		return no_var_pack();
}


// ============= ANSI ===============
    static package
bf_parse_ansi(Var arglist, Byte next, void *vdata, Objid progr)
{
#define ANSI_TAG_TO_CODE(tag, code, case_matters)			\
    {									\
        stream_add_strsub(str, reset_stream(tmp), tag, code, case_matters); \
        stream_add_string(tmp, reset_stream(str));				\
    }

    Var r;
    r.type = TYPE_STR;

    Stream *str = new_stream(50);
    Stream *tmp = new_stream(50);
    const char *random_codes[] = {"\e[31m", "\e[32m", "\e[33m", "\e[34m", "\e[35m", "\e[35m", "\e[36m"};

    stream_add_string(tmp, arglist.v.list[1].v.str);
    free_var(arglist);

    ANSI_TAG_TO_CODE("[red]",        "\e[31m",   0);
    ANSI_TAG_TO_CODE("[green]",      "\e[32m",   0);
    ANSI_TAG_TO_CODE("[yellow]",     "\e[33m",   0);
    ANSI_TAG_TO_CODE("[blue]",       "\e[34m",   0);
    ANSI_TAG_TO_CODE("[purple]",     "\e[35m",   0);
    ANSI_TAG_TO_CODE("[cyan]",       "\e[36m",   0);
    ANSI_TAG_TO_CODE("[normal]",     "\e[0m",    0);
    ANSI_TAG_TO_CODE("[inverse]",    "\e[7m",    0);
    ANSI_TAG_TO_CODE("[underline]",  "\e[4m",    0);
    ANSI_TAG_TO_CODE("[bold]",       "\e[1m",    0);
    ANSI_TAG_TO_CODE("[bright]",     "\e[1m",    0);
    ANSI_TAG_TO_CODE("[unbold]",     "\e[22m",   0);
    ANSI_TAG_TO_CODE("[blink]",      "\e[5m",    0);
    ANSI_TAG_TO_CODE("[unblink]",    "\e[25m",   0);
    ANSI_TAG_TO_CODE("[magenta]",    "\e[35m",   0);
    ANSI_TAG_TO_CODE("[unbright]",   "\e[22m",   0);
    ANSI_TAG_TO_CODE("[white]",      "\e[37m",   0);
    ANSI_TAG_TO_CODE("[gray]",       "\e[1;30m", 0);
    ANSI_TAG_TO_CODE("[grey]",       "\e[1;30m", 0);
    ANSI_TAG_TO_CODE("[beep]",       "\a",       0);
    ANSI_TAG_TO_CODE("[black]",      "\e[30m",   0);
    ANSI_TAG_TO_CODE("[b:black]",   "\e[40m",   0);
    ANSI_TAG_TO_CODE("[b:red]",     "\e[41m",   0);
    ANSI_TAG_TO_CODE("[b:green]",   "\e[42m",   0);
    ANSI_TAG_TO_CODE("[b:yellow]",  "\e[43m",   0);
    ANSI_TAG_TO_CODE("[b:blue]",    "\e[44m",   0);
    ANSI_TAG_TO_CODE("[b:magenta]", "\e[45m",   0);
    ANSI_TAG_TO_CODE("[b:purple]",  "\e[45m",   0);
    ANSI_TAG_TO_CODE("[b:cyan]",    "\e[46m",   0);
    ANSI_TAG_TO_CODE("[b:white]",   "\e[47m",   0);

    char *t = reset_stream(tmp);
    while (*t) {
        if (!mystrncasecmp(t, "[random]", 8)) {
            stream_add_string(str, random_codes[RANDOM() % 6]);
            t += 8;
        } else
            stream_add_char(str, *t++);
    }

    stream_add_strsub(tmp, reset_stream(str), "[null]", "", 0);

    ANSI_TAG_TO_CODE("[null]", "", 0);

    r.v.str = str_dup(reset_stream(tmp));

    free_stream(tmp);
    free_stream(str);
    return make_var_pack(r);

#undef ANSI_TAG_TO_CODE
}


    static package
bf_remove_ansi(Var arglist, Byte next, void *vdata, Objid progr)
{

#define MARK_FOR_REMOVAL(tag)					\
    {								\
        stream_add_strsub(tmp, reset_stream(tmp), tag, "", 0);	\
    }
    Var r;
    Stream *tmp;

    tmp = new_stream(50);
    stream_add_string(tmp, arglist.v.list[1].v.str);
    free_var(arglist);

    MARK_FOR_REMOVAL("[red]");
    MARK_FOR_REMOVAL("[green]");
    MARK_FOR_REMOVAL("[yellow]");
    MARK_FOR_REMOVAL("[blue]");
    MARK_FOR_REMOVAL("[purple]");
    MARK_FOR_REMOVAL("[cyan]");
    MARK_FOR_REMOVAL("[normal]");
    MARK_FOR_REMOVAL("[inverse]");
    MARK_FOR_REMOVAL("[underline]");
    MARK_FOR_REMOVAL("[bold]");
    MARK_FOR_REMOVAL("[bright]");
    MARK_FOR_REMOVAL("[unbold]");
    MARK_FOR_REMOVAL("[blink]");
    MARK_FOR_REMOVAL("[unblink]");
    MARK_FOR_REMOVAL("[magenta]");
    MARK_FOR_REMOVAL("[unbright]");
    MARK_FOR_REMOVAL("[white]");
    MARK_FOR_REMOVAL("[gray]");
    MARK_FOR_REMOVAL("[grey]");
    MARK_FOR_REMOVAL("[beep]");
    MARK_FOR_REMOVAL("[black]");
    MARK_FOR_REMOVAL("[b:black]");
    MARK_FOR_REMOVAL("[b:red]");
    MARK_FOR_REMOVAL("[b:green]");
    MARK_FOR_REMOVAL("[b:yellow]");
    MARK_FOR_REMOVAL("[b:blue]");
    MARK_FOR_REMOVAL("[b:magenta]");
    MARK_FOR_REMOVAL("[b:purple]");
    MARK_FOR_REMOVAL("[b:cyan]");
    MARK_FOR_REMOVAL("[b:white]");
    MARK_FOR_REMOVAL("[random]");
    MARK_FOR_REMOVAL("[null]");

    r.type = TYPE_STR;
    r.v.str = str_dup(reset_stream(tmp));

    free_stream(tmp);
    return make_var_pack(r);

#undef MARK_FOR_REMOVAL
}
//==============================================================

    void
register_extensions()
{
    register_function("frandom", 1, 2, bf_frandom, TYPE_FLOAT, TYPE_FLOAT);
    register_function("round", 1, 1, bf_round, TYPE_FLOAT);
    register_function("distance", 2, 2, bf_distance, TYPE_LIST, TYPE_LIST);
    register_function("relative_heading", 2, 2, bf_relative_heading, TYPE_LIST, TYPE_LIST);
    register_function("memory_usage", 0, 0, bf_memory_usage);
    register_function("usage", 0, 0, bf_usage);
    register_function("ftime", 0, 1, bf_ftime, TYPE_INT);
    register_function("panic", 0, 1, bf_panic, TYPE_STR);
    register_function("locate_by_name", 1, 2, bf_locate_by_name, TYPE_STR, TYPE_INT);
    register_function("explode", 1, 2, bf_explode, TYPE_STR, TYPE_STR);
    register_function("occupants", 1, 3, bf_occupants, TYPE_LIST, TYPE_OBJ, TYPE_INT);
//    register_function("from_list", 1, 1, bf_from_list, TYPE_STR);
    /* Debugger */
    register_function("task_frames", 0, 1, bfTaskFrames, TYPE_INT);
	register_function("alter_variable", 4, 4, bfAlterVariable, TYPE_INT, TYPE_INT, TYPE_ANY, TYPE_ANY);
	register_function("undef_variable", 3, 3, bfUndefVariable, TYPE_INT, TYPE_INT, TYPE_ANY);
    /* ======== ANSI =========== */
    register_function("parse_ansi", 1, 1, bf_parse_ansi, TYPE_STR);
    register_function("remove_ansi", 1, 1, bf_remove_ansi, TYPE_STR);
}
