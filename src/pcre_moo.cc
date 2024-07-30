#include "options.h"

#ifdef PCRE_FOUND

#include <ctype.h>
#include <unordered_map>
#include <limits.h>
#include <string>

#include "pcre_moo.h"
#include "functions.h"
#include "list.h"
#include "utils.h"
#include "log.h"
#include "server.h"
#include "map.h"
#include "dependencies/pcrs.h"
#include "dependencies/xtrapbits.h"

#ifndef PCRE_STUDY_JIT_COMPILE
#define PCRE_STUDY_JIT_COMPILE 0
#endif

template<>
struct std::hash<cache_type>
{
    std::size_t operator()(const cache_type& t) const noexcept
    {
        return std::hash<std::string>{}(std::string(t.first) + (char)t.second);
    }
};

struct CacheEqual : public std::binary_function<const cache_type&, const cache_type&, bool>
{
public:
    bool operator() (const cache_type& c1, const cache_type& c2) const
    {
        return strcmp(c1.first, c2.first) == 0 && c1.second == c2.second;
    }
};

static pthread_mutex_t cache_mutex;
typedef std::pair<const char*, unsigned char> cache_type;
static std::unordered_map<cache_type, pcre_cache_entry*, std::hash<cache_type>, CacheEqual> pcre_pattern_cache;

static struct pcre_cache_entry *
get_pcre(const char *string, unsigned char options)
{
    pcre_cache_entry *entry = nullptr;

    pthread_mutex_lock(&cache_mutex);
    cache_type pair = std::make_pair(string, options);
    if (pcre_pattern_cache.count(pair) != 0) {
        entry = pcre_pattern_cache[pair];
        entry->cache_hits++;
        entry->refcount++;
    } else {
        /* If the cache is too large, remove the entry with the least amount of hits. */
        if (pcre_pattern_cache.size() >= PCRE_PATTERN_CACHE_SIZE) {
            std::unordered_map<cache_type, pcre_cache_entry*, std::hash<cache_type>, CacheEqual>::iterator entry_to_delete = pcre_pattern_cache.begin(), it = entry_to_delete;
            while (it != pcre_pattern_cache.end()) {
                if (it->second->cache_hits < entry_to_delete->second->cache_hits) {
                    entry_to_delete = it;
                    /* Don't bother trying to find anything less than 0... waste of string comparisons. */
                    if (it->second->cache_hits == 0)
                        break;
                }
                it++;
            }
            /* We could use delete_cache_entry here, and arguably it would be cleaner, but that would cause an extra pointless
               iteration full of string comparisons. Fortunately we have enough information here to deal with it directly. */
            auto entry = *entry_to_delete;
            pcre_pattern_cache.erase(entry_to_delete);
            free_entry(entry.second);
            free_str(entry.first.first);
        }

        const char *err;
        int eos; /* Error offset */
        char buf[256];

        entry = (pcre_cache_entry*)malloc(sizeof(pcre_cache_entry));
        entry->error = nullptr;
        entry->re = nullptr;
        entry->captures = 0;
        entry->extra = nullptr;
        entry->cache_hits = 0;
        entry->refcount = 1;

        entry->re = pcre_compile(string, options, &err, &eos, nullptr);
        if (entry->re == nullptr) {
            sprintf(buf, "PCRE compile error at offset %d: %s", eos, err);
            entry->error = str_dup(buf);
        } else {
            const char *error = nullptr;
            entry->extra = pcre_study(entry->re, PCRE_STUDY_JIT_COMPILE, &error);
            if (error != nullptr)
                entry->error = str_dup(error);
            else
                (void)pcre_fullinfo(entry->re, nullptr, PCRE_INFO_CAPTURECOUNT, &(entry->captures));
        }

        if (entry->error == nullptr) {
            entry->refcount++;
            pcre_pattern_cache[std::make_pair(str_dup(string), options)] = entry;
        }
    }

    pthread_mutex_unlock(&cache_mutex);
    return entry;
}

