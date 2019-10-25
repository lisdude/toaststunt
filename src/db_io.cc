/******************************************************************************
  Copyright (c) 1995, 1996 Xerox Corporation.  All rights reserved.
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

/*****************************************************************************
 * Routines for use by non-DB modules with persistent state stored in the DB
 *****************************************************************************/

#include "config.h"
#include <ctype.h>
#include <float.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "db.h"
#include "db_io.h"
#include "db_private.h"
#include "list.h"
#include "log.h"
#include "map.h"
#include "numbers.h"
#include "parser.h"
#include "server.h"
#include "storage.h"
#include "streams.h"
#include "structures.h"
#include "str_intern.h"
#include "unparse.h"
#include "version.h"
#include "waif.h"


/*********** Input ***********/

static FILE *input;

void
dbpriv_set_dbio_input(FILE * f)
{
    input = f;
}

void
dbio_read_line(char *s, int n)
{
    fgets(s, n, input);
}

int
dbio_scanf(const char *format,...)
{
    va_list args;
    int count;

    va_start(args, format);
    count = vfscanf(input, format, args);
    va_end(args);

    return count;
}

Num
dbio_read_num(void)
{
    char s[22];
    char *p;
    long long i;

    fgets(s, sizeof(s), input);
    i = strtoll(s, &p, 10);
    if (isspace(*s) || *p != '\n')
	errlog("DBIO_READ_NUM: Bad number: \"%s\" at file pos. %ld\n",
	       s, ftell(input));
    return i;
}

double
dbio_read_float(void)
{
    char s[40];
    char *p;
    double d;

    fgets(s, 40, input);
    d = strtod(s, &p);
    if (isspace(*s) || *p != '\n')
	errlog("DBIO_READ_FLOAT: Bad number: \"%s\" at file pos. %ld\n",
	       s, ftell(input));
    return d;
}

Objid
dbio_read_objid(void)
{
    return dbio_read_num();
}

const char *
dbio_read_string(void)
{
    static Stream *str = nullptr;
    static char buffer[1024];
    int len, used_stream = 0;

    if (str == nullptr)
	str = new_stream(1024);

  try_again:
    fgets(buffer, sizeof(buffer), input);
    len = strlen(buffer);
    if (len == sizeof(buffer) - 1 && buffer[len - 1] != '\n') {
	stream_add_string(str, buffer);
	used_stream = 1;
	goto try_again;
    }
    if (buffer[len - 1] == '\n')
	buffer[len - 1] = '\0';

    if (used_stream) {
	stream_add_string(str, buffer);
	return reset_stream(str);
    } else
	return buffer;
}

const char *
dbio_read_string_intern(void)
{
    const char *s, *r;

    s = dbio_read_string();
    r = str_intern(s);

    /* puts(r); */

    return r;
}

Var
dbio_read_var(void)
{
    int i, l = dbio_read_num();

    if (l == waif_conversion_type && waif_conversion_type != _TYPE_WAIF)
        return read_waif();

    Var r;
    if (l == (int) TYPE_ANY && dbio_input_version == DBV_Prehistory)
	l = TYPE_NONE;		/* Old encoding for VM's empty temp register
				 * and any as-yet unassigned variables.
				 */
    r.type = (var_type) l;


    switch (l) {
    case TYPE_CLEAR:
    case TYPE_NONE:
	break;
    case _TYPE_STR:
	r.v.str = dbio_read_string_intern();
	r.type = TYPE_STR;
	break;
    case TYPE_OBJ:
    case TYPE_ERR:
    case TYPE_INT:
    case TYPE_CATCH:
    case TYPE_FINALLY:
	r.v.num = dbio_read_num();
	break;
    case _TYPE_FLOAT:
    r.v.fnum = dbio_read_float();
	break;
    case _TYPE_MAP:
	l = dbio_read_num();
	r = new_map();
	for (i = 0; i < l; i++) {
	    Var key, value;
	    key = dbio_read_var();
	    value = dbio_read_var();
	    r = mapinsert(r, key, value);
	}
	break;
    case _TYPE_LIST:
	l = dbio_read_num();
	r = new_list(l);
	for (i = 0; i < l; i++)
	    r.v.list[i + 1] = dbio_read_var();
	break;
    case _TYPE_ITER:
	r = dbio_read_var();
	break;
    case _TYPE_ANON:
	r = db_read_anonymous();
	break;
    case _TYPE_WAIF:
    r = read_waif();
    break;
    default:
	errlog("DBIO_READ_VAR: Unknown type (%d) at DB file pos. %ld\n",
	       l, ftell(input));
	r = zero;
	break;
    }
    return r;
}

