/*
 * timeshift.h — pause en différé (option « Pause en différé », moteur VLC).
 *
 * Avec l'option, l'image principale ne lit plus le tuner directement mais
 * http://127.0.0.1:<port>/live/<clé>/<n>, servi par ce module.  Pour chaque
 * connexion de libVLC :
 *   - un thread (TsWriter) lit le flux du tuner sans jamais s'arrêter et l'écrit
 *     dans des segments temporaires de 32 Mo (%TEMP%\HomeRunPlayer-timeshift) ;
 *   - le thread de la connexion (TsConn) renvoie ces segments à libVLC à son
 *     rythme et efface chaque segment dès qu'il est lu.
 * En pause, libVLC ne lit plus : le tuner continue d'être mis en mémoire et, à la
 * reprise, l'émission repart exactement là où elle s'était arrêtée — en différé.
 *
 * Le retard sur le direct est la somme des durées de pause.  Il s'affiche dans
 * une pastille en haut à droite de l'image et dans la barre d'état ; il est
 * limité à 30 min (la lecture reprend alors toute seule).  Retour au direct :
 * touche D, clic sur la pastille, menu du clic droit, ou changement de chaîne.
 *
 * Disque : seulement ce qui n'a pas encore été regardé (≈ différé × débit, soit
 * 1 à 4 Go pour 30 min selon la chaîne).  Les segments sont effacés au fur et à
 * mesure, au retour au direct, au changement de chaîne et à la fermeture ; ceux
 * d'une session interrompue (plantage) sont effacés au démarrage suivant.
 *
 * Inclus par vlcmaster.c après mpc.h.
 */
#include <winsock2.h>

#define TS_SEG       (32LL * 1024 * 1024)        /* taille d'un segment temporaire */
#ifndef TS_MAXDELAY                               /* (redéfinissable pour les tests) */
#define TS_MAXDELAY  (30ULL * 60 * 1000)          /* différé maximal (ms) */
#endif
#define TS_MAXBYTES  (8LL * 1024 * 1024 * 1024)   /* garde-fou disque (non lu) */
#define TS_BUF       (256 * 1024)

typedef struct {
    char url[1024];                 /* flux du tuner */
    wchar_t base[MAX_PATH];         /* préfixe des fichiers de segments */
    CRITICAL_SECTION cs;
    HANDLE ev;                      /* « nouvelles données » */
    volatile LONG stop;
    HINTERNET volatile hreq;        /* requête du tuner (fermée pour interrompre) */
    int wseg, wdone;                /* segment en cours d'écriture ; flux du tuner terminé */
    volatile LONGLONG written, sent;
} TsSess;

static SOCKET   g_tsLs = INVALID_SOCKET;
static int      g_tsPort, g_tsReady, g_tsFailed;
static unsigned g_tsKey;
static CRITICAL_SECTION g_tsCs;
static char     g_tsUrl[1024];
static volatile LONG g_tsGen, g_tsSessN;
static wchar_t  g_tsDir[MAX_PATH];

/* état côté interface (thread principal) */
static int       g_tsOn;                    /* l'image principale lit via le différé */
static ULONGLONG g_tsBase, g_tsPauseAt;     /* différé cumulé ; début de la pause en cours (0 = pas en pause) */
static HWND      g_tsBadge;
static HFONT     g_tsFont; static UINT g_tsFontDpi;
static wchar_t   g_tsBadgeTxt[96];

static void RequestPlayCur(void);

static void TsSegPath(const TsSess *s, int i, wchar_t *o) { swprintf(o, MAX_PATH, L"%ls-%d.ts", s->base, i); }

