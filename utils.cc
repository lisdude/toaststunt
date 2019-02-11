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

#include "my-ctype.h"
#include "my-stdio.h"
#include "my-string.h"

#include "config.h"
#include "db.h"
#include "db_io.h"
#include "garbage.h"
#include "list.h"
#include "log.h"
#include "map.h"
#include "match.h"
#include "numbers.h"
#include "quota.h"
#include "server.h"
#include "storage.h"
#include "streams.h"
#include "structures.h"
#include "waif.h"
#include "utils.h"

static const char ascii[] =
"\000\001\002\003\004\005\006\007\010\011\012\013\014\015\016\017"
"\020\021\022\023\024\025\026\027\030\031\032\033\034\035\036\037"
"\040\041\042\043\044\045\046\047\050\051\052\053\054\055\056\057"
"\060\061\062\063\064\065\066\067\070\071\072\073\074\075\076\077"
"\100\101\102\103\104\105\106\107\110\111\112\113\114\115\116\117"
"\120\121\122\123\124\125\126\127\130\131\132\133\134\135\136\137"
"\140\141\142\143\144\145\146\147\150\151\152\153\154\155\156\157"
"\160\161\162\163\164\165\166\167\170\171\172\173\174\175\176\177";

/*
 * These versions of strcasecmp() and strncasecmp() depend on ASCII.
 * We implement them here because neither one is in the ANSI standard.
 */

static const char cmap[] =
"\000\001\002\003\004\005\006\007\010\011\012\013\014\015\016\017"
"\020\021\022\023\024\025\026\027\030\031\032\033\034\035\036\037"
"\040\041\042\043\044\045\046\047\050\051\052\053\054\055\056\057"
"\060\061\062\063\064\065\066\067\070\071\072\073\074\075\076\077"
"\100\141\142\143\144\145\146\147\150\151\152\153\154\155\156\157"
"\160\161\162\163\164\165\166\167\170\171\172\133\134\135\136\137"
"\140\141\142\143\144\145\146\147\150\151\152\153\154\155\156\157"
"\160\161\162\163\164\165\166\167\170\171\172\173\174\175\176\177"
"\200\201\202\203\204\205\206\207\210\211\212\213\214\215\216\217"
"\220\221\222\223\224\225\226\227\230\231\232\233\234\235\236\237"
"\240\241\242\243\244\245\246\247\250\251\252\253\254\255\256\257"
"\260\261\262\263\264\265\266\267\270\271\272\273\274\275\276\277"
"\300\301\302\303\304\305\306\307\310\311\312\313\314\315\316\317"
"\320\321\322\323\324\325\326\327\330\331\332\333\334\335\336\337"
"\340\341\342\343\344\345\346\347\350\351\352\353\354\355\356\357"
"\360\361\362\363\364\365\366\367\370\371\372\373\374\375\376\377";

int
verbcasecmp(const char *verb, const char *word)
{
    const unsigned char *w;
    const unsigned char *v = (const unsigned char *) verb;
    enum {
	none, inner, end
    } star;

    if (verb == word) {
	return 1;
    }
    while (*v) {
	w = (const unsigned char *) word;
	star = none;
	while (1) {
	    while (*v == '*') {
		v++;
		star = (!*v || *v == ' ') ? end : inner;
	    }
	    if (!*v || *v == ' ' || !*w || cmap[*w] != cmap[*v])
		break;
	    w++;
	    v++;
	}
	if (!*w ? (star != none || !*v || *v == ' ')
	    : (star == end))
	    return 1;
	while (*v && *v != ' ')
	    v++;
	while (*v == ' ')
	    v++;
    }
    return 0;
}

unsigned
str_hash(const char *s)
{
    unsigned ans = 0;

    while (*s) {
	ans = (ans << 3) + (ans >> 28) + cmap[(unsigned char) *s++];
    }
    return ans;
}

/* Used by the cyclic garbage collector to free values that entered
 * the buffer of possible roots, but subsequently had their refcount
 * drop to zero.  Roughly corresponds to `Free' in Bacon and Rajan.
 */
