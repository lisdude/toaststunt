/******************************************************************************
  Copyright (c) 1994, 1995, 1996 Xerox Corporation.  All rights reserved.
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

#include "my-stdio.h"

#include "bf_register.h"
#include "config.h"
#include "db.h"
#include "functions.h"
#include "list.h"
#include "opcode.h"
#include "program.h"
#include "storage.h"
#include "streams.h"
#include "unparse.h"
#include "utils.h"
#include "verbs.h"

const char *mnemonics[256], *ext_mnemonics[256];

struct mapping {
    unsigned value;
    const char *name;
};

struct mapping mappings[] =
{
    {OP_IF, "IF"},
    {OP_WHILE, "WHILE"},
    {OP_EIF, "ELSEIF"},
    {OP_FORK, "FORK"},
    {OP_FORK_WITH_ID, "FORK_NAMED"},
    {OP_FOR_LIST, "FOR_LIST"},
    {OP_FOR_RANGE, "FOR_RANGE"},
    {OP_INDEXSET, "INDEXSET"},
    {OP_PUSH_GET_PROP, "PUSH_GET_PROP"},
    {OP_GET_PROP, "GET_PROP"},
    {OP_CALL_VERB, "CALL_VERB"},
    {OP_PUT_PROP, "PUT_PROP"},
    {OP_BI_FUNC_CALL, "CALL_FUNC"},
    {OP_IF_QUES, "IF_EXPR"},
    {OP_REF, "INDEX"},
    {OP_RANGE_REF, "RANGE"},
    {OP_MAKE_SINGLETON_LIST, "MAKE_SINGLETON_LIST"},
    {OP_CHECK_LIST_FOR_SPLICE, "CHECK_LIST_FOR_SPLICE"},
    {OP_MULT, "MULTIPLY"},
    {OP_DIV, "DIVIDE"},
    {OP_MOD, "MOD"},
    {OP_ADD, "ADD"},
    {OP_MINUS, "SUBTRACT"},
    {OP_EQ, "EQ"},
    {OP_NE, "NE"},
    {OP_LT, "LT"},
    {OP_LE, "LE"},
    {OP_GT, "GT"},
    {OP_GE, "GE"},
    {OP_IN, "IN"},
    {OP_AND, "AND"},
    {OP_OR, "OR"},
    {OP_UNARY_MINUS, "NEGATE"},
    {OP_NOT, "NOT"},
    {OP_G_PUT, "PUT"},
    {OP_G_PUSH, "PUSH"},
#ifdef BYTECODE_REDUCE_REF
    {OP_G_PUSH_CLEAR, "PUSH_CLEAR"},
#endif /* BYTECODE_REDUCE_REF */
    {OP_IMM, "PUSH_LITERAL"},
    {OP_MAP_CREATE, "MAP_CREATE"},
    {OP_MAP_INSERT, "MAP_INSERT"},
    {OP_MAKE_EMPTY_LIST, "MAKE_EMPTY_LIST"},
    {OP_LIST_ADD_TAIL, "LIST_ADD_TAIL"},
    {OP_LIST_APPEND, "LIST_APPEND"},
    {OP_PUSH_REF, "PUSH_INDEX"},
    {OP_PUT_TEMP, "PUT_TEMP"},
    {OP_PUSH_TEMP, "PUSH_TEMP"},
    {OP_JUMP, "JUMP"},
    {OP_RETURN, "RETURN"},
    {OP_RETURN0, "RETURN 0"},
    {OP_DONE, "DONE"},
    {OP_POP, "POP"}
};

struct mapping ext_mappings[] =
{
    {EOP_RANGESET, "RANGESET"},
    {EOP_FIRST, "FIRST"},
    {EOP_LAST, "LAST"},
    {EOP_PUSH_LABEL, "PUSH_LABEL"},
    {EOP_SCATTER, "SCATTER"},
    {EOP_EXP, "EXPONENT"},
    {EOP_CATCH, "CATCH"},
    {EOP_END_CATCH, "END_CATCH"},
    {EOP_TRY_EXCEPT, "TRY_EXCEPT"},
    {EOP_END_EXCEPT, "END_EXCEPT"},
    {EOP_TRY_FINALLY, "TRY_FINALLY"},
    {EOP_END_FINALLY, "END_FINALLY"},
    {EOP_CONTINUE, "CONTINUE"},
    {EOP_WHILE_ID, "WHILE_ID"},
    {EOP_EXIT, "EXIT"},
    {EOP_FOR_LIST_1, "FOR_LIST_1"},
    {EOP_FOR_LIST_2, "FOR_LIST_2"},
    {EOP_EXIT_ID, "EXIT_ID"},
    {EOP_BITOR, "BITOR"},
    {EOP_BITAND, "BITAND"},
    {EOP_BITXOR, "BITXOR"},
    {EOP_BITSHL, "BITSHL"},
    {EOP_BITSHR, "BITSHR"},
    {EOP_COMPLEMENT, "COMPLEMENT"}
};

