#include <math.h>           // sqrt, atan, round, etc
#include "functions.h"      // register builtins
#include "log.h"            // oklog()
#include "utils.h"          // streams
#include "numbers.h"        // new_float()
#include "list.h"           // listappend and friends
#include "my-stdlib.h"      // rand()
#include "random.h"         // random() (nowai)
#include "server.h"         // panic_moo()
#include <sys/time.h>       // getrusage
#include <sys/resource.h>   // getrusage
#if !defined(__FreeBSD__) && !defined(__MACH__)
#include <sys/sysinfo.h>    // CPU usage
#endif
#include "extension-background.h"   // Threads
#ifdef __MACH__
#include <mach/clock.h>     // Millisecond time for macOS
#include <mach/mach.h>
#include <sys/sysctl.h>
#endif
#include <vector>
#include <algorithm>        // std::sort
#include "dependencies/strnatcmp.c" // natural sorting

/**
 * On FreeBSD, CLOCK_MONOTONIC_RAW is simply CLOCK_MONOTONIC
 */
#ifdef __FreeBSD__
#define CLOCK_MONOTONIC_RAW CLOCK_MONOTONIC
#endif
/**
 * BSD doesn't support sysinfo.
 * There are probably other ways to get CPU info, but for the sake of compilation we'll just return all 0 for CPU usage on BSD for now.
 */
#if defined(__FreeBSD__)
#define _MOO_NO_CPU_USAGE
#endif

/* Returns a float of the time (including milliseconds)
   Optional arguments specify monotonic time; 1: Monotonic. 2. Monotonic raw.
   (seconds since an arbitrary period of time. More useful for timing
   since its not affected by NTP or other time changes.) */
    static package
