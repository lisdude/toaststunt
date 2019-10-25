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

#include <sys/types.h> //for u_int64_t
#include <limits.h>
#include <errno.h>
#include <float.h>
#include <random>
#include <algorithm>
#include <functional>
#ifdef __MACH__
#include <mach/clock.h>     // Millisecond time for macOS
#include <mach/mach.h>
#include <sys/sysctl.h>
#endif

#include "my-math.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "config.h"
#include "functions.h"
#include "log.h"
#include "random.h"
#include "server.h"
#include "dependencies/sosemanuk.h"
#include "storage.h"
#include "streams.h"
#include "structures.h"
#include "utils.h"
#include "list.h"

sosemanuk_key_context key_context;
sosemanuk_run_context run_context;

static std::mt19937 rng;

void reseed_rng()
{
    std::random_device entropy_source;
    std::seed_seq::result_type data[std::mt19937::state_size];
    std::generate_n(data, std::mt19937::state_size, std::ref(entropy_source));

    std::seed_seq prng_seed(data, data + std::mt19937::state_size);
    rng.seed(prng_seed);
}

int
parse_number(const char *str, Num *result, int try_floating_point)
{
    char *p;

    *result = (Num) strtoimax(str, &p, 10);
    if (try_floating_point &&
	(p == str || *p == '.' || *p == 'e' || *p == 'E'))
	*result = (Num) strtod(str, &p);
    if (p == str)
	return 0;
    while (*p) {
	if (*p != ' ')
	    return 0;
	p++;
    }
    return 1;
}

static int
parse_object(const char *str, Objid * result)
{
    Num number;

    while (*str && *str == ' ')
	str++;
    if (*str == '#')
	str++;
    if (parse_number(str, &number, 0)) {
	*result = number;
	return 1;
    } else
	return 0;
}

int
parse_float(const char *str, double *result)
{
    char *p;
    int negative = 0;

    while (*str && *str == ' ')
	str++;
    if (*str == '-') {
	str++;
	negative = 1;
    }
    *result = strtod(str, &p);
    if (p == str)
	return 0;
    while (*p) {
	if (*p != ' ')
	    return 0;
	p++;
    }
    if (negative)
	*result = -*result;
    return 1;
}

enum error
become_integer(Var in, Num *ret, int called_from_toint)
{
    switch (in.type) {
    case TYPE_INT:
	*ret = in.v.num;
	break;
    case TYPE_STR:
	if (!(called_from_toint
	      ? parse_number(in.v.str, ret, 1)
	      : parse_object(in.v.str, ret)))
	    *ret = 0;
	break;
    case TYPE_OBJ:
	*ret = in.v.obj;
	break;
    case TYPE_ERR:
	*ret = in.v.err;
	break;
    case TYPE_FLOAT:
    if (!IS_REAL(in.v.fnum))
	    return E_FLOAT;
	*ret = (Num) in.v.fnum;
	break;
    case TYPE_MAP:
    case TYPE_LIST:
    case TYPE_ANON:
    case TYPE_WAIF:
	return E_TYPE;
    default:
	errlog("BECOME_INTEGER: Impossible var type: %d\n", (int) in.type);
    }
    return E_NONE;
}

static enum error
become_float(Var in, double *ret)
{
    switch (in.type) {
    case TYPE_INT:
	*ret = (double) in.v.num;
	break;
    case TYPE_STR:
	if (!parse_float(in.v.str, ret) || !IS_REAL(*ret))
	    return E_INVARG;
	break;
    case TYPE_OBJ:
	*ret = (double) in.v.obj;
	break;
    case TYPE_ERR:
	*ret = (double) in.v.err;
	break;
    case TYPE_FLOAT:
	*ret = in.v.fnum;
	break;
    case TYPE_MAP:
    case TYPE_LIST:
    case TYPE_ANON:
    case TYPE_WAIF:
	return E_TYPE;
    default:
	errlog("BECOME_FLOAT: Impossible var type: %d\n", (int) in.type);
    }
    return E_NONE;
}

