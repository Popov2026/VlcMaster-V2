/*
 * guide_ui.h — Guide TV : fenêtre grille (inclus dans vlcmaster.c)
 *
 * Grille horaire : une ligne par chaîne visible, une colonne par tranche
 * de temps ; ligne rouge = maintenant. Clic = détails, double-clic ou
 * Entrée = regarder la chaîne. Molette = chaînes, Maj+molette = heures.
 */

enum { IDG_BACK = 700, IDG_NOW, IDG_FWD, IDG_WATCH, IDG_REFRESH, IDG_INFO, IDG_DET };

static HWND      g_gbtn[5], g_ginfo, g_gdet;
static HFONT     g_gfBold;
static long long g_gt0, g_gtmin, g_gtmax;
static int       g_gtop;
static int       g_selCh = -1;
static long long g_selSt;

static int GNameW(void) { return S(176); }
static int GRowH(void)  { return S(48); }
static int GHdrH(void)  { return S(30); }
static int GPpm(void)   { return S(5); }        /* pixels par minute : 30 min = 150 px */

static long long Floor30(long long t) { return t - (((t % 1800) + 1800) % 1800); }

static void GridRange(void)
{
    long long now = NowEpoch();
    g_gtmin = Floor30(now) - 3600;
    g_gtmax = g_gdEnd > now ? g_gdEnd : now + 6 * 3600;
    for (int i = 0; i < g_ngd; i++)
        if (g_gd[i].n && Floor30(g_gd[i].p[0].st) < g_gtmin && g_gd[i].p[0].en > now - 6 * 3600)
            g_gtmin = Floor30(g_gd[i].p[0].st);
}

static void GridScroll(void)
{
    if (!g_grid) return;
    RECT rc; GetClientRect(g_grid, &rc);
    GridRange();
    int rowsVis = (rc.bottom - GHdrH()) / GRowH(); if (rowsVis < 1) rowsVis = 1;
    if (g_gtop > g_nvis - rowsVis) g_gtop = g_nvis - rowsVis;
    if (g_gtop < 0) g_gtop = 0;
    SCROLLINFO si = { sizeof si, SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL, 0, g_nvis > 0 ? g_nvis - 1 : 0, (UINT)rowsVis, g_gtop, 0 };
    SetScrollInfo(g_grid, SB_VERT, &si, TRUE);

    int visMin = (rc.right - GNameW()) / GPpm(); if (visMin < 30) visMin = 30;
    long long last = g_gtmax - (long long)visMin * 60;
    if (g_gt0 > last) g_gt0 = last;
    if (g_gt0 < g_gtmin) g_gt0 = g_gtmin;
    SCROLLINFO h = { sizeof h, SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL, 0,
                     (int)((g_gtmax - g_gtmin) / 60), (UINT)visMin, (int)((g_gt0 - g_gtmin) / 60), 0 };
    SetScrollInfo(g_grid, SB_HORZ, &h, TRUE);
    InvalidateRect(g_grid, NULL, FALSE);
}

static void GridGoNow(void) { g_gt0 = Floor30(NowEpoch()) - 1800; GridScroll(); }

static void GridDetails(void)
{
    if (!g_gdet) return;
    wchar_t t[2048] = L"";
    if (g_selCh >= 0 && g_selCh < g_nch) {
        const GuideCh *g = GuideForCh(g_selCh);
        const Prog *p = NULL;
        if (g) for (int i = 0; i < g->n; i++) if (g->p[i].st == g_selSt) { p = &g->p[i]; break; }
        if (p) {
            wchar_t a[8], b[8]; FmtHM(p->st, a); FmtHM(p->en, b);
            SYSTEMTIME d; EpochToLocal(p->st, &d);
            static const wchar_t *jours[7] = { L"dim.", L"lun.", L"mar.", L"mer.", L"jeu.", L"ven.", L"sam." };
            swprintf(t, 2048, L"%ls%ls%ls\r\n%ls %02u/%02u   %ls – %ls   (%lld min)   ·   %ls\r\n\r\n%ls",
                     p->title ? p->title : L"(sans titre)",
                     p->ep ? L"  —  " : L"", p->ep ? p->ep : L"",
                     jours[d.wDayOfWeek % 7], d.wDay, d.wMonth, a, b, (p->en - p->st) / 60,
                     g_ch[g_selCh].name, p->syn ? p->syn : L"");
        } else swprintf(t, 2048, L"%ls\r\nAucun programme sélectionné.", g_ch[g_selCh].name);
    } else wcscpy(t, L"Cliquez sur un programme pour voir ses détails ; double-clic pour regarder la chaîne.");
    SetWindowTextW(g_gdet, t);
}

