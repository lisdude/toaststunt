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

static char rcsid[] = "$Id: waif.c,v 1.6 1999/08/20 05:55:57 bjj Exp $";

#include "structures.h"
#include "bf_register.h"
#include "config.h"
#include "exceptions.h"
#include "functions.h"
#include "storage.h"
#include "streams.h"
#include "utils.h"
#include "db_private.h"
#include "db_io.h"
#include "waif.h"
#include "my-string.h"

static unsigned long waif_count = 0;

#define PROP_MAPPED(Mmap, Mbit)	((Mmap)[(Mbit) / 32] & (1 << ((Mbit) % 32)))
#define MAP_PROP(Mmap, Mbit) (Mmap)[(Mbit) / 32] |= 1 << ((Mbit) % 32)
#define N_MAPPABLE_PROPS (WAIF_MAPSZ * 32)

static int
count_set_bits(unsigned long x)
{
	register unsigned long i = x;	/* take no chances! */

	/* clever trick for adding bits together in parallel to count them */
	i = ((i & 0xAAAAAAAA) >> 1) + (i & ~0xAAAAAAAA);
	i = ((i & 0xCCCCCCCC) >> 2) + (i & ~0xCCCCCCCC);
	i = ((i & 0xF0F0F0F0) >> 4) + (i & ~0xF0F0F0F0);
	i = ((i & 0xFF00FF00) >> 8) + (i & ~0xFF00FF00);
	i = ((i & 0xFFFF0000) >> 16) + (i & ~0xFFFF0000);

	return i;
}

void
free_waif_propdefs(WaifPropdefs *wpd)
{
	int i;

	if (!wpd || --wpd->refcount > 0)
		return;

	/* Release the strings! */
	for (i = 0; i < wpd->length; ++i)
		free_str(wpd->defs[i].name);

	/* the actual defs are allocated right with the header. */
	myfree(wpd, M_WAIF_XTRA);
}

static WaifPropdefs *
ref_waif_propdefs(WaifPropdefs *wpd)
{
	++wpd->refcount;
	return wpd;
}

/* Find all of the .:props defined on an object or its ancestors and
 * build a useful structure for keeping track of them within waifs.
 *
 * Assume this will only be called when it should change or doesn't
 * already exist.  Spuriously calling this when waif_propdefs was
 * already up to date would not be performant.  If there's a reason
 * to do that, modify this function to leave waif_propdefs untouched
 * if it can, to avoid triggering propval remapping
 */
static void
gen_waif_propdefs(Object *o)
{
	WaifPropdefs *wpd;
	int cnt, i;
	Object *p;

	/* This is a lot like dbpriv_count_properties, except we're only
	 * counting relevant properties.
	 */
	cnt = 0;
	for(p = o; p; p = dbpriv_find_object(p->parent))
	    for (i = 0; i < p->propdefs.cur_length; ++i)
		if (p->propdefs.l[i].name[0] == WAIF_PROP_PREFIX)
		    ++cnt;
	
	wpd = (WaifPropdefs *) mymalloc(sizeof(WaifPropdefs) +
			(cnt-1) * sizeof(Propdef), M_WAIF_XTRA);
	/* must free this after to avoid getting the same pointer! */
	free_waif_propdefs(o->waif_propdefs);

	wpd->refcount = 1;
	wpd->length = cnt;
	cnt = 0;
	for(p = o; p; p = dbpriv_find_object(p->parent)) {
	    Propdef *pd = p->propdefs.l;

	    for (i = 0; i < p->propdefs.cur_length; ++i, ++pd)
		if (pd->name[0] == WAIF_PROP_PREFIX) {
		    wpd->defs[cnt].name = str_ref(pd->name);
		    wpd->defs[cnt].hash = pd->hash;
		    ++cnt;
		}
	}

	o->waif_propdefs = wpd;
}

/* Rename a property in a set of propdefs.
 */