/**** opcode implementations ****/

/*
 * All of the following implementations are strict, not performing any
 * coercions between integer and floating-point operands.
 */

int
do_equals(Var lhs, Var rhs)
{				/* LHS == RHS */
    /* At least one of LHS and RHS is TYPE_FLOAT */

    if (lhs.type != rhs.type)
	return 0;
    else
	return lhs.v.fnum == rhs.v.fnum;
}

int
compare_integers(Num a, Num b)
{
    if (a < b)
	return -1;
    else if (a > b)
	return 1;
    else
	return 0;
}

Var
compare_numbers(Var a, Var b)
{
    Var ans;

    if (a.type != b.type) {
	ans.type = TYPE_ERR;
	ans.v.err = E_TYPE;
    } else if (a.type == TYPE_INT) {
	ans.type = TYPE_INT;
    if (a.v.num < b.v.num)
        ans.v.num = -1;
    else if (a.v.num > b.v.num)
        ans.v.num = 1;
    else
        ans.v.num = 0;
    } else {
	ans.type = TYPE_INT;
	if (a.v.fnum < b.v.fnum)
	    ans.v.num = -1;
    else if (a.v.fnum > b.v.fnum)
        ans.v.num = 1;
	else
	    ans.v.num = 0;
    }

    return ans;
}


#define SIMPLE_BINARY(name, op)					\
		Var						\
		do_ ## name(Var a, Var b)			\
		{						\
		    Var	ans;					\
								\
		    if (a.type != b.type) {			\
			ans.type = TYPE_ERR;			\
			ans.v.err = E_TYPE;			\
		    } else if (a.type == TYPE_INT) {		\
			ans.type = TYPE_INT;			\
			ans.v.num = a.v.num op b.v.num;		\
		    } else {					\
			double d = a.v.fnum op b.v.fnum;	\
								\
			if (!IS_REAL(d)) {			\
			    ans.type = TYPE_ERR;		\
			    ans.v.err = E_FLOAT;		\
			} else {				\
			    ans.type = TYPE_FLOAT;		\
			    ans.v.fnum = d;			\
			}					\
		    }						\
								\
		    return ans;					\
		}

SIMPLE_BINARY(add, +)
SIMPLE_BINARY(subtract, -)
SIMPLE_BINARY(multiply, *)

Var
do_modulus(Var a, Var b)
{
    Var ans;

    if (a.type != b.type) {
	ans.type = TYPE_ERR;
	ans.v.err = E_TYPE;
    } else if ((a.type == TYPE_INT && b.v.num == 0) ||
               (a.type == TYPE_FLOAT && b.v.fnum == 0.0)) {
	ans.type = TYPE_ERR;
	ans.v.err = E_DIV;
    } else if (a.type == TYPE_INT) {
	ans.type = TYPE_INT;
	if (a.v.num == MININT && b.v.num == -1)
	    ans.v.num = 0;
	else
	    ans.v.num = a.v.num % b.v.num;
    } else { // must be float
	double d = fmod(a.v.fnum, b.v.fnum);
	if (!IS_REAL(d)) {
	    ans.type = TYPE_ERR;
	    ans.v.err = E_FLOAT;
	} else {
        ans.type = TYPE_FLOAT;
        ans.v.fnum = d;
    }
    }
    return ans;
}

Var
do_divide(Var a, Var b)
{
    Var ans;

    if (a.type != b.type) {
	ans.type = TYPE_ERR;
	ans.v.err = E_TYPE;
    } else if ((a.type == TYPE_INT && b.v.num == 0) ||
               (a.type == TYPE_FLOAT && b.v.fnum == 0.0)) {
	ans.type = TYPE_ERR;
	ans.v.err = E_DIV;
    } else if (a.type == TYPE_INT) {
	ans.type = TYPE_INT;
	if (a.v.num == MININT && b.v.num == -1)
	    ans.v.num = MININT;
	else
	    ans.v.num = a.v.num / b.v.num;
    } else { // must be float
	double d = a.v.fnum / b.v.fnum;
	if (!IS_REAL(d)) {
	    ans.type = TYPE_ERR;
	    ans.v.err = E_FLOAT;
	} else {
        ans.type = TYPE_FLOAT;
        ans.v.fnum = d;
    }
    }

    return ans;
}

