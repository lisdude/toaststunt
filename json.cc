/******************************************************************************
  Copyright 2010 Todd Sundsted. All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

   1. Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.

   2. Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY TODD SUNDSTED ``AS IS'' AND ANY EXPRESS OR
  IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
  EVENT SHALL TODD SUNDSTED OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
  OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
  EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

  The views and conclusions contained in the software and documentation are
  those of the authors and should not be interpreted as representing official
  policies, either expressed or implied, of Todd Sundsted.
 *****************************************************************************/

#include <stdio.h>
#include <errno.h>

#include "my-string.h"
#include "my-stdlib.h"

#include "functions.h"
#include "json.h"
#include "list.h"
#include "map.h"
#include "numbers.h"
#include "server.h"
#include "storage.h"
#include "streams.h"
#include "unparse.h"
#include "utils.h"
#include "yajl/yajl_gen.h"
#include "yajl/yajl_lex.h"
#include "yajl/yajl_parse.h"

/*
  Handle many modes of mapping between JSON and internal MOO types.

  JSON defines types that MOO (currently) does not support, such as
  boolean true and false, and null.  MOO uses types that JSON does not
  support, such as object references and errors.

  Mode 0 -- Common Subset

  In Mode 0, only the common subset of types (strings and numbers) are
  translated with fidelity between MOO types and JSON types.  All
  other types are treated as alternative representations of the string
  type.  Furthermore, all MOO types used as keys (_both_ strings and
  numbers) are treated as strings.  Neither MOO lists nor maps may be
  used as keys in this mode.

  Mode 0 is useful for data transfer with non-MOO applications.

  Mode 1 -- Embedded Types

  In Mode 1, MOO types are encoded as strings, suffixed with type
  information.  Strings and numbers, which are valid JSON types, carry
  implicit type information, if type information is not specified.
  The boolean values and null are still interpreted as short-hand for
  explicit string notation.  Neither MOO lists nor maps may be used as
  keys in this mode.

  Mode 1 is useful for serializing/deserializing MOO types.
 */

struct stack_item {
    struct stack_item *prev;
    Var v;
};

static void
push(struct stack_item **top, Var v)
{
    struct stack_item *item = (struct stack_item *)malloc(sizeof(struct stack_item));
    item->prev = *top;
    item->v = v;
    *top = item;
}

static Var
pop(struct stack_item **top)
{
    struct stack_item *item = *top;
    *top = item->prev;
    Var v = item->v;
    free(item);
    return v;
}

#define PUSH(top, v) push(&(top), v)

#define POP(top) pop(&(top))

typedef enum {
    MODE_COMMON_SUBSET, MODE_EMBEDDED_TYPES
} mode_type;

struct parse_context {
    struct stack_item stack;
    struct stack_item *top;
    mode_type mode;
};

struct generate_context {
    mode_type mode;
};

#define ARRAY_SENTINEL -1
#define MAP_SENTINEL -2

static const char *
value_to_literal(Var v)
{
    static Stream *s = NULL;
    if (!s)
	s = new_stream(100);
    unparse_value(s, v);
    return reset_stream(s);
}

/* If type information is present, extract it and return the type. */
static var_type
valid_type(const char **val, size_t *len)
{
    /* format: "...|<TYPE>"
       where <TYPE> is a MOO type string: "obj", "int", "float", "err", "str" */
    if (*len > 3 && !strncmp(*val + *len - 4, "|obj", 4)) {
	*len = *len - 4;
	return TYPE_OBJ;
    } else if (*len > 3 && !strncmp(*val + *len - 4, "|int", 4)) {
	*len = *len - 4;
	return TYPE_INT;
    } else if (*len > 5 && !strncmp(*val + *len - 6, "|float", 6)) {
	*len = *len - 6;
	return TYPE_FLOAT;
    } else if (*len > 3 && !strncmp(*val + *len - 4, "|err", 4)) {
	*len = *len - 4;
	return TYPE_ERR;
    } else if (*len > 3 && !strncmp(*val + *len - 4, "|str", 4)) {
	*len = *len - 4;
	return TYPE_STR;
    } else
	return TYPE_NONE;
}

