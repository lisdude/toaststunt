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
#include <stdlib.h>

#include "my-ctype.h"
#include "my-string.h"
#include "my-math.h"

#include "bf_register.h"
#include "collection.h"
#include "config.h"
#include "functions.h"
#include "list.h"
#include "log.h"
#include "map.h"
#include "options.h"
#include "pattern.h"
#include "streams.h"
#include "storage.h"
#include "structures.h"
#include "unparse.h"
#include "utils.h"
#include "server.h"

/* Bandaid: Something is killing all of our references to the
 * empty list, which is causing the server to crash. So this is
 * now a global and utils.cc won't free the list if it's emptylist. */
Var emptylist;

Var
new_list(int size)
{
    Var list;
    Var *ptr;

    if (size == 0) {

	if (emptylist.v.list == NULL) {
	    if ((ptr = (Var *)mymalloc(1 * sizeof(Var), M_LIST)) == NULL)
		panic_moo("EMPTY_LIST: mymalloc failed");

	    emptylist.type = TYPE_LIST;
	    emptylist.v.list = ptr;
	    emptylist.v.list[0].type = TYPE_INT;
	    emptylist.v.list[0].v.num = 0;
	}

#ifdef ENABLE_GC
	assert(gc_get_color(emptylist.v.list) == GC_GREEN);
#endif

	addref(emptylist.v.list);

	return emptylist;
    }

    if ((ptr = (Var *)mymalloc((size + 1) * sizeof(Var), M_LIST)) == NULL)
	panic_moo("EMPTY_LIST: mymalloc failed");

    list.type = TYPE_LIST;
    list.v.list = ptr;
    list.v.list[0].type = TYPE_INT;
    list.v.list[0].v.num = size;

#ifdef ENABLE_GC
    gc_set_color(list.v.list, GC_YELLOW);
#endif

    return list;
}

/* called from utils.c */
void
destroy_list(Var list)
{
    int i;
    Var *pv;

    for (i = list.v.list[0].v.num, pv = list.v.list + 1; i > 0; i--, pv++)
	free_var(*pv);

    /* Since this list could possibly be the root of a cycle, final
     * destruction is handled in the garbage collector if garbage
     * collection is enabled.
     */
#ifndef ENABLE_GC
    myfree(list.v.list, M_LIST);
#endif
}

/* called from utils.c */
Var
list_dup(Var list)
{
    int i, n = list.v.list[0].v.num;
    Var _new = new_list(n);

    for (i = 1; i <= n; i++)
	_new.v.list[i] = var_ref(list.v.list[i]);

    gc_set_color(_new.v.list, gc_get_color(list.v.list));

    return _new;
}

int
listforeach(Var list, listfunc func, void *data)
{				/* does NOT consume `list' */
    int i, n;
    int first = 1;
    int ret;

    for (i = 1, n = list.v.list[0].v.num; i <= n; i++) {
	if ((ret = (*func)(list.v.list[i], data, first)))
	    return ret;
	first = 0;
    }

    return 0;
}

Var
setadd(Var list, Var value)
{
    if (ismember(value, list, 0)) {
	free_var(value);
	return list;
    }
    return listappend(list, value);
}

Var
setremove(Var list, Var value)
{
    int i;

    if ((i = ismember(value, list, 0)) != 0) {
	return listdelete(list, i);
    } else {
	return list;
    }
}

Var
listset(Var list, Var value, int pos)
{				/* consumes `list', `value' */
    Var _new = list;

    if (var_refcount(list) > 1) {
	_new = var_dup(list);
	free_var(list);
    }

#ifdef MEMO_VALUE_BYTES
    /* reset the memoized size */
    ((int *)(_new.v.list))[-2] = 0;
#endif

    free_var(_new.v.list[pos]);
    _new.v.list[pos] = value;

#ifdef ENABLE_GC
    gc_set_color(_new.v.list, GC_YELLOW);
#endif

    return _new;
}

static Var
doinsert(Var list, Var value, int pos)
{
    Var _new;
    int i;
    int size = list.v.list[0].v.num + 1;

    /* Bandaid: See the top of list.cc for an explanation */
    if (list.v.list != emptylist.v.list && var_refcount(list) == 1 && pos == size) {
	list.v.list = (Var *) myrealloc(list.v.list, (size + 1) * sizeof(Var), M_LIST);
#ifdef MEMO_VALUE_BYTES
	/* reset the memoized size */
	((int *)(list.v.list))[-2] = 0;
#endif
	list.v.list[0].v.num = size;
	list.v.list[pos] = value;

#ifdef ENABLE_GC
	gc_set_color(list.v.list, GC_YELLOW);
#endif

	return list;
    }
    _new = new_list(size);
    for (i = 1; i < pos; i++)
	_new.v.list[i] = var_ref(list.v.list[i]);
    _new.v.list[pos] = value;
    for (i = pos; i <= list.v.list[0].v.num; i++)
	_new.v.list[i + 1] = var_ref(list.v.list[i]);

    free_var(list);

#ifdef ENABLE_GC
    gc_set_color(_new.v.list, GC_YELLOW);
#endif

    return _new;
}

