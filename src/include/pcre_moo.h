#ifndef EXTENSION_PCRE_H
#define EXTENSION_PCRE_H

#ifndef _PCRE_H
#include <pcre.h>
#endif

#include "structures.h"

#define EXT_PCRE_VERSION    "3.0"
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
};

void free_entry(pcre_cache_entry *);
void delete_cache_entry(const char *pattern);
Var result_indices(int ovector[], int n);
extern void pcre_shutdown(void);

#endif /* EXTENSION_PCRE_H */
