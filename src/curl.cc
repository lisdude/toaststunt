#include "options.h"

#ifdef CURL_FOUND

#include <curl/curl.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "curl.h"
#include "functions.h"
#include "utils.h"
#include "list.h"
#include "map.h"
#include "json.h"
#include "log.h"
#include "background.h"
#include "server.h"
#include "storage.h"
#include "streams.h"
#include "version.h"

/*
 * curl(STR url [, MAP options])                          -- extended form
 * curl(STR url [, ANY include_headers [, INT timeout]])  -- legacy form
 *
 * Recognized options:
 *   "method"           -> STR: GET, HEAD, POST, PUT, PATCH, DELETE, OPTIONS
 *   "body"             -> STR: request body (a MOO binary string)
 *   "json"             -> ANY: value serialized as the JSON request body
 *   "headers"          -> MAP of STR -> (STR | LIST of STR) request headers
 *   "timeout"          -> INT: seconds (1 .. $server_options.curl_max_timeout)
 *   "max_size"         -> INT: response byte cap (may only lower the server cap)
 *   "follow_redirects" -> INT/BOOL: follow up to N http(s) redirects
 *   "parse"            -> ANY: truthy parses the response body as JSON
 *   "full"             -> ANY: truthy returns ["status", "headers", "body", "url"]
 *   "include_headers"  -> ANY: truthy prepends raw headers to the body (legacy)
 *   "user_agent"       -> STR: override the User-Agent header
 *
 * Security posture:
 *   - wizard-only, and disabled entirely when outbound networking is off.
 *   - protocol allowlist (http/https/dict); redirects, when enabled at all,
 *     are restricted to http/https.
 *   - TLS peer/host verification is always on and cannot be disabled in-MOO.
 *   - request methods come from a fixed allowlist, which can be narrowed at
 *     compile time via CURL_ALLOWED_METHODS in options.h; header names/values
 *     are validated to reject CR/LF injection.
 *   - response size is capped (curl_max_response_bytes) and timeouts are
 *     clamped (curl_max_timeout) so a hostile or slow endpoint can't exhaust
 *     memory or pin a background thread indefinitely.
 *   - JSON parsing of responses uses the common-subset mode only, so a remote
 *     server can never materialize object references, errors, or other
 *     embedded MOO types inside your database.
 *
 * Threading note: everything that touches the database ($server_options,
 * wizard checks, option validation) happens on the main thread in bf_curl().
 * The background thread receives a fully-resolved curl_request and performs
 * only the transfer plus pure data conversion.
 */

/* The most response-header bytes we will buffer for the "full" return mode. */
#define CURL_RESPONSE_HEADER_LIMIT (256 * 1024)

/* Hard ceiling on curl_max_response_bytes: binary-string escaping can triple
 * the size of the returned string, which must stay well under INT_MAX. */
#define CURL_RESPONSE_BYTES_CEILING (256 * 1024 * 1024)

static CURL *curl_handle = nullptr;

typedef struct CurlMemoryStruct {
    char *result;
    size_t size;
    size_t limit;
    bool overflowed;
} CurlMemoryStruct;

/* All request parameters are validated and resolved on the main server
 * thread in bf_curl() and handed to the worker thread in this struct.
 * The worker thread must not touch the database (including
 * $server_options); see the warning in background.cc. */
typedef struct curl_request {
    struct curl_slist *headers; /* validated request headers */
    char *body;                 /* decoded request body, or nullptr */
    size_t body_len;
    char method[9];             /* longest allowed method is "OPTIONS" */
    char *user_agent;
    long timeout;               /* seconds */
    size_t max_size;            /* response body cap in bytes */
    long max_redirs;            /* < 0: don't follow redirects */
    int json_max_depth;         /* for "parse"; resolved on the main thread */
    bool include_headers;       /* legacy CURLOPT_HEADER behavior */
    bool parse;                 /* parse response body as JSON */
    bool full;                  /* return ["status", "headers", "body", "url"] */
    /* Written by libcurl callbacks on the worker thread: */
    bool shutdown_abort;        /* transfer aborted for server shutdown */
} curl_request;