void
waif_rename_propdef(Object *o, const char *old, const char *new)
{
	WaifPropdefs *wpd = o->waif_propdefs;
	int i;

	/* If the old AND new names are both waif properties just rename
	 * the entry in the propdefs.  That way existing waifs with
	 * up-to-date propdefs will be able to keep their values for this
	 * property.  To others which go through an update after this, the
	 * property value will appear to have been deleted and re-added (and
	 * the value will be lost).
	 *
	 * This is an ugly bug, but the Easy Way Out would be to cause   XXX
	 * them to all lose their values.  The Right Thing would be for  XXX
	 * them to all KEEP the values, but that's not going to be easy! XXX
	 */
	if (old[0] == WAIF_PROP_PREFIX && new[0] == WAIF_PROP_PREFIX) {
		for (i = 0; i < wpd->length; ++i)
			if (wpd->defs[i].name == old) {
				free_str(old);
				wpd->defs[i].name = str_ref(new);
				wpd->defs[i].hash = str_hash(new);
				return;
			}
		panic("waif_rename_propdef(): missing old propdef?");
	}

	/* Otherwise a waif property has been created or deleted.  This
	 * case is handled by update_waif_propdefs() like property addition
	 * or removal.
	 */
	free_waif_propdefs(wpd);
	o->waif_propdefs = NULL;
}

/* Figure out how many actual propvals are allocated for a waif, if any.
 * This is not waif->propdefs->length because the propvals array tries to
 * contain only non-clear values.
 */
static int
count_waif_propvals(Waif *w)
{
	int i, j;

	/* The peril of saving waifs of recycled classes was not recognized
	 * initially, so it is possible to have databases with invalid waifs
	 * saved.  On load their class objects will not have propdefs defined
	 * and the waif itself will not have enough information even to free
	 * it.  In that case we just leak the memory, and we will finally get
	 * rid of the waif on the next dump/restart.  That's better than a
	 * server panic!
	 */
	if (!w->propdefs)
		return 0;

	/* Properties which fit in the bitmap are unallocated until assigned
	 * (when they get an entry in the bitmap).  Values which don't fit
	 * just have to be allocated all the time.
	 */
	i = w->propdefs->length - N_MAPPABLE_PROPS;
	if (i < 0)
		i = 0;
	for (j = 0; j < WAIF_MAPSZ; ++j)
		i += count_set_bits(w->map[j]);
	return i;
}

static Var *
alloc_waif_propvals(Waif *w, int clear)
{
	int cnt;
	Var *p;

	cnt = count_waif_propvals(w);
	if (cnt == 0)
		return NULL;

	p = (Var *)mymalloc(cnt * sizeof(Var), M_WAIF_XTRA);
	if (clear)
		while (cnt--)
			p[cnt].type = TYPE_CLEAR;

	return p;
}

int
refers_to(Var target, Var key)
{
	int i;
	Var *p;

	switch((int) target.type) {
	case TYPE_LIST:
		if (target.v.list == key.v.list)
			return 1;
		for (i = 1; i <= target.v.list[0].v.num; ++i)
			if (refers_to(target.v.list[i], key))
				return 1;
		return 0;
	case TYPE_WAIF:
		if (target.v.waif == key.v.waif)
			return 1;
		p = target.v.waif->propvals;
		i = count_waif_propvals(target.v.waif);
		while (i-- > 0)
			if (refers_to(*p++, key))
				return 1;
		return 0;
	case TYPE_FLOAT:
		return target.v.fnum == key.v.fnum;
	case TYPE_STR:
		return target.v.str == key.v.str;
	}
	return 0;
}

Var
new_waif(Objid class, Objid owner)
{
	Object *classp;
	Var res;
	int i;

	classp = dbpriv_find_object(class);
	if (!classp)
		panic("new_waif() called with invalid class");

	res.type = TYPE_WAIF;
	res.v.waif = (Waif *) mymalloc(sizeof(Waif), M_WAIF);
	res.v.waif->class = class;
	res.v.waif->owner = owner;
	if (!classp->waif_propdefs)
		gen_waif_propdefs(classp);
	res.v.waif->propdefs = ref_waif_propdefs(classp->waif_propdefs);
	for (i = 0; i < WAIF_MAPSZ; ++i)
		res.v.waif->map[i] = 0;
	res.v.waif->propvals = alloc_waif_propvals(res.v.waif, 1);
	++waif_count;

	return res;
}