static void
initialize_tables(void)
{
    static int tables_initialized = 0;
    unsigned i;

    if (tables_initialized)
	return;

    for (i = 0; i < 256; i++) {
	mnemonics[i] = "*** Unknown opcode ***";
	ext_mnemonics[i] = "*** Unknown extended opcode ***";
    }

    for (i = 0; i < Arraysize(mappings); i++)
	mnemonics[mappings[i].value] = mappings[i].name;

    for (i = 0; i < Arraysize(ext_mappings); i++)
	ext_mnemonics[ext_mappings[i].value] = ext_mappings[i].name;

    tables_initialized = 1;
}

typedef void (*Printer) (const char *, void *);
static Printer print;
static void *print_data;
static int bytes_width, max_bytes_width;

static void
output(Stream * s)
{
    (*print) (reset_stream(s), print_data);
}

static void
new_insn(Stream * s, unsigned pc)
{
    stream_printf(s, "%3d:", pc);
    bytes_width = max_bytes_width;
}

static unsigned
add_bytes(Stream * s, Byte * vector, unsigned pc, unsigned length)
{
    unsigned arg = 0, b;

    while (length--) {
	if (bytes_width == 0) {
	    output(s);
	    stream_add_string(s, "    ");
	    bytes_width = max_bytes_width;
	}
	b = vector[pc++];
	stream_printf(s, " %03d", b);
	arg = (arg << 8) + b;
	bytes_width--;
    }

    return arg;
}

static void
finish_insn(Stream * s, Stream * insn)
{
    while (bytes_width--)
	stream_add_string(s, "    ");
    stream_add_string(s, reset_stream(insn));
    output(s);
}