static size_t
CurlWriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    CurlMemoryStruct *mem = (CurlMemoryStruct *)userp;

    if (realsize > mem->limit || mem->size > mem->limit - realsize) {
        /* Over the cap: abort the transfer rather than buffering more. */
        mem->overflowed = true;
        return 0;
    }

    char *ptr = (char*)realloc(mem->result, mem->size + realsize + 1);
    if (ptr == nullptr) {
        /* out of memory! */
        errlog("not enough memory for curl (realloc returned NULL)\n");
        return 0;
    }

    mem->result = ptr;
    memcpy(&(mem->result[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->result[mem->size] = 0;

    return realsize;
}

/* Progress callback: lets us abort in-flight transfers when the server is
 * shutting down instead of holding up the shutdown for the full timeout. */
static int
CurlXferInfoCallback(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
                     curl_off_t ultotal, curl_off_t ulnow)
{
    curl_request *req = (curl_request *)clientp;

    if (is_shutdown_triggered()) {
        req->shutdown_abort = true;
        return 1;
    }
    return 0;
}

/* ---------- request construction helpers (main thread only) -------------- */

static void
curl_request_free(void *extra_data)
{
    curl_request *req = (curl_request *)extra_data;

    if (req == nullptr)
        return;
    if (req->headers)
        curl_slist_free_all(req->headers);
    free(req->body);
    free(req->user_agent);
    free(req);
}

/* RFC 7230 token characters, the only bytes legal in a header field name. */
static bool
valid_header_name(const char *s)
{
    if (*s == '\0')
        return false;
    for (; *s; s++) {
        unsigned char c = *s;
        if (!(isalnum(c) || strchr("!#$%&'*+-.^_`|~", c)))
            return false;
    }
    return true;
}

/* Header field values: no CR, LF, NUL, or control characters other than
 * horizontal tab.  This is what makes header injection impossible no matter
 * what ends up in the database. */
static bool
valid_header_value(const char *s)
{
    for (; *s; s++) {
        unsigned char c = *s;
        if (c == '\r' || c == '\n' || c == 0x7f || (c < 0x20 && c != '\t'))
            return false;
    }
    return true;
}

struct curl_opt_state {
    curl_request *req;
    Num max_timeout;
    /* deferred cross-field validation */
    bool have_method;
    bool have_body;
    bool have_json;
    bool sent_content_type;
    /* error reporting */
    const char *err;    /* static message; nullptr means no error */
    Var err_value;      /* extra raise() value (owned; freed by caller) */
};

static int
add_request_header(struct curl_opt_state *st, const char *name, const char *value)
{
    if (!valid_header_name(name)) {
        st->err = "Invalid header name";
        st->err_value = str_dup_to_var(name);
        return 1;
    }
    if (!valid_header_value(value)) {
        st->err = "Invalid header value";
        st->err_value = str_dup_to_var(name);
        return 1;
    }

    char *line = nullptr;
    if (*value == '\0')
        asprintf(&line, "%s;", name);       /* libcurl: empty-valued header */
    else
        asprintf(&line, "%s: %s", name, value);
    if (line == nullptr) {
        st->err = "Out of memory building request headers";
        return 1;
    }
    struct curl_slist *headers = curl_slist_append(st->req->headers, line);
    free(line);
    if (headers == nullptr) {
        st->err = "Out of memory building request headers";
        return 1;
    }
    st->req->headers = headers;

    if (!strcasecmp(name, "Content-Type"))
        st->sent_content_type = true;

    return 0;
}

static int
parse_one_header(Var key, Var value, void *data, int first)
{
    struct curl_opt_state *st = (struct curl_opt_state *)data;

    if (key.type != TYPE_STR) {
        st->err = "Header names must be strings";
        return 1;
    }

    if (value.type == TYPE_STR)
        return add_request_header(st, key.v.str, value.v.str);

    if (value.type == TYPE_LIST) {
        for (int i = 1; i <= value.v.list[0].v.num; i++) {
            if (value.v.list[i].type != TYPE_STR) {
                st->err = "Header values must be strings";
                st->err_value = str_dup_to_var(key.v.str);
                return 1;
            }
            if (add_request_header(st, key.v.str, value.v.list[i].v.str))
                return 1;
        }
        return 0;
    }

    st->err = "Header values must be strings or lists of strings";
    st->err_value = str_dup_to_var(key.v.str);
    return 1;
}

static const char *curl_allowed_methods[] = {
    "GET", "HEAD", "POST", "PUT", "PATCH", "DELETE", "OPTIONS", nullptr
};

/* Is `method' (already uppercased and known-supported) present in the
 * compile-time CURL_ALLOWED_METHODS list?  The list is a comma-separated
 * string constant from options.h; surrounding spaces are tolerated and
 * matching is case-insensitive. */
static bool
method_is_enabled(const char *method)
{
    const char *list = CURL_ALLOWED_METHODS;
    size_t mlen = strlen(method);

    while (*list) {
        while (*list == ',' || *list == ' ')
            list++;
        const char *end = list;
        while (*end && *end != ',' && *end != ' ')
            end++;
        if ((size_t)(end - list) == mlen && !strncasecmp(list, method, mlen))
            return true;
        list = end;
    }

    return false;
}

static int
set_body_bytes(struct curl_opt_state *st, const char *bytes, size_t len)
{
    char *copy = (char *)malloc(len > 0 ? len : 1);
    if (copy == nullptr) {
        st->err = "Out of memory copying request body";
        return 1;
    }
    memcpy(copy, bytes, len);
    free(st->req->body);
    st->req->body = copy;
    st->req->body_len = len;
    return 0;
}

static int
parse_one_option(Var key, Var value, void *data, int first)
{
    struct curl_opt_state *st = (struct curl_opt_state *)data;
    curl_request *req = st->req;

    if (key.type != TYPE_STR) {
        st->err = "Option names must be strings";
        return 1;
    }
    const char *name = key.v.str;

    if (!strcasecmp(name, "method")) {
        if (value.type != TYPE_STR) {
            st->err = "\"method\" must be a string";
            return 1;
        }
        size_t len = strlen(value.v.str);
        if (len == 0 || len >= sizeof(req->method)) {
            st->err = "Unsupported request method";
            st->err_value = str_dup_to_var(value.v.str);
            return 1;
        }
        for (size_t i = 0; i <= len; i++)
            req->method[i] = toupper((unsigned char)value.v.str[i]);
        bool ok = false;
        for (int i = 0; curl_allowed_methods[i]; i++)
            if (!strcmp(req->method, curl_allowed_methods[i]))
                ok = true;
        if (!ok) {
            st->err = "Unsupported request method";
            st->err_value = str_dup_to_var(value.v.str);
            return 1;
        }
        st->have_method = true;
    } else if (!strcasecmp(name, "body")) {
        if (value.type != TYPE_STR) {
            st->err = "\"body\" must be a (binary) string";
            return 1;
        }
        int len;
        const char *bytes = binary_to_raw_bytes(value.v.str, &len);
        if (bytes == nullptr) {
            st->err = "\"body\" is an invalid binary string";
            return 1;
        }
        if (set_body_bytes(st, bytes, (size_t)len))
            return 1;
        st->have_body = true;
    } else if (!strcasecmp(name, "json")) {
        char *json = json_generate_string(value, 0, 0);
        if (json == nullptr) {
            st->err = "\"json\" value cannot be represented as JSON";
            return 1;
        }
        int rv = set_body_bytes(st, json, strlen(json));
        free_str(json);
        if (rv)
            return 1;
        st->have_json = true;
    } else if (!strcasecmp(name, "headers")) {
        if (value.type != TYPE_MAP) {
            st->err = "\"headers\" must be a map";
            return 1;
        }
        if (mapforeach(value, parse_one_header, st))
            return 1;
    } else if (!strcasecmp(name, "timeout")) {
        if (value.type != TYPE_INT) {
            st->err = "\"timeout\" must be an integer";
            return 1;
        }
        if (value.v.num < 1 || value.v.num > st->max_timeout) {
            st->err = "\"timeout\" must be between 1 and $server_options.curl_max_timeout seconds";
            st->err_value = Var::new_int(value.v.num);
            return 1;
        }
        req->timeout = (long)value.v.num;
    } else if (!strcasecmp(name, "max_size")) {
        if (value.type != TYPE_INT || value.v.num < 1) {
            st->err = "\"max_size\" must be a positive integer";
            return 1;
        }
        /* Callers may lower the server-wide cap, never raise it. */
        if ((size_t)value.v.num < req->max_size)
            req->max_size = (size_t)value.v.num;
    } else if (!strcasecmp(name, "follow_redirects")) {
        if (value.type == TYPE_INT || value.type == TYPE_BOOL) {
            Num n = (value.type == TYPE_BOOL) ? (value.v.truth ? 1 : 0)
                                              : value.v.num;
            if (n < 0 || n > CURL_MAX_REDIRECTS_LIMIT) {
                st->err = "\"follow_redirects\" is out of range";
                st->err_value = Var::new_int(n);
                return 1;
            }
            if (n == 0)
                req->max_redirs = -1;
            else if (n == 1)
                req->max_redirs = CURL_MAX_REDIRECTS;
            else
                req->max_redirs = (long)n;
        } else {
            st->err = "\"follow_redirects\" must be an integer or boolean";
            return 1;
        }
    } else if (!strcasecmp(name, "parse")) {
        req->parse = is_true(value);
    } else if (!strcasecmp(name, "full")) {
        req->full = is_true(value);
    } else if (!strcasecmp(name, "include_headers")) {
        req->include_headers = is_true(value);
    } else if (!strcasecmp(name, "user_agent")) {
        if (value.type != TYPE_STR || !valid_header_value(value.v.str)) {
            st->err = "\"user_agent\" must be a plain string";
            return 1;
        }
        free(req->user_agent);
        req->user_agent = strdup(value.v.str);
    } else {
        st->err = "Unknown curl option";
        st->err_value = str_dup_to_var(name);
        return 1;
    }

    return 0;
}

/* ---------- the worker (background thread; no database access!) ---------- */

static Var
bytes_to_moo_string(const char *bytes, size_t len)
{
    Stream *s = new_stream(len + 1);
    stream_add_raw_bytes_to_binary(s, bytes, (int)len);
    Var r = str_dup_to_var(reset_stream(s));
    free_stream(s);
    return r;
}

/* Turn the accumulated raw response-header block into a MOO map.  A repeated
 * header name accumulates its values into a list.  On redirects (or interim
 * 1xx responses) only the final response's headers are kept. */
static Var
parse_response_headers(const char *raw, size_t len)
{
    Var map = new_map();
    Stream *s = new_stream(64);
    size_t i = 0;

    while (i < len) {
        size_t start = i;
        while (i < len && raw[i] != '\n')
            i++;
        size_t end = i;
        if (i < len)
            i++;                            /* skip the '\n' */
        if (end > start && raw[end - 1] == '\r')
            end--;
        size_t n = end - start;
        if (n == 0)
            continue;
        if (n >= 5 && !strncasecmp(raw + start, "HTTP/", 5)) {
            /* A new status line: previous block was an interim response
             * or a redirect hop.  Start over. */
            free_var(map);
            map = new_map();
            continue;
        }
        const char *colon = (const char *)memchr(raw + start, ':', n);
        if (colon == nullptr)
            continue;
        size_t name_len = colon - (raw + start);
        const char *val = colon + 1;
        const char *vend = raw + end;
        while (val < vend && (*val == ' ' || *val == '\t'))
            val++;
        while (vend > val && (vend[-1] == ' ' || vend[-1] == '\t'))
            vend--;

        stream_add_raw_bytes_to_binary(s, raw + start, (int)name_len);
        Var key = str_dup_to_var(reset_stream(s));
        stream_add_raw_bytes_to_binary(s, val, (int)(vend - val));
        Var value = str_dup_to_var(reset_stream(s));

        Var existing;
        if (maplookup(map, key, &existing, 0) != nullptr) {
            Var lst;
            if (existing.type == TYPE_LIST)
                lst = listappend(var_ref(existing), value);
            else {
                lst = new_list(0);
                lst = listappend(lst, var_ref(existing));
                lst = listappend(lst, value);
            }
            map = mapinsert(map, key, lst);
        } else {
            map = mapinsert(map, key, value);
        }
    }

    free_stream(s);
    return map;
}

static void curl_thread_callback(Var arglist, Var *ret, void *extra_data)
{
    curl_request *req = (curl_request *)extra_data;
    const char *url = arglist.v.list[1].v.str;
    CURL *curl_handle;
    CURLcode res;
    CurlMemoryStruct chunk, header_chunk;

    curl_handle = curl_easy_init();
    if (curl_handle == nullptr) {
        make_error_map(E_QUOTA, "Could not initialize curl handle", ret);
        return;
    }

    chunk.result = (char*)malloc(1);
    chunk.size = 0;
    chunk.limit = req->max_size;
    chunk.overflowed = false;

    header_chunk.result = (char*)malloc(1);
    header_chunk.size = 0;
    header_chunk.limit = CURL_RESPONSE_HEADER_LIMIT;
    header_chunk.overflowed = false;

    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_PROTOCOLS_STR, "http,https,dict");
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, CurlWriteMemoryCallback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, req->user_agent);
    curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, req->timeout);
    curl_easy_setopt(curl_handle, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl_handle, CURLOPT_ACCEPT_ENCODING, "");
    /* TLS verification stays on, always.  There is intentionally no MOO-side
     * switch to weaken this. */
    curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl_handle, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl_handle, CURLOPT_XFERINFOFUNCTION, CurlXferInfoCallback);
    curl_easy_setopt(curl_handle, CURLOPT_XFERINFODATA, (void *)req);

    if (req->include_headers)
        curl_easy_setopt(curl_handle, CURLOPT_HEADER, 1L);

    if (req->full) {
        curl_easy_setopt(curl_handle, CURLOPT_HEADERFUNCTION, CurlWriteMemoryCallback);
        curl_easy_setopt(curl_handle, CURLOPT_HEADERDATA, (void *)&header_chunk);
    }

    if (req->headers)
        curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, req->headers);

    if (req->body) {
        curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, req->body);
        curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)req->body_len);
    }

    if (!strcmp(req->method, "HEAD"))
        curl_easy_setopt(curl_handle, CURLOPT_NOBODY, 1L);
    else if (!strcmp(req->method, "POST"))
        curl_easy_setopt(curl_handle, CURLOPT_POST, 1L);
    else if (strcmp(req->method, "GET"))
        curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, req->method);

    if (req->max_redirs >= 0) {
        curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl_handle, CURLOPT_MAXREDIRS, req->max_redirs);
        curl_easy_setopt(curl_handle, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
        if (req->body && strcmp(req->method, "POST"))
            curl_easy_setopt(curl_handle, CURLOPT_POSTREDIR, CURL_REDIR_POST_ALL);
    }

    res = curl_easy_perform(curl_handle);

    if (res != CURLE_OK) {
        if (chunk.overflowed || header_chunk.overflowed || res == CURLE_FILESIZE_EXCEEDED)
            make_error_map(E_QUOTA, "Response exceeded the maximum allowed size", ret);
        else if (req->shutdown_abort)
            make_error_map(E_INTRPT, "Transfer aborted: server is shutting down", ret);
        else
            make_error_map(E_INVARG, curl_easy_strerror(res), ret);
    } else {
        Var body_var;
        bool ok = true;

        if (req->parse) {
            if (!json_parse_string(chunk.result, chunk.size, 0,
                                   req->json_max_depth, 1, &body_var)) {
                make_error_map(E_INVARG, "Response body is not valid JSON", ret);
                ok = false;
            }
        } else {
            body_var = bytes_to_moo_string(chunk.result, chunk.size);
        }

        if (ok) {
            if (req->full) {
                long status = 0;
                char *eff_url = nullptr;
                curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &status);
                curl_easy_getinfo(curl_handle, CURLINFO_EFFECTIVE_URL, &eff_url);
                if (eff_url == nullptr)
                    eff_url = (char *)url;

                Var r = new_map();
                r = mapinsert(r, str_dup_to_var("status"), Var::new_int(status));
                r = mapinsert(r, str_dup_to_var("headers"),
                              parse_response_headers(header_chunk.result, header_chunk.size));
                r = mapinsert(r, str_dup_to_var("body"), body_var);
                r = mapinsert(r, str_dup_to_var("url"),
                              str_dup_to_var(eff_url));
                *ret = r;
            } else {
                *ret = body_var;
            }
            oklog("CURL: %s %lu bytes retrieved from: %s\n", req->method,
                  (unsigned long)chunk.size, url);
        }
    }

    curl_easy_cleanup(curl_handle);
    free(chunk.result);
    free(header_chunk.result);
}

