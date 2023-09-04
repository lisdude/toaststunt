/******************************************************************************
  Copyright 2010 Todd Sundsted. All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice,
       this list of conditions and the following disclaimer.

    2. Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.

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

#include <string.h>

#include "functions.h"
#include "list.h"
#include "log.h"
#include "map.h"
#include "server.h"
#include "storage.h"
#include "structures.h"
#include "utils.h"

/*
  Red Black balanced tree library

    > Created (Julienne Walker): August 23, 2003
    > Modified (Julienne Walker): March 14, 2008

  This code is in the public domain. Anyone may
  use it or change it in any way that they see
  fit. The author assumes no responsibility for
  damages incurred through use of the original
  code or any variations thereof.

  It is requested, but not required, that due
  credit is given to the original author and
  anyone who has modified the code through a
  header comment, such as this one.
*/

#define HEIGHT_LIMIT 64     /* Tallest allowable tree */

struct rbtree {
    rbnode *root;       /* Top of the tree */
    size_t size;        /* Number of items */
};

struct rbnode {
    Var key;
    Var value;
    int red;            /* Color (1=red, 0=black) */
    rbnode *link[2];        /* Left (0) and right (1) links */
};

struct rbtrav {
    rbtree *tree;       /* Paired tree */
    rbnode *it;         /* Current node */
    rbnode *path[HEIGHT_LIMIT]; /* Traversal path */
    size_t top;         /* Top of stack */
};

static int
node_compare(const rbnode *node1, const rbnode *node2, int case_matters)
{
    return compare(node1->key, node2->key, case_matters);
}

static void
node_free_data(const rbnode *node)
{
    free_var(node->key);
    free_var(node->value);
}

/*
 * Returns 1 for a red node, 0 for a black node.
 */
static int
is_red(const rbnode *root)
{
    return root != nullptr && root->red == 1;
}

/*
 * Performs a single red black rotation in the specified direction.
 * Assumes that all nodes are valid for a rotation.
 *
 * `dir' is the direction to rotate (0 = left, 1 = right).
 */
static rbnode *
rbsingle(rbnode *root, int dir)
{
    rbnode *save = root->link[!dir];

    root->link[!dir] = save->link[dir];
    save->link[dir] = root;

    root->red = 1;
    save->red = 0;

    return save;
}

/*
 * Performs a double red black rotation in the specified direction.
 * Assumes that all nodes are valid for a rotation.
 *
 * `dir' is the direction to rotate (0 = left, 1 = right).
 */
static rbnode *
rbdouble(rbnode *root, int dir)
{
    root->link[!dir] = rbsingle(root->link[!dir], !dir);

    return rbsingle(root, dir);
}

/*
 * Creates and initializes a new red black node with a copy of the
 * data.  This function does not insert the new node into a tree.
 */
static rbnode *
new_node(rbtree *tree, Var key, Var value)
{
    rbnode *rn = (rbnode *)mymalloc(sizeof * rn, M_NODE);

    if (rn == nullptr)
        return nullptr;

    rn->red = 1;
    rn->key = key;
    rn->value = value;
    rn->link[0] = rn->link[1] = nullptr;

    return rn;
}

/*
 * Creates and initializes an empty red black tree.  The returned
 * pointer must be released with `rbdelete'.
 */
static rbtree *
rbnew(void)
{
    rbtree *rt = (rbtree *)mymalloc(sizeof * rt, M_TREE);

    if (rt == nullptr)
        return nullptr;

    rt->root = nullptr;
    rt->size = 0;

    return rt;
}

/*
 * Releases a valid red black tree.
 */
static void
rbdelete(rbtree *tree)
{
    rbnode *it = tree->root;
    rbnode *save;

    /*
       Rotate away the left links so that
       we can treat this like the destruction
       of a linked list
     */
    while (it != nullptr) {
        if (it->link[0] == nullptr) {
            /* No left links, just kill the node and move on */
            save = it->link[1];
            node_free_data(it);
            myfree(it, M_NODE);
        } else {
            /* Rotate away the left link and check again */
            save = it->link[0];
            it->link[0] = save->link[1];
            save->link[1] = it;
        }

        it = save;
    }

    /* Since this map could possibly be the root of a cycle, final
     * destruction is handled in the garbage collector if garbage
     * collection is enabled.
     */
#ifndef ENABLE_GC
    myfree(tree, M_TREE);
#endif
}

