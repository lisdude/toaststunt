#include "extension-pcre.h"

static struct pcre_cache_entry *
get_pcre(const char *string, unsigned char options) {
    const char *err;
    int eos; /* Error offset */
    char buf[256];

    pcre_cache_entry *entry = (pcre_cache_entry*)malloc(sizeof(pcre_cache_entry));
    entry->error = NULL;
    entry->re = NULL;
    entry->captures = 0;
    entry->extra = NULL;

    entry->re = pcre_compile(string, options, &err, &eos, NULL);
    if (entry->re == NULL) {
        sprintf(buf, "PCRE compile error at offset %d: %s", eos, err);
        entry->error = str_dup(buf);
    } else {
        const char *error = NULL;
        entry->extra = pcre_study(entry->re, 0, &error);
        if (error != NULL)
            entry->error = str_dup(error);
        else {
            (void)pcre_fullinfo(entry->re, NULL, PCRE_INFO_CAPTURECOUNT, &(entry->captures));
        }
    }

    return entry;
}

static package
bf_pcre_match(Var arglist, Byte next, void *vdata, Objid progr) {
    /* Some useful constants. */
    static Var match = nothing;
    if (match.v.obj == NOTHING) {
        match.type = TYPE_STR;
        match.v.str = str_dup("match");
    }
    static Var position = nothing;
    if (position.v.obj == NOTHING) {
        position.type = TYPE_STR;
        position.v.str = str_dup("position");
    }
    /**************************/

    const char *subject, *pattern;
    char err[256]; /* Our general-purpose error holder. Handy! */
    unsigned char options = 0;
    unsigned char flags = FIND_ALL;

    subject = str_ref(arglist.v.list[1].v.str);
    pattern = str_ref(arglist.v.list[2].v.str);
    options = (arglist.v.list[0].v.num >= 3 && is_true(arglist.v.list[3])) ? 0 : PCRE_CASELESS;

    if (arglist.v.list[0].v.num >= 4 && arglist.v.list[4].v.num == 0)
        flags ^= FIND_ALL;

    free_var(arglist);

    /* Return E_INVARG if the pattern or subject are empty. */
    if (pattern[0] == '\0' || subject[0] == '\0')
        return make_error_pack(E_INVARG);

    /* Compile the pattern */
    struct pcre_cache_entry *entry = get_pcre(pattern, options);

    if (entry->error != NULL)
    {
        package r = make_raise_pack(E_INVARG, entry->error, var_ref(zero));
        free_entry(entry);
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
            free_entry(entry);
            sprintf(err, "pcre_exec returned error: %d", rc);
            return make_raise_pack(E_INVARG, err, var_ref(zero));
        } else if (rc == 0) {
            /* We don't have enough room to store all of these substrings. */
            free_entry(entry);
            sprintf(err, "pcre_exec only has room for %d substrings", entry->captures);
            return make_raise_pack(E_QUOTA, err, var_ref(zero));
        } else if (rc == PCRE_ERROR_NOMATCH) {
            /* There are no more matches. */
            break;
        } else if (loops >= total_loops) {
            /* The loop has iterated beyond the maximum limit, probably locking the server. Kill it. */
            free_entry(entry);
            sprintf(err, "Too many iterations of matching loop: %d", loops);
            return make_raise_pack(E_MAXREC, err, var_ref(zero));
        } else {
            /* We'll use a bit array to indicate which index matches are superfluous. e.g. which results
             * have a NAMED result instead of a numbered result. I'm definitely open to better ideas! */
            static unsigned char *bit_array;
            bit_array = (unsigned char *)mymalloc(rc * sizeof(unsigned char), M_ARRAY);
            memset(bit_array, 0, rc);
            (void)pcre_fullinfo(entry->re, NULL, PCRE_INFO_NAMECOUNT, &named_substrings);

            if (named_substrings > 0)
            {
                unsigned char *name_table, *tabptr;
                int name_entry_size;

                (void)pcre_fullinfo(entry->re, NULL, PCRE_INFO_NAMETABLE, &name_table);
                (void)pcre_fullinfo(entry->re, NULL, PCRE_INFO_NAMEENTRYSIZE, &name_entry_size);

                tabptr = name_table;
                for (int i = 0; i < named_substrings; i++)
                {
                    /* Determine which result number corresponds to the named capture group */
                    int n = (tabptr[0] << 8) | tabptr[1];
                    /* Create a list of indices for the substring */
                    Var pos = result_indices(ovector, n);
                    Var result = new_map();
                    int substring_size = ovector[2*n+1] - ovector[2*n];
                    result = mapinsert(result, var_ref(position), pos);

                    /* Extract the substring itself with obnoxious printf magic */
                    char *substring = (char *)mymalloc(substring_size + 1, M_STRING);
                    sprintf(substring, "%.*s", substring_size, subject + ovector[2*n]);
                    result = mapinsert(result, var_ref(match), str_dup_to_var(substring));
                    myfree(substring, M_STRING);

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
    return make_var_pack(ret);
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

/* Create a two element list with the substring indices. */
Var result_indices(int ovector[], int n)
{
    Var pos = new_list(2);
    pos.v.list[1].type = TYPE_INT;
    pos.v.list[2].type = TYPE_INT;

    pos.v.list[2].v.num = ovector[2*n+1];
    pos.v.list[1].v.num = ovector[2*n] + 1;
    return pos;
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
    //                                                   string    pattern   ?case     ?find_all
    register_function("pcre_match", 2, 4, bf_pcre_match, TYPE_STR, TYPE_STR, TYPE_INT, TYPE_INT);
    register_function("pcre_replace", 2, 2, bf_pcre_replace, TYPE_STR, TYPE_STR);
}

