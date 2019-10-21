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

#include <ctype.h>
#include <stdio.h>

#include "ast.h"
#include "config.h"
#include "decompile.h"
#include "functions.h"
#include "keywords.h"
#include "list.h"
#include "log.h"
#include "opcode.h"
#include "program.h"
#include "unparse.h"
#include "storage.h"
#include "streams.h"
#include "utils.h"

static Program *prog;

const char *
unparse_error(enum error e)
{
    switch (e) {
    case E_NONE:
	return "No error";
    case E_TYPE:
	return "Type mismatch";
    case E_DIV:
	return "Division by zero";
    case E_PERM:
	return "Permission denied";
    case E_PROPNF:
	return "Property not found";
    case E_VERBNF:
	return "Verb not found";
    case E_VARNF:
	return "Variable not found";
    case E_INVIND:
	return "Invalid indirection";
    case E_RECMOVE:
	return "Recursive move";
    case E_MAXREC:
	return "Too many verb calls";
    case E_RANGE:
	return "Range error";
    case E_ARGS:
	return "Incorrect number of arguments";
    case E_NACC:
	return "Move refused by destination";
    case E_INVARG:
	return "Invalid argument";
    case E_QUOTA:
	return "Resource limit exceeded";
    case E_FLOAT:
	return "Floating-point arithmetic error";
    case E_FILE:
	return "File error";
    case E_EXEC:
	return "Exec error";
    case E_INTRPT:
	return "Interrupted";
    }

    return "Unknown Error";
}

const char *
error_name(enum error e)
{
    switch (e) {
    case E_NONE:
	return "E_NONE";
    case E_TYPE:
	return "E_TYPE";
    case E_DIV:
	return "E_DIV";
    case E_PERM:
	return "E_PERM";
    case E_PROPNF:
	return "E_PROPNF";
    case E_VERBNF:
	return "E_VERBNF";
    case E_VARNF:
	return "E_VARNF";
    case E_INVIND:
	return "E_INVIND";
    case E_RECMOVE:
	return "E_RECMOVE";
    case E_MAXREC:
	return "E_MAXREC";
    case E_RANGE:
	return "E_RANGE";
    case E_ARGS:
	return "E_ARGS";
    case E_NACC:
	return "E_NACC";
    case E_INVARG:
	return "E_INVARG";
    case E_QUOTA:
	return "E_QUOTA";
    case E_FLOAT:
	return "E_FLOAT";
    case E_FILE:
	return "E_FILE";
    case E_EXEC:
	return "E_EXEC";
    case E_INTRPT:
	return "E_INTRPT";
    }

    return "E_?";
}

/*
  This probably doesn't belong here, but it keeps the code that
  parses/unparses errors in one place, which makes changes easier.
 */
int
parse_error(const char *e)
{
    if (!strcasecmp("E_NONE", e))
	return E_NONE;
    if (!strcasecmp("E_TYPE", e))
	return E_TYPE;
    if (!strcasecmp("E_DIV", e))
	return E_DIV;
    if (!strcasecmp("E_PERM", e))
	return E_PERM;
    if (!strcasecmp("E_PROPNF", e))
	return E_PROPNF;
    if (!strcasecmp("E_VERBNF", e))
	return E_VERBNF;
    if (!strcasecmp("E_VARNF", e))
	return E_VARNF;
    if (!strcasecmp("E_INVIND", e))
	return E_INVIND;
    if (!strcasecmp("E_RECMOVE", e))
	return E_RECMOVE;
    if (!strcasecmp("E_MAXREC", e))
	return E_MAXREC;
    if (!strcasecmp("E_RANGE", e))
	return E_RANGE;
    if (!strcasecmp("E_ARGS", e))
	return E_ARGS;
    if (!strcasecmp("E_NACC", e))
	return E_NACC;
    if (!strcasecmp("E_INVARG", e))
	return E_INVARG;
    if (!strcasecmp("E_QUOTA", e))
	return E_QUOTA;
    if (!strcasecmp("E_FLOAT", e))
	return E_FLOAT;
    if (!strcasecmp("E_FILE", e))
	return E_FILE;
    if (!strcasecmp("E_EXEC", e))
	return E_EXEC;
    if (!strcasecmp("E_INTRPT", e))
	return E_INTRPT;

    return -1;
}