Var
listinsert(Var list, Var value, int pos)
{
    if (pos <= 0)
	pos = 1;
    else if (pos > list.v.list[0].v.num)
	pos = list.v.list[0].v.num + 1;
    return doinsert(list, value, pos);
}

Var
listappend(Var list, Var value)
{
    return doinsert(list, value, list.v.list[0].v.num + 1);
}

Var
listdelete(Var list, int pos)
{
    Var _new;
    int i;
    int size = list.v.list[0].v.num - 1;

    _new = new_list(size);
    for (i = 1; i < pos; i++) {
	_new.v.list[i] = var_ref(list.v.list[i]);
    }
    for (i = pos + 1; i <= list.v.list[0].v.num; i++)
	_new.v.list[i - 1] = var_ref(list.v.list[i]);

    free_var(list);

#ifdef ENABLE_GC
    if (size > 0)		/* only non-empty lists */
	gc_set_color(_new.v.list, GC_YELLOW);
#endif

    return _new;
}

Var
listconcat(Var first, Var second)
{
    int lsecond = second.v.list[0].v.num;
    int lfirst = first.v.list[0].v.num;
    Var _new;
    int i;

    _new = new_list(lsecond + lfirst);
    for (i = 1; i <= lfirst; i++)
	_new.v.list[i] = var_ref(first.v.list[i]);
    for (i = 1; i <= lsecond; i++)
	_new.v.list[i + lfirst] = var_ref(second.v.list[i]);

    free_var(first);
    free_var(second);

#ifdef ENABLE_GC
    if (lsecond + lfirst > 0)	/* only non-empty lists */
	gc_set_color(_new.v.list, GC_YELLOW);
#endif

    return _new;
}

Var
listrangeset(Var base, int from, int to, Var value)
{
    /* base and value are free'd */
    int index, offset = 0;
    int val_len = value.v.list[0].v.num;
    int base_len = base.v.list[0].v.num;
    int lenleft = (from > 1) ? from - 1 : 0;
    int lenmiddle = val_len;
    int lenright = (base_len > to) ? base_len - to : 0;
    int newsize = lenleft + lenmiddle + lenright;
    Var ans;

    ans = new_list(newsize);
    for (index = 1; index <= lenleft; index++)
	ans.v.list[++offset] = var_ref(base.v.list[index]);
    for (index = 1; index <= lenmiddle; index++)
	ans.v.list[++offset] = var_ref(value.v.list[index]);
    for (index = 1; index <= lenright; index++)
	ans.v.list[++offset] = var_ref(base.v.list[to + index]);

    free_var(base);
    free_var(value);

#ifdef ENABLE_GC
    if (newsize > 0)	/* only non-empty lists */
	gc_set_color(ans.v.list, GC_YELLOW);
#endif

    return ans;
}

Var
sublist(Var list, int lower, int upper)
{
    if (lower > upper) {
	free_var(list);
	return new_list(0);
    } else {
	Var r;
	int i;

	r = new_list(upper - lower + 1);
	for (i = lower; i <= upper; i++)
	    r.v.list[i - lower + 1] = var_ref(list.v.list[i]);

	free_var(list);

#ifdef ENABLE_GC
	gc_set_color(r.v.list, GC_YELLOW);
#endif

	return r;
    }
}

int
listequal(Var lhs, Var rhs, int case_matters)
{
    if (lhs.v.list == rhs.v.list)
	return 1;

    if (lhs.v.list[0].v.num != rhs.v.list[0].v.num)
	return 0;

    int i, c = lhs.v.list[0].v.num;
    for (i = 1; i <= c; i++) {
	if (!equality(lhs.v.list[i], rhs.v.list[i], case_matters))
	    return 0;
    }

    return 1;
}

