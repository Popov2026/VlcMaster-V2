/*
 * update.h — mises à jour de HomeRun Player.
 *
 * Au démarrage (puis toutes les 12 h) : lecture de la dernière release publiée
 * sur GitHub (api.github.com/repos/…/releases/latest) et comparaison avec la
 * version installée (APP_VER).  Si elle est plus récente, un bandeau apparaît
 * en haut de la fenêtre : « Télécharger » (zip de la release), « Nouveautés »
 * (page de la release), « ✕ » (ne plus signaler cette version).
 * Désactivable dans Paramètres › Interface ; « Vérifier maintenant » à la main.
 *
 * Inclus par vlcmaster.c après tuner.h.
 */
#define UPD_REPO L"Popov2026/VlcMaster-V2"
#define UPD_API  L"https://api.github.com/repos/" UPD_REPO L"/releases/latest"
#define UPD_PAGE L"https://github.com/" UPD_REPO L"/releases/latest"

typedef struct { int ok, manual; wchar_t tag[32], page[512], zip[512], err[256]; } UpdInfo;
static UpdInfo g_upd;
enum { IDB_DL = 1, IDB_NOTES, IDB_CLOSE };
static HWND g_bdl, g_bnotes, g_bclose;

/* « v3.10.2 » → 3, 10, 2 ; renvoie <0, 0, >0 comme strcmp. */
static int VerCmp(const wchar_t *a, const wchar_t *b)
{
    for (int i = 0; i < 4; i++) {
        while (*a && !iswdigit(*a)) a++;
        while (*b && !iswdigit(*b)) b++;
        long x = *a ? wcstol(a, (wchar_t **)&a, 10) : 0, y = *b ? wcstol(b, (wchar_t **)&b, 10) : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

static DWORD WINAPI UpdThread(LPVOID pv)
{
    UpdInfo *u = (UpdInfo *)pv;
    size_t len = 0;
    char *buf = ReadHttp(UPD_API, &len, u->err);
    if (buf) {
        JV j; const char *ok = JVal(buf, &j, 0);
        if (ok && j.t == J_OBJ && JGetS(&j, "tag_name")) {
            u->ok = 1;
            JStrW(&j, "tag_name", u->tag, 32);
            JStrW(&j, "html_url", u->page, 512);
            const JV *as = JGet(&j, "assets");
            if (as && as->t == J_ARR)
                for (int i = 0; i < as->cnt; i++) {
                    const char *nm = JGetS(&as->v[i], "name"), *url = JGetS(&as->v[i], "browser_download_url");
                    if (nm && url && strstr(nm, ".zip")) { Utf8ToW(url, u->zip, 512); break; }
                }
        } else if (!u->err[0]) swprintf(u->err, 256, TR(L"réponse inattendue du serveur"));
        JFree(&j); free(buf);
    }
    PostMessageW(g_main, WM_APP_UPD, 0, (LPARAM)u);
    return 0;
}

static void UpdCheck(int manual)
{
    UpdInfo *u = (UpdInfo *)calloc(1, sizeof *u);
    if (!u) return;
    u->manual = manual;
    if (g_set && manual) SetWindowTextW(U.updInfo, TR(L"Vérification…"));
    HANDLE h = CreateThread(NULL, 0, UpdThread, u, 0, NULL);
    if (h) CloseHandle(h); else free(u);
}

static int UpdNewer(void) { return g_upd.ok && VerCmp(g_upd.tag, APP_VER) > 0; }

static void UpdSettingsInfo(void)
{
    if (!g_set) return;
    wchar_t t[200];
    if (!g_upd.ok) {
        if (g_upd.err[0]) swprintf(t, 200, TR(L"Version %ls · vérification impossible : %ls"), APP_VER, g_upd.err);
        else swprintf(t, 200, TR(L"Version installée : %ls"), APP_VER);
    } else if (UpdNewer()) swprintf(t, 200, TR(L"Version installée : %ls · nouvelle version %ls disponible"), APP_VER, g_upd.tag);
    else swprintf(t, 200, TR(L"Version installée : %ls · à jour"), APP_VER);
    SetWindowTextW(U.updInfo, t);
}

static void BannerShow(int on)
{
    if (!g_banner) return;
    on = on && UpdNewer();
    if (on == g_bannerOn) { if (on) InvalidateRect(g_banner, NULL, TRUE); return; }
    g_bannerOn = on;
    LayoutMain();
    if (on) InvalidateRect(g_banner, NULL, TRUE);
}

static void OnUpd(UpdInfo *u)
{
    int manual = u->manual;
    g_upd = *u; free(u);
    UpdSettingsInfo();
    if (!g_upd.ok) { if (manual) Flash(TR(L"Vérification des mises à jour impossible")); return; }
    if (UpdNewer()) {
        if (manual) C.updSkip[0] = 0;               /* vérification manuelle : on montre toujours */
        BannerShow(_wcsicmp(C.updSkip, g_upd.tag) != 0);
    } else {
        BannerShow(0);
        if (manual) { wchar_t t[120]; swprintf(t, 120, TR(L"HomeRun Player est à jour (%ls)"), APP_VER); Flash(t); }
    }
}

/* ------------------------------------------------------------------ */
/* Bandeau                                                             */
/* ------------------------------------------------------------------ */
static void BannerLayout(HWND h)
{
    RECT rc; GetClientRect(h, &rc);
    int bh = rc.bottom - S(8), y = S(4), x = rc.right - S(8);
    x -= S(30);  MoveWindow(g_bclose, x, y, S(30), bh, TRUE);
    x -= S(118); MoveWindow(g_bnotes, x, y, S(112), bh, TRUE);
    x -= S(130); MoveWindow(g_bdl, x, y, S(124), bh, TRUE);
}

static LRESULT CALLBACK BannerProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    switch (msg) {
    case WM_CREATE:
        g_bdl    = Mk(h, WC_BUTTONW, TR(L"⬇  Télécharger"), BS_DEFPUSHBUTTON, 0, IDB_DL);
        g_bnotes = Mk(h, WC_BUTTONW, TR(L"Nouveautés"), BS_PUSHBUTTON, 0, IDB_NOTES);
        g_bclose = Mk(h, WC_BUTTONW, L"✕", BS_PUSHBUTTON, 0, IDB_CLOSE);
        return 0;
    case WM_SIZE: BannerLayout(h); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        HBRUSH bg = CreateSolidBrush(RGB(255, 241, 204)); FillRect(dc, &rc, bg); DeleteObject(bg);
        HBRUSH ln = CreateSolidBrush(RGB(230, 170, 60)); RECT b = { 0, rc.bottom - 1, rc.right, rc.bottom }; FillRect(dc, &b, ln); DeleteObject(ln);
        SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(60, 40, 0));
        HGDIOBJ of = SelectObject(dc, g_font);
        wchar_t t[300];
        swprintf(t, 300, TR(L"⬆  Nouvelle version de %ls disponible : %ls  (vous avez la %ls)"), APP_NAME, g_upd.tag, APP_VER);
        RECT tr = { S(12), 0, rc.right - S(300), rc.bottom };
        DrawTextW(dc, t, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        SelectObject(dc, of);
        EndPaint(h, &ps);
        return 0; }
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case IDB_DL:    ShellExecuteW(g_main, L"open", g_upd.zip[0] ? g_upd.zip : (g_upd.page[0] ? g_upd.page : UPD_PAGE), NULL, NULL, SW_SHOWNORMAL); break;
        case IDB_NOTES: ShellExecuteW(g_main, L"open", g_upd.page[0] ? g_upd.page : UPD_PAGE, NULL, NULL, SW_SHOWNORMAL); break;
        case IDB_CLOSE: wcsncpy(C.updSkip, g_upd.tag, 31); BannerShow(0); SaveConfig(); break;
        }
        SetFocus(g_main);
        return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}

static void BannerRegister(HINSTANCE hi)
{
    WNDCLASSEXW wc; memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc; wc.hInstance = hi; wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.lpfnWndProc = BannerProc; wc.lpszClassName = L"HrpBanner";
    RegisterClassExW(&wc);
}

static void BannerCreate(HWND main)
{
    g_banner = CreateWindowExW(0, L"HrpBanner", NULL, WS_CHILD | WS_CLIPCHILDREN, 0, 0, 10, 10, main, NULL, g_hi, NULL);
    if (g_banner) EnumChildWindows(g_banner, SetFontProc, (LPARAM)g_font);
}
