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

#ifndef Storage_h
#define Storage_h 1

#include <string.h>

#include "options.h"

/* See "Concurrent Cycle Collection in Reference Counted Systems",
 * (Bacon and Rajan, 2001) for a description of the cycle collection
 * algorithm and the colors.
 */
typedef enum GC_Color {
    GC_GREEN,
    GC_YELLOW,
    GC_BLACK,
    GC_GRAY,
    GC_WHITE,
    GC_PURPLE,
    GC_PINK
} GC_Color;

typedef struct reference_overhead {
    unsigned int count:28;
    unsigned int buffered:1;
    GC_Color color:3;
} reference_overhead;

static inline int
addref(const void *ptr)
{
    return ++((reference_overhead *)ptr)[-1].count;
}

static inline int
delref(const void *ptr)
{
    return --((reference_overhead *)ptr)[-1].count;
}

static inline int
refcount(const void *ptr)
{
    return ((reference_overhead *)ptr)[-1].count;
}

static inline void
gc_set_buffered(const void *ptr)
{
    ((reference_overhead *)ptr)[-1].buffered = 1;
}

static inline void
gc_clear_buffered(const void *ptr)
{
    ((reference_overhead *)ptr)[-1].buffered = 0;
}

static inline int
gc_is_buffered(const void *ptr)
{
    return ((reference_overhead *)ptr)[-1].buffered;
}

static inline void
gc_set_color(const void *ptr, GC_Color color)
{
    ((reference_overhead *)ptr)[-1].color = color;
}

static inline GC_Color
gc_get_color(const void *ptr)
{
    return ((reference_overhead *)ptr)[-1].color;
}

typedef enum Memory_Type {
    M_AST_POOL, M_AST, M_PROGRAM, M_PVAL, M_NETWORK, M_STRING, M_VERBDEF,
    M_LIST, M_PREP, M_PROPDEF, M_OBJECT_TABLE, M_OBJECT, M_FLOAT, M_INT,
    M_STREAM, M_NAMES, M_ENV, M_TASK, M_PATTERN,

    M_BYTECODES, M_FORK_VECTORS, M_LIT_LIST,
    M_PROTOTYPE, M_CODE_GEN, M_DISASSEMBLE, M_DECOMPILE,

    M_RT_STACK, M_RT_ENV, M_BI_FUNC_DATA, M_VM,

    M_REF_ENTRY, M_REF_TABLE, M_VC_ENTRY, M_VC_TABLE, M_STRING_PTRS,
    M_INTERN_POINTER, M_INTERN_ENTRY, M_INTERN_HUNK,

    M_TREE, M_NODE, M_TRAV,

    M_ANON, /* anonymous object */

    M_WAIF, M_WAIF_XTRA,

    /* to be used when no more specific type applies */
    M_STRUCT, M_ARRAY,

    Sizeof_Memory_Type

} Memory_Type;

extern char *str_dup(const char *);
extern const char *str_ref(const char *);

extern void myfree(void *where, Memory_Type type);
extern void *mymalloc(unsigned size, Memory_Type type);
extern void *myrealloc(void *where, unsigned size, Memory_Type type);

static inline void		/* XXX was extern, fix for non-gcc compilers */
free_str(const char *s)
{
    if (delref(s) == 0)
	myfree((void *) s, M_STRING);
}

#ifdef MEMO_STRLEN
/*
 * Using the same mechanism as ref_count.h uses to hide Value ref counts,
 * keep a memozied strlen in the storage with the string.
 */
#define memo_strlen(X)		((void)0, (((int *)(X))[-2]))
#else
#define memo_strlen(X)		strlen(X)

#endif /* MEMO_STRLEN */

#endif				/* Storage_h */
