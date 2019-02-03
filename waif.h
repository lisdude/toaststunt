/* Copyright (c) 1998-2002 Ben Jackson (ben@ben.com).  All rights reserved.
 *
 * Use and copying of this software and preparation of derivative works based
 * upon this software are permitted provided this copyright notice remains
 * intact.
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

/* $Id$ */

#ifndef WAIF_h
#define WAIF_h

#include <unordered_map>

#define WAIF_PROP_PREFIX	':'
#define WAIF_VERB_PREFIX	':'

extern std::unordered_map<Waif *, bool> recycled_waifs;

#ifdef WAIF_DICT
#define WAIF_INDEX_VERB ":_index"
#define WAIF_INDEXSET_VERB ":_set_index"
#endif /* WAIF_DICT */

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
