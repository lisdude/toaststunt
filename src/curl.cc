#include "options.h"

#ifdef CURL_FOUND

#include <curl/curl.h>

#include "curl.h"
#include "functions.h"
#include "utils.h"
#include "log.h"
#include "background.h"

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

    mem->result= ptr;
    memcpy(&(mem->result[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->result[mem->size] = 0;

    return realsize;
}

void curl_thread_callback(Var arglist, Var *ret)
{
    int nargs = arglist.v.list[0].v.num;
    CURL *curl_handle;
    CURLcode res;
    CurlMemoryStruct chunk;

    chunk.result = (char*)malloc(1);
    chunk.size = 0;

    curl_handle = curl_easy_init();
    curl_easy_setopt(curl_handle, CURLOPT_URL, arglist.v.list[1].v.str);
    curl_easy_setopt(curl_handle, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, CurlWriteMemoryCallback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");

    if (nargs > 1 && is_true(arglist.v.list[2]))
        curl_easy_setopt(curl_handle, CURLOPT_HEADER, 1L);

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

void curl_shutdown(void)
{
    curl_global_cleanup();
}

    void
register_curl(void)
{
    oklog("REGISTER_CURL: Using libcurl version %s\n", curl_version());
    curl_global_init(CURL_GLOBAL_ALL);
    register_function("curl", 1, 2, bf_curl, TYPE_STR, TYPE_ANY);
}

#else /* CURL_FOUND */
void register_curl(void) { }
void curl_shutdown(void) { }
#endif /* CURL_FOUND */