/* Append type information. */
static const char *
append_type(const char *str, var_type type)
{
    static Stream *stream = NULL;
    if (NULL == stream)
	stream = new_stream(20);
    stream_add_string(stream, str);
    switch (type) {
    case TYPE_OBJ:
	stream_add_string(stream, "|obj");
	break;
    case TYPE_INT:
	stream_add_string(stream, "|int");
	break;
    case TYPE_FLOAT:
	stream_add_string(stream, "|float");
	break;
    case TYPE_ERR:
	stream_add_string(stream, "|err");
	break;
    case TYPE_STR:
	stream_add_string(stream, "|str");
	break;
    default:
	panic("Unsupported type in append_type()");
    }
    return reset_stream(stream);
}

static int
handle_null(void *ctx)
{
    struct parse_context *pctx = (struct parse_context *)ctx;
    Var v;
    v.type = TYPE_STR;
    v.v.str = str_dup("null");
    PUSH(pctx->top, v);
    return 1;
}

static int
handle_boolean(void *ctx, int boolean)
{
    struct parse_context *pctx = (struct parse_context *)ctx;
    Var v;
    v.type = TYPE_STR;
    v.v.str = str_dup(boolean ? "true" : "false");
    PUSH(pctx->top, v);
    return 1;
}

static int
handle_number(void *ctx, const char *numberVal, unsigned int numberLen, yajl_tok tok)
{
    struct parse_context *pctx = (struct parse_context *)ctx;
    Var v;

    if (yajl_tok_integer == tok) {

	long int i = 0;

	errno = 0;
	i = strtol(numberVal, NULL, 10);

	if (0 == errno && (i >= MININT && i <= MAXINT)) {
	    v = Var::new_int(i);
	    PUSH(pctx->top, v);
	    return 1;
        }
    }

    double d = 0.0;

    errno = 0;
    d = strtod(numberVal, NULL);

    if (0 == errno) {
        v.type = TYPE_FLOAT;
        v.v.fnum = d;
	PUSH(pctx->top, v);
	return 1;
    }

    return 0;
}

static int
handle_string(void *ctx, const unsigned char *stringVal, unsigned int stringLen)
{
    struct parse_context *pctx = (struct parse_context *)ctx;
    var_type type;
    Var v;

    const char *val = (const char *)stringVal;
    size_t len = (size_t)stringLen;

    if (MODE_EMBEDDED_TYPES == pctx->mode
	&& TYPE_NONE != (type = valid_type(&val, &len))) {
	switch (type) {
	case TYPE_OBJ:
	    {
		char *p;
		if (*val == '#')
		    val++;
		v.type = TYPE_OBJ;
		v.v.num = strtol(val, &p, 10);
		break;
	    }
	case TYPE_INT:
	    {
		char *p;
		v = Var::new_int(strtol(val, &p, 10));
		break;
	    }
	case TYPE_FLOAT:
	    {
		char *p;
        v.type = TYPE_FLOAT;
		v.v.fnum = strtod(val, &p);
		break;
	    }
	case TYPE_ERR:
	    {
		char temp[len + 1];
		strncpy(temp, val, len);
		temp[len] = '\0';
		v.type = TYPE_ERR;
		int err = parse_error(temp);
		v.v.err = err > -1 ? (error)err : E_NONE;
		break;
	    }
	case TYPE_STR:
	    {
		char temp[len + 1];
		strncpy(temp, val, len);
		temp[len] = '\0';
		v.type = TYPE_STR;
		v.v.str = str_dup(temp);
		break;
	    }
	default:
	    panic("Unsupported type in handle_string()");
	}
    } else {
	char temp[len + 1];
	strncpy(temp, val, len);
	temp[len] = '\0';
	v.type = TYPE_STR;
	v.v.str = str_dup(temp);
    }

    PUSH(pctx->top, v);
    return 1;
}

static int
handle_start_map(void *ctx)
{
    struct parse_context *pctx = (struct parse_context *)ctx;
    Var k, v;
    k.type = (var_type)MAP_SENTINEL;
    PUSH(pctx->top, k);
    v.type = (var_type)MAP_SENTINEL;
    PUSH(pctx->top, v);
    return 1;
}

