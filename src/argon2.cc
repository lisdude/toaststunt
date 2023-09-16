#include "options.h"

#ifdef ARGON2_FOUND

#include <argon2.h>

#include "functions.h"
#include "utils.h"
#include "log.h"
#include "map.h"
#include "background.h"

static void argon2_thread_callback(Var arglist, Var *r, void *extra_data)
{
    const int nargs = arglist.v.list[0].v.num;

    // password, salt, iterations, memory, parallelism
    const char *str = arglist.v.list[1].v.str;
    const char *salt = arglist.v.list[2].v.str;
    const uint32_t outlen = 32;                                              // Hash output length.
    const argon2_type type = Argon2_id;
    const uint32_t t_cost = nargs >= 3 ? arglist.v.list[3].v.num : 3;        // Iterations
    const uint32_t m_cost = nargs >= 4 ? arglist.v.list[4].v.num : 4096;     // Memory usage (kb)
    const uint32_t parallelism = nargs >= 5 ? arglist.v.list[5].v.num : 1;   // Number of threads
    const size_t saltlen = strlen(salt);
    const size_t len = strlen(str);

    if (saltlen > UINT32_MAX)
        return make_error_map(E_INVARG, "salt too long", r);

    unsigned char *out = (unsigned char *)malloc(outlen + 1);
    if (!out)
        return make_error_map(E_QUOTA, "could not allocate memory for output", r);

    size_t encodedlen = argon2_encodedlen(t_cost, m_cost, parallelism, saltlen, outlen, type);
    char *encoded = (char *)mymalloc(encodedlen + 1, M_STRING);
    if (!encoded) {
        free(out);
        return make_error_map(E_QUOTA, "could not allocate memory for hash", r);
    }

    int result = argon2_hash(t_cost, m_cost, parallelism, str, len, salt, saltlen, out, outlen, encoded, encodedlen, type, ARGON2_VERSION_NUMBER);
    if (result != ARGON2_OK) {
        free(out);
        myfree(encoded, M_STRING);
        return make_error_map(E_INVIND, argon2_error_message(result), r);
    }

    r->type = TYPE_STR;
    r->v.str = encoded;
    free(out);
}

static package
bf_argon2(Var arglist, Byte next, void *vdata, Objid progr)
{
    if (!is_wizard(progr)) {
        free_var(arglist);
        return make_error_pack(E_PERM);
    }

#ifdef THREAD_ARGON2
    return background_thread(argon2_thread_callback, &arglist);
#else
    Var ret;
    argon2_thread_callback(arglist, &ret, nullptr);

    free_var(arglist);
    return make_var_pack(ret);
#endif
}

static void argon2_verify_thread_callback(Var arglist, Var *r, void *extra_data)
{
    const char *encoded = arglist.v.list[1].v.str;
    const char *str = arglist.v.list[2].v.str;
    const size_t len = strlen(str);

    int result = argon2_verify(encoded, str, len, Argon2_id);

    r->type = TYPE_INT;

    if (result != ARGON2_OK)
        r->v.num = 0;
    else
        r->v.num = 1;
}

static package
bf_argon2_verify(Var arglist, Byte next, void *vdata, Objid progr)
{
    if (!is_wizard(progr)) {
        free_var(arglist);
        return make_error_pack(E_PERM);
    }

#ifdef THREAD_ARGON2
    return background_thread(argon2_verify_thread_callback, &arglist);
#else
    Var ret;
    argon2_verify_thread_callback(arglist, &ret, nullptr);

    free_var(arglist);
    return make_var_pack(ret);
#endif
}

void
register_argon2(void)
{
    oklog("REGISTER_ARGON2: Using Argon2 version %i\n", ARGON2_VERSION_NUMBER);
    //                                           password, salt,     iterations, memory, parallelism
    register_function("argon2", 2, 5, bf_argon2, TYPE_STR, TYPE_STR, TYPE_INT, TYPE_INT, TYPE_INT);
    register_function("argon2_verify", 2, 2, bf_argon2_verify, TYPE_STR, TYPE_STR);
}

#else /* ARGON2_FOUND */
void register_argon2(void) { }
#endif /* ARGON2_FOUND */