static package
bf_pcre_match(Var arglist, Byte next, void *vdata, Objid progr)
{
    /* Some useful constants. */
    static const Var match = str_dup_to_var("match");
    static const Var position = str_dup_to_var("position");
    /**************************/

    const char *subject, *pattern;
    char err[256]; /* Our general-purpose error holder. Handy! */
    unsigned char options = 0;
    unsigned char flags = FIND_ALL;

    subject = arglist.v.list[1].v.str;
    pattern = arglist.v.list[2].v.str;
    options = (arglist.v.list[0].v.num >= 3 && is_true(arglist.v.list[3])) ? 0 : PCRE_CASELESS;

    if (arglist.v.list[0].v.num >= 4 && arglist.v.list[4].v.num == 0)
        flags ^= FIND_ALL;

    /* Return E_INVARG if the pattern or subject are empty. */
    if (pattern[0] == '\0' || subject[0] == '\0') {
        free_var(arglist);
        return make_error_pack(E_INVARG);
    }

    /* Compile the pattern */
    struct pcre_cache_entry *entry = get_pcre(pattern, options);

    if (entry->error != nullptr)
    {
        package r = make_raise_pack(E_INVARG, entry->error, var_ref(zero));
        free_entry(entry);
        free_var(arglist);
        /* We don't need to remove it from the cache because error entries never make it to begin with. */
        return r;
    }

    /* Determine how many subpatterns match so we can allocate memory. */
    int oveccount = (entry->captures + 1) * 3;
    int ovector[oveccount];

    /* Set up the MOO variables to store the final value and intermediaries. */
    Var named_groups = new_map();
    Var ret = new_list(0);

    /* Variables pertaining to the main execution loop */
    int offset = 0, rc = 0, i = 0, named_substrings;
    int subject_length = memo_strlen(subject);
    unsigned int loops = 0;
    const char *matched_substring;

    /* Check for the existence of the pcre_match_max_iterations server option to determine
     * how many iterations of the match loop we'll attempt before giving up. */
    unsigned int total_loops = server_int_option("pcre_match_max_iterations", DEFAULT_LOOPS);
    if (total_loops < 100)
        total_loops = 100;
    else if (total_loops >= 100000000)
        total_loops = 100000000;

    /* Execute the match. */
    while (offset < subject_length)
    {
        loops++;
        rc = pcre_exec(entry->re, entry->extra, subject, subject_length, offset, 0, ovector, oveccount);
        if (rc < 0 && rc != PCRE_ERROR_NOMATCH)
        {
            /* We've encountered some funky error. Back out and let them know what it is. */
            delete_cache_entry(pattern, options);
            free_entry(entry);
            free_var(arglist);
            sprintf(err, "pcre_exec returned error: %d", rc);
            return make_raise_pack(E_INVARG, err, var_ref(zero));
        } else if (rc == 0) {
            /* We don't have enough room to store all of these substrings. */
            sprintf(err, "pcre_exec only has room for %d substrings", entry->captures);
            delete_cache_entry(pattern, options);
            free_entry(entry);
            free_var(arglist);
            return make_raise_pack(E_QUOTA, err, var_ref(zero));
        } else if (rc == PCRE_ERROR_NOMATCH) {
            /* There are no more matches. */
            break;
        } else if (loops >= total_loops) {
            /* The loop has iterated beyond the maximum limit, probably locking the server. Kill it. */
            delete_cache_entry(pattern, options);
            free_entry(entry);
            free_var(arglist);
            sprintf(err, "Too many iterations of matching loop: %u", loops);
            return make_raise_pack(E_MAXREC, err, var_ref(zero));
        } else {
            /* We'll use a bit array to indicate which index matches are superfluous. e.g. which results
             * have a NAMED result instead of a numbered result. I'm definitely open to better ideas! */
            static unsigned char *bit_array;
            bit_array = (unsigned char *)mymalloc(rc * sizeof(unsigned char), M_ARRAY);
            memset(bit_array, 0, rc);
            (void)pcre_fullinfo(entry->re, nullptr, PCRE_INFO_NAMECOUNT, &named_substrings);

            if (named_substrings > 0)
            {
                unsigned char *name_table, *tabptr;
                int name_entry_size;

                (void)pcre_fullinfo(entry->re, nullptr, PCRE_INFO_NAMETABLE, &name_table);
                (void)pcre_fullinfo(entry->re, nullptr, PCRE_INFO_NAMEENTRYSIZE, &name_entry_size);

                tabptr = name_table;
                for (int i = 0; i < named_substrings; i++)
                {
                    /* Determine which result number corresponds to the named capture group */
                    int n = (tabptr[0] << 8) | tabptr[1];
                    /* Create a list of indices for the substring */
                    Var pos = result_indices(ovector, n);
                    Var result = new_map();
                    int substring_size = ovector[2 * n + 1] - ovector[2 * n];
                    result = mapinsert(result, var_ref(position), pos);

                    /* Extract the substring itself with obnoxious printf magic */
                    char *substring = (char *)mymalloc(substring_size + 1, M_STRING);
                    sprintf(substring, "%.*s", substring_size, subject + ovector[2 * n]);
                    Var substring_var;
                    substring_var.type = TYPE_STR;
                    substring_var.v.str = substring;
                    result = mapinsert(result, var_ref(match), substring_var);

                    named_groups = mapinsert(named_groups, str_dup_to_var((const char*)(tabptr + 2)), result);
                    bit_true(bit_array, n);
                    tabptr += name_entry_size;
                }
            }

            /* Store any numbered substrings that didn't match a named capture group. */
            for (i = 0; i < rc; i++) {
                /* First check if we have a named match for this number. If so, skip it. */
                if (bit_is_true(bit_array, i))
                    continue;

                pcre_get_substring(subject, ovector, rc, i, &(matched_substring));
                Var pos = result_indices(ovector, i);

                Var result = new_map();
                result = mapinsert(result, var_ref(position), pos);
                result = mapinsert(result, var_ref(match), str_dup_to_var(matched_substring));
                pcre_free_substring(matched_substring);

                /* Convert the numbered group to a string. */
                char tmp_buffer[100];
                sprintf(tmp_buffer, "%i", i);

                named_groups = mapinsert(named_groups, str_dup_to_var(tmp_buffer), result);
            }

            /* Begin at the end of the previous match on the next iteration of the loop. */
            offset = ovector[1];

            myfree(bit_array, M_ARRAY);
        }

        ret = listappend(ret, named_groups);
        named_groups = new_map();

        /* Only loop a single time without /g */
        if (!(flags & FIND_ALL) && loops == 1)
            break;
    }

    free_entry(entry);
    free_var(arglist);
    return make_var_pack(ret);
}

