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

#include "config.h"

typedef struct {
    int start, end;
} Match_Indices;

typedef struct {
    void *ptr;
} Pattern;

typedef enum {
    MATCH_SUCCEEDED, MATCH_FAILED, MATCH_ABORTED
} Match_Result;

extern Pattern new_pattern(const char *pattern, int case_matters);
extern Match_Result match_pattern(Pattern p, const char *string,
				Match_Indices * indices, int is_reverse);
extern void free_pattern(Pattern p);
