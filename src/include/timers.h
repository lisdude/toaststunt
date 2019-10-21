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

#ifndef Timers_H
#define Timers_H 1

#include "my-time.h"

typedef int Timer_ID;
typedef void *Timer_Data;
typedef void (*Timer_Proc) (Timer_ID, Timer_Data);

extern Timer_ID set_timer(unsigned, Timer_Proc, Timer_Data);
extern Timer_ID set_virtual_timer(unsigned, Timer_Proc, Timer_Data);
extern int cancel_timer(Timer_ID);
extern void reenable_timers(void);
extern unsigned timer_wakeup_interval(Timer_ID);
extern void timer_sleep(unsigned seconds);
extern int virtual_timer_available();

#endif				/* !Timers_H */