/*
 * Searches for a copy of the specified node data in a red black tree.
 * Returns a pointer to the data value stored in the tree, or a null
 * pointer if no data could be found.
 */
static rbnode *
rbfind(rbtree *tree, rbnode *node, int case_matters)
{
    rbnode *it = tree->root;

    while (it != nullptr) {
        int cmp = node_compare(it, node, case_matters);

        if (cmp == 0)
            break;

        /*
           If the tree supports duplicates, they should be
           chained to the right subtree for this to work
         */
        if (case_matters) {
            /* The tree is built without case sensitivity. So if we try to
             * navigate with case_matters, we will skip things and ultimately get
             * incorrect results. So in lieu of a smarter solution, compare again
             * without case sensitivity for the purposes of navigation. */
            cmp = node_compare(it, node, 0);
        }
        it = it->link[cmp < 0];
    }

    return it;
}

/*
 * Searches for a copy of the specified node data in a red black tree.
 * Returns a new traversal object initialized to start at the
 * specified node, or a null pointer if no data could be found.  The
 * pointer must be released with `rbtdelete'.
 */
static rbtrav *
rbseek(rbtree *tree, rbnode *node, int case_matters)
{
    rbtrav *trav = (rbtrav *)mymalloc(sizeof(rbtrav), M_TRAV);

    trav->tree = tree;
    trav->it = tree->root;
    trav->top = 0;

    while (trav->it != nullptr) {
        int cmp = node_compare(trav->it, node, case_matters);

        if (cmp == 0)
            break;

        /*
           If the tree supports duplicates, they should be
           chained to the right subtree for this to work
         */
        trav->path[trav->top++] = trav->it;
        trav->it = trav->it->link[cmp < 0];
    }

    if (trav->it == nullptr) {
        myfree(trav, M_TRAV);
        trav = nullptr;
    }

    return trav;
}

/*
 * Inserts a copy of the user-specified data into a red black tree.
 * Returns 1 if the value was inserted successfully, 0 if the
 * insertion failed for any reason.
 */
static int
rbinsert(rbtree *tree, rbnode *node)
{
    if (tree->root == nullptr) {
        /*
           We have an empty tree; attach the
           new node directly to the root
         */
        tree->root = new_node(tree, node->key, node->value);

        if (tree->root == nullptr)
            return 0;
    } else {
        rbnode head = {};   /* False tree root */
        rbnode *g, *t;      /* Grandparent & parent */
        rbnode *p, *q;      /* Iterator & parent */
        int dir = 0, last = 0;

        /* Set up our helpers */
        t = &head;
        g = p = nullptr;
        q = t->link[1] = tree->root;

        /* Search down the tree for a place to insert */
        for (;;) {
            if (q == nullptr) {
                /* Insert a new node at the first null link */
                p->link[dir] = q = new_node(tree, node->key, node->value);

                if (q == nullptr)
                    return 0;
            } else if (is_red(q->link[0]) && is_red(q->link[1])) {
                /* Simple red violation: color flip */
                q->red = 1;
                q->link[0]->red = 0;
                q->link[1]->red = 0;
            }

            if (is_red(q) && is_red(p)) {
                /* Hard red violation: rotations necessary */
                int dir2 = t->link[1] == g;

                if (q == p->link[last])
                    t->link[dir2] = rbsingle(g, !last);
                else
                    t->link[dir2] = rbdouble(g, !last);
            }

            /*
               Stop working if we inserted a node. This
               check also disallows duplicates in the tree
             */
            if (node_compare(q, node, 0) == 0)
                break;

            last = dir;
            dir = node_compare(q, node, 0) < 0;

            /* Move the helpers down */
            if (g != nullptr)
                t = g;

            g = p, p = q;
            q = q->link[dir];
        }

        /* Update the root (it may be different) */
        tree->root = head.link[1];
    }

    /* Make the root black for simplified logic */
    tree->root->red = 0;
    ++tree->size;

    return 1;
}

