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

#include <stdio.h>

#include "config.h"
#include "structures.h"

extern void set_log_file(FILE *);
FILE* get_log_file();
extern void set_log_file_name(const char *name);
const char* get_log_file_name();

enum {LOG_NONE, LOG_INFO1, LOG_INFO2, LOG_INFO3, LOG_INFO4,
      LOG_NOTICE, LOG_WARNING, LOG_ERROR};

extern void oklog(const char *,...);
extern void errlog(const char *,...);
extern void applog(int, const char *,...);
extern void log_perror(const char *);

extern void reset_command_history(void);
extern void log_command_history(void);
extern void add_command_to_history(Objid player, const char *command);

#define log_report_progress() ((--log_pcount <= 0) && log_report_progress_cktime())

extern int log_report_progress_cktime();
extern int log_pcount;
