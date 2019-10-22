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

#ifndef Eval_env_h
#define Eval_env_h 1

#include "config.h"
#include "structures.h"
#include "version.h"

extern Var *new_rt_env(unsigned size);
extern void free_rt_env(Var * rt_env, unsigned size);
extern Var *copy_rt_env(Var * from, unsigned size);

void set_rt_env_obj(Var * env, int slot, Objid o);
void set_rt_env_str(Var * env, int slot, const char *s);
void set_rt_env_var(Var * env, int slot, Var v);

void fill_in_rt_consts(Var * env, DB_Version);

#endif