struct prec {
    enum Expr_Kind kind;
    int precedence;
};

static struct prec prec_table[] =
{
    {EXPR_ASGN, 1},

    {EXPR_COND, 2},		/* the unparser for this depends on only ASGN having
				   lower precedence.  Fix that if this changes. */
    {EXPR_OR, 3},
    {EXPR_AND, 3},

    {EXPR_EQ, 4},
    {EXPR_NE, 4},
    {EXPR_LT, 4},
    {EXPR_LE, 4},
    {EXPR_GT, 4},
    {EXPR_GE, 4},
    {EXPR_IN, 4},

    {EXPR_BITOR, 5},
    {EXPR_BITAND, 5},
    {EXPR_BITXOR, 5},

    {EXPR_BITSHL, 6},
    {EXPR_BITSHR, 6},

    {EXPR_PLUS, 7},
    {EXPR_MINUS, 7},

    {EXPR_TIMES, 8},
    {EXPR_DIVIDE, 8},
    {EXPR_MOD, 8},

    {EXPR_EXP, 9},

    {EXPR_NEGATE, 10},
    {EXPR_COMPLEMENT, 10},
    {EXPR_NOT, 10},

    {EXPR_PROP, 11},
    {EXPR_VERB, 11},
    {EXPR_INDEX, 11},
    {EXPR_RANGE, 11},

    {EXPR_VAR, 12},
    {EXPR_ID, 12},
    {EXPR_LIST, 12},
    {EXPR_CALL, 12},
    {EXPR_FIRST, 12},
    {EXPR_LAST, 12},
    {EXPR_CATCH, 12}
};

static int expr_prec[SizeOf_Expr_Kind];

struct binop {
    enum Expr_Kind kind;
    const char *string;
};

static struct binop binop_table[] =
{
    {EXPR_IN, " in "},
    {EXPR_OR, " || "},
    {EXPR_AND, " && "},
    {EXPR_EQ, " == "},
    {EXPR_NE, " != "},
    {EXPR_LT, " < "},
    {EXPR_LE, " <= "},
    {EXPR_GT, " > "},
    {EXPR_GE, " >= "},
    {EXPR_PLUS, " + "},
    {EXPR_MINUS, " - "},
    {EXPR_TIMES, " * "},
    {EXPR_DIVIDE, " / "},
    {EXPR_MOD, " % "},
    {EXPR_EXP, " ^ "},
    {EXPR_BITOR, " |. "},
    {EXPR_BITAND, " &. "},
    {EXPR_BITXOR, " ^. "},
    {EXPR_BITSHL, " << "},
    {EXPR_BITSHR, " >> "}
};

static const char *binop_string[SizeOf_Expr_Kind];

static int expr_tables_initialized = 0;

static void
init_expr_tables()
{
    unsigned int i;

    for (i = 0; i < Arraysize(prec_table); i++)
	expr_prec[prec_table[i].kind] = prec_table[i].precedence;

    for (i = 0; i < Arraysize(binop_table); i++)
	binop_string[binop_table[i].kind] = binop_table[i].string;

    expr_tables_initialized = 1;
}

/********** globals *********************************/

static Unparser_Receiver receiver;
static void *receiver_data;
static int fully_parenthesize, indent_code;

/********** AST to receiver procedures **************/

static void unparse_stmt(Stmt * s, int indent);
static void unparse_expr(Stream * str, Expr * e);
static void unparse_maplist(Stream *str, Map_List *map);
static void unparse_arglist(Stream * str, Arg_List * a);
static void unparse_scatter(Stream * str, Scatter * sc);

static void
list_prg(Stmt * program, int p, int i)
{
    fully_parenthesize = p;
    indent_code = i;
    if (!expr_tables_initialized)
	init_expr_tables();
    unparse_stmt(program, 0);
}

static void
bracket_lt(Stream * str, enum Expr_Kind parent, Expr * child)
{
    if ((fully_parenthesize && expr_prec[child->kind] < expr_prec[EXPR_PROP])
	|| expr_prec[parent] > expr_prec[child->kind]) {
	stream_add_char(str, '(');
	unparse_expr(str, child);
	stream_add_char(str, ')');
    } else {
	unparse_expr(str, child);
    }
}

