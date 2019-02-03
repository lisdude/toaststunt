#include "options.h"

#ifdef HAS_ARGON2

#include <argon2.h>

#include "functions.h"
#include "utils.h"
#include "log.h"

static package
bf_argon2(Var arglist, Byte next, void *vdata, Objid progr)
{
    if (!is_wizard(progr)) {
        free_var(arglist);
        return make_error_pack(E_PERM);
    }

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

    if (saltlen > UINT32_MAX) {
        free_var(arglist);
        return make_raise_pack(E_INVARG, "salt too long", var_ref(zero));
    }

    unsigned char *out = (unsigned char *)malloc(outlen + 1);
    if (!out) {
        free_var(arglist);
        return make_raise_pack(E_QUOTA, "could not allocate memory for output", var_ref(zero));
    }

    size_t encodedlen = argon2_encodedlen(t_cost, m_cost, parallelism, saltlen, outlen, type);
    char *encoded = (char *)mymalloc(encodedlen + 1, M_STRING);
    if (!encoded) {
        free_var(arglist);
        free(out);
        return make_raise_pack(E_QUOTA, "could not allocate memory for hash", var_ref(zero));
    }

    int result = argon2_hash(t_cost, m_cost, parallelism, str, len, salt, saltlen, out, outlen, encoded, encodedlen, type, ARGON2_VERSION_NUMBER);
    if (result != ARGON2_OK) {
        free_var(arglist);
        free(out);
        myfree(encoded, M_STRING);
        return make_raise_pack(E_INVIND, argon2_error_message(result), var_ref(zero));
    }

    Var r;
    r.type = TYPE_STR;
    r.v.str = encoded;
    free_var(arglist);
    free(out);

    return make_var_pack(r);
}

static package
bf_argon2_verify(Var arglist, Byte next, void *vdata, Objid progr)
{
    if (!is_wizard(progr)) {
        free_var(arglist);
        return make_error_pack(E_PERM);
    }

    const char *encoded = arglist.v.list[1].v.str;
    const char *str = arglist.v.list[2].v.str;
    const size_t len = strlen(str);

    int result = argon2_verify(encoded, str, len, Argon2_id);

    free_var(arglist);

    Var r;
    r.type = TYPE_INT;

    if (result != ARGON2_OK)
        r.v.num = 0;
    else
        r.v.num = 1;

    return make_var_pack(r);
}

void
register_argon2(void)
{
    oklog("REGISTER_ARGON2: Using Argon2 version %i\n", ARGON2_VERSION_NUMBER);
    //                                           password, salt,     iterations, memory, parallelism
    register_function("argon2", 2, 5, bf_argon2, TYPE_STR, TYPE_STR, TYPE_INT, TYPE_INT, TYPE_INT);
    register_function("argon2_verify", 2, 2, bf_argon2_verify, TYPE_STR, TYPE_STR);
}

#else /* HAS_ARGON2 */
void register_argon2(void) { }
#endif /* HAS_ARGON2 */
