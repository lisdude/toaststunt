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

#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "list.h"
#include "options.h"
#include "server.h"
#include "storage.h"
#include "structures.h"
#include "utils.h"

static inline int
refcount_overhead(Memory_Type type)
{
    /* These are the only allocation types that are addref()'d.
     * As long as we're living on the wild side, avoid getting the
     * refcount slot for allocations that won't need it.
     */
    int total = 0;
    switch (type) {
        /* deal with systems with picky alignment issues */
        case M_LIST:
        case M_TREE:
        case M_TRAV:
        case M_ANON:
        case M_WAIF:
        case M_STRING:
            total = sizeof(var_metadata);
            break;
        default:
            total = 0;
    }

    return total;
}

void *
mymalloc(unsigned size, Memory_Type type)
{
    char *memptr;
    char msg[100];
    int offs;

    if (size == 0)      /* For queasy systems */
        size = 1;

    offs = refcount_overhead(type);
    memptr = (char *) malloc(offs + size);
    if (!memptr) {
        sprintf(msg, "memory allocation (size %u) failed!", size);
        panic_moo(msg);
    }

    if (offs) {
        memptr += offs;
        var_metadata *metadata = (var_metadata *)(memptr - sizeof(var_metadata));

        metadata->refcount = 1;

#ifdef ENABLE_GC
        if (type == M_LIST || type == M_TREE || type == M_ANON) {
            metadata->buffered = 0;
            metadata->color = (type == M_ANON) ? GC_BLACK : GC_GREEN;
        }
#endif /* ENABLE_GC */

#ifdef MEMO_SIZE
        if (type == M_STRING)
            metadata->size = size - 1;
#endif /* MEMO_SIZE */

#ifdef MEMO_SIZE
        if (type == M_LIST || type == M_TREE)
            metadata->size = 0;
#endif /* MEMO_SIZE */

    }
    return memptr;
}

const char *
str_ref(const char *s)
{
    addref(s);
    return s;
}

char *
str_dup(const char *s)
{
    char *r;

    if (s == nullptr || *s == '\0') {
        static char *emptystring;

        if (!emptystring) {
            emptystring = (char *) mymalloc(1, M_STRING);
            *emptystring = '\0';
        }
        addref(emptystring);
        return emptystring;
    } else {
        r = (char *) mymalloc(strlen(s) + 1, M_STRING); /* NO MEMO HERE */
        strcpy(r, s);
    }
    return r;
}

void *
myrealloc(void *ptr, unsigned size, Memory_Type type)
{
    int offs = refcount_overhead(type);
    static char msg[100];

    ptr = realloc((char *) ptr - offs, size + offs);
    if (!ptr) {
        sprintf(msg, "memory re-allocation (size %u) failed!", size);
        panic_moo(msg);
    }

    return (char *) ptr + offs;
}

void
myfree(void *ptr, Memory_Type type)
{
    free((char *) ptr - refcount_overhead(type));
}