static int
handle_end_map(void *ctx)
{
    struct parse_context *pctx = (struct parse_context *)ctx;
    Var map = new_map();
    Var k, v;
    for (v = POP(pctx->top), k = POP(pctx->top);
	 (int)v.type > MAP_SENTINEL && (int)k.type > MAP_SENTINEL;
	 v = POP(pctx->top), k = POP(pctx->top)) {
	map = mapinsert(map, k, v);
    }
    PUSH(pctx->top, map);
    return 1;
}

static int
handle_start_array(void *ctx)
{
    struct parse_context *pctx = (struct parse_context *)ctx;
    Var v;
    v.type = (var_type)ARRAY_SENTINEL;
    PUSH(pctx->top, v);
    return 1;
}

static int
handle_end_array(void *ctx)
{
    struct parse_context *pctx = (struct parse_context *)ctx;
    Var list = new_list(0);
    Var v;
    for (v = POP(pctx->top); (int)v.type > ARRAY_SENTINEL;
	 v = POP(pctx->top)) {
	list = listinsert(list, v, 1);
    }
    PUSH(pctx->top, list);
    return 1;
}

static yajl_gen_status
generate_key(yajl_gen g, Var v, void *ctx)
{
    struct generate_context *gctx = (struct generate_context *)ctx;

    switch (v.type) {
    case TYPE_OBJ:
    case TYPE_INT:
    case TYPE_FLOAT:
    case TYPE_ERR:
	{
	    const char *tmp = value_to_literal(v);
	    if (MODE_EMBEDDED_TYPES == gctx->mode)
		tmp = append_type(tmp, v.type);
	    return yajl_gen_string(g, (const unsigned char *)tmp, strlen(tmp));
	}
    case TYPE_STR:
	{
	    const char *tmp = v.v.str;
	    size_t len = strlen(tmp);
	    if (MODE_EMBEDDED_TYPES == gctx->mode)
		if (TYPE_NONE != valid_type(&tmp, &len))
		    tmp = append_type(tmp, v.type);
	    return yajl_gen_string(g, (const unsigned char *)tmp, strlen(tmp));
	}
    case TYPE_ANON:
    case TYPE_WAIF:
    break;
    default:
	panic("Unsupported type in generate_key()");
    }

    return yajl_gen_keys_must_be_strings;
}

static yajl_gen_status
generate(yajl_gen g, Var v, void *ctx);

struct do_closure {
    yajl_gen g;
    struct generate_context *gctx;
    yajl_gen_status status;
};

static int
do_map(Var key, Var value, void *data, int first)
{
    struct do_closure *dmc = (struct do_closure *)data;

    dmc->status = generate_key(dmc->g, key, dmc->gctx);
    if (yajl_gen_status_ok != dmc->status)
	return 1;
    dmc->status = generate(dmc->g, value, dmc->gctx);
    if (yajl_gen_status_ok != dmc->status)
	return 1;

    return 0;
}

static int
do_list(Var value, void *data, int first)
{
    struct do_closure *dmc = (struct do_closure *)data;

    dmc->status = generate(dmc->g, value, dmc->gctx);
    if (yajl_gen_status_ok != dmc->status)
	return 1;

    return 0;
}

static yajl_gen_status
generate(yajl_gen g, Var v, void *ctx)
{
    struct generate_context *gctx = (struct generate_context *)ctx;

    switch (v.type) {
    case TYPE_INT:
	return yajl_gen_integer(g, v.v.num);
    case TYPE_FLOAT:
	return yajl_gen_double(g, v.v.fnum);
    case TYPE_OBJ:
    case TYPE_ERR:
	{
	    const char *tmp = value_to_literal(v);
	    if (MODE_EMBEDDED_TYPES == gctx->mode)
		tmp = append_type(tmp, v.type);
	    return yajl_gen_string(g, (const unsigned char *)tmp, strlen(tmp));
	}
    case TYPE_STR:
	{
	    const char *tmp = v.v.str;
	    size_t len = strlen(tmp);
	    if (MODE_EMBEDDED_TYPES == gctx->mode)
		if (TYPE_NONE != valid_type(&tmp, &len))
		    tmp = append_type(tmp, v.type);
	    return yajl_gen_string(g, (const unsigned char *)tmp, strlen(tmp));
	}
    case TYPE_MAP:
	{
	    struct do_closure dmc;
	    dmc.g = g;
	    dmc.gctx = gctx;
	    dmc.status = yajl_gen_status_ok;
	    yajl_gen_map_open(g);
	    if (mapforeach(v, do_map, &dmc))
		return dmc.status;
	    yajl_gen_map_close(g);
	    return yajl_gen_status_ok;
	}
    case TYPE_LIST:
	{
	    struct do_closure dmc;
	    dmc.g = g;
	    dmc.gctx = gctx;
	    dmc.status = yajl_gen_status_ok;
	    yajl_gen_array_open(g);
	    if (listforeach(v, do_list, &dmc))
		return dmc.status;
	    yajl_gen_array_close(g);
	    return yajl_gen_status_ok;
	}
    }

    return (yajl_gen_status)-1;
}