static void free_entry(pcre_cache_entry *entry)
{
    if (--entry->refcount > 0) {
        return;
    }
    if (entry->re != nullptr)
        pcre_free(entry->re);

    if (entry->error != nullptr)
        free_str(entry->error);

    if (entry->extra != nullptr) {
#ifdef PCRE_CONFIG_JIT
        pcre_free_study(entry->extra);
#else
        pcre_free(entry->extra);
#endif
    }

    free(entry);
}

static void delete_cache_entry(const char *pattern, unsigned char options)
{
    cache_type pair = std::make_pair(pattern, options);
    pthread_mutex_lock(&cache_mutex);
    auto it = pcre_pattern_cache.find(pair);
    auto entry = *it;
    pcre_pattern_cache.erase(it);
    free_str(entry.first.first);
    free_entry(entry.second);
    pthread_mutex_unlock(&cache_mutex);
}

/* Create a two element list with the substring indices. */
static Var result_indices(int ovector[], int n)
{
    Var pos = new_list(2);
    pos.v.list[1].type = TYPE_INT;
    pos.v.list[2].type = TYPE_INT;

    pos.v.list[2].v.num = ovector[2 * n + 1];
    pos.v.list[1].v.num = ovector[2 * n] + 1;
    return pos;
}

static package
bf_pcre_replace(Var arglist, Byte next, void *vdata, Objid progr)
{
    const char *linebuf = arglist.v.list[1].v.str;
    const char *pattern = arglist.v.list[2].v.str;

    int err;
    pcrs_job *job = pcrs_compile_command(pattern, &err);

    if (job == nullptr)
    {
        free_var(arglist);
        char error_msg[255];
        sprintf(error_msg, "Compile error:  %s (%d)", pcrs_strerror(err), err);
        return make_raise_pack(E_INVARG, error_msg, var_ref(zero));
    }

    char *result;
    size_t length = memo_strlen(linebuf);

    err = pcrs_execute(job, linebuf, length, &result, &length);
    if (err >= 0)
    {
        /* Sanitize the result so people don't introduce 'dangerous' characters into the database */
        char *p = result;
        while (*p)
        {
            if (!isprint(*p))
                *p = ' ';
            p++;
        }

        Var ret;
        ret.type = TYPE_STR;
        ret.v.str = str_dup(result);

        free_var(arglist);
        pcrs_free_job(job);
        free(result);

        return make_var_pack(ret);
    } else {
        free_var(arglist);
        char error_msg[255];
        sprintf(error_msg, "Exec error:  %s (%d)", pcrs_strerror(err), err);
        return make_raise_pack(E_INVARG, error_msg, var_ref(zero));
    }
}