void
aux_free(Var v)
{
    switch ((int) v.type) {
    case TYPE_LIST:
	myfree(v.v.list, M_LIST);
	break;
    case TYPE_MAP:
	myfree(v.v.tree, M_TREE);
	break;
    case TYPE_ANON:
	assert(db_object_has_flag2(v, FLAG_INVALID));
	myfree(v.v.anon, M_ANON);
	break;
    }
}

#ifdef ENABLE_GC
/* Corresponds to `Decrement' and `Release' in Bacon and Rajan. */
void
complex_free_var(Var v)
{
    switch (v.type) {
    case TYPE_STR:
	if (v.v.str)
	    free_str(v.v.str);
	break;
    case TYPE_LIST:
	if (v.v.list != emptylist.v.list && delref(v.v.list) == 0) {
	    destroy_list(v);
	    gc_set_color(v.v.list, GC_BLACK);
	    if (!gc_is_buffered(v.v.list))
		myfree(v.v.list, M_LIST);
	}
	else
	    gc_possible_root(v);
	break;
    case TYPE_MAP:
	if (delref(v.v.tree) == 0) {
	    destroy_map(v);
	    gc_set_color(v.v.tree, GC_BLACK);
	    if (!gc_is_buffered(v.v.tree))
		myfree(v.v.tree, M_TREE);
	}
	else
	    gc_possible_root(v);
	break;
    case TYPE_ITER:
	if (delref(v.v.trav) == 0)
	    destroy_iter(v);
	break;
    case TYPE_WAIF:
    if (delref(v.v.waif) == 0) {
        if (destroyed_waifs.count(v.v.waif) == 0)
            destroyed_waifs[v.v.waif] = false;
    }
    break;
    case TYPE_ANON:
	/* The first time an anonymous object's reference count drops
	 * to zero, it isn't immediately destroyed/freed.  Instead, it
	 * is queued up to be "recycled" (to have its `recycle' verb
	 * called) -- this has the effect of (perhaps temporarily)
	 * creating a new reference to the object, as well as setting
	 * the recycled flag and (eventually) the invalid flag.
	 */
	if (v.v.anon) {
	    if (delref(v.v.anon) == 0) {
		if (db_object_has_flag2(v, FLAG_RECYCLED)) {
		    gc_set_color(v.v.anon, GC_BLACK);
		    if (!gc_is_buffered(v.v.anon))
			myfree(v.v.anon, M_ANON);
		}
		else if (db_object_has_flag2(v, FLAG_INVALID)) {
		    incr_quota(db_object_owner2(v));
		    db_destroy_anonymous_object(v.v.anon);
		    gc_set_color(v.v.anon, GC_BLACK);
		    if (!gc_is_buffered(v.v.anon))
			myfree(v.v.anon, M_ANON);
		}
		else {
		    queue_anonymous_object(v);
		}
	    }
	    else {
		gc_possible_root(v);
	    }
	}
	break;
    }
}
#else
void
complex_free_var(Var v)
{
    switch ((int) v.type) {
    case TYPE_STR:
	if (v.v.str)
	    free_str(v.v.str);
	break;
    case TYPE_LIST:
	if (v.v.list != emptylist.v.list && delref(v.v.list) == 0)
	    destroy_list(v);
	break;
    case TYPE_MAP:
	if (delref(v.v.tree) == 0)
	    destroy_map(v);
	break;
    case TYPE_ITER:
	if (delref(v.v.trav) == 0)
	    destroy_iter(v);
	break;
	case TYPE_WAIF:
	if (delref(v.v.waif) == 0) {
        if (destroyed_waifs.count(v.v.waif) == 0)
            destroyed_waifs[v.v.waif] = false;
    }
	break;
    case TYPE_ANON:
	if (v.v.anon && delref(v.v.anon) == 0) {
	    if (db_object_has_flag2(v, FLAG_RECYCLED)) {
		myfree(v.v.anon, M_ANON);
	    }
	    else if (db_object_has_flag2(v, FLAG_INVALID)) {
		incr_quota(db_object_owner2(v));
		db_destroy_anonymous_object(v.v.anon);
		myfree(v.v.anon, M_ANON);
	    }
	    else {
		queue_anonymous_object(v);
	    }
	}
	break;
    }
}
#endif