/* Find the offset into the waif properties for the class (pidx) and
 * the offset into this waif's propvals.
 */
static int
find_propval_offset(Waif *w, const char *name, int *pidx)
{
	int i, j, idx;
	int hash = str_hash(name);
	struct Propdef *pd;

	/* First find the offset into the list of possible properties
	 */
	for (i = 0,pd = w->propdefs->defs; i < w->propdefs->length; ++i, ++pd)
		if (pd->hash == hash && !mystrcasecmp(pd->name, name))
			goto found;
	return -2;

found:
	if (pidx)
		*pidx = i;

	/* Now determine the offset into the acutal property values.  Use a
	 * bitmap that indicates which values aren't clear, so we don't have
	 * to represent undifferentiated (TYPE_CLEAR) values.  Since the map
	 * is of limited size, if there are more properties than will fit in
	 * the map, the rest are always represented, even if they are clear.
	 * If you make a WAIF class with more than N_MAPPABLE_PROPS (currently
	 * 96) properties then empty waifs will just be bigger.
	 */
	if (i >= N_MAPPABLE_PROPS) {
		/* count all of the map words to find the start of the
		 * unmappable propvals which are always allocated.
		 */
		for (idx = j = 0; j < WAIF_MAPSZ; ++j)
			idx += count_set_bits(w->map[j]);
		return i - N_MAPPABLE_PROPS + idx;
	} else if (!PROP_MAPPED(w->map, i)) {
		/* property unmapped, so it's clear */
		return -1;
	} else {
		/* count completely full map words, then count the partial
		 * word to the right of the bit we found
		 */
		for (idx = j = 0; j < i / 32; ++j)
			idx += count_set_bits(w->map[j]);
		if (i % 32 != 0) {
			unsigned long mask = ~0U;
			mask >>= 32 - i % 32;
			idx += count_set_bits(w->map[j] & mask);
		}
		return idx;
	}
}

/* We want to write into an unmapped propval, so map it and adjust the
 * propval array accordingly.
 */
static int
alloc_propval_offset(Waif *w, int idx) 
{
	int result = -1;	/* avoid warning */
	Var *newpv, *old, *new;
	int i;

	/* assert(idx < N_MAPPABLE_PROPS) */
	if (PROP_MAPPED(w->map, idx))
		panic("alloc_propval_offset for already allocated idx");
	MAP_PROP(w->map, idx);

	newpv = alloc_waif_propvals(w, 0);

	old = w->propvals;
	new = newpv;
	for (i = 0; i < N_MAPPABLE_PROPS; ++i)
		if (PROP_MAPPED(w->map, i)) {
			if (i == idx) {
				new->type = TYPE_CLEAR;
				result = new - newpv;
				new++;
			} else
				 *new++ = *old++;
		}
	for (; i < w->propdefs->length; ++i)
		 *new++ = *old++;
	if (w->propvals)
		myfree(w->propvals, M_WAIF_XTRA);
	w->propvals = newpv;
	return result;
}

/* When class object properties change, waifs are not immediately updated.
 * The next time they're accessed their old propdef list is reconciled with
 * the current one and their propvals adjusted accordingly.
 */