/* ------------------------------------------------------------------ */
/* Threads : tuner -> segments -> libVLC                               */
/* ------------------------------------------------------------------ */
static DWORD WINAPI TsWriter(LPVOID pv)
{
    TsSess *s = (TsSess *)pv;
    BYTE *buf = (BYTE *)malloc(TS_BUF);
    HINTERNET net = InternetOpenW(L"HomeRunPlayer/" APP_VER, INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    HANDLE f = INVALID_HANDLE_VALUE;
    LONGLONG segBytes = 0;
    if (buf && net && !s->stop) {
        DWORD to = 6000;  InternetSetOptionW(net, INTERNET_OPTION_CONNECT_TIMEOUT, &to, sizeof to);
        to = 15000;       InternetSetOptionW(net, INTERNET_OPTION_RECEIVE_TIMEOUT, &to, sizeof to);
        wchar_t wurl[1024]; Utf8ToW(s->url, wurl, 1024);
        HINTERNET q = InternetOpenUrlW(net, wurl, NULL, 0, INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_UI, 0);
        if (q) {
            DWORD code = 0, cl = sizeof code;
            HttpQueryInfoW(q, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &code, &cl, NULL);
            if (code && code != 200) { InternetCloseHandle(q); q = NULL; }
        }
        if (q) {
            InterlockedExchangePointer((PVOID volatile *)&s->hreq, q);
            DWORD n = 0;
            while (!s->stop && InternetReadFile(q, buf, TS_BUF, &n) && n) {
                if (s->written - s->sent > TS_MAXBYTES) continue;          /* garde-fou : on jette */
                if (f == INVALID_HANDLE_VALUE) {
                    wchar_t p[MAX_PATH]; TsSegPath(s, s->wseg, p);
                    f = CreateFileW(p, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED, NULL);
                    if (f == INVALID_HANDLE_VALUE) break;
                    segBytes = 0;
                }
                DWORD wr = 0;
                if (!WriteFile(f, buf, n, &wr, NULL) || wr != n) break;      /* disque plein */
                segBytes += n;
                EnterCriticalSection(&s->cs);
                s->written += n;
                if (segBytes >= TS_SEG) { CloseHandle(f); f = INVALID_HANDLE_VALUE; s->wseg++; }
                LeaveCriticalSection(&s->cs);
                SetEvent(s->ev);
            }
            HINTERNET q2 = (HINTERNET)InterlockedExchangePointer((PVOID volatile *)&s->hreq, NULL);
            if (q2) InternetCloseHandle(q2);
        }
    }
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    EnterCriticalSection(&s->cs); s->wdone = 1; LeaveCriticalSection(&s->cs);
    SetEvent(s->ev);
    if (net) InternetCloseHandle(net);
    free(buf);
    return 0;
}

static int TsSendAll(SOCKET c, const char *p, int n)
{
    while (n > 0) { int k = send(c, p, n, 0); if (k <= 0) return 0; p += k; n -= k; }
    return 1;
}

/* Une connexion de libVLC : lance la copie du tuner, puis envoie les segments dans l'ordre. */
static DWORD WINAPI TsConn(LPVOID pv)
{
    SOCKET c = (SOCKET)(UINT_PTR)pv;
    char req[2048]; int got = 0;
    while (got < (int)sizeof req - 1) {                       /* en-tête HTTP jusqu'à la ligne vide */
        int k = recv(c, req + got, (int)sizeof req - 1 - got, 0);
        if (k <= 0) break;
        got += k; req[got] = 0;
        if (strstr(req, "\r\n\r\n")) break;
    }
    req[got] = 0;
    unsigned key = 0; long gen = -1;
    TsSess *s = NULL;
    if (sscanf(req, "GET /live/%x/%ld", &key, &gen) == 2) {
        EnterCriticalSection(&g_tsCs);
        if (key == g_tsKey && gen == g_tsGen && g_tsUrl[0] && (s = (TsSess *)calloc(1, sizeof *s)))
            memcpy(s->url, g_tsUrl, sizeof s->url);
        LeaveCriticalSection(&g_tsCs);
    }
    if (!s) {
        static const char nf[] = "HTTP/1.0 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        TsSendAll(c, nf, (int)sizeof nf - 1);
        closesocket(c);
        return 0;
    }
    InitializeCriticalSection(&s->cs);
    s->ev = CreateEventW(NULL, FALSE, FALSE, NULL);
    swprintf(s->base, MAX_PATH, L"%ls\\%lu-%ld", g_tsDir, GetCurrentProcessId(), InterlockedIncrement(&g_tsSessN));
    HANDLE wt = CreateThread(NULL, 0, TsWriter, s, 0, NULL);

    /* Attend les premières données du tuner : sans elles (aucun tuner libre…), erreur 503. */
    int ok = 0;
    for (int i = 0; wt && i < 200; i++) {
        EnterCriticalSection(&s->cs); LONGLONG w = s->written; int d = s->wdone; LeaveCriticalSection(&s->cs);
        if (w > 0) { ok = 1; break; }
        if (d) break;
        WaitForSingleObject(s->ev, 100);
    }
    if (ok) {
        static const char hdr[] = "HTTP/1.0 200 OK\r\nContent-Type: video/mp2t\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n";
        ok = TsSendAll(c, hdr, (int)sizeof hdr - 1);
    } else {
        static const char un[] = "HTTP/1.0 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        TsSendAll(c, un, (int)sizeof un - 1);
    }

    BYTE *buf = (BYTE *)malloc(TS_BUF);
    int rseg = 0;
    HANDLE rf = INVALID_HANDLE_VALUE;
    wchar_t p[MAX_PATH];
    while (ok && buf) {
        if (rf == INVALID_HANDLE_VALUE) {
            TsSegPath(s, rseg, p);
            rf = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                             FILE_ATTRIBUTE_TEMPORARY, NULL);
            if (rf == INVALID_HANDLE_VALUE) {                  /* pas encore créé par le writer */
                EnterCriticalSection(&s->cs); int d = s->wdone; LeaveCriticalSection(&s->cs);
                if (d) break;
                WaitForSingleObject(s->ev, 200);
                continue;
            }
        }
        DWORD n = 0;
        if (!ReadFile(rf, buf, TS_BUF, &n, NULL)) break;
        if (n) {
            if (!TsSendAll(c, (const char *)buf, (int)n)) break;     /* libVLC a fermé (zapping, arrêt) */
            InterlockedExchangeAdd64(&s->sent, (LONGLONG)n);
            continue;
        }
        EnterCriticalSection(&s->cs); int complete = rseg < s->wseg, d = s->wdone; LeaveCriticalSection(&s->cs);
        if (complete || d) {
            /* Le writer a fini ce segment : une dernière lecture récupère ce qui a pu arriver entre-temps. */
            if (ReadFile(rf, buf, TS_BUF, &n, NULL) && n) {
                if (!TsSendAll(c, (const char *)buf, (int)n)) break;
                InterlockedExchangeAdd64(&s->sent, (LONGLONG)n);
                continue;
            }
            if (!complete) break;                              /* flux du tuner terminé */
            CloseHandle(rf); rf = INVALID_HANDLE_VALUE;
            DeleteFileW(p);
            rseg++;
            continue;
        }
        WaitForSingleObject(s->ev, 300);
    }

    /* Fin : arrête la copie du tuner (libère le tuner), efface les segments restants. */
    InterlockedExchange(&s->stop, 1);
    HINTERNET q = (HINTERNET)InterlockedExchangePointer((PVOID volatile *)&s->hreq, NULL);
    if (q) InternetCloseHandle(q);
    if (wt) { WaitForSingleObject(wt, 20000); CloseHandle(wt); }
    if (rf != INVALID_HANDLE_VALUE) CloseHandle(rf);
    for (int i = rseg; i <= s->wseg; i++) { TsSegPath(s, i, p); DeleteFileW(p); }
    shutdown(c, SD_BOTH);
    closesocket(c);
    free(buf);
    CloseHandle(s->ev);
    DeleteCriticalSection(&s->cs);
    free(s);
    return 0;
}

