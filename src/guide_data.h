/*
 * guide_data.h — Guide TV : données (inclus dans vlcmaster.c)
 *
 * Source : le service guide de SiliconDust (api.hdhomerun.com), interrogé
 * avec le DeviceAuth publié par le tuner (http://<tuner>/discover.json).
 * Chaque requête renvoie ~10 h de programmes ; on enchaîne jusqu'à 24 h.
 */

/* ------------------------------------------------------------------ */
/* Mini-lecteur JSON (arbre en mémoire)                                */
/* ------------------------------------------------------------------ */
enum { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ };
typedef struct JV { int t; double n; char *s; int cnt, cap; struct JV *v; char **k; } JV;

static void JFree(JV *j)
{
    if (!j) return;
    free(j->s);
    for (int i = 0; i < j->cnt; i++) { JFree(&j->v[i]); if (j->k) free(j->k[i]); }
    free(j->v); free(j->k);
    memset(j, 0, sizeof *j);
}

static const char *JWs(const char *p) { while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++; return p; }

static void JPutUtf8(char **o, unsigned c)
{
    char *q = *o;
    if (c < 0x80) *q++ = (char)c;
    else if (c < 0x800) { *q++ = (char)(0xC0 | (c >> 6)); *q++ = (char)(0x80 | (c & 63)); }
    else if (c < 0x10000) { *q++ = (char)(0xE0 | (c >> 12)); *q++ = (char)(0x80 | ((c >> 6) & 63)); *q++ = (char)(0x80 | (c & 63)); }
    else { *q++ = (char)(0xF0 | (c >> 18)); *q++ = (char)(0x80 | ((c >> 12) & 63)); *q++ = (char)(0x80 | ((c >> 6) & 63)); *q++ = (char)(0x80 | (c & 63)); }
    *o = q;
}

static unsigned JHex4(const char *p)
{
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
        char c = p[i]; v <<= 4;
        if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
        else return 0xFFFFFFFFu;
    }
    return v;
}

static const char *JStr(const char *p, char **out)
{
    p++;
    const char *e = p;
    while (*e && *e != '"') { if (*e == '\\' && e[1]) e++; e++; }
    if (*e != '"') return NULL;
    char *o = (char *)malloc((size_t)(e - p) * 2 + 1), *q = o;
    if (!o) return NULL;
    while (p < e) {
        if (*p != '\\') { *q++ = *p++; continue; }
        p++;
        switch (*p) {
        case 'n': *q++ = '\n'; break;
        case 't': *q++ = '\t'; break;
        case 'r': case 'b': case 'f': break;
        case 'u': {
            unsigned c = JHex4(p + 1);
            if (c == 0xFFFFFFFFu) break;
            p += 4;
            if (c >= 0xD800 && c < 0xDC00 && p[1] == '\\' && p[2] == 'u') {
                unsigned d = JHex4(p + 3);
                if (d >= 0xDC00 && d < 0xE000) { c = 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00); p += 6; }
            }
            JPutUtf8(&q, c);
            break; }
        default: *q++ = *p;
        }
        p++;
    }
    *q = 0; *out = o;
    return e + 1;
}