Var
do_power(Var lhs, Var rhs)
{				/* LHS ^ RHS */
    Var ans;

    if (lhs.type == TYPE_INT) {	/* integer exponentiation */
	Num a = lhs.v.num, b, r;

	if (rhs.type != TYPE_INT)
	    goto type_error;

	b = rhs.v.num;
	ans.type = TYPE_INT;
	if (b < 0) {
	    switch (a) {
	    case -1:
		ans.v.num = (b & 1) ? 1 : -1;
		break;
	    case 0:
		ans.type = TYPE_ERR;
		ans.v.err = E_DIV;
		break;
	    case 1:
		ans.v.num = 1;
		break;
	    default:
		ans.v.num = 0;
        break;
        }
	} else {
	    r = 1;
	    while (b != 0) {
		if (b & 1)
		    r *= a;
		a *= a;
		b >>= 1;
	    }
	    ans.v.num = r;
	}
    } else if (lhs.type == TYPE_FLOAT) {	/* floating-point exponentiation */
	double d;

	switch (rhs.type) {
	case TYPE_INT:
	    d = (double) rhs.v.num;
	    break;
	case TYPE_FLOAT:
	    d = rhs.v.fnum;
	    break;
	default:
	    goto type_error;
	}
	errno = 0;
	d = pow(lhs.v.fnum, d);
	if (errno != 0 || !IS_REAL(d)) {
	    ans.type = TYPE_ERR;
	    ans.v.err = E_FLOAT;
	} else {
        ans.type = TYPE_FLOAT;
        ans.v.fnum = d;
    }
    } else
	goto type_error;

    return ans;

  type_error:
    ans.type = TYPE_ERR;
    ans.v.err = E_TYPE;
    return ans;
}

/**** built in functions ****/

static package
bf_toint(Var arglist, Byte next, void *vdata, Objid progr)
{
    Var r;
    enum error e;

    r.type = TYPE_INT;
    e = become_integer(arglist.v.list[1], &(r.v.num), 1);

    free_var(arglist);
    if (e != E_NONE)
	return make_error_pack(e);

    return make_var_pack(r);
}

static package
bf_tofloat(Var arglist, Byte next, void *vdata, Objid progr)
{
    Var r;
    enum error e;

    r.type = TYPE_FLOAT;
    e = become_float(arglist.v.list[1], &r.v.fnum);

    free_var(arglist);
    if (e != E_NONE)
        return make_error_pack(e);

    return make_var_pack(r);
}

static package
bf_min(Var arglist, Byte next, void *vdata, Objid progr)
{
    Var r;
    int i, nargs = arglist.v.list[0].v.num;
    int bad_types = 0;

    r = arglist.v.list[1];
    if (r.type == TYPE_INT) {	/* integers */
	for (i = 2; i <= nargs; i++)
	    if (arglist.v.list[i].type != TYPE_INT)
		bad_types = 1;
	    else if (arglist.v.list[i].v.num < r.v.num)
		r = arglist.v.list[i];
    } else {			/* floats */
	for (i = 2; i <= nargs; i++)
	    if (arglist.v.list[i].type != TYPE_FLOAT)
		bad_types = 1;
	    else if (arglist.v.list[i].v.fnum < r.v.fnum)
		r = arglist.v.list[i];
    }

    r = var_ref(r);
    free_var(arglist);
    if (bad_types)
	return make_error_pack(E_TYPE);
    else
	return make_var_pack(r);
}

