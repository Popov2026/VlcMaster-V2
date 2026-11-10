/*
 * VLC Master 2 — lecteur IPTV natif (Win32 + libVLC)
 *
 * La vidéo est rendue directement dans la fenêtre de l'application via
 * libVLC (plus de VLC externe piloté en HTTP). libvlc.dll est chargée
 * dynamiquement : il suffit qu'un VLC 3.x 64 bits soit installé (ou que
 * son dossier soit indiqué dans les paramètres).
 *
 * By Popov ©2026 — réécriture C.
 */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <wininet.h>
#include <uxtheme.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "libvlc_dyn.h"

#define APP_NAME L"VLC Master"
#define APP_VER  L"2.0"

/* ------------------------------------------------------------------ */
/* Identifiants                                                        */
/* ------------------------------------------------------------------ */
enum { WM_APP_VLCREADY = WM_APP + 1, WM_APP_VLCEVENT, WM_APP_PLAYLIST, WM_APP_WORKERDONE };
enum { TMR_FS = 1, TMR_DIGITS, TMR_RETRY, TMR_REPLAY, TMR_FORCEQUIT, TMR_FLASH, TMR_SIZE, TMR_HUD };

enum {
    IDC_HOST = 100, IDC_LIST, IDC_PREV, IDC_NEXT, IDC_PAUSE, IDC_STOP, IDC_VOL,
    IDC_MUTE, IDC_STATUS, IDC_CHANS, IDC_FULL, IDC_SETTINGS, IDC_SIZEBTN, IDC_TOPBTN,

    IDS_URL = 200, IDS_URLBROWSE, IDS_RELOAD, IDS_CACHE, IDS_CACHESPIN, IDS_HW,
    IDS_DEINT, IDS_ASPECT, IDS_DELAY, IDS_DELAYSPIN, IDS_TOPMOST, IDS_RESUME,
    IDS_AUTOPLAY, IDS_SHOWLIST, IDS_VLCDIR, IDS_VLCBROWSE, IDS_LV, IDS_UP,
    IDS_DOWN, IDS_RENAME, IDS_ORIGNAME, IDS_SHOWALL, IDS_CLOSE,
    IDS_THIN, IDS_THINBAR, IDS_SIZES, IDS_HUD, IDS_HUDSPIN,

    IDM_PLAY = 300, IDM_RENAME, IDM_HIDE,
    IDM_ASPECT0 = 400, IDM_DEINT0 = 450,
    IDM_SIZEDEF = 499, IDM_SIZE0 = 500, IDM_THIN = 600, IDM_MINIMIZE, IDM_QUIT
};

/* ------------------------------------------------------------------ */
/* Options                                                             */
/* ------------------------------------------------------------------ */
typedef struct { const wchar_t *label; const char *val; } Opt;

static const Opt HW_OPTS[] = {
    { L"Automatique",                 "any"     },
    { L"Désactivé (logiciel)",        "none"    },
    { L"Direct3D 11 (D3D11VA)",       "d3d11va" },
    { L"DXVA 2",                      "dxva2"   },
};
static const Opt DEINT_OPTS[] = {
    { L"Désactivé",          NULL      },
    { L"Yadif",              "yadif"   },
    { L"Yadif 2x (fluide)",  "yadif2x" },
    { L"Bob",                "bob"     },
    { L"Linéaire",           "linear"  },
    { L"Mélange",            "blend"   },
    { L"Moyenne",            "mean"    },
    { L"Rejet",              "discard" },
    { L"X",                  "x"       },
    { L"Phosphore",          "phosphor"},
    { L"Film NTSC (IVTC)",   "ivtc"    },
};
static const Opt ASPECT_OPTS[] = {
    { L"Par défaut", NULL      },
    { L"16:9",       "16:9"    },
    { L"4:3",        "4:3"     },
    { L"16:10",      "16:10"   },
    { L"21:9",       "21:9"    },
    { L"2.35:1",     "235:100" },
    { L"5:4",        "5:4"     },
    { L"1:1",        "1:1"     },
};
#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* ------------------------------------------------------------------ */
/* Données                                                             */
/* ------------------------------------------------------------------ */
typedef struct {
    wchar_t name[128];      /* nom affiché (renommable)      */
    wchar_t orig[128];      /* nom issu de la playlist       */
    wchar_t num[16];        /* numéro de chaîne (m3u)        */
    char    url[1024];      /* MRL UTF-8                     */
    unsigned long long key; /* hash de l'URL (persistance)   */
    int hidden, order, src;
} Channel;

static struct {
    wchar_t playlist[2048];
    wchar_t vlcdir[MAX_PATH];
    int caching, hw, deint, aspect, delay, volume, muted;
    int topmost, resume, autoplay, showlist;
    int thin, thinHideBar, sizeIdx, hudSecs;
    wchar_t sizes[256];
    unsigned long long lastkey;
    RECT mainrc; int mainmax; RECT setrc;
} C;

static HINSTANCE g_hi;
static wchar_t   g_ini[MAX_PATH], g_exedir[MAX_PATH];

static HWND  g_hud;
static HWND  g_main, g_host, g_list, g_status, g_vol, g_tip, g_set;
static HFONT g_font, g_fontBig;
static UINT  g_dpi = 96;

static Channel *g_ch;  static int g_nch;
static int     *g_vis; static int g_nvis;
static int      g_cur = -1;
static LONG     g_plGen;

/* état lecture */
static int     g_vlcState;          /* 0 = chargement, 1 = prêt, -1 = échec */
static int     g_workerAlive, g_quitting;
static wchar_t g_vlcInfo[512];
static wchar_t g_vlcDirReq[MAX_PATH];
static int     g_playing, g_paused, g_userStopped, g_retries;
static wchar_t g_state[96], g_size[48], g_hostMsg[512], g_flash[96];
static DWORD   g_flashUntil;
static int     g_full, g_fsBar; static DWORD g_fsLastNear; static WINDOWPLACEMENT g_wpPrev;
static wchar_t g_digits[8];
static int     g_menuCh = -1;
static RECT    g_sizeMoveRc;

/* thread VLC */
static libvlc_instance_t     *g_inst;
static libvlc_media_player_t *g_mp;
static CRITICAL_SECTION       g_cs;
static HANDLE                 g_wake;
static struct { int has, stop, quit, caching; char url[1024]; char hw[16]; } g_req;

/* fenêtre paramètres */
static struct {
    HWND hdr[5], line[5];
    HWND lblUrl, url, urlBr, reload;
    HWND lblCache, cache, cacheSp, lblHw, hw, lblDeint, deint, lblAsp, aspect, lblDelay, delay, delaySp;
    HWND topmost, resume, autoplay, showlist;
    HWND thin, thinbar, lblSizes, sizes, lblHud, hud, hudSp;
    HWND lblVlc, vlcdir, vlcBr, vlcInfo;
    HWND lv, up, down, rename, orig, showall, hint, close;
} U;
static HFONT g_sfont, g_sfontBold;
static UINT  g_sdpi = 96;
static int   g_setInit, g_lvFilling;

static int S(int v)  { return MulDiv(v, (int)g_dpi, 96); }
static int SS(int v) { return MulDiv(v, (int)g_sdpi, 96); }

/* ------------------------------------------------------------------ */
/* Utilitaires                                                         */
/* ------------------------------------------------------------------ */
static void Utf8ToW(const char *s, wchar_t *out, int cap)
{
    UINT cp = CP_UTF8;
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);
    if (n <= 0) { cp = CP_ACP; n = MultiByteToWideChar(CP_ACP, 0, s, -1, NULL, 0); }
    if (n <= 0) { out[0] = 0; return; }
    wchar_t *tmp = (n <= cap) ? out : (wchar_t *)malloc((size_t)n * sizeof(wchar_t));
    if (!tmp) { out[0] = 0; return; }
    MultiByteToWideChar(cp, 0, s, -1, tmp, n);
    if (tmp != out) { wcsncpy(out, tmp, (size_t)cap - 1); out[cap - 1] = 0; free(tmp); }
}

static void WToUtf8(const wchar_t *s, char *out, int cap)
{
    if (!WideCharToMultiByte(CP_UTF8, 0, s, -1, out, cap, NULL, NULL)) out[0] = 0;
}

static unsigned long long Fnv(const char *s)
{
    unsigned long long h = 1469598103934665603ULL;
    while (*s) { h ^= (unsigned char)*s++; h *= 1099511628211ULL; }
    return h;
}

static void TrimW(wchar_t *s)
{
    wchar_t *p = s; while (*p == L' ' || *p == L'\t' || *p == L'"') p++;
    if (p != s) memmove(s, p, (wcslen(p) + 1) * sizeof(wchar_t));
    size_t n = wcslen(s);
    while (n && (s[n-1] == L' ' || s[n-1] == L'\t' || s[n-1] == L'"')) s[--n] = 0;
    if (n > 3 && s[n-1] == L'\\') s[--n] = 0;   /* « C:\VLC\ » -> « C:\VLC » */
}

static HFONT MakeFont(UINT dpi, int pt, int weight)
{
    return CreateFontW(-MulDiv(pt, (int)dpi, 72), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH, L"Segoe UI");
}

static BOOL CALLBACK SetFontProc(HWND h, LPARAM f) { SendMessageW(h, WM_SETFONT, (WPARAM)f, TRUE); return TRUE; }

static HWND Mk(HWND parent, const wchar_t *cls, const wchar_t *txt, DWORD style, DWORD ex, int id)
{
    return CreateWindowExW(ex, cls, txt, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10,
                           parent, (HMENU)(INT_PTR)id, g_hi, NULL);
}

static int PickFile(HWND owner, const wchar_t *filter, const wchar_t *title, wchar_t *out, int cap)
{
    OPENFILENAMEW o; memset(&o, 0, sizeof o);
    o.lStructSize = sizeof o; o.hwndOwner = owner; o.lpstrFilter = filter;
    o.lpstrFile = out; o.nMaxFile = (DWORD)cap; o.lpstrTitle = title;
    o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_EXPLORER | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&o);
}

/* ------------------------------------------------------------------ */
/* Configuration (INI UTF-16, portable à côté de l'exe ou %APPDATA%)  */
/* ------------------------------------------------------------------ */
static int TryCreateIni(const wchar_t *path)
{
    if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) {
        HANDLE f = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (f == INVALID_HANDLE_VALUE) return 0;
        CloseHandle(f); return 1;
    }
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    static const BYTE bom[2] = { 0xFF, 0xFE };  /* => WritePrivateProfile écrit en Unicode */
    DWORD wr; WriteFile(f, bom, 2, &wr, NULL); CloseHandle(f);
    return 1;
}

static void InitIniPath(void)
{
    swprintf(g_ini, MAX_PATH, L"%ls\\VlcMaster.ini", g_exedir);
    if (TryCreateIni(g_ini)) return;
    wchar_t app[MAX_PATH];
    if (GetEnvironmentVariableW(L"APPDATA", app, MAX_PATH)) {
        wchar_t dir[MAX_PATH]; swprintf(dir, MAX_PATH, L"%ls\\VlcMaster", app);
        CreateDirectoryW(dir, NULL);
        swprintf(g_ini, MAX_PATH, L"%ls\\VlcMaster.ini", dir);
        TryCreateIni(g_ini);
    }
}

static int IniInt(const wchar_t *sec, const wchar_t *k, int def)
{
    wchar_t b[32];
    GetPrivateProfileStringW(sec, k, L"", b, 32, g_ini);
    return b[0] ? _wtoi(b) : def;
}
static void IniPutInt(const wchar_t *sec, const wchar_t *k, int v)
{
    wchar_t b[32]; swprintf(b, 32, L"%d", v); WritePrivateProfileStringW(sec, k, b, g_ini);
}
static void IniRect(const wchar_t *k, RECT *r)
{
    wchar_t b[96]; long a, t, w, h;
    GetPrivateProfileStringW(L"Fenetres", k, L"", b, 96, g_ini);
    if (swscanf(b, L"%ld,%ld,%ld,%ld", &a, &t, &w, &h) == 4) SetRect(r, a, t, w, h);
    else SetRectEmpty(r);
}
static void IniPutRect(const wchar_t *k, const RECT *r)
{
    wchar_t b[96]; swprintf(b, 96, L"%ld,%ld,%ld,%ld", r->left, r->top, r->right, r->bottom);
    WritePrivateProfileStringW(L"Fenetres", k, b, g_ini);
}

static int Clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void LoadConfig(void)
{
    GetPrivateProfileStringW(L"General", L"Playlist", L"http://192.168.1.90/lineup.m3u", C.playlist, 2048, g_ini);
    GetPrivateProfileStringW(L"General", L"VlcDir", L"", C.vlcdir, MAX_PATH, g_ini);
    C.caching  = Clamp(IniInt(L"Lecture", L"Cache", 1000), 0, 20000);
    C.hw       = Clamp(IniInt(L"Lecture", L"Decodage", 0), 0, COUNT(HW_OPTS) - 1);
    C.deint    = Clamp(IniInt(L"Lecture", L"Desentrelacement", 1), 0, COUNT(DEINT_OPTS) - 1);
    C.aspect   = Clamp(IniInt(L"Lecture", L"Format", 0), 0, COUNT(ASPECT_OPTS) - 1);
    C.delay    = Clamp(IniInt(L"Lecture", L"SynchroAudio", 0), -10000, 10000);
    C.volume   = Clamp(IniInt(L"Lecture", L"Volume", 100), 0, 150);
    C.muted    = IniInt(L"Lecture", L"Muet", 0) != 0;
    C.topmost  = IniInt(L"Interface", L"PremierPlan", 0) != 0;
    C.resume   = IniInt(L"Interface", L"Reprendre", 1) != 0;
    C.autoplay = IniInt(L"Interface", L"LectureAuto", 1) != 0;
    C.showlist = IniInt(L"Interface", L"ListeChaines", 1) != 0;
    C.thin     = IniInt(L"Interface", L"BordureFine", 0) != 0;
    C.thinHideBar = IniInt(L"Interface", L"BarreAutoMasquee", 1) != 0;
    C.sizeIdx  = IniInt(L"Fenetres", L"TailleIdx", -1);
    C.hudSecs  = Clamp(IniInt(L"Interface", L"HudSecondes", 3), 0, 60);
    GetPrivateProfileStringW(L"Fenetres", L"Tailles", L"1080p, 720p, 480p, 240p", C.sizes, 256, g_ini);
    C.mainmax  = IniInt(L"Fenetres", L"PrincipaleMax", 0) != 0;
    wchar_t b[32]; GetPrivateProfileStringW(L"General", L"Derniere", L"0", b, 32, g_ini);
    C.lastkey = wcstoull(b, NULL, 16);
    IniRect(L"Principale", &C.mainrc);
    IniRect(L"Parametres", &C.setrc);
}