static void
disassemble(Program * prog, Printer p, void *data)
{
    Stream *s = new_stream(100);
    Stream *insn = new_stream(50);
    int i, l;
    unsigned pc;
    Bytecodes bc;
    const char *ptr;
    const char **names = prog->var_names;
    unsigned num_names = prog->num_var_names;
#   define NAMES(i) (i < num_names ? names[i] : "*** Unknown variable ***")
    Var *literals = prog->literals;

    initialize_tables();
    print = p;
    print_data = data;
    stream_printf(s, "Language version number: %d", (int) prog->version);
    output(s);
    stream_printf(s, "First line number: %d", prog->first_lineno);
    output(s);

    for (i = -1; i < 0 || i < prog->fork_vectors_size; i++) {
	output(s);
	if (i == -1) {
	    stream_printf(s, "Main code vector:");
	    output(s);
	    stream_printf(s, "=================");
	    output(s);
	    bc = prog->main_vector;
	} else {
	    stream_printf(s, "Forked code vector %d:", i);
	    l = stream_length(s);
	    output(s);
	    while (l--)
		stream_add_char(s, '=');
	    output(s);
	    bc = prog->fork_vectors[i];
	}

	stream_printf(s, "[Bytes for labels = %d, literals = %d, ",
		      bc.numbytes_label, bc.numbytes_literal);
	stream_printf(s, "forks = %d, variables = %d, stack refs = %d]",
		      bc.numbytes_fork, bc.numbytes_var_name,
		      bc.numbytes_stack);
	output(s);
	stream_printf(s, "[Maximum stack size = %d]", bc.max_stack);
	output(s);

	max_bytes_width = 5;

	for (pc = 0; pc < bc.size;) {
	    Byte b;
	    unsigned arg;
#	    define ADD_BYTES(n)	(arg = add_bytes(s, bc.vector, pc, n),	\
				 pc += n,				\
				 arg)
	    unsigned a1, a2, a3;

	    new_insn(s, pc);
	    b = add_bytes(s, bc.vector, pc++, 1);
	    if (b != OP_EXTENDED)
		stream_add_string(insn, COUNT_TICK(b) ? " * " : "   ");
	    if (IS_OPTIM_NUM_OPCODE(b))
		stream_printf(insn, "NUM %d", OPCODE_TO_OPTIM_NUM(b));
#ifdef BYTECODE_REDUCE_REF
	    else if (IS_PUSH_CLEAR_n(b))
		stream_printf(insn, "PUSH_CLEAR %s", NAMES(PUSH_CLEAR_n_INDEX(b)));
#endif /* BYTECODE_REDUCE_REF */
	    else if (IS_PUSH_n(b))
		stream_printf(insn, "PUSH %s", NAMES(PUSH_n_INDEX(b)));
	    else if (IS_PUT_n(b))
		stream_printf(insn, "PUT %s", NAMES(PUT_n_INDEX(b)));
	    else if (b == OP_EXTENDED) {
		b = ADD_BYTES(1);
		stream_add_string(insn, COUNT_EOP_TICK(b) ? " * " : "   ");
		stream_add_string(insn, ext_mnemonics[b]);
		switch ((Extended_Opcode) b) {
		case EOP_WHILE_ID:
		    a1 = ADD_BYTES(bc.numbytes_var_name);
		    a2 = ADD_BYTES(bc.numbytes_label);
		    stream_printf(insn, " %s %d", NAMES(a1), a2);
		    break;
		case EOP_EXIT_ID:
		    stream_printf(insn, " %s",
				  NAMES(ADD_BYTES(bc.numbytes_var_name)));
		    /* fall thru */
		case EOP_EXIT:
		    a1 = ADD_BYTES(bc.numbytes_stack);
		    a2 = ADD_BYTES(bc.numbytes_label);
		    stream_printf(insn, " %d %d", a1, a2);
		    break;
		case EOP_PUSH_LABEL:
		case EOP_END_CATCH:
		case EOP_END_EXCEPT:
		case EOP_TRY_FINALLY:
		    stream_printf(insn, " %d", ADD_BYTES(bc.numbytes_label));
		    break;
		case EOP_TRY_EXCEPT:
		    stream_printf(insn, " %d", ADD_BYTES(1));
		    break;
		case EOP_FIRST:
		case EOP_LAST:
		    stream_printf(insn, " %d", ADD_BYTES(bc.numbytes_stack));
		    break;
		case EOP_SCATTER:
		    {
			int i, nargs = ADD_BYTES(1);

			a1 = ADD_BYTES(1);
			a2 = ADD_BYTES(1);
			stream_printf(insn, " %d/%d/%d:", nargs, a1, a2);
			for (i = 0; i < nargs; i++) {
			    a1 = ADD_BYTES(bc.numbytes_var_name);
			    a2 = ADD_BYTES(bc.numbytes_label);
			    stream_printf(insn, " %s/%d", NAMES(a1), a2);
			}
			stream_printf(insn, " %d",
				      ADD_BYTES(bc.numbytes_label));
		    }
		    break;
		case EOP_FOR_LIST_1:
		    a1 = ADD_BYTES(bc.numbytes_var_name);
		    a2 = ADD_BYTES(bc.numbytes_label);
		    stream_printf(insn, " %s %d", NAMES(a1), a2);
		    break;
		case EOP_FOR_LIST_2:
		    a1 = ADD_BYTES(bc.numbytes_var_name);
		    a2 = ADD_BYTES(bc.numbytes_var_name);
		    a3 = ADD_BYTES(bc.numbytes_label);
		    stream_printf(insn, " %s %s %d", NAMES(a1), NAMES(a2), a3);
		    break;
		default:
		    break;
		}
	    } else {
		stream_add_string(insn, mnemonics[b]);
		switch ((Opcode) b) {
		case OP_IF:
		case OP_IF_QUES:
		case OP_EIF:
		case OP_AND:
		case OP_OR:
		case OP_JUMP:
		case OP_WHILE:
		    stream_printf(insn, " %d", ADD_BYTES(bc.numbytes_label));
		    break;
		case OP_FORK:
		    stream_printf(insn, " %d", ADD_BYTES(bc.numbytes_fork));
		    break;
		case OP_FORK_WITH_ID:
		    a1 = ADD_BYTES(bc.numbytes_fork);
		    a2 = ADD_BYTES(bc.numbytes_var_name);
		    stream_printf(insn, " %d %s", a1, NAMES(a2));
		    break;
		case OP_FOR_LIST:
		case OP_FOR_RANGE:
		    a1 = ADD_BYTES(bc.numbytes_var_name);
		    a2 = ADD_BYTES(bc.numbytes_label);
		    stream_printf(insn, " %s %d", NAMES(a1), a2);
		    break;
		case OP_G_PUSH:
#ifdef BYTECODE_REDUCE_REF
		case OP_G_PUSH_CLEAR:
#endif /* BYTECODE_REDUCE_REF */
		case OP_G_PUT:
		    stream_printf(insn, " %s",
				  NAMES(ADD_BYTES(bc.numbytes_var_name)));
		    break;
		case OP_IMM:
		    {
			Var v;

			v = literals[ADD_BYTES(bc.numbytes_literal)];
			switch (v.type) {
			case TYPE_OBJ:
			    stream_printf(insn, " #%" PRIdN, v.v.obj);
			    break;
			case TYPE_INT:
			    stream_printf(insn, " %" PRIdN, v.v.num);
			    break;
            case TYPE_FLOAT:
                stream_add_char(insn, ' ');
                unparse_value(insn, v);
                break;
			case TYPE_STR:
			    stream_add_string(insn, " \"");
			    for (ptr = v.v.str; *ptr; ptr++) {
				if (*ptr == '"' || *ptr == '\\')
				    stream_add_char(insn, '\\');
				stream_add_char(insn, *ptr);
			    }
			    stream_add_char(insn, '"');
			    break;
			case TYPE_ERR:
			    stream_printf(insn, " %s", error_name(v.v.err));
			    break;
			default:
			    stream_printf(insn, " <literal type = %d>",
					  v.type);
			    break;
			}
		    }
		    break;
		case OP_BI_FUNC_CALL:
		    stream_printf(insn, " %s", name_func_by_num(ADD_BYTES(1)));
		default:
		    break;
		}
	    }

	    finish_insn(s, insn);
	}
    }

    free_stream(s);
    free_stream(insn);
}