/*
 * Removes a node from a red black tree that matches the
 * user-specified data.  Returns 1 if the value was removed
 * successfully, 0 if the removal failed for any reason.
*/
static int
rberase(rbtree *tree, rbnode *node)
{
    int ret = 1;

    if (tree->root == nullptr) {
        return 0;
    } else {
        rbnode head = {};   /* False tree root */
        rbnode *q, *p, *g;  /* Helpers */
        rbnode *f = nullptr;    /* Found item */
        int dir = 1;

        /* Set up our helpers */
        q = &head;
        g = p = nullptr;
        q->link[1] = tree->root;

        /*
           Search and push a red node down
           to fix red violations as we go
         */
        while (q->link[dir] != nullptr) {
            int last = dir;

            /* Move the helpers down */
            g = p, p = q;
            q = q->link[dir];
            dir = node_compare(q, node, 0) < 0;

            /*
               Save the node with matching data and keep
               going; we'll do removal tasks at the end
             */
            if (node_compare(q, node, 0) == 0)
                f = q;

            /* Push the red node down with rotations and color flips */
            if (!is_red(q) && !is_red(q->link[dir])) {
                if (is_red(q->link[!dir]))
                    p = p->link[last] = rbsingle(q, dir);
                else if (!is_red(q->link[!dir])) {
                    rbnode *s = p->link[!last];

                    if (s != nullptr) {
                        if (!is_red(s->link[!last])
                                && !is_red(s->link[last])) {
                            /* Color flip */
                            p->red = 0;
                            s->red = 1;
                            q->red = 1;
                        } else {
                            int dir2 = g->link[1] == p;

                            if (is_red(s->link[last]))
                                g->link[dir2] = rbdouble(p, last);
                            else if (is_red(s->link[!last]))
                                g->link[dir2] = rbsingle(p, last);

                            /* Ensure correct coloring */
                            q->red = g->link[dir2]->red = 1;
                            g->link[dir2]->link[0]->red = 0;
                            g->link[dir2]->link[1]->red = 0;
                        }
                    }
                }
            }
        }

        /* Replace and remove the saved node */
        if (f != nullptr) {
            node_free_data(f);
            f->key = q->key;
            f->value = q->value;
            p->link[p->link[1] == q] = q->link[q->link[0] == nullptr];
            myfree(q, M_NODE);

            --tree->size;
        } else
            ret = 0;

        /* Update the root (it may be different) */
        tree->root = head.link[1];

        /* Make the root black for simplified logic */
        if (tree->root != nullptr)
            tree->root->red = 0;
    }

    return ret;
}

/*
 * Creates a new traversal object.  The traversal object is not
 * initialized until `rbtfirst' or `rbtlast' are called.  The
 * pointer must be released with `rbtdelete'.
 */
static rbtrav *
rbtnew(void)
{
    return (rbtrav *)mymalloc(sizeof(rbtrav), M_TRAV);
}

/*
 * Releases a traversal object.
 */
static void
rbtdelete(rbtrav *trav)
{
    myfree(trav, M_TRAV);
}

/*
 * Initializes a traversal object. The user-specified direction
 * determines whether to begin traversal at the smallest or largest
 * valued node.  `dir' is the direction to traverse (0 = ascending, 1
 * = descending).
 */
static rbnode *
rbstart(rbtrav *trav, rbtree *tree, int dir)
{
    trav->tree = tree;
    trav->it = tree->root;
    trav->top = 0;

    /* Save the path for later traversal */
    if (trav->it != nullptr) {
        while (trav->it->link[dir] != nullptr) {
            trav->path[trav->top++] = trav->it;
            trav->it = trav->it->link[dir];
        }
    }

    return trav->it == nullptr ? nullptr : trav->it;
}

/*
 * Traverses a red black tree in the user-specified direction.  `dir'
 * is the direction to traverse (0 = ascending, 1 = descending).
 * Returns a pointer to the next data value in the specified
 * direction.
 */
