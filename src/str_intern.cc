#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "storage.h"
#include "str_intern.h"
#include "utils.h"

#ifdef STRING_INTERNING

struct intern_entry {
    const char *s;
    unsigned hash;
    struct intern_entry *next;
};

/* If we're using the intern table during db load, we have a stunning
  * opportunity to fragment memory with little intern_entry
  * structures.  So, the inevitable suballocator.
  *
  * On Linux (at least) malloc() calls past a certain size are
  * converted to mmap() allocations.  That's really nice since we
  * don't bloat sbrk() with memory we'll never use past db load.
  * That makes us look better in /usr/bin/top.
  */

struct intern_entry_hunk {
    int size;
    int handout;
    struct intern_entry *contents;
    struct intern_entry_hunk *next;
};

static struct intern_entry_hunk *intern_alloc = nullptr;

static struct intern_entry_hunk *
new_intern_entry_hunk(int size)
{
    struct intern_entry_hunk *_new;

    _new = (struct intern_entry_hunk *)mymalloc(sizeof(struct intern_entry_hunk), M_INTERN_HUNK);
    _new->size = size;
    _new->handout = 0;
    _new->contents = (struct intern_entry *)mymalloc(sizeof(struct intern_entry) * size, M_INTERN_ENTRY);
    _new->next = nullptr;

    return _new;
}

/* Chosen large enough to trigger the mmap() semantics of linux
   malloc. */
#define INTERN_ENTRY_HUNK_SIZE 100000

static struct intern_entry *
allocate_intern_entry(void)
{
    if (intern_alloc == nullptr) {
        intern_alloc = new_intern_entry_hunk(INTERN_ENTRY_HUNK_SIZE);
    }

    if (intern_alloc->handout  < intern_alloc->size) {
        struct intern_entry *e;

        e = &(intern_alloc->contents[intern_alloc->handout]);
        intern_alloc->handout++;

        return e;
    } else {
        struct intern_entry_hunk *new_hunk = new_intern_entry_hunk(INTERN_ENTRY_HUNK_SIZE);

        new_hunk->next = intern_alloc;
        intern_alloc = new_hunk;
        return allocate_intern_entry();
    }
}

static void
free_intern_entry_hunks(void)
{
    struct intern_entry_hunk *h, *next;

    for (h = intern_alloc; h; h = next) {
        next = h->next;
        myfree(h->contents, M_INTERN_ENTRY);
        myfree(h, M_INTERN_HUNK);
    }

    intern_alloc = nullptr;
}

/**********************/

static struct intern_entry **intern_table;
static int intern_table_size = 0;
static int intern_table_count = 0;

static int intern_bytes_saved = 0;
static int intern_allocations_saved = 0;

#define INTERN_TABLE_SIZE_INITIAL 10007

static struct intern_entry **
make_intern_table(int size) {
    struct intern_entry **table;
    int i;

    table = (intern_entry **)mymalloc(sizeof(struct intern_entry *) * size, M_INTERN_POINTER);
    for (i = 0; i < size; i++) {
        table[i] = nullptr;
    }

    return table;
}


void
str_intern_open(int table_size)
{
    if (table_size == 0) {
        table_size = INTERN_TABLE_SIZE_INITIAL;
    }
    intern_table = make_intern_table(table_size);
    intern_table_size = table_size;

    intern_bytes_saved = 0;
    intern_allocations_saved = 0;
}

void
str_intern_close(void)
{
    int i;
    struct intern_entry *e, *next;

    for (i = 0; i < intern_table_size; i++) {
        for (e = intern_table[i]; e; e = next) {
            next = e->next;

            free_str(e->s);

            /* myfree(e, M_INTERN_ENTRY); */
        }
    }

    myfree(intern_table, M_INTERN_POINTER);
    intern_table = nullptr;

    free_intern_entry_hunks();

    oklog("INTERN: %d allocations saved, %d bytes\n", intern_allocations_saved, intern_bytes_saved);
    oklog("INTERN: at end, %d entries in a %d bucket hash table.\n", intern_table_count, intern_table_size);
}

static struct intern_entry *
find_interned_string(const char *s, unsigned hash)
{
    int bucket = hash % intern_table_size;
    struct intern_entry *p;

    for (p = intern_table[bucket]; p; p = p->next) {
        if (hash == p->hash) {
            if (!strcmp(s, p->s)) {
                return p;
            }
        }
    }

    return nullptr;
}

/* Caller must addref s */

static void
add_interned_string(const char *s, unsigned hash)
{
    int bucket = hash % intern_table_size;
    struct intern_entry *p;

    /* p = mymalloc(sizeof(struct intern_entry), M_INTERN_ENTRY); */
    p = allocate_intern_entry();
    p->s = s;
    p->hash = hash;
    p->next = intern_table[bucket];

    intern_table[bucket] = p;

    intern_table_count++;
}

static void
intern_rehash(int new_size) {
    struct intern_entry **new_table;
    int i, count;
    struct intern_entry *e, *next;

    count =  0;
    new_table = make_intern_table(new_size);

    for (i = 0; i < intern_table_size; i++) {
        for (e = intern_table[i]; e; e = next) {
            int new_bucket = e->hash % new_size;
            /* Keep the next pointer, since we're gonna nuke it. */
            next = e->next;

            e->next = new_table[new_bucket];
            new_table[new_bucket] = e;

            count++;
        }
    }

    if (count != intern_table_count) {
        errlog("counted %d entries in intern hash table, but intern_table_count says %d!\n", count, intern_table_count);
    }

    intern_table_size = new_size;

    myfree(intern_table, M_INTERN_POINTER);
    intern_table = new_table;
}


/* Make an immutable copy of s.  If there's an intern table open,
   possibly share storage. */
const char *
str_intern(const char *s)
{
    struct intern_entry *e;
    unsigned hash;
    const char *r;

    if (s == nullptr || *s == '\0') {
        /* str_dup already has a canonical empty string */
        return str_dup(s);
    }

    if (intern_table == nullptr) {
        return str_dup(s);
    }

    hash = str_hash(s);

    e = find_interned_string(s, hash);

    if (e != nullptr) {
        intern_allocations_saved++;
        intern_bytes_saved += memo_strlen(e->s);
        return str_ref(e->s);
    }

    if (intern_table_count > intern_table_size) {
        intern_rehash(intern_table_size * 2);
    }

    r = str_dup(s);
    r = str_ref(r);
    add_interned_string(r, hash);

    return r;
}

#else /* STRING_INTERNING */

const char *
str_intern(const char *s)
{
    return str_dup(s);
}

void
str_intern_close(void)
{
    ;
}

void
str_intern_open(int table_size)
{
    ;
}

#endif /* STRING_INTERNING */