static void
update_waif_propdefs(Waif *waif)
{
	WaifPropdefs *old;
	Object *classp = dbpriv_find_object(waif->class);
	Propdef *a, *a_end, *b, *b_end;
	Var *xp, *ov;
	int i, cnt;
	static Var *xfer;
	static int xfer_sz;

	/* If the class has been recycled we're invalid!  Destroy the
	 * properties and release our reference to the old propvals.
	 */
	if (!classp) {
		cnt = count_waif_propvals(waif);
		free_waif_propdefs(waif->propdefs);
		waif->propdefs = NULL;
		for (i = 0; i < cnt; ++i)
			free_var(waif->propvals[i]);
		if (waif->propvals) {
			myfree(waif->propvals, M_WAIF_XTRA);
			waif->propvals = NULL;
		}
		return;
	}

	/* Compare pointers to see if we're in sync.  Changes to the
	 * class object hierarchy will cause this to be NULL'd so that
	 * we'll get a mismatch and cause a regen as needed.
	 */
	if (waif->propdefs == classp->waif_propdefs)
		return;

	if (!classp->waif_propdefs) {
		/* This is true between the time that a new property is
		 * added (or one is deleted) and the first lazy waif
		 * update (here) or a waif creation.  The prop update
		 * doesn't know if there are any WAIF instances when
		 * it updates the object.
		 */
		gen_waif_propdefs(classp);
	}

	old = waif->propdefs;
	waif->propdefs = ref_waif_propdefs(classp->waif_propdefs);

	/* If the waif is totally undifferentiated, there's no need for
	 * any remapping, is there?
	 */
	if (!waif->propvals) {
		free_waif_propdefs(old);
		/* In the rare case that the update is happening because
		 * the parent just added a prop which takes us past the
		 * mappable size, must allocate propvals here.  You could
		 * optimize this case but it's probably pointless.
		 */
		waif->propvals = alloc_waif_propvals(waif, 1);
		return;
	}

	/* Make the transfer var list (xfer) big enough to hold all
	 * propvals in the new object.
	 */
	cnt = waif->propdefs->length;
	if (xfer_sz < cnt) {
		xfer_sz = cnt;
		if (xfer)
			myfree(xfer, M_WAIF_XTRA);
		xfer = (Var *)mymalloc(xfer_sz * sizeof(Var), M_WAIF_XTRA);
	}
	/* they're clear unless filled explicitly below */
	for (i = 0; i < cnt; ++i)
		xfer[i].type = TYPE_CLEAR;

	/* Get back in sync by iterating over the old and new lists and
	 * looking for insertions and deletions.  We don't need to search
	 * for propdefs by name because if the pointer to the name or the
	 * order has changed then it was deleted and recreated, and we
	 * want to revert to a clear value in that case anyway.  Remember
	 * the new property name can't get the same pointer because we hold
	 * a ref to it which means even though the object was done with it
	 * it couldn't be freed.
	 */
	ov = waif->propvals;		/* old vals */
	xp = xfer;			/* copy old vals with new spacing */
	a = old->defs;			/* old propdefs */
	a_end = a + old->length;	/* end of old propdefs */
	b = waif->propdefs->defs;	/* new propdefs */
	b_end = b + cnt;		/* end of new propdefs */

	while (b < b_end) {
		Propdef *tmp;

		if (a >= a_end) {
			/* There can't be any more old vals because
			 * we're out of old defs!
			 */
			break;
		} else if (a->name == b->name) {
			/* Unchanged, keep going, copy propval if any */
			int idx = a - old->defs;

			if (idx >= N_MAPPABLE_PROPS ||
			    PROP_MAPPED(waif->map, idx))
				*xp = *ov++;
			++xp; ++a; ++b;
			continue;
		}

		/* search A to find out if a group of propdefs has been
		 * inserted in B or a group of propdefs has been deleted
		 * from A.
		 */
		for (tmp = a + 1; tmp < a_end; ++tmp)
			if (tmp->name == b->name)
				break;
		if (tmp != a_end) {
			/* If the above loop found b, then everything between
			 * a and tmp-1 must have been deleted.  All we have
			 * to do is free our values for those props, if we
			 * have any.
			 */
			for (i = a - old->defs; a < tmp; ++a, ++i) {
				if (i >= N_MAPPABLE_PROPS ||
				    PROP_MAPPED(waif->map, i))
					free_var(*ov++);
			}
			/* now a and b match.  continue and let the next
			 * pass catch this so it can copy the propval.
			 */
			continue;
		}
		/* At this point, b was not found in the rest of a, so it
		 * must be inserted.  Nothing we have to do here but skip
		 * a slot in the xfer propvals.
		 */
		++b;
		++xp;
	}
	for (i = a - old->defs; a < a_end; ++a, ++i) {
		/* We're out of new defs, if there are leftover old defs
		 * free those values, if any.
		 */
		if (i >= N_MAPPABLE_PROPS ||
		    PROP_MAPPED(waif->map, i))
			free_var(*ov++);
	}

	/* Ok!  Now we have all the propvals we want to keep in a boring
	 * old flat array.  Reset the waif, generate the map for the mapped
	 * propvals, allocate the new space and pack them down.
	 */
	for (i = 0; i < WAIF_MAPSZ; ++i)
		waif->map[i] = 0U;
	for (i = 0; i < cnt && i < N_MAPPABLE_PROPS; ++i)
		if (xfer[i].type != TYPE_CLEAR)
			MAP_PROP(waif->map, i);

	if (waif->propvals)
		myfree(waif->propvals, M_WAIF_XTRA);
	ov = waif->propvals = alloc_waif_propvals(waif, 1);
	for (i = 0; i < cnt && i < N_MAPPABLE_PROPS; ++i)
		if (xfer[i].type != TYPE_CLEAR)
			*ov++ = xfer[i];
	for (; i < cnt; ++i)
		*ov++ = xfer[i];

	free_waif_propdefs(old);
}