static void
bracket_le(Stream * str, enum Expr_Kind parent, Expr * child)
{
    if ((fully_parenthesize && expr_prec[child->kind] < expr_prec[EXPR_PROP])
	|| expr_prec[parent] >= expr_prec[child->kind]) {
	stream_add_char(str, '(');
	unparse_expr(str, child);
	stream_add_char(str, ')');
    } else {
	unparse_expr(str, child);
    }
}

static void
output(Stream * str)
{
    (*receiver) (receiver_data, reset_stream(str));
}

static void
indent_stmt(Stream * str, int indent)
{
    int i;

    if (indent_code)
	for (i = 0; i < indent; i++)
	    stream_add_char(str, ' ');
}

static void
unparse_stmt_cond(Stream * str, struct Stmt_Cond cond, int indent)
{
    Cond_Arm *elseifs;

    stream_add_string(str, "if (");
    unparse_expr(str, cond.arms->condition);
    stream_add_char(str, ')');
    output(str);
    unparse_stmt(cond.arms->stmt, indent + 2);
    for (elseifs = cond.arms->next; elseifs; elseifs = elseifs->next) {
	indent_stmt(str, indent);
	stream_add_string(str, "elseif (");
	unparse_expr(str, elseifs->condition);
	stream_add_char(str, ')');
	output(str);
	unparse_stmt(elseifs->stmt, indent + 2);
    }
    if (cond.otherwise) {
	indent_stmt(str, indent);
	stream_add_string(str, "else");
	output(str);
	unparse_stmt(cond.otherwise, indent + 2);
    }
    indent_stmt(str, indent);
    stream_add_string(str, "endif");
    output(str);
}

static void
unparse_stmt_list(Stream * str, struct Stmt_List list, int indent)
{
    if (list.index > -1)
	stream_printf(str, "for %s, %s in (", prog->var_names[list.id], prog->var_names[list.index]);
    else
	stream_printf(str, "for %s in (", prog->var_names[list.id]);
    unparse_expr(str, list.expr);
    stream_add_char(str, ')');
    output(str);
    unparse_stmt(list.body, indent + 2);
    indent_stmt(str, indent);
    stream_add_string(str, "endfor");
    output(str);
}

static void
unparse_stmt_range(Stream * str, struct Stmt_Range range, int indent)
{
    stream_printf(str, "for %s in [", prog->var_names[range.id]);
    unparse_expr(str, range.from);
    stream_add_string(str, "..");
    unparse_expr(str, range.to);
    stream_add_char(str, ']');
    output(str);
    unparse_stmt(range.body, indent + 2);
    indent_stmt(str, indent);
    stream_add_string(str, "endfor");
    output(str);
}

static void
unparse_stmt_fork(Stream * str, struct Stmt_Fork fork_stmt, int indent)
{
    if (fork_stmt.id >= 0)
	stream_printf(str, "fork %s (", prog->var_names[fork_stmt.id]);
    else
	stream_add_string(str, "fork (");
    unparse_expr(str, fork_stmt.time);
    stream_add_char(str, ')');
    output(str);
    unparse_stmt(fork_stmt.body, indent + 2);
    indent_stmt(str, indent);
    stream_add_string(str, "endfork");
    output(str);
}

static void
unparse_stmt_catch(Stream * str, struct Stmt_Catch _catch, int indent)
{
    Except_Arm *ex;

    stream_add_string(str, "try");
    output(str);
    unparse_stmt(_catch.body, indent + 2);
    for (ex = _catch.excepts; ex; ex = ex->next) {
	indent_stmt(str, indent);
	stream_add_string(str, "except ");
	if (ex->id >= 0)
	    stream_printf(str, "%s ", prog->var_names[ex->id]);
	stream_add_char(str, '(');
	if (ex->codes)
	    unparse_arglist(str, ex->codes);
	else
	    stream_add_string(str, "ANY");
	stream_add_char(str, ')');
	output(str);
	unparse_stmt(ex->stmt, indent + 2);
    }
    indent_stmt(str, indent);
    stream_add_string(str, "endtry");
    output(str);
}

