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

#include "bf_register.h"
#include "collection.h"
#include "functions.h"
#include "list.h"
#include "map.h"
#include "utils.h"

struct ismember_data {
    int i;
    Var value;
    int case_matters;
};

static int
do_map_iteration(Var key, Var value, void *data, int first)
{
    struct ismember_data *ismember_data = (struct ismember_data *)data;

    if (equality(value, ismember_data->value, ismember_data->case_matters)) {
	return ismember_data->i;
    }

    ismember_data->i++;

    return 0;
}

int
ismember(Var lhs, Var rhs, int case_matters)
{
    if (rhs.type == TYPE_LIST) {
	int i;

	for (i = 1; i <= rhs.v.list[0].v.num; i++) {
	    if (equality(lhs, rhs.v.list[i], case_matters)) {
		return i;
	    }
	}

	return 0;
    } else if (rhs.type == TYPE_MAP) {
	struct ismember_data ismember_data;

	ismember_data.i = 1;
	ismember_data.value = lhs;
	ismember_data.case_matters = case_matters;

	return mapforeach(rhs, do_map_iteration, &ismember_data);
    } else {
	return 0;
    }
}

/**** built in functions ****/

static package
bf_is_member(Var arglist, Byte next, void *vdata, Objid progr)
{
    Var r;
    Var rhs = arglist.v.list[2];

    if (rhs.type != TYPE_LIST && rhs.type != TYPE_MAP) {
	free_var(arglist);
	return make_error_pack(E_INVARG);
    }

    bool case_matters = arglist.v.list[0].v.num < 3 || (arglist.v.list[0].v.num >= 3 && is_true(arglist.v.list[3]));

    r.type = TYPE_INT;
    r.v.num = ismember(arglist.v.list[1], rhs, case_matters);
    free_var(arglist);
    return make_var_pack(r);
}

void
register_collection(void)
{
    register_function("is_member", 2, 3, bf_is_member, TYPE_ANY, TYPE_ANY, TYPE_INT);
}