/* Called from complex_free_var()
 */
void
free_waif(Waif *waif)
{
	int i, cnt;

	/* assert(refcount(waif) == 0) */
	cnt = count_waif_propvals(waif);
	free_waif_propdefs(waif->propdefs);
	for (i = 0; i < cnt; ++i)
		free_var(waif->propvals[i]);
	if (waif->propvals)
		myfree(waif->propvals, M_WAIF_XTRA);
	myfree(waif, M_WAIF);
	--waif_count;
}

/* Called from complex_var_dup().  Callers of var_dup() generally already
 * know the type of the var, so this will never happen unless someone adds
 * an explicit call expecting to do this.
 */
Waif *
dup_waif(Waif *waif)
{
	update_waif_propdefs(waif);
	panic("can't dup waif yet");
	return NULL;
}

static package
bf_new_waif(Var arglist, Byte next, void *vdata, Objid progr)
{
	free_var(arglist);

	if (!valid(caller()))
		return make_error_pack(E_INVIND);
	return make_var_pack(new_waif(caller(), progr));
}

void
register_waif()
{
	register_function("new_waif", 0, 0, bf_new_waif);
}

/* Waif proprety permissions are derived from the class object's property
 * definition, except that in the case of +c properties waif.owner is
 * considered the owner of the property rather than using the owner in the
 * db_prop_handle.
 */
static int
waif_property_allows(Waif *w, db_prop_handle h, Objid progr, db_prop_flag flag)
{
	int flags = db_property_flags(h);

	return (flags & flag) ||
	    ((flags & PF_CHOWN) ? w->owner : db_property_owner(h)) == progr ||
	    is_wizard(progr);
}

/* called from execute.c run() when reading a property value.  This returns
 * the prop ALREADY REFERENCED because the interpreter is going to free OBJ
 * immediately upon return, and if that is the last ref the prop returned
 * could already be invalid.
 */
