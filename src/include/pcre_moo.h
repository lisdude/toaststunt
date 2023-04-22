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
};

extern void pcre_shutdown(void);

extern void free_entry(pcre_cache_entry *);
extern struct pcre_cache_entry * get_pcre(const char *string, unsigned char options);

#endif /* EXTENSION_PCRE_H */
