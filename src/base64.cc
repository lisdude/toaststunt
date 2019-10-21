/******************************************************************************
  Copyright 2010, 2011, 2012, 2013, 2014 Todd Sundsted. All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

   1. Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.

   2. Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.

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

#include <sstream>
#include <string>

#include "base64.h"
#include "functions.h"
#include "log.h"
#include "storage.h"
#include "utils.h"
#include "server.h"

static package
make_space_pack()
{
    if (server_flag_option_cached(SVO_MAX_CONCAT_CATCHABLE))
	return make_error_pack(E_QUOTA);
    else
	return make_abort_pack(ABORT_SECONDS);
}

static const unsigned char base64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static const unsigned char url_safe_base64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static package
bf_encode_base64(Var arglist, Byte next, void *vdata, Objid progr)
{
    const int safe = arglist.v.list[0].v.num > 1 && is_true(arglist.v.list[2]);
    const unsigned char *chars = safe ? url_safe_base64_chars : base64_chars;

    /* check input */

    int len;
    const char *in;

    in = binary_to_raw_bytes(arglist.v.list[1].v.str, &len);

    if (!in) {
	const package pack = make_raise_pack(E_INVARG, "Invalid binary string", var_ref(arglist.v.list[1]));
	free_var(arglist);
	return pack;
    }

    if ((len / 3) * 4 > stream_alloc_maximum) {
	const package pack = make_space_pack();
	free_var(arglist);
	return pack;
    }

    /* encode */

    std::stringstream buffer;
    const unsigned char *endp = (unsigned char *)(in + len);
    const unsigned char *inp = (unsigned char *)in;

    while (endp - inp >= 3) {
	buffer << chars[inp[0] >> 2];
	buffer << chars[((inp[0] & 0x03) << 4) | (inp[1] >> 4)];
	buffer << chars[((inp[1] & 0x0f) << 2) | (inp[2] >> 6)];
	buffer << chars[inp[2] & 0x3f];
	inp += 3;
    }

    if (endp - inp) {
	buffer << chars[inp[0] >> 2];
	if (endp - inp == 2) {
	    buffer << chars[((inp[0] & 0x03) << 4) | (inp[1] >> 4)];
	    buffer << chars[(inp[1] & 0x0f) << 2];
	} else {
	    buffer << chars[(inp[0] & 0x03) << 4];
	    if (!safe)
		buffer << '=';
	}
	if (!safe)
	    buffer << '=';
    }

    /* return */

    Var ret;

    ret.type = TYPE_STR;
    ret.v.str = str_dup(buffer.str().c_str());

    free_var(arglist);

    return make_var_pack(ret);
}

static const unsigned char base64_table[] = {
    80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80,
    80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80,
    80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 62, 80, 80, 80, 63,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 80, 80, 80, 90, 80, 80,
    80,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 80, 80, 80, 80, 80,
    80, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 80, 80, 80, 80, 80,
    80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80,
    80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80,
    80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80,
    80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80,
    80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80,
    80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80,
    80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80,
    80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80
};

static const unsigned char url_safe_base64_table[] = {
    80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80,
    80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80,
    80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 62, 80, 80,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 80, 80, 80, 90, 80, 80,
    80,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 80, 80, 80, 80, 63,
    80, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 80, 80, 80, 80, 80,
    80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80,
    80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80,
    80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80,
    80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80,
    80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80,
    80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80,
    80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80,
    80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80
};

static package
bf_decode_base64(Var arglist, Byte next, void *vdata, Objid progr)
{
    const int safe = arglist.v.list[0].v.num > 1 && is_true(arglist.v.list[2]);
    const unsigned char *table = safe ? url_safe_base64_table : base64_table;

    /* check input */

    unsigned int len;
    const char *in;

    in = arglist.v.list[1].v.str;
    len = memo_strlen(in);

    int i, pad = 0;

    for (i = 0; i < len; i++) {
	const unsigned char tmp = (unsigned char)in[i];
	if (table[tmp] == 80) {
	    const package pack = make_raise_pack(E_INVARG, "Invalid character in encoded data", var_ref(arglist.v.list[1]));
	    free_var(arglist);
	    return pack;
	}
	if (pad && tmp != '=') {
	    const package pack = make_raise_pack(E_INVARG, "Pad character in encoded data", var_ref(arglist.v.list[1]));
	    free_var(arglist);
	    return pack;
	}
	if (tmp == '=')
	    pad++;
    }

    if (pad > 2) {
	const package pack = make_raise_pack(E_INVARG, "Too many pad characters", var_ref(arglist.v.list[1]));
	free_var(arglist);
	return pack;
    }
    if ((len - pad == 1) || (!safe && len % 4)) {
	const package pack = make_raise_pack(E_INVARG, "Invalid length", var_ref(arglist.v.list[1]));
	free_var(arglist);
	return pack;
    }

    if ((len / 4) * 3 > stream_alloc_maximum) {
	const package pack = make_space_pack();
	free_var(arglist);
	return pack;
    }

    /* decode */

    std::stringstream buffer;
    unsigned char ar[4], block[4];

    for (i = 0; i < len + len % 4; i++) {
	if (i < len) {
	    block[i % 4] = table[(unsigned char)in[i]];
	    ar[i % 4] = in[i];
	} else {
	    block[i % 4] = '\0';
	    ar[i % 4] = '=';
	}
	if (i % 4 == 3) {
	    buffer << (char)((block[0] << 2) | (block[1] >> 4));
	    buffer << (char)((block[1] << 4) | (block[2] >> 2));
	    buffer << (char)((block[2] << 6) | block[3]);
	}
    }

    const std::string& tmp = buffer.str();
    const char* out = tmp.c_str();
    int size = tmp.size();

    if (size) {
	if (ar[2] == '=')
	    size -= 2;
	else if (ar[3] == '=')
	    size -= 1;
    }

    /* return */

    Var ret;

    ret.type = TYPE_STR;
    ret.v.str = str_dup(raw_bytes_to_binary(out, size));

    free_var(arglist);

    return make_var_pack(ret);
}

void
register_base64(void)
{
    register_function("encode_base64", 1, 2, bf_encode_base64, TYPE_STR, TYPE_ANY);
    register_function("decode_base64", 1, 2, bf_decode_base64, TYPE_STR, TYPE_ANY);
}