#ifdef ENABLE_GC
/* Corresponds to `Increment' in Bacon and Rajan. */
Var
complex_var_ref(Var v)
{
    switch (v.type) {
    case TYPE_STR:
	addref(v.v.str);
	break;
    case TYPE_LIST:
	addref(v.v.list);
	break;
    case TYPE_MAP:
	addref(v.v.tree);
	break;
    case TYPE_ITER:
	addref(v.v.trav);
	break;
	case TYPE_WAIF:
	addref(v.v.waif);
	break;
    case TYPE_ANON:
	if (v.v.anon) {
	    addref(v.v.anon);
	    if (gc_get_color(v.v.anon) != GC_BLACK)
		gc_set_color(v.v.anon, GC_BLACK);
	}
	break;
    }
    return v;
}
#else
Var
complex_var_ref(Var v)
{
    switch (v.type) {
    case TYPE_STR:
	addref(v.v.str);
	break;
    case TYPE_LIST:
	addref(v.v.list);
	break;
    case TYPE_MAP:
	addref(v.v.tree);
	break;
    case TYPE_ITER:
	addref(v.v.trav);
	break;
	case TYPE_WAIF:
	addref(v.v.waif);
	break;
    case TYPE_ANON:
	if (v.v.anon)
	    addref(v.v.anon);
	break;
    }
    return v;
}
#endif

Var
complex_var_dup(Var v)
{
    switch (v.type) {
    case TYPE_STR:
	v.v.str = str_dup(v.v.str);
	break;
    case TYPE_LIST:
	v = list_dup(v);
	break;
    case TYPE_MAP:
	v = map_dup(v);
	break;
    case TYPE_ITER:
	v = iter_dup(v);
	break;
	case TYPE_WAIF:
	v.v.waif = dup_waif(v.v.waif);
	break;
    case TYPE_ANON:
	panic("cannot var_dup() anonymous objects\n");
	break;
    }
    return v;
}

/* could be inlined and use complex_etc like the others, but this should
 * usually be called in a context where we already konw the type.
 */
int
var_refcount(Var v)
{
    switch (v.type) {
    case TYPE_STR:
	return refcount(v.v.str);
	break;
    case TYPE_LIST:
	return refcount(v.v.list);
	break;
    case TYPE_MAP:
	return refcount(v.v.tree);
	break;
    case TYPE_ITER:
	return refcount(v.v.trav);
	break;
    case TYPE_ANON:
	if (v.v.anon)
	    return refcount(v.v.anon);
	break;
    }
    return 1;
}

int
is_true(Var v)
{
    return ((v.type == TYPE_INT && v.v.num != 0)
	    || (v.type == TYPE_FLOAT && v.v.fnum != 0.0)
	    || (v.type == TYPE_STR && v.v.str && *v.v.str != '\0')
	    || (v.type == TYPE_LIST && v.v.list[0].v.num != 0)
	    || (v.type == TYPE_MAP && !mapempty(v)));
}

/* What is the sound of the comparison:
 *   [1 -> 2] < [2 -> 1]
 * I don't know either; therefore, I do not compare maps
 * (nor other collection types, for the time being).
 */