enum error
waif_get_prop(Waif *w, const char *name, Var *prop, Objid progr)
{
	db_prop_handle h;
	static Stream *s;
	int idx;

	update_waif_propdefs(w);

	if (!mystrcasecmp(name, "owner")) {
		prop->type = TYPE_OBJ;
		prop->v.obj = w->owner;
		return E_NONE;
	} else if (!mystrcasecmp(name, "class")) {
		prop->type = TYPE_OBJ;
		prop->v.obj = w->class;
		return E_NONE;
	} else if (!valid(w->class))
		return E_INVIND;

	/* There doesn't seem to be any way around constructing the
	 * prefixed name when doing the db_find_property later.
	 */
	if (!s)
		s = new_stream(50);
	stream_add_char(s, WAIF_PROP_PREFIX);
	stream_add_string(s, name);
	name = reset_stream(s);

	/* First find the in offset into the waif's own propvals for
	 * this property.  This will tell us more quickly if it doesn't
	 * exist.
	 */
	idx = find_propval_offset(w, name, NULL);
	switch (idx) {
	case -2:
		return E_PROPNF;
	case -1:
		/* clear, note that */
		prop->type = TYPE_CLEAR;
		break;
	default:
		*prop = w->propvals[idx];
		break;
	}

	/* If it exists, we have to find the class's def of it to get
	 * flags and owner.  Get the value from the class object if
	 * the waif's value is clear.
	 */
	h = db_find_property(w->class, name,
		prop->type == TYPE_CLEAR ? prop : NULL);
	if (!h.ptr)
		panic("waif propdef update failed in waif_get_prop");
	else if (h.built_in != BP_NONE)
		panic("built-in property beginning with WAIF_PROP_PREFIX?!");
	else if (!waif_property_allows(w, h, progr, PF_READ))
		return E_PERM;

	*prop = var_ref(*prop);
	return E_NONE;
}

/* called from execute.c run() when setting a property value
 */
enum error
waif_put_prop(Waif *w, const char *name, Var val, Objid progr)
{
	db_prop_handle h;
	static Stream *s;
	int idx, pdef_idx;
	Var *dest;

	update_waif_propdefs(w);

	if (!mystrcasecmp(name, "owner") || !mystrcasecmp(name, "class"))
		/* FYI, allowing these assignments would work, because
		 * .owner can be anything without changing the waif, and
		 * .class can change and the magic of update_waif_propdefs()
		 * will make it work just like chparent (I recommend an
		 * explicit call at the chparent rather than really being
		 * lazy in this case).
		 */
		return E_PERM;
	else if (!valid(w->class))
		return E_INVIND;

	/* There doesn't seem to be any way around constructing the
	 * prefixed name when doing the db_find_property later.
	 */
	if (!s)
		s = new_stream(50);
	stream_add_char(s, WAIF_PROP_PREFIX);
	stream_add_string(s, name);
	name = reset_stream(s);

	/* First find the in offset into the waif's own propvals for
	 * this property.  This will tell us more quickly if it doesn't
	 * exist.
	 */
	idx = find_propval_offset(w, name, &pdef_idx);
	switch (idx) {
	case -2:
		return E_PROPNF;
	case -1:
		/* clear, we'll need to allocate a slot for it later.  It
		 * would be cleaner to do it here but we could still fail
		 * with E_PERM so let's hold off.
		 */
		dest = NULL;
		break;
	default:
		dest = &w->propvals[idx];
		break;
	}

	/* If it exists, we have to find the class's def of it to get
	 * flags and owner.
	 */
	h = db_find_property(w->class, name, NULL);
	if (!h.ptr)
		panic("waif propdef update failed in waif_put_prop");
	else if (h.built_in != BP_NONE)
		panic("built-in property beginning with WAIF_PROP_PREFIX?!");
	else if (!waif_property_allows(w, h, progr, PF_WRITE))
		return E_PERM;

	/* Could do this sooner, but it doesn't really matter.  Disallow
	 * self-reference.
	 */
	{
		Var me;

		me.type = TYPE_WAIF;
		me.v.waif = w;
		if (refers_to(val, me))
			return E_RECMOVE;
	}

	if (dest) {
		/* This is the easy case, there's already a slot for it.
		 * Just fill it in.
		 */
		free_var(*dest);
		*dest = var_ref(val);
	} else {
		/* This will require mapping a new propval slot.
		 */
		idx = alloc_propval_offset(w, pdef_idx);
		w->propvals[idx] = var_ref(val);
	}
	return E_NONE;
}