static rbnode *
rbmove(rbtrav *trav, int dir)
{
    if (trav->it->link[dir] != nullptr) {
        /* Continue down this branch */
        trav->path[trav->top++] = trav->it;
        trav->it = trav->it->link[dir];

        while (trav->it->link[!dir] != nullptr) {
            trav->path[trav->top++] = trav->it;
            trav->it = trav->it->link[!dir];
        }
    } else {
        /* Move to the next branch */
        rbnode *last;

        do {
            if (trav->top == 0) {
                trav->it = nullptr;
                break;
            }

            last = trav->it;
            trav->it = trav->path[--trav->top];
        } while (last == trav->it->link[dir]);
    }

    return trav->it == nullptr ? nullptr : trav->it;
}

/*
 * Initializes a traversal object to the smallest valued node.
 */
static rbnode *
rbtfirst(rbtrav *trav, rbtree *tree)
{
    return rbstart(trav, tree, 0);  /* Min value */
}

/*
 * Initializes a traversal object to the largest valued node.
 */
static rbnode *
rbtlast(rbtrav *trav, rbtree *tree)
{
    return rbstart(trav, tree, 1);  /* Max value */
}

/*
 * Traverses to the next value in ascending order.
 */
static rbnode *
rbtnext(rbtrav *trav)
{
    return rbmove(trav, 1); /* Toward larger items */
}

/*
 * Traverses to the next value in descending order.
 */
static rbnode *
rbtprev(rbtrav *trav)
{
    return rbmove(trav, 0); /* Toward smaller items */
}

/********/

static Var
empty_map(void)
{
    Var map;
    rbtree *tree;

    if ((tree = rbnew()) == nullptr)
        panic_moo("EMPTY_MAP: rbnew failed");

    map.type = TYPE_MAP;
    map.v.tree = tree;

    return map;
}

Var
new_map(void)
{
    static Var map;

    if (map.v.tree == nullptr)
        map = empty_map();

#ifdef ENABLE_GC
    assert(gc_get_color(map.v.tree) == GC_GREEN);
#endif

    addref(map.v.tree);

    return map;
}

/* called from utils.c */
void
destroy_map(Var map)
{
    rbdelete(map.v.tree);
}

/* called from utils.c */
Var
map_dup(Var map)
{
    rbtrav trav;
    rbnode node;
    const rbnode *pnode;
    Var _new = empty_map();

    for (pnode = rbtfirst(&trav, map.v.tree); pnode; pnode = rbtnext(&trav)) {
        node.key = var_ref(pnode->key);
        node.value = var_ref(pnode->value);
        if (!rbinsert(_new.v.tree, &node))
            panic_moo("MAP_DUP: rbinsert failed");
    }
#ifdef ENABLE_GC
    gc_set_color(_new.v.tree, gc_get_color(map.v.tree));
#endif

    return _new;
}

/* called from utils.c */
int
map_sizeof(rbtree *tree)
{
    rbtrav trav;
    const rbnode *pnode;
    int size;

#ifdef MEMO_VALUE_BYTES
    if ((size = (((int *)(tree))[MEMO_OFFSET])))
        return size;
#endif

    size = sizeof(rbtree);
    for (pnode = rbtfirst(&trav, tree); pnode; pnode = rbtnext(&trav)) {
        size += sizeof(rbnode) - 2 * sizeof(Var);
        size += value_bytes(pnode->key);
        size += value_bytes(pnode->value);
    }

#ifdef MEMO_VALUE_BYTES
    (((int *)(tree))[MEMO_OFFSET]) = size;
#endif

    return size;
}

Var
mapinsert(Var map, Var key, Var value)
{   /* consumes `map', `key', `value' */
    /* Prevent the insertion of invalid values -- specifically keys
     * that have the values `none' and `clear' (which are used as
     * boundary conditions in the looping logic), and keys that are
     * collections (for which `compare' does not currently work).
     */
    if (key.type == TYPE_NONE || key.type == TYPE_CLEAR
            || (key.is_collection() && TYPE_ANON != key.type))
        panic_moo("MAPINSERT: invalid key");

    Var _new = map;

    if (var_refcount(map) > 1) {
        _new = map_dup(map);
        free_var(map);
    }

#ifdef MEMO_VALUE_BYTES
    /* reset the memoized size */
    ((int *)(_new.v.tree))[MEMO_OFFSET] = 0;
#endif

    rbnode node;
    node.key = key;
    node.value = value;

    rberase(_new.v.tree, &node);

    if (!rbinsert(_new.v.tree, &node))
        panic_moo("MAPINSERT: rbinsert failed");

#ifdef ENABLE_GC
    gc_set_color(_new.v.tree, GC_YELLOW);
#endif

    return _new;
}