static void SaveConfig(void)
{
    WritePrivateProfileStringW(L"General", L"Playlist", C.playlist, g_ini);
    WritePrivateProfileStringW(L"General", L"VlcDir", C.vlcdir, g_ini);
    wchar_t b[32]; swprintf(b, 32, L"%016llx", C.lastkey);
    WritePrivateProfileStringW(L"General", L"Derniere", b, g_ini);
    IniPutInt(L"Lecture", L"Cache", C.caching);
    IniPutInt(L"Lecture", L"Decodage", C.hw);
    IniPutInt(L"Lecture", L"Desentrelacement", C.deint);
    IniPutInt(L"Lecture", L"Format", C.aspect);
    IniPutInt(L"Lecture", L"SynchroAudio", C.delay);
    IniPutInt(L"Lecture", L"Volume", C.volume);
    IniPutInt(L"Lecture", L"Muet", C.muted);
    IniPutInt(L"Interface", L"PremierPlan", C.topmost);
    IniPutInt(L"Interface", L"Reprendre", C.resume);
    IniPutInt(L"Interface", L"LectureAuto", C.autoplay);
    IniPutInt(L"Interface", L"ListeChaines", C.showlist);
    IniPutInt(L"Interface", L"BordureFine", C.thin);
    IniPutInt(L"Interface", L"BarreAutoMasquee", C.thinHideBar);
    IniPutInt(L"Fenetres", L"TailleIdx", C.sizeIdx);
    IniPutInt(L"Interface", L"HudSecondes", C.hudSecs);
    WritePrivateProfileStringW(L"Fenetres", L"Tailles", C.sizes, g_ini);
    IniPutInt(L"Fenetres", L"PrincipaleMax", C.mainmax);
    if (!IsRectEmpty(&C.mainrc)) IniPutRect(L"Principale", &C.mainrc);
    if (!IsRectEmpty(&C.setrc))  IniPutRect(L"Parametres", &C.setrc);
}

/* Noms, masquage et ordre des chaînes, indexés par hash d'URL. */
static void SaveChannels(void)
{
    if (!g_nch) return;
    size_t cap = (size_t)g_nch * 200 + 16;
    wchar_t *nm = (wchar_t *)calloc(cap, sizeof(wchar_t));
    wchar_t *hd = (wchar_t *)calloc(cap, sizeof(wchar_t));
    wchar_t *od = (wchar_t *)calloc(cap, sizeof(wchar_t));
    if (!nm || !hd || !od) { free(nm); free(hd); free(od); return; }
    wchar_t *pn = nm, *ph = hd, *po = od;
    for (int i = 0; i < g_nch; i++) {
        Channel *c = &g_ch[i];
        if (wcscmp(c->name, c->orig)) pn += swprintf(pn, cap - (size_t)(pn - nm), L"%016llx=%ls", c->key, c->name) + 1;
        if (c->hidden)                ph += swprintf(ph, cap - (size_t)(ph - hd), L"%016llx=1", c->key) + 1;
        po += swprintf(po, cap - (size_t)(po - od), L"%016llx=%d", c->key, i) + 1;
    }
    WritePrivateProfileSectionW(L"Noms", nm, g_ini);
    WritePrivateProfileSectionW(L"Masquees", hd, g_ini);
    WritePrivateProfileSectionW(L"Ordre", od, g_ini);
    free(nm); free(hd); free(od);
}

/* ------------------------------------------------------------------ */
/* Playlist M3U                                                        */
/* ------------------------------------------------------------------ */
typedef struct { Channel *ch; int n; wchar_t err[256]; } PlResult;
typedef struct { wchar_t *src; LONG gen; } PlArg;

static int IsM3U(const wchar_t *s)
{
    size_t n = wcscspn(s, L"?#");
    if (n >= 5 && !_wcsnicmp(s + n - 5, L".m3u8", 5)) return 1;
    if (n >= 4 && !_wcsnicmp(s + n - 4, L".m3u", 4)) return 1;
    return 0;
}

static char *ReadHttp(const wchar_t *url, size_t *len, wchar_t *err)
{
    char *buf = NULL; size_t n = 0, cap = 0;
    HINTERNET hi = InternetOpenW(L"VlcMaster/2.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hi) { swprintf(err, 256, L"WinINet indisponible (%lu)", GetLastError()); return NULL; }
    DWORD to = 5000;
    InternetSetOptionW(hi, INTERNET_OPTION_CONNECT_TIMEOUT, &to, sizeof to);
    InternetSetOptionW(hi, INTERNET_OPTION_RECEIVE_TIMEOUT, &to, sizeof to);
    HINTERNET hu = InternetOpenUrlW(hi, url, NULL, 0,
        INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_UI | INTERNET_FLAG_PRAGMA_NOCACHE, 0);
    if (!hu) {
        swprintf(err, 256, L"Playlist injoignable (erreur %lu)", GetLastError());
        InternetCloseHandle(hi); return NULL;
    }
    DWORD code = 0, sz = sizeof code;
    if (HttpQueryInfoW(hu, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &code, &sz, NULL) && code >= 400) {
        swprintf(err, 256, L"Playlist : réponse HTTP %lu", code);
        InternetCloseHandle(hu); InternetCloseHandle(hi); return NULL;
    }
    for (;;) {
        if (cap - n < 16384) {
            if (cap >= (8u << 20)) break;
            cap = cap ? cap * 2 : 65536;
            char *nb = (char *)realloc(buf, cap + 1);
            if (!nb) break;
            buf = nb;
        }
        DWORD rd = 0;
        if (!InternetReadFile(hu, buf + n, (DWORD)(cap - n), &rd) || !rd) break;
        n += rd;
    }
    InternetCloseHandle(hu); InternetCloseHandle(hi);
    if (buf) buf[n] = 0;
    *len = n;
    return buf;
}

static char *ReadLocal(const wchar_t *path, size_t *len, wchar_t *err)
{
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) { swprintf(err, 256, L"Fichier introuvable : %ls", path); return NULL; }
    LARGE_INTEGER sz; GetFileSizeEx(f, &sz);
    DWORD n = (DWORD)(sz.QuadPart > (8 << 20) ? (8 << 20) : sz.QuadPart), rd = 0;
    char *buf = (char *)malloc((size_t)n + 1);
    if (buf && ReadFile(f, buf, n, &rd, NULL)) buf[rd] = 0; else if (buf) buf[0] = 0;
    CloseHandle(f);
    *len = rd;
    return buf;
}

static void AddCh(PlResult *r, int *cap, const char *name, const char *num, const char *url)
{
    if (r->n >= *cap) {
        int nc = *cap ? *cap * 2 : 64;
        Channel *nb = (Channel *)realloc(r->ch, (size_t)nc * sizeof(Channel));
        if (!nb) return;
        r->ch = nb; *cap = nc;
    }
    Channel *c = &r->ch[r->n];
    memset(c, 0, sizeof *c);
    strncpy(c->url, url, sizeof c->url - 1);
    Utf8ToW(name && *name ? name : url, c->orig, 128);
    wcscpy(c->name, c->orig);
    Utf8ToW(num ? num : "", c->num, 16);
    c->key = Fnv(c->url);
    c->src = r->n;
    c->order = -1;
    r->n++;
}

static void AttrVal(const char *line, const char *attr, char *out, int cap)
{
    out[0] = 0;
    const char *p = strstr(line, attr);
    if (!p) return;
    p += strlen(attr);
    int i = 0;
    while (*p && *p != '"' && i < cap - 1) out[i++] = *p++;
    out[i] = 0;
}

static void ParseM3U(char *buf, PlResult *r)
{
    int cap = 0;
    char name[512] = "", num[16] = "";
    char *p = buf;
    if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) p += 3;
    while (*p) {
        char *e = p; while (*e && *e != '\n' && *e != '\r') e++;
        char save = *e; *e = 0;
        char *l = p; while (*l == ' ' || *l == '\t') l++;
        size_t ln = strlen(l); while (ln && (l[ln-1] == ' ' || l[ln-1] == '\t')) l[--ln] = 0;
        if (!_strnicmp(l, "#EXTINF", 7)) {
            AttrVal(l, "channel-number=\"", num, sizeof num);
            if (!num[0]) AttrVal(l, "tvg-chno=\"", num, sizeof num);
            int q = 0; const char *c = l + 7; name[0] = 0;
            for (; *c; c++) { if (*c == '"') q = !q; else if (*c == ',' && !q) break; }
            if (*c == ',') { c++; while (*c == ' ') c++; strncpy(name, c, sizeof name - 1); name[sizeof name - 1] = 0; }
        } else if (*l && *l != '#') {
            AddCh(r, &cap, name, num, l);
            name[0] = 0; num[0] = 0;
        }
        *e = save;
        p = e; while (*p == '\n' || *p == '\r') p++;
    }
}

static DWORD WINAPI PlaylistThread(LPVOID pv)
{
    PlArg *a = (PlArg *)pv;
    PlResult *r = (PlResult *)calloc(1, sizeof *r);
    if (!r) { free(a->src); free(a); return 0; }
    if (IsM3U(a->src)) {
        size_t len = 0;
        char *buf = wcsstr(a->src, L"://") ? ReadHttp(a->src, &len, r->err) : ReadLocal(a->src, &len, r->err);
        if (buf) {
            ParseM3U(buf, r);
            if (!r->n) swprintf(r->err, 256, L"La playlist ne contient aucune chaîne");
            free(buf);
        }
    } else {
        /* Flux direct (URL ou fichier vidéo) : une seule « chaîne ». */
        char u[1024]; WToUtf8(a->src, u, sizeof u);
        const wchar_t *base = wcsrchr(a->src, L'/'); const wchar_t *b2 = wcsrchr(a->src, L'\\');
        if (b2 > base) base = b2;
        char nm[256]; WToUtf8(base && base[1] ? base + 1 : a->src, nm, sizeof nm);
        int cap = 0; AddCh(r, &cap, nm, "", u);
    }
    PostMessageW(g_main, WM_APP_PLAYLIST, (WPARAM)a->gen, (LPARAM)r);
    free(a->src); free(a);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Thread libVLC : chargement, puis file de commandes (zap coalescé)  */
/* ------------------------------------------------------------------ */
static void VlcEvent(const vlc_event_t *e, void *ud)
{
    static int lastBuf = -1;
    (void)ud;
    LPARAM l = 0;
    if (e->type == EV_Buffering) {
        int b = (int)e->u.buffering.new_cache;
        if (b == lastBuf) return;
        lastBuf = b; l = b;
    } else lastBuf = -1;
    PostMessageW(g_main, WM_APP_VLCEVENT, (WPARAM)e->type, l);
}

static HMODULE TryVlcDir(const wchar_t *dir, DWORD *err)
{
    if (!dir || !dir[0]) return NULL;
    wchar_t p[MAX_PATH * 2];
    size_t n = wcslen(dir);
    swprintf(p, MAX_PATH * 2, L"%ls%lslibvlc.dll", dir, (n && dir[n-1] == L'\\') ? L"" : L"\\");
    if (GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES) return NULL;
    HMODULE h = LoadLibraryExW(p, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!h) *err = GetLastError();
    else wcsncpy(g_vlcDirReq, dir, MAX_PATH - 1);
    return h;
}

static HMODULE LoadVlc(void)
{
    DWORD err = 0; HMODULE h;
    wchar_t dir[MAX_PATH]; DWORD sz;
    if ((h = TryVlcDir(g_vlcDirReq, &err))) return h;
    sz = sizeof dir;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\VideoLAN\\VLC", L"InstallDir",
                     RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, NULL, dir, &sz) == ERROR_SUCCESS
        && (h = TryVlcDir(dir, &err))) return h;
    if ((h = TryVlcDir(g_exedir, &err))) return h;
    swprintf(dir, MAX_PATH, L"%ls\\vlc", g_exedir);
    if ((h = TryVlcDir(dir, &err))) return h;
    if (GetEnvironmentVariableW(L"ProgramFiles", dir, MAX_PATH)) {
        wcscat(dir, L"\\VideoLAN\\VLC");
        if ((h = TryVlcDir(dir, &err))) return h;
    }
    if (GetEnvironmentVariableW(L"ProgramFiles(x86)", dir, MAX_PATH)) {
        wcscat(dir, L"\\VideoLAN\\VLC");
        if ((h = TryVlcDir(dir, &err))) return h;
    }
    if (err == ERROR_BAD_EXE_FORMAT)
        swprintf(g_vlcInfo, 512, L"Le VLC trouvé est en 32 bits.\nInstallez VLC 3.x 64 bits, puis indiquez son dossier dans ⚙ Paramètres.");
    else if (err)
        swprintf(g_vlcInfo, 512, L"libvlc.dll trouvée mais impossible à charger (erreur %lu).", err);
    else
        swprintf(g_vlcInfo, 512, L"VLC introuvable.\nInstallez VLC 3.x 64 bits ou indiquez son dossier dans ⚙ Paramètres.");
    return NULL;
}

static DWORD WINAPI VlcWorker(LPVOID unused)
{
    (void)unused;
    HMODULE h = LoadVlc();
    if (h && !VlcBind(h)) {
        swprintf(g_vlcInfo, 512, L"Version de libVLC incompatible (VLC 3.x 64 bits requis).");
        FreeLibrary(h); h = NULL;
    }
    if (h) {
        const char *args[] = {
            "--no-video-title-show", "--no-snapshot-preview", "--no-stats",
            "--no-sub-autodetect-file", "--drop-late-frames", "--skip-frames", "--quiet",
            "--reset-plugins-cache"
        };
        /* Sans plugins.dat (VLC portable), le scan des ~400 plugins prend
           10-20 s : on demande à libVLC d'écrire le cache une fois pour toutes. */
        wchar_t cache[MAX_PATH * 2];
        swprintf(cache, MAX_PATH * 2, L"%ls\\plugins\\plugins.dat", g_vlcDirReq);
        int argc = COUNT(args) - (GetFileAttributesW(cache) != INVALID_FILE_ATTRIBUTES);
        g_inst = p_libvlc_new(argc, args);
        if (!g_inst) {
            const char *m = p_libvlc_errmsg();
            wchar_t w[256]; Utf8ToW(m ? m : "?", w, 256);
            swprintf(g_vlcInfo, 512, L"Échec d'initialisation de libVLC : %ls", w);
        } else if (!(g_mp = p_libvlc_media_player_new(g_inst))) {
            swprintf(g_vlcInfo, 512, L"Impossible de créer le lecteur libVLC.");
            p_libvlc_release(g_inst); g_inst = NULL;
        }
    }
    if (!g_mp) {
        if (h) FreeLibrary(h);
        PostMessageW(g_main, WM_APP_VLCREADY, 0, 0);
        return 0;
    }

    p_libvlc_media_player_set_hwnd(g_mp, g_host);
    p_libvlc_video_set_mouse_input(g_mp, 0);   /* clics / touches => notre fenêtre */
    p_libvlc_video_set_key_input(g_mp, 0);
    libvlc_event_manager_t *em = p_libvlc_media_player_event_manager(g_mp);
    static const int evs[] = { EV_Opening, EV_Buffering, EV_Playing, EV_Paused, EV_Stopped, EV_EndReached, EV_Error, EV_Vout };
    for (int i = 0; i < COUNT(evs); i++) p_libvlc_event_attach(em, evs[i], VlcEvent, NULL);
    {
        wchar_t v[96]; Utf8ToW(p_libvlc_get_version(), v, 96);
        swprintf(g_vlcInfo, 512, L"libVLC %ls\n%ls", v, g_vlcDirReq);
    }
    PostMessageW(g_main, WM_APP_VLCREADY, 1, 0);

    for (;;) {
        WaitForSingleObject(g_wake, INFINITE);
        EnterCriticalSection(&g_cs);
        int quit = g_req.quit, has = g_req.has, stop = g_req.stop, caching = g_req.caching;
        char url[1024], hw[16];
        memcpy(url, g_req.url, sizeof url); memcpy(hw, g_req.hw, sizeof hw);
        g_req.has = 0;
        LeaveCriticalSection(&g_cs);
        if (quit) break;
        if (!has) continue;
        if (stop) { p_libvlc_media_player_stop(g_mp); continue; }
        libvlc_media_t *m = strstr(url, "://") ? p_libvlc_media_new_location(g_inst, url)
                                               : p_libvlc_media_new_path(g_inst, url);
        if (!m) { PostMessageW(g_main, WM_APP_VLCEVENT, EV_Error, 0); continue; }
        char opt[64];
        snprintf(opt, sizeof opt, ":network-caching=%d", caching); p_libvlc_media_add_option(m, opt);
        snprintf(opt, sizeof opt, ":live-caching=%d", caching);    p_libvlc_media_add_option(m, opt);
        snprintf(opt, sizeof opt, ":avcodec-hw=%s", hw);           p_libvlc_media_add_option(m, opt);
        p_libvlc_media_player_set_media(g_mp, m);
        p_libvlc_media_release(m);
        p_libvlc_media_player_play(g_mp);
    }
    p_libvlc_media_player_stop(g_mp);
    p_libvlc_media_player_release(g_mp); g_mp = NULL;
    p_libvlc_release(g_inst); g_inst = NULL;
    PostMessageW(g_main, WM_APP_WORKERDONE, 0, 0);
    return 0;
}

