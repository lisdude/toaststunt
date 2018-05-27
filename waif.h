/* Copyright (c) 1998 Ben Jackson (ben@ben.com).  All rights reserved.
 *
 * Permission is hereby granted to use this software for private, academic
 * and non-commercial use. No commercial or profitable use of this
 * software may be made without the prior permission of the author,
 * Ben Jackson <ben@ben.com>.
 *
 * THIS SOFTWARE IS PROVIDED BY BEN J JACKSON ``AS IS'' AND ANY
 * EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT BEN J JACKSON BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* $Id: waif.h,v 1.3 1999/08/20 05:55:19 bjj Exp $ */

#ifndef WAIF_h
#define WAIF_h

#define WAIF_PROP_PREFIX	':'
#define WAIF_VERB_PREFIX	':'

#include "db_private.h"

typedef struct WaifPropdefs {
	int		refcount;
	int		length;
	struct Propdef	defs[1];
} WaifPropdefs;

extern void free_waif(Waif *);
extern Waif *dup_waif(Waif *);
extern enum error waif_get_prop(Waif *, const char *, Var *, Objid progr);
extern enum error waif_put_prop(Waif *, const char *, Var, Objid progr);
extern int waif_bytes(Waif *);
extern void waif_before_saving();
extern void waif_after_saving();
extern void waif_before_loading();
extern void waif_after_loading();
extern void write_waif(Var);
extern Var read_waif();
extern void free_waif_propdefs(WaifPropdefs *);                                 
extern void waif_rename_propdef(Object *, const char *, const char *);

#endif /* WAIF_h */