/* Called from value_bytes()
 */
int
waif_bytes(Waif *w)
{
	int len;
	int cnt;

	update_waif_propdefs(w);

	/* never need to count the propdefs becuase we're now guaranteed to
	 * be sharing that with the class object which is billed for that
	 * space
	 */
	len = sizeof(Waif);
	cnt = count_waif_propvals(w);
	while (cnt--)
		len += value_bytes(w->propvals[cnt]);
	return len;
}

static Waif **saved_waifs;
static unsigned long n_saved_waifs;

void
waif_before_saving()
{
	int size;

	size = sizeof(Waif *) * waif_count;
	saved_waifs = (Waif **) mymalloc(size, M_WAIF_XTRA);
	memset(saved_waifs, 0, size);
	n_saved_waifs = 0;
}

void
write_waif(Var v)
{
	Waif *w = v.v.waif;
	int index;
	int i, len;
	unsigned long map[WAIF_MAPSZ];
	Var *val;

	/* doesn't matter if it's a random number, because then the reverse
	 * mapping will be wrong and we'll just ignore the index.
	 */
	index = w->waif_save_index;
	if (index < waif_count && saved_waifs[index] == w) {
		/* just refer to an old one */
		dbio_printf("r %d\n.\n", index);	/* XXX 1.9 terminator*/
		return;
	}

	/* Has to be after the table check because in forked checkpoints
	 * the saved waif will be trashed.  Has to be before the next
	 * bit because that's where we trash it!
	 */
	update_waif_propdefs(w);

	/* Save the propval map info (for forked checkpoints we'll have to
	 * clobber it before saving to stash the index).  Then allocate a
	 * table index for this waif and record it.
	 */
	memcpy(map, w->map, sizeof(map));
	w->waif_save_index = index = n_saved_waifs++;
	saved_waifs[index] = w;

	/* actually write this one!
	 */
	dbio_printf("c %d\n", index);
	dbio_write_objid(w->class);
	dbio_write_objid(w->owner);

	/* Write out all of the non-clear properties.  Don't rely on mapsize
	 * in output format so that it can be changed between dbsave/load
	 * without ill effects.
	 */
	len = w->propdefs ? w->propdefs->length : 0;
	dbio_write_num(len);
	val = w->propvals;
	for (i = 0; i < len; ++i) {
		if ((i < N_MAPPABLE_PROPS) ? PROP_MAPPED(map, i) :
		    val->type != TYPE_CLEAR) {
			dbio_printf("%d\n", i);
			/* Look out!  This could recurse someday.  That's
			 * why the saved_waifs table has to be up-to-date
			 * at this point so if we come back here we write
			 * out a ref rather than another copy.
			 */
			dbio_write_var(*val);
		}
		/* step through the propvals, different rule than above */
		if (i >= N_MAPPABLE_PROPS || PROP_MAPPED(map, i))
			++val;
	}
	dbio_write_num(-1);	/* our terminator */
	dbio_printf(".\n");	/* XXX 1.9 terminator? */
}

void
waif_after_saving()
{
	myfree(saved_waifs, M_WAIF_XTRA);
	if (n_saved_waifs != waif_count)
		fprintf(stderr, "WARN: waif_count != n_saved_waifs!\n");
}

void
waif_before_loading()
{
	int size;

	/* woop woop gross variable reuse */
	n_saved_waifs = 256;
	size = sizeof(Waif *) * n_saved_waifs;
	saved_waifs = (Waif **) mymalloc(size, M_WAIF_XTRA);
	memset(saved_waifs, 0, size);
}