static yajl_callbacks callbacks = {
    handle_null,
    handle_boolean,
    NULL,
    NULL,
    handle_number,
    handle_string,
    handle_start_map,
    handle_string,
    handle_end_map,
    handle_start_array,
    handle_end_array
};

/**** built in functions ****/

static package
bf_parse_json(Var arglist, Byte next, void *vdata, Objid progr)
{
    yajl_handle hand;
    yajl_parser_config cfg = { 1, 1 };
    yajl_status stat;

    struct parse_context pctx;
    pctx.top = &pctx.stack;
    pctx.stack.v.type = TYPE_INT;
    pctx.stack.v.v.num = 0;
    pctx.mode = MODE_COMMON_SUBSET;

    const char *str = arglist.v.list[1].v.str;
    size_t len = strlen(str);

    package pack;

    int done = 0;

    if (1 < arglist.v.list[0].v.num) {
	if (!strcasecmp(arglist.v.list[2].v.str, "common-subset")) {
	    pctx.mode = MODE_COMMON_SUBSET;
	} else if (!strcasecmp(arglist.v.list[2].v.str, "embedded-types")) {
	    pctx.mode = MODE_EMBEDDED_TYPES;
	} else {
	    free_var(arglist);
	    return make_error_pack(E_INVARG);
	}
    }

    hand = yajl_alloc(&callbacks, &cfg, NULL, (void *)&pctx);

    while (!done) {
	if (len == 0)
	    done = 1;

	if (done)
	    stat = yajl_parse_complete(hand);
	else
	    stat = yajl_parse(hand, (const unsigned char *)str, len);

	len = 0;

	if (done) {
	    if (stat != yajl_status_ok) {
		/* clean up the stack */
		while (pctx.top != &pctx.stack) {
		    Var v = POP(pctx.top);
		    free_var(v);
		}
		pack = make_error_pack(E_INVARG);
	    } else {
		Var v = POP(pctx.top);
		pack = make_var_pack(v);
	    }
	}
    }

    yajl_free(hand);

    free_var(arglist);
    return pack;
}

static package
bf_generate_json(Var arglist, Byte next, void *vdata, Objid progr)
{
    yajl_gen g;
    yajl_gen_config cfg = { 0, "" };

    struct generate_context gctx;
    gctx.mode = MODE_COMMON_SUBSET;

    const char *buf;
    unsigned int len;

    Var json;

    package pack;

    if (1 < arglist.v.list[0].v.num) {
	if (!strcasecmp(arglist.v.list[2].v.str, "common-subset")) {
	    gctx.mode = MODE_COMMON_SUBSET;
	} else if (!strcasecmp(arglist.v.list[2].v.str, "embedded-types")) {
	    gctx.mode = MODE_EMBEDDED_TYPES;
	} else {
	    free_var(arglist);
	    return make_error_pack(E_INVARG);
	}
    }

    g = yajl_gen_alloc(&cfg, NULL);

    if (yajl_gen_status_ok == generate(g, arglist.v.list[1], &gctx)) {
	yajl_gen_get_buf(g, (const unsigned char **)&buf, &len);

	json.type = TYPE_STR;
	json.v.str = str_dup(buf);

	pack = make_var_pack(json);
    } else {
	pack = make_error_pack(E_INVARG);
    }

    yajl_gen_clear(g);
    yajl_gen_free(g);

    free_var(arglist);
    return pack;
}

void
register_yajl(void)
{
    register_function("parse_json", 1, 2, bf_parse_json, TYPE_STR, TYPE_STR);
    register_function("generate_json", 1, 2, bf_generate_json, TYPE_ANY, TYPE_STR);
}