static DWORD WINAPI TsAccept(LPVOID pv)
{
    (void)pv;
    for (;;) {
        SOCKET c = accept(g_tsLs, NULL, NULL);
        if (c == INVALID_SOCKET) { if (g_tsLs == INVALID_SOCKET || g_quitting) break; Sleep(50); continue; }
        HANDLE t = CreateThread(NULL, 0, TsConn, (LPVOID)(UINT_PTR)c, 0, NULL);
        if (t) CloseHandle(t); else closesocket(c);
    }
    return 0;
}

/* Efface les segments laissés par une session précédente (programme fermé brutalement). */
static void TsCleanOld(void)
{
    wchar_t pat[MAX_PATH]; swprintf(pat, MAX_PATH, L"%ls\\*.ts", g_tsDir);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        DWORD pid = (DWORD)wcstoul(fd.cFileName, NULL, 10);
        HANDLE ph = pid && pid != GetCurrentProcessId() ? OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid) : NULL;
        DWORD code = 0;
        int alive = ph && GetExitCodeProcess(ph, &code) && code == STILL_ACTIVE;
        if (ph) CloseHandle(ph);
        if (!alive) { wchar_t f[MAX_PATH]; swprintf(f, MAX_PATH, L"%ls\\%ls", g_tsDir, fd.cFileName); DeleteFileW(f); }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