struct db_state {
    char prev_char;
    const char *(*fmtr) (void *);
    void *data;
};

static const char *
program_name(struct db_state *s)
{
    if (!s->fmtr)
	return (const char *)s->data;
    else
	return (*s->fmtr) (s->data);
}

static void
my_error(void *data, const char *msg)
{
    errlog("PARSER: Error in %s:\n", program_name((db_state *)data));
    errlog("           %s\n", msg);
}

static void
my_warning(void *data, const char *msg)
{
    oklog("PARSER: Warning in %s:\n", program_name((db_state *)data));
    oklog("           %s\n", msg);
}

static int
my_getc(void *data)
{
    struct db_state *s = (db_state *)data;
    int c;

    c = fgetc(input);
    if (c == '.' && s->prev_char == '\n') {
	/* end-of-verb marker in DB */
	c = fgetc(input);	/* skip next newline */
	return EOF;
    }
    if (c == EOF)
	my_error(data, "Unexpected EOF");
    s->prev_char = c;
    return c;
}

static Parser_Client parser_client =
{my_error, my_warning, my_getc};

Program *
dbio_read_program(DB_Version version, const char *(*fmtr) (void *), void *data)
{
    struct db_state s;

    s.prev_char = '\n';
    s.fmtr = fmtr;
    s.data = data;
    return parse_program(version, parser_client, &s);
}


/*********** Output ***********/

static FILE *output;

void
dbpriv_set_dbio_output(FILE * f)
{
    output = f;
}

void
dbio_printf(const char *format,...)
{
    va_list args;

    va_start(args, format);
    if (vfprintf(output, format, args) < 0)
	throw dbpriv_dbio_failed();
    va_end(args);
}

void
dbio_write_num(Num n)
{
    dbio_printf("%" PRIdN "\n", n);
}

void
dbio_write_float(double d)
{
    static const char *fmt = nullptr;
    static char buffer[10];

    if (!fmt) {
	sprintf(buffer, "%%.%dg\n", DBL_DIG + 4);
	fmt = buffer;
    }
    dbio_printf(fmt, d);
}

void
dbio_write_objid(Objid oid)
{
    dbio_write_num(oid);
}

void
dbio_write_string(const char *s)
{
    dbio_printf("%s\n", s ? s : "");
}

static int
dbio_write_map(Var key, Var value, void *data, int first)
{
    dbio_write_var(key);
    dbio_write_var(value);
    return 0;
}

void
dbio_write_var(Var v)
{
    int i;

    /* don't write out the iterator */
    if (v.type == TYPE_ITER) {
	var_pair pair;
	iterget(v, &pair)
	    ? dbio_write_var(pair.a)
	    : dbio_write_var(clear);
	return;
    }

    dbio_write_num((int) v.type & TYPE_DB_MASK);

    switch ((int) v.type) {
    case TYPE_CLEAR:
    case TYPE_NONE:
	break;
    case TYPE_STR:
	dbio_write_string(v.v.str);
	break;
    case TYPE_OBJ:
    case TYPE_ERR:
    case TYPE_INT:
    case TYPE_CATCH:
    case TYPE_FINALLY:
	dbio_write_num(v.v.num);
	break;
    case TYPE_FLOAT:
    dbio_write_float(v.v.fnum);
	break;
    case TYPE_MAP:
        dbio_write_num(maplength(v));
        mapforeach(v, dbio_write_map, nullptr);
        break;
    case TYPE_LIST:
	dbio_write_num(v.v.list[0].v.num);
	for (i = 0; i < v.v.list[0].v.num; i++)
	    dbio_write_var(v.v.list[i + 1]);
	break;
    case TYPE_ANON:
	db_write_anonymous(v);
	break;
    case TYPE_WAIF:
    write_waif(v);
    break;
    default:
	errlog("DBIO_WRITE_VAR: Unknown type (%d)\n", (int)v.type);
	break;
    }
}

static void
receiver(void *data, const char *line)
{
    dbio_printf("%s\n", line);
}

void
dbio_write_program(Program * program)
{
    unparse_program(program, receiver, nullptr, 1, 0, MAIN_VECTOR);
    dbio_printf(".\n");
}

void
dbio_write_forked_program(Program * program, int f_index)
{
    unparse_program(program, receiver, nullptr, 1, 0, f_index);
    dbio_printf(".\n");
}