static void
unparse_stmt(Stmt * stmt, int indent)
{
    Stream *str = new_stream(100);

    while (stmt) {
	indent_stmt(str, indent);
	switch (stmt->kind) {
	case STMT_COND:
	    unparse_stmt_cond(str, stmt->s.cond, indent);
	    break;
	case STMT_LIST:
	    unparse_stmt_list(str, stmt->s.list, indent);
	    break;
	case STMT_RANGE:
	    unparse_stmt_range(str, stmt->s.range, indent);
	    break;
	case STMT_FORK:
	    unparse_stmt_fork(str, stmt->s.fork, indent);
	    break;
	case STMT_EXPR:
	    unparse_expr(str, stmt->s.expr);
	    stream_add_char(str, ';');
	    output(str);
	    break;
	case STMT_WHILE:
	    if (stmt->s.loop.id == -1)
		stream_add_string(str, "while (");
	    else
		stream_printf(str, "while %s (",
			      prog->var_names[stmt->s.loop.id]);
	    unparse_expr(str, stmt->s.loop.condition);
	    stream_add_char(str, ')');
	    output(str);
	    unparse_stmt(stmt->s.loop.body, indent + 2);
	    indent_stmt(str, indent);
	    stream_add_string(str, "endwhile");
	    output(str);
	    break;
	case STMT_RETURN:
	    if (stmt->s.expr) {
		stream_add_string(str, "return ");
		unparse_expr(str, stmt->s.expr);
	    } else
		stream_add_string(str, "return");
	    stream_add_char(str, ';');
	    output(str);
	    break;
	case STMT_TRY_EXCEPT:
	    unparse_stmt_catch(str, stmt->s._catch, indent);
	    break;
	case STMT_TRY_FINALLY:
	    stream_add_string(str, "try");
	    output(str);
	    unparse_stmt(stmt->s.finally.body, indent + 2);
	    indent_stmt(str, indent);
	    stream_add_string(str, "finally");
	    output(str);
	    unparse_stmt(stmt->s.finally.handler, indent + 2);
	    indent_stmt(str, indent);
	    stream_add_string(str, "endtry");
	    output(str);
	    break;
	case STMT_BREAK:
	case STMT_CONTINUE:
	    {
		const char *kwd = (stmt->kind == STMT_BREAK ? "break"
				   : "continue");

		if (stmt->s.exit == -1)
		    stream_printf(str, "%s;", kwd);
		else
		    stream_printf(str, "%s %s;", kwd,
				  prog->var_names[stmt->s.exit]);
		output(str);
	    }
	    break;
	default:
	    errlog("UNPARSE_STMT: Unknown Stmt_Kind: %d\n", stmt->kind);
	    stream_add_string(str, "?!?!?!?;");
	    output(str);
	    break;
	}
	stmt = stmt->next;
    }

    free_stream(str);
}

static int
ok_identifier(const char *name)
{
    const char *p = name;

    if (*p != '\0' && (isalpha(*p) || *p == '_')) {
	while (*++p != '\0' && (isalnum(*p) || *p == '_'));
	if (*p == '\0' && !find_keyword(name))
	    return 1;
    }
    return 0;
}

static void
unparse_name_expr(Stream * str, Expr * expr)
{
    /*
     * Handle the right-hand expression in EXPR_PROP and EXPR_VERB.
     * If it's a simple string literal with the syntax of an identifier,
     * just print the name.  Otherwise, use parens and unparse the
     * expression normally.
     */

    if (expr->kind == EXPR_VAR && expr->e.var.type == TYPE_STR
	&& ok_identifier(expr->e.var.v.str)) {
	stream_add_string(str, expr->e.var.v.str);
	return;
    }
    /* We need to use the full unparser */
    stream_add_char(str, '(');
    unparse_expr(str, expr);
    stream_add_char(str, ')');
}