/* Sélectionne le programme en cours de la chaîne ci. */
static void GridSelectNow(int ci)
{
    g_selCh = ci; g_selSt = 0;
    const Prog *p = ProgAt(GuideForCh(ci), NowEpoch(), NULL);
    if (p) g_selSt = p->st;
    GridDetails();
    if (g_grid) InvalidateRect(g_grid, NULL, FALSE);
}

static void FillR(HDC dc, int l, int t, int r, int b, COLORREF c)
{
    RECT x = { l, t, r, b };
    HBRUSH br = CreateSolidBrush(c); FillRect(dc, &x, br); DeleteObject(br);
}

static void GridPaint(HDC dc, int W, int H)
{
    const COLORREF cBg = RGB(22, 23, 28), cHdr = RGB(30, 31, 38), cName = RGB(34, 36, 43), cNameCur = RGB(0, 70, 130),
                   cProg = RGB(46, 49, 59), cNowP = RGB(0, 84, 155), cSel = RGB(0, 120, 215), cPast = RGB(33, 34, 40),
                   cTxt = RGB(236, 236, 242), cSub = RGB(165, 168, 180), cDim = RGB(110, 112, 122), cLine = RGB(52, 54, 63);
    long long now = NowEpoch();
    int nw = GNameW(), hh = GHdrH(), rh = GRowH(), ppm = GPpm();
    FillR(dc, 0, 0, W, H, cBg);
    SetBkMode(dc, TRANSPARENT);

    /* En-tête : date + graduations toutes les 30 min */
    FillR(dc, 0, 0, W, hh, cHdr);
    SelectObject(dc, g_gfBold);
    SYSTEMTIME d; EpochToLocal(g_gt0 > 0 ? g_gt0 : now, &d);
    static const wchar_t *jours[7] = { L"Dimanche", L"Lundi", L"Mardi", L"Mercredi", L"Jeudi", L"Vendredi", L"Samedi" };
    wchar_t day[48]; swprintf(day, 48, L"%ls %02u/%02u", jours[d.wDayOfWeek % 7], d.wDay, d.wMonth);
    SetTextColor(dc, cTxt);
    RECT dr = { S(10), 0, nw, hh }; DrawTextW(dc, day, -1, &dr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, g_font);
    for (long long t = Floor30(g_gt0); ; t += 1800) {
        int x = nw + (int)((t - g_gt0) * ppm / 60);
        if (x > W) break;
        if (x < nw) continue;
        FillR(dc, x, hh - S(8), x + 1, hh, cDim);
        wchar_t hm[8]; FmtHM(t, hm);
        SetTextColor(dc, cSub);
        RECT tr = { x + S(5), 0, x + S(80), hh - S(4) };
        DrawTextW(dc, hm, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    /* Lignes de chaînes */
    for (int r = 0; ; r++) {
        int v = g_gtop + r, y = hh + r * rh;
        if (v >= g_nvis || y >= H) break;
        int ci = g_vis[v];
        const Channel *c = &g_ch[ci];
        FillR(dc, 0, y, nw, y + rh - 1, ci == g_cur ? cNameCur : cName);
        wchar_t num[16]; const wchar_t *name = c->name;
        if (c->num[0]) wcscpy(num, c->num); else swprintf(num, 16, L"%d", v + 1);
        size_t n = wcslen(num);
        if (!wcsncmp(name, num, n) && name[n] == L' ') { name += n; while (*name == L' ') name++; }
        SelectObject(dc, g_gfBold);
        SetTextColor(dc, RGB(64, 170, 255));
        RECT nr = { S(8), y, S(44), y + rh - 1 }; DrawTextW(dc, num, -1, &nr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SetTextColor(dc, cTxt);
        RECT mr = { S(54), y, nw - S(6), y + rh - 1 }; DrawTextW(dc, name, -1, &mr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

        const GuideCh *g = GuideForCh(ci);
        int any = 0;
        if (g) for (int i = 0; i < g->n; i++) {
            const Prog *p = &g->p[i];
            if (p->en <= g_gt0) continue;
            int x0 = nw + (int)((p->st - g_gt0) * ppm / 60), x1 = nw + (int)((p->en - g_gt0) * ppm / 60);
            if (x0 > W) break;
            if (x0 < nw) x0 = nw;
            if (x1 - x0 < 3) continue;
            any = 1;
            int sel = (ci == g_selCh && p->st == g_selSt), cur = (p->st <= now && now < p->en);
            FillR(dc, x0 + 1, y + 1, x1 - 1, y + rh - 2, sel ? cSel : cur ? cNowP : p->en <= now ? cPast : cProg);
            if (cur) {   /* avancement du programme en cours */
                int px = x0 + 1 + (int)((long long)(x1 - x0 - 2) * (now - p->st) / (p->en - p->st));
                FillR(dc, x0 + 1, y + rh - S(4), px, y + rh - 2, RGB(64, 170, 255));
            }
            if (x1 - x0 > S(18)) {
                RECT tr = { x0 + S(7), y + S(4), x1 - S(5), y + S(4) + rh / 2 };
                SelectObject(dc, g_gfBold);
                SetTextColor(dc, p->en <= now && !sel ? cSub : cTxt);
                DrawTextW(dc, p->title ? p->title : L"—", -1, &tr, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
                wchar_t a[8], b[8], tm[48]; FmtHM(p->st, a); FmtHM(p->en, b);
                swprintf(tm, 48, L"%ls – %ls", a, b);
                RECT sr = { x0 + S(7), y + rh / 2, x1 - S(5), y + rh - S(5) };
                SelectObject(dc, g_fontSmall);
                SetTextColor(dc, sel ? RGB(220, 235, 255) : cSub);
                DrawTextW(dc, tm, -1, &sr, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            }
        }
        if (!any) {
            SelectObject(dc, g_fontSmall); SetTextColor(dc, cDim);
            RECT er = { nw + S(10), y, W, y + rh - 1 };
            DrawTextW(dc, g_ngd ? L"Pas de programme pour cette chaîne" : g_gdInfo, -1, &er,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        }
        FillR(dc, 0, y + rh - 1, W, y + rh, cLine);
    }
    FillR(dc, nw - 1, hh, nw, H, cLine);

    /* Maintenant */
    int xn = nw + (int)((now - g_gt0) * ppm / 60);
    if (xn >= nw && xn < W) {
        FillR(dc, xn - 1, hh - S(6), xn + 1, H, RGB(232, 56, 56));
        FillR(dc, xn - S(4), hh - S(6), xn + S(4), hh - S(2), RGB(232, 56, 56));
    }
}

static int GridHit(int x, int y, int *ci, long long *st)
{
    *ci = -1; *st = 0;
    if (y < GHdrH()) return 0;
    int v = g_gtop + (y - GHdrH()) / GRowH();
    if (v < 0 || v >= g_nvis) return 0;
    *ci = g_vis[v];
    if (x < GNameW()) return 1;
    long long t = g_gt0 + (long long)(x - GNameW()) * 60 / GPpm();
    const GuideCh *g = GuideForCh(*ci);
    if (g) for (int i = 0; i < g->n; i++) if (g->p[i].st <= t && t < g->p[i].en) { *st = g->p[i].st; break; }
    return 1;
}

static void GridMoveSel(int drow, int dprog)
{
    if (!g_nvis) return;
    int v = VisPos(g_selCh);
    if (v < 0) { GridSelectNow(g_vis[VisPos(g_cur) >= 0 ? VisPos(g_cur) : 0]); return; }
    if (drow) {
        v += drow; if (v < 0) v = 0; if (v >= g_nvis) v = g_nvis - 1;
        long long ref = g_selSt ? g_selSt : NowEpoch();
        g_selCh = g_vis[v]; g_selSt = 0;
        const GuideCh *g = GuideForCh(g_selCh);
        if (g) for (int i = 0; i < g->n; i++) if (g->p[i].en > ref) { g_selSt = g->p[i].st; break; }
        RECT rc; GetClientRect(g_grid, &rc);
        int rowsVis = (rc.bottom - GHdrH()) / GRowH();
        if (v < g_gtop) g_gtop = v;
        if (rowsVis > 0 && v >= g_gtop + rowsVis) g_gtop = v - rowsVis + 1;
    }
    if (dprog) {
        const GuideCh *g = GuideForCh(g_selCh);
        if (g && g->n) {
            int k = 0;
            for (int i = 0; i < g->n; i++) if (g->p[i].st == g_selSt) { k = i; break; }
            k += dprog; if (k < 0) k = 0; if (k >= g->n) k = g->n - 1;
            g_selSt = g->p[k].st;
            RECT rc; GetClientRect(g_grid, &rc);
            long long visEnd = g_gt0 + (long long)(rc.right - GNameW()) * 60 / GPpm();
            if (g->p[k].st < g_gt0) g_gt0 = Floor30(g->p[k].st);
            else if (g->p[k].st > visEnd - 1800) g_gt0 = Floor30(g->p[k].st) - 1800;
        }
    }
    GridScroll();
    GridDetails();
}

static LRESULT CALLBACK GridProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_SIZE: GridScroll(); return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        HDC md = CreateCompatibleDC(dc);
        HBITMAP bm = CreateCompatibleBitmap(dc, rc.right > 0 ? rc.right : 1, rc.bottom > 0 ? rc.bottom : 1);
        HGDIOBJ ob = SelectObject(md, bm), of = SelectObject(md, g_font);
        GridPaint(md, rc.right, rc.bottom);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, md, 0, 0, SRCCOPY);
        SelectObject(md, of); SelectObject(md, ob); DeleteObject(bm); DeleteDC(md);
        EndPaint(h, &ps);
        return 0; }
    case WM_VSCROLL: {
        SCROLLINFO si = { sizeof si, SIF_ALL }; GetScrollInfo(h, SB_VERT, &si);
        switch (LOWORD(w)) {
        case SB_LINEUP: g_gtop--; break;
        case SB_LINEDOWN: g_gtop++; break;
        case SB_PAGEUP: g_gtop -= (int)si.nPage; break;
        case SB_PAGEDOWN: g_gtop += (int)si.nPage; break;
        case SB_THUMBTRACK: case SB_THUMBPOSITION: g_gtop = si.nTrackPos; break;
        case SB_TOP: g_gtop = 0; break;
        case SB_BOTTOM: g_gtop = g_nvis; break;
        }
        GridScroll(); return 0; }
    case WM_HSCROLL: {
        SCROLLINFO si = { sizeof si, SIF_ALL }; GetScrollInfo(h, SB_HORZ, &si);
        switch (LOWORD(w)) {
        case SB_LINELEFT: g_gt0 -= 1800; break;
        case SB_LINERIGHT: g_gt0 += 1800; break;
        case SB_PAGELEFT: g_gt0 -= (long long)si.nPage * 60; break;
        case SB_PAGERIGHT: g_gt0 += (long long)si.nPage * 60; break;
        case SB_THUMBTRACK: case SB_THUMBPOSITION: g_gt0 = g_gtmin + (long long)si.nTrackPos * 60; break;
        }
        GridScroll(); return 0; }
    case WM_MOUSEWHEEL: {
        int notch = GET_WHEEL_DELTA_WPARAM(w) > 0 ? -1 : 1;
        if (GET_KEYSTATE_WPARAM(w) & MK_SHIFT) g_gt0 += notch * 1800;
        else g_gtop += notch * 3;
        GridScroll(); return 0; }
    case WM_MOUSEHWHEEL:
        g_gt0 += (GET_WHEEL_DELTA_WPARAM(w) > 0 ? 1 : -1) * 1800;
        GridScroll(); return 0;
    case WM_LBUTTONDOWN: {
        SetFocus(h);
        int ci; long long st;
        if (GridHit(GET_X_LPARAM(l), GET_Y_LPARAM(l), &ci, &st)) {
            if (st) { g_selCh = ci; g_selSt = st; GridDetails(); InvalidateRect(h, NULL, FALSE); }
            else GridSelectNow(ci);
        }
        return 0; }
    case WM_LBUTTONDBLCLK: {
        int ci; long long st;
        if (GridHit(GET_X_LPARAM(l), GET_Y_LPARAM(l), &ci, &st) && ci >= 0) PlayChannel(ci);
        InvalidateRect(h, NULL, FALSE);
        return 0; }
    case WM_GETDLGCODE: return DLGC_WANTARROWS | DLGC_WANTCHARS;
    case WM_KEYDOWN:
        switch (w) {
        case VK_UP: GridMoveSel(-1, 0); return 0;
        case VK_DOWN: GridMoveSel(1, 0); return 0;
        case VK_LEFT: GridMoveSel(0, -1); return 0;
        case VK_RIGHT: GridMoveSel(0, 1); return 0;
        case VK_PRIOR: g_gtop -= 8; GridScroll(); return 0;
        case VK_NEXT: g_gtop += 8; GridScroll(); return 0;
        case VK_HOME: GridGoNow(); return 0;
        case VK_RETURN: if (g_selCh >= 0) { PlayChannel(g_selCh); InvalidateRect(h, NULL, FALSE); } return 0;
        case VK_ESCAPE: DestroyWindow(g_gw); return 0;
        }
        break;
    }
    return DefWindowProcW(h, msg, w, l);
}

static void GuideLayout(int W, int H)
{
    int m = S(10), bh = S(30), detH = S(118), x = m;
    static const int bw[5] = { 92, 110, 92, 120, 110 };
    HDWP d = BeginDeferWindowPos(9);
    for (int i = 0; i < 5; i++) {
        d = DeferWindowPos(d, g_gbtn[i], NULL, x, m, S(bw[i]), bh, SWP_NOZORDER | SWP_NOACTIVATE);
        x += S(bw[i]) + S(6);
        if (i == 2) x += S(12);
    }
    d = DeferWindowPos(d, g_ginfo, NULL, x + S(10), m, W - x - S(10) - m, bh, SWP_NOZORDER | SWP_NOACTIVATE);
    int gy = m + bh + S(8);
    d = DeferWindowPos(d, g_grid, NULL, 0, gy, W, H - gy - detH, SWP_NOZORDER | SWP_NOACTIVATE);
    d = DeferWindowPos(d, g_gdet, NULL, m, H - detH + S(6), W - 2 * m, detH - S(12), SWP_NOZORDER | SWP_NOACTIVATE);
    EndDeferWindowPos(d);
}

static void GuideFonts(void)
{
    if (g_gfBold) DeleteObject(g_gfBold);
    g_gfBold = MakeFont(g_dpi, 10, FW_SEMIBOLD);
    if (g_gw) EnumChildWindows(g_gw, SetFontProc, (LPARAM)g_font);
}

static LRESULT CALLBACK GuideWinProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    switch (msg) {
    case WM_CREATE: {
        g_gw = h;
        static const wchar_t *lbl[5] = { L"◀  3 h", L"Maintenant", L"3 h  ▶", L"▶  Regarder", L"Actualiser" };
        for (int i = 0; i < 5; i++) g_gbtn[i] = Mk(h, WC_BUTTONW, lbl[i], BS_PUSHBUTTON, 0, IDG_BACK + i);
        g_ginfo = Mk(h, WC_STATICW, g_gdInfo, SS_LEFT | SS_CENTERIMAGE | SS_ENDELLIPSIS | SS_NOPREFIX, 0, IDG_INFO);
        g_grid = CreateWindowExW(0, L"VlcMasterGrid", NULL, WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP,
                                 0, 0, 10, 10, h, NULL, g_hi, NULL);
        g_gdet = Mk(h, WC_EDITW, L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL, WS_EX_CLIENTEDGE, IDG_DET);
        GuideFonts();
        return 0; }
    case WM_SIZE: if (w != SIZE_MINIMIZED) GuideLayout(LOWORD(l), HIWORD(l)); return 0;
    case WM_GETMINMAXINFO: { MINMAXINFO *mm = (MINMAXINFO *)l; mm->ptMinTrackSize.x = S(640); mm->ptMinTrackSize.y = S(380); return 0; }
    case WM_DPICHANGED: {
        GuideFonts();
        RECT *r = (RECT *)l;
        SetWindowPos(h, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0; }
    case WM_ACTIVATE: if (LOWORD(w) != WA_INACTIVE && g_grid) SetFocus(g_grid); return 0;
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case IDG_BACK: g_gt0 -= 3 * 3600; GridScroll(); break;
        case IDG_NOW: GridGoNow(); if (g_cur >= 0) GridSelectNow(g_cur); break;
        case IDG_FWD: g_gt0 += 3 * 3600; GridScroll(); break;
        case IDG_WATCH: if (g_selCh >= 0) PlayChannel(g_selCh); InvalidateRect(g_grid, NULL, FALSE); break;
        case IDG_REFRESH: GuideLoad(); break;
        default: return 0;
        }
        SetFocus(g_grid);
        return 0;
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY: {
        WINDOWPLACEMENT wp = { sizeof wp };
        GetWindowPlacement(h, &wp);
        C.guiderc = wp.rcNormalPosition;
        g_gw = NULL; g_grid = NULL; g_gdet = NULL; g_ginfo = NULL;
        if (g_main && !g_quitting) SetFocus(g_main);
        return 0; }
    }
    return DefWindowProcW(h, msg, w, l);
}

static void GuideRegister(HINSTANCE hi, HICON ico)
{
    WNDCLASSEXW wc; memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc; wc.hInstance = hi; wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.lpfnWndProc = GuideWinProc; wc.lpszClassName = L"VlcMasterGuideWin"; wc.hIcon = ico;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassExW(&wc);
    wc.lpfnWndProc = GridProc; wc.lpszClassName = L"VlcMasterGrid"; wc.style = CS_DBLCLKS; wc.hIcon = NULL;
    wc.hbrBackground = NULL;
    RegisterClassExW(&wc);
}

static int RectOnScreen(const RECT *r);

static void OpenGuide(void)
{
    if (g_gw) {
        if (IsIconic(g_gw)) ShowWindow(g_gw, SW_RESTORE);
        SetForegroundWindow(g_gw);
        return;
    }
    CreateWindowExW(0, L"VlcMasterGuideWin", L"Guide TV — " APP_NAME, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                    CW_USEDEFAULT, CW_USEDEFAULT, 100, 100, g_main, NULL, g_hi, NULL);
    if (!g_gw) return;
    WINDOWPLACEMENT wp = { sizeof wp };
    GetWindowPlacement(g_gw, &wp);
    if (RectOnScreen(&C.guiderc)) wp.rcNormalPosition = C.guiderc;
    else {
        RECT mr; GetWindowRect(g_main, &mr);
        int w = S(1180), hh = S(720);
        int x = (mr.left + mr.right - w) / 2, y = (mr.top + mr.bottom - hh) / 2;
        if (y < 0) y = 0;
        if (x < 0) x = 0;
        SetRect(&wp.rcNormalPosition, x, y, x + w, y + hh);
    }
    wp.showCmd = SW_SHOWNORMAL;
    SetWindowPlacement(g_gw, &wp);
    GridGoNow();
    if (g_cur >= 0) {
        GridSelectNow(g_cur);
        int v = VisPos(g_cur);
        if (v > 2) { g_gtop = v - 2; GridScroll(); }
    } else GridDetails();
    SetFocus(g_grid);
    if (!g_ngd) GuideLoad();
}

/* Rafraîchit tout ce qui affiche le guide. */
static void GuideRefreshUI(void)
{
    if (g_list) InvalidateRect(g_list, NULL, FALSE);
    if (g_ginfo) SetWindowTextW(g_ginfo, g_gdInfo);
    if (g_grid) { GridScroll(); GridDetails(); }
    if (g_set && U.guideInfo) SetWindowTextW(U.guideInfo, g_gdInfo);
    UpdateStatus();
}