static const char *JVal(const char *p, JV *j, int depth)
{
    memset(j, 0, sizeof *j);
    p = JWs(p);
    if (depth > 64) return NULL;
    if (*p == '{' || *p == '[') {
        int obj = (*p == '{');
        char close = obj ? '}' : ']';
        j->t = obj ? J_OBJ : J_ARR;
        p = JWs(p + 1);
        if (*p == close) return p + 1;
        for (;;) {
            char *key = NULL;
            if (obj) {
                if (*p != '"') return NULL;
                p = JStr(p, &key); if (!p) return NULL;
                p = JWs(p);
                if (*p != ':') { free(key); return NULL; }
                p++;
            }
            if (j->cnt == j->cap) {
                int nc = j->cap ? j->cap * 2 : 8;
                JV *nv = (JV *)realloc(j->v, (size_t)nc * sizeof(JV));
                if (!nv) { free(key); return NULL; }
                j->v = nv;
                if (obj) {
                    char **nk = (char **)realloc(j->k, (size_t)nc * sizeof(char *));
                    if (!nk) { free(key); return NULL; }
                    j->k = nk;
                }
                j->cap = nc;
            }
            p = JVal(p, &j->v[j->cnt], depth + 1);
            if (obj) j->k[j->cnt] = key;
            j->cnt++;
            if (!p) return NULL;
            p = JWs(p);
            if (*p == ',') { p = JWs(p + 1); continue; }
            if (*p == close) return p + 1;
            return NULL;
        }
    }
    if (*p == '"') { j->t = J_STR; return JStr(p, &j->s); }
    if (!strncmp(p, "true", 4))  { j->t = J_BOOL; j->n = 1; return p + 4; }
    if (!strncmp(p, "false", 5)) { j->t = J_BOOL; return p + 5; }
    if (!strncmp(p, "null", 4))  { j->t = J_NULL; return p + 4; }
    char *e; j->n = strtod(p, &e);
    if (e == p) return NULL;
    j->t = J_NUM;
    return e;
}

static const JV *JGet(const JV *o, const char *k)
{
    if (!o || o->t != J_OBJ) return NULL;
    for (int i = 0; i < o->cnt; i++) if (o->k[i] && !strcmp(o->k[i], k)) return &o->v[i];
    return NULL;
}
static const char *JGetS(const JV *o, const char *k) { const JV *v = JGet(o, k); return v && v->t == J_STR ? v->s : NULL; }
static double JGetN(const JV *o, const char *k) { const JV *v = JGet(o, k); return v && v->t == J_NUM ? v->n : 0; }

/* ------------------------------------------------------------------ */
/* Programmes                                                          */
/* ------------------------------------------------------------------ */
typedef struct { long long st, en; wchar_t *title, *ep, *syn; } Prog;
typedef struct { wchar_t num[16]; wchar_t name[64]; Prog *p; int n, cap; } GuideCh;
typedef struct { GuideCh *ch; int n, cap; long long maxEnd; wchar_t err[256]; } GuideResult;
typedef struct { wchar_t base[256]; LONG gen; } GuideArg;

static GuideCh  *g_gd;
static int       g_ngd;
static long long g_gdEnd;
static wchar_t   g_gdInfo[200] = L"Guide TV : en attente";
static LONG      g_gdGen;
static HWND      g_gw, g_grid;           /* fenêtre Guide TV (guide_ui.h) */

static long long NowEpoch(void)
{
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u; u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime;
    return (long long)(u.QuadPart / 10000000ULL) - 11644473600LL;
}

static void EpochToLocal(long long t, SYSTEMTIME *st)
{
    ULARGE_INTEGER u; u.QuadPart = (unsigned long long)(t + 11644473600LL) * 10000000ULL;
    FILETIME ft; ft.dwLowDateTime = u.LowPart; ft.dwHighDateTime = u.HighPart;
    SYSTEMTIME utc; FileTimeToSystemTime(&ft, &utc);
    if (!SystemTimeToTzSpecificLocalTime(NULL, &utc, st)) *st = utc;
}

static void FmtHM(long long t, wchar_t *o)
{
    SYSTEMTIME s; EpochToLocal(t, &s);
    swprintf(o, 8, L"%02u:%02u", s.wHour, s.wMinute);
}

static wchar_t *WDup8(const char *s)
{
    if (!s || !*s) return NULL;
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0) return NULL;
    wchar_t *w = (wchar_t *)malloc((size_t)n * sizeof(wchar_t));
    if (w) MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

static void FreeGuide(GuideCh *g, int n)
{
    if (!g) return;
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < g[i].n; j++) { free(g[i].p[j].title); free(g[i].p[j].ep); free(g[i].p[j].syn); }
        free(g[i].p);
    }
    free(g);
}