static package
bf_pcre_cache_stats(Var arglist, Byte next, void *vdata, Objid progr)
{
    free_var(arglist);

    if (!is_wizard(progr))
        return make_error_pack(E_PERM);

    Var ret = new_list(pcre_pattern_cache.size());

    int count = 0;
    for (const auto& x : pcre_pattern_cache) {
        count++;
        Var entry = new_list(2);
        entry.v.list[1] = str_dup_to_var(x.first.first);
        entry.v.list[2] = Var::new_int(x.second->cache_hits);
        ret.v.list[count] = entry;
    }

    return make_var_pack(ret);
}

void
pcre_shutdown(void)
{
    for (const auto& x : pcre_pattern_cache) {
        free_entry(x.second);
        free_str(x.first.first);
    }

    pcre_pattern_cache.clear();
    pthread_mutex_destroy(&cache_mutex);
}

#ifdef SQLITE3_FOUND
void sqlite_regexp(sqlite3_context *ctx, int argc, sqlite3_value **argv)
{
    const char *pattern = (const char *)sqlite3_value_text(argv[0]);
    if (!pattern)
    {
        sqlite3_result_error(ctx, "SQLite REGEXP called with invalid pattern.", -1);
        return;
    }

    const char *string = (const char *)sqlite3_value_text(argv[1]);
    if (!string)
    {
        sqlite3_result_error(ctx, "SQLite REGEXP called with invalid string.", -1);
        return;
    }

    struct pcre_cache_entry *entry = get_pcre(pattern, 0);
    if (entry->error != nullptr)
    {
        sqlite3_result_error(ctx, entry->error, -1);
        free_entry(entry);
        return;
    }

    int result = pcre_exec(entry->re, entry->extra, string, strlen(string), 0, 0, nullptr, 0);

    free_entry(entry);
    sqlite3_result_int(ctx, result >= 0);
}
#endif /* SQLITE3_FOUND */

void
register_pcre() {
    oklog("REGISTER_PCRE: Using PCRE Library v%s"
#ifdef PCRE_CONFIG_JIT
    " (JIT)"
#endif
    "\n", pcre_version());

    //                                                   string    pattern   ?case     ?find_all
    register_function("pcre_match", 2, 4, bf_pcre_match, TYPE_STR, TYPE_STR, TYPE_INT, TYPE_INT);
    register_function("pcre_replace", 2, 2, bf_pcre_replace, TYPE_STR, TYPE_STR);
    register_function("pcre_cache_stats", 0, 0, bf_pcre_cache_stats);

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);

    pthread_mutex_init(&cache_mutex, &attr);

    pthread_mutexattr_destroy(&attr);
}

#else /* PCRE_FOUND */
void register_pcre(void) { }
#endif /* PCRE_FOUND */
