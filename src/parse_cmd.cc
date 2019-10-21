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

#include "my-ctype.h"
#include "my-stdio.h"
#include "my-stdlib.h"
#include "my-string.h"
#include "my-time.h"

#include "config.h"
#include "db.h"
#include "list.h"
#include "match.h"
#include "parse_cmd.h"
#include "storage.h"
#include "structures.h"
#include "utils.h"

char **
parse_into_words(char *input, int *nwords)
{
    static char **words = 0;
    static int max_words = 0;
    int in_quotes = 0;
    char *ptr = input;

    if (!words) {
	max_words = 50;
	words = (char **)mymalloc(max_words * sizeof(char *), M_STRING_PTRS);
    }
    while (*input == ' ')
	input++;

    for (*nwords = 0; *input != '\0'; (*nwords)++) {
	if (*nwords == max_words) {
	    int new_max = max_words * 2;
	    char **_new = (char **)mymalloc(new_max * sizeof(char *), M_STRING_PTRS);
	    int i;

	    for (i = 0; i < max_words; i++)
		_new[i] = words[i];

	    myfree(words, M_STRING_PTRS);
	    words = _new;
	    max_words = new_max;
	}
	words[*nwords] = ptr;
	while (*input != '\0' && (in_quotes || *input != ' ')) {
	    char c = *(input++);

	    if (c == '"')
		in_quotes = !in_quotes;
	    else if (c == '\\') {
		if (*input != '\0')
		    *(ptr++) = *(input++);
	    } else
		*(ptr++) = c;
	}
	while (*input == ' ')
	    input++;
	*(ptr++) = '\0';
    }

    return words;
}

static char *
build_string(int argc, char *argv[])
{
    int i, len = 0;
    char *str;

    if (!argc)
	return str_dup("");

    len = strlen(argv[0]);
    for (i = 1; i < argc; i++)
	len += 1 + strlen(argv[i]);

    str = (char *) mymalloc(len + 1, M_STRING);

    strcpy(str, argv[0]);
    for (i = 1; i < argc; i++) {
	strcat(str, " ");
	strcat(str, argv[i]);
    }

    return str;
}

#define MAXWORDS		500	/* maximum number of words in a line */
					/* This limit should be removed...   */

Var
parse_into_wordlist(const char *command)
{
    int argc, i;
    char **argv;
    Var args;
    char *s = str_dup(command);

    argv = parse_into_words(s, &argc);
    args = new_list(argc);
    for (i = 1; i <= argc; i++) {
	args.v.list[i].type = TYPE_STR;
	args.v.list[i].v.str = str_dup(argv[i - 1]);
    }
    free_str(s);
    return args;
}

Parsed_Command *
parse_command(const char *command, Objid user)
{
    static Parsed_Command pc;
    const char *argstr;
    char *buf;
    const char *verb;
    int argc;
    char **argv;
    int pstart, pend, dlen;
    int i;

    while (*command == ' ')
	command++;
    switch (*command) {
    case '"':
	verb = "say";
	goto finish_specials;
    case ':':
	verb = "emote";
	goto finish_specials;
    case ';':
	verb = "eval";
	goto finish_specials;

      finish_specials:
	argstr = command + 1;
	buf = (char *) mymalloc(strlen(argstr) + strlen(verb) + 2,
				M_STRING);
	strcpy(buf, verb);
	strcat(buf, " ");
	strcat(buf, argstr);
	break;

    default:
	buf = str_dup(command);
	{			/* Skip past even complexly-quoted verbs */
	    int in_quotes = 0;

	    argstr = command;
	    while (*argstr && (in_quotes || *argstr != ' ')) {
		char c = *(argstr++);

		if (c == '"')
		    in_quotes = !in_quotes;
		else if (c == '\\' && *argstr)
		    argstr++;
	    }
	}
	while (*argstr == ' ')
	    argstr++;
	break;
    }
    argv = parse_into_words(buf, &argc);

    if (argc == 0) {
	free_str(buf);
	return 0;
    }
    pc.verb = str_dup(argv[0]);
    pc.argstr = str_dup(argstr);

    pc.args = new_list(argc - 1);
    for (i = 1; i < argc; i++) {
	pc.args.v.list[i].type = TYPE_STR;
	pc.args.v.list[i].v.str = str_dup(argv[i]);
    }

    /*
     * look for a preposition
     */
    if (argc > 1) {
	pc.prep = db_find_prep(argc - 1, argv + 1, &pstart, &pend);
	if (pc.prep == PREP_NONE) {
	    pstart = argc;
	    pend = argc;
	} else {
	    pstart++;
	    pend++;
	}
    } else {
	pc.prep = PREP_NONE;
	pstart = argc;
	pend = argc;
    }

    /*
     * if there's a preposition,
     * find the iobj & dobj around it, if any
     */
    if (pc.prep != PREP_NONE) {
	pc.prepstr = build_string(pend - pstart + 1, argv + pstart);
	pc.iobjstr = build_string(argc - (pend + 1), argv + (pend + 1));
	pc.iobj = match_object(user, pc.iobjstr);
    } else {
	pc.prepstr = str_dup("");
	pc.iobjstr = str_dup("");
	pc.iobj = NOTHING;
    }

    dlen = pstart - 1;
    if (dlen == 0) {
	pc.dobjstr = str_dup("");
	pc.dobj = NOTHING;
    } else {
	pc.dobjstr = build_string(dlen, argv + 1);
	pc.dobj = match_object(user, pc.dobjstr);
    }

    free_str(buf);

    return &pc;
}

void
free_parsed_command(Parsed_Command * pc)
{
    free_str(pc->verb);
    free_str(pc->argstr);
    free_var(pc->args);
    free_str(pc->dobjstr);
    free_str(pc->prepstr);
    free_str(pc->iobjstr);
}