static void
stream_add_tostr(Stream * s, Var v)
{
    switch (v.type) {
    case TYPE_INT:
	stream_printf(s, "%" PRIdN, v.v.num);
	break;
    case TYPE_OBJ:
	stream_printf(s, "#%" PRIdN, v.v.obj);
	break;
    case TYPE_STR:
	stream_add_string(s, v.v.str);
	break;
    case TYPE_ERR:
	stream_add_string(s, unparse_error(v.v.err));
	break;
    case TYPE_FLOAT:
	unparse_value(s, v);
	break;
    case TYPE_MAP:
	stream_add_string(s, "[map]");
	break;
    case TYPE_LIST:
	stream_add_string(s, "{list}");
	break;
    case TYPE_ANON:
	stream_add_string(s, "*anonymous*");
	break;
    case TYPE_WAIF:
    stream_add_string(s, "[[waif]]");
    break;
    default:
	panic_moo("STREAM_ADD_TOSTR: Unknown Var type");
    }
}

const char *
value2str(Var value)
{
    if (value.type == TYPE_STR) {
	/* do this case separately to avoid two copies
	 * and to ensure that the stream never grows */
	return str_ref(value.v.str);
    }
    else {
	static Stream *s = 0;
	if (!s)
	    s = new_stream(32);
	stream_add_tostr(s, value);
	return str_dup(reset_stream(s));
    }
}

static int
print_map_to_stream(Var key, Var value, void *sptr, int first)
{
    Stream *s = (Stream *)sptr;

    if (!first) {
	stream_add_string(s, ", ");
    }

    unparse_value(s, key);
    stream_add_string(s, " -> ");
    unparse_value(s, value);

    return 0;
}

void
unparse_value(Stream * s, Var v)
{
    switch (v.type) {
    case TYPE_INT:
	stream_printf(s, "%" PRIdN, v.v.num);
	break;
    case TYPE_OBJ:
	stream_printf(s, "#%" PRIdN, v.v.obj);
	break;
    case TYPE_ERR:
	stream_add_string(s, error_name(v.v.err));
	break;
    case TYPE_FLOAT:
    char buffer[40];
    snprintf(buffer, 40, "%.*g", DBL_DIG, v.v.fnum);
    if (!strchr(buffer, '.') && !strchr(buffer, 'e'))
        strncat(buffer, ".0", 40);
    stream_add_string(s, buffer);
    break;
    case TYPE_STR:
	{
	    const char *str = v.v.str;

	    stream_add_char(s, '"');
	    while (*str) {
		switch (*str) {
		case '"':
		case '\\':
		    stream_add_char(s, '\\');
		    /* fall thru */
		default:
		    stream_add_char(s, *str++);
		}
	    }
	    stream_add_char(s, '"');
	}
	break;
    case TYPE_LIST:
	{
	    const char *sep = "";
	    int len, i;

	    stream_add_char(s, '{');
	    len = v.v.list[0].v.num;
	    for (i = 1; i <= len; i++) {
		stream_add_string(s, sep);
		sep = ", ";
		unparse_value(s, v.v.list[i]);
	    }
	    stream_add_char(s, '}');
	}
	break;
    case TYPE_MAP:
	{
	    stream_add_char(s, '[');
	    mapforeach(v, print_map_to_stream, (void *)s);
	    stream_add_char(s, ']');
	}
	break;
    case TYPE_ANON:
	stream_add_string(s, "*anonymous*");
	break;
    case TYPE_WAIF:
    stream_printf(s, "[[class = #%" PRIdN ", owner = #%" PRIdN "]]", v.v.waif->_class, v.v.waif->owner);
    break;
    default:
	errlog("UNPARSE_VALUE: Unknown Var type = %d\n", v.type);
	stream_add_string(s, ">>Unknown value<<");
    }
}

/* called from utils.c */
int
list_sizeof(Var *list)
{
    int i, len, size;

#ifdef MEMO_VALUE_BYTES
    if ((size = (((int *)(list))[-2])))
	return size;
#endif

    size = sizeof(Var);	/* for the `length' element */
    len = list[0].v.num;
    for (i = 1; i <= len; i++) {
	size += value_bytes(list[i]);
    }

#ifdef MEMO_VALUE_BYTES
    (((int *)(list))[-2]) = size;
#endif

    return size;
}

Var
strrangeset(Var base, int from, int to, Var value)
{
    /* base and value are free'd */
    int index, offset = 0;
    int val_len = memo_strlen(value.v.str);
    int base_len = memo_strlen(base.v.str);
    int lenleft = (from > 1) ? from - 1 : 0;
    int lenmiddle = val_len;
    int lenright = (base_len > to) ? base_len - to : 0;
    int newsize = lenleft + lenmiddle + lenright;

    Var ans;
    char *s;

    ans.type = TYPE_STR;
    s = (char *)mymalloc(sizeof(char) * (newsize + 1), M_STRING);

    for (index = 0; index < lenleft; index++)
	s[offset++] = base.v.str[index];
    for (index = 0; index < lenmiddle; index++)
	s[offset++] = value.v.str[index];
    for (index = 0; index < lenright; index++)
	s[offset++] = base.v.str[index + to];
    s[offset] = '\0';
    ans.v.str = s;
    free_var(base);
    free_var(value);
    return ans;
}

