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
#include "map.h"
#include <string.h>         // strtok

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

    Var r = Var::new_float((double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0);

    free_var(arglist);
    return make_var_pack(r);
}

/* Locate an object in the database by name more quickly than is possible in-DB.
 * To avoid numerous list reallocations, we put everything in a vector and then
 * transfer it over to a list when we know how many values we have. */
void locate_by_name_thread_callback(Var arglist, Var *ret)
{
    Var name, object;
    object.type = TYPE_OBJ;
    std::vector<int> tmp;

    const int case_matters = arglist.v.list[0].v.num < 2 ? 0 : is_true(arglist.v.list[2]);
    const int string_length = memo_strlen(arglist.v.list[1].v.str);

    const auto last_objid = db_last_used_objid();
    for (int x = 1; x < last_objid; x++)
    {
        if (!valid(x))
            continue;

        object.v.obj = x;
        db_find_property(object, "name", &name);
        if (strindex(name.v.str, memo_strlen(name.v.str), arglist.v.list[1].v.str, string_length, case_matters))
            tmp.push_back(x);
    }

    *ret = new_list(tmp.size());
    const auto vector_size = tmp.size();
    for (size_t x = 0; x < vector_size; x++) {
        ret->v.list[x+1] = Var::new_obj(tmp[x]);
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

/* Sorts various MOO types using std::sort.
 * Args: LIST <values to sort>, [LIST <values to sort by>], [INT <natural sort ordering?>], [INT <reverse?>] */
void sort_callback(Var arglist, Var *ret)
{
    int nargs = arglist.v.list[0].v.num;
    int list_to_sort = (nargs >= 2 && arglist.v.list[2].v.list[0].v.num > 0 ? 2 : 1);
    bool natural = (nargs >= 3 && is_true(arglist.v.list[3]));
    bool reverse = (nargs >= 4 && is_true(arglist.v.list[4]));

    if (arglist.v.list[list_to_sort].v.list[0].v.num == 0) {
        *ret = new_list(0);
        return;
    } else if (list_to_sort == 2 && arglist.v.list[1].v.list[0].v.num != arglist.v.list[2].v.list[0].v.num) {
        ret->type = TYPE_ERR;
        ret->v.err = E_INVARG;
        return;
    }

    // Create and sort a vector of indices rather than values. This makes it easier to sort a list by another list.
    std::vector<size_t> s(arglist.v.list[list_to_sort].v.list[0].v.num);
    var_type type_to_sort = arglist.v.list[list_to_sort].v.list[1].type;

    for (int count = 1; count <= arglist.v.list[list_to_sort].v.list[0].v.num; count++)
    {
        var_type type = arglist.v.list[list_to_sort].v.list[count].type;
        if (type != type_to_sort || type == TYPE_LIST || type == TYPE_MAP || type == TYPE_ANON || type == TYPE_WAIF)
        {
            ret->type = TYPE_ERR;
            ret->v.err = E_TYPE;
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

    if (reverse)
    {
        int moo_list_pos = 0;
        for (auto it = s.rbegin(); it != s.rend(); ++it)
            ret->v.list[++moo_list_pos] = var_ref(arglist.v.list[1].v.list[*it]);
    } else {
        for (size_t x = 0; x < s.size(); x++)
            ret->v.list[x+1] = var_ref(arglist.v.list[1].v.list[s[x]]);
    }
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

/* Return a list of substrings of an argument separated by a delimiter. */
    static package
bf_explode(Var arglist, Byte next, void *vdata, Objid progr)
{
    const int nargs = arglist.v.list[0].v.num;
    const char *delim = (nargs > 1 ? arglist.v.list[2].v.str : " ");
    const bool adjacent_delim = (nargs > 2 && is_true(arglist.v.list[3]));
    char *found, *return_string, *freeme;
    Var ret = new_list(0);

    freeme = return_string = strdup(arglist.v.list[1].v.str);
    free_var(arglist);

    if (adjacent_delim) {
        while ((found = strsep(&return_string, delim)) != NULL)
            ret = listappend(ret, str_dup_to_var(found));
    } else {
        found = strtok(return_string, delim);
        while (found != NULL) {
            ret = listappend(ret, str_dup_to_var(found));
            found = strtok(NULL, delim);
        }
    }
    free(freeme);
    return make_var_pack(ret);
}

    static package
bf_reverse(Var arglist, Byte next, void *vdata, Objid progr)
{
    Var ret;

    if (arglist.v.list[1].type == TYPE_LIST) {
        int elements = arglist.v.list[1].v.list[0].v.num;
        ret = new_list(elements);

        for (size_t x = elements, y = 1; x >= 1; x--, y++) {
            ret.v.list[y] = var_ref(arglist.v.list[1].v.list[x]);
        }
    } else if (arglist.v.list[1].type == TYPE_STR) {
        size_t len = memo_strlen(arglist.v.list[1].v.str);
        if (len <= 1) {
            ret = var_ref(arglist.v.list[1]);
        } else {
            char *new_str = (char *)mymalloc(len + 1, M_STRING);
            for (size_t x = 0, y = len-1; x < len; x++, y--)
                new_str[x] = arglist.v.list[1].v.str[y];
            new_str[len] = '\0';
            ret.type = TYPE_STR;
            ret.v.str = new_str;
        }
    } else {
        ret.type = TYPE_ERR;
        ret.v.err = E_INVARG;
    }

    free_var(arglist);
    return ret.type == TYPE_ERR ? make_error_pack(ret.v.err) : make_var_pack(ret);
}

static package
bf_slice(Var arglist, Byte next, void *vdata, Objid progr)
{
    Var ret;
    int nargs = arglist.v.list[0].v.num;
    Var alist = arglist.v.list[1];
    Var index = (nargs < 2 ? Var::new_int(1) : arglist.v.list[2]);

    // Validate the types here since we used TYPE_ANY to allow lists and ints
    if (nargs > 1 && index.type != TYPE_LIST && index.type != TYPE_INT && index.type != TYPE_STR) {
        free_var(arglist);
        return make_error_pack(E_INVARG);
    }

    // Check that that index isn't an empty list and doesn't contain negative or zeroes
    if (index.type == TYPE_LIST) {
        if (index.v.list[0].v.num == 0) {
            free_var(arglist);
            return make_error_pack(E_RANGE);
        }

        for (int x = 1; x <= index.v.list[0].v.num; x++) {
            if (index.v.list[x].type != TYPE_INT || index.v.list[x].v.num <= 0) {
                free_var(arglist);
                return make_error_pack((index.v.list[x].type != TYPE_INT ? E_INVARG : E_RANGE));
            }
        }
    } else if (index.v.num <= 0) {
        free_var(arglist);
        return make_error_pack(E_RANGE);
    }

    /* Ideally, we could allocate the list with the number of elements in our first list.
     * Unfortunately, if we need to return an error in the middle of setting elements in the return list,
     * we can't free_var the entire list because some elements haven't been set yet. So instead we do it the
     * old fashioned way unless/until somebody wants to refactor this to do all the error checking ahead of time. */
    ret = new_list(0);

    for (int x = 1; x <= alist.v.list[0].v.num; x++) {
        Var element = alist.v.list[x];
        if ((element.type != TYPE_LIST && element.type != TYPE_STR && element.type != TYPE_MAP)
                || ((element.type == TYPE_MAP && index.type != TYPE_STR) || (index.type == TYPE_STR && element.type != TYPE_MAP))) {
            free_var(arglist);
            free_var(ret);
            return make_error_pack(E_INVARG);
        }
        if (index.type == TYPE_STR) {
            if (element.type != TYPE_MAP) {
                free_var(arglist);
                free_var(ret);
                return make_error_pack(E_INVARG);
            } else {
                Var tmp;
                if (maplookup(element, index, &tmp, 0) != NULL)
                    ret = listappend(ret, var_ref(tmp));
            }
        } else if (index.type == TYPE_INT) {
            if (index.v.num > (element.type == TYPE_STR ? memo_strlen(element.v.str) : element.v.list[0].v.num)) {
                free_var(arglist);
                free_var(ret);
                return make_error_pack(E_RANGE);
            } else {
                ret = listappend(ret, (element.type == TYPE_STR ? substr(var_ref(element), index.v.num, index.v.num) : var_ref(element.v.list[index.v.num])));
            }
        } else if (index.type == TYPE_LIST) {
            Var tmp = new_list(0);
            for (int y = 1; y <= index.v.list[0].v.num; y++) {
                if (index.v.list[y].v.num > (element.type == TYPE_STR ? memo_strlen(element.v.str) : element.v.list[0].v.num)) {
                    free_var(arglist);
                    free_var(ret);
                    free_var(tmp);
                    return make_error_pack(E_RANGE);
                } else {
                    tmp = listappend(tmp, (element.type == TYPE_STR ? substr(var_ref(element), index.v.list[y].v.num, index.v.list[y].v.num) : var_ref(element.v.list[index.v.list[y].v.num])));
                }
            }
            ret = listappend(ret, tmp);
        }
    }
    free_var(arglist);
    return make_var_pack(ret);
}

static bool multi_parent_isa(const Var *object, const Var *parents)
{
    if (parents->type == TYPE_OBJ)
        return db_object_isa(*object, *parents);

    for (int y = 1; y <= parents->v.list[0].v.num; y++)
        if (db_object_isa(*object, parents->v.list[y]))
            return true;

    return false;
}

/* Return a list of objects of parent, optionally with a player flag set.
 * With only one argument, player flag is assumed to be the only condition.
 * With two arguments, parent is the only condition.
 * With three arguments, parent is checked first and then the player flag is checked.
 * occupants(LIST objects, OBJ | LIST parent, ?INT player flag set)
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

    if (check_parent && !is_obj_or_list_of_objs(parent)) {
        free_var(arglist);
        return make_error_pack(E_TYPE);
    }

    for (int x = 1; x <= content_length; x++) {
        Objid oid = contents.v.list[x].v.obj;
        if (valid(oid)
                && (!check_parent ? 1 : multi_parent_isa(&contents.v.list[x], &parent))
                && (!check_player_flag || (check_player_flag && is_user(oid))))
        {
            ret = setadd(ret, contents.v.list[x]);
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

void all_members_thread_callback(Var arglist, Var *ret)
{
    *ret = new_list(0);
    Var data = arglist.v.list[1];
    Var *thelist = arglist.v.list[2].v.list;

    for (int x = 1, list_size = arglist.v.list[2].v.list[0].v.num; x <= list_size; x++)
        if (equality(data, thelist[x], 0))
            *ret = listappend(*ret, Var::new_int(x));
}

/* Return the indices of all elements of a value in a list. */
    static package
bf_all_members(Var arglist, Byte next, void *vdata, Objid progr)
{
    char *human_string = 0;
    asprintf(&human_string, "all_members in %" PRIdN " element list", arglist.v.list[2].v.list[0].v.num);

    return background_thread(all_members_thread_callback, &arglist, human_string);
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

#define STUPID_VERB_CACHE 1
#ifdef STUPID_VERB_CACHE
#include "db_tune.h"

static package
bf_verb_cache_stats(Var arglist, Byte next, void *vdata, Objid progr)
{
    Var r;

    free_var(arglist);

    if (!is_wizard(progr)) {
	return make_error_pack(E_PERM);
    }
    r = db_verb_cache_stats();

    return make_var_pack(r);
}

static package
bf_log_cache_stats(Var arglist, Byte next, void *vdata, Objid progr)
{
    free_var(arglist);

    if (!is_wizard(progr)) {
	return make_error_pack(E_PERM);
    }
    db_log_cache_stats();

    return no_var_pack();
}
#endif

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
    register_function("explode", 1, 3, bf_explode, TYPE_STR, TYPE_STR, TYPE_INT);
    register_function("reverse", 1, 1, bf_reverse, TYPE_ANY);
    register_function("slice", 1, 2, bf_slice, TYPE_LIST, TYPE_ANY);
    register_function("occupants", 1, 3, bf_occupants, TYPE_LIST, TYPE_ANY, TYPE_INT);
    register_function("locations", 1, 1, bf_locations, TYPE_OBJ);
    register_function("sort", 1, 4, bf_sort, TYPE_LIST, TYPE_LIST, TYPE_INT, TYPE_INT);
    register_function("all_members", 2, 2, bf_all_members, TYPE_ANY, TYPE_LIST);
    // ======== ANSI ===========
    register_function("parse_ansi", 1, 1, bf_parse_ansi, TYPE_STR);
    register_function("remove_ansi", 1, 1, bf_remove_ansi, TYPE_STR);
#ifdef STUPID_VERB_CACHE
    register_function("log_cache_stats", 0, 0, bf_log_cache_stats);
    register_function("verb_cache_stats", 0, 0, bf_verb_cache_stats);
#endif
}
