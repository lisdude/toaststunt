#ifndef EXTENSION_PCRE_H
#define EXTENSION_PCRE_H

#ifndef _PCRE_H
#include <pcre.h>
#endif

#include "structures.h"

#define DEFAULT_LOOPS       1000
#define RETURN_INDEXES      2
#define RETURN_GROUPS       4
#define FIND_ALL            8

struct pcre_cache_entry {
    char *error;
    pcre *re;
    pcre_extra *extra;
    int captures;
    unsigned int cache_hits;
    std::atomic_uint refcount;
};

extern void pcre_shutdown(void);

#ifdef SQLITE3_FOUND
#include <sqlite3.h>
extern void sqlite_regexp(sqlite3_context *ctx, int argc, sqlite3_value **argv);
#endif

#endif /* EXTENSION_PCRE_H */
