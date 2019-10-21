/******************************************************************************
  Copyright (c) 1995, 1996 Xerox Corporation.  All rights reserved.
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
#include "my-stdlib.h"
#include "my-string.h"

#include "config.h"
#include "pattern.h"
#include "storage.h"
#include "streams.h"

extern "C" {
#include "regexpr.h"
}

static char casefold[256];

static void
init_casefold_once(void)
{
    if (casefold[1] != 1) {
	int i;

	for (i = 0; i < 256; i++)
	    casefold[i] = isupper(i) ? tolower(i) : i;
    }
}

static const char *
translate_pattern(const char *pattern, int *tpatlen)
{
    /* Translate a MOO pattern into a more standard syntax.  Effectively, this
     * just involves converting from `%' escapes into `\' escapes.
     */

    static Stream *s = 0;
    const char *p = pattern;
    char c;

    if (!s)
	s = new_stream(100);

    while (*p) {
	switch (c = *p++) {
	case '%':
	    c = *p++;
	    if (!c)
		goto fail;
	    else if (strchr(".*+?[^$|()123456789bB<>wW", c))
		stream_add_char(s, '\\');
	    stream_add_char(s, c);
	    break;
	case '\\':
	    stream_add_string(s, "\\\\");
	    break;
	case '[':
	    /* Any '%' or '\' characters inside a charset should be copied
	     * over without translation. */
	    stream_add_char(s, c);
	    c = *p++;
	    if (c == '^') {
		stream_add_char(s, c);
		c = *p++;
	    }
	    /* This is the only place a ']' can appear and not be the end of
	     * the charset. */
	    if (c == ']') {
		stream_add_char(s, c);
		c = *p++;
	    }
	    while (c && c != ']') {
		stream_add_char(s, c);
		c = *p++;
	    }
	    if (!c)
		goto fail;
	    else
		stream_add_char(s, c);
	    break;
	default:
	    stream_add_char(s, c);
	    break;
	}
    }

    *tpatlen = stream_length(s);
    return reset_stream(s);

  fail:
    reset_stream(s);
    return 0;
}

#define MOO_SYNTAX	(RE_CONTEXT_INDEP_OPS)

Pattern
new_pattern(const char *pattern, int case_matters)
{
    int tpatlen = -1;
    const char *tpattern = translate_pattern(pattern, &tpatlen);
    regexp_t buf = (regexp_t)mymalloc(sizeof(*buf), M_PATTERN);
    Pattern p;

    init_casefold_once();

    buf->buffer = 0;
    buf->allocated = 0;
    buf->translate = case_matters ? 0 : casefold;
    re_set_syntax(MOO_SYNTAX);

    if (tpattern
	&& !re_compile_pattern((char *)tpattern, tpatlen, buf)) {
	buf->fastmap = (char *)mymalloc(256 * sizeof(char), M_PATTERN);
	re_compile_fastmap(buf);
	p.ptr = buf;
    } else {
	if (buf->buffer)
	    free(buf->buffer);
	myfree(buf, M_PATTERN);
	p.ptr = 0;
    }

    return p;
}

Match_Result
match_pattern(Pattern p, const char *string, Match_Indices * indices,
	      int is_reverse)
{
    regexp_t buf = (regexp_t)p.ptr;
    int len = strlen(string);
    int i;
    struct re_registers regs;

    switch (re_search(buf, (char *)string, len,
		      is_reverse ? len : 0,
		      is_reverse ? -len : len,
		      &regs)) {
    default:
	for (i = 0; i < 10; i++) {
	    /* Convert from 0-based open interval to 1-based closed one. */
	    indices[i].start = regs.start[i] + 1;
	    indices[i].end = regs.end[i];
	}
	return MATCH_SUCCEEDED;
    case -1:
	return MATCH_FAILED;
    case -2:
	return MATCH_ABORTED;
    }
}

void
free_pattern(Pattern p)
{
    regexp_t buf = (regexp_t)p.ptr;

    if (buf) {
	free(buf->buffer);
	myfree(buf->fastmap, M_PATTERN);
	myfree(buf, M_PATTERN);
    }
}