static void
unparse_expr(Stream * str, Expr * expr)
{
    switch (expr->kind) {
    case EXPR_PROP:
	if (expr->e.bin.lhs->kind == EXPR_VAR
	    && expr->e.bin.lhs->e.var.type == TYPE_OBJ
	    && expr->e.bin.lhs->e.var.v.obj == 0
	    && expr->e.bin.rhs->kind == EXPR_VAR
	    && expr->e.bin.rhs->e.var.type == TYPE_STR
	    && ok_identifier(expr->e.bin.rhs->e.var.v.str)) {
	    stream_add_char(str, '$');
	    stream_add_string(str, expr->e.bin.rhs->e.var.v.str);
	} else {
	    bracket_lt(str, EXPR_PROP, expr->e.bin.lhs);
	    if (expr->e.bin.lhs->kind == EXPR_VAR
		&& expr->e.bin.lhs->e.var.type == TYPE_INT)
		/* avoid parsing digits followed by dot as floating-point */
		stream_add_char(str, ' ');
	    stream_add_char(str, '.');
	    unparse_name_expr(str, expr->e.bin.rhs);
	}
	break;

    case EXPR_VERB:
	if (expr->e.verb.obj->kind == EXPR_VAR
	    && expr->e.verb.obj->e.var.type == TYPE_OBJ
	    && expr->e.verb.obj->e.var.v.obj == 0
	    && expr->e.verb.verb->kind == EXPR_VAR
	    && expr->e.verb.verb->e.var.type == TYPE_STR
	    && ok_identifier(expr->e.verb.verb->e.var.v.str)) {
	    stream_add_char(str, '$');
	    stream_add_string(str, expr->e.verb.verb->e.var.v.str);
	} else {
	    bracket_lt(str, EXPR_VERB, expr->e.verb.obj);
	    stream_add_char(str, ':');
	    unparse_name_expr(str, expr->e.verb.verb);
	}
	stream_add_char(str, '(');
	unparse_arglist(str, expr->e.verb.args);
	stream_add_char(str, ')');
	break;

    case EXPR_INDEX:
	bracket_lt(str, EXPR_INDEX, expr->e.bin.lhs);
	stream_add_char(str, '[');
	unparse_expr(str, expr->e.bin.rhs);
	stream_add_char(str, ']');
	break;

    case EXPR_RANGE:
	bracket_lt(str, EXPR_RANGE, expr->e.range.base);
	stream_add_char(str, '[');
	unparse_expr(str, expr->e.range.from);
	stream_add_string(str, "..");
	unparse_expr(str, expr->e.range.to);
	stream_add_char(str, ']');
	break;

	/* left-associative binary operators */
    case EXPR_PLUS:
    case EXPR_MINUS:
    case EXPR_TIMES:
    case EXPR_DIVIDE:
    case EXPR_MOD:
    case EXPR_AND:
    case EXPR_OR:
    case EXPR_EQ:
    case EXPR_NE:
    case EXPR_LT:
    case EXPR_GT:
    case EXPR_LE:
    case EXPR_GE:
    case EXPR_IN:
    case EXPR_BITOR:
    case EXPR_BITAND:
    case EXPR_BITXOR:
    case EXPR_BITSHL:
    case EXPR_BITSHR:
	bracket_lt(str, expr->kind, expr->e.bin.lhs);
	stream_add_string(str, binop_string[expr->kind]);
	bracket_le(str, expr->kind, expr->e.bin.rhs);
	break;

	/* right-associative binary operators */
    case EXPR_EXP:
	bracket_le(str, expr->kind, expr->e.bin.lhs);
	stream_add_string(str, binop_string[expr->kind]);
	bracket_lt(str, expr->kind, expr->e.bin.rhs);
	break;

    case EXPR_COND:
	bracket_le(str, EXPR_COND, expr->e.cond.condition);
	stream_add_string(str, " ? ");
	unparse_expr(str, expr->e.cond.consequent);
	stream_add_string(str, " | ");
	bracket_le(str, EXPR_COND, expr->e.cond.alternate);
	break;

    case EXPR_NEGATE:
	stream_add_char(str, '-');
	bracket_lt(str, EXPR_NEGATE, expr->e.expr);
	break;

    case EXPR_NOT:
	stream_add_char(str, '!');
	bracket_lt(str, EXPR_NOT, expr->e.expr);
	break;

    case EXPR_COMPLEMENT:
	stream_add_char(str, '~');
	bracket_lt(str, EXPR_COMPLEMENT, expr->e.expr);
	break;

    case EXPR_VAR:
	unparse_value(str, expr->e.var);
	break;

    case EXPR_ASGN:
	unparse_expr(str, expr->e.bin.lhs);
	stream_add_string(str, " = ");
	unparse_expr(str, expr->e.bin.rhs);
	break;

    case EXPR_CALL:
	stream_add_string(str, name_func_by_num(expr->e.call.func));
	stream_add_char(str, '(');
	unparse_arglist(str, expr->e.call.args);
	stream_add_char(str, ')');
	break;

    case EXPR_ID:
	stream_add_string(str, prog->var_names[expr->e.id]);
	break;

    case EXPR_LIST:
	stream_add_char(str, '{');
	unparse_arglist(str, expr->e.list);
	stream_add_char(str, '}');
	break;

    case EXPR_MAP:
	stream_add_char(str, '[');
	unparse_maplist(str, expr->e.map);
	stream_add_char(str, ']');
	break;

    case EXPR_SCATTER:
	stream_add_char(str, '{');
	unparse_scatter(str, expr->e.scatter);
	stream_add_char(str, '}');
	break;

    case EXPR_CATCH:
	stream_add_string(str, "`");
	unparse_expr(str, expr->e._catch._try);
	stream_add_string(str, " ! ");
	if (expr->e._catch.codes)
	    unparse_arglist(str, expr->e._catch.codes);
	else
	    stream_add_string(str, "ANY");
	if (expr->e._catch.except) {
	    stream_add_string(str, " => ");
	    unparse_expr(str, expr->e._catch.except);
	}
	stream_add_string(str, "'");
	break;

    case EXPR_FIRST:
	stream_add_string(str, "^");
	break;

    case EXPR_LAST:
	stream_add_string(str, "$");
	break;

    default:
	errlog("UNPARSE_EXPR: Unknown Expr_Kind: %d\n", expr->kind);
	stream_add_string(str, "(?!?!?!?!?)");
	break;
    }
}