static void StartVlc(void)
{
    if (g_workerAlive || g_vlcState == 1) return;
    g_vlcState = 0;
    wcsncpy(g_vlcDirReq, C.vlcdir, MAX_PATH - 1);
    swprintf(g_vlcInfo, 512, L"Chargement de libVLC…");
    HANDLE t = CreateThread(NULL, 0, VlcWorker, NULL, 0, NULL);
    if (t) { g_workerAlive = 1; CloseHandle(t); }
}

/* ------------------------------------------------------------------ */
/* Lecture                                                             */
/* ------------------------------------------------------------------ */
static void UpdateStatus(void);
static void ShowHud(void);
static void SettingsSync(void);
static void FillLV(void);

static void SetHostMsg(const wchar_t *m)
{
    wcsncpy(g_hostMsg, m, 511); g_hostMsg[511] = 0;
    if (g_host) InvalidateRect(g_host, NULL, FALSE);
}

static void Flash(const wchar_t *t)
{
    wcsncpy(g_flash, t, 95); g_flash[95] = 0;
    g_flashUntil = GetTickCount() + 1500;
    SetTimer(g_main, TMR_FLASH, 1600, NULL);
    UpdateStatus();
    ShowHud();
}

static void SetState(const wchar_t *s) { wcsncpy(g_state, s, 95); g_state[95] = 0; UpdateStatus(); }

static void RequestPlayCur(void)
{
    if (g_cur < 0 || g_cur >= g_nch) return;
    if (g_vlcState != 1) { SetState(g_vlcState == 0 ? L"En attente de libVLC…" : L"VLC indisponible"); return; }
    EnterCriticalSection(&g_cs);
    g_req.has = 1; g_req.stop = 0; g_req.caching = C.caching;
    memcpy(g_req.url, g_ch[g_cur].url, sizeof g_req.url);
    strncpy(g_req.hw, HW_OPTS[C.hw].val, sizeof g_req.hw - 1);
    LeaveCriticalSection(&g_cs);
    SetEvent(g_wake);
}

static void ApplyVideoOpts(void)
{
    if (g_vlcState != 1) return;
    p_libvlc_video_set_aspect_ratio(g_mp, ASPECT_OPTS[C.aspect].val);
    p_libvlc_video_set_deinterlace(g_mp, DEINT_OPTS[C.deint].val);
}

static void ApplyAudio(void)
{
    if (g_vlcState == 1) {
        p_libvlc_audio_set_volume(g_mp, C.volume);
        p_libvlc_audio_set_mute(g_mp, C.muted);
        p_libvlc_audio_set_delay(g_mp, (int64_t)C.delay * 1000);
    }
    SendMessageW(g_vol, TBM_SETPOS, TRUE, C.volume);
    SetDlgItemTextW(g_main, IDC_MUTE, C.muted ? L"Rétablir" : L"Muet");
}

static void SetVolume(int v)
{
    C.volume = Clamp(v, 0, 150); C.muted = 0;
    ApplyAudio();
    wchar_t t[48]; swprintf(t, 48, L"Volume %d %%", C.volume); Flash(t);
}

static void ToggleMute(void)
{
    C.muted = !C.muted; ApplyAudio();
    Flash(C.muted ? L"Son coupé" : L"Son rétabli");
}

static void TogglePause(void)
{
    if (g_vlcState != 1) return;
    if (!g_playing && !g_paused) { g_userStopped = 0; RequestPlayCur(); return; }
    p_libvlc_media_player_set_pause(g_mp, !g_paused);
}

static void StopPlayback(void)
{
    g_userStopped = 1;
    KillTimer(g_main, TMR_RETRY);
    if (g_vlcState != 1) return;
    EnterCriticalSection(&g_cs); g_req.has = 1; g_req.stop = 1; LeaveCriticalSection(&g_cs);
    SetEvent(g_wake);
}

static void PlayChannel(int i)
{
    if (i < 0 || i >= g_nch) return;
    g_cur = i; g_retries = 0; g_userStopped = 0;
    KillTimer(g_main, TMR_RETRY);
    C.lastkey = g_ch[i].key;
    for (int v = 0; v < g_nvis; v++)
        if (g_vis[v] == i) { SendMessageW(g_list, LB_SETCURSEL, (WPARAM)v, 0); break; }
    wchar_t m[256]; swprintf(m, 256, L"Ouverture de « %ls »…", g_ch[i].name);
    SetHostMsg(g_vlcState == -1 ? g_vlcInfo : m);
    g_size[0] = 0;
    SetState(L"Ouverture…");
    RequestPlayCur();
    ShowHud();
}

static int VisPos(int ch)
{
    for (int v = 0; v < g_nvis; v++) if (g_vis[v] == ch) return v;
    return -1;
}

static void Zap(int step)
{
    if (!g_nvis) return;
    int p = VisPos(g_cur);
    p = (p < 0) ? (step > 0 ? 0 : g_nvis - 1) : ((p + step) % g_nvis + g_nvis) % g_nvis;
    PlayChannel(g_vis[p]);
}

/* ------------------------------------------------------------------ */
/* Liste des chaînes                                                   */
/* ------------------------------------------------------------------ */
static void RebuildVisible(void)
{
    free(g_vis); g_vis = (int *)malloc(sizeof(int) * (size_t)(g_nch + 1)); g_nvis = 0;
    if (!g_vis) return;
    for (int i = 0; i < g_nch; i++) if (!g_ch[i].hidden) g_vis[g_nvis++] = i;
}

static void ChannelLabel(int v, wchar_t *t, int cap)
{
    const Channel *c = &g_ch[g_vis[v]];
    wchar_t num[16];
    if (c->num[0]) wcscpy(num, c->num); else swprintf(num, 16, L"%d", v + 1);
    size_t n = wcslen(num);
    if (!wcsncmp(c->name, num, n) && (c->name[n] == L' ' || c->name[n] == 0)) swprintf(t, cap, L"%ls", c->name);
    else swprintf(t, cap, L"%ls   %ls", num, c->name);
}