Var
substr(Var str, int lower, int upper)
{
    Var r;

    r.type = TYPE_STR;
    if (lower > upper)
	r.v.str = str_dup("");
    else {
	int loop, index = 0;
	char *s = (char *)mymalloc(upper - lower + 2, M_STRING);

	for (loop = lower - 1; loop < upper; loop++)
	    s[index++] = str.v.str[loop];
	s[index] = '\0';
	r.v.str = s;
    }
    free_var(str);
    return r;
}

Var
strget(Var str, int i)
{
    Var r;
    char *s;

    r.type = TYPE_STR;
    s = str_dup(" ");
    s[0] = str.v.str[i - 1];
    r.v.str = s;
    return r;
}

/**** helpers for catching overly large allocations ****/

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

/**** built in functions ****/

static package
bf_length(Var arglist, Byte next, void *vdata, Objid progr)
{
    Var r;
    switch (arglist.v.list[1].type) {
    case TYPE_LIST:
	r.type = TYPE_INT;
	r.v.num = arglist.v.list[1].v.list[0].v.num;
	break;
    case TYPE_MAP:
	r.type = TYPE_INT;
	r.v.num = maplength(arglist.v.list[1]);
	break;
    case TYPE_STR:
	r.type = TYPE_INT;
	r.v.num = memo_strlen(arglist.v.list[1].v.str);
	break;
    default:
	free_var(arglist);
	return make_error_pack(E_TYPE);
	break;
    }

    free_var(arglist);
    return make_var_pack(r);
}

static package
bf_setadd(Var arglist, Byte next, void *vdata, Objid progr)
{
    Var r;
    Var lst = var_ref(arglist.v.list[1]);
    Var elt = var_ref(arglist.v.list[2]);

    free_var(arglist);

    r = setadd(lst, elt);

    if (value_bytes(r) <= server_int_option_cached(SVO_MAX_LIST_VALUE_BYTES))
	return make_var_pack(r);
    else {
	free_var(r);
	return make_space_pack();
    }
}


static package
bf_setremove(Var arglist, Byte next, void *vdata, Objid progr)
{
    Var r;

    r = setremove(var_ref(arglist.v.list[1]), arglist.v.list[2]);
    free_var(arglist);

    if (value_bytes(r) <= server_int_option_cached(SVO_MAX_LIST_VALUE_BYTES))
	return make_var_pack(r);
    else {
	free_var(r);
	return make_space_pack();
    }
}


static package
insert_or_append(Var arglist, int append1)
{
    int pos;
    Var r;
    Var lst = var_ref(arglist.v.list[1]);
    Var elt = var_ref(arglist.v.list[2]);

    if (arglist.v.list[0].v.num == 2)
	pos = append1 ? lst.v.list[0].v.num + 1 : 1;
    else {
	pos = arglist.v.list[3].v.num + append1;
	if (pos <= 0)
	    pos = 1;
	else if (pos > lst.v.list[0].v.num + 1)
	    pos = lst.v.list[0].v.num + 1;
    }
    free_var(arglist);

    r = doinsert(lst, elt, pos);

    if (value_bytes(r) <= server_int_option_cached(SVO_MAX_LIST_VALUE_BYTES))
	return make_var_pack(r);
    else {
	free_var(r);
	return make_space_pack();
    }
}


static package
bf_listappend(Var arglist, Byte next, void *vdata, Objid progr)
{
    return insert_or_append(arglist, 1);
}


static package
bf_listinsert(Var arglist, Byte next, void *vdata, Objid progr)
{
    return insert_or_append(arglist, 0);
}


static package
bf_listdelete(Var arglist, Byte next, void *vdata, Objid progr)
{
    Var r;
    if (arglist.v.list[2].v.num <= 0
	|| arglist.v.list[2].v.num > arglist.v.list[1].v.list[0].v.num) {
	free_var(arglist);
	return make_error_pack(E_RANGE);
    }

    r = listdelete(var_ref(arglist.v.list[1]), arglist.v.list[2].v.num);

    free_var(arglist);

    if (value_bytes(r) <= server_int_option_cached(SVO_MAX_LIST_VALUE_BYTES))
	return make_var_pack(r);
    else {
	free_var(r);
	return make_space_pack();
    }
}


