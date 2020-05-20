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

#include "config.h"
#include "db.h"
#include "quota.h"
#include "structures.h"

#ifdef OWNERSHIP_QUOTA
static const char *quota_name = "ownership_quota";
#endif

int
decr_quota(Objid player)
{
#ifdef OWNERSHIP_QUOTA
    db_prop_handle h;
    Var v;

    if (!valid(player))
        return 1;

    h = db_find_property(Var::new_obj(player), quota_name, &v);
    if (!h.ptr)
        return 1;

    if (v.type != TYPE_INT)
        return 1;

    if (v.v.num <= 0)
        return 0;

    v.v.num--;
    db_set_property_value(h, v);
#endif /* OWNERSHIP_QUOTA */
    return 1;
}

void
incr_quota(Objid player)
{
#ifdef OWNERSHIP_QUOTA
    db_prop_handle h;
    Var v;

    if (!valid(player))
        return;

    h = db_find_property(Var::new_obj(player), quota_name, &v);
    if (!h.ptr)
        return;

    if (v.type != TYPE_INT)
        return;

    v.v.num++;
    db_set_property_value(h, v);
#endif /* OWNERSHIP_QUOTA */
}