bf_ftime(Var arglist, Byte next, void *vdata, Objid progr)
{
#ifdef __MACH__
    // macOS only provides SYSTEM_CLOCK for monotonic time, so our arguments don't matter.
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
    // macOS lacks clock_gettime, use clock_get_time instead
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

    Var r;
    r.type = TYPE_FLOAT;
    r.v.fnum = (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;

    free_var(arglist);
    return make_var_pack(r);
}

/* Locate an object in the database by name more quickly than is possible in-DB.
 * To avoid numerous list reallocations, we put everything in a vector and then
 * transfer it over to a list when we know how many values we have. */
void locate_by_name_thread_callback(void *bw, Var *ret)
{
    Var arglist = ((background_waiter*)bw)->data;

    Var name, object;
    object.type = TYPE_OBJ;
    std::vector<int> tmp;

    int case_matters = is_true(arglist.v.list[2]);
    int string_length = memo_strlen(arglist.v.list[1].v.str);

    for (int x = 1; x < db_last_used_objid(); x++)
    {
        if (!valid(x))
            continue;

        object.v.obj = x;
        db_find_property(object, "name", &name);
        if (strindex(name.v.str, memo_strlen(name.v.str), arglist.v.list[1].v.str, string_length, case_matters))
            tmp.push_back(x);
    }

    *ret = new_list(tmp.size());
    for (size_t x = 0; x < tmp.size(); x++) {
        ret->v.list[x+1].type = TYPE_OBJ;
        ret->v.list[x+1].v.obj = tmp[x];
    }
}

    static package
bf_locate_by_name(Var arglist, Byte next, void *vdata, Objid progr)
{
    if (!is_wizard(progr))
    {
        free_var(arglist);
        return make_error_pack(E_PERM);
    }

    char *human_string = 0;
    asprintf(&human_string, "locate_by_name: \"%s\"", arglist.v.list[1].v.str);

    return background_thread(locate_by_name_thread_callback, &arglist, human_string);
}

int compare_vars(const void *a, const void *b)
{
    Var lhs = *(Var*)a;
    Var rhs = *(Var*)b;

    if (rhs.type != lhs.type)
        return 0;

    switch (rhs.type) {
        case TYPE_INT:
            return lhs.v.num > rhs.v.num ? 1 : -1;
        case TYPE_FLOAT:
            return lhs.v.fnum > rhs.v.fnum ? 1 : -1;
        case TYPE_OBJ:
            return lhs.v.obj > rhs.v.obj ? 1 : -1;
        case TYPE_ERR:
            return ((int) lhs.v.err) > ((int) rhs.v.err) ? 1 : -1;
        case TYPE_STR:
            return strcasecmp(lhs.v.str, rhs.v.str) > 0 ? 1 : -1;
        default:
            errlog("Unknown type in sort compare: %d\n", rhs.type);
            return 0;
    }
}

/* Sort values in descending order.
 * NOTE: If disparate types are combined, they will be considered
 *       equal rather than an error for performance reasons. It's
 *       up to the programmer to use correct types. */
void qsort_callback(void *bw, Var *ret)
{
    Var arglist = ((background_waiter*)bw)->data;

    if (arglist.v.list[1].v.list[0].v.num == 0)
        return;

    *ret = var_dup(arglist.v.list[1]);

    qsort(ret->v.list+1, ret->v.list[0].v.num, sizeof(Var), compare_vars);

}

    static package
bf_qsort(Var arglist, Byte next, void *vdata, Objid progr)
{
    char *human_string = 0;
    asprintf(&human_string, "qsorting %" PRIdN " element list", arglist.v.list[1].v.list[0].v.num);

    return background_thread(qsort_callback, &arglist, human_string);
}

void sort_callback(void *bw, Var *ret)
{
    Var arglist = ((background_waiter*)bw)->data;
    int nargs = arglist.v.list[0].v.num;
    int list_to_sort = (nargs >= 2 && arglist.v.list[2].v.list[0].v.num > 0 ? 2 : 1);
    bool natural = (nargs >= 3 && is_true(arglist.v.list[3]));

    if (arglist.v.list[list_to_sort].v.list[0].v.num == 0)
        return;
    else if (list_to_sort == 2 && arglist.v.list[1].v.list[0].v.num != arglist.v.list[2].v.list[0].v.num) {
        ret->type = TYPE_ERR;
        ret->v.err = E_INVARG;
        return;
    }

    // A vector of indices
    std::vector<size_t> s(arglist.v.list[list_to_sort].v.list[0].v.num);
    var_type type_to_sort = arglist.v.list[list_to_sort].v.list[1].type;

    for (int count = 1; count <= arglist.v.list[list_to_sort].v.list[0].v.num; count++)
    {
        var_type type = arglist.v.list[list_to_sort].v.list[count].type;
        if (type == TYPE_LIST || type == TYPE_MAP || type == TYPE_ANON || type == TYPE_WAIF || type != type_to_sort)
        {
            ret->type = TYPE_ERR;
            ret->v.err = E_INVARG;
            return;
        }
        s[count-1] = count;
    }

    struct VarCompare {
        VarCompare(const Var *Arglist, const bool Natural) : m_Arglist(Arglist), m_Natural(Natural) {}

        bool operator()(size_t a, size_t b) const
        {
            Var lhs = m_Arglist[a];
            Var rhs = m_Arglist[b];

            switch (rhs.type) {
                case TYPE_INT:
                    return lhs.v.num < rhs.v.num;
                case TYPE_FLOAT:
                    return lhs.v.fnum < rhs.v.fnum;
                case TYPE_OBJ:
                    return lhs.v.obj < rhs.v.obj;
                case TYPE_ERR:
                    return ((int) lhs.v.err) < ((int) rhs.v.err);
                case TYPE_STR:
                    return (m_Natural ? strnatcasecmp(lhs.v.str, rhs.v.str) : strcasecmp(lhs.v.str, rhs.v.str)) < 0;
                default:
                    errlog("Unknown type in sort compare: %d\n", rhs.type);
                    return 0;
            }
        }
        const Var *m_Arglist;
        const bool m_Natural;
    };

    std::sort(s.begin(), s.end(), VarCompare(arglist.v.list[list_to_sort].v.list, natural));

    *ret = new_list(s.size());
    for (size_t x = 0; x < s.size(); x++)
        ret->v.list[x+1] = var_dup(arglist.v.list[1].v.list[s[x]]);
}

    static package
bf_sort(Var arglist, Byte next, void *vdata, Objid progr)
{
    char *human_string = 0;
    asprintf(&human_string, "sorting %" PRIdN " element list", arglist.v.list[1].v.list[0].v.num);

    return background_thread(sort_callback, &arglist, human_string);
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
            tmp = (arglist.v.list[2].v.list[count].type == TYPE_INT ? (double)arglist.v.list[2].v.list[count].v.num : arglist.v.list[2].v.list[count].v.fnum) - (arglist.v.list[1].v.list[count].type == TYPE_INT ? (double)arglist.v.list[1].v.list[count].v.num : arglist.v.list[1].v.list[count].v.fnum);
            ret = ret + (tmp * tmp);
        }
    }

    free_var(arglist);

    Var s;
    s.type = TYPE_FLOAT;
    s.v.fnum = sqrt(ret);

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

    double dx = arglist.v.list[2].v.list[1].v.fnum - arglist.v.list[1].v.list[1].v.fnum;
    double dy = arglist.v.list[2].v.list[2].v.fnum - arglist.v.list[1].v.list[2].v.fnum;
    double dz = arglist.v.list[2].v.list[3].v.fnum - arglist.v.list[1].v.list[3].v.fnum;

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

#ifndef _MOO_NO_CPU_USAGE
    /*** Begin CPU load averages ***/
#ifdef __MACH__
    struct loadavg load;
    size_t size = sizeof(load);
    if (sysctlbyname("vm.loadavg", &load, &size, 0, 0) != -1) {
        for (x = 0; x < 3; x++)
            cpu.v.list[x+1].v.num = load.ldavg[x];
    }
#else
    struct sysinfo sys_info;
    int info_ret = sysinfo(&sys_info);

    for (x = 0; x < 3; x++)
        cpu.v.list[x+1].v.num = (info_ret != 0 ? 0 : sys_info.loads[x]);
#endif
#endif

    /*** Now rusage ***/
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);

    r.v.list[1].type = TYPE_FLOAT;
    r.v.list[2].type = TYPE_FLOAT;
    r.v.list[1].v.fnum =(double)usage.ru_utime.tv_sec + (double)usage.ru_utime.tv_usec / CLOCKS_PER_SEC;
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
    panic_moo(msg);

    return make_error_pack(E_NONE);
}

