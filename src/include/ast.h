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

#ifndef AST_h
#define AST_h 1

#include "config.h"
#include "parser.h"
#include "structures.h"
#include "sym_table.h"

typedef struct Expr Expr;
typedef struct Map_List Map_List;
typedef struct Arg_List Arg_List;
typedef struct Stmt Stmt;
typedef struct Cond_Arm Cond_Arm;
typedef struct Except_Arm Except_Arm;
typedef struct Scatter Scatter;

struct Expr_Binary {
    Expr *lhs, *rhs;
};

struct Map_List {
    Map_List *next;
    Expr *key;
    Expr *value;
};

enum Arg_Kind {
    ARG_NORMAL, ARG_SPLICE
};

struct Arg_List {
    Arg_List *next;
    enum Arg_Kind kind;
    Expr *expr;
};

enum Scatter_Kind {
    SCAT_REQUIRED, SCAT_OPTIONAL, SCAT_REST
};

struct Scatter {
    Scatter *next;
    enum Scatter_Kind kind;
    int id;
    Expr *expr;
    /* These fields for convenience during code generation and decompiling */
    int label, next_label;
};

struct Expr_Call {
    unsigned func;
    Arg_List *args;
};

struct Expr_Verb {
    Expr *obj, *verb;
    Arg_List *args;
};

struct Expr_Range {
    Expr *base, *from, *to;
};

struct Expr_Cond {
    Expr *condition, *consequent, *alternate;
};

struct Expr_Catch {
    Expr *_try;
    Arg_List *codes;
    Expr *except;
};

enum Expr_Kind {
    EXPR_VAR, EXPR_ID,
    EXPR_PROP, EXPR_VERB,
    EXPR_INDEX, EXPR_RANGE,
    EXPR_ASGN, EXPR_CALL,
    EXPR_PLUS, EXPR_MINUS, EXPR_TIMES, EXPR_DIVIDE, EXPR_MOD, EXPR_EXP,
    EXPR_NEGATE,
    EXPR_AND, EXPR_OR, EXPR_NOT,
    EXPR_EQ, EXPR_NE, EXPR_LT, EXPR_LE, EXPR_GT, EXPR_GE,
    EXPR_IN, EXPR_LIST, EXPR_COND,
    EXPR_CATCH, EXPR_LENGTH, EXPR_SCATTER, /* EXPR_LENGTH is retired */
    EXPR_MAP,
    EXPR_FIRST, EXPR_LAST,
    EXPR_BITOR, EXPR_BITAND, EXPR_BITXOR,
    EXPR_BITSHL, EXPR_BITSHR, EXPR_COMPLEMENT,
    SizeOf_Expr_Kind		/* The last element is also the number of elements... */
};

union Expr_Data {
    Var var;
    int id;
    struct Expr_Binary bin;
    struct Expr_Call call;
    struct Expr_Verb verb;
    struct Expr_Range range;
    struct Expr_Cond cond;
    struct Expr_Catch _catch;
    Expr *expr;
    Map_List *map;
    Arg_List *list;
    Scatter *scatter;
};

struct Expr {
    enum Expr_Kind kind;
    union Expr_Data e;
};

struct Cond_Arm {
    Cond_Arm *next;
    Expr *condition;
    Stmt *stmt;
};

struct Except_Arm {
    Except_Arm *next;
    int id;
    Arg_List *codes;
    Stmt *stmt;
    /* This field is for convenience during code generation and decompiling */
    int label;
};

struct Stmt_Cond {
    Cond_Arm *arms;
    Stmt *otherwise;
};

struct Stmt_List {
    int id;
    int index;
    Expr *expr;
    Stmt *body;
};

struct Stmt_Range {
    int id;
    Expr *from, *to;
    Stmt *body;
};

struct Stmt_Loop {
    int id;
    Expr *condition;
    Stmt *body;
};

struct Stmt_Fork {
    int id;
    Expr *time;
    Stmt *body;
};

struct Stmt_Catch {
    Stmt *body;
    Except_Arm *excepts;
};

struct Stmt_Finally {
    Stmt *body;
    Stmt *handler;
};

enum Stmt_Kind {
    STMT_COND, STMT_LIST, STMT_RANGE, STMT_WHILE, STMT_FORK, STMT_EXPR,
    STMT_RETURN, STMT_TRY_EXCEPT, STMT_TRY_FINALLY, STMT_BREAK, STMT_CONTINUE
};

union Stmt_Data {
    struct Stmt_Cond cond;
    struct Stmt_List list;
    struct Stmt_Range range;
    struct Stmt_Loop loop;
    struct Stmt_Fork fork;
    struct Stmt_Catch _catch;
    struct Stmt_Finally finally;
    Expr *expr;
    int exit;
};

struct Stmt {
    Stmt *next;
    enum Stmt_Kind kind;
    union Stmt_Data s;
};


extern void begin_code_allocation(void);
extern void end_code_allocation(int);

extern Stmt *alloc_stmt(enum Stmt_Kind);
extern Cond_Arm *alloc_cond_arm(Expr *, Stmt *);
extern Expr *alloc_expr(enum Expr_Kind);
extern Expr *alloc_var(var_type);
extern Expr *alloc_binary(enum Expr_Kind, Expr *, Expr *);
extern Expr *alloc_verb(Expr *, Expr *, Arg_List *);
extern Map_List * alloc_map_list(Expr * key, Expr * value);
extern Arg_List *alloc_arg_list(enum Arg_Kind, Expr *);
extern Except_Arm *alloc_except(int, Arg_List *, Stmt *);
extern Scatter *alloc_scatter(enum Scatter_Kind, int, Expr *);
extern char *alloc_string(const char *);

extern void dealloc_node(void *);
extern void dealloc_string(char *);
extern void free_stmt(Stmt *);

#endif				/* !AST_h */