int
compare(Var lhs, Var rhs, int case_matters)
{
    if (lhs.type == rhs.type) {
	switch (lhs.type) {
	case TYPE_INT:
	    return lhs.v.num - rhs.v.num;
	case TYPE_OBJ:
	    return lhs.v.obj - rhs.v.obj;
	case TYPE_ERR:
	    return lhs.v.err - rhs.v.err;
	case TYPE_STR:
	    if (lhs.v.str == rhs.v.str)
		return 0;
	    else if (case_matters)
		return strcmp(lhs.v.str, rhs.v.str);
	    else
		return strcasecmp(lhs.v.str, rhs.v.str);
	case TYPE_FLOAT:
	    if (lhs.v.fnum == rhs.v.fnum)
		return 0;
	    else
		return (lhs.v.fnum) - (rhs.v.fnum);
    case TYPE_WAIF:
        return lhs.v.waif == rhs.v.waif ? 0 : 1;
    case TYPE_ANON:
        return lhs.v.anon == rhs.v.anon ? 0 : 1;
	default:
	    panic("COMPARE: Invalid value type");
	}
    }
    return lhs.type - rhs.type;
}

int
equality(Var lhs, Var rhs, int case_matters)
{
    if (lhs.type == rhs.type) {
	switch (lhs.type) {
	case TYPE_CLEAR:
	    return 1;
	case TYPE_NONE:
	    return 1;
	case TYPE_INT:
	    return lhs.v.num == rhs.v.num;
	case TYPE_OBJ:
	    return lhs.v.obj == rhs.v.obj;
	case TYPE_ERR:
	    return lhs.v.err == rhs.v.err;
	case TYPE_STR:
	    if (lhs.v.str == rhs.v.str)
		return 1;
	    else if (case_matters)
		return !strcmp(lhs.v.str, rhs.v.str);
	    else
		return !strcasecmp(lhs.v.str, rhs.v.str);
	case TYPE_FLOAT:
	    if (lhs.v.fnum == rhs.v.fnum)
		return 1;
	    else
		return (lhs.v.fnum) == (rhs.v.fnum);
	case TYPE_LIST:
	    return listequal(lhs, rhs, case_matters);
	case TYPE_MAP:
	    return mapequal(lhs, rhs, case_matters);
	case TYPE_ANON:
	    return lhs.v.anon == rhs.v.anon;
	case TYPE_WAIF:
		return lhs.v.waif == rhs.v.waif;
	default:
	    panic("EQUALITY: Unknown value type");
	}
    }
    return 0;
}

void
stream_add_strsub(Stream *str, const char *source, const char *what, const char *with, int case_counts)
{
    int lwhat = strlen(what);

    while (*source) {
	if (!(case_counts ? strncmp(source, what, lwhat)
	      : strncasecmp(source, what, lwhat))) {
	    stream_add_string(str, with);
	    source += lwhat;
	} else
	    stream_add_char(str, *source++);
    }
}

const char *
strtr(const char *source, int source_len,
      const char *from, int from_len,
      const char *to, int to_len,
      int case_counts)
{
    int i;
    char temp[128];
    static Stream *str = 0;

    if (!str)
	str = new_stream(100);

    memcpy(temp, ascii, 128);

    for (i = 0; i < from_len; i++) {
	int c = from[i];
	if (!case_counts && isalpha(c)) {
	    temp[toupper(c)] = i < to_len ? toupper(to[i]) : 0;
	    temp[tolower(c)] = i < to_len ? tolower(to[i]) : 0;
	}
	else {
	    temp[c] = i < to_len ? to[i] : 0;
	}
    }

    for (i = 0; i < source_len; i++) {
	int c = temp[source[i]];
	if (c > 0)
	    stream_add_char(str, c);
    }

    return reset_stream(str);
}

int
strindex(const char *source, int source_len,
         const char *what, int what_len, int case_counts)
{
    const char *s, *e;

    for (s = source, e = source + source_len - what_len; s <= e; s++) {
	if (!(case_counts ? strncmp(s, what, what_len)
	      : strncasecmp(s, what, what_len))) {
	    return s - source + 1;
	}
    }
    return 0;
}

int
strrindex(const char *source, int source_len,
          const char *what, int what_len, int case_counts)
{
    const char *s;

    for (s = source + source_len - what_len; s >= source; s--) {
	if (!(case_counts ? strncmp(s, what, what_len)
	      : strncasecmp(s, what, what_len))) {
	    return s - source + 1;
	}
    }
    return 0;
}

