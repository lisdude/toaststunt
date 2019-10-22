/******************************************************************************
  Copyright 2012 Todd Sundsted. All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

   1. Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.

   2. Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY TODD SUNDSTED ``AS IS'' AND ANY EXPRESS OR
  IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
  EVENT SHALL TODD SUNDSTED OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
  OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
  EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

  The views and conclusions contained in the software and documentation are
  those of the authors and should not be interpreted as representing official
  policies, either expressed or implied, of Todd Sundsted.
 *****************************************************************************/

#include <assert.h>

#include "functions.h"
#include "garbage.h"
#include "list.h"
#include "log.h"
#include "map.h"
#include "server.h"
#include "storage.h"
#include "utils.h"

/* See "Concurrent Cycle Collection in Reference Counted Systems",
 * (Bacon and Rajan, 2001) for a description of the cycle collection
 * algorithm and the colors.  This implementation differs from the
 * algorithm described because it has to deal with objects that
 * may/must be recycled -- anonymous objects aren't immediately
 * deleted when cyclic references are found.  Instead they are queued
 * up to have their `recycle()' verb called (if defined), which can
 * have the consequence of adding a reference to the object (albeit in
 * an invalid state).  This is called "finalization".  The
 * implementation below still identifies cyclic garbage, and colors
 * the values white.  However, instead of deleting the values, it
 * restores their refcounts and adds them to the same pending queue
 * that recycles anonymous objects that have no more references.
 */

int gc_roots_count = 0;
int gc_run_called = 0;

struct pending_recycle {
    struct pending_recycle *next;
    Var v;
};

static struct pending_recycle *pending_free = 0;
static struct pending_recycle *pending_head = 0;
static struct pending_recycle *pending_tail = 0;

#define FOR_EACH_ROOT(v, head, last)			\
    for (last = NULL, head = pending_head;		\
         head && (v = head->v, 1);			\
         last = head, head = head ? head->next : pending_head)

#define REMOVE_ROOT(head, last)				\
    do {						\
	if (pending_head == head)			\
	    pending_head = head->next;			\
	if (pending_tail == head && last)		\
	    pending_tail = last;			\
	else if (pending_tail == head)			\
	    pending_tail = NULL;			\
	if (last)					\
	    last->next = head->next;			\
	head->next = pending_free;			\
	pending_free = head;				\
	head = last;					\
    } while (0)

/* I'm sure there's a better way to do this.  Values are a union of
 * several _different_ kinds of pointers (see structures.h).  The
 * specific type isn't important to the garbage collector, so this
 * macro picks a flavor and casts the pointer to a pointer to a void.
 */
#define VOID_PTR(var) ((void *)((var).v.str))

static void
gc_stats(int color[])
{
    Var v;
    struct pending_recycle *head, *last;

    FOR_EACH_ROOT (v, head, last)
	color[gc_get_color(VOID_PTR(v))]++;
}

static void
gc_debug(const char *name)
{
    int color[7] = {0, 0, 0, 0, 0, 0, 0};

    gc_stats(color);

    oklog("GC: %s (green %d, yellow %d, black %d, gray %d, white %d, purple %d, pink %d)\n",
          name, color[0], color[1], color[2], color[3], color[4], color[5], color[6]);
}

static void
gc_add_root(Var v)
{
    if (!pending_free) {
	pending_free = (struct pending_recycle *)mymalloc(sizeof(struct pending_recycle), M_STRUCT);
	pending_free->next = NULL;
    }

    struct pending_recycle *next = pending_free;
    pending_free = next->next;

    next->v = v;
    next->next = NULL;

    if (pending_tail) {
	pending_tail->next = next;
	pending_tail = next;
    }
    else {
	pending_head = next;
	pending_tail = next;
    }

    gc_roots_count++;
}

/* corresponds to `PossibleRoot' in Bacon and Rajan */
void
gc_possible_root(Var v)
{
    GC_Color color;

    assert(v.is_collection());

    if ((color = gc_get_color(VOID_PTR(v))) != GC_PURPLE && color != GC_GREEN && color != GC_YELLOW) {
	gc_set_color(VOID_PTR(v), GC_PURPLE);
	if (!gc_is_buffered(VOID_PTR(v))) {
	    gc_set_buffered(VOID_PTR(v));
	    gc_add_root(v);
	}
    }
}

static inline int
is_not_green(Var v)
{
    void *p = VOID_PTR(v);
    return p && gc_get_color(p) != GC_GREEN;
}

typedef void (gc_func)(Var);

static int
do_obj(void *data, Var v)
{
    gc_func *fp = (gc_func *)data;
    if (v.is_collection() && is_not_green(v))
	(*fp)(v);
    return 0;
}

static int
do_list(Var v, void *data, int first)
{
    gc_func *fp = (gc_func *)data;
    if (v.is_collection() && is_not_green(v))
	(*fp)(v);
    return 0;
}

static int
do_map(Var k, Var v, void *data, int first)
{
    gc_func *fp = (gc_func *)data;
    if (v.is_collection() && is_not_green(v))
	(*fp)(v);
    return 0;
}

static void
for_all_children(Var v, gc_func *fp)
{
    if (v.is_object())
	db_for_all_propvals(v, do_obj, (void *)fp);
    else if (TYPE_LIST == v.type)
	listforeach(v, do_list, (void *)fp);
    else if (TYPE_MAP == v.type)
	mapforeach(v, do_map, (void *)fp);
}

/* corresponds to `MarkGray' in Bacon and Rajan */
static void
mark_gray(Var);

static void
cb_mark_gray(Var v)
{
    delref(VOID_PTR(v));
    mark_gray(v);
}

