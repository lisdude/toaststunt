#!/bin/sh

# Copyright (c) 1992, 1994, 1995, 1996 Xerox Corporation.  All rights reserved.
# Portions of this code were written by Stephen White, aka ghond.
# Use and copying of this software and preparation of derivative works based
# upon this software are permitted.  Any distribution of this software or
# derivative works must comply with all applicable United States export
# control laws.  This software is made available AS IS, and Xerox Corporation
# makes no warranty about the software, its performance or its conformity to
# any specification.  Any person obtaining a copy of this software is requested
# to send their name and post office or electronic mail address to:
#   Pavel Curtis
#   Xerox PARC
#   3333 Coyote Hill Rd.
#   Palo Alto, CA 94304
#   Pavel@Xerox.Com

if [ $# -lt 1 -o $# -gt 2 ]; then
	echo 'Usage: restart dbase-prefix [port]'
	exit 1
fi

if [ ! -r $1.db ]; then
	echo "Unknown database: $1.db"
	exit 1
fi

if [ -r $1.db.new ]; then
	mv $1.db $1.db.old
	mv $1.db.new $1.db
	rm -f $1.db.old.Z
	compress $1.db.old &
fi

if [ -f $1.log ]; then
	cat $1.log >> $1.log.old
	rm $1.log
fi

echo `date`: RESTARTED >> $1.log
./moo $1.db $1.db.new $2 2>&1 