static void MergeGuide(GuideResult *r, const JV *arr)
{
    for (int i = 0; i < arr->cnt; i++) {
        const JV *c = &arr->v[i];
        const char *num = JGetS(c, "GuideNumber");
        const JV *progs = JGet(c, "Guide");
        if (!num || !progs || progs->t != J_ARR) continue;
        wchar_t wnum[16]; Utf8ToW(num, wnum, 16);
        GuideCh *g = NULL;
        for (int k = 0; k < r->n; k++) if (!wcscmp(r->ch[k].num, wnum)) { g = &r->ch[k]; break; }
        if (!g) {
            if (r->n == r->cap) {
                int nc = r->cap ? r->cap * 2 : 32;
                GuideCh *nb = (GuideCh *)realloc(r->ch, (size_t)nc * sizeof(GuideCh));
                if (!nb) return;
                r->ch = nb; r->cap = nc;
            }
            g = &r->ch[r->n++];
            memset(g, 0, sizeof *g);
            wcscpy(g->num, wnum);
            const char *nm = JGetS(c, "GuideName");
            Utf8ToW(nm ? nm : "", g->name, 64);
        }
        for (int j = 0; j < progs->cnt; j++) {
            const JV *p = &progs->v[j];
            long long st = (long long)JGetN(p, "StartTime"), en = (long long)JGetN(p, "EndTime");
            if (en <= st) continue;
            if (g->n && st < g->p[g->n - 1].en) continue;          /* chevauchement entre pages */
            if (g->n == g->cap) {
                int nc = g->cap ? g->cap * 2 : 16;
                Prog *np = (Prog *)realloc(g->p, (size_t)nc * sizeof(Prog));
                if (!np) break;
                g->p = np; g->cap = nc;
            }
            Prog *q = &g->p[g->n++];
            q->st = st; q->en = en;
            q->title = WDup8(JGetS(p, "Title"));
            const char *et = JGetS(p, "EpisodeTitle"), *en_ = JGetS(p, "EpisodeNumber");
            if (et && en_) {
                char b[512]; snprintf(b, sizeof b, "%s (%s)", et, en_); q->ep = WDup8(b);
            } else q->ep = WDup8(et ? et : en_);
            q->syn = WDup8(JGetS(p, "Synopsis"));
            if (en > r->maxEnd) r->maxEnd = en;
        }
    }
}

static DWORD WINAPI GuideThread(LPVOID pv)
{
    GuideArg *a = (GuideArg *)pv;
    GuideResult *r = (GuideResult *)calloc(1, sizeof *r);
    if (!r) { free(a); return 0; }
    wchar_t url[700]; size_t len = 0;
    char auth[128] = "";

    swprintf(url, 700, L"%ls/discover.json", a->base);
    char *buf = ReadHttp(url, &len, r->err);
    if (buf) {
        JV j; JVal(buf, &j, 0);
        const char *s = JGetS(&j, "DeviceAuth");
        if (s) { strncpy(auth, s, sizeof auth - 1); auth[sizeof auth - 1] = 0; }
        JFree(&j); free(buf);
    }
    if (!auth[0]) {
        if (!r->err[0]) swprintf(r->err, 256, L"aucun HDHomeRun détecté sur %ls", a->base);
    } else {
        wchar_t wa[128]; Utf8ToW(auth, wa, 128);
        long long now = NowEpoch(), start = 0;
        for (int i = 0; i < 8; i++) {
            if (start) swprintf(url, 700, L"https://api.hdhomerun.com/api/guide.php?DeviceAuth=%ls&Start=%lld", wa, start);
            else       swprintf(url, 700, L"https://api.hdhomerun.com/api/guide.php?DeviceAuth=%ls", wa);
            wchar_t e[256] = L"";
            buf = ReadHttp(url, &len, e);
            if (!buf) { if (!r->n) wcscpy(r->err, e); break; }
            JV j; const char *ok = JVal(buf, &j, 0);
            long long before = r->maxEnd;
            if (ok && j.t == J_ARR) MergeGuide(r, &j);
            JFree(&j); free(buf);
            if (r->maxEnd <= before) break;
            start = r->maxEnd;
            if (r->maxEnd >= now + 24 * 3600) break;
        }
        if (!r->n && !r->err[0]) swprintf(r->err, 256, L"le service guide n'a renvoyé aucun programme");
    }
    PostMessageW(g_main, WM_APP_GUIDE, (WPARAM)a->gen, (LPARAM)r);
    free(a);
    return 0;
}

