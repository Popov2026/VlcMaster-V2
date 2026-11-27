/* json.c — voir json.h. */
#include "json.h"

#include <stdlib.h>
#include <string.h>

typedef struct { const char *p; int depth; } P;

static void ws(P *p)
{
    while (*p->p == ' ' || *p->p == '\t' || *p->p == '\n' || *p->p == '\r') p->p++;
}

static int hex4(const char *s, unsigned *out)
{
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
        char c = s[i];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
        else return 0;
    }
    *out = v;
    return 1;
}

static size_t utf8_put(char *o, unsigned cp)
{
    if (cp < 0x80) { o[0] = (char)cp; return 1; }
    if (cp < 0x800) { o[0] = (char)(0xC0 | (cp >> 6)); o[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    if (cp < 0x10000) {
        o[0] = (char)(0xE0 | (cp >> 12)); o[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        o[2] = (char)(0x80 | (cp & 0x3F)); return 3;
    }
    o[0] = (char)(0xF0 | (cp >> 18)); o[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    o[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); o[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

/* Chaîne entre guillemets (p->p sur le guillemet ouvrant). */
static char *str(P *p)
{
    const char *s = p->p + 1, *e = s;
    while (*e && *e != '"') { if (*e == '\\' && e[1]) e++; e++; }
    if (*e != '"') return NULL;
    char *out = malloc((size_t)(e - s) + 1), *o = out;   /* le décodage ne rallonge jamais */
    if (!out) return NULL;
    while (s < e) {
        if (*s != '\\') { *o++ = *s++; continue; }
        s++;
        switch (*s) {
        case 'n': *o++ = '\n'; s++; break;
        case 't': *o++ = '\t'; s++; break;
        case 'r': *o++ = '\r'; s++; break;
        case 'b': *o++ = '\b'; s++; break;
        case 'f': *o++ = '\f'; s++; break;
        case 'u': {
            unsigned cp, lo;
            if (s + 5 > e || !hex4(s + 1, &cp)) { free(out); return NULL; }
            s += 5;
            if (cp >= 0xD800 && cp < 0xDC00 && s + 6 <= e && s[0] == '\\' && s[1] == 'u' &&
                hex4(s + 2, &lo) && lo >= 0xDC00 && lo < 0xE000) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                s += 6;
            }
            if (cp == 0) cp = ' ';
            o += utf8_put(o, cp);
            break;
        }
        default: *o++ = *s++; break;   /* \" \\ \/ */
        }
    }
    *o = 0;
    p->p = e + 1;
    return out;
}

static int val(P *p, JVal *v);

static int push(JVal *v, int *cap)
{
    if (v->cnt < *cap) return 1;
    int nc = *cap ? *cap * 2 : 8;
    JVal *nv = realloc(v->v, sizeof *nv * (size_t)nc);
    if (!nv) return 0;
    v->v = nv;
    if (v->t == J_OBJ) {
        char **nk = realloc(v->k, sizeof *nk * (size_t)nc);
        if (!nk) return 0;
        v->k = nk;
    }
    *cap = nc;
    return 1;
}

static int val(P *p, JVal *v)
{
    memset(v, 0, sizeof *v);
    ws(p);
    char c = *p->p;
    if (c == '{' || c == '[') {
        if (++p->depth > 64) return 0;
        int cap = 0, obj = c == '{';
        v->t = obj ? J_OBJ : J_ARR;
        p->p++;
        ws(p);
        if (*p->p == (obj ? '}' : ']')) { p->p++; p->depth--; return 1; }
        for (;;) {
            char *key = NULL;
            ws(p);
            if (obj) {
                if (*p->p != '"' || !(key = str(p))) return 0;
                ws(p);
                if (*p->p != ':') { free(key); return 0; }
                p->p++;
            }
            if (!push(v, &cap)) { free(key); return 0; }
            if (obj) v->k[v->cnt] = key;
            if (!val(p, &v->v[v->cnt])) { v->cnt++; return 0; }   /* compté pour être libéré */
            v->cnt++;
            ws(p);
            if (*p->p == ',') { p->p++; continue; }
            if (*p->p == (obj ? '}' : ']')) { p->p++; p->depth--; return 1; }
            return 0;
        }
    }
    if (c == '"') { v->t = J_STR; return (v->s = str(p)) != NULL; }
    if (!strncmp(p->p, "true", 4)) { v->t = J_BOOL; v->n = 1; p->p += 4; return 1; }
    if (!strncmp(p->p, "false", 5)) { v->t = J_BOOL; p->p += 5; return 1; }
    if (!strncmp(p->p, "null", 4)) { v->t = J_NULL; p->p += 4; return 1; }
    if (c == '-' || (c >= '0' && c <= '9')) {
        char *e;
        v->t = J_NUM;
        v->n = strtod(p->p, &e);
        if (e == p->p) return 0;
        p->p = e;
        return 1;
    }
    return 0;
}

static void clear(JVal *v)
{
    if (!v) return;
    free(v->s);
    for (int i = 0; i < v->cnt; i++) {
        clear(&v->v[i]);
        if (v->k) free(v->k[i]);
    }
    free(v->v);
    free(v->k);
}

JVal *json_parse(const char *text)
{
    if (!text) return NULL;
    P p = { text, 0 };
    if ((unsigned char)p.p[0] == 0xEF && (unsigned char)p.p[1] == 0xBB && (unsigned char)p.p[2] == 0xBF) p.p += 3;
    JVal *v = malloc(sizeof *v);
    if (!v) return NULL;
    if (!val(&p, v)) { clear(v); free(v); return NULL; }
    ws(&p);
    if (*p.p) { clear(v); free(v); return NULL; }
    return v;
}

void json_free(JVal *v)
{
    if (!v) return;
    clear(v);
    free(v);
}

const JVal *json_get(const JVal *o, const char *key)
{
    if (!o || o->t != J_OBJ) return NULL;
    for (int i = 0; i < o->cnt; i++)
        if (!strcmp(o->k[i], key)) return &o->v[i];
    return NULL;
}

const JVal *json_at(const JVal *a, int i)
{
    if (!a || a->t != J_ARR || i < 0 || i >= a->cnt) return NULL;
    return &a->v[i];
}

const char *json_str(const JVal *o, const char *key)
{
    const JVal *v = json_get(o, key);
    return v && v->t == J_STR ? v->s : NULL;
}

double json_num(const JVal *o, const char *key, double def)
{
    const JVal *v = json_get(o, key);
    if (!v) return def;
    if (v->t == J_NUM || v->t == J_BOOL) return v->n;
    if (v->t == J_STR && v->s && *v->s) {
        char *e;
        double d = strtod(v->s, &e);
        if (!*e) return d;
    }
    return def;
}