Var
read_waif()
{
	Var res;
	char ref;
	unsigned int index;
	Waif *w;
	Var packable[N_MAPPABLE_PROPS], *p, *q;
	int i, cnt, size, cur, propdefs_length;

	/* WAIFs are saved as _r_eferences or _c_reations.  The first
	 * occurance in a db should be a C, subsequent ones R.
	 */
	dbio_scanf("%c %u\n", &ref, &index);
	if (ref == 'r') {
		(void) dbio_read_string();	/* XXX 1.9 terminator */
		w = saved_waifs[index];
		if (!w)
			panic("waif ref to unsaved waif!");

		res.type = TYPE_WAIF;
		res.v.waif = w;
		return var_ref(res);
	}

	/* Extend the table by doubling its size if we've filled it.
	 */
	if (waif_count == n_saved_waifs) {
		int size;

		n_saved_waifs *= 2;
		size = sizeof(Waif *) * n_saved_waifs;
		saved_waifs = (Waif **) myrealloc(saved_waifs, size,
						M_WAIF_XTRA);
		size /= 2;
		memset((char *)saved_waifs + size, 0, size);
	}

	/* These have to line up or subsequent refs will not get the right
	 * waif.
	 */
	if (index != waif_count)
		panic("WAIF index mismatch");

	/* I'd like to use new_waif() here but this is so hacked up it
	 * seemed silly to try and overload new_waif() to do it.
	 */
	res.type = TYPE_WAIF;
	res.v.waif = (Waif *) mymalloc(sizeof(Waif), M_WAIF);
	saved_waifs[waif_count++] = w = res.v.waif;
	res.v.waif->propdefs = NULL;
	res.v.waif->class = dbio_read_objid();
	res.v.waif->owner = dbio_read_objid();
	for (i = 0; i < WAIF_MAPSZ; ++i)
		res.v.waif->map[i] = 0;
	propdefs_length = dbio_read_num();

	/* Read propvals into the `packable' array until we run out of
	 * mappable props, then allocate the finished value array and
	 * start reading into there
	 */
	cnt = 0;
	p = packable;
	while ((cur = dbio_read_num()) < N_MAPPABLE_PROPS && cur > -1) {
		*p++ = dbio_read_var();
		MAP_PROP(w->map, cur);
		cnt++;
	}
	size = cnt;
	if (propdefs_length > N_MAPPABLE_PROPS)
		size += propdefs_length - N_MAPPABLE_PROPS;
	w->propvals = (Var *)mymalloc(size * sizeof(Var), M_WAIF_XTRA);
	for (p = packable, q = w->propvals, i = 0; i < cnt; ++i)
		*q++ = *p++;

	/* Whew, finally got those.  Ok, the rest are stored in memory
	 * no matter what, so we've already allocated them and can read
	 * straight into the target array.  The disk db may still be
	 * sparse, however.
	 */
	i = N_MAPPABLE_PROPS;
	/* q from above */
	if (cur >= 0) do {
		/* clear out ones we didn't save */
		for (; i < cur; ++i, ++q)
			q->type = TYPE_CLEAR;
		*q++ = dbio_read_var();
		++i;
	} while ((cur = dbio_read_num()) >= 0);
	/* clear out ones we didn't save */
	for (; i < propdefs_length; ++i, ++q)
		q->type = TYPE_CLEAR;

	(void) dbio_read_string();	/* XXX 1.9 terminator */

	return res;
}

void
waif_after_loading()
{
	int i;

	/* This part is no fun.  Now that all of the objs are loaded, go
	 * generate waif_propdefs for them and backfill all of the waifs
	 * we loaded.  If this is abominably slow due to memory thrashing,
	 * I recommend qsort()ing the pointers.
	 */
	for (i = 0; i < waif_count; ++i) {
		Waif *w = saved_waifs[i];
		Object *o = dbpriv_find_object(w->class);

		if (!o) {
			/* see count_waif_propvals() for the workaround to
			 * this problem.  For newer databases the saving of
			 * propvals will be suppressed, so this won't matter.
			 */
			continue;
		}
		if (!o->waif_propdefs)
			gen_waif_propdefs(o);
		w->propdefs = ref_waif_propdefs(o->waif_propdefs);
	}
	myfree(saved_waifs, M_WAIF_XTRA);
}