static package
bf_listset(Var arglist, Byte next, void *vdata, Objid progr)
{
    Var r;

    Var lst = var_ref(arglist.v.list[1]);
    Var elt = var_ref(arglist.v.list[2]);
    int pos = arglist.v.list[3].v.num;

    free_var(arglist);

    if (pos <= 0 || pos > listlength(lst))
	return make_error_pack(E_RANGE);

    r = listset(lst, elt, pos);

    if (value_bytes(r) <= server_int_option_cached(SVO_MAX_LIST_VALUE_BYTES))
	return make_var_pack(r);
    else {
	free_var(r);
	return make_space_pack();
    }
}

static package
bf_equal(Var arglist, Byte next, void *vdata, Objid progr)
{
    Var r;

    r.type = TYPE_INT;
    r.v.num = equality(arglist.v.list[1], arglist.v.list[2], 1);
    free_var(arglist);
    return make_var_pack(r);
}

static package
bf_strsub(Var arglist, Byte next, void *vdata, Objid progr)
{				/* (source, what, with [, case-matters]) */
    int case_matters = 0;
    Stream *s;
    package p;

    if (arglist.v.list[0].v.num == 4)
	case_matters = is_true(arglist.v.list[4]);
    if (arglist.v.list[2].v.str[0] == '\0') {
	free_var(arglist);
	return make_error_pack(E_INVARG);
    }
    s = new_stream(100);
    TRY_STREAM;
    try {
	Var r;
	stream_add_strsub(s, arglist.v.list[1].v.str, arglist.v.list[2].v.str,
			  arglist.v.list[3].v.str, case_matters);
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

static int
signum(int x)
{
    return x < 0 ? -1 : (x > 0 ? 1 : 0);
}

static package
bf_strcmp(Var arglist, Byte next, void *vdata, Objid progr)
{				/* (string1, string2) */
    Var r;

    r.type = TYPE_INT;
    r.v.num = signum(strcmp(arglist.v.list[1].v.str, arglist.v.list[2].v.str));
    free_var(arglist);
    return make_var_pack(r);
}

static package
bf_strtr(Var arglist, Byte next, void *vdata, Objid progr)
{				/* (subject, from, to [, case_matters]) */
    Var r;
    int case_matters = 0;

    if (arglist.v.list[0].v.num > 3)
	case_matters = is_true(arglist.v.list[4]);
    r.type = TYPE_STR;
    r.v.str = str_dup(strtr(arglist.v.list[1].v.str, memo_strlen(arglist.v.list[1].v.str),
			    arglist.v.list[2].v.str, memo_strlen(arglist.v.list[2].v.str),
			    arglist.v.list[3].v.str, memo_strlen(arglist.v.list[3].v.str),
			    case_matters));
    free_var(arglist);
    return make_var_pack(r);
}

static package
bf_index(Var arglist, Byte next, void *vdata, Objid progr)
{				/* (source, what [, case-matters [, offset]]) */
    Var r;
    int case_matters = 0;
    int offset = 0;

    if (arglist.v.list[0].v.num > 2)
	case_matters = is_true(arglist.v.list[3]);
    if (arglist.v.list[0].v.num > 3)
	offset = arglist.v.list[4].v.num;
    if (offset < 0) {
	free_var(arglist);
	return make_error_pack(E_INVARG);
    }
    r.type = TYPE_INT;
    r.v.num = strindex(arglist.v.list[1].v.str + offset, memo_strlen(arglist.v.list[1].v.str) - offset,
		       arglist.v.list[2].v.str, memo_strlen(arglist.v.list[2].v.str),
		       case_matters);

    free_var(arglist);
    return make_var_pack(r);
}

static package
bf_rindex(Var arglist, Byte next, void *vdata, Objid progr)
{				/* (source, what [, case-matters [, offset]]) */
    Var r;

    int case_matters = 0;
    int offset = 0;

    if (arglist.v.list[0].v.num > 2)
	case_matters = is_true(arglist.v.list[3]);
    if (arglist.v.list[0].v.num > 3)
	offset = arglist.v.list[4].v.num;
    if (offset > 0) {
	free_var(arglist);
	return make_error_pack(E_INVARG);
    }
    r.type = TYPE_INT;
    r.v.num = strrindex(arglist.v.list[1].v.str, memo_strlen(arglist.v.list[1].v.str) + offset,
			arglist.v.list[2].v.str, memo_strlen(arglist.v.list[2].v.str),
			case_matters);

    free_var(arglist);
    return make_var_pack(r);
}

static package
bf_tostr(Var arglist, Byte next, void *vdata, Objid progr)
{
    package p;
    Stream *s = new_stream(100);

    TRY_STREAM;
    try {
	Var r;
	int i;

	for (i = 1; i <= arglist.v.list[0].v.num; i++) {
	    stream_add_tostr(s, arglist.v.list[i]);
	}
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

static package
bf_toliteral(Var arglist, Byte next, void *vdata, Objid progr)
{
    package p;
    Stream *s = new_stream(100);

    TRY_STREAM;
    try {
	Var r;

	unparse_value(s, arglist.v.list[1]);
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

struct pat_cache_entry {
    char *string;
    int case_matters;
    Pattern pattern;
    struct pat_cache_entry *next;
};

static struct pat_cache_entry *pat_cache;
static struct pat_cache_entry pat_cache_entries[PATTERN_CACHE_SIZE];

static void
setup_pattern_cache()
{
    int i;

    for (i = 0; i < PATTERN_CACHE_SIZE; i++) {
	pat_cache_entries[i].string = 0;
	pat_cache_entries[i].pattern.ptr = 0;
    }

    for (i = 0; i < PATTERN_CACHE_SIZE - 1; i++)
	pat_cache_entries[i].next = &(pat_cache_entries[i + 1]);
    pat_cache_entries[PATTERN_CACHE_SIZE - 1].next = 0;

    pat_cache = &(pat_cache_entries[0]);
}

static Pattern
get_pattern(const char *string, int case_matters)
{
    struct pat_cache_entry *entry, **entry_ptr;

    entry = pat_cache;
    entry_ptr = &pat_cache;

    while (1) {
	if (entry->string && !strcmp(string, entry->string)
	    && case_matters == entry->case_matters) {
	    /* A cache hit; move this entry to the front of the cache. */
	    break;
	} else if (!entry->next) {
	    /* A cache miss; this is the last entry in the cache, so reuse that
	     * one for this pattern, moving it to the front of the cache iff
	     * the compilation succeeds.
	     */
	    if (entry->string) {
		free_str(entry->string);
		free_pattern(entry->pattern);
	    }
	    entry->pattern = new_pattern(string, case_matters);
	    entry->case_matters = case_matters;
	    if (!entry->pattern.ptr)
		entry->string = 0;
	    else
		entry->string = str_dup(string);
	    break;
	} else {
	    /* not done searching the cache... */
	    entry_ptr = &(entry->next);
	    entry = entry->next;
	}
    }

    *entry_ptr = entry->next;
    entry->next = pat_cache;
    pat_cache = entry;
    return entry->pattern;
}

Var
do_match(Var arglist, int reverse)
{
    const char *subject, *pattern;
    int i;
    Pattern pat;
    Var ans;
    Match_Indices regs[10];

    subject = arglist.v.list[1].v.str;
    pattern = arglist.v.list[2].v.str;
    pat = get_pattern(pattern, (arglist.v.list[0].v.num == 3
				&& is_true(arglist.v.list[3])));

    if (!pat.ptr) {
	ans.type = TYPE_ERR;
	ans.v.err = E_INVARG;
    } else
	switch (match_pattern(pat, subject, regs, reverse)) {
	default:
	    panic_moo("do_match:  match_pattern returned unfortunate value.\n");
	    /*notreached*/
	case MATCH_SUCCEEDED:
	    ans = new_list(4);
	    ans.v.list[1].type = TYPE_INT;
	    ans.v.list[2].type = TYPE_INT;
	    ans.v.list[4].type = TYPE_STR;
	    ans.v.list[1].v.num = regs[0].start;
	    ans.v.list[2].v.num = regs[0].end;
	    ans.v.list[3] = new_list(9);
	    ans.v.list[4].v.str = str_ref(subject);
	    for (i = 1; i <= 9; i++) {
		ans.v.list[3].v.list[i] = new_list(2);
		ans.v.list[3].v.list[i].v.list[1].type = TYPE_INT;
		ans.v.list[3].v.list[i].v.list[1].v.num = regs[i].start;
		ans.v.list[3].v.list[i].v.list[2].type = TYPE_INT;
		ans.v.list[3].v.list[i].v.list[2].v.num = regs[i].end;
	    }
	    break;
	case MATCH_FAILED:
	    ans = new_list(0);
	    break;
	case MATCH_ABORTED:
	    ans.type = TYPE_ERR;
	    ans.v.err = E_QUOTA;
	    break;
	}

    return ans;
}

static package
bf_match(Var arglist, Byte next, void *vdata, Objid progr)
{
    Var ans;

    ans = do_match(arglist, 0);
    free_var(arglist);
    if (ans.type == TYPE_ERR)
	return make_error_pack(ans.v.err);
    else
	return make_var_pack(ans);
}

static package
bf_rmatch(Var arglist, Byte next, void *vdata, Objid progr)
{
    Var ans;

    ans = do_match(arglist, 1);
    free_var(arglist);
    if (ans.type == TYPE_ERR)
	return make_error_pack(ans.v.err);
    else
	return make_var_pack(ans);
}

int
invalid_pair(int num1, int num2, int max)
{
    if ((num1 == 0 && num2 == -1)
	|| (num1 > 0 && num2 >= num1 - 1 && num2 <= max))
	return 0;
    else
	return 1;
}

int
check_subs_list(Var subs)
{
    const char *subj;
    int subj_length, loop;

    if (subs.type != TYPE_LIST || subs.v.list[0].v.num != 4
	|| subs.v.list[1].type != TYPE_INT
	|| subs.v.list[2].type != TYPE_INT
	|| subs.v.list[3].type != TYPE_LIST
	|| subs.v.list[3].v.list[0].v.num != 9
	|| subs.v.list[4].type != TYPE_STR)
	return 1;
    subj = subs.v.list[4].v.str;
    subj_length = memo_strlen(subj);
    if (invalid_pair(subs.v.list[1].v.num, subs.v.list[2].v.num,
		     subj_length))
	return 1;

    for (loop = 1; loop <= 9; loop++) {
	Var pair;
	pair = subs.v.list[3].v.list[loop];
	if (pair.type != TYPE_LIST
	    || pair.v.list[0].v.num != 2
	    || pair.v.list[1].type != TYPE_INT
	    || pair.v.list[2].type != TYPE_INT
	    || invalid_pair(pair.v.list[1].v.num, pair.v.list[2].v.num,
			    subj_length))
	    return 1;
    }
    return 0;
}

static package
bf_substitute(Var arglist, Byte next, void *vdata, Objid progr)
{
    int template_length;
    const char *_template, *subject;
    Var subs, ans;
    package p;
    Stream *s;
    char c = '\0';

    _template = arglist.v.list[1].v.str;
    template_length = memo_strlen(_template);
    subs = arglist.v.list[2];

    if (check_subs_list(subs)) {
	free_var(arglist);
	return make_error_pack(E_INVARG);
    }
    subject = subs.v.list[4].v.str;

    s = new_stream(template_length);
    TRY_STREAM;
    try {
	while ((c = *(_template++)) != '\0') {
	    if (c != '%')
		stream_add_char(s, c);
	    else if ((c = *(_template++)) == '%')
		stream_add_char(s, '%');
	    else {
		int start = 0, end = 0;
		if (c >= '1' && c <= '9') {
		    Var pair = subs.v.list[3].v.list[c - '0'];
		    start = pair.v.list[1].v.num - 1;
		    end = pair.v.list[2].v.num - 1;
		} else if (c == '0') {
		    start = subs.v.list[1].v.num - 1;
		    end = subs.v.list[2].v.num - 1;
		} else {
		    p = make_error_pack(E_INVARG);
		    goto oops;
		}
		while (start <= end)
		    stream_add_char(s, subject[start++]);
	    }
	}
	ans.type = TYPE_STR;
	ans.v.str = str_dup(stream_contents(s));
	p = make_var_pack(ans);
      oops: ;
    }
    catch (stream_too_big& exception) {
	p = make_space_pack();
    }
    ENDTRY_STREAM;
    free_var(arglist);
    free_stream(s);
    return p;
}

static package
bf_value_bytes(Var arglist, Byte next, void *vdata, Objid progr)
{
    Var r;

    r.type = TYPE_INT;
    r.v.num = value_bytes(arglist.v.list[1]);
    free_var(arglist);
    return make_var_pack(r);
}

static package
bf_decode_binary(Var arglist, Byte next, void *vdata, Objid progr)
{
    int length;
    const char *bytes = binary_to_raw_bytes(arglist.v.list[1].v.str, &length);
    int nargs = arglist.v.list[0].v.num;
    int fully = (nargs >= 2 && is_true(arglist.v.list[2]));
    Var r;
    int i;

    free_var(arglist);
    if (!bytes)
	return make_error_pack(E_INVARG);

    if (fully) {
	r = new_list(length);
	for (i = 1; i <= length; i++) {
	    r.v.list[i].type = TYPE_INT;
	    r.v.list[i].v.num = (unsigned char) bytes[i - 1];
	}
    } else {
	int count, in_string;
	Stream *s = new_stream(50);

	for (count = in_string = 0, i = 0; i < length; i++) {
	    unsigned char c = bytes[i];

	    if (isgraph(c) || c == ' ' || c == '\t') {
		if (!in_string)
		    count++;
		in_string = 1;
	    } else {
		count++;
		in_string = 0;
	    }
	}

	r = new_list(count);
	for (count = 1, in_string = 0, i = 0; i < length; i++) {
	    unsigned char c = bytes[i];

	    if (isgraph(c) || c == ' ' || c == '\t') {
		stream_add_char(s, c);
		in_string = 1;
	    } else {
		if (in_string) {
		    r.v.list[count].type = TYPE_STR;
		    r.v.list[count].v.str = str_dup(reset_stream(s));
		    count++;
		}
		r.v.list[count].type = TYPE_INT;
		r.v.list[count].v.num = c;
		count++;
		in_string = 0;
	    }
	}

	if (in_string) {
	    r.v.list[count].type = TYPE_STR;
	    r.v.list[count].v.str = str_dup(reset_stream(s));
	}
	free_stream(s);
    }

    if (value_bytes(r) <= server_int_option_cached(SVO_MAX_LIST_VALUE_BYTES))
	return make_var_pack(r);
    else {
	free_var(r);
	return make_space_pack();
    }
}

static int
encode_binary(Stream * s, Var v)
{
    int i;

    switch (v.type) {
    case TYPE_INT:
	if (v.v.num < 0 || v.v.num >= 256)
	    return 0;
	stream_add_char(s, (char) v.v.num);
	break;
    case TYPE_STR:
	stream_add_string(s, v.v.str);
	break;
    case TYPE_LIST:
	for (i = 1; i <= v.v.list[0].v.num; i++)
	    if (!encode_binary(s, v.v.list[i]))
		return 0;
	break;
    default:
	return 0;
    }

    return 1;
}

static package
bf_encode_binary(Var arglist, Byte next, void *vdata, Objid progr)
{
    Var r;
    package p;
    Stream *s = new_stream(100);
    Stream *s2 = new_stream(100);

    TRY_STREAM;
    try {
	if (encode_binary(s, arglist)) {
	    stream_add_raw_bytes_to_binary(
		s2, stream_contents(s), stream_length(s));
	    r.type = TYPE_STR;
	    r.v.str = str_dup(stream_contents(s2));
	    p = make_var_pack(r);
	}
	else
	    p = make_error_pack(E_INVARG);
    }
    catch (stream_too_big& exception) {
	p = make_space_pack();
    }
    ENDTRY_STREAM;
    free_stream(s2);
    free_stream(s);
    free_var(arglist);
    return p;
}

void
register_list(void)
{
    register_function("value_bytes", 1, 1, bf_value_bytes, TYPE_ANY);

    register_function("decode_binary", 1, 2, bf_decode_binary,
		      TYPE_STR, TYPE_ANY);
    register_function("encode_binary", 0, -1, bf_encode_binary);
    /* list */
    register_function("length", 1, 1, bf_length, TYPE_ANY);
    register_function("setadd", 2, 2, bf_setadd, TYPE_LIST, TYPE_ANY);
    register_function("setremove", 2, 2, bf_setremove, TYPE_LIST, TYPE_ANY);
    register_function("listappend", 2, 3, bf_listappend,
		      TYPE_LIST, TYPE_ANY, TYPE_INT);
    register_function("listinsert", 2, 3, bf_listinsert,
		      TYPE_LIST, TYPE_ANY, TYPE_INT);
    register_function("listdelete", 2, 2, bf_listdelete, TYPE_LIST, TYPE_INT);
    register_function("listset", 3, 3, bf_listset,
		      TYPE_LIST, TYPE_ANY, TYPE_INT);
    register_function("equal", 2, 2, bf_equal, TYPE_ANY, TYPE_ANY);

    /* string */
    register_function("tostr", 0, -1, bf_tostr);
    register_function("toliteral", 1, 1, bf_toliteral, TYPE_ANY);
    setup_pattern_cache();
    register_function("match", 2, 3, bf_match, TYPE_STR, TYPE_STR, TYPE_ANY);
    register_function("rmatch", 2, 3, bf_rmatch, TYPE_STR, TYPE_STR, TYPE_ANY);
    register_function("substitute", 2, 2, bf_substitute, TYPE_STR, TYPE_LIST);
    register_function("index", 2, 4, bf_index,
		      TYPE_STR, TYPE_STR, TYPE_ANY, TYPE_INT);
    register_function("rindex", 2, 4, bf_rindex,
		      TYPE_STR, TYPE_STR, TYPE_ANY, TYPE_INT);
    register_function("strcmp", 2, 2, bf_strcmp, TYPE_STR, TYPE_STR);
    register_function("strsub", 3, 4, bf_strsub,
		      TYPE_STR, TYPE_STR, TYPE_STR, TYPE_ANY);
    register_function("strtr", 3, 4, bf_strtr,
		      TYPE_STR, TYPE_STR, TYPE_STR, TYPE_ANY);
}
