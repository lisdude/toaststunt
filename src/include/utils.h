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

#ifndef Utils_h
#define Utils_h 1

#include <stdio.h>

#include "config.h"
#include "execute.h"
#include "streams.h"

#undef MAX
#undef MIN
#define MAX(A, B) ((A) > (B) ? (A) : (B))
#define MIN(A, B) ((A) < (B) ? (A) : (B))

#define Arraysize(x) (sizeof(x) / sizeof(*x))

extern int verbcasecmp(const char *verb, const char *word);

extern unsigned str_hash(const char *);

extern void complex_free_var(Var);
extern Var complex_var_ref(Var);
extern Var complex_var_dup(Var);
extern int var_refcount(Var);

extern void aux_free(Var);

static inline void
free_var(Var v)
{
    if (v.is_complex())
	complex_free_var(v);
}

static inline Var
var_ref(Var v)
{
    if (v.is_complex())
	return complex_var_ref(v);
    else
	return v;
}

static inline Var
var_dup(Var v)
{
    if (v.is_complex())
	return complex_var_dup(v);
    else
	return v;
}

extern int is_true(Var v);
extern int compare(Var lhs, Var rhs, int case_matters);
extern int equality(Var lhs, Var rhs, int case_matters);

extern void stream_add_strsub(Stream *, const char *, const char *, const char *, int);
extern int strindex(const char *, int, const char *, int, int);
extern int strrindex(const char *, int, const char *, int, int);

extern const char *strtr(const char *, int, const char *, int, const char *, int, int);

extern Var get_system_property(const char *);
extern Objid get_system_object(const char *);

extern int value_bytes(Var);

extern void stream_add_raw_bytes_to_clean(Stream *, const char *buffer, int buflen);
extern const char *raw_bytes_to_clean(const char *buffer, int buflen);
extern const char *clean_to_raw_bytes(const char *binary, int *rawlen);

extern void stream_add_raw_bytes_to_binary(Stream *, const char *buffer, int buflen);
extern const char *raw_bytes_to_binary(const char *buffer, int buflen);
extern const char *binary_to_raw_bytes(const char *binary, int *rawlen);

extern Var anonymizing_var_ref(Var v, Objid progr);
				/* To be used in places where you
				 * need to copy (via `var_ref()') a
				 * value and also need to ensure that
				 * _if_ it's an anonymous object it
				 * remains anonymous for
				 * non-wizards/owners.
				 */

#endif