/* Return a random floating point value between 0.0..args[1] or args[1]..args[2] */
    static package
bf_frandom(Var arglist, Byte next, void *vdata, Objid progr)
{
    double fmin = (arglist.v.list[0].v.num > 1 ? arglist.v.list[1].v.fnum : 0.0);
    double fmax = (arglist.v.list[0].v.num > 1 ? arglist.v.list[2].v.fnum : arglist.v.list[1].v.fnum);

    free_var(arglist);

    double f = (double)rand() / RAND_MAX;
    f = fmin + f * (fmax - fmin);

    Var ret;
    ret.type = TYPE_FLOAT;
    ret.v.fnum = f;

    return make_var_pack(ret);

}

/* Round numbers to the nearest integer value to args[1] */
    static package
bf_round(Var arglist, Byte next, void *vdata, Objid progr)
{
    double r = round((double)arglist.v.list[1].v.fnum);

    free_var(arglist);

    Var ret;
    ret.type = TYPE_FLOAT;
    ret.v.fnum = r;

    return make_var_pack(ret);
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

/* Return all items of sublists at index */
    static package
bf_slice(Var arglist, Byte next, void *vdata, Objid progr)
{
    const int length = arglist.v.list[1].v.list[0].v.num;
    if(length < 0)
    {
        free_var(arglist);
        return make_error_pack(E_INVARG);
    }

    Var ret = new_list(0);
    int c;
    if(arglist.v.list[0].v.num == 2)
        c = arglist.v.list[2].v.num;
    else
        c = 1;

    const Var list=arglist.v.list[1];
    for(int i = 1; i <= length; ++i)
        if( list.v.list[i].type != TYPE_LIST || list.v.list[i].v.list[0].v.num < c )
        {
            free_var(ret);
            free_var(arglist);
            return make_error_pack(E_INVARG);
        }
        else
        {
            Var element = var_ref(list.v.list[i].v.list[c]);
            ret = listappend(ret, element);
        }

    free_var(arglist);
    return make_var_pack(ret);
}

/* Return a list of objects of parent, optionally with a player flag set.
 * With only one argument, player flag is assumed to be the only condition.
 * With two arguments, parent is the only condition.
 * With three arguments, parent is checked first and then the player flag is checked.
 * occupants(LIST objects, OBJ parent, ?INT player flag set)
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

/* Return a list of nested locations for an object
 * For objects in $nothing (#-1), this returns an empty list.
 * locations(OBJ object)
 */
    static package
bf_locations(Var arglist, Byte next, void *vdata, Objid progr)
{
    Objid what = arglist.v.list[1].v.obj;

    free_var(arglist);

    if (!valid(what))
        return make_error_pack(E_INVIND);

    Var locs = new_list(0);

    Objid loc = db_object_location(what);

    while (valid(loc)) {
        locs = setadd(locs, Var::new_obj(loc));
        loc = db_object_location(loc);
    }

    return make_var_pack(locs);
}

/* Return a symbol for the ASCII value associated. */
    static package
bf_chr(Var arglist, Byte next, void *vdata, Objid progr)
{
    Var r;
    char str[2];

    switch (arglist.v.list[1].type) {
        case TYPE_INT:
            if ((arglist.v.list[1].v.num < 1) || (arglist.v.list[1].v.num > 255)) {
                free_var(arglist);
                return make_error_pack(E_INVARG);
            } else if (arglist.v.list[1].v.num < 32 && !is_wizard(progr)) {
                free_var(arglist);
                return make_error_pack(E_PERM);
            }
            str[0] = (char) arglist.v.list[1].v.num;
            str[1] = '\0';
            r.type = TYPE_STR;
            r.v.str = str_dup(str);
            break;
        case TYPE_STR:
            if (!(r.v.num = (int) arglist.v.list[1].v.str[0])) {
                free_var(arglist);
                return make_error_pack(E_INVARG);
            }
            r.type = TYPE_INT;
            break;
        default:
            free_var(arglist);
            return make_error_pack(E_TYPE);
    }

    free_var(arglist);
    return make_var_pack(r);
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
        if (!strncasecmp(t, "[random]", 8)) {
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
    register_function("slice", 1, 2, bf_slice, TYPE_LIST, TYPE_INT);
    register_function("occupants", 1, 3, bf_occupants, TYPE_LIST, TYPE_OBJ, TYPE_INT);
    register_function("locations", 1, 1, bf_locations, TYPE_OBJ);
    register_function("chr", 1, 1, bf_chr, TYPE_INT);
    register_function("qsort", 1, 1, bf_qsort, TYPE_LIST);
    register_function("sort", 1, 3, bf_sort, TYPE_LIST, TYPE_LIST, TYPE_INT);
    // ======== ANSI ===========
    register_function("parse_ansi", 1, 1, bf_parse_ansi, TYPE_STR);
    register_function("remove_ansi", 1, 1, bf_remove_ansi, TYPE_STR);
}