/* ---------- the builtin (main thread) ------------------------------------ */

static package
bf_curl(Var arglist, Byte next, void *vdata, Objid progr)
{
    const int nargs = arglist.v.list[0].v.num;

    if (!is_wizard(progr)) {
        free_var(arglist);
        return make_error_pack(E_PERM);
    } else if (!outbound_network_enabled) {
        free_var(arglist);
        return make_raise_pack(E_PERM, "Outbound network connections are disabled.", zero);
    }

    /* Resolve all server options here, on the main thread. */
    Num max_timeout = server_int_option("curl_max_timeout", CURL_MAX_TIMEOUT);
    if (max_timeout < 1)
        max_timeout = 1;
    Num def_timeout = server_int_option("curl_timeout", CURL_TIMEOUT);
    if (def_timeout < 1)
        def_timeout = 1;
    else if (def_timeout > max_timeout)
        def_timeout = max_timeout;
    Num max_bytes = server_int_option("curl_max_response_bytes", CURL_MAX_RESPONSE_BYTES);
    if (max_bytes < 1)
        max_bytes = CURL_MAX_RESPONSE_BYTES;
    else if (max_bytes > CURL_RESPONSE_BYTES_CEILING)
        max_bytes = CURL_RESPONSE_BYTES_CEILING;

    curl_request *req = (curl_request *)calloc(1, sizeof(curl_request));
    if (req == nullptr) {
        free_var(arglist);
        return make_error_pack(E_QUOTA);
    }
    strcpy(req->method, "GET");
    req->timeout = (long)def_timeout;
    req->max_size = (size_t)max_bytes;
    req->max_redirs = -1;
    req->json_max_depth = server_int_option("json_max_parse_depth", JSON_MAX_PARSE_DEPTH);

    const bool options_mode = nargs >= 2 && arglist.v.list[2].type == TYPE_MAP;

    if (options_mode) {
        if (nargs > 2) {
            free_var(arglist);
            curl_request_free(req);
            return make_raise_pack(E_INVARG,
                "The timeout argument cannot be combined with an options map; use the \"timeout\" option",
                zero);
        }

        struct curl_opt_state st;
        memset(&st, 0, sizeof(st));
        st.req = req;
        st.max_timeout = max_timeout;
        st.err = nullptr;
        st.err_value = zero;

        if (mapforeach(arglist.v.list[2], parse_one_option, &st)) {
            free_var(arglist);
            curl_request_free(req);
            return make_raise_pack(E_INVARG, st.err ? st.err : "Invalid curl options", st.err_value);
        }

        /* Cross-field validation. */
        if (st.have_body && st.have_json) {
            free_var(arglist);
            curl_request_free(req);
            return make_raise_pack(E_INVARG, "\"body\" and \"json\" cannot both be given", zero);
        }
        if (req->body != nullptr) {
            if (!st.have_method)
                strcpy(req->method, "POST");
            else if (!strcmp(req->method, "GET") || !strcmp(req->method, "HEAD")
                     || !strcmp(req->method, "OPTIONS")) {
                Var method = str_dup_to_var(req->method);
                free_var(arglist);
                curl_request_free(req);
                return make_raise_pack(E_INVARG, "This request method does not accept a body",
                                       method);
            }
        }
        if (st.have_json && !st.sent_content_type
                && add_request_header(&st, "Content-Type", "application/json")) {
            free_var(arglist);
            curl_request_free(req);
            return make_raise_pack(E_INVARG, st.err, st.err_value);
        }
        if (req->include_headers && (req->full || req->parse)) {
            free_var(arglist);
            curl_request_free(req);
            return make_raise_pack(E_INVARG,
                "\"include_headers\" cannot be combined with \"full\" or \"parse\"", zero);
        }
    } else {
        /* Legacy form: curl(url [, include_headers [, timeout]]) */
        if (nargs > 1)
            req->include_headers = is_true(arglist.v.list[2]);
        if (nargs > 2) {
            Num t = arglist.v.list[3].v.num;
            if (t < 1 || t > max_timeout) {
                free_var(arglist);
                curl_request_free(req);
                return make_raise_pack(E_INVARG,
                    "Timeout must be between 1 and $server_options.curl_max_timeout seconds",
                    Var::new_int(t));
            }
            req->timeout = (long)t;
        }
    }

    /* Check the resolved method against the compile-time allowlist.  This also
     * covers implicit POST requests and legacy calls, which use GET. */
    if (!method_is_enabled(req->method)) {
        Var method = str_dup_to_var(req->method);
        free_var(arglist);
        curl_request_free(req);
        return make_raise_pack(E_PERM,
            "Request method disabled at compile time (see CURL_ALLOWED_METHODS in options.h)",
            method);
    }

    if (req->user_agent == nullptr)
        asprintf(&req->user_agent, "ToastStunt/%s", server_version);
    if (req->user_agent == nullptr) {
        free_var(arglist);
        curl_request_free(req);
        return make_error_pack(E_QUOTA);
    }

    return background_thread(curl_thread_callback, &arglist, (void *)req, curl_request_free);
}