/* Base « http://hôte » de la playlist (le tuner HDHomeRun). */
static int PlaylistHost(wchar_t *out, int cap)
{
    const wchar_t *p = C.playlist, *s = wcsstr(p, L"://");
    if (!s || _wcsnicmp(p, L"http", 4)) return 0;
    s += 3;
    size_t n = wcscspn(s, L"/:?#");
    if (!n || (size_t)(s - p) + n + 1 > (size_t)cap) return 0;
    swprintf(out, (size_t)cap, L"http://%.*ls", (int)n, s);
    return 1;
}

static void GuideRefreshUI(void);
static void GuideFonts(void);

static void GuideLoad(void)
{
    if (!C.guide) return;
    GuideArg *a = (GuideArg *)calloc(1, sizeof *a);
    if (!a) return;
    if (!PlaylistHost(a->base, 256)) {
        free(a);
        swprintf(g_gdInfo, 200, L"Guide TV : la playlist n'est pas celle d'un HDHomeRun");
        GuideRefreshUI();
        return;
    }
    a->gen = InterlockedIncrement(&g_gdGen);
    if (!g_ngd) { swprintf(g_gdInfo, 200, L"Guide TV : chargement…"); GuideRefreshUI(); }
    HANDLE t = CreateThread(NULL, 0, GuideThread, a, 0, NULL);
    if (t) CloseHandle(t); else free(a);
}

static void GuideClear(void)
{
    FreeGuide(g_gd, g_ngd); g_gd = NULL; g_ngd = 0; g_gdEnd = 0;
}

static void OnGuide(WPARAM gen, GuideResult *r)
{
    if ((LONG)gen == g_gdGen && C.guide) {
        if (r->n) {
            GuideClear();
            g_gd = r->ch; g_ngd = r->n; g_gdEnd = r->maxEnd;
            r->ch = NULL;
            wchar_t hm[8]; FmtHM(g_gdEnd, hm);
            SYSTEMTIME d; EpochToLocal(g_gdEnd, &d);
            static const wchar_t *jours[7] = { L"dimanche", L"lundi", L"mardi", L"mercredi", L"jeudi", L"vendredi", L"samedi" };
            swprintf(g_gdInfo, 200, L"Guide TV : %d chaînes, jusqu'à %ls %u à %ls", g_ngd, jours[d.wDayOfWeek % 7], d.wDay, hm);
        } else {
            swprintf(g_gdInfo, 200, L"Guide TV indisponible : %ls", r->err);
        }
        GuideRefreshUI();
    }
    FreeGuide(r->ch, r->n);
    free(r);
}

/* Correspondance chaîne ↔ guide par numéro (channel-number du m3u = GuideNumber). */
static const GuideCh *GuideForCh(int ci)
{
    if (ci < 0 || ci >= g_nch || !g_ngd) return NULL;
    const Channel *c = &g_ch[ci];
    wchar_t key[16];
    if (c->num[0]) wcscpy(key, c->num);
    else {
        int i = 0;
        while (i < 15 && c->name[i] >= L'0' && c->name[i] <= L'9') { key[i] = c->name[i]; i++; }
        key[i] = 0;
    }
    if (!key[0]) return NULL;
    for (int i = 0; i < g_ngd; i++) if (!wcscmp(g_gd[i].num, key)) return &g_gd[i];
    return NULL;
}

/* Programme en cours à l'instant t (et le suivant). */
static const Prog *ProgAt(const GuideCh *g, long long t, const Prog **next)
{
    if (next) *next = NULL;
    if (!g) return NULL;
    for (int i = 0; i < g->n; i++) {
        if (g->p[i].en > t) {
            if (g->p[i].st <= t) { if (next) *next = (i + 1 < g->n) ? &g->p[i + 1] : NULL; return &g->p[i]; }
            if (next) *next = &g->p[i];
            return NULL;
        }
    }
    return NULL;
}
