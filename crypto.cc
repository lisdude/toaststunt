/******************************************************************************
  Copyright 2013 Todd Sundsted. All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice,
       this list of conditions and the following disclaimer.

    2. Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY TODD SUNDSTED ``AS IS'' AND ANY EXPRESS OR
  IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
  EVENT SHALL TODD SUNDSTED OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
  OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
  EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

  The views and conclusions contained in the software and documentation are
  those of the authors and should not be interpreted as representing official
  policies, either expressed or implied, of Todd Sundsted.
 *****************************************************************************/

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

#include "functions.h"
#include "crypto.h"
#include "list.h"
#include "nettle/hmac.h"
#include "nettle/md5.h"
#include "nettle/ripemd160.h"
#include "nettle/sha1.h"
#include "nettle/sha2.h"
#include "random.h"
#include "server.h"
#include "storage.h"
#include "utils.h"

/* supported algorithms */

static int algorithms = 0;

#define MD5    (1<<1)
#define SHA256 (1<<2)
#define SHA512 (1<<3)
#define BCRYPT (1<<4)

#define HAS_MD5    (algorithms | MD5)
#define HAS_SHA256 (algorithms | SHA256)
#define HAS_SHA512 (algorithms | SHA512)
#define HAS_BCRYPT (algorithms | BCRYPT)

extern "C" {

    extern unsigned char _crypt_itoa64[];

    extern char *_crypt_gensalt_md5_rn(const char *, unsigned long,
                                       const char *, int,
                                       char *, int);
    extern char *_crypt_gensalt_sha2_rn(const char *, unsigned long,
                                        const char *, int,
                                        char *, int);
    extern char *_crypt_gensalt_blowfish_rn(const char *, unsigned long,
                                            const char *, int,
                                            char *, int);
    extern char *_crypt_gensalt_traditional_rn(const char *, unsigned long,
                                               const char *, int,
                                               char *, int);
    extern char *_crypt_blowfish_rn(const char *, const char *,
                                    char *, int);
}

/* Parse a salt prefix.  Identify the format, the count (AKA rounds or
 * work factor), and the start of any trailing characters not part of
 * the prefix.  Returns 1 if successful, otherwise 0.
 */
static int
parse_prefix(const char *prefix, size_t prefix_length,
             const char **rest, size_t *rest_length,
             unsigned long *count, int *format)
{
    *count = 0;
    *format = 0;
    *rest = prefix;
    *rest_length = prefix_length;

    char *str_end;

    if (3 <= prefix_length &&
        (('1' == prefix[1] && HAS_MD5) ||
         ('5' == prefix[1] && HAS_SHA256) ||
         ('6' == prefix[1] && HAS_SHA512))) {
	if (!strncmp("$1$", prefix, 3)) {
	    *format = MD5;
	    *rest = prefix + 3;
	    *rest_length = prefix_length - 3;
	    return 1;
	}
	else if (!strncmp("$5$", prefix, 3))
	    *format = SHA256;
	else if (!strncmp("$6$", prefix, 3))
	    *format = SHA512;
	else
	    return 0;
	int i;
	for (i = prefix_length - 1; 3 <= i; i--)
	    if (prefix[i] == '$')
		break;
	if (3 <= i && 11 <= prefix_length && !strncmp("rounds=", prefix + 3, 7)) {
	    errno = 0;
	    if ((*count = strtol(prefix + 10, &str_end, 10), prefix + 10 < str_end) &&
	          prefix + i == str_end &&
	          *count >= 1000 && *count <= 999999999 &&
	          !errno) {
		*rest = str_end + 1;
		*rest_length = prefix_length - (*rest - prefix);
		return 1;
	    }
	    else
		return 0;
	}
	else if (3 <= i)
	    return 0;
	*rest = prefix + 3;
	*rest_length = prefix_length - 3;
	return 1;
    }
    else if (4 <= prefix_length &&
             prefix[0] == '$' && prefix[1] == '2' &&
             (prefix[2] == 'a' || prefix[2] == 'x' || prefix[2] == 'y') &&
             prefix[3] == '$') {
	*format = BCRYPT;
	int i;
	for (i = prefix_length - 1; 4 <= i; i--)
	    if (prefix[i] == '$')
		break;
	if (4 <= i && 7 <= prefix_length) {
	    errno = 0;
	    if ((*count = strtol(prefix + 4, &str_end, 10), prefix + 4 < str_end) &&
	          prefix + i == str_end &&
	          *count >= 4 && *count <= 31 &&
	          !errno) {
		*rest = str_end + 1;
		*rest_length = prefix_length - (*rest - prefix);
		return 1;
	    }
	    else
		return 0;
	}
	else if (4 <= i)
	    return 0;
	*rest = prefix + 4;
	*rest_length = prefix_length - 4;
	return 1;
    }

    return 1;
}

