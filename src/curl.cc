#include "options.h"

#ifdef CURL_FOUND

#include <curl/curl.h>

#include "curl.h"
#include "functions.h"
#include "utils.h"
#include "log.h"
#include "background.h"

static CURL *curl_handle = nullptr;

    static std::map<std::string, int>
curl_option  = {{"post", 1}};



typedef struct CurlMemoryStruct {
    char *result;
    size_t size;
} CurlMemoryStruct;

static size_t
CurlWriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    CurlMemoryStruct *mem = (CurlMemoryStruct *)userp;

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

/* Initialize the curl handle with the indicated option */
static void
parse_curl_option(CURL *handle, Var arglist)
{

    const char *input = arglist.v.list[2].v.str;

    switch(curl_option[input])
    {
        case 1:
        {
            const char *data = arglist.v.list[3].v.str;
            curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE, (long) strlen(data));
            curl_easy_setopt(handle, CURLOPT_POSTFIELDS, data);
            break;
        }
        default:
            curl_easy_setopt(handle, CURLOPT_HTTPGET, 1L);

    }

}

static void curl_thread_callback(Var arglist, Var *ret)
{
    int nargs = arglist.v.list[0].v.num;
    CURLcode res;
    CurlMemoryStruct chunk;

    chunk.result = (char*)malloc(1);
    chunk.size = 0;

    CURL *curl_handle = curl_easy_init();
    curl_easy_setopt(curl_handle, CURLOPT_URL, arglist.v.list[1].v.str);
    curl_easy_setopt(curl_handle, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, CurlWriteMemoryCallback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");

    if (nargs >= 3) 
    {

        /* Set appropriate option */
        parse_curl_option(curl_handle, arglist);

        if (nargs == 4 && is_true(arglist.v.list[4]))
            curl_easy_setopt(curl_handle, CURLOPT_HEADER, 1L);
   }

    res = curl_easy_perform(curl_handle);

    if (res != CURLE_OK)
        make_error_map(E_INVARG, curl_easy_strerror(res), ret);
    else {
        *ret = str_dup_to_var(raw_bytes_to_binary(chunk.result, chunk.size));
        oklog("CURL: %lu bytes retrieved from: %s\n", (unsigned long)chunk.size, arglist.v.list[1].v.str);
    }

    curl_easy_cleanup(curl_handle);
    free(chunk.result);
}

static package
bf_curl(Var arglist, Byte next, void *vdata, Objid progr)
{
    if (!is_wizard(progr))
        return make_error_pack(E_PERM);

    char *human_string = nullptr;
    asprintf(&human_string, "curl %s", arglist.v.list[1].v.str);

    return background_thread(curl_thread_callback, &arglist, human_string);
}

static package
bf_url_encode(Var arglist, Byte next, void *vdata, Objid progr)
{
    Var r;
    const char *url = arglist.v.list[1].v.str;

    free_var(arglist);

    char *encoded = curl_easy_escape(curl_handle, url, memo_strlen(url));

    if (encoded == nullptr) {
        return make_error_pack(E_INVARG);
    }

    r.type = TYPE_STR;
    r.v.str = str_dup(encoded);

    curl_free(encoded);

    return make_var_pack(r);
}

static package
bf_url_decode(Var arglist, Byte next, void *vdata, Objid progr)
{
    Var r;
    const char *url = arglist.v.list[1].v.str;

    free_var(arglist);

    char *decoded = curl_easy_unescape(curl_handle, url, memo_strlen(url), nullptr);

    if (decoded == nullptr) {
        return make_error_pack(E_INVARG);
    }

    r.type = TYPE_STR;
    r.v.str = str_dup(decoded);

    curl_free(decoded);

    return make_var_pack(r);
}

void curl_shutdown(void)
{
    curl_global_cleanup();
    
    if (curl_handle != nullptr)
        curl_easy_cleanup(curl_handle);
}

void
register_curl(void)
{
    oklog("REGISTER_CURL: Using libcurl version %s\n", curl_version());
    curl_global_init(CURL_GLOBAL_ALL);
    curl_handle = curl_easy_init();
 
   register_function("curl", 1, 4, bf_curl, TYPE_STR, TYPE_STR, TYPE_STR, TYPE_ANY);
    register_function("url_encode", 1, 1, bf_url_encode, TYPE_STR);
    register_function("url_decode", 1, 1, bf_url_decode, TYPE_STR);
}

#else /* CURL_FOUND */
void register_curl(void) { }
void curl_shutdown(void) { }
#endif /* CURL_FOUND */