/* Liste dessinée : numéro aligné en gris, nom, surlignage de la chaîne en cours. */
static void DrawChannelItem(const DRAWITEMSTRUCT *d)
{
    HDC dc = d->hDC; RECT rc = d->rcItem;
    int sel = (d->itemState & ODS_SELECTED) != 0;
    HBRUSH bg = CreateSolidBrush(sel ? RGB(0, 103, 192) : GetSysColor(COLOR_WINDOW));
    FillRect(dc, &rc, bg); DeleteObject(bg);
    if ((int)d->itemID < 0 || (int)d->itemID >= g_nvis) return;
    const Channel *c = &g_ch[g_vis[d->itemID]];
    wchar_t num[16];
    if (c->num[0]) wcscpy(num, c->num); else swprintf(num, 16, L"%u", d->itemID + 1);
    const wchar_t *name = c->name;
    size_t n = wcslen(num);
    if (!wcsncmp(name, num, n) && name[n] == L' ') { name += n; while (*name == L' ') name++; }
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ of = SelectObject(dc, g_font);
    RECT r1 = rc; r1.left += S(6); r1.right = r1.left + S(30);
    SetTextColor(dc, sel ? RGB(200, 225, 255) : RGB(120, 120, 128));
    DrawTextW(dc, num, -1, &r1, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    RECT r2 = rc; r2.left = r1.right + S(12); r2.right -= S(6);
    SetTextColor(dc, sel ? RGB(255, 255, 255) : GetSysColor(COLOR_WINDOWTEXT));
    DrawTextW(dc, name, -1, &r2, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, of);
    if (d->itemState & ODS_FOCUS) { RECT f = rc; InflateRect(&f, -1, -1); DrawFocusRect(dc, &f); }
}

static void FillMainList(void)
{
    SendMessageW(g_list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(g_list, LB_RESETCONTENT, 0, 0);
    for (int v = 0; v < g_nvis; v++) {
        wchar_t t[200]; ChannelLabel(v, t, 200);
        SendMessageW(g_list, LB_ADDSTRING, 0, (LPARAM)t);
    }
    int p = VisPos(g_cur);
    SendMessageW(g_list, LB_SETCURSEL, (WPARAM)p, 0);
    SendMessageW(g_list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_list, NULL, TRUE);
}

static void ChannelsChanged(int save)
{
    RebuildVisible();
    FillMainList();
    if (save) SaveChannels();
    UpdateStatus();
}

static int CmpCh(const void *a, const void *b)
{
    const Channel *x = (const Channel *)a, *y = (const Channel *)b;
    int ka = x->order >= 0 ? x->order : 1000000 + x->src;
    int kb = y->order >= 0 ? y->order : 1000000 + y->src;
    return ka < kb ? -1 : ka > kb;
}

static void LoadPlaylist(void)
{
    LONG gen = InterlockedIncrement(&g_plGen);
    TrimW(C.playlist);
    if (!C.playlist[0]) {
        free(g_ch); g_ch = NULL; g_nch = 0; g_cur = -1;
        ChannelsChanged(0);
        SetHostMsg(L"Aucune playlist.\nIndiquez l'adresse de votre lineup.m3u dans ⚙ Paramètres.");
        if (g_set) FillLV();
        return;
    }
    PlArg *a = (PlArg *)malloc(sizeof *a);
    if (!a) return;
    a->src = _wcsdup(C.playlist); a->gen = gen;
    SetState(L"Chargement de la playlist…");
    HANDLE t = CreateThread(NULL, 0, PlaylistThread, a, 0, NULL);
    if (t) CloseHandle(t); else { free(a->src); free(a); }
}

static void OnPlaylist(WPARAM gen, PlResult *r)
{
    if ((LONG)gen != g_plGen) { free(r->ch); free(r); return; }
    unsigned long long curKey = (g_cur >= 0 && g_cur < g_nch) ? g_ch[g_cur].key : 0;
    free(g_ch); g_ch = r->ch; g_nch = r->n; g_cur = -1;

    for (int i = 0; i < g_nch; i++) {
        Channel *c = &g_ch[i];
        wchar_t k[24]; swprintf(k, 24, L"%016llx", c->key);
        wchar_t nm[128];
        GetPrivateProfileStringW(L"Noms", k, L"", nm, 128, g_ini);
        if (nm[0]) wcscpy(c->name, nm);
        c->hidden = IniInt(L"Masquees", k, 0) != 0;
        c->order  = IniInt(L"Ordre", k, -1);
    }
    if (g_nch > 1) qsort(g_ch, (size_t)g_nch, sizeof(Channel), CmpCh);
    RebuildVisible();

    if (curKey) for (int i = 0; i < g_nch; i++) if (g_ch[i].key == curKey) { g_cur = i; break; }
    FillMainList();
    if (g_set) FillLV();

    if (r->err[0]) {
        SetState(r->err);
        wchar_t m[400]; swprintf(m, 400, L"%ls\n\nVérifiez l'adresse de la playlist dans ⚙ Paramètres.", r->err);
        SetHostMsg(m);
    } else if (g_cur < 0) {
        int pick = -1;
        if (C.resume && C.lastkey)
            for (int i = 0; i < g_nch; i++) if (g_ch[i].key == C.lastkey && !g_ch[i].hidden) { pick = i; break; }
        if (pick < 0 && g_nvis) pick = g_vis[0];
        wchar_t t[64]; swprintf(t, 64, L"%d chaînes", g_nch); SetState(t);
        if (pick >= 0 && C.autoplay) PlayChannel(pick);
        else {
            g_cur = pick; FillMainList();
            SetHostMsg(L"Choisissez une chaîne dans la liste\n(ou ← → pour zapper).");
        }
    }
    free(r);
}

/* ------------------------------------------------------------------ */
/* Plein écran                                                         */
/* ------------------------------------------------------------------ */
static void LayoutMain(void);

/* Style de la fenêtre principale selon le mode (normal / bordure fine / plein écran). */
static DWORD MainStyle(void)
{
    DWORD base = WS_CLIPCHILDREN | (IsWindowVisible(g_main) ? WS_VISIBLE : 0);
    if (g_full) return base | WS_POPUP;
    if (C.thin) return base | WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX;
    return base | WS_OVERLAPPEDWINDOW;
}

static int ThinFrame(void) { return C.thin && !g_full; }
static int ThinB(void)     { return ThinFrame() ? S(2) : 0; }       /* liseré visible */
static int AutoBar(void)   { return g_full || (C.thin && C.thinHideBar); }

static void UpdateAutoBar(void)
{
    if (AutoBar()) SetTimer(g_main, TMR_FS, 200, NULL);
    else { KillTimer(g_main, TMR_FS); g_fsBar = 0; }
}

/* Coins arrondis Windows 11 aussi en bordure fine (sans effet ailleurs). */
static void ApplyCorners(void)
{
    typedef HRESULT (WINAPI *DwmSetAttr)(HWND, DWORD, LPCVOID, DWORD);
    static DwmSetAttr fn; static int tried;
    if (!tried) {
        tried = 1;
        HMODULE m = LoadLibraryW(L"dwmapi.dll");
        if (m) fn = (DwmSetAttr)(void (*)(void))GetProcAddress(m, "DwmSetWindowAttribute");
    }
    if (!fn) return;
    DWORD pref = g_full ? 1 /* DWMWCP_DONOTROUND */ : 2 /* DWMWCP_ROUND */;
    fn(g_main, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &pref, sizeof pref);
}

static void SetFullscreen(int on)
{
    if (on == g_full) return;
    if (on) {
        g_wpPrev.length = sizeof g_wpPrev;
        GetWindowPlacement(g_main, &g_wpPrev);
        MONITORINFO mi = { sizeof mi };
        GetMonitorInfoW(MonitorFromWindow(g_main, MONITOR_DEFAULTTONEAREST), &mi);
        g_full = 1; g_fsBar = 0;
        SetWindowLongW(g_main, GWL_STYLE, (LONG)MainStyle());
        SetWindowPos(g_main, NULL, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    } else {
        g_full = 0;
        SetWindowLongW(g_main, GWL_STYLE, (LONG)MainStyle());
        SetWindowPlacement(g_main, &g_wpPrev);
        SetWindowPos(g_main, NULL, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
    ApplyCorners();
    UpdateAutoBar();
    SetDlgItemTextW(g_main, IDC_FULL, g_full ? L"Fenêtre" : L"Plein écran");
    LayoutMain();
    SetFocus(g_main);
}

/* Bascule bordure fine : la zone cliente (donc l'image) garde sa place à l'écran. */
static void SetThin(int on)
{
    on = !!on;
    if (on == C.thin) return;
    if (g_full) { C.thin = on; SettingsSync(); return; }   /* appliqué en sortant du plein écran */
    if (IsZoomed(g_main)) ShowWindow(g_main, SW_RESTORE);
    RECT r; GetClientRect(g_main, &r);
    MapWindowPoints(g_main, NULL, (POINT *)&r, 2);
    C.thin = on;
    DWORD st = MainStyle();
    SetWindowLongW(g_main, GWL_STYLE, (LONG)st);
    if (!on) AdjustWindowRectExForDpi(&r, st, FALSE, (DWORD)GetWindowLongW(g_main, GWL_EXSTYLE), g_dpi);
    SetWindowPos(g_main, NULL, r.left, r.top, r.right - r.left, r.bottom - r.top,
                 SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    ApplyCorners();
    UpdateAutoBar();
    LayoutMain();
    SettingsSync();
    Flash(on ? L"Bordure fine  (B pour revenir)" : L"Fenêtre normale");
}

/* ---- Tailles prédéfinies : « 1080p » = image 1920×1080, ou « 1280x720 » ---- */
typedef struct { wchar_t label[24]; int w, h; } SizePreset;

static int ParseSizes(SizePreset *out, int max)
{
    wchar_t buf[256]; wcsncpy(buf, C.sizes, 255); buf[255] = 0;
    int n = 0; wchar_t *ctx = NULL;
    for (wchar_t *t = wcstok(buf, L",; \t", &ctx); t && n < max; t = wcstok(NULL, L",; \t", &ctx)) {
        for (wchar_t *q = t; *q; q++) if (*q == L'\u00D7' || *q == L'X') *q = L'x';
        int w = 0, h = 0;
        size_t len = wcslen(t);
        if (len > 1 && (t[len-1] == L'p' || t[len-1] == L'P')) {
            h = _wtoi(t);
            w = ((h * 16) / 9 + 1) & ~1;        /* 16:9, largeur paire */
        } else if (swscanf(t, L"%dx%d", &w, &h) != 2) continue;
        if (w < 64 || h < 64 || w > 8192 || h > 8192) continue;
        if (len > 1 && (t[len-1] == L'p' || t[len-1] == L'P')) swprintf(out[n].label, 24, L"%dp", h);
        else swprintf(out[n].label, 24, L"%d\u00D7%d", w, h);
        out[n].w = w; out[n].h = h; n++;
    }
    return n;
}

static void UpdateSizeButton(void)
{
    SizePreset ps[16]; int n = ParseSizes(ps, 16);
    wchar_t t[40];
    if (C.sizeIdx >= 0 && C.sizeIdx < n) swprintf(t, 40, L"\u25A3  %ls", ps[C.sizeIdx].label);
    else wcscpy(t, L"\u25A3  Taille");
    SetDlgItemTextW(g_main, IDC_SIZEBTN, t);
}

static void ApplySizePreset(int idx)
{
    SizePreset ps[16]; int n = ParseSizes(ps, 16);
    if (!n) { Flash(L"Aucune taille définie (\u2699 Paramètres)"); return; }
    if (idx < 0 || idx >= n) idx = 0;
    if (g_full) SetFullscreen(0);
    if (IsZoomed(g_main) || IsIconic(g_main)) ShowWindow(g_main, SW_RESTORE);

    /* Taille cliente = image demandée + liste + barre (si fixe) + liseré. */
    int b = ThinB();
    int listW = C.showlist ? S(240) : 0;
    int barH = AutoBar() ? 0 : S(46);
    RECT r = { 0, 0, ps[idx].w + listW + 2 * b, ps[idx].h + barH + 2 * b };
    DWORD st = (DWORD)GetWindowLongW(g_main, GWL_STYLE);
    if (!C.thin) AdjustWindowRectExForDpi(&r, st, FALSE, (DWORD)GetWindowLongW(g_main, GWL_EXSTYLE), g_dpi);
    int w = r.right - r.left, h = r.bottom - r.top;

    /* Centrée sur la position actuelle, contenue dans l'écran de travail. */
    RECT cur; GetWindowRect(g_main, &cur);
    MONITORINFO mi = { sizeof mi };
    GetMonitorInfoW(MonitorFromWindow(g_main, MONITOR_DEFAULTTONEAREST), &mi);
    RECT wa = mi.rcWork;
    int clipped = 0;
    if (w > wa.right - wa.left) { w = wa.right - wa.left; clipped = 1; }
    if (h > wa.bottom - wa.top) { h = wa.bottom - wa.top; clipped = 1; }
    int x = (cur.left + cur.right - w) / 2, y = (cur.top + cur.bottom - h) / 2;
    if (x < wa.left) x = wa.left;
    if (y < wa.top) y = wa.top;
    if (x + w > wa.right) x = wa.right - w;
    if (y + h > wa.bottom) y = wa.bottom - h;
    SetWindowPos(g_main, NULL, x, y, w, h, SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE);

    C.sizeIdx = idx;
    UpdateSizeButton();
    wchar_t t[96];
    swprintf(t, 96, clipped ? L"Image %d\u00D7%d : limitée par l'écran" : L"Image %d\u00D7%d", ps[idx].w, ps[idx].h);
    Flash(t);
}

static void ShowSizeMenu(int x, int y, UINT align)
{
    SizePreset ps[16]; int n = ParseSizes(ps, 16);
    HMENU m = CreatePopupMenu();
    for (int i = 0; i < n; i++) {
        wchar_t t[64]; swprintf(t, 64, L"%ls\t%d\u00D7%d", ps[i].label, ps[i].w, ps[i].h);
        AppendMenuW(m, MF_STRING | (C.sizeIdx == i ? MF_CHECKED : 0), IDM_SIZE0 + i, t);
    }
    if (n) AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, IDM_SIZEDEF, L"Définir les tailles…");
    TrackPopupMenu(m, TPM_RIGHTBUTTON | align, x, y, 0, g_main, NULL);
    DestroyMenu(m);
}

static void FsTick(void)
{
    POINT pt; RECT rc;
    GetCursorPos(&pt); GetWindowRect(g_main, &rc);
    int near_ = PtInRect(&rc, pt) && pt.y >= rc.bottom - S(70);
    DWORD now = GetTickCount();
    if (near_) {
        g_fsLastNear = now;
        if (!g_fsBar) { g_fsBar = 1; LayoutMain(); }
    } else if (g_fsBar && now - g_fsLastNear > 2500) {
        g_fsBar = 0; LayoutMain();
    }
}

/* ------------------------------------------------------------------ */
/* Fenêtre principale                                                  */
/* ------------------------------------------------------------------ */
typedef struct { int id; const wchar_t *txt; const wchar_t *tip; int w, prio, right; } BarItem;
static const BarItem BAR[] = {
    { IDC_PREV,     L"◀  CH−",      L"Chaîne précédente  (←)",               78, 0, 0 },
    { IDC_NEXT,     L"CH+  ▶",      L"Chaîne suivante  (→)",                 78, 0, 0 },
    { IDC_PAUSE,    L"Pause",        L"Pause / lecture  (Espace)",            72, 1, 0 },
    { IDC_STOP,     L"Stop",         L"Arrêter la lecture  (S)",              60, 4, 0 },
    { IDC_VOL,      NULL,            L"Volume  (↑ ↓ ou molette sur l'image)", 130, 3, 0 },
    { IDC_MUTE,     L"Muet",         L"Couper / rétablir le son  (M)",        76, 2, 0 },
    { IDC_STATUS,   NULL,            NULL,                                     90, 6, 0 },
    { IDC_SETTINGS, L"⚙  Paramètres", L"Paramètres  (P)",                     118, 0, 1 },
    { IDC_FULL,     L"Plein écran",  L"Plein écran  (F, double-clic, Échap)",  100, 2, 1 },
    { IDC_SIZEBTN,     L"\u25A3  Taille", L"Taille prédéfinie de l'image  (T = suivante, flèche = choisir)", 108, 3, 1 },
    { IDC_TOPBTN,   L"\u25BD  Arrière-plan", L"Toujours au premier plan / fenêtre normale  (A)", 122, 4, 1 },
    { IDC_CHANS,    L"☰  Chaînes",   L"Afficher / masquer la liste  (L)",      98, 5, 1 },
};

static void LayoutMain(void)
{
    RECT rc; GetClientRect(g_main, &rc);
    int b = ThinB();                       /* liseré de la bordure fine */
    int W = rc.right - 2 * b, H = rc.bottom - 2 * b;
    int showBar = !AutoBar() || g_fsBar;
    int barH = showBar ? S(46) : 0;
    int listW = (C.showlist && !g_full) ? S(240) : 0;
    if (listW > W / 2) listW = W / 2;

    HDWP d = BeginDeferWindowPos(18);
    d = DeferWindowPos(d, g_host, NULL, b, b, W - listW, H - barH, SWP_NOZORDER | SWP_NOACTIVATE);
    d = DeferWindowPos(d, g_list, NULL, b + W - listW, b, listW, H - barH,
                       SWP_NOZORDER | SWP_NOACTIVATE | (listW ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));

    /* Barre : on masque les éléments les moins prioritaires si la place manque. */
    int pad = S(8), gap = S(6), avail = W - 2 * pad, level;
    for (level = 6; level > 0; level--) {
        int tot = 0;
        for (int i = 0; i < COUNT(BAR); i++) if (BAR[i].prio <= level) tot += S(BAR[i].w) + gap;
        if (tot - gap <= avail) break;
    }
    int bh = S(30), by = b + H - barH + (barH - bh) / 2;
    int xl = b + pad, xr = b + W - pad;
    for (int i = 0; i < COUNT(BAR); i++) {
        if (BAR[i].id == IDC_STATUS || BAR[i].right) continue;
        HWND h = GetDlgItem(g_main, BAR[i].id);
        int vis = showBar && BAR[i].prio <= level;
        d = DeferWindowPos(d, h, NULL, xl, by, S(BAR[i].w), bh,
                           SWP_NOZORDER | SWP_NOACTIVATE | (vis ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
        if (vis) xl += S(BAR[i].w) + gap;
    }
    for (int i = 0; i < COUNT(BAR); i++) {
        if (!BAR[i].right) continue;
        HWND h = GetDlgItem(g_main, BAR[i].id);
        int vis = showBar && BAR[i].prio <= level;
        if (vis) xr -= S(BAR[i].w);
        d = DeferWindowPos(d, h, NULL, xr, by, S(BAR[i].w), bh,
                           SWP_NOZORDER | SWP_NOACTIVATE | (vis ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
        if (vis) xr -= gap;
    }
    int sv = showBar && level >= 6;
    d = DeferWindowPos(d, g_status, NULL, xl + S(6), by, xr - xl - S(6), bh,
                       SWP_NOZORDER | SWP_NOACTIVATE | (sv ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    EndDeferWindowPos(d);
    InvalidateRect(g_main, NULL, TRUE);
}

static void UpdateStatus(void)
{
    if (!g_status) return;
    wchar_t t[512];
    if (GetTickCount() < g_flashUntil) { SetWindowTextW(g_status, g_flash); return; }
    if (g_digits[0]) swprintf(t, 512, L"Chaîne n° %ls_", g_digits);
    else if (g_cur >= 0)
        swprintf(t, 512, L"%ls   ·   %ls%ls%ls", g_ch[g_cur].name, g_state, g_size[0] ? L"   ·   " : L"", g_size);
    else wcscpy(t, g_state);
    SetWindowTextW(g_status, t);

    wchar_t title[256];
    if (g_cur >= 0) swprintf(title, 256, L"%ls — %ls", g_ch[g_cur].name, APP_NAME);
    else wcscpy(title, APP_NAME L" " APP_VER);
    SetWindowTextW(g_main, title);
    SetDlgItemTextW(g_main, IDC_PAUSE, g_paused ? L"Lecture" : L"Pause");
    if (g_hud && IsWindowVisible(g_hud)) InvalidateRect(g_hud, NULL, FALSE);
}

static void ApplyTopmost(void)
{
    SetWindowPos(g_main, C.topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    HWND b = GetDlgItem(g_main, IDC_TOPBTN);
    Button_SetCheck(b, C.topmost ? BST_CHECKED : BST_UNCHECKED);
    SetWindowTextW(b, C.topmost ? L"\u25B2  Premier plan" : L"\u25BD  Arrière-plan");
}

static void SetTopmost(int on)
{
    C.topmost = !!on;
    ApplyTopmost();
    SettingsSync();
    Flash(C.topmost ? L"Toujours au premier plan" : L"Fenêtre normale (arrière-plan possible)");
}

static void ApplyMainFonts(void)
{
    if (g_font) DeleteObject(g_font);
    if (g_fontBig) DeleteObject(g_fontBig);
    g_font = MakeFont(g_dpi, 10, FW_NORMAL);
    g_fontBig = MakeFont(g_dpi, 13, FW_NORMAL);
    EnumChildWindows(g_main, SetFontProc, (LPARAM)g_font);
    SendMessageW(g_list, LB_SETITEMHEIGHT, 0, S(28));
}

static BOOL CALLBACK DisableProc(HWND h, LPARAM l) { (void)l; EnableWindow(h, FALSE); return TRUE; }

static void OnVlcEvent(int type, int val)
{
    switch (type) {
    case EV_Opening:   g_playing = 0; g_paused = 0; SetState(L"Ouverture…"); break;
    case EV_Buffering:
        if (!g_playing) { wchar_t t[48]; swprintf(t, 48, L"Mise en mémoire %d %%", val); SetState(t); }
        break;
    case EV_Playing:
        g_playing = 1; g_paused = 0; g_retries = 0;
        ApplyAudio(); ApplyVideoOpts();
        EnumChildWindows(g_host, DisableProc, 0);
        SetState(L"Lecture");
        SetTimer(g_main, TMR_SIZE, 800, NULL);
        break;
    case EV_Vout: {
        EnumChildWindows(g_host, DisableProc, 0);
        unsigned w = 0, h = 0;
        if (p_libvlc_video_get_size(g_mp, 0, &w, &h) == 0 && w) swprintf(g_size, 48, L"%u×%u", w, h);
        ApplyVideoOpts();
        UpdateStatus();
        break; }
    case EV_Paused:  g_paused = 1; SetState(L"En pause"); break;
    case EV_Stopped:
        g_playing = 0; g_paused = 0; g_size[0] = 0;
        SetState(L"Arrêté");
        if (g_userStopped) SetHostMsg(L"Lecture arrêtée.\nEspace ou double-clic sur une chaîne pour reprendre.");
        break;
    case EV_EndReached:
    case EV_Error:
        g_playing = 0; g_paused = 0;
        if (!g_userStopped && g_cur >= 0 && g_retries < 5) {
            wchar_t t[96]; swprintf(t, 96, L"%ls — nouvelle tentative…", type == EV_Error ? L"Erreur de lecture" : L"Flux interrompu");
            SetState(t); SetHostMsg(t);
            SetTimer(g_main, TMR_RETRY, 1500u * (unsigned)(g_retries + 1), NULL);
        } else {
            SetState(type == EV_Error ? L"Erreur de lecture" : L"Fin du flux");
            SetHostMsg(L"Impossible de lire cette chaîne.\nVérifiez le tuner / la playlist, ou augmentez le cache dans ⚙ Paramètres.");
        }
        break;
    }
}

static void OpenSettings(int editCh);

/* ------------------------------------------------------------------ */
/* HUD : incrustation (chaîne, état, heure, volume) au-dessus de l'image */
/* ------------------------------------------------------------------ */
static HFONT g_hudBig, g_hudSmall;
static UINT  g_hudDpi;

static void HudFonts(void)
{
    if (g_hudBig && g_hudDpi == g_dpi) return;
    if (g_hudBig) DeleteObject(g_hudBig);
    if (g_hudSmall) DeleteObject(g_hudSmall);
    g_hudBig = MakeFont(g_dpi, 17, FW_SEMIBOLD);
    g_hudSmall = MakeFont(g_dpi, 10, FW_NORMAL);
    g_hudDpi = g_dpi;
}

static void HideHud(void)
{
    if (!g_hud) return;
    KillTimer(g_main, TMR_HUD);
    ShowWindow(g_hud, SW_HIDE);
}

/* Place le HUD en haut à gauche de la zone vidéo (suit fenêtre, plein écran, bordure fine). */
static void HudPlace(void)
{
    if (!g_hud) return;
    if (!IsWindowVisible(g_main) || IsIconic(g_main) || !IsWindowVisible(g_host)) { ShowWindow(g_hud, SW_HIDE); return; }
    RECT r; GetWindowRect(g_host, &r);
    int m = S(16), w = S(460), hh = S(108);
    if (w > r.right - r.left - 2 * m) w = r.right - r.left - 2 * m;
    if (w < S(180) || hh > r.bottom - r.top - 2 * m) { ShowWindow(g_hud, SW_HIDE); return; }
    SetWindowRgn(g_hud, CreateRoundRectRgn(0, 0, w + 1, hh + 1, S(18), S(18)), FALSE);
    SetWindowPos(g_hud, HWND_TOP, r.left + m, r.top + m, w, hh, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(g_hud, NULL, FALSE);
}

static void ShowHud(void)
{
    if (!g_hud || g_quitting) return;
    if (C.hudSecs <= 0) { HideHud(); return; }
    HudFonts();
    HudPlace();
    SetTimer(g_main, TMR_HUD, (UINT)C.hudSecs * 1000u, NULL);
}

static void HudPaint(HDC dc, int W, int H)
{
    HBRUSH bg = CreateSolidBrush(RGB(16, 17, 22));
    RECT rc = { 0, 0, W, H }; FillRect(dc, &rc, bg); DeleteObject(bg);
    HBRUSH ac = CreateSolidBrush(RGB(0, 120, 215));
    RECT a = { 0, 0, S(6), H }; FillRect(dc, &a, ac); DeleteObject(ac);
    SetBkMode(dc, TRANSPARENT);
    int x = S(22), right = W - S(18), y = S(10);

    /* Ligne 1 : numéro + nom de la chaîne, heure à droite */
    wchar_t num[16] = L"", clk[8];
    const wchar_t *name = L"Aucune chaîne";
    if (g_cur >= 0 && g_cur < g_nch) {
        const Channel *c = &g_ch[g_cur];
        int v = VisPos(g_cur);
        if (c->num[0]) wcscpy(num, c->num); else swprintf(num, 16, L"%d", v + 1);
        name = c->name;
        size_t n = wcslen(num);
        if (!wcsncmp(name, num, n) && name[n] == L' ') { name += n; while (*name == L' ') name++; }
    }
    SYSTEMTIME st; GetLocalTime(&st); swprintf(clk, 8, L"%02u:%02u", st.wHour, st.wMinute);
    SelectObject(dc, g_hudSmall);
    RECT ck = { x, y + S(6), right, y + S(30) };
    SetTextColor(dc, RGB(150, 150, 160));
    DrawTextW(dc, clk, -1, &ck, DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);
    SIZE cs; GetTextExtentPoint32W(dc, clk, (int)wcslen(clk), &cs);
    SelectObject(dc, g_hudBig);
    RECT t = { x, y, right - cs.cx - S(12), y + S(34) };
    if (num[0]) {
        SetTextColor(dc, RGB(64, 170, 255));
        DrawTextW(dc, num, -1, &t, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
        SIZE ns; GetTextExtentPoint32W(dc, num, (int)wcslen(num), &ns);
        t.left += ns.cx + S(14);
    }
    SetTextColor(dc, RGB(245, 245, 250));
    DrawTextW(dc, name, -1, &t, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

    /* Ligne 2 : message éphémère, sinon état · résolution */
    y += S(38);
    wchar_t l2[160];
    if (GetTickCount() < g_flashUntil) wcsncpy(l2, g_flash, 159), l2[159] = 0;
    else swprintf(l2, 160, L"%ls%ls%ls", g_state, g_size[0] ? L"   ·   " : L"", g_size);
    SelectObject(dc, g_hudSmall);
    SetTextColor(dc, RGB(190, 190, 200));
    RECT t2 = { x, y, right, y + S(22) };
    DrawTextW(dc, l2, -1, &t2, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

    /* Ligne 3 : volume (barre 0-150 %, repère à 100 %) */
    y += S(28);
    const wchar_t *lbl = C.muted ? L"Muet" : L"Volume";
    wchar_t pct[16]; swprintf(pct, 16, L"%d %%", C.volume);
    SetTextColor(dc, C.muted ? RGB(255, 95, 95) : RGB(190, 190, 200));
    RECT l3 = { x, y - S(2), x + S(70), y + S(18) };
    DrawTextW(dc, lbl, -1, &l3, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
    RECT p3 = { right - S(60), y - S(2), right, y + S(18) };
    DrawTextW(dc, pct, -1, &p3, DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);
    int bx0 = x + S(72), bx1 = right - S(68), by = y + S(4), bh = S(8);
    if (bx1 > bx0) {
        HBRUSH tr = CreateSolidBrush(RGB(58, 60, 70));
        RECT bar = { bx0, by, bx1, by + bh }; FillRect(dc, &bar, tr); DeleteObject(tr);
        int fill = bx0 + (bx1 - bx0) * C.volume / 150;
        HBRUSH fb = CreateSolidBrush(C.muted ? RGB(110, 110, 118) : (C.volume > 100 ? RGB(255, 170, 40) : RGB(0, 140, 255)));
        RECT f = { bx0, by, fill, by + bh }; FillRect(dc, &f, fb); DeleteObject(fb);
        int mark = bx0 + (bx1 - bx0) * 100 / 150;
        HBRUSH mb = CreateSolidBrush(RGB(220, 220, 230));
        RECT mk = { mark, by - S(3), mark + S(2), by + bh + S(3) }; FillRect(dc, &mk, mb); DeleteObject(mb);
    }
}

static LRESULT CALLBACK HudProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    switch (msg) {
    case WM_NCHITTEST: return HTTRANSPARENT;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        HDC md = CreateCompatibleDC(dc);
        HBITMAP bm = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        HGDIOBJ ob = SelectObject(md, bm);
        HGDIOBJ of = SelectObject(md, g_hudSmall);
        HudPaint(md, rc.right, rc.bottom);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, md, 0, 0, SRCCOPY);
        SelectObject(md, of); SelectObject(md, ob);
        DeleteObject(bm); DeleteDC(md);
        EndPaint(h, &ps);
        return 0; }
    }
    return DefWindowProcW(h, msg, w, l);
}

static void ShowVideoMenu(int x, int y)
{
    HMENU m = CreatePopupMenu(), a = CreatePopupMenu(), dm = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | (g_paused ? MF_CHECKED : 0), IDC_PAUSE, L"Pause\tEspace");
    AppendMenuW(m, MF_STRING, IDC_STOP, L"Stop\tS");
    AppendMenuW(m, MF_STRING | (C.muted ? MF_CHECKED : 0), IDC_MUTE, L"Muet\tM");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING | (g_full ? MF_CHECKED : 0), IDC_FULL, L"Plein écran\tF");
    AppendMenuW(m, MF_STRING | (C.showlist ? MF_CHECKED : 0), IDC_CHANS, L"Liste des chaînes\tL");
    for (int i = 0; i < COUNT(ASPECT_OPTS); i++)
        AppendMenuW(a, MF_STRING | (C.aspect == i ? MF_CHECKED : 0), IDM_ASPECT0 + i, ASPECT_OPTS[i].label);
    for (int i = 0; i < COUNT(DEINT_OPTS); i++)
        AppendMenuW(dm, MF_STRING | (C.deint == i ? MF_CHECKED : 0), IDM_DEINT0 + i, DEINT_OPTS[i].label);
    AppendMenuW(m, MF_POPUP, (UINT_PTR)a, L"Format d'image");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)dm, L"Désentrelacement");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING | (C.thin ? MF_CHECKED : 0), IDM_THIN, L"Bordure fine\tB");
    {
        HMENU sz = CreatePopupMenu();
        SizePreset ps[16]; int n = ParseSizes(ps, 16);
        for (int i = 0; i < n; i++) {
            wchar_t t[64]; swprintf(t, 64, L"%ls\t%d\u00D7%d", ps[i].label, ps[i].w, ps[i].h);
            AppendMenuW(sz, MF_STRING | (C.sizeIdx == i ? MF_CHECKED : 0), IDM_SIZE0 + i, t);
        }
        if (n) AppendMenuW(sz, MF_SEPARATOR, 0, NULL);
        AppendMenuW(sz, MF_STRING, IDM_SIZEDEF, L"Définir les tailles…");
        AppendMenuW(m, MF_POPUP, (UINT_PTR)sz, L"Taille de l'image");
    }
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, IDC_SETTINGS, L"⚙  Paramètres…\tP");
    if (C.thin && !g_full) {
        AppendMenuW(m, MF_SEPARATOR, 0, NULL);
        AppendMenuW(m, MF_STRING, IDM_MINIMIZE, L"Réduire");
        AppendMenuW(m, MF_STRING, IDM_QUIT, L"Quitter\tAlt+F4");
    }
    TrackPopupMenu(m, TPM_RIGHTBUTTON, x, y, 0, g_main, NULL);
    DestroyMenu(m);
}

static POINT g_dragPt; static int g_dragArm;

/* En bordure fine, les 6 px du bord appartiennent à la fenêtre principale (poignées). */
static int NearMainEdge(LPARAM l)
{
    if (!ThinFrame()) return 0;
    POINT p = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };
    RECT r; GetWindowRect(g_main, &r);
    int g = S(6);
    return p.x < r.left + g || p.x >= r.right - g || p.y < r.top + g || p.y >= r.bottom - g;
}

static LRESULT CALLBACK ListSubProc(HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR ref)
{
    (void)id; (void)ref;
    if (msg == WM_NCHITTEST && NearMainEdge(l)) return HTTRANSPARENT;
    return DefSubclassProc(h, msg, w, l);
}

static LRESULT CALLBACK HostProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        FillRect(dc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
        if (!g_playing) {
            SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(165, 165, 170));
            HGDIOBJ of = SelectObject(dc, g_fontBig);
            RECT t = rc; InflateRect(&t, -S(24), 0);
            RECT c = t; DrawTextW(dc, g_hostMsg, -1, &c, DT_CENTER | DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
            t.top = (rc.bottom - (c.bottom - c.top)) / 2; t.bottom = t.top + (c.bottom - c.top);
            DrawTextW(dc, g_hostMsg, -1, &t, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
            SelectObject(dc, of);
        }
        EndPaint(h, &ps);
        return 0; }
    case WM_NCHITTEST:
        if (NearMainEdge(l)) return HTTRANSPARENT;   /* redimensionnement par la fenêtre principale */
        break;
    case WM_LBUTTONDOWN:
        SetFocus(g_main);
        if (ThinFrame()) { g_dragPt.x = GET_X_LPARAM(l); g_dragPt.y = GET_Y_LPARAM(l); g_dragArm = 1; SetCapture(h); }
        return 0;
    case WM_MOUSEMOVE:
        if (g_dragArm && (w & MK_LBUTTON)) {
            int dx = GET_X_LPARAM(l) - g_dragPt.x, dy = GET_Y_LPARAM(l) - g_dragPt.y;
            if (abs(dx) > GetSystemMetrics(SM_CXDRAG) || abs(dy) > GetSystemMetrics(SM_CYDRAG)) {
                g_dragArm = 0; ReleaseCapture();
                SendMessageW(g_main, WM_SYSCOMMAND, SC_MOVE | HTCAPTION, 0);   /* glisser = déplacer */
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (g_dragArm) { g_dragArm = 0; ReleaseCapture(); }
        return 0;
    case WM_LBUTTONDBLCLK: SetFullscreen(!g_full); return 0;
    case WM_RBUTTONUP: { POINT p = { GET_X_LPARAM(l), GET_Y_LPARAM(l) }; ClientToScreen(h, &p); ShowVideoMenu(p.x, p.y); return 0; }
    case WM_MOUSEWHEEL: SetVolume(C.volume + (GET_WHEEL_DELTA_WPARAM(w) > 0 ? 5 : -5)); return 0;
    case WM_SIZE: InvalidateRect(h, NULL, FALSE); return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}

static void CommitDigits(void)
{
    KillTimer(g_main, TMR_DIGITS);
    if (!g_digits[0]) return;
    int target = -1;
    for (int v = 0; v < g_nvis; v++) if (!wcscmp(g_ch[g_vis[v]].num, g_digits)) { target = g_vis[v]; break; }
    if (target < 0) { int n = _wtoi(g_digits); if (n >= 1 && n <= g_nvis) target = g_vis[n - 1]; }
    g_digits[0] = 0;
    if (target >= 0) PlayChannel(target); else UpdateStatus();
}

static int HandleKey(WPARAM vk)
{
    if (GetKeyState(VK_CONTROL) < 0 || GetKeyState(VK_MENU) < 0) return 0;
    if ((vk >= '0' && vk <= '9') || (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9)) {
        size_t n = wcslen(g_digits);
        if (n < 5) { g_digits[n] = (wchar_t)(L'0' + (vk >= VK_NUMPAD0 ? vk - VK_NUMPAD0 : vk - '0')); g_digits[n+1] = 0; }
        SetTimer(g_main, TMR_DIGITS, 1300, NULL);
        UpdateStatus();
        return 1;
    }
    switch (vk) {
    case VK_LEFT: case VK_PRIOR:  Zap(-1); return 1;
    case VK_RIGHT: case VK_NEXT:  Zap(1);  return 1;
    case VK_UP:    SetVolume(C.volume + 5); return 1;
    case VK_DOWN:  SetVolume(C.volume - 5); return 1;
    case VK_SPACE: TogglePause(); return 1;
    case VK_RETURN: if (g_digits[0]) { CommitDigits(); return 1; } return 0;
    case 'F': case VK_F11: SetFullscreen(!g_full); return 1;
    case VK_ESCAPE: if (g_full) { SetFullscreen(0); return 1; } return 0;
    case 'M': ToggleMute(); return 1;
    case 'S': StopPlayback(); return 1;
    case 'L': C.showlist = !C.showlist; LayoutMain(); SettingsSync(); return 1;
    case 'P': case VK_F2: OpenSettings(-1); return 1;
    case 'B': SetThin(!C.thin); return 1;
    case 'A': SetTopmost(!C.topmost); return 1;
    case 'I': ShowHud(); return 1;
    case 'T': { SizePreset ps[16]; int n = ParseSizes(ps, 16); if (n) ApplySizePreset((C.sizeIdx + 1) % n); return 1; }
    }
    return 0;
}

static LRESULT CALLBACK MainProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    switch (msg) {
    case WM_CREATE: {
        g_main = h;
        g_dpi = GetDpiForWindow(h);
        g_host = CreateWindowExW(0, L"VlcMasterVideo", NULL, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                                 0, 0, 10, 10, h, (HMENU)(INT_PTR)IDC_HOST, g_hi, NULL);
        g_list = Mk(h, WC_LISTBOXW, NULL, LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP, 0, IDC_LIST);
        SetWindowTheme(g_list, L"Explorer", NULL);
        SetWindowSubclass(g_list, ListSubProc, 1, 0);
        g_tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                                0, 0, 0, 0, h, NULL, g_hi, NULL);
        for (int i = 0; i < COUNT(BAR); i++) {
            HWND c;
            if (BAR[i].id == IDC_VOL) {
                c = g_vol = Mk(h, TRACKBAR_CLASSW, NULL, TBS_HORZ | TBS_NOTICKS | TBS_BOTH | WS_TABSTOP, 0, IDC_VOL);
                SendMessageW(c, TBM_SETRANGE, TRUE, MAKELPARAM(0, 150));
                SendMessageW(c, TBM_SETPAGESIZE, 0, 10);
            } else if (BAR[i].id == IDC_STATUS) {
                c = g_status = Mk(h, WC_STATICW, L"", SS_LEFTNOWORDWRAP | SS_CENTERIMAGE | SS_ENDELLIPSIS | SS_NOPREFIX, 0, IDC_STATUS);
            } else {
                c = Mk(h, WC_BUTTONW, BAR[i].txt, (BAR[i].id == IDC_SIZEBTN ? BS_SPLITBUTTON : BAR[i].id == IDC_TOPBTN ? (BS_AUTOCHECKBOX | BS_PUSHLIKE) : BS_PUSHBUTTON) | WS_TABSTOP, 0, BAR[i].id);
            }
            if (BAR[i].tip) {
                TTTOOLINFOW ti; memset(&ti, 0, sizeof ti);
                ti.cbSize = sizeof ti; ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
                ti.hwnd = h; ti.uId = (UINT_PTR)c; ti.lpszText = (LPWSTR)BAR[i].tip;
                SendMessageW(g_tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
            }
        }
        ApplyMainFonts();
        SetHostMsg(L"Chargement…");
        ApplyAudio();
        return 0; }

    case WM_SIZE:
        if (w != SIZE_MINIMIZED) LayoutMain();
        if (g_hud && IsWindowVisible(g_hud)) HudPlace();
        if (w == SIZE_MINIMIZED) HideHud();
        return 0;
    case WM_MOVE:
        if (g_hud && IsWindowVisible(g_hud)) HudPlace();
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)l;
        mm->ptMinTrackSize.x = S(200); mm->ptMinTrackSize.y = S(120);
        return 0; }

    case WM_ERASEBKGND: {
        HDC dc = (HDC)w; RECT rc; GetClientRect(h, &rc);
        FillRect(dc, &rc, GetSysColorBrush(COLOR_BTNFACE));
        int b = ThinB();
        if (b) {
            HBRUSH br = CreateSolidBrush(GetActiveWindow() == h ? RGB(0, 103, 192) : RGB(90, 90, 96));
            RECT e;
            SetRect(&e, 0, 0, rc.right, b);                 FillRect(dc, &e, br);
            SetRect(&e, 0, rc.bottom - b, rc.right, rc.bottom); FillRect(dc, &e, br);
            SetRect(&e, 0, 0, b, rc.bottom);                FillRect(dc, &e, br);
            SetRect(&e, rc.right - b, 0, rc.right, rc.bottom);  FillRect(dc, &e, br);
            DeleteObject(br);
        }
        return 1; }

    case WM_ACTIVATE:
        if (ThinFrame()) InvalidateRect(h, NULL, TRUE);   /* couleur du liseré */
        break;

    case WM_NCHITTEST:
        if (ThinFrame()) {
            POINT p = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };
            RECT r; GetWindowRect(h, &r);
            int g = S(6), c = S(16);
            int L = p.x < r.left + g, R = p.x >= r.right - g, T = p.y < r.top + g, B = p.y >= r.bottom - g;
            int Lc = p.x < r.left + c, Rc = p.x >= r.right - c, Tc = p.y < r.top + c, Bc = p.y >= r.bottom - c;
            if ((T && Lc) || (L && Tc)) return HTTOPLEFT;
            if ((T && Rc) || (R && Tc)) return HTTOPRIGHT;
            if ((B && Lc) || (L && Bc)) return HTBOTTOMLEFT;
            if ((B && Rc) || (R && Bc)) return HTBOTTOMRIGHT;
            if (L) return HTLEFT;
            if (R) return HTRIGHT;
            if (T) return HTTOP;
            if (B) return HTBOTTOM;
            return HTCAPTION;          /* fond de la barre : déplacer la fenêtre */
        }
        break;

    case WM_NCLBUTTONDBLCLK:
        if (ThinFrame() && w == HTCAPTION) { SetFullscreen(1); return 0; }
        break;

    case WM_ENTERSIZEMOVE: GetWindowRect(h, &g_sizeMoveRc); return 0;
    case WM_EXITSIZEMOVE: {
        RECT r; GetWindowRect(h, &r);
        if (r.right - r.left != g_sizeMoveRc.right - g_sizeMoveRc.left
            || r.bottom - r.top != g_sizeMoveRc.bottom - g_sizeMoveRc.top) {
            C.sizeIdx = -1; UpdateSizeButton();       /* taille libre */
        }
        return 0; }

    case WM_NOTIFY: {
        NMHDR *nh = (NMHDR *)l;
        if (nh->idFrom == IDC_SIZEBTN && nh->code == BCN_DROPDOWN) {
            RECT r; GetWindowRect(nh->hwndFrom, &r);
            ShowSizeMenu(r.right, r.top, TPM_RIGHTALIGN | TPM_BOTTOMALIGN);
            return 0;
        }
        break; }

    case WM_DPICHANGED: {
        g_dpi = HIWORD(w);
        ApplyMainFonts();
        RECT *r = (RECT *)l;
        SetWindowPos(h, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        LayoutMain();
        return 0; }

    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == IDC_LIST) {
            if (HIWORD(w) == LBN_SELCHANGE) {
                int s = (int)SendMessageW(g_list, LB_GETCURSEL, 0, 0);
                if (s >= 0 && s < g_nvis && g_vis[s] != g_cur) PlayChannel(g_vis[s]);
            }
            return 0;
        }
        if (id >= IDM_ASPECT0 && id < IDM_ASPECT0 + COUNT(ASPECT_OPTS)) {
            C.aspect = id - IDM_ASPECT0; ApplyVideoOpts(); SettingsSync(); Flash(ASPECT_OPTS[C.aspect].label); return 0;
        }
        if (id >= IDM_SIZE0 && id < IDM_SIZE0 + 16) { ApplySizePreset(id - IDM_SIZE0); return 0; }
        if (id >= IDM_DEINT0 && id < IDM_DEINT0 + COUNT(DEINT_OPTS)) {
            C.deint = id - IDM_DEINT0; ApplyVideoOpts(); SettingsSync(); return 0;
        }
        switch (id) {
        case IDC_PREV: Zap(-1); break;
        case IDC_NEXT: Zap(1); break;
        case IDC_PAUSE: TogglePause(); break;
        case IDC_STOP: StopPlayback(); break;
        case IDC_MUTE: ToggleMute(); break;
        case IDC_FULL: SetFullscreen(!g_full); break;
        case IDC_CHANS: C.showlist = !C.showlist; LayoutMain(); SettingsSync(); break;
        case IDC_SETTINGS: OpenSettings(-1); break;
        case IDC_SIZEBTN: { SizePreset ps[16]; int n = ParseSizes(ps, 16);
                         if (n) ApplySizePreset((C.sizeIdx + 1) % n); else OpenSettings(-2); break; }
        case IDM_SIZEDEF: OpenSettings(-2); break;
        case IDM_THIN: SetThin(!C.thin); break;
        case IDC_TOPBTN: SetTopmost(Button_GetCheck(GetDlgItem(h, IDC_TOPBTN)) == BST_CHECKED); break;
        case IDM_MINIMIZE: ShowWindow(h, SW_MINIMIZE); break;
        case IDM_QUIT: PostMessageW(h, WM_CLOSE, 0, 0); break;
        case IDM_PLAY: PlayChannel(g_menuCh); break;
        case IDM_RENAME: OpenSettings(g_menuCh); break;
        case IDM_HIDE:
            if (g_menuCh >= 0 && g_menuCh < g_nch) { g_ch[g_menuCh].hidden = 1; ChannelsChanged(1); if (g_set) FillLV(); }
            break;
        }
        return 0; }

    case WM_MEASUREITEM:
        if (w == IDC_LIST) { ((MEASUREITEMSTRUCT *)l)->itemHeight = (UINT)S(28); return TRUE; }
        break;
    case WM_DRAWITEM:
        if (w == IDC_LIST) { DrawChannelItem((DRAWITEMSTRUCT *)l); return TRUE; }
        break;

    case WM_CONTEXTMENU:
        if ((HWND)w == g_list) {
            POINT p = { GET_X_LPARAM(l), GET_Y_LPARAM(l) }, c = p;
            ScreenToClient(g_list, &c);
            DWORD r = (DWORD)SendMessageW(g_list, LB_ITEMFROMPOINT, 0, MAKELPARAM(c.x, c.y));
            if (HIWORD(r) || LOWORD(r) >= (DWORD)g_nvis) return 0;
            g_menuCh = g_vis[LOWORD(r)];
            HMENU m = CreatePopupMenu();
            AppendMenuW(m, MF_STRING | MF_DEFAULT, IDM_PLAY, L"Regarder");
            AppendMenuW(m, MF_STRING, IDM_RENAME, L"Renommer…");
            AppendMenuW(m, MF_STRING, IDM_HIDE, L"Masquer de la liste");
            TrackPopupMenu(m, TPM_RIGHTBUTTON, p.x, p.y, 0, h, NULL);
            DestroyMenu(m);
            return 0;
        }
        break;

    case WM_HSCROLL:
        if ((HWND)l == g_vol) {
            int v = (int)SendMessageW(g_vol, TBM_GETPOS, 0, 0);
            if (v != C.volume || C.muted) SetVolume(v);
        }
        return 0;

    case WM_TIMER:
        switch (w) {
        case TMR_FS: FsTick(); break;
        case TMR_DIGITS: CommitDigits(); break;
        case TMR_RETRY: KillTimer(h, TMR_RETRY); g_retries++; RequestPlayCur(); break;
        case TMR_FLASH: KillTimer(h, TMR_FLASH); UpdateStatus(); break;
        case TMR_HUD: HideHud(); break;
        case TMR_SIZE: {
            static int tries;
            unsigned vw = 0, vh = 0;
            if (g_vlcState == 1 && g_playing && p_libvlc_video_get_size(g_mp, 0, &vw, &vh) == 0 && vw) {
                swprintf(g_size, 48, L"%u×%u", vw, vh); UpdateStatus();
                KillTimer(h, TMR_SIZE); tries = 0;
            } else if (!g_playing || ++tries > 15) { KillTimer(h, TMR_SIZE); tries = 0; }
            break; }
        case TMR_FORCEQUIT: KillTimer(h, TMR_FORCEQUIT); DestroyWindow(h); break;
        }
        return 0;

    case WM_APP_VLCREADY:
        if (w) {
            g_vlcState = 1;
            ApplyAudio(); ApplyVideoOpts();
            if (g_quitting) break;
            if (g_cur >= 0) PlayChannel(g_cur);
            else if (!g_nch) SetHostMsg(g_hostMsg);
        } else {
            g_vlcState = -1; g_workerAlive = 0;
            if (g_quitting) { DestroyWindow(h); return 0; }
            SetHostMsg(g_vlcInfo);
            SetState(L"VLC indisponible");
        }
        SettingsSync();
        return 0;

    case WM_APP_VLCEVENT: OnVlcEvent((int)w, (int)l); return 0;
    case WM_APP_PLAYLIST: OnPlaylist(w, (PlResult *)l); return 0;
    case WM_APP_WORKERDONE:
        g_workerAlive = 0;
        if (g_quitting) DestroyWindow(h);
        return 0;

    case WM_CLOSE: {
        WINDOWPLACEMENT wp = { sizeof wp };
        if (g_full) wp = g_wpPrev; else GetWindowPlacement(h, &wp);
        C.mainrc = wp.rcNormalPosition; C.mainmax = (wp.showCmd == SW_SHOWMAXIMIZED);
        if (g_set) DestroyWindow(g_set);
        SaveConfig();
        g_quitting = 1;
        HideHud();
        ShowWindow(h, SW_HIDE);
        if (g_workerAlive) {
            EnterCriticalSection(&g_cs); g_req.quit = 1; LeaveCriticalSection(&g_cs);
            SetEvent(g_wake);
            SetTimer(h, TMR_FORCEQUIT, 5000, NULL);
        } else DestroyWindow(h);
        return 0; }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}

/* ------------------------------------------------------------------ */
/* Fenêtre Paramètres (redimensionnable, mise en page adaptative)     */
/* ------------------------------------------------------------------ */
static int LayoutSettings(int W, int H, BOOL apply)
{
    int m = SS(16), g = SS(8), rh = SS(26), lh = SS(19), hh = SS(24), sp = SS(6);
    int wide = W >= SS(900);
    int lw = wide ? SS(430) : W - 2 * m;
    int x = m, y = m;
    HDWP d = apply ? BeginDeferWindowPos(64) : NULL;
#define P(hw, X, Y, WW, HH) do { if (apply) d = DeferWindowPos(d, (hw), NULL, (X), (Y), (WW), (HH), SWP_NOZORDER | SWP_NOACTIVATE); } while (0)
#define HDR(i, X, Y, WW) do { P(U.hdr[i], X, Y, WW, hh); P(U.line[i], X, (Y) + hh, WW, 2); } while (0)
    int labw = SS(160), cw = lw - labw - g;

    HDR(0, x, y, lw); y += hh + SS(10);
    P(U.lblUrl, x, y, lw, lh); y += lh + SS(4);
    { int bw = SS(34), rw = SS(100);
      P(U.url, x, y, lw - bw - rw - 2 * sp, rh);
      P(U.urlBr, x + lw - bw - rw - sp, y, bw, rh);
      P(U.reload, x + lw - rw, y, rw, rh); }
    y += rh + SS(18);

    HDR(1, x, y, lw); y += hh + SS(10);
#define ROW(lbl, ctl, cwid, combo) do { P(lbl, x, y + (rh - lh) / 2, labw, lh); \
        P(ctl, x + labw + g, y, (cwid), (combo) ? rh + SS(260) : rh); y += rh + sp; } while (0)
    ROW(U.lblCache, U.cache, SS(110), 0);
    ROW(U.lblHw, U.hw, cw, 1);
    ROW(U.lblDeint, U.deint, cw, 1);
    ROW(U.lblAsp, U.aspect, cw, 1);
    ROW(U.lblDelay, U.delay, SS(110), 0);
#undef ROW
    y += SS(12);

    HDR(2, x, y, lw); y += hh + SS(8);
    P(U.topmost, x, y, lw, rh);  y += rh;
    P(U.resume, x, y, lw, rh);   y += rh;
    P(U.autoplay, x, y, lw, rh); y += rh;
    P(U.showlist, x, y, lw, rh); y += rh;
    P(U.thin, x, y, lw, rh);     y += rh;
    P(U.thinbar, x, y, lw, rh);  y += rh + SS(4);
    P(U.lblSizes, x, y + (rh - lh) / 2, labw + SS(70), lh);
    P(U.sizes, x + labw + SS(70) + g, y, lw - labw - SS(70) - g, rh); y += rh + SS(6);
    P(U.lblHud, x, y + (rh - lh) / 2, labw + SS(70), lh);
    P(U.hud, x + labw + SS(70) + g, y, SS(90), rh); y += rh + SS(14);

    HDR(3, x, y, lw); y += hh + SS(10);
    P(U.lblVlc, x, y, lw, lh); y += lh + SS(4);
    P(U.vlcdir, x, y, lw - SS(34) - sp, rh);
    P(U.vlcBr, x + lw - SS(34), y, SS(34), rh); y += rh + SS(6);
    P(U.vlcInfo, x, y, lw, lh * 2); y += lh * 2;
    int leftBottom = y;

    int cx, cy, cwd;
    if (wide) { cx = x + lw + SS(32); cy = m; cwd = W - cx - m; }
    else      { cx = m; cy = y + SS(16); cwd = W - 2 * m; }
    HDR(4, cx, cy, cwd); cy += hh + SS(10);
    int closeY = H - m - rh;
    int btnY = closeY - SS(14) - rh;
    int lvH = btnY - g - cy; if (lvH < SS(60)) lvH = SS(60);
    P(U.lv, cx, cy, cwd, lvH);
    { HWND b[5] = { U.up, U.down, U.rename, U.orig, U.showall };
      int bw = (cwd - 4 * sp) / 5;
      for (int i = 0; i < 5; i++) P(b[i], cx + i * (bw + sp), btnY, bw, rh); }
    P(U.close, W - m - SS(120), closeY, SS(120), rh);
    P(U.hint, m, closeY + (rh - lh) / 2, W - 2 * m - SS(130), lh);

    if (apply) {
        EndDeferWindowPos(d);
        SendMessageW(U.cacheSp, UDM_SETBUDDY, (WPARAM)U.cache, 0);
        SendMessageW(U.delaySp, UDM_SETBUDDY, (WPARAM)U.delay, 0);
        SendMessageW(U.hudSp, UDM_SETBUDDY, (WPARAM)U.hud, 0);
        ListView_SetColumnWidth(U.lv, 2, LVSCW_AUTOSIZE_USEHEADER);
    }
#undef P
#undef HDR
    int listMin = hh + SS(10) + SS(140) + g + rh + SS(14) + rh + m;
    if (wide) return (leftBottom + SS(14) + rh + m > m + listMin) ? leftBottom + SS(14) + rh + m : m + listMin;
    return leftBottom + SS(16) + listMin;
}

static void FillLV(void)
{
    if (!g_set) return;
    g_lvFilling = 1;
    int sel = ListView_GetNextItem(U.lv, -1, LVNI_SELECTED);
    SendMessageW(U.lv, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(U.lv);
    for (int i = 0; i < g_nch; i++) {
        LVITEMW it; memset(&it, 0, sizeof it);
        it.mask = LVIF_TEXT; it.iItem = i; it.pszText = g_ch[i].name;
        ListView_InsertItem(U.lv, &it);
        wchar_t n[16]; if (g_ch[i].num[0]) wcscpy(n, g_ch[i].num); else swprintf(n, 16, L"%d", i + 1);
        ListView_SetItemText(U.lv, i, 1, n);
        wchar_t u[1024]; Utf8ToW(g_ch[i].url, u, 1024);
        ListView_SetItemText(U.lv, i, 2, u);
        ListView_SetCheckState(U.lv, i, !g_ch[i].hidden);
    }
    if (sel >= 0 && sel < g_nch) ListView_SetItemState(U.lv, sel, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    SendMessageW(U.lv, WM_SETREDRAW, TRUE, 0);
    g_lvFilling = 0;
    wchar_t t[96]; swprintf(t, 96, L"Chaînes  (%d, dont %d visibles)", g_nch, g_nvis);
    SetWindowTextW(U.hdr[4], t);
}

static void SettingsSync(void)
{
    if (!g_set) return;
    g_setInit = 1;
    wchar_t cur[2048]; GetWindowTextW(U.url, cur, 2048);
    if (wcscmp(cur, C.playlist)) SetWindowTextW(U.url, C.playlist);
    SendMessageW(U.cacheSp, UDM_SETPOS32, 0, C.caching);
    SendMessageW(U.delaySp, UDM_SETPOS32, 0, C.delay);
    SendMessageW(U.hw, CB_SETCURSEL, (WPARAM)C.hw, 0);
    SendMessageW(U.deint, CB_SETCURSEL, (WPARAM)C.deint, 0);
    SendMessageW(U.aspect, CB_SETCURSEL, (WPARAM)C.aspect, 0);
    Button_SetCheck(U.topmost, C.topmost ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(U.resume, C.resume ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(U.autoplay, C.autoplay ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(U.showlist, C.showlist ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(U.thin, C.thin ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(U.thinbar, C.thinHideBar ? BST_CHECKED : BST_UNCHECKED);
    EnableWindow(U.thinbar, C.thin);
    SendMessageW(U.hudSp, UDM_SETPOS32, 0, C.hudSecs);
    GetWindowTextW(U.sizes, cur, 256);
    if (wcscmp(cur, C.sizes)) SetWindowTextW(U.sizes, C.sizes);
    GetWindowTextW(U.vlcdir, cur, MAX_PATH);
    if (wcscmp(cur, C.vlcdir)) SetWindowTextW(U.vlcdir, C.vlcdir);
    SetWindowTextW(U.vlcInfo, g_vlcInfo);
    g_setInit = 0;
}

static void ApplySettingsFonts(void)
{
    if (g_sfont) DeleteObject(g_sfont);
    if (g_sfontBold) DeleteObject(g_sfontBold);
    g_sfont = MakeFont(g_sdpi, 10, FW_NORMAL);
    g_sfontBold = MakeFont(g_sdpi, 11, FW_SEMIBOLD);
    EnumChildWindows(g_set, SetFontProc, (LPARAM)g_sfont);
    for (int i = 0; i < 5; i++) SendMessageW(U.hdr[i], WM_SETFONT, (WPARAM)g_sfontBold, TRUE);
    ListView_SetColumnWidth(U.lv, 0, SS(220));
    ListView_SetColumnWidth(U.lv, 1, SS(56));
}

static void CreateSettingsControls(HWND h)
{
    static const wchar_t *hdrs[5] = { L"Source", L"Lecture", L"Interface", L"Moteur VLC", L"Chaînes" };
    for (int i = 0; i < 5; i++) {
        U.hdr[i]  = Mk(h, WC_STATICW, hdrs[i], SS_LEFT | SS_NOPREFIX, 0, -1);
        U.line[i] = Mk(h, WC_STATICW, L"", SS_ETCHEDHORZ, 0, -1);
    }
    U.lblUrl = Mk(h, WC_STATICW, L"Playlist M3U, flux ou fichier (ex. http://192.168.1.90/lineup.m3u) :", SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS, 0, -1);
    U.url    = Mk(h, WC_EDITW, L"", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, IDS_URL);
    U.urlBr  = Mk(h, WC_BUTTONW, L"…", BS_PUSHBUTTON | WS_TABSTOP, 0, IDS_URLBROWSE);
    U.reload = Mk(h, WC_BUTTONW, L"Recharger", BS_PUSHBUTTON | WS_TABSTOP, 0, IDS_RELOAD);

    U.lblCache = Mk(h, WC_STATICW, L"Cache réseau (ms)", SS_LEFT, 0, -1);
    U.cache    = Mk(h, WC_EDITW, L"", ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, IDS_CACHE);
    U.cacheSp  = Mk(h, UPDOWN_CLASSW, NULL, UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_NOTHOUSANDS, 0, IDS_CACHESPIN);
    SendMessageW(U.cacheSp, UDM_SETBUDDY, (WPARAM)U.cache, 0);
    SendMessageW(U.cacheSp, UDM_SETRANGE32, 0, 20000);
    { UDACCEL ac[2] = { { 0, 100 }, { 2, 500 } }; SendMessageW(U.cacheSp, UDM_SETACCEL, 2, (LPARAM)ac); }

    U.lblHw = Mk(h, WC_STATICW, L"Décodage matériel", SS_LEFT, 0, -1);
    U.hw    = Mk(h, WC_COMBOBOXW, NULL, CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, IDS_HW);
    for (int i = 0; i < COUNT(HW_OPTS); i++) SendMessageW(U.hw, CB_ADDSTRING, 0, (LPARAM)HW_OPTS[i].label);
    U.lblDeint = Mk(h, WC_STATICW, L"Désentrelacement", SS_LEFT, 0, -1);
    U.deint    = Mk(h, WC_COMBOBOXW, NULL, CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, IDS_DEINT);
    for (int i = 0; i < COUNT(DEINT_OPTS); i++) SendMessageW(U.deint, CB_ADDSTRING, 0, (LPARAM)DEINT_OPTS[i].label);
    U.lblAsp = Mk(h, WC_STATICW, L"Format d'image", SS_LEFT, 0, -1);
    U.aspect = Mk(h, WC_COMBOBOXW, NULL, CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, IDS_ASPECT);
    for (int i = 0; i < COUNT(ASPECT_OPTS); i++) SendMessageW(U.aspect, CB_ADDSTRING, 0, (LPARAM)ASPECT_OPTS[i].label);
    U.lblDelay = Mk(h, WC_STATICW, L"Synchro audio (ms)", SS_LEFT, 0, -1);
    U.delay    = Mk(h, WC_EDITW, L"", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, IDS_DELAY);
    U.delaySp  = Mk(h, UPDOWN_CLASSW, NULL, UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_NOTHOUSANDS, 0, IDS_DELAYSPIN);
    SendMessageW(U.delaySp, UDM_SETBUDDY, (WPARAM)U.delay, 0);
    SendMessageW(U.delaySp, UDM_SETRANGE32, (WPARAM)-10000, 10000);
    { UDACCEL ac[2] = { { 0, 50 }, { 2, 250 } }; SendMessageW(U.delaySp, UDM_SETACCEL, 2, (LPARAM)ac); }

    U.topmost  = Mk(h, WC_BUTTONW, L"Toujours au premier plan", BS_AUTOCHECKBOX | WS_TABSTOP, 0, IDS_TOPMOST);
    U.resume   = Mk(h, WC_BUTTONW, L"Reprendre la dernière chaîne au démarrage", BS_AUTOCHECKBOX | WS_TABSTOP, 0, IDS_RESUME);
    U.autoplay = Mk(h, WC_BUTTONW, L"Lancer la lecture automatiquement", BS_AUTOCHECKBOX | WS_TABSTOP, 0, IDS_AUTOPLAY);
    U.showlist = Mk(h, WC_BUTTONW, L"Afficher la liste des chaînes à côté de l'image", BS_AUTOCHECKBOX | WS_TABSTOP, 0, IDS_SHOWLIST);
    U.thin     = Mk(h, WC_BUTTONW, L"Bordure fine : fenêtre sans barre de titre  (B)", BS_AUTOCHECKBOX | WS_TABSTOP, 0, IDS_THIN);
    U.thinbar  = Mk(h, WC_BUTTONW, L"En bordure fine, barre de commandes masquée (survol du bas)", BS_AUTOCHECKBOX | WS_TABSTOP, 0, IDS_THINBAR);
    U.lblSizes = Mk(h, WC_STATICW, L"Tailles du bouton « Taille » (image) :", SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS, 0, -1);
    U.sizes    = Mk(h, WC_EDITW, L"", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, IDS_SIZES);
    U.lblHud   = Mk(h, WC_STATICW, L"Incrustation HUD (secondes, 0 = aucune) :", SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS, 0, -1);
    U.hud      = Mk(h, WC_EDITW, L"", ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, IDS_HUD);
    U.hudSp    = Mk(h, UPDOWN_CLASSW, NULL, UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_NOTHOUSANDS, 0, IDS_HUDSPIN);
    SendMessageW(U.hudSp, UDM_SETBUDDY, (WPARAM)U.hud, 0);
    SendMessageW(U.hudSp, UDM_SETRANGE32, 0, 60);

    U.lblVlc  = Mk(h, WC_STATICW, L"Dossier de VLC 64 bits (vide = détection automatique) :", SS_LEFT | SS_ENDELLIPSIS, 0, -1);
    U.vlcdir  = Mk(h, WC_EDITW, L"", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, IDS_VLCDIR);
    U.vlcBr   = Mk(h, WC_BUTTONW, L"…", BS_PUSHBUTTON | WS_TABSTOP, 0, IDS_VLCBROWSE);
    U.vlcInfo = Mk(h, WC_STATICW, L"", SS_LEFT | SS_NOPREFIX, 0, -1);

    U.lv = Mk(h, WC_LISTVIEWW, NULL, LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL | LVS_EDITLABELS | WS_TABSTOP,
              WS_EX_CLIENTEDGE, IDS_LV);
    SetWindowTheme(U.lv, L"Explorer", NULL);
    ListView_SetExtendedListViewStyle(U.lv, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_LABELTIP);
    { LVCOLUMNW c; memset(&c, 0, sizeof c); c.mask = LVCF_TEXT | LVCF_WIDTH;
      c.cx = 220; c.pszText = (LPWSTR)L"Nom (affiché si coché)"; ListView_InsertColumn(U.lv, 0, &c);
      c.cx = 56;  c.pszText = (LPWSTR)L"N°";       ListView_InsertColumn(U.lv, 1, &c);
      c.cx = 300; c.pszText = (LPWSTR)L"Adresse";  ListView_InsertColumn(U.lv, 2, &c); }
    U.up      = Mk(h, WC_BUTTONW, L"▲ Monter", BS_PUSHBUTTON | WS_TABSTOP, 0, IDS_UP);
    U.down    = Mk(h, WC_BUTTONW, L"▼ Descendre", BS_PUSHBUTTON | WS_TABSTOP, 0, IDS_DOWN);
    U.rename  = Mk(h, WC_BUTTONW, L"Renommer (F2)", BS_PUSHBUTTON | WS_TABSTOP, 0, IDS_RENAME);
    U.orig    = Mk(h, WC_BUTTONW, L"Nom d'origine", BS_PUSHBUTTON | WS_TABSTOP, 0, IDS_ORIGNAME);
    U.showall = Mk(h, WC_BUTTONW, L"Tout afficher", BS_PUSHBUTTON | WS_TABSTOP, 0, IDS_SHOWALL);
    U.hint    = Mk(h, WC_STATICW, L"Réglages appliqués immédiatement · double-clic sur une chaîne pour la regarder",
                   SS_LEFT | SS_ENDELLIPSIS | SS_NOPREFIX, 0, -1);
    U.close   = Mk(h, WC_BUTTONW, L"Fermer", BS_DEFPUSHBUTTON | WS_TABSTOP, 0, IDS_CLOSE);
}

static int LvSel(void) { return ListView_GetNextItem(U.lv, -1, LVNI_SELECTED); }

static void LvSelect(int i)
{
    ListView_SetItemState(U.lv, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetItemState(U.lv, i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(U.lv, i, FALSE);
}

static void MoveChannel(int dir)
{
    int s = LvSel(), t = s + dir;
    if (s < 0 || t < 0 || t >= g_nch) return;
    Channel tmp = g_ch[s]; g_ch[s] = g_ch[t]; g_ch[t] = tmp;
    if (g_cur == s) g_cur = t; else if (g_cur == t) g_cur = s;
    ChannelsChanged(1);
    FillLV();
    LvSelect(t);
    SetFocus(U.lv);
}

static void ReplaySoon(void) { if (g_cur >= 0) SetTimer(g_set, TMR_REPLAY, 700, NULL); }

static LRESULT SettingsNotify(NMHDR *nh)
{
    if (nh->idFrom != IDS_LV) return 0;
    switch (nh->code) {
    case LVN_ITEMCHANGED: {
        NMLISTVIEW *n = (NMLISTVIEW *)nh;
        if (g_lvFilling || n->iItem < 0 || n->iItem >= g_nch || !(n->uChanged & LVIF_STATE)) break;
        if ((n->uNewState ^ n->uOldState) & LVIS_STATEIMAGEMASK) {
            int checked = ((n->uNewState & LVIS_STATEIMAGEMASK) >> 12) == 2;
            if (g_ch[n->iItem].hidden == !checked) break;
            g_ch[n->iItem].hidden = !checked;
            ChannelsChanged(1);
            wchar_t t[96]; swprintf(t, 96, L"Chaînes  (%d, dont %d visibles)", g_nch, g_nvis);
            SetWindowTextW(U.hdr[4], t);
        }
        break; }
    case LVN_ENDLABELEDITW: {
        NMLVDISPINFOW *di = (NMLVDISPINFOW *)nh;
        int i = di->item.iItem;
        if (!di->item.pszText || i < 0 || i >= g_nch) return FALSE;
        wchar_t nm[128]; wcsncpy(nm, di->item.pszText, 127); nm[127] = 0; TrimW(nm);
        if (!nm[0]) wcscpy(nm, g_ch[i].orig);
        wcscpy(g_ch[i].name, nm);
        ListView_SetItemText(U.lv, i, 0, g_ch[i].name);
        ChannelsChanged(1);
        return FALSE; }
    case LVN_ITEMACTIVATE: {
        NMITEMACTIVATE *a = (NMITEMACTIVATE *)nh;
        if (a->iItem >= 0 && a->iItem < g_nch) PlayChannel(a->iItem);
        break; }
    case LVN_KEYDOWN: {
        NMLVKEYDOWN *k = (NMLVKEYDOWN *)nh;
        int s = LvSel();
        if (k->wVKey == VK_F2 && s >= 0) ListView_EditLabel(U.lv, s);
        else if (k->wVKey == VK_UP && GetKeyState(VK_MENU) < 0) MoveChannel(-1);
        else if (k->wVKey == VK_DOWN && GetKeyState(VK_MENU) < 0) MoveChannel(1);
        break; }
    }
    return 0;
}

static void SettingsCommand(HWND h, int id, int code)
{
    switch (id) {
    case IDOK:
        if (GetFocus() == U.url) SettingsCommand(h, IDS_RELOAD, BN_CLICKED);
        else if (GetFocus() == U.vlcdir) SettingsCommand(h, IDS_VLCDIR, EN_KILLFOCUS);
        break;
    case IDCANCEL: case IDS_CLOSE: DestroyWindow(h); break;

    case IDS_RELOAD:
        GetWindowTextW(U.url, C.playlist, 2048);
        g_cur = -1;
        LoadPlaylist();
        SaveConfig();
        break;
    case IDS_URLBROWSE: {
        wchar_t f[MAX_PATH] = L"";
        if (PickFile(h, L"Playlists (*.m3u;*.m3u8)\0*.m3u;*.m3u8\0Vidéos\0*.ts;*.mkv;*.mp4;*.avi;*.mpg\0Tous les fichiers\0*.*\0",
                     L"Choisir une playlist ou un fichier", f, MAX_PATH)) {
            SetWindowTextW(U.url, f);
            SettingsCommand(h, IDS_RELOAD, BN_CLICKED);
        }
        break; }

    case IDS_CACHE:
        if (code == EN_CHANGE && !g_setInit) {
            wchar_t b[16]; GetWindowTextW(U.cache, b, 16);
            C.caching = Clamp(_wtoi(b), 0, 20000);
            ReplaySoon();
        }
        break;
    case IDS_DELAY:
        if (code == EN_CHANGE && !g_setInit) {
            wchar_t b[16]; GetWindowTextW(U.delay, b, 16);
            C.delay = Clamp(_wtoi(b), -10000, 10000);
            if (g_vlcState == 1) p_libvlc_audio_set_delay(g_mp, (int64_t)C.delay * 1000);
        }
        break;
    case IDS_HW:
        if (code == CBN_SELCHANGE) { C.hw = (int)SendMessageW(U.hw, CB_GETCURSEL, 0, 0); ReplaySoon(); }
        break;
    case IDS_DEINT:
        if (code == CBN_SELCHANGE) { C.deint = (int)SendMessageW(U.deint, CB_GETCURSEL, 0, 0); ApplyVideoOpts(); }
        break;
    case IDS_ASPECT:
        if (code == CBN_SELCHANGE) { C.aspect = (int)SendMessageW(U.aspect, CB_GETCURSEL, 0, 0); ApplyVideoOpts(); }
        break;

    case IDS_TOPMOST:  C.topmost  = Button_GetCheck(U.topmost) == BST_CHECKED; ApplyTopmost(); break;
    case IDS_RESUME:   C.resume   = Button_GetCheck(U.resume) == BST_CHECKED; break;
    case IDS_AUTOPLAY: C.autoplay = Button_GetCheck(U.autoplay) == BST_CHECKED; break;
    case IDS_SHOWLIST: C.showlist = Button_GetCheck(U.showlist) == BST_CHECKED; LayoutMain(); break;
    case IDS_HUD:
        if (code == EN_CHANGE && !g_setInit) {
            wchar_t b[16]; GetWindowTextW(U.hud, b, 16);
            C.hudSecs = Clamp(_wtoi(b), 0, 60);
            ShowHud();
        }
        break;
    case IDS_THIN:     SetThin(Button_GetCheck(U.thin) == BST_CHECKED); break;
    case IDS_THINBAR:
        C.thinHideBar = Button_GetCheck(U.thinbar) == BST_CHECKED;
        UpdateAutoBar(); LayoutMain();
        break;
    case IDS_SIZES:
        if (code == EN_CHANGE && !g_setInit) {
            GetWindowTextW(U.sizes, C.sizes, 256);
            SizePreset ps[16];
            if (C.sizeIdx >= ParseSizes(ps, 16)) C.sizeIdx = -1;
            UpdateSizeButton();
        }
        break;

    case IDS_VLCDIR:
        if (code == EN_KILLFOCUS) {
            wchar_t b[MAX_PATH]; GetWindowTextW(U.vlcdir, b, MAX_PATH); TrimW(b);
            if (wcscmp(b, C.vlcdir)) {
                wcscpy(C.vlcdir, b); SaveConfig();
                if (g_vlcState == -1) { StartVlc(); SetHostMsg(L"Chargement de libVLC…"); }
                else { wchar_t t[600]; swprintf(t, 600, L"%ls\n→ redémarrez l'application pour utiliser le nouveau dossier.", g_vlcInfo); SetWindowTextW(U.vlcInfo, t); }
            }
        }
        break;
    case IDS_VLCBROWSE: {
        wchar_t f[MAX_PATH] = L"libvlc.dll";
        if (PickFile(h, L"libvlc.dll\0libvlc.dll\0", L"Sélectionner libvlc.dll (dossier de VLC 64 bits)", f, MAX_PATH)) {
            wchar_t *s = wcsrchr(f, L'\\'); if (s) *s = 0;
            SetWindowTextW(U.vlcdir, f);
            SettingsCommand(h, IDS_VLCDIR, EN_KILLFOCUS);
        }
        break; }

    case IDS_UP:   MoveChannel(-1); break;
    case IDS_DOWN: MoveChannel(1); break;
    case IDS_RENAME: { int s = LvSel(); if (s >= 0) { SetFocus(U.lv); ListView_EditLabel(U.lv, s); } break; }
    case IDS_ORIGNAME: {
        int s = LvSel();
        if (s >= 0) { wcscpy(g_ch[s].name, g_ch[s].orig); ListView_SetItemText(U.lv, s, 0, g_ch[s].name); ChannelsChanged(1); }
        break; }
    case IDS_SHOWALL:
        for (int i = 0; i < g_nch; i++) g_ch[i].hidden = 0;
        ChannelsChanged(1); FillLV();
        break;
    }
}

static LRESULT CALLBACK SettingsProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    switch (msg) {
    case WM_CREATE:
        g_set = h;
        g_sdpi = GetDpiForWindow(h);
        CreateSettingsControls(h);
        ApplySettingsFonts();
        SettingsSync();
        FillLV();
        return 0;
    case WM_SIZE:
        if (w != SIZE_MINIMIZED) LayoutSettings(LOWORD(l), HIWORD(l), TRUE);
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)l;
        RECT rc; GetClientRect(h, &rc);
        RECT r = { 0, 0, SS(520), LayoutSettings(rc.right ? rc.right : SS(980), rc.bottom, FALSE) };
        AdjustWindowRectEx(&r, (DWORD)GetWindowLongW(h, GWL_STYLE), FALSE, (DWORD)GetWindowLongW(h, GWL_EXSTYLE));
        mm->ptMinTrackSize.x = r.right - r.left; mm->ptMinTrackSize.y = r.bottom - r.top;
        return 0; }
    case WM_DPICHANGED: {
        g_sdpi = HIWORD(w);
        ApplySettingsFonts();
        RECT *r = (RECT *)l;
        SetWindowPos(h, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0; }
    case WM_CTLCOLORSTATIC:
        for (int i = 0; i < 5; i++) if ((HWND)l == U.hdr[i]) {
            SetTextColor((HDC)w, RGB(0, 95, 184));
            SetBkColor((HDC)w, GetSysColor(COLOR_BTNFACE));
            return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
        }
        break;
    case WM_COMMAND: SettingsCommand(h, LOWORD(w), HIWORD(w)); return 0;
    case WM_NOTIFY:  return SettingsNotify((NMHDR *)l);
    case WM_TIMER:
        if (w == TMR_REPLAY) { KillTimer(h, TMR_REPLAY); if (g_cur >= 0 && !g_userStopped) RequestPlayCur(); }
        return 0;
    case WM_DESTROY: {
        WINDOWPLACEMENT wp = { sizeof wp };
        GetWindowPlacement(h, &wp);
        C.setrc = wp.rcNormalPosition;
        GetWindowTextW(U.vlcdir, C.vlcdir, MAX_PATH); TrimW(C.vlcdir);
        SaveConfig();
        g_set = NULL;
        if (g_main && !g_quitting) SetFocus(g_main);
        return 0; }
    }
    return DefWindowProcW(h, msg, w, l);
}

static int RectOnScreen(const RECT *r)
{
    return !IsRectEmpty(r) && r->right - r->left > 200 && r->bottom - r->top > 150
        && MonitorFromRect(r, MONITOR_DEFAULTTONULL) != NULL;
}

static void OpenSettings(int editCh)
{
    if (!g_set) {
        CreateWindowExW(0, L"VlcMasterSettings", L"Paramètres — " APP_NAME,
                        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 100, 100,
                        g_main, NULL, g_hi, NULL);
        if (!g_set) return;
        WINDOWPLACEMENT wp = { sizeof wp };
        GetWindowPlacement(g_set, &wp);
        if (RectOnScreen(&C.setrc)) wp.rcNormalPosition = C.setrc;
        else {
            RECT mr; GetWindowRect(g_main, &mr);
            int w = SS(1000), hh = SS(800);
            int x = (mr.left + mr.right - w) / 2, y = (mr.top + mr.bottom - hh) / 2;
            if (y < 0) y = 0;
            SetRect(&wp.rcNormalPosition, x, y, x + w, y + hh);
        }
        wp.showCmd = SW_SHOWNORMAL;
        SetWindowPlacement(g_set, &wp);
    } else {
        if (IsIconic(g_set)) ShowWindow(g_set, SW_RESTORE);
        SetForegroundWindow(g_set);
    }
    if (editCh == -2) { SetFocus(U.sizes); SendMessageW(U.sizes, EM_SETSEL, 0, -1); }
    if (editCh >= 0 && editCh < g_nch) {
        LvSelect(editCh);
        SetFocus(U.lv);
        ListView_EditLabel(U.lv, editCh);
    }
}

/* ------------------------------------------------------------------ */
/* Point d'entrée                                                      */
/* ------------------------------------------------------------------ */
int WINAPI wWinMain(HINSTANCE hi, HINSTANCE prev, PWSTR cmd, int show)
{
    (void)prev;
    g_hi = hi;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    GetModuleFileNameW(NULL, g_exedir, MAX_PATH);
    { wchar_t *s = wcsrchr(g_exedir, L'\\'); if (s) *s = 0; }
    InitIniPath();
    LoadConfig();

    /* Argument facultatif : playlist ou flux à ouvrir. */
    if (cmd && *cmd) {
        wchar_t a[2048]; wcsncpy(a, cmd, 2047); a[2047] = 0; TrimW(a);
        if (a[0]) wcscpy(C.playlist, a);
    }

    /* Contrôles communs v6 (thèmes) : manifeste embarqué en ressource n° 2. */
    {
        ACTCTXW ac; memset(&ac, 0, sizeof ac);
        ac.cbSize = sizeof ac; ac.hModule = hi;
        ac.dwFlags = ACTCTX_FLAG_HMODULE_VALID | ACTCTX_FLAG_RESOURCE_NAME_VALID;
        ac.lpResourceName = MAKEINTRESOURCEW(2);
        HANDLE ctx = CreateActCtxW(&ac);
        ULONG_PTR cookie;
        if (ctx != INVALID_HANDLE_VALUE) ActivateActCtx(ctx, &cookie);
    }
    INITCOMMONCONTROLSEX icc = { sizeof icc, ICC_STANDARD_CLASSES | ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES | ICC_UPDOWN_CLASS };
    InitCommonControlsEx(&icc);
    InitializeCriticalSection(&g_cs);
    g_wake = CreateEventW(NULL, FALSE, FALSE, NULL);

    HICON ico = LoadIconW(hi, MAKEINTRESOURCEW(1));
    if (!ico) ico = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);

    WNDCLASSEXW wc; memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc; wc.hInstance = hi; wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW); wc.hIcon = ico;
    wc.lpfnWndProc = MainProc; wc.lpszClassName = L"VlcMasterMain";
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassExW(&wc);
    wc.lpfnWndProc = HostProc; wc.lpszClassName = L"VlcMasterVideo"; wc.style = CS_DBLCLKS;
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassExW(&wc);
    wc.lpfnWndProc = HudProc; wc.lpszClassName = L"VlcMasterHud"; wc.style = 0;
    wc.hbrBackground = NULL; wc.hIcon = NULL;
    RegisterClassExW(&wc);
    wc.hIcon = ico;
    wc.lpfnWndProc = SettingsProc; wc.lpszClassName = L"VlcMasterSettings"; wc.style = 0;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassExW(&wc);

    UINT sdpi = GetDpiForSystem();
    HWND h = CreateWindowExW(0, L"VlcMasterMain", APP_NAME, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             CW_USEDEFAULT, CW_USEDEFAULT, MulDiv(1120, (int)sdpi, 96), MulDiv(700, (int)sdpi, 96),
                             NULL, NULL, hi, NULL);
    if (!h) return 1;
    g_hud = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                            L"VlcMasterHud", NULL, WS_POPUP, 0, 0, 10, 10, h, NULL, hi, NULL);
    if (g_hud) SetLayeredWindowAttributes(g_hud, 0, 225, LWA_ALPHA);
    ApplyTopmost();

    if (C.thin) { SetWindowLongW(h, GWL_STYLE, (LONG)MainStyle()); SetWindowPos(h, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED); }
    ApplyCorners();
    UpdateAutoBar();
    UpdateSizeButton();
    if (RectOnScreen(&C.mainrc)) {
        WINDOWPLACEMENT wp = { sizeof wp };
        GetWindowPlacement(h, &wp);
        wp.rcNormalPosition = C.mainrc;
        wp.showCmd = C.mainmax ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
        SetWindowPlacement(h, &wp);
    } else ShowWindow(h, show);
    UpdateWindow(h);

    StartVlc();
    LoadPlaylist();

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && g_main && GetAncestor(msg.hwnd, GA_ROOT) == g_main
            && HandleKey(msg.wParam)) continue;
        if (msg.message == WM_MOUSEWHEEL && g_main && GetAncestor(msg.hwnd, GA_ROOT) == g_main) {
            POINT pt = { GET_X_LPARAM(msg.lParam), GET_Y_LPARAM(msg.lParam) };
            HWND u = WindowFromPoint(pt);
            if (u == g_host || IsChild(g_host, u)) {
                SetVolume(C.volume + (GET_WHEEL_DELTA_WPARAM(msg.wParam) > 0 ? 5 : -5));
                continue;
            }
        }
        if (g_set && IsDialogMessageW(g_set, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