/* Démarre le petit serveur local (une fois).  Renvoie 0 si impossible. */
static int TsStart(void)
{
    if (g_tsReady) return 1;
    if (g_tsFailed) return 0;
    g_tsFailed = 1;
    WSADATA wd;
    if (WSAStartup(MAKEWORD(2, 2), &wd)) return 0;
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return 0;
    struct sockaddr_in a; memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = 0;
    int al = sizeof a;
    if (bind(s, (struct sockaddr *)&a, sizeof a) || listen(s, 8) || getsockname(s, (struct sockaddr *)&a, &al)) { closesocket(s); return 0; }
    g_tsPort = ntohs(a.sin_port);
    g_tsKey = (unsigned)(GetTickCount64() * 2654435761u) ^ GetCurrentProcessId();
    wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
    swprintf(g_tsDir, MAX_PATH, L"%lsHomeRunPlayer-timeshift", tmp);
    CreateDirectoryW(g_tsDir, NULL);
    TsCleanOld();
    InitializeCriticalSection(&g_tsCs);
    g_tsLs = s;
    HANDLE t = CreateThread(NULL, 0, TsAccept, NULL, 0, NULL);
    if (!t) { closesocket(s); g_tsLs = INVALID_SOCKET; return 0; }
    CloseHandle(t);
    g_tsReady = 1; g_tsFailed = 0;
    return 1;
}

/* Fermeture du programme : plus de connexions ; les segments encore ouverts sont effacés. */
static void TsShutdown(void)
{
    if (!g_tsReady) return;
    SOCKET s = g_tsLs; g_tsLs = INVALID_SOCKET;
    closesocket(s);
    wchar_t pat[MAX_PATH]; swprintf(pat, MAX_PATH, L"%ls\\%lu-*.ts", g_tsDir, GetCurrentProcessId());
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do { wchar_t f[MAX_PATH]; swprintf(f, MAX_PATH, L"%ls\\%ls", g_tsDir, fd.cFileName); DeleteFileW(f); }
        while (FindNextFileW(h, &fd));
        FindClose(h);
    }
}

/* ------------------------------------------------------------------ */
/* Interface : différé, pastille, retour au direct                    */
/* ------------------------------------------------------------------ */
static ULONGLONG TsDelayMs(void)
{
    if (!g_tsOn) return 0;
    return g_tsBase + (g_tsPauseAt ? GetTickCount64() - g_tsPauseAt : 0);
}

