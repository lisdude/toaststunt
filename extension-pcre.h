#ifndef EXTENSION_PCRE_H
#define EXTENSION_PCRE_H

#include <pcre.h>

#include "functions.h"
#include "list.h"
#include "utils.h"
#include "log.h"
#include "pcrs.h"
#include "server.h"
#include "map.h"
#include "xtrapbits.h"

#define EXT_PCRE_VERSION    "3.0"
#define DEFAULT_LOOPS       1000
#define RETURN_INDEXES      2
#define RETURN_GROUPS       4
#define FIND_ALL            8

/* A misnomer, since we don't have a cache anymore. But I'm leaving
 * this struct around just in case we want to go back to caching
 * in the future, which we might with studying. */
struct pcre_cache_entry {
    char *error;
    pcre *re;
    pcre_extra *extra;
    int captures;
};

void free_entry(pcre_cache_entry *);
Var result_indices(int ovector[], int n);

#endif /* EXTENSION_PCRE_H */