const rbnode *
maplookup(Var map, Var key, Var *value, int case_matters)
{   /* does NOT consume `map' or `'key',
       does NOT increment the ref count on `value' */
    rbnode node;
    const rbnode *pnode;

    node.key = key;
    pnode = rbfind(map.v.tree, &node, case_matters);
    if (pnode && value)
        *value = pnode->value;

    return pnode;
}

/* Seeks to the item with the specified key in the specified map and
 * returns an iterator value for the map starting at that key.
 */
int
mapseek(Var map, Var key, Var *iter, int case_matters)
{   /* does NOT consume `map' or `'key',
       ALWAYS returns a newly allocated value in `iter' */
    rbnode node;
    rbtrav *ptrav;

    node.key = key;
    ptrav = rbseek(map.v.tree, &node, case_matters);
    if (ptrav && iter) {
        iter->type = TYPE_ITER;
        iter->v.trav = ptrav;
    } else if (iter) {
        *iter = none;
    }

    return ptrav != nullptr;
}

int
mapequal(Var lhs, Var rhs, int case_matters)
{
    rbtrav trav_lhs, trav_rhs;
    const rbnode *pnode_lhs = nullptr, *pnode_rhs = nullptr;

    if (lhs.v.tree == rhs.v.tree)
        return 1;

    while (1) {
        pnode_lhs =
            pnode_lhs == nullptr ? rbtfirst(&trav_lhs, lhs.v.tree)
            : rbtnext(&trav_lhs);
        pnode_rhs =
            pnode_rhs == nullptr ? rbtfirst(&trav_rhs, rhs.v.tree)
            : rbtnext(&trav_rhs);
        if (pnode_lhs == nullptr || pnode_rhs == nullptr)
            break;
        if (!equality(pnode_lhs->key, pnode_rhs->key, case_matters)
                || !equality(pnode_lhs->value, pnode_rhs->value, case_matters))
            break;
    }

    return pnode_lhs == nullptr && pnode_rhs == nullptr;
}

int
mapempty(Var map)
{
    return map.v.tree->size == 0;
}

Num
maplength(Var map)
{
    return map.v.tree->size;
}

/* Iterate over the map, calling the function `func' once per
 * key/value pair.  Don't dynamically allocate `rbtrav' because
 * `mapforeach()' can be called from contexts where exception handling
 * is in effect.
 */
int
mapforeach(Var map, mapfunc func, void *data)
{   /* does NOT consume `map' */
    rbtrav trav;
    const rbnode *pnode;
    int first = 1;
    int ret;

    for (pnode = rbtfirst(&trav, map.v.tree); pnode; pnode = rbtnext(&trav)) {
        if ((ret = (*func)(pnode->key, pnode->value, data, first)))
            return ret;
        first = 0;
    }

    return 0;
}

int
mapfirst(Var map, var_pair *pair)
{
    rbnode *node = map.v.tree->root;

    if (node != nullptr) {
        while (node->link[0] != nullptr) {
            node = node->link[0];
        }
    }

    if (node != nullptr && pair != nullptr) {
        pair->a = node->key;
        pair->b = node->value;
    }

    return node != nullptr;
}

int
maplast(Var map, var_pair *pair)
{
    rbnode *node = map.v.tree->root;

    if (node != nullptr) {
        while (node->link[1] != nullptr) {
            node = node->link[1];
        }
    }

    if (node != nullptr && pair != nullptr) {
        pair->a = node->key;
        pair->b = node->value;
    }

    return node != nullptr;
}

/* Returns the specified range from the map.  `from' and `to' must be
 * valid iterators for the map or the behavior is unspecified.
 */