static void
mark_gray(Var v)
{
    if (gc_get_color(VOID_PTR(v)) != GC_GRAY) {
	gc_set_color(VOID_PTR(v), GC_GRAY);
	for_all_children(v, &cb_mark_gray);
    }
}

/* corresponds to `MarkRoots' in Bacon and Rajan */
static void
mark_roots(void)
{
#ifdef LOG_GC_STATS
    gc_debug("mark_roots");
#endif

    Var v;
    struct pending_recycle *head, *last;

    FOR_EACH_ROOT (v, head, last) {
	if (gc_get_color(VOID_PTR(v)) == GC_PURPLE)
	    mark_gray(v);
	else {
	    REMOVE_ROOT(head, last);
	    gc_clear_buffered(VOID_PTR(v));
	    if (gc_get_color(VOID_PTR(v)) == GC_BLACK && refcount(VOID_PTR(v)) == 0)
		aux_free(v);
	}
    }
}

/* corresponds to `ScanBlack' in Bacon and Rajan */
static void
scan_black(Var);

static void
cb_scan_black(Var v)
{
    addref(VOID_PTR(v));
    if (gc_get_color(VOID_PTR(v)) != GC_BLACK)
	scan_black(v);
}

static void
scan_black(Var v)
{
    gc_set_color(VOID_PTR(v), GC_BLACK);
    for_all_children(v, &cb_scan_black);
}

/* corresponds to `Scan' in Bacon and Rajan */
static void
scan(Var);

static void
cb_scan(Var v)
{
    scan(v);
}

static void
scan(Var v)
{
    if (gc_get_color(VOID_PTR(v)) == GC_GRAY) {
	if (refcount(VOID_PTR(v)) > 0)
	    scan_black(v);
	else {
	    gc_set_color(VOID_PTR(v), GC_WHITE);
	    for_all_children(v, &cb_scan);
	}
    }
}

/* corresponds to `ScanRoots' in Bacon and Rajan */
static void
scan_roots()
{
#ifdef LOG_GC_STATS
    gc_debug("scan_roots");
#endif

    Var v;
    struct pending_recycle *head, *last;

    FOR_EACH_ROOT (v, head, last)
	scan(v);
}

/* no correspondence in Bacon and Rajan */
static void
scan_white(Var);

static void
cb_scan_white(Var v)
{
    addref(VOID_PTR(v));
    if (gc_get_color(VOID_PTR(v)) != GC_PINK)
	scan_white(v);
}

static void
scan_white(Var v)
{
    gc_set_color(VOID_PTR(v), GC_PINK);
    for_all_children(v, &cb_scan_white);
}

/* no correspondence in Bacon and Rajan */
static void
restore_white()
{
#ifdef LOG_GC_STATS
    gc_debug("restore_white");
#endif

    Var v;
    struct pending_recycle *head, *last;

    FOR_EACH_ROOT (v, head, last) {
	if (gc_get_color(VOID_PTR(v)) == GC_WHITE)
	    scan_white(v);
    }
}

/* replaces `CollectWhite' in Bacon and Rajan */
static void
collect_white(Var);

static void
cb_collect_white(Var v)
{
    collect_white(v);
}

static void
collect_white(Var v)
{
    if (gc_get_color(VOID_PTR(v)) == GC_PINK && !gc_is_buffered(VOID_PTR(v))) {
	gc_set_color(VOID_PTR(v), GC_BLACK);
	for_all_children(v, &cb_collect_white);
	if (TYPE_ANON == v.type) {
	    assert(refcount(v.v.anon) != 0);
	    queue_anonymous_object(v);
	}
    }
}

/* replaces `CollectCycles' in Bacon and Rajan */
static void
collect_roots()
{
#ifdef LOG_GC_STATS
    gc_debug("collect_roots");
#endif

    Var v;
    struct pending_recycle *head, *last;

    FOR_EACH_ROOT (v, head, last) {
	REMOVE_ROOT(head, last);
	gc_clear_buffered(VOID_PTR(v));
	collect_white(v);
    }
}

void
gc_collect()
{
    if (!pending_head)
	return;

#ifdef LOG_GC_STATS
    oklog("GC: starting with %d root reference(s)\n", gc_roots_count);
#endif

    mark_roots();
    scan_roots();
    restore_white();
    collect_roots();

    gc_roots_count = 0;
    gc_run_called = 0;
}

/**** built in functions ****/

static package
bf_run_gc(Var arglist, Byte next, void *vdata, Objid progr)
{
    free_var(arglist);

    if (!is_wizard(progr))
        return make_error_pack(E_PERM);

    gc_run_called = 1;

    return no_var_pack();
}

static package
bf_gc_stats(Var arglist, Byte next, void *vdata, Objid progr)
{
    free_var(arglist);

    if (!is_wizard(progr))
        return make_error_pack(E_PERM);

    int color[7] = {0, 0, 0, 0, 0, 0, 0};

    gc_stats(color);

    Var k, v, r = new_map();

#define PACK_COLOR(c, i)	\
    k.type = TYPE_STR;		\
    k.v.str = str_dup(#c);	\
    v.type = TYPE_INT;		\
    v.v.num = color[i];		\
    r = mapinsert(r, k, v)

    PACK_COLOR(green, 0);
    PACK_COLOR(yellow, 1);
    PACK_COLOR(black, 2);
    PACK_COLOR(gray, 3);
    PACK_COLOR(white, 4);
    PACK_COLOR(purple, 5);
    PACK_COLOR(pink, 6);

#undef PACK_COLOR

    return make_var_pack(r);
}

void
register_gc(void)
{
    register_function("run_gc", 0, 0, bf_run_gc);
    register_function("gc_stats", 0, 0, bf_gc_stats);
}