static char digits[] = "0123456789ABCDEF";

#define DEF_HASH(algo, size)							\
static const char *								\
algo##_hash_bytes(const char *input, int length, int binary)			\
{										\
    algo##_ctx context;								\
    unsigned char result[size];							\
    char *hex = (char *)mymalloc(size * (binary ? 3 : 2) + 1, M_STRING);	\
    const char *answer = hex;							\
    algo##_init(&context);							\
    algo##_update(&context, length, (unsigned char *)input);			\
    algo##_digest(&context, size, result);					\
    for (int i = 0; i < size; i++) {						\
	if (binary) *hex++ = '~';						\
	*hex++ = digits[result[i] >> 4];					\
	*hex++ = digits[result[i] & 0xF];					\
    }										\
    *hex = 0;									\
    return answer;								\
}

DEF_HASH(md5, 16)
DEF_HASH(sha1, 20)
DEF_HASH(sha224, 28)
DEF_HASH(sha256, 32)
DEF_HASH(sha384, 48)
DEF_HASH(sha512, 64)
DEF_HASH(ripemd160, 20)

#undef DEF_HASH

#define DEF_HMAC(algo, size)										\
static const char *											\
algo##_bytes(const char *message, int message_length, const char *key, int key_length, int binary)	\
{													\
    algo##_ctx context;											\
    unsigned char result[size];										\
    char *hex = (char *)mymalloc(size * (binary ? 3 : 2) + 1, M_STRING);				\
    const char *answer = hex;										\
    algo##_set_key(&context, key_length, (unsigned char *)key);						\
    algo##_update(&context, message_length, (unsigned char *)message);					\
    algo##_digest(&context, size, result);								\
    for (int i = 0; i < size; i++) {									\
	if (binary) *hex++ = '~';									\
	*hex++ = digits[result[i] >> 4];								\
	*hex++ = digits[result[i] & 0xF];								\
    }													\
    *hex = 0;												\
    return answer;											\
}

DEF_HMAC(hmac_sha1, 20)
DEF_HMAC(hmac_sha256, 32)

#undef DEF_HMAC

/**** built in functions ****/

static package
bf_salt(Var arglist, Byte next, void *vdata, Objid progr)
{				/* (prefix, input) */
    Var r;
    package p;

    const char *prefix = arglist.v.list[1].v.str;
    size_t prefix_length = memo_strlen(prefix);
    const char *input = arglist.v.list[2].v.str;

    const char *rest;
    size_t rest_length;
    unsigned long count;
    int format;

    if (!parse_prefix(prefix, prefix_length, &rest, &rest_length, &count, &format)) {
	p = make_raise_pack(E_INVARG, "Invalid prefix", var_ref(arglist.v.list[1]));
	free_var(arglist);
	return p;
    }

    char *(*use)(const char *, unsigned long,
                 const char *, int,
                 char *, int);

    if (MD5 == format)
	use = _crypt_gensalt_md5_rn;
    else if (SHA256 == format || SHA512 == format)
	use = _crypt_gensalt_sha2_rn;
    else if (BCRYPT == format)
	use = _crypt_gensalt_blowfish_rn;
    else if (!prefix[0] ||
             (2 <= prefix_length &&
              memchr(_crypt_itoa64, prefix[0], 64) &&
              memchr(_crypt_itoa64, prefix[1], 64)))
	use = _crypt_gensalt_traditional_rn;
    else {
	p = make_raise_pack(E_INVARG, "Invalid prefix", var_ref(arglist.v.list[1]));
	free_var(arglist);
	return p;
    }

    int random_length;

    const char *random = binary_to_raw_bytes(input, &random_length);

    if (NULL == random) {
	p = make_raise_pack(E_INVARG, "Invalid binary input", var_ref(arglist.v.list[2]));
	free_var(arglist);
	return p;
    }

    errno = 0;

    char output[64];
    char *ret = use(prefix, count, random, random_length, output, sizeof(output));

    if (errno) {
	p = make_error_pack(E_INVARG);
	free_var(arglist);
	return p;
    }

    r.type = TYPE_STR;
    r.v.str = str_dup(ret);

    free_var(arglist);

    return make_var_pack(r);
}