static void
unparse_maplist(Stream *str, Map_List *map)
{
    while (map) {
	unparse_expr(str, map->key);
	stream_add_string(str, " -> ");
	unparse_expr(str, map->value);
	if (map->next)
	    stream_add_string(str, ", ");
	map = map->next;
    }
}


static void
unparse_arglist(Stream * str, Arg_List * args)
{
    while (args) {
	if (args->kind == ARG_SPLICE)
	    stream_add_char(str, '@');
	unparse_expr(str, args->expr);
	if (args->next)
	    stream_add_string(str, ", ");
	args = args->next;
    }
}

static void
unparse_scatter(Stream * str, Scatter * sc)
{
    while (sc) {
	switch (sc->kind) {
	case SCAT_REST:
	    stream_add_char(str, '@');
	    /* fall thru to ... */
	case SCAT_REQUIRED:
	    stream_add_string(str, prog->var_names[sc->id]);
	    break;
	case SCAT_OPTIONAL:
	    stream_printf(str, "?%s", prog->var_names[sc->id]);
	    if (sc->expr) {
		stream_add_string(str, " = ");
		unparse_expr(str, sc->expr);
	    }
	}
	if (sc->next)
	    stream_add_string(str, ", ");
	sc = sc->next;
    }
}

void
unparse_program(Program * p, Unparser_Receiver r, void *data,
		int fully_parenthesize, int indent_lines, int f_index)
{
    Stmt *stmt = decompile_program(p, f_index);

    prog = p;
    receiver = r;
    receiver_data = data;
    list_prg(stmt, fully_parenthesize, indent_lines);
    free_stmt(stmt);
}

static void
print_line(void *data, const char *line)
{
    FILE *fp = (FILE *)data;

    fprintf(fp, "%s\n", line);
}

void
unparse_to_file(FILE * fp, Program * p, int fully_parenthesize, int indent_lines,
		int f_index)
{
    unparse_program(p, print_line, fp, fully_parenthesize, indent_lines,
		    f_index);
}

void
unparse_to_stderr(Program * p, int fully_parenthesize, int indent_lines,
		  int f_index)
{
    unparse_to_file(stderr, p, fully_parenthesize, indent_lines, f_index);
}
