/******************************************************************************
 * Perl-compatible Regular Expressions for LambdaMOO.
 *
 * Albori "lisdude" Sninvel <albori@toastsoft.net>
 ******************************************************************************/

#include <pcre.h>
#include "my-string.h"
#include "structures.h"
#include "functions.h"
#include "storage.h"
#include "list.h"
#include "utils.h"
#include "ast.h"
#include "log.h"
#include "pcrs.h"
#include "server.h"

#define EXT_PCRE_VERSION "2.2"
#define DEFAULT_LOOPS 1000
#define RETURN_INDEXES  2
#define RETURN_GROUPS   4
#define FIND_ALL        8

struct pcre_cache_entry {
    char *error;
    pcre *re;
    pcre_extra *extra;
    int captures;
};

void free_pcre_vars(Var *, Var *, Var *, Var *, pcre_cache_entry *);
void free_entry(pcre_cache_entry *);

static struct pcre_cache_entry *
get_pcre(const char *string, unsigned char options) {
    const char *err;
    int eos; // Error offset
    char buf[256];

    pcre_cache_entry *entry = (pcre_cache_entry*)malloc(sizeof(pcre_cache_entry));
    entry->error = NULL;
    entry->re = NULL;
    entry->captures = 0;

    entry->re = pcre_compile(string, options, &err, &eos, NULL);
    if (entry->re == NULL) {
        sprintf(buf, "PCRE compile error at offset %d: %s", eos, err);
        entry->error = str_dup(buf);
    } else {
        const char *error = NULL;
        entry->extra = pcre_study(entry->re, 0, &error);
        if (error != NULL)
            entry->error = str_dup(error);
        else
            (void)pcre_fullinfo(entry->re, NULL, PCRE_INFO_CAPTURECOUNT, &(entry->captures));
    }

    return entry;
}

static package
bf_pcre_match(Var arglist, Byte next, void *vdata, Objid progr) {
    const char *subject, *pattern;
    char err[256]; // Our general-purpose error holder. Handy!
    unsigned char options = 0;
    unsigned char flags = RETURN_GROUPS | FIND_ALL;

    subject = arglist.v.list[1].v.str;
    pattern = arglist.v.list[2].v.str;
    options = (arglist.v.list[0].v.num >= 3 && is_true(arglist.v.list[3])) ? 0 : PCRE_CASELESS;

    if (arglist.v.list[0].v.num >= 4 && arglist.v.list[4].v.num == 0)
        flags ^= RETURN_GROUPS;
    if (arglist.v.list[0].v.num >= 5 && arglist.v.list[5].v.num == 1)
        flags |= RETURN_INDEXES;
    if (arglist.v.list[0].v.num >= 6 && arglist.v.list[6].v.num == 0)
        flags ^= FIND_ALL;

    // Return E_INVARG if the pattern or subject are empty.
    if (pattern[0] == '\0' || subject[0] == '\0')
    {
        free_var(arglist);
        return make_error_pack(E_INVARG);
    }

    // Compile the pattern
    struct pcre_cache_entry *entry = get_pcre(pattern, options);

    if (entry->error != NULL)
    {
        package r = make_raise_pack(E_INVARG, entry->error, var_ref(zero));
        free_var(arglist);
        free_entry(entry);
        return r;
    }

    // Determine how many subpatterns match so we can allocate memory.
    int oveccount = (entry->captures + 1) * 3;
    int ovector[oveccount];

    // Set up the MOO variables to store the final value and intermediaries.
    Var ret, tmp, group;
    ret = new_list(0);
    group = new_list(0);

    // Return indexes come in the form {start, end} so we'll need to
    // create a list to store them temporarily as we work.
    if (flags & RETURN_INDEXES)
    {
        tmp = new_list(2);
        tmp.v.list[1].type = TYPE_INT;
        tmp.v.list[2].type = TYPE_INT;
    } else {
        tmp.type = TYPE_STR;
        tmp.v.str = NULL;
    }

    // Variables pertaining to the main execution loop
    int offset = 0, rc = 0, i = 0;
    int subject_length = memo_strlen(subject);
    unsigned int loops = 0;
    const char *matched_substring;

    // Check for the existence of the pcre_match_max_iterations server option to determine
    // how many iterations of the match loop we'll attempt before giving up.
    unsigned int total_loops = server_int_option("pcre_match_max_iterations", DEFAULT_LOOPS);
    if (total_loops < 100)
        total_loops = 100;
    else if (total_loops >= 100000000)
        total_loops = 100000000;

    // Execute the match.
    while (offset < subject_length)
    {
        loops++;
        rc = pcre_exec(entry->re, entry->extra, subject, subject_length, offset, 0, ovector, oveccount);
        if (rc < 0 && rc != PCRE_ERROR_NOMATCH)
        {
            // We've encountered some funky error. Back out and let them know what it is.
            free_pcre_vars(&ret, &group, &tmp, &arglist, entry);
            sprintf(err, "pcre_exec returned error: %d", rc);
            return make_raise_pack(E_INVARG, err, var_ref(zero));
        } else if (rc == 0) {
            // We don't have enough room to store all of these substrings.
            free_pcre_vars(&ret, &group, &tmp, &arglist, entry);
            sprintf(err, "pcre_exec only has room for %d substrings", entry->captures);
            return make_raise_pack(E_QUOTA, err, var_ref(zero));
        } else if (rc == PCRE_ERROR_NOMATCH) {
            // There are no more matches.
            break;
        } else if (loops >= total_loops) {
            // The loop has iterated beyond the maximum limit, probably locking the server. Kill it.
            free_pcre_vars(&ret, &group, &tmp, &arglist, entry);
            sprintf(err, "Too many iterations of matching loop: %d", loops);
            return make_raise_pack(E_MAXREC, err, var_ref(zero));
        } else {
            // Store the matched substrings.
            for (i = 0; i < rc; i++) {
                pcre_get_substring(subject, ovector, rc, i, &(matched_substring));
                // Store the offsets if tmp is a list or the string if it's not.
                if (tmp.type == TYPE_LIST)
                {
                    tmp.v.list[1].v.num = ovector[2*i] + 1;
                    tmp.v.list[2].v.num = ovector[2*i+1];
                } else {
                    tmp.v.str = str_dup(matched_substring);
                }

                /* Store the resulting substrings either as a group or into the main return var.
                 * group used to use setadd but we were losing results, this now we don't! */
                if (flags & RETURN_GROUPS)
                    group = listappend(group, var_ref(tmp));
                else
                    ret = listappend(ret, var_ref(tmp));

                pcre_free_substring(matched_substring);

                // Begin at the end of the previous match on the next iteration of the loop.
                offset = ovector[1];
            }

            // Store all of our groups (if applicable) into the main return var.
            if ((flags & RETURN_GROUPS) && group.v.list[0].v.num > 0)
            {
                ret = listappend(ret, group);
                group = new_list(0);
            }
        }

        // Only loop a single time without /g
        if (!(flags & FIND_ALL) && loops == 1)
            break;
    }

    free_pcre_vars(NULL, &group, &tmp, &arglist, entry);

    return make_var_pack(ret);
}

