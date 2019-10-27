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

extern void register_gc(void);
extern void register_collection(void);
extern void register_disassemble(void);
extern void register_extensions(void);
extern void register_execute(void);
extern void register_functions(void);
extern void register_list(void);
extern void register_log(void);
extern void register_map(void);
extern void register_numbers(void);
extern void register_objects(void);
extern void register_property(void);
extern void register_server(void);
extern void register_tasks(void);
extern void register_verbs(void);
extern void register_yajl(void);
extern void register_base64(void);
extern void register_fileio(void);
extern void register_system(void);
extern void register_exec(void);
extern void register_crypto(void);
extern void register_sqlite(void);
extern void register_pcre(void);
extern void register_background(void);
extern void register_waif(void);
extern void register_simplexnoise(void);
extern void register_argon2(void);
extern void register_spellcheck(void);
extern void register_curl(void);
extern void register_name_lookup(void);