static package
bf_crypt(Var arglist, Byte next, void *vdata, Objid progr)
{				/* (string, [salt]) */
    Var r;
    package p;

    const char *salt;
    int salt_length;

    static char saltstuff[] =
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789./";

    char temp[3];

    if (arglist.v.list[0].v.num == 1 || memo_strlen(arglist.v.list[2].v.str) < 2) {
	/* provide a random 2-letter salt, works with old and new crypts */
	temp[0] = saltstuff[RANDOM() % (int) strlen(saltstuff)];
	temp[1] = saltstuff[RANDOM() % (int) strlen(saltstuff)];
	temp[2] = '\0';
	salt = temp;
	salt_length = 2;
    } else {
	salt = arglist.v.list[2].v.str;
	salt_length = memo_strlen(arglist.v.list[2].v.str);
    }

    const char *rest;
    size_t rest_length;
    unsigned long count;
    int format;

    int success = parse_prefix(salt, salt_length, &rest, &rest_length, &count, &format);

    if (!success) {
	r.type = TYPE_STR;
	r.v.str = str_dup(salt);
	p = make_raise_pack(E_INVARG, "Invalid salt", r);
	free_var(arglist);
	return p;
    }
    if (!is_wizard(progr) &&
        ((BCRYPT == format && count != 5) ||
         (BCRYPT != format && count))) {
	p = make_raise_pack(E_PERM, "Cannot specify non-default strength", Var::new_int(count));
	free_var(arglist);
	return p;
    }

    if (BCRYPT == format) {
	errno = 0;

	char output[64];
	char *ret = _crypt_blowfish_rn(arglist.v.list[1].v.str, salt, output, sizeof(output));

	if (errno) {
	    free_var(arglist);
	    return make_error_pack(E_INVARG);
	}

	r.type = TYPE_STR;
	r.v.str = str_dup(ret);
    }
    else {
#if HAVE_CRYPT
	r.type = TYPE_STR;
	r.v.str = str_dup(crypt(arglist.v.list[1].v.str, salt));
#else
	r.type = TYPE_STR;
	r.v.str = str_ref(arglist.v.list[1].v.str);
#endif
    }

    free_var(arglist);

    return make_var_pack(r);
}

#define TRY_STREAM enable_stream_exceptions()
#define ENDTRY_STREAM disable_stream_exceptions()

static package
make_space_pack()
{
    if (server_flag_option_cached(SVO_MAX_CONCAT_CATCHABLE))
	return make_error_pack(E_QUOTA);
    else
	return make_abort_pack(ABORT_SECONDS);
}

