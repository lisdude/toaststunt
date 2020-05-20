#include "options.h"

#ifdef ASPELL_FOUND

#include "bf_register.h"
#include "functions.h"
#include "log.h"
#include "utils.h"
#include "list.h"

#include <aspell.h>

#define EXT_SPELLCHECK_VERSION "1.0"

static package
bf_spellcheck(Var arglist, Byte next, void *vdata, Objid progr) {
    Var r;
    static AspellConfig *spell_config;
    if (!spell_config)
    {
        spell_config = new_aspell_config();
        aspell_config_replace(spell_config, "lang", "en_US");
    }
    static AspellCanHaveError *possible_err;
    if (!possible_err)
    {
        possible_err = new_aspell_speller(spell_config);
    }
    static AspellSpeller *spell_checker = nullptr;
    if (aspell_error_number(possible_err) != 0)
    {
        free_var(arglist);
        errlog("SPELLCHECK: Failed to initialize aspell: %s\n", aspell_error_message(possible_err));
        r.type = TYPE_ERR;
        return make_error_pack(E_INVARG);
    }
    else if (!spell_checker)
    {
        spell_checker = to_aspell_speller(possible_err);
    }

    const char *word = arglist.v.list[1].v.str;
    int word_size = memo_strlen(arglist.v.list[1].v.str);

    int correct = aspell_speller_check(spell_checker, word, word_size);
    if (!correct) {
        r = new_list(0);
        Var s;
        s.type = TYPE_STR;
        const AspellWordList *suggestions = aspell_speller_suggest(spell_checker, word, word_size);
        AspellStringEnumeration *elements = aspell_word_list_elements(suggestions);
        const char *word_suggestion;
        while ((word_suggestion = aspell_string_enumeration_next(elements)) != nullptr)
        {
            s.v.str = str_dup(word_suggestion);
            r = listappend(r, s);
        }
        delete_aspell_string_enumeration(elements);
    } else {
        r = Var::new_int(correct);
    }
    free_var(arglist);
    return make_var_pack(r);
}

void register_spellcheck(void)
{
    // FreeBSD / macOS/REL aspell doesn't include version string for some reason. (Maybe just remove this?)
#if !defined(__FreeBSD__) && !defined(__MACH__) && !defined(USING_REL)
    oklog("REGISTER_SPELLCHECK: v%s (Aspell Library v%s)\n", EXT_SPELLCHECK_VERSION, aspell_version_string());
#else
    oklog("REGISTER_SPELLCHECK: v%s\n", EXT_SPELLCHECK_VERSION);
#endif

    register_function("spellcheck", 1, 1, bf_spellcheck, TYPE_STR);
}

#else /* ASPELL_FOUND */
void register_spellcheck(void) { }
#endif /* ASPELL_FOUND */