Var
get_system_property(const char *name)
{
    Var value;
    db_prop_handle h;

    if (!valid(SYSTEM_OBJECT)) {
	value.type = TYPE_ERR;
	value.v.err = E_INVIND;
	return value;
    }
    h = db_find_property(Var::new_obj(SYSTEM_OBJECT), name, &value);
    if (!h.ptr) {
	value.type = TYPE_ERR;
	value.v.err = E_PROPNF;
    } else if (!db_is_property_built_in(h))	/* make two cases the same */
	value = var_ref(value);
    return value;
}

Objid
get_system_object(const char *name)
{
    Var value;

    value = get_system_property(name);
    if (value.type != TYPE_OBJ) {
	free_var(value);
	return NOTHING;
    } else
	return value.v.obj;
}

int
value_bytes(Var v)
{
    int size = sizeof(Var);

    switch (v.type) {
    case TYPE_STR:
	size += memo_strlen(v.v.str) + 1;
	break;
    case TYPE_FLOAT:
	size += sizeof(double);
	break;
    case TYPE_LIST:
	size += list_sizeof(v.v.list);
	break;
    case TYPE_MAP:
	size += map_sizeof(v.v.tree);
	break;
	case TYPE_WAIF:
	size += waif_bytes(v.v.waif);
	break;
    default:
	break;
    }

    return size;
}

void
stream_add_raw_bytes_to_clean(Stream *s, const char *buffer, int buflen)
{
    int i;

    for (i = 0; i < buflen; i++) {
	unsigned char c = buffer[i];

	if (isgraph(c) || c == ' ')
	    stream_add_char(s, c);
	/* else
	    drop it */
    }
}

const char *
raw_bytes_to_clean(const char *buffer, int buflen)
{
    static Stream *s = 0;

    if (!s)
	s = new_stream(buflen+1);

    stream_add_raw_bytes_to_clean(s, buffer, buflen);

    return reset_stream(s);
}

const char *
clean_to_raw_bytes(const char *buffer, int *buflen)
{
    *buflen = strlen(buffer);
    return buffer;
}

void
stream_add_raw_bytes_to_binary(Stream *s, const char *buffer, int buflen)
{
    int i;

    for (i = 0; i < buflen; i++) {
	unsigned char c = buffer[i];

	if (c != '~' && (isgraph(c) || c == ' '))
	    stream_add_char(s, c);
	else
	    stream_printf(s, "~%02x", (int) c);
    }
}

const char *
raw_bytes_to_binary(const char *buffer, int buflen)
{
    static Stream *s = 0;

    if (!s)
	s = new_stream(100);

    stream_add_raw_bytes_to_binary(s, buffer, buflen);

    return reset_stream(s);
}

const char *
binary_to_raw_bytes(const char *binary, int *buflen)
{
    static Stream *s = 0;
    const char *ptr = binary;

    if (!s)
	s = new_stream(100);
    else
	reset_stream(s);

    while (*ptr) {
	unsigned char c = *ptr++;

	if (c != '~')
	    stream_add_char(s, c);
	else {
	    int i;
	    char cc = 0;

	    for (i = 1; i <= 2; i++) {
		c = toupper(*ptr++);
		if (('0' <= c && c <= '9') || ('A' <= c && c <= 'F'))
		    cc = cc * 16 + (c <= '9' ? c - '0' : c - 'A' + 10);
		else
		    return 0;
	    }

	    stream_add_char(s, cc);
	}
    }

    *buflen = stream_length(s);
    return reset_stream(s);
}

Var
anonymizing_var_ref(Var v, Objid progr)
{
    Var r;

    if (TYPE_ANON != v.type)
	return var_ref(v);

    if (valid(progr)
        && (is_wizard(progr) || db_object_owner2(v) == progr))
	return var_ref(v);

    r.type = TYPE_ANON;
    r.v.anon = NULL;

    return r;
}