static package
bf_max(Var arglist, Byte next, void *vdata, Objid progr)
{
    Var r;
    int i, nargs = arglist.v.list[0].v.num;
    int bad_types = 0;

    r = arglist.v.list[1];
    if (r.type == TYPE_INT) {	/* integers */
	for (i = 2; i <= nargs; i++)
	    if (arglist.v.list[i].type != TYPE_INT)
		bad_types = 1;
	    else if (arglist.v.list[i].v.num > r.v.num)
		r = arglist.v.list[i];
    } else {			/* floats */
	for (i = 2; i <= nargs; i++)
	    if (arglist.v.list[i].type != TYPE_FLOAT)
		bad_types = 1;
	    else if (arglist.v.list[i].v.fnum > r.v.fnum)
		r = arglist.v.list[i];
    }

    r = var_ref(r);
    free_var(arglist);
    if (bad_types)
	return make_error_pack(E_TYPE);
    else
	return make_var_pack(r);
}

static package
bf_abs(Var arglist, Byte next, void *vdata, Objid progr)
{
    Var r;

    r = var_dup(arglist.v.list[1]);
    if (r.type == TYPE_INT) {
	if (r.v.num < 0)
	    r.v.num = -r.v.num;
    } else
	r.v.fnum = fabs(r.v.fnum);

    free_var(arglist);
    return make_var_pack(r);
}

#define MATH_FUNC(name)							      \
		static package						      \
		bf_ ## name(Var arglist, Byte next, void *vdata, Objid progr) \
		{							      \
		    double	d;					      \
									      \
		    d = arglist.v.list[1].v.fnum;			      \
		    errno = 0;						      \
		    d = name(arglist.v.list[1].v.fnum);					      \
		    free_var(arglist);					      \
		    if (errno == EDOM)					      \
		        return make_error_pack(E_INVARG);		      \
		    else if (errno != 0  ||  !IS_REAL(d))		      \
			return make_error_pack(E_FLOAT);		      \
		    else						      \
			return make_float_pack(d);		      \
		}

MATH_FUNC(sqrt)
MATH_FUNC(sin)
MATH_FUNC(cos)
MATH_FUNC(tan)
MATH_FUNC(asin)
MATH_FUNC(acos)
MATH_FUNC(sinh)
MATH_FUNC(cosh)
MATH_FUNC(tanh)
MATH_FUNC(exp)
MATH_FUNC(log)
MATH_FUNC(log10)
MATH_FUNC(ceil)
MATH_FUNC(floor)

    static package
     bf_trunc(Var arglist, Byte next, void *vdata, Objid progr)
{
    double d;

    d = arglist.v.list[1].v.fnum;
    errno = 0;
    if (d < 0.0)
	d = ceil(d);
    else
	d = floor(d);
    free_var(arglist);
    if (errno == EDOM)
	return make_error_pack(E_INVARG);
    else if (errno != 0 || !IS_REAL(d))
	return make_error_pack(E_FLOAT);
    else
        return make_float_pack(d);
}

static package
bf_atan(Var arglist, Byte next, void *vdata, Objid progr)
{
    double d, dd;

    d = arglist.v.list[1].v.fnum;
    errno = 0;
    if (arglist.v.list[0].v.num >= 2) {
	dd = arglist.v.list[2].v.fnum;
	d = atan2(d, dd);
    } else
	d = atan(d);
    free_var(arglist);
    if (errno == EDOM)
	return make_error_pack(E_INVARG);
    else if (errno != 0 || !IS_REAL(d))
	return make_error_pack(E_FLOAT);
    else
        return make_float_pack(d);
}

static package
bf_time(Var arglist, Byte next, void *vdata, Objid progr)
{
    Var r;
    r.type = TYPE_INT;
    r.v.num = time(nullptr);
    free_var(arglist);
    return make_var_pack(r);
}