Var
maprange(Var map, rbtrav *from, rbtrav *to)
{   /* consumes `map' */
    rbnode node;
    const rbnode *pnode = nullptr;
    Var _new = empty_map();

    do {
        pnode = pnode == nullptr ? from->it : rbtnext(from);

        node.key = var_ref(pnode->key);
        node.value = var_ref(pnode->value);
        if (!rbinsert(_new.v.tree, &node))
            panic_moo("MAP_DUP: rbinsert failed");
    } while (pnode != to->it);

    free_var(map);

#ifdef ENABLE_GC
    gc_set_color(_new.v.tree, GC_YELLOW);
#endif

    return _new;
}

/* Returns a new map with all the keys from the right
 * removed from the map on the left.
 */
Var
mapsubtract(Var first, Var second)
{   /* consumes `maps' */
    Var _new = map_dup(first);
    rbtrav trav;
    rbnode node;
    const rbnode *pnode;

    for (pnode = rbtfirst(&trav, second.v.tree); pnode; pnode = rbtnext(&trav)) {
        node.key = var_ref(pnode->key);
        rberase(_new.v.tree, &node);         
    }

    free_var(first);
    free_var(second);

#ifdef ENABLE_GC
    gc_set_color(_new.v.tree, GC_YELLOW);
#endif

    return _new;
}

/* Returns a new map with all the keys and values from the right
 * added to the map on the left.
 */
Var
mapconcat(Var first, Var second)
{   /* consumes `maps' */
    Var _new = empty_map();
    rbtrav trav;
    rbnode node;
    const rbnode *pnode1;
    const rbnode *pnode2;

    for (pnode1 = rbtfirst(&trav, first.v.tree); pnode1; pnode1 = rbtnext(&trav)) {
        node.key = var_ref(pnode1->key);
        node.value = var_ref(pnode1->value);
        if (!rbinsert(_new.v.tree, &node))
            panic_moo("MAP_DUP: rbinsert failed");
    }

    for (pnode2 = rbtfirst(&trav, second.v.tree); pnode2; pnode2 = rbtnext(&trav)) {
        node.key = var_ref(pnode2->key);
        node.value = var_ref(pnode2->value);
        if (!rbinsert(_new.v.tree, &node))
            panic_moo("MAP_DUP: rbinsert failed");
    }

    free_var(first);
    free_var(second);

#ifdef ENABLE_GC
    gc_set_color(_new.v.tree, GC_YELLOW);
#endif

    return _new;
}

/* Replaces the specified range in the map.  `from' and `to' must be
 * valid iterators for the map or the behavior is unspecified.  The
 * new map is placed in `new' (`new' is first freed).  Returns
 * `E_NONE' if successful.
 */
enum error
maprangeset(Var map, rbtrav *from, rbtrav *to, Var value, Var *_new)
{   /* consumes `map', `value' */
    rbtrav trav;
    rbnode node;
    const rbnode *pnode = nullptr;
    enum error e = E_NONE;

    if (_new == nullptr)
        panic_moo("MAP_DUP: new is NULL");

    free_var(*_new);
    *_new = empty_map();

    for (pnode = rbtfirst(&trav, map.v.tree); pnode; pnode = rbtnext(&trav)) {
        if (pnode == from->it)
            break;
        node.key = var_ref(pnode->key);
        node.value = var_ref(pnode->value);
        if (!rbinsert(_new->v.tree, &node))
            panic_moo("MAP_DUP: rbinsert failed");
    }

    for (pnode = rbtfirst(&trav, value.v.tree); pnode; pnode = rbtnext(&trav)) {
        node.key = var_ref(pnode->key);
        node.value = var_ref(pnode->value);
        rberase(_new->v.tree, &node);
        if (!rbinsert(_new->v.tree, &node))
            panic_moo("MAP_DUP: rbinsert failed");
    }

    while ((pnode = rbtnext(to))) {
        node.key = var_ref(pnode->key);
        node.value = var_ref(pnode->value);
        rberase(_new->v.tree, &node);
        if (!rbinsert(_new->v.tree, &node))
            panic_moo("MAP_DUP: rbinsert failed");
    }

    free_var(map);
    free_var(value);

#ifdef ENABLE_GC
    gc_set_color(_new->v.tree, GC_YELLOW);
#endif

    return e;
}

Var
new_iter(Var map)
{
    Var iter;

    iter.type = TYPE_ITER;
    if ((iter.v.trav = rbtnew()) == nullptr)
        panic_moo("NEW_ITER: rbtnew failed");

    rbtfirst(iter.v.trav, map.v.tree);

    return iter;
}

