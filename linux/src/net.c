/* net.c — requêtes HTTP (libcurl, appelées hors du thread GTK) et travaux en arrière-plan. */
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"

void net_init(void)
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

const char *net_user_agent(void)
{
    return "HomeRunPlayer-Linux/" APP_VERSION;
}

typedef struct { char *p; size_t n, cap; } Buf;

static size_t on_data(char *d, size_t sz, size_t nm, void *ud)
{
    Buf *b = ud;
    size_t n = sz * nm;
    if (b->n + n + 1 > b->cap) {
        size_t nc = MAX(b->cap * 2, b->n + n + 1 + 4096);
        if (nc > 64u * 1024 * 1024) return 0;   /* réponse anormalement grosse */
        char *np = g_try_realloc(b->p, nc);
        if (!np) return 0;
        b->p = np;
        b->cap = nc;
    }
    memcpy(b->p + b->n, d, n);
    b->n += n;
    b->p[b->n] = 0;
    return n;
}

static char *request(const char *url, gboolean post, int timeout_ms, char **err)
{
    if (err) *err = NULL;
    if (!g_str_has_prefix(url, "http://") && !g_str_has_prefix(url, "https://")) {
        /* fichier local (playlist enregistrée sur le disque) */
        char *txt = NULL;
        GError *e = NULL;
        const char *path = g_str_has_prefix(url, "file://") ? url + 7 : url;
        if (!post && g_file_get_contents(path, &txt, NULL, &e)) return txt;
        if (err) *err = g_strdup(e ? e->message : url);
        if (e) g_error_free(e);
        return NULL;
    }
    CURL *c = curl_easy_init();
    if (!c) { if (err) *err = g_strdup("curl"); return NULL; }
    Buf b = { 0 };
    char eb[CURL_ERROR_SIZE] = "";
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_USERAGENT, net_user_agent());
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, (long)timeout_ms);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, (long)MIN(timeout_ms, 8000));
    curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, on_data);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &b);
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, eb);
    if (post) {
        curl_easy_setopt(c, CURLOPT_POST, 1L);
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, "");
        curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, 0L);
    }
    CURLcode r = curl_easy_perform(c);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(c);
    if (r != CURLE_OK) {
        if (err) *err = g_strdup(eb[0] ? eb : curl_easy_strerror(r));
        g_free(b.p);
        return NULL;
    }
    if (code < 200 || code > 299) {
        if (err) *err = g_strdup_printf("HTTP %ld", code);
        g_free(b.p);
        return NULL;
    }
    return b.p ? b.p : g_strdup("");
}

char *net_get(const char *url, int timeout_ms, char **err)
{
    return request(url, FALSE, timeout_ms, err);
}

gboolean net_post(const char *url, int timeout_ms, char **err)
{
    char *r = request(url, TRUE, timeout_ms, err);
    gboolean ok = r != NULL;
    g_free(r);
    return ok;
}

/* FetchFn de core.c : mémoire malloc/free (core.c n'utilise pas GLib). */
char *net_fetch(const char *url, int timeout_ms, char **err, void *ud)
{
    (void)ud;
    char *e = NULL, *r = request(url, FALSE, timeout_ms, &e);
    char *out = r ? strdup(r) : NULL;
    if (!r && err) *err = strdup(e ? e : "?");
    g_free(r);
    g_free(e);
    return out;
}

/* ---- travaux en arrière-plan -------------------------------------------- */
typedef struct { void (*work)(gpointer); void (*done)(gpointer); gpointer data; } Job;

static gboolean job_done(gpointer p)
{
    Job *j = p;
    if (j->done) j->done(j->data);
    g_free(j);
    return G_SOURCE_REMOVE;
}

static gpointer job_thread(gpointer p)
{
    Job *j = p;
    j->work(j->data);
    g_idle_add(job_done, j);
    return NULL;
}

void bg_run(void (*work)(gpointer), void (*done)(gpointer), gpointer data)
{
    Job *j = g_new0(Job, 1);
    j->work = work;
    j->done = done;
    j->data = data;
    GThread *t = g_thread_new("bg", job_thread, j);
    g_thread_unref(t);
}