static package
bf_ctime(Var arglist, Byte next, void *vdata, Objid progr)
{
    Var r;
    time_t c;
    char buffer[128];
    struct tm *t;

    if (arglist.v.list[0].v.num == 1) {
	c = arglist.v.list[1].v.num;
    } else {
	c = time(nullptr);
    }

    free_var(arglist);

    t = localtime(&c);
    if (t == nullptr)
        return make_error_pack(E_INVARG);

    {				/* Format the time, including a timezone name */
#if HAVE_STRFTIME
	if (strftime(buffer, sizeof buffer, "%a %b %d %H:%M:%S %Y %Z", t) == 0)
        return make_error_pack(E_INVARG);
#else
#  if HAVE_TM_ZONE
	struct tm *t = localtime(&c);
	char *tzname = t->tm_zone;
#  else
#    if !HAVE_TZNAME
	const char *tzname = "XXX";
#    endif
#  endif

	strcpy(buffer, ctime(&c));
	buffer[24] = ' ';
	strncpy(buffer + 25, tzname, 3);
	buffer[28] = '\0';
#endif
    }

    if (buffer[8] == '0')
	buffer[8] = ' ';
    r.type = TYPE_STR;
    r.v.str = str_dup(buffer);

    return make_var_pack(r);
}

#ifdef __FreeBSD__
#define CLOCK_MONOTONIC_RAW CLOCK_MONOTONIC
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

    free_var(arglist);
    return make_var_pack(Var::new_float((double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0));
}

static package
bf_random(Var arglist, Byte next, void *vdata, Objid progr)
{
    int nargs = arglist.v.list[0].v.num;

    Num minnum = (nargs == 2 ? arglist.v.list[1].v.num : 1);
    Num maxnum = (nargs >= 1 ? arglist.v.list[nargs].v.num : INTNUM_MAX);

    free_var(arglist);

    if (maxnum <= 0 || maxnum < minnum || minnum > maxnum)
        	return make_error_pack(E_INVARG);

    std::uniform_int_distribution<Num> distribution(minnum, maxnum);
    Var r = Var::new_int(distribution(rng));
    return make_var_pack(r);
}

static package
bf_reseed_random(Var arglist, Byte next, void *vdata, Objid progr)
{
    free_var(arglist);

    if (!is_wizard(progr))
        return make_error_pack(E_PERM);

    reseed_rng();
    return no_var_pack();
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

    return make_var_pack(Var::new_float(r));
}

#define TRY_STREAM enable_stream_exceptions()
#define ENDTRY_STREAM disable_stream_exceptions()

static package
make_space_pack()
{
    if (server_flag_option_cached(SVO_MAX_CONCAT_CATCHABLE))
	return make_error_pack(E_QUOTA);
    else
	return make_abort_pack(ABORT_SECONDS);
}

static package
bf_random_bytes(Var arglist, Byte next, void *vdata, Objid progr)
{				/* (count) */
    Var r;
    package p;

    int len = arglist.v.list[1].v.num;

    if (len < 0 || len > 10000) {
	p = make_raise_pack(E_INVARG, "Invalid count", var_ref(arglist.v.list[1]));
	free_var(arglist);
	return p;
    }

    unsigned char out[len];

    sosemanuk_prng(&run_context, out, len);

    Stream *s = new_stream(32 * 3);

    TRY_STREAM;
    try {
	stream_add_raw_bytes_to_binary(s, (char *)out, len);

	r.type = TYPE_STR;
	r.v.str = str_dup(stream_contents(s));
	p = make_var_pack(r);
    }
    catch (stream_too_big& exception) {
	p = make_space_pack();
    }
    ENDTRY_STREAM;

    free_stream(s);
    free_var(arglist);

    return p;
}

#undef TRY_STREAM
#undef ENDTRY_STREAM

