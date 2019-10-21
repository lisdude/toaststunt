/******************************************************************************
  Copyright (c) 1992 Xerox Corporation.  All rights reserved.
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

#include "options.h"

#if NETWORK_PROTOCOL == NP_SINGLE
#  include "net_single.cc"
#else
#  include "net_multi.cc"
#endif

Var
network_connection_options(network_handle nh, Var list)
{
    CONNECTION_OPTION_LIST(NETWORK_CO_TABLE, nh, list);
}

int
network_connection_option(network_handle nh, const char *option, Var * value)
{
    CONNECTION_OPTION_GET(NETWORK_CO_TABLE, nh, option, value);
}

int
network_set_connection_option(network_handle nh, const char *option, Var value)
{
    CONNECTION_OPTION_SET(NETWORK_CO_TABLE, nh, option, value);
}