/* Free the billions of variables that we had to declare.
 * We give extra checks to the lists to make sure they actually
 * have memory to give up. Also some vars can be null because,
 * well, I'm lazy and it's easier to keep it all together like this. */
void free_pcre_vars(Var *ret, Var *group, Var *tmp, Var *arglist, pcre_cache_entry *entry)
{
    if (arglist != NULL)
        free_var(*arglist);

    if (ret != NULL)
        free_var(*ret);

    if (tmp != NULL)
        free_var(*tmp);

    if (group != NULL && group->type == TYPE_LIST)
        free_var(*group);

    free_entry(entry);
}

void free_entry(pcre_cache_entry *entry)
{
    if (entry->re != NULL)
        pcre_free(entry->re);

    if (entry->error != NULL)
        free_str(entry->error);

    if(entry->extra != NULL) {
#ifdef PCRE_CONFIG_JIT
        pcre_free_study(entry->extra);
#else
        pcre_free(entry->extra);
#endif
    }

    free(entry);
}

static package
bf_pcre_replace(Var arglist, Byte next, void *vdata, Objid progr) {
    const char *linebuf = arglist.v.list[1].v.str;
    const char *pattern = arglist.v.list[2].v.str;

    int err;
    pcrs_job *job = pcrs_compile_command(pattern, &err);

    if (job == NULL)
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
        sprintf(error_msg, "Exec error:  %s (%d)",pcrs_strerror(err), err);
        return make_raise_pack(E_INVARG, error_msg, var_ref(zero));
    }
}

void
register_pcre() {
    oklog("REGISTER_PCRE: v%s (PCRE Library v%s)\n", EXT_PCRE_VERSION, pcre_version());
    //                                                   string    pattern   ?case     ?group    ?indexes  ?find_all
    register_function("pcre_match", 2, 6, bf_pcre_match, TYPE_STR, TYPE_STR, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT);
    register_function("pcre_replace", 2, 2, bf_pcre_replace, TYPE_STR, TYPE_STR);
}