static package
bf_floatstr(Var arglist, Byte next, void *vdata, Objid progr)
{				/* (float, precision [, sci-notation]) */
    double d = arglist.v.list[1].v.fnum;
    int prec = arglist.v.list[2].v.num;
    int use_sci = (arglist.v.list[0].v.num >= 3
		   && is_true(arglist.v.list[3]));
    char fmt[10], output[500];	/* enough for IEEE double */
    Var r;

    free_var(arglist);
    if (prec > __DECIMAL_DIG__)
	prec = __DECIMAL_DIG__;
    else if (prec < 0)
	return make_error_pack(E_INVARG);
    sprintf(fmt, "%%.%d%c", prec, use_sci ? 'e' : 'f');
    sprintf(output, fmt, d);

    r.type = TYPE_STR;
    r.v.str = str_dup(output);

    return make_var_pack(r);
}

/* Calculates the distance between two n-dimensional sets of coordinates. */
    static package
bf_distance(Var arglist, Byte next, void *vdata, Objid progr)
{
    double ret = 0.0, tmp = 0.0;
    int count;

    const Num list_length = arglist.v.list[1].v.list[0].v.num;
    for (count = 1; count <= list_length; count++)
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

    return make_var_pack(Var::new_float(sqrt(ret)));
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

Var zero;			/* useful constant */

void
register_numbers(void)
{
    zero.type = TYPE_INT;
    zero.v.num = 0;

    reseed_rng();

    register_function("toint", 1, 1, bf_toint, TYPE_ANY);
    register_function("tofloat", 1, 1, bf_tofloat, TYPE_ANY);
    register_function("min", 1, -1, bf_min, TYPE_NUMERIC);
    register_function("max", 1, -1, bf_max, TYPE_NUMERIC);
    register_function("abs", 1, 1, bf_abs, TYPE_NUMERIC);
    register_function("random", 0, 2, bf_random, TYPE_INT, TYPE_INT);
    register_function("reseed_random", 0, 0, bf_reseed_random);
    register_function("frandom", 1, 2, bf_frandom, TYPE_FLOAT, TYPE_FLOAT);
    register_function("round", 1, 1, bf_round, TYPE_FLOAT);
    register_function("random_bytes", 1, 1, bf_random_bytes, TYPE_INT);
    register_function("time", 0, 0, bf_time);
    register_function("ctime", 0, 1, bf_ctime, TYPE_INT);
    register_function("ftime", 0, 1, bf_ftime, TYPE_INT);
    register_function("floatstr", 2, 3, bf_floatstr,
		      TYPE_FLOAT, TYPE_INT, TYPE_ANY);

    register_function("sqrt", 1, 1, bf_sqrt, TYPE_FLOAT);
    register_function("sin", 1, 1, bf_sin, TYPE_FLOAT);
    register_function("cos", 1, 1, bf_cos, TYPE_FLOAT);
    register_function("tan", 1, 1, bf_tan, TYPE_FLOAT);
    register_function("asin", 1, 1, bf_asin, TYPE_FLOAT);
    register_function("acos", 1, 1, bf_acos, TYPE_FLOAT);
    register_function("atan", 1, 2, bf_atan, TYPE_FLOAT, TYPE_FLOAT);
    register_function("sinh", 1, 1, bf_sinh, TYPE_FLOAT);
    register_function("cosh", 1, 1, bf_cosh, TYPE_FLOAT);
    register_function("tanh", 1, 1, bf_tanh, TYPE_FLOAT);
    register_function("exp", 1, 1, bf_exp, TYPE_FLOAT);
    register_function("log", 1, 1, bf_log, TYPE_FLOAT);
    register_function("log10", 1, 1, bf_log10, TYPE_FLOAT);
    register_function("ceil", 1, 1, bf_ceil, TYPE_FLOAT);
    register_function("floor", 1, 1, bf_floor, TYPE_FLOAT);
    register_function("trunc", 1, 1, bf_trunc, TYPE_FLOAT);

    /* Possibly misplaced functions... */
    register_function("distance", 2, 2, bf_distance, TYPE_LIST, TYPE_LIST);
    register_function("relative_heading", 2, 2, bf_relative_heading, TYPE_LIST, TYPE_LIST);
}