static void
print_line(const char *line, void *data)
{
    FILE *f = (FILE *)data;

    fprintf(f, "%s\n", line);
}

void
disassemble_to_file(FILE * fp, Program * prog)
{
    disassemble(prog, print_line, fp);
}

void
disassemble_to_stderr(Program * prog)
{
    disassemble_to_file(stderr, prog);
}

struct data {
    char **lines;
    int used, max;
};

static void
add_line(const char *line, void *data)
{
    struct data *d = (struct data *)data;

    if (d->used >= d->max) {
	int new_max = (d->max == 0 ? 20 : d->max * 2);
	char **_new = (char **)mymalloc(sizeof(char **) * new_max, M_DISASSEMBLE);
	int i;

	for (i = 0; i < d->used; i++)
	    _new[i] = d->lines[i];
	if (d->lines)
	    myfree(d->lines, M_DISASSEMBLE);
	d->lines = _new;
	d->max = new_max;
    }
    d->lines[d->used++] = str_dup(line);
}

static package
bf_disassemble(Var arglist, Byte next, void *vdata, Objid progr)
{
    Var obj = arglist.v.list[1];
    Var desc = arglist.v.list[2];
    db_verb_handle h;
    struct data data;
    Var r;
    int i;
    enum error e;

    if (!obj.is_object()) {
	free_var(arglist);
	return make_error_pack(E_TYPE);
    } else if ((e = validate_verb_descriptor(desc)) != E_NONE
	|| (e = E_INVARG, !is_valid(obj))) {
	free_var(arglist);
	return make_error_pack(e);
    }
    h = find_described_verb(obj, desc);
    free_var(arglist);

    if (!h.ptr)
	return make_error_pack(E_VERBNF);
    if (!db_verb_allows(h, progr, VF_READ))
	return make_error_pack(E_PERM);

    data.lines = 0;
    data.used = data.max = 0;
    disassemble(db_verb_program(h), add_line, &data);
    r = new_list(data.used);
    for (i = 1; i <= data.used; i++) {
	r.v.list[i].type = TYPE_STR;
	r.v.list[i].v.str = data.lines[i - 1];
    }
    if (data.lines)
	myfree(data.lines, M_DISASSEMBLE);
    return make_var_pack(r);
}

void
register_disassemble(void)
{
    register_function("disassemble", 2, 2, bf_disassemble, TYPE_ANY, TYPE_ANY);
}