/* « 12:34 » (ou « 1:02:03 ») */
static void TsFmt(ULONGLONG ms, wchar_t *o, int cap)
{
    unsigned s = (unsigned)(ms / 1000);
    if (s >= 3600) swprintf(o, (size_t)cap, L"%u:%02u:%02u", s / 3600, s / 60 % 60, s % 60);
    else swprintf(o, (size_t)cap, L"%02u:%02u", s / 60, s % 60);
}

/* Texte du différé pour la barre d'état (vide s'il n'y en a pas). */
static int TsStatusText(wchar_t *o, int cap)
{
    ULONGLONG d = TsDelayMs();
    if (!g_tsOn || (!g_tsPauseAt && d < 1000)) { o[0] = 0; return 0; }
    wchar_t t[24]; TsFmt(d, t, 24);
    swprintf(o, (size_t)cap, TR(L"Différé −%ls"), t);
    return 1;
}

static void TsBadgeUpdate(void)
{
    if (!g_tsBadge) return;
    ULONGLONG d = TsDelayMs();
    int show = g_tsOn && (g_tsPauseAt || d >= 1000) && !g_fileMode;
    if (show && (!IsWindowVisible(g_main) || IsIconic(g_main) || !IsWindowVisible(g_host))) show = 0;
    if (!show) { if (IsWindowVisible(g_tsBadge)) ShowWindow(g_tsBadge, SW_HIDE); return; }
    wchar_t t[24]; TsFmt(d, t, 24);
    if (g_tsPauseAt) swprintf(g_tsBadgeTxt, 96, TR(L"En pause  ·  différé −%ls"), t);
    else swprintf(g_tsBadgeTxt, 96, TR(L"Différé −%ls   ·   Direct ▶"), t);
    if (!g_tsFont || g_tsFontDpi != g_dpi) {
        if (g_tsFont) DeleteObject(g_tsFont);
        g_tsFont = MakeFont(g_dpi, 10, FW_SEMIBOLD); g_tsFontDpi = g_dpi;
    }
    HDC dc = GetDC(g_tsBadge); HGDIOBJ of = SelectObject(dc, g_tsFont);
    SIZE sz; GetTextExtentPoint32W(dc, g_tsBadgeTxt, (int)wcslen(g_tsBadgeTxt), &sz);
    SelectObject(dc, of); ReleaseDC(g_tsBadge, dc);
    int w = sz.cx + S(36), h = sz.cy + S(14);
    RECT r; GetWindowRect(g_host, &r);
    int x = r.right - S(14) - w, y = r.top + S(14);
    if (x < r.left) x = r.left;
    RECT cur; GetWindowRect(g_tsBadge, &cur);
    if (cur.left != x || cur.top != y || cur.right - cur.left != w || cur.bottom - cur.top != h || !IsWindowVisible(g_tsBadge)) {
        SetWindowRgn(g_tsBadge, CreateRoundRectRgn(0, 0, w + 1, h + 1, h, h), FALSE);
        SetWindowPos(g_tsBadge, HWND_TOP, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    InvalidateRect(g_tsBadge, NULL, FALSE);
}

/* Retour au direct : nouvelle connexion, donc nouveau flux et segments effacés. */
static void TsLive(void)
{
    if (!g_tsOn || (!g_tsPauseAt && TsDelayMs() < 1000)) return;
    RequestPlayCur();
    Flash(TR(L"Retour au direct"));
}

static LRESULT CALLBACK TsBadgeProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    switch (msg) {
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_SETCURSOR: SetCursor(LoadCursorW(NULL, IDC_HAND)); return TRUE;
    case WM_LBUTTONUP: TsLive(); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        HBRUSH bg = CreateSolidBrush(g_tsPauseAt ? RGB(16, 17, 22) : RGB(150, 70, 0));
        FillRect(dc, &rc, bg); DeleteObject(bg);
        HBRUSH dot = CreateSolidBrush(g_tsPauseAt ? RGB(255, 150, 30) : RGB(255, 220, 160));
        HGDIOBJ ob = SelectObject(dc, dot), op = SelectObject(dc, GetStockObject(NULL_PEN));
        int r = S(5), cy = rc.bottom / 2;
        Ellipse(dc, S(12), cy - r, S(12) + 2 * r, cy + r);
        SelectObject(dc, ob); SelectObject(dc, op); DeleteObject(dot);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(250, 250, 252));
        HGDIOBJ of = SelectObject(dc, g_tsFont);
        RECT tr = { S(26), 0, rc.right - S(10), rc.bottom };
        DrawTextW(dc, g_tsBadgeTxt, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, of);
        EndPaint(h, &ps);
        return 0; }
    }
    return DefWindowProcW(h, msg, w, l);
}

