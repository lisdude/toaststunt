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

#ifndef Sym_Table_h
#define Sym_Table_h 1

#include "config.h"
#include "version.h"

typedef struct {
    unsigned max_size;
    unsigned size;
    const char **names;
} Names;

extern Names *new_builtin_names(DB_Version);
extern int first_user_slot(DB_Version);
extern unsigned find_or_add_name(Names **, const char *);
extern int find_name(Names *, const char *);
extern void free_names(Names *);

/* Environment slots for built-in variables */
#define SLOT_NUM	0
#define SLOT_OBJ	1
#define SLOT_STR	2
#define SLOT_LIST	3
#define SLOT_ERR	4
#define SLOT_PLAYER	5
#define SLOT_THIS	6
#define SLOT_CALLER	7
#define SLOT_VERB	8
#define SLOT_ARGS	9
#define SLOT_ARGSTR	10
#define SLOT_DOBJ	11
#define SLOT_DOBJSTR	12
#define SLOT_PREPSTR	13
#define SLOT_IOBJ	14
#define SLOT_IOBJSTR	15

/* Added in DBV_Float: */
#define SLOT_INT	16
#define SLOT_FLOAT	17

/* Added in DBV_Map: */
#define SLOT_MAP	18

/* Added in DBV_Anon */
#define SLOT_ANON	19

/* Added in DBV_Waif */
#define SLOT_WAIF   20

/* Added in DBV_Bool */
#define SLOT_BOOL   21
#define SLOT_TRUE   22
#define SLOT_FALSE   23

#endif				/* !Sym_Table_h */
