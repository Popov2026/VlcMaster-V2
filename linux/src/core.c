/* core.c — voir core.h. */
#include "core.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static char *xstrdup(const char *s)
{
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *d = malloc(n);
    if (d) memcpy(d, s, n);
    return d;
}

static char *xstrndup(const char *s, size_t n)
{
    char *d = malloc(n + 1);
    if (!d) return NULL;
    memcpy(d, s, n);
    d[n] = 0;
    return d;
}

/* Recherche sans tenir compte de la casse (ASCII). */
static const char *ci_find(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    for (; *hay; hay++) {
        size_t i = 0;
        while (i < n && hay[i] && tolower((unsigned char)hay[i]) == tolower((unsigned char)needle[i])) i++;
        if (i == n) return hay;
    }
    return NULL;
}

static void trim_inplace(char *s)
{
    size_t n = strlen(s), a = 0;
    while (a < n && isspace((unsigned char)s[a])) a++;
    while (n > a && isspace((unsigned char)s[n - 1])) n--;
    memmove(s, s + a, n - a);
    s[n - a] = 0;
}

/* ======================================================================= */
/* Playlist                                                                */
/* ======================================================================= */
int m3u_attr(const char *line, const char *key, char *out, size_t cap)
{
    size_t kl = strlen(key);
    for (const char *p = line; (p = ci_find(p, key)) != NULL; p++) {
        if (p > line && !isspace((unsigned char)p[-1])) continue;
        if (p[kl] != '=' || p[kl + 1] != '"') continue;
        const char *s = p + kl + 2, *e = strchr(s, '"');
        if (!e) return 0;
        size_t n = (size_t)(e - s);
        if (n >= cap) n = cap - 1;
        memcpy(out, s, n);
        out[n] = 0;
        return 1;
    }
    return 0;
}