static void TsCreate(HWND owner)
{
    WNDCLASSW wc; memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = TsBadgeProc; wc.hInstance = g_hi; wc.lpszClassName = L"HrpTsBadge";
    wc.hCursor = LoadCursorW(NULL, IDC_HAND);
    RegisterClassW(&wc);
    g_tsBadge = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"HrpTsBadge", NULL, WS_POPUP,
                                0, 0, 10, 10, owner, NULL, g_hi, NULL);
    if (g_tsBadge) SetLayeredWindowAttributes(g_tsBadge, 0, 235, LWA_ALPHA);
}

/* URL à donner au lecteur pour la chaîne : le tuner directement, ou le différé si l'option est active.
   Remet le différé à zéro (nouvelle connexion = direct). */
static void TsPlayUrl(const char *chUrl, char *out, size_t cap)
{
    g_tsBase = 0; g_tsPauseAt = 0;
    g_tsOn = C.timeshift && !g_engine && !g_fileMode && TsStart();
    if (!g_tsOn) snprintf(out, cap, "%s", chUrl);
    else {
        EnterCriticalSection(&g_tsCs);
        snprintf(g_tsUrl, sizeof g_tsUrl, "%s", chUrl);
        long gen = InterlockedIncrement(&g_tsGen);
        LeaveCriticalSection(&g_tsCs);
        snprintf(out, cap, "http://127.0.0.1:%d/live/%x/%ld", g_tsPort, g_tsKey, gen);
    }
    KillTimer(g_main, TMR_TS);
    TsBadgeUpdate();
}

/* Appelé juste avant de mettre en pause / reprendre l'image principale. */
static void TsOnPause(int pausing)
{
    if (!g_tsOn) return;
    ULONGLONG now = GetTickCount64();
    if (pausing) { if (!g_tsPauseAt) g_tsPauseAt = now; }
    else if (g_tsPauseAt) { g_tsBase += now - g_tsPauseAt; g_tsPauseAt = 0; }
    if (g_tsBase > TS_MAXDELAY) g_tsBase = TS_MAXDELAY;
    SetTimer(g_main, TMR_TS, 500, NULL);
    TsBadgeUpdate();
    UpdateStatus();
}

/* Toutes les 500 ms pendant une pause ou un différé : compteur, limite de 30 min. */
static void TsTick(void)
{
    if (!g_tsOn) { KillTimer(g_main, TMR_TS); TsBadgeUpdate(); return; }
    if (g_tsPauseAt && TsDelayMs() >= TS_MAXDELAY) {
        g_tsBase = TS_MAXDELAY; g_tsPauseAt = 0;
        EngSetPause(0);
        Flash(TR(L"Différé maximal (30 min) atteint : la lecture reprend"));
    }
    static unsigned lastSec = ~0u;
    unsigned sec = (unsigned)(TsDelayMs() / 1000);
    if (sec != lastSec) { lastSec = sec; UpdateStatus(); }
    TsBadgeUpdate();
    if (!g_tsPauseAt && TsDelayMs() < 1000) KillTimer(g_main, TMR_TS);
}

/* Lecture arrêtée ou passage à un enregistrement : plus de différé affiché. */
static void TsReset(void)
{
    g_tsOn = 0; g_tsBase = 0; g_tsPauseAt = 0;
    KillTimer(g_main, TMR_TS);
    TsBadgeUpdate();
}
