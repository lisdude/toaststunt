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


/* Locates objects in the database by name. Wizard only because of the potential to lock up the whole MOO. */
void locate_by_name_thread_callback(void *bw, Var *ret)
{
    background_waiter *w = (background_waiter*)bw;
    Var *args = (Var*)w->data;

    int x = 1, case_matters = 0;
    *ret = new_list(0);
    Var tmp, name;

    tmp.type = TYPE_OBJ;

    if (args->v.list[0].v.num == 2)
        case_matters = is_true(args->v.list[2]);


    for (x = 1; x < db_last_used_objid(); x++)
    {
        if (valid(x))
        {
            db_find_property(Var::new_obj(x), "name", &name);
            if (strindex(name.v.str, memo_strlen(name.v.str), args->v.list[1].v.str, memo_strlen(args->v.list[1].v.str), case_matters))
            {
                tmp.v.obj = x;
                *ret = listappend(*ret, tmp);
            }
        }
    }

    free_var(tmp);
}

    static package
bf_locate_by_name(Var arglist, Byte next, void *vdata, Objid progr)
{

    if(!is_wizard(progr))
    {
        free_var(arglist);
        return make_error_pack(E_PERM);
    }

    Var *data = (Var*)mymalloc(sizeof(arglist), M_STRUCT);
    *data = var_dup(arglist);
    free_var(arglist);
    return background_thread(locate_by_name_thread_callback, data);
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
    Var s = new_list(5);

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
    register_function("locate_by_name", 1, 2, bf_locate_by_name, TYPE_STR);
    // ======== ANSI ===========
    register_function("parse_ansi", 1, 1, bf_parse_ansi, TYPE_STR);
    register_function("remove_ansi", 1, 1, bf_remove_ansi, TYPE_STR);
}