int playlist_parse(const char *text, ChannelList *out)
{
    out->v = NULL;
    out->n = 0;
    if (!text) return 0;
    int cap = 0;
    char num[64] = "", name[512] = "";
    const char *p = text;
    while (*p) {
        const char *e = p;
        while (*e && *e != '\n') e++;
        size_t len = (size_t)(e - p);
        char *line = xstrndup(p, len);
        p = *e ? e + 1 : e;
        if (!line) break;
        if ((unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF)
            memmove(line, line + 3, strlen(line + 3) + 1);
        trim_inplace(line);
        if (!line[0]) { free(line); continue; }
        if (!strncmp(line, "#EXTINF", 7) || !strncmp(line, "#extinf", 7)) {
            if (!m3u_attr(line, "channel-number", num, sizeof num) && !m3u_attr(line, "tvg-chno", num, sizeof num))
                num[0] = 0;
            const char *c = strrchr(line, ',');
            name[0] = 0;
            if (c) { snprintf(name, sizeof name, "%s", c + 1); trim_inplace(name); }
            if (!name[0]) m3u_attr(line, "tvg-name", name, sizeof name);
        } else if (line[0] != '#') {
            if (!num[0]) {
                /* « …/auto/v5.1 » → 5.1 */
                const char *v = line, *best = NULL;
                while ((v = strstr(v, "/v")) != NULL) { if (isdigit((unsigned char)v[2])) best = v + 2; v += 2; }
                if (best) {
                    size_t n = 0;
                    while (n + 1 < sizeof num && (isdigit((unsigned char)best[n]) || (best[n] == '.' && isdigit((unsigned char)best[n + 1])))) {
                        num[n] = best[n];
                        n++;
                    }
                    num[n] = 0;
                }
            }
            if (!name[0]) {
                const char *s = strrchr(line, '/');
                snprintf(name, sizeof name, "%s", s ? s + 1 : line);
            }
            /* « 1 TF1 » : numéro en tête du nom (format HDHomeRun) */
            size_t nl = strlen(num);
            if (nl && !strncmp(name, num, nl) && name[nl] == ' ') {
                memmove(name, name + nl + 1, strlen(name + nl + 1) + 1);
                trim_inplace(name);
            }
            if (out->n == cap) {
                int nc = cap ? cap * 2 : 64;
                Channel *nv = realloc(out->v, sizeof *nv * (size_t)nc);
                if (!nv) { free(line); break; }
                out->v = nv;
                cap = nc;
            }
            Channel *ch = &out->v[out->n++];
            ch->num = xstrdup(num);
            ch->name = xstrdup(name);
            ch->url = xstrdup(line);
            num[0] = name[0] = 0;
        }
        free(line);
    }
    return out->n;
}

void playlist_free(ChannelList *l)
{
    for (int i = 0; i < l->n; i++) {
        free(l->v[i].num);
        free(l->v[i].name);
        free(l->v[i].url);
    }
    free(l->v);
    l->v = NULL;
    l->n = 0;
}

int playlist_find_url(const ChannelList *l, const char *url)
{
    if (!url || !*url) return -1;
    for (int i = 0; i < l->n; i++)
        if (!strcmp(l->v[i].url, url)) return i;
    return -1;
}

int playlist_find_num(const ChannelList *l, const char *num)
{
    if (!num || !*num) return -1;
    for (int i = 0; i < l->n; i++)
        if (!strcmp(l->v[i].num, num)) return i;
    return -1;
}

/* ======================================================================= */
/* Guide TV                                                                */
/* ======================================================================= */
static GuideCh *guide_get_or_add(Guide *g, const char *num, const char *name)
{
    for (int i = 0; i < g->n; i++)
        if (!strcmp(g->ch[i].num, num)) return &g->ch[i];
    if (g->n == g->cap) {
        int nc = g->cap ? g->cap * 2 : 64;
        GuideCh *nv = realloc(g->ch, sizeof *nv * (size_t)nc);
        if (!nv) return NULL;
        g->ch = nv;
        g->cap = nc;
    }
    GuideCh *c = &g->ch[g->n++];
    memset(c, 0, sizeof *c);
    c->num = xstrdup(num);
    c->name = xstrdup(name ? name : "");
    return c;
}

long long guide_merge(Guide *g, const JVal *arr)
{
    long long max_e = 0;
    if (!arr || arr->t != J_ARR) return 0;
    for (int i = 0; i < arr->cnt; i++) {
        const JVal *c = &arr->v[i];
        const char *num = json_str(c, "GuideNumber");
        const JVal *progs = json_get(c, "Guide");
        if (!num || !*num || !progs || progs->t != J_ARR) continue;
        GuideCh *gc = guide_get_or_add(g, num, json_str(c, "GuideName"));
        if (!gc) continue;
        for (int j = 0; j < progs->cnt; j++) {
            const JVal *p = &progs->v[j];
            long long st = (long long)json_num(p, "StartTime", 0), en = (long long)json_num(p, "EndTime", 0);
            if (en <= st) continue;
            if (gc->n && st < gc->p[gc->n - 1].end) continue;   /* chevauchement entre pages */
            if (gc->n == gc->cap) {
                int nc = gc->cap ? gc->cap * 2 : 32;
                Prog *nv = realloc(gc->p, sizeof *nv * (size_t)nc);
                if (!nv) break;
                gc->p = nv;
                gc->cap = nc;
            }
            const char *et = json_str(p, "EpisodeTitle"), *enr = json_str(p, "EpisodeNumber");
            if (et && !*et) et = NULL;
            if (enr && !*enr) enr = NULL;
            char *ep = NULL;
            if (et && enr) {
                size_t n = strlen(et) + strlen(enr) + 4;
                if ((ep = malloc(n))) snprintf(ep, n, "%s (%s)", et, enr);
            } else ep = xstrdup(et ? et : enr);
            const char *syn = json_str(p, "Synopsis");
            Prog *pr = &gc->p[gc->n++];
            pr->start = st;
            pr->end = en;
            pr->title = xstrdup(json_str(p, "Title") ? json_str(p, "Title") : "");
            pr->episode = ep;
            pr->synopsis = syn && *syn ? xstrdup(syn) : NULL;
            if (en > max_e) max_e = en;
        }
    }
    if (max_e > g->max_end) g->max_end = max_e;
    return max_e;
}

void guide_free(Guide *g)
{
    for (int i = 0; i < g->n; i++) {
        GuideCh *c = &g->ch[i];
        for (int j = 0; j < c->n; j++) {
            free(c->p[j].title);
            free(c->p[j].episode);
            free(c->p[j].synopsis);
        }
        free(c->p);
        free(c->num);
        free(c->name);
    }
    free(g->ch);
    memset(g, 0, sizeof *g);
}

int guide_load(const char *base, FetchFn fetch, void *ud, long long now, Guide *out, char **err)
{
    memset(out, 0, sizeof *out);
    *err = NULL;
    char url[1024];
    snprintf(url, sizeof url, "%s/discover.json", base);
    char *e = NULL, *txt = fetch(url, 8000, &e, ud);
    JVal *d = txt ? json_parse(txt) : NULL;
    free(txt);
    const char *auth = json_str(d, "DeviceAuth");
    if (!auth || !*auth) {
        json_free(d);
        free(e);
        *err = xstrdup(base);
        return GUIDE_ERR_NO_TUNER;
    }
    free(e);
    char *enc = url_encode(auth);
    json_free(d);
    if (!enc) return GUIDE_ERR_SERVICE;
    long long start = 0, end = 0;
    char *first_err = NULL;
    for (int i = 0; i < 8; i++) {
        int n = snprintf(url, sizeof url, "https://api.hdhomerun.com/api/guide.php?DeviceAuth=%s", enc);
        if (start > 0) snprintf(url + n, sizeof url - (size_t)n, "&Start=%lld", start);
        e = NULL;
        txt = fetch(url, 15000, &e, ud);
        JVal *arr = txt ? json_parse(txt) : NULL;
        free(txt);
        if (!arr || arr->t != J_ARR) {
            if (out->n == 0 && !first_err) first_err = e ? e : xstrdup("JSON");
            else free(e);
            json_free(arr);
            break;
        }
        free(e);
        long long before = end, m = guide_merge(out, arr);
        json_free(arr);
        if (m > end) end = m;
        if (end <= before) break;
        start = end;
        if (end >= now + 24 * 3600) break;
    }
    free(enc);
    out->max_end = end;
    if (out->n == 0) {
        if (first_err) { *err = first_err; return GUIDE_ERR_SERVICE; }
        return GUIDE_ERR_EMPTY;
    }
    free(first_err);
    return GUIDE_OK;
}

const GuideCh *guide_find(const Guide *g, const char *num)
{
    if (!g || !num) return NULL;
    for (int i = 0; i < g->n; i++)
        if (!strcmp(g->ch[i].num, num)) return &g->ch[i];
    return NULL;
}

const Prog *guide_at(const GuideCh *c, long long t)
{
    if (!c) return NULL;
    for (int i = 0; i < c->n; i++)
        if (t >= c->p[i].start && t < c->p[i].end) return &c->p[i];
    return NULL;
}

const Prog *guide_next(const GuideCh *c, long long t)
{
    if (!c) return NULL;
    for (int i = 0; i < c->n; i++)
        if (c->p[i].start >= t) return &c->p[i];
    return NULL;
}

/* ======================================================================= */
/* Pistes                                                                  */
/* ======================================================================= */
int track_bracket(const char *raw, char *out, size_t cap)
{
    const char *a = strrchr(raw, '[');
    const char *b = a ? strchr(a, ']') : NULL;
    if (!a || !b || b <= a + 1) return 0;
    size_t n = (size_t)(b - a - 1);
    if (n >= cap) n = cap - 1;
    memcpy(out, a + 1, n);
    out[n] = 0;
    trim_inplace(out);
    return out[0] != 0;
}

int track_is_ad(const char *raw)
{
    return ci_find(raw, "audio description") || ci_find(raw, "audiodescription");
}

int track_is_hoh(const char *raw)
{
    return ci_find(raw, "hard of hearing") || ci_find(raw, "hearing impaired") || ci_find(raw, "malentendants");
}

void track_key(const char *raw, char *out, size_t cap)
{
    char b[128];
    if (!track_bracket(raw, b, sizeof b)) snprintf(b, sizeof b, "%s", raw);
    for (char *p = b; *p; p++) *p = (char)tolower((unsigned char)*p);
    snprintf(out, cap, "%s%s%s", b, track_is_ad(raw) ? "|ad" : "", track_is_hoh(raw) ? "|hoh" : "");
}

/* ======================================================================= */
/* Divers                                                                  */
/* ======================================================================= */
int version_cmp(const char *a, const char *b)
{
    for (int i = 0; i < 4; i++) {
        while (*a && !isdigit((unsigned char)*a)) a++;
        while (*b && !isdigit((unsigned char)*b)) b++;
        long x = 0, y = 0;
        if (*a) x = strtol(a, (char **)&a, 10);
        if (*b) y = strtol(b, (char **)&b, 10);
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

int tuner_base(const char *u, char *out, size_t cap)
{
    if (!u) return 0;
    while (isspace((unsigned char)*u)) u++;
    if (ci_find(u, "http") != u) return 0;
    const char *s = strstr(u, "://");
    if (!s) return 0;
    s += 3;
    size_t n = strcspn(s, "/?#");
    size_t total = (size_t)(s - u) + n;
    if (!n || total + 1 > cap) return 0;
    memcpy(out, u, total);
    out[total] = 0;
    return 1;
}

void sanitize_name(const char *in, char *out, size_t cap, size_t maxlen)
{
    size_t o = 0;
    int sp = 1;   /* supprime les espaces en tête */
    if (maxlen + 1 > cap) maxlen = cap - 1;
    for (const unsigned char *p = (const unsigned char *)(in ? in : ""); *p; p++) {
        unsigned char c = *p;
        if (c < 0x20 || c == 0x7F || strchr("\\/:*?\"<>|", c)) c = ' ';
        if (c == ' ') {
            if (sp) continue;
            sp = 1;
        } else sp = 0;
        /* ne coupe pas au milieu d'un caractère UTF-8 */
        size_t clen = 1;
        if (c >= 0xF0) clen = 4; else if (c >= 0xE0) clen = 3; else if (c >= 0xC0) clen = 2;
        if (o + clen > maxlen) break;
        out[o++] = (char)c;
        for (size_t k = 1; k < clen && p[1]; k++) out[o++] = (char)*++p;
    }
    while (o && out[o - 1] == ' ') o--;
    out[o] = 0;
}

void rec_basename(time_t t, const char *channel, const char *title, char *out, size_t cap)
{
    struct tm tm = *localtime(&t);
    char ch[256], ti[256];
    sanitize_name(channel, ch, sizeof ch, 60);
    sanitize_name(title, ti, sizeof ti, 80);
    snprintf(out, cap, "%04d-%02d-%02d %02dh%02d - %s%s%s", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, ch, ti[0] ? " - " : "", ti);
}

char *rec_unique_path(const char *dir, const char *base)
{
    size_t n = strlen(dir) + strlen(base) + 16;
    char *p = malloc(n);
    if (!p) return NULL;
    struct stat st;
    snprintf(p, n, "%s/%s.ts", dir, base);
    for (int k = 2; stat(p, &st) == 0 && k < 100; k++) snprintf(p, n, "%s/%s (%d).ts", dir, base, k);
    return p;
}

void fmt_dur(long long sec, char *out, size_t cap)
{
    if (sec < 0) sec = 0;
    long long h = sec / 3600, m = (sec / 60) % 60, s = sec % 60;
    if (h > 0 && m == 0) snprintf(out, cap, "%lld h", h);
    else if (h > 0) snprintf(out, cap, "%lld h %02lld", h, m);
    else if (s == 0) snprintf(out, cap, "%lld min", m);
    else snprintf(out, cap, "%lld min %02lld s", m, s);
}

char *url_encode(const char *s)
{
    static const char hex[] = "0123456789ABCDEF";
    char *o = malloc(strlen(s) * 3 + 1), *q = o;
    if (!o) return NULL;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (isalnum(*p) || strchr("-_.~", *p)) *q++ = (char)*p;
        else { *q++ = '%'; *q++ = hex[*p >> 4]; *q++ = hex[*p & 15]; }
    }
    *q = 0;
    return o;
}