static package
bf_string_hash(Var arglist, Byte next, void *vdata, Objid progr)
{
    Var r;
    int nargs = arglist.v.list[0].v.num;
    const char *str = arglist.v.list[1].v.str;
    const char *algo = (1 < nargs) ? arglist.v.list[2].v.str : "sha256";
    int binary = (2 < nargs) ? is_true(arglist.v.list[3]) : 0;

#define CASE(op, temp)							\
    op (!mystrcasecmp(#temp, algo)) {					\
        r.type = TYPE_STR;						\
        r.v.str = temp##_hash_bytes(str, memo_strlen(str), binary);	\
    }

    CASE(if, md5)
    CASE(else if, sha1)
    CASE(else if, sha224)
    CASE(else if, sha256)
    CASE(else if, sha384)
    CASE(else if, sha512)
    CASE(else if, ripemd160)
    else {
	free_var(arglist);
	return make_error_pack(E_INVARG);
    }

#undef CASE

    free_var(arglist);
    return make_var_pack(r);
}

static package
bf_binary_hash(Var arglist, Byte next, void *vdata, Objid progr)
{
    package p;

    TRY_STREAM;
    try {
	Var r;
	int length;
	int nargs = arglist.v.list[0].v.num;
	const char *bytes = binary_to_raw_bytes(arglist.v.list[1].v.str, &length);
	const char *algo = (1 < nargs) ? arglist.v.list[2].v.str : "sha256";
	int binary = (2 < nargs) ? is_true(arglist.v.list[3]) : 0;

#define CASE(op, temp)							\
	op (!mystrcasecmp(#temp, algo)) {				\
	    r.type = TYPE_STR;						\
	    r.v.str = temp##_hash_bytes(bytes, length, binary);		\
	    p = make_var_pack(r);					\
	}

	if (!bytes) {
	    p = make_error_pack(E_INVARG);
	}
	CASE(else if, md5)
	CASE(else if, sha1)
	CASE(else if, sha224)
	CASE(else if, sha256)
	CASE(else if, sha384)
	CASE(else if, sha512)
	CASE(else if, ripemd160)
	else {
	  p = make_error_pack(E_INVARG);
	}

#undef CASE

    }
    catch (stream_too_big& exception) {
	p = make_space_pack();
    }
    ENDTRY_STREAM;

    free_var(arglist);
    return p;
}

static package
bf_value_hash(Var arglist, Byte next, void *vdata, Objid progr)
{
    package p;
    Stream *s = new_stream(100);

    TRY_STREAM;
    try {
	Var r;
	int nargs = arglist.v.list[0].v.num;
	const char *algo = (1 < nargs) ? arglist.v.list[2].v.str : "sha256";
	int binary = (2 < nargs) ? is_true(arglist.v.list[3]) : 0;

	unparse_value(s, arglist.v.list[1]);

#define CASE(op, temp)									\
	op (!mystrcasecmp(#temp, algo)) {						\
	    r.type = TYPE_STR;								\
	    r.v.str = temp##_hash_bytes(stream_contents(s), stream_length(s), binary);	\
	    p = make_var_pack(r);							\
	}

	CASE(if, md5)
	CASE(else if, sha1)
	CASE(else if, sha224)
	CASE(else if, sha256)
	CASE(else if, sha384)
	CASE(else if, sha512)
	CASE(else if, ripemd160)
	else {
	    p = make_error_pack(E_INVARG);
	}

#undef CASE

    }
    catch (stream_too_big& exception) {
	p = make_space_pack();
    }
    ENDTRY_STREAM;

    free_stream(s);
    free_var(arglist);
    return p;
}

static package
bf_string_hmac(Var arglist, Byte next, void *vdata, Objid progr)
{
    package p;
    Stream *s = new_stream(100);

    TRY_STREAM;
    try {
	Var r;

	int nargs = arglist.v.list[0].v.num;
	const char *algo = (2 < nargs) ? arglist.v.list[3].v.str : "sha256";
	int binary = (3 < nargs) ? is_true(arglist.v.list[4]) : 0;

	const char *str = arglist.v.list[1].v.str;
	int str_length = memo_strlen(str);

	int key_length;
	const char *key = binary_to_raw_bytes(arglist.v.list[2].v.str, &key_length);

	if (!key) {
	    p = make_error_pack(E_INVARG);
	}
	else {
	    char *key_new = (char *)mymalloc(key_length, M_STRING);
	    memcpy(key_new, key, key_length);
	    key = key_new;

#define CASE(op, temp)										\
	    op (!mystrcasecmp(#temp, algo)) {							\
		r.type = TYPE_STR;								\
		r.v.str = hmac_##temp##_bytes(str, str_length, key, key_length, binary);	\
		p = make_var_pack(r);								\
	    }

	    CASE(if, sha1)
	    CASE(else if, sha256)
	    else {
		p = make_error_pack(E_INVARG);
	    }

#undef CASE

	    free_str(key);
	}
    }
    catch (stream_too_big& exception) {
	p = make_space_pack();
    }
    ENDTRY_STREAM;

    free_stream(s);
    free_var(arglist);
    return p;
}

static package
bf_binary_hmac(Var arglist, Byte next, void *vdata, Objid progr)
{
    package p;
    Stream *s = new_stream(100);

    TRY_STREAM;
    try {
	Var r;

	int nargs = arglist.v.list[0].v.num;
	const char *algo = (2 < nargs) ? arglist.v.list[3].v.str : "sha256";
	int binary = (3 < nargs) ? is_true(arglist.v.list[4]) : 0;

	int bytes_length;
	const char *bytes = binary_to_raw_bytes(arglist.v.list[1].v.str, &bytes_length);

	if (!bytes) {
	    p = make_error_pack(E_INVARG);
	}
	else {
	    char *bytes_new = (char *)mymalloc(bytes_length, M_STRING);
	    memcpy(bytes_new, bytes, bytes_length);
	    bytes = bytes_new;

	    int key_length;
	    const char *key = binary_to_raw_bytes(arglist.v.list[2].v.str, &key_length);

	    if (!key) {
		free_str(bytes);
		p = make_error_pack(E_INVARG);
	    }
	    else {
		char *key_new = (char *)mymalloc(key_length, M_STRING);
		memcpy(key_new, key, key_length);
		key = key_new;

#define CASE(op, temp)											\
		op (!mystrcasecmp(#temp, algo)) {							\
		    r.type = TYPE_STR;									\
		    r.v.str = hmac_##temp##_bytes(bytes, bytes_length, key, key_length, binary);	\
		    p = make_var_pack(r);								\
		}

		CASE(if, sha1)
		CASE(else if, sha256)
		else {
		    p = make_error_pack(E_INVARG);
		}

#undef CASE

		free_str(bytes);
		free_str(key);
	    }
	}
    }
    catch (stream_too_big& exception) {
	p = make_space_pack();
    }
    ENDTRY_STREAM;

    free_stream(s);
    free_var(arglist);
    return p;
}

static package
bf_value_hmac(Var arglist, Byte next, void *vdata, Objid progr)
{
    package p;
    Stream *s = new_stream(100);

    TRY_STREAM;
    try {
	Var r;

	int nargs = arglist.v.list[0].v.num;
	const char *algo = (2 < nargs) ? arglist.v.list[3].v.str : "sha256";
	int binary = (3 < nargs) ? is_true(arglist.v.list[4]) : 0;

	unparse_value(s, arglist.v.list[1]);
	const char *lit = str_dup(stream_contents(s));
	int lit_length = stream_length(s);

	int key_length;
	const char *key = binary_to_raw_bytes(arglist.v.list[2].v.str, &key_length);

	if (!key) {
	    free_str(lit);
	    p = make_error_pack(E_INVARG);
	}
	else {
	    char *key_new = (char *)mymalloc(key_length, M_STRING);
	    memcpy(key_new, key, key_length);
	    key = key_new;

#define CASE(op, temp)										\
	    op (!mystrcasecmp(#temp, algo)) {							\
		r.type = TYPE_STR;								\
		r.v.str = hmac_##temp##_bytes(lit, lit_length, key, key_length, binary);	\
		p = make_var_pack(r);								\
	    }

	    CASE(if, sha1)
	    CASE(else if, sha256)
	    else {
		p = make_error_pack(E_INVARG);
	    }

#undef CASE

	    free_str(lit);
	    free_str(key);
	}
    }
    catch (stream_too_big& exception) {
	p = make_space_pack();
    }
    ENDTRY_STREAM;

    free_stream(s);
    free_var(arglist);
    return p;
}

#undef TRY_STREAM
#undef ENDTRY_STREAM

void
register_crypto(void)
{
    if (!strncmp("$1$", crypt("password", "$1$"), 3))
	algorithms = HAS_MD5;
    if (!strncmp("$5$", crypt("password", "$5$"), 3))
	algorithms = HAS_SHA256;
    if (!strncmp("$6$", crypt("password", "$6$"), 3))
	algorithms = HAS_SHA512;
    algorithms = HAS_BCRYPT;

    register_function("salt", 2, 2, bf_salt, TYPE_STR, TYPE_STR);
    register_function("crypt", 1, 2, bf_crypt, TYPE_STR, TYPE_STR);

    register_function("string_hash", 1, 3, bf_string_hash, TYPE_STR, TYPE_STR, TYPE_ANY);
    register_function("binary_hash", 1, 3, bf_binary_hash, TYPE_STR, TYPE_STR, TYPE_ANY);
    register_function("value_hash", 1, 3, bf_value_hash, TYPE_ANY, TYPE_STR, TYPE_ANY);

    register_function("string_hmac", 2, 4, bf_string_hmac, TYPE_STR, TYPE_STR, TYPE_STR, TYPE_ANY);
    register_function("binary_hmac", 2, 4, bf_binary_hmac, TYPE_STR, TYPE_STR, TYPE_STR, TYPE_ANY);
    register_function("value_hmac", 2, 4, bf_value_hmac, TYPE_ANY, TYPE_STR, TYPE_STR, TYPE_ANY);
}
