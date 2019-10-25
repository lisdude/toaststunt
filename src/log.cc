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

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "bf_register.h"
#include "config.h"
#include "functions.h"
#include "log.h"
#include "options.h"
#include "storage.h"
#include "streams.h"
#include "utils.h"

static FILE *log_file = nullptr;
static const char *log_file_name = nullptr;

void
set_log_file(FILE * f)
{
    log_file = f;
}

FILE*
get_log_file()
{
    return log_file;
}

void
set_log_file_name(const char *name)
{
    log_file_name = name;
}

const char*
get_log_file_name()
{
    return log_file_name;
}

int log_pcount = 5000;
static time_t log_prev = 0;
int log_report_progress_cktime()
{
    time_t now = time(nullptr);
    log_pcount = 5000;
    return ((now >= log_prev + 2) && (log_prev = now, 1));
}

static void
do_log(const int severity, const char *fmt, va_list args)
{
    FILE *f;
    char *nowstr = nullptr;

    log_prev = time(nullptr);
    log_pcount = 5000;

    if (log_file) {
	nowstr = ctime(&log_prev);
	nowstr[19] = '\0';	/* kill the year and newline at the end */
	nowstr = nowstr + 4;	/* skip the day of week */
	f = log_file;
    } else {
	f = stderr;
    }

#ifdef COLOR_LOGS
    int color = isatty(fileno(f));
#else
    int color = 0;
#endif

    if (nowstr) {
	fprintf(f, "%s: ", nowstr);
    }

    if (color) {
	if (LOG_NONE == severity)
	    ;
	else if (LOG_INFO1 == severity)
	    fprintf(f, "\x1b[1m");	// bright
	else if (LOG_INFO2 == severity)
	    fprintf(f, "\x1b[36;1m");	// bright cyan
	else if (LOG_INFO3 == severity)
	    fprintf(f, "\x1b[35;1m");	// bright magenta
	else if (LOG_INFO4 == severity)
	    fprintf(f, "\x1b[34;1m");	// bright blue
	else if (LOG_NOTICE == severity)
	    fprintf(f, "\x1b[32;1m");	// bright green
	else if (LOG_WARNING == severity)
	    fprintf(f, "\x1b[33;1m");	// bright yellow
	else if (LOG_ERROR == severity)
	    fprintf(f, "\x1b[31;1m");	// bright red
    } else {
	if (LOG_NONE == severity)
	    ;
	else if (LOG_INFO1 >= severity && LOG_INFO4 <= severity)
	    fprintf(f, "> ");
	else if (LOG_NOTICE == severity)
	    fprintf(f, "!!! ");
	else if (LOG_WARNING == severity)
	    fprintf(f, "### ");
	else if (LOG_ERROR == severity)
	    fprintf(f, "*** ");
    }

    vfprintf(f, fmt, args);

    if (color) {
	fprintf(f, "\x1b[0m");
    }

    fflush(f);
}

void
oklog(const char *fmt,...)
{
    va_list args;

    va_start(args, fmt);
    do_log(LOG_NONE, fmt, args);
    va_end(args);
}

void
errlog(const char *fmt,...)
{
    va_list args;

    va_start(args, fmt);
    do_log(LOG_ERROR, fmt, args);
    va_end(args);
}

void
applog(int level, const char *fmt,...)
{
    va_list args;

    va_start(args, fmt);
    do_log(level, fmt, args);
    va_end(args);
}

void
log_perror(const char *what)
{
    errlog("%s: %s\n", what, strerror(errno));
}


#ifdef LOG_COMMANDS
static Stream *command_history = 0;
#endif

void
reset_command_history()
{
#ifdef LOG_COMMANDS
    if (command_history == 0)
	command_history = new_stream(1024);
    else
	reset_stream(command_history);
#endif
}

void
log_command_history()
{
#ifdef LOG_COMMANDS
    errlog("COMMAND HISTORY:\n%s", stream_contents(command_history));
#endif
}

void
add_command_to_history(Objid player, const char *command)
{
#ifdef LOG_COMMANDS
    time_t now = time(0);
    char *nowstr = ctime(&now);

    nowstr[19] = '\0';		/* kill the year and newline at the end */
    stream_printf(command_history, "%s: #%" PRIdN ": %s\n",
		  nowstr + 4,	/* skip day of week */
		  player, command);
#endif				/* LOG_COMMANDS */
}

/**** built in functions ****/

static package
bf_server_log(Var arglist, Byte next, void *vdata, Objid progr)
{
    if (!is_wizard(progr)) {
	free_var(arglist);
	return make_error_pack(E_PERM);
    } else {
	const char *message = arglist.v.list[1].v.str;
	int level = LOG_NONE;

	if (arglist.v.list[0].v.num == 2) {
	    if (TYPE_INT == arglist.v.list[2].type
	          && LOG_NONE <= arglist.v.list[2].v.num
	          && LOG_ERROR >= arglist.v.list[2].v.num) {
		level = arglist.v.list[2].v.num;
	    } else if (is_true(arglist.v.list[2])) {
		level = LOG_ERROR;
	    }
	}

	applog(level, "> %s\n", message);

	free_var(arglist);
	return no_var_pack();
    }
}

void
register_log(void)
{
    register_function("server_log", 1, 2, bf_server_log, TYPE_STR, TYPE_ANY);
}