static package
bf_url_encode(Var arglist, Byte next, void *vdata, Objid progr)
{
    if (!outbound_network_enabled) {
        free_var(arglist);
        return make_raise_pack(E_PERM, "Outbound network connections are disabled.", zero);
    }

    Var r;
    const char *url = arglist.v.list[1].v.str;

    char *encoded = curl_easy_escape(curl_handle, url, memo_strlen(url));

    if (encoded == nullptr) {
        free_var(arglist);
        return make_error_pack(E_INVARG);
    }

    r.type = TYPE_STR;
    r.v.str = str_dup(encoded);

    free_var(arglist);
    curl_free(encoded);

    return make_var_pack(r);
}

static package
bf_url_decode(Var arglist, Byte next, void *vdata, Objid progr)
{
    if (!outbound_network_enabled) {
        free_var(arglist);
        return make_raise_pack(E_PERM, "Outbound network connections are disabled.", zero);
    }

    Var r;
    const char *url = arglist.v.list[1].v.str;

    char *decoded = curl_easy_unescape(curl_handle, url, memo_strlen(url), nullptr);

    if (decoded == nullptr) {
        free_var(arglist);
        return make_error_pack(E_INVARG);
    }

    r.type = TYPE_STR;
    r.v.str = str_dup(decoded);

    free_var(arglist);
    curl_free(decoded);

    return make_var_pack(r);
}

void curl_shutdown(void)
{
    if (outbound_network_enabled)
    {
        curl_global_cleanup();

        if (curl_handle != nullptr)
            curl_easy_cleanup(curl_handle);
    }
}

void
register_curl(void)
{
    if (outbound_network_enabled)
    {
        oklog("REGISTER_CURL: Using libcurl version %s\n", curl_version());
        curl_global_init(CURL_GLOBAL_ALL);
        curl_handle = curl_easy_init();
    }

    register_function("curl", 1, 3, bf_curl, TYPE_STR, TYPE_ANY, TYPE_INT);
    register_function("url_encode", 1, 1, bf_url_encode, TYPE_STR);
    register_function("url_decode", 1, 1, bf_url_decode, TYPE_STR);
}

#else /* CURL_FOUND */
void register_curl(void) { }
void curl_shutdown(void) { }
#endif /* CURL_FOUND */