/* called from utils.c */
void
destroy_iter(Var iter)
{
    rbtdelete(iter.v.trav);
}

/* called from utils.c */
Var
iter_dup(Var iter)
{
    panic_moo("ITER_DUP: don't do this");

    return none;
}

int
iterget(Var iter, var_pair *pair)
{
    if (iter.v.trav->it) {
        pair->a = iter.v.trav->it->key;
        pair->b = iter.v.trav->it->value;

        return 1;
    }

    return 0;
}

void
iternext(Var iter)
{
    rbtnext(iter.v.trav);
}

/* called from execute.c */

void
clear_node_value(const rbnode *node)
{
    ((rbnode *)node)->value.type = TYPE_NONE;
}

/**** built in functions ****/

static package
bf_mapdelete(Var arglist, Byte next, void *vdata, Objid progr)
{
    Var r;
    Var map = arglist.v.list[1];
    Var key = arglist.v.list[2];

    if (key.is_collection()) {
        free_var(arglist);
        return make_error_pack(E_TYPE);
    }

    r = var_refcount(map) == 1 ? var_ref(map) : map_dup(map);

#ifdef MEMO_VALUE_BYTES
    /* reset the memoized size */
    ((int *)(r.v.tree))[MEMO_OFFSET] = 0;
#endif

    rbnode node;
    node.key = key;
    if (!rberase(r.v.tree, &node)) {
        free_var(r);
        free_var(arglist);
        return make_error_pack(E_RANGE);
    }

    free_var(arglist);
    return make_var_pack(r);
}

static int
do_map_keys(Var key, Var value, void *data, int first)
{
    Var *list = (Var *)data;
    *list = listappend(*list, var_ref(key));
    return 0;
}

static package
bf_mapkeys(Var arglist, Byte next, void *vdata, Objid progr)
{
    Var r = new_list(0);
    mapforeach(arglist.v.list[1], do_map_keys, &r);
    free_var(arglist);
    return make_var_pack(r);
}

static int
do_map_values(Var key, Var value, void *data, int first)
{
    Var *list = (Var *)data;
    *list = listappend(*list, var_ref(value));
    return 0;
}

static package
bf_mapvalues(Var arglist, Byte next, void *vdata, Objid progr)
{
    const auto nargs = arglist.v.list[0].v.num;
    //nargs==1: simple mapvalues, dump all values of the map.
    if (nargs == 1)
    {
        Var r = new_list(0);
        mapforeach(arglist.v.list[1], do_map_values, &r);
        free_var(arglist);
        return make_var_pack(r);
    }
    else
    {
        Var r = new_list(0);
        for (int i = 2; i <= nargs; ++i)
        {
            const auto rbnode = maplookup(arglist.v.list[1], arglist.v.list[i], nullptr, true);
            if (!rbnode)
            {
                free_var(r);
                free_var(arglist);
                return make_error_pack(E_RANGE);
            }
            r = listappend(r, var_ref(rbnode->value));
        }
        free_var(arglist);
        return make_var_pack(r);
    }
}

static package
bf_maphaskey(Var arglist, Byte next, void *vdata, Objid progr)
{
    Var key = arglist.v.list[2];
    if (key.is_collection()) {
        free_var(arglist);
        return make_error_pack(E_TYPE);
    }

    Var map = arglist.v.list[1];
    Var ret;
    bool case_matters = arglist.v.list[0].v.num >= 3 && is_true(arglist.v.list[3]);

    ret = Var::new_int(!(maplookup(map, key, nullptr, case_matters) == nullptr));

    free_var(arglist);
    return make_var_pack(ret);
}


void
register_map(void)
{
    register_function("mapdelete", 2, 2, bf_mapdelete, TYPE_MAP, TYPE_ANY);
    register_function("mapkeys", 1, 1, bf_mapkeys, TYPE_MAP);
    register_function("mapvalues", 1, -1, bf_mapvalues, TYPE_MAP);
    register_function("maphaskey", 2, 3, bf_maphaskey, TYPE_MAP, TYPE_ANY, TYPE_INT);
}
