/*
 * pip.h — image dans l'image (PIP) : une seconde chaîne du tuner incrustée
 * dans un coin de l'image (moteur VLC : second lecteur libVLC, second tuner).
 *
 * Une seule des deux images a le son : un clic sur une image lui donne le son
 * (Tab bascule).  L'image qui a le son est aussi celle que pilotent le zapping,
 * la saisie du numéro et la liste des chaînes.  Double-clic sur l'encart (ou W) :
 * échange des deux chaînes.
 *
 * Inclus par vlcmaster.c après record.h.  État : g_pipOn, g_pipCh, g_pipActive.
 */
static const wchar_t *PIP_POS_NAMES[4] = { L"En haut à gauche", L"En haut à droite", L"En bas à gauche", L"En bas à droite" };
static const int PIP_SIZES[5] = { 20, 25, 30, 35, 40 };

static void ListSelect(int ch)
{
    if (!g_list) return;
    int v = ch >= 0 ? VisPos(ch) : -1;
    SendMessageW(g_list, LB_SETCURSEL, (WPARAM)v, 0);
}

static void PipSyncButton(void)
{
    HWND b = g_main ? GetDlgItem(g_main, IDC_PIPBTN) : NULL;
    if (b) Button_SetCheck(b, g_pipOn ? BST_CHECKED : BST_UNCHECKED);
}

/* Demande au thread libVLC d'ouvrir (stop = 0) ou d'arrêter la chaîne PIP. */
static void PipRequest(int stop)
{
    if (g_vlcState != 1 || !g_mp2) return;
    EnterCriticalSection(&g_cs);
    g_req2.has = 1; g_req2.stop = stop; g_req2.caching = C.caching;
    if (!stop && g_pipCh >= 0 && g_pipCh < g_nch) memcpy(g_req2.url, g_ch[g_pipCh].url, sizeof g_req2.url);
    strncpy(g_req2.hw, HW_OPTS[C.hw].val, sizeof g_req2.hw - 1);
    LeaveCriticalSection(&g_cs);
    SetEvent(g_wake);
}

/* Encart dans le coin choisi de la zone vidéo (suit fenêtre, plein écran, bordure fine). */
static void PipPlace(void)
{
    if (!g_pip) return;
    if (!g_pipOn || g_quitting || !IsWindowVisible(g_main) || IsIconic(g_main) || !IsWindowVisible(g_host)) {
        ShowWindow(g_pip, SW_HIDE); return;
    }
    RECT r; GetWindowRect(g_host, &r);
    int W = r.right - r.left, H = r.bottom - r.top, m = S(14), b = S(3);
    int w = W * C.pipSize / 100;
    if (w < S(160)) w = S(160);
    int h = w * 9 / 16, fw = w + 2 * b, fh = h + 2 * b;
    if (fw > W - 2 * m || fh > H - 2 * m) { ShowWindow(g_pip, SW_HIDE); return; }
    int x = (C.pipCorner & 1) ? r.right - m - fw : r.left + m;
    int y = (C.pipCorner & 2) ? r.bottom - m - fh : r.top + m;
    SetWindowPos(g_pip, HWND_TOP, x, y, fw, fh, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    SetWindowPos(g_pipVid, NULL, b, b, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    if (g_hud && IsWindowVisible(g_hud))     /* le HUD reste au-dessus */
        SetWindowPos(g_hud, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

static void PipRefresh(void)
{
    if (g_pip) InvalidateRect(g_pip, NULL, TRUE);
    if (g_pipVid) InvalidateRect(g_pipVid, NULL, TRUE);
    if (g_list) InvalidateRect(g_list, NULL, FALSE);
    UpdateStatus();
}

static void PipPlay(int ch)
{
    if (ch < 0 || ch >= g_nch) return;
    if (g_engine != ENG_VLC) { Flash(TR(L"PIP : disponible uniquement avec le moteur VLC")); PipSyncButton(); return; }
    g_pipOn = 1; C.pip = 1;
    g_pipCh = ch; C.pipKey = g_ch[ch].key;
    g_pipPlaying = 0; g_pipRetries = 0;
    KillTimer(g_main, TMR_PIPRETRY);
    swprintf(g_pipMsg, 200, TR(L"Ouverture de « %ls »…"), g_ch[ch].name);
    wcscpy(g_pipState, TR(L"Ouverture…"));
    PipPlace();
    PipRequest(0);
    if (PipHasSound()) { ListSelect(ch); ShowHud(); }
    PipSyncButton();
    PipRefresh();
}

static void PipOff(void)
{
    if (!g_pipOn) { PipSyncButton(); return; }
    g_pipOn = 0; C.pip = 0; g_pipActive = 0; g_pipPlaying = 0;
    KillTimer(g_main, TMR_PIPRETRY);
    PipRequest(1);
    if (g_pip) ShowWindow(g_pip, SW_HIDE);
    ApplyAudio();
    ListSelect(g_cur);
    PipSyncButton();
    PipRefresh();
    Flash(TR(L"PIP fermé"));
}

/* Chaîne proposée à l'ouverture du PIP : la dernière utilisée, sinon la suivante. */
static int PipDefaultCh(void)
{
    if (C.pipKey)
        for (int v = 0; v < g_nvis; v++) if (g_ch[g_vis[v]].key == C.pipKey && g_vis[v] != g_cur) return g_vis[v];
    if (!g_nvis) return -1;
    int p = VisPos(g_cur);
    return g_vis[(p < 0 ? 0 : p + 1) % g_nvis];
}

static void PipToggle(void)
{
    if (g_pipOn) PipOff();
    else {
        int ch = PipDefaultCh();
        if (ch < 0) { Flash(TR(L"Choisissez d'abord une chaîne")); PipSyncButton(); return; }
        PipPlay(ch);
    }
}

/* Reprise au démarrage si le PIP était ouvert à la fermeture. */
static void PipResume(void)
{
    if (!C.pip || g_pipOn || g_engine != ENG_VLC || g_vlcState != 1 || !g_mp2 || !g_nvis) return;
    int ch = PipDefaultCh();
    if (ch >= 0) PipPlay(ch);
}

/* Donne le son (et le zapping) à l'image principale (0) ou au PIP (1). */
static void PipSetActive(int a)
{
    if (!g_pipOn) a = 0;
    if (a == g_pipActive) return;
    g_pipActive = a;
    ApplyAudio();
    ListSelect(HudCh());
    int ch = HudCh();
    wchar_t t[200];
    swprintf(t, 200, a ? TR(L"Son : PIP — %ls") : TR(L"Son : image principale — %ls"), ch >= 0 && ch < g_nch ? g_ch[ch].name : L"");
    PipRefresh();
    Flash(t);
}

/* Échange des deux chaînes ; le son suit l'image qu'il accompagnait. */
static void PipSwap(void)
{
    if (!g_pipOn || g_pipCh < 0 || g_cur < 0) return;
    if (g_fileMode) { Flash(TR(L"Inversion impossible pendant la lecture d'un enregistrement")); return; }
    int a = g_cur, b = g_pipCh;
    g_pipActive = !g_pipActive;
    PlayChannel(b);
    PipPlay(a);
    ApplyAudio();
    ListSelect(HudCh());
    PipRefresh();
}

/* Joue une chaîne dans l'image qui a le son. */
static void PlayActive(int ch)
{
    if (PipHasSound()) PipPlay(ch);
    else PlayChannel(ch);
}

static void OnPipEvent(int type, int val)
{
    if (!g_pipOn) return;
    switch (type) {
    case EV_Opening: g_pipPlaying = 0; wcscpy(g_pipState, TR(L"Ouverture…")); break;
    case EV_Buffering:
        if (!g_pipPlaying) swprintf(g_pipState, 96, TR(L"Mise en mémoire %d %%"), val);
        break;
    case EV_Playing:
        g_pipPlaying = 1; g_pipRetries = 0;
        wcscpy(g_pipState, TR(L"En lecture"));
        ApplyAudio(); ApplyVideoOpts();
        if (PipHasSound()) SetTimer(g_main, TMR_TRACKS, 1500, NULL);
        EnumChildWindows(g_pipVid, DisableProc, 0);
        break;
    case EV_Vout:
        EnumChildWindows(g_pipVid, DisableProc, 0);
        ApplyAudio(); ApplyVideoOpts();
        break;
    case EV_Paused: wcscpy(g_pipState, TR(L"En pause")); break;
    case EV_Stopped: g_pipPlaying = 0; wcscpy(g_pipState, TR(L"Arrêté")); break;
    case EV_EndReached:
    case EV_Error:
        g_pipPlaying = 0;
        if (g_pipRetries < 4) {
            g_pipRetries++;
            swprintf(g_pipMsg, 200, TR(L"%ls — nouvelle tentative…"), type == EV_Error ? TR(L"Erreur de lecture") : TR(L"Flux interrompu"));
            SetTimer(g_main, TMR_PIPRETRY, 2000u * (unsigned)g_pipRetries, NULL);
        } else wcscpy(g_pipMsg, TR(L"Impossible de lire cette chaîne en PIP\n(tuner libre ?)"));
        wcscpy(g_pipState, type == EV_Error ? TR(L"Erreur de lecture") : TR(L"Fin du flux"));
        break;
    }
    if (g_pipVid) InvalidateRect(g_pipVid, NULL, TRUE);
    if (PipHasSound()) UpdateStatus();
}

/* ------------------------------------------------------------------ */
/* Menu de l'encart et commandes                                       */
/* ------------------------------------------------------------------ */
static void PipMenu(int x, int y)
{
    HMENU m = CreatePopupMenu(), pos = CreatePopupMenu(), sz = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | (PipHasSound() ? MF_CHECKED : 0), IDM_PIPSOUND, TR(L"Son du PIP\tTab"));
    AppendMenuW(m, MF_STRING | (g_fileMode ? MF_GRAYED : 0), IDM_PIPSWAP, TR(L"Inverser avec l'image principale\tW"));
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    for (int i = 0; i < 4; i++)
        AppendMenuW(pos, MF_STRING | (C.pipCorner == i ? MF_CHECKED : 0), IDM_PIPPOS0 + i, TR(PIP_POS_NAMES[i]));
    for (int i = 0; i < 5; i++) {
        wchar_t t[16]; swprintf(t, 16, L"%d %%", PIP_SIZES[i]);
        AppendMenuW(sz, MF_STRING | (C.pipSize == PIP_SIZES[i] ? MF_CHECKED : 0), IDM_PIPSIZE0 + i, t);
    }
    AppendMenuW(m, MF_POPUP, (UINT_PTR)pos, TR(L"Position"));
    AppendMenuW(m, MF_POPUP, (UINT_PTR)sz, TR(L"Taille"));
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, IDM_PIPOFF, TR(L"Fermer le PIP\tX"));
    TrackPopupMenu(m, TPM_RIGHTBUTTON, x, y, 0, g_main, NULL);
    DestroyMenu(m);
}

/* Commandes PIP (menus, bouton) ; renvoie 1 si traitée. */
static int PipCommand(int id)
{
    if (id == IDM_PIPCH) { PipPlay(g_menuCh); return 1; }
    if (id == IDM_PIPSWAP) { PipSwap(); return 1; }
    if (id == IDM_PIPOFF) { PipOff(); return 1; }
    if (id == IDM_PIPTOGGLE) { PipToggle(); return 1; }
    if (id == IDM_PIPSOUND) { PipSetActive(!g_pipActive); return 1; }
    if (id >= IDM_PIPPOS0 && id < IDM_PIPPOS0 + 4) { C.pipCorner = id - IDM_PIPPOS0; PipPlace(); SettingsSync(); return 1; }
    if (id >= IDM_PIPSIZE0 && id < IDM_PIPSIZE0 + 5) { C.pipSize = PIP_SIZES[id - IDM_PIPSIZE0]; PipPlace(); SettingsSync(); return 1; }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Fenêtres : cadre (liseré orange quand le PIP a le son) + zone vidéo */
/* ------------------------------------------------------------------ */
static LRESULT PipMouse(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    (void)h; (void)l;
    switch (msg) {
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_LBUTTONDOWN: PipSetActive(1); return 0;
    case WM_LBUTTONDBLCLK: PipSwap(); return 0;
    case WM_RBUTTONUP: { POINT p; GetCursorPos(&p); PipMenu(p.x, p.y); return 0; }
    case WM_MOUSEWHEEL: SetVolume(C.volume + (GET_WHEEL_DELTA_WPARAM(w) > 0 ? 5 : -5)); return 0;
    }
    return -1;
}

static LRESULT CALLBACK PipProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    LRESULT r = PipMouse(h, msg, w, l);
    if (r != -1) return r;
    switch (msg) {
    case WM_ERASEBKGND: {
        RECT rc; GetClientRect(h, &rc);
        HBRUSH b = CreateSolidBrush(PipHasSound() ? RGB(255, 150, 30) : RGB(70, 72, 82));
        FillRect((HDC)w, &rc, b); DeleteObject(b);
        return 1; }
    case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); EndPaint(h, &ps); return 0; }
    }
    return DefWindowProcW(h, msg, w, l);
}

static LRESULT CALLBACK PipVidProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    LRESULT r = PipMouse(h, msg, w, l);
    if (r != -1) return r;
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        FillRect(dc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
        if (!g_pipPlaying) {
            SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(170, 170, 178));
            HGDIOBJ of = SelectObject(dc, g_font);
            RECT t = rc; InflateRect(&t, -S(10), 0);
            RECT c = t; DrawTextW(dc, g_pipMsg, -1, &c, DT_CENTER | DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
            t.top = (rc.bottom - (c.bottom - c.top)) / 2; t.bottom = t.top + (c.bottom - c.top);
            DrawTextW(dc, g_pipMsg, -1, &t, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
            SelectObject(dc, of);
        }
        EndPaint(h, &ps);
        return 0; }
    }
    return DefWindowProcW(h, msg, w, l);
}

static void PipRegister(HINSTANCE hi)
{
    WNDCLASSEXW wc; memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc; wc.hInstance = hi; wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_HAND);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = PipProc; wc.lpszClassName = L"HrpPip";
    RegisterClassExW(&wc);
    wc.lpfnWndProc = PipVidProc; wc.lpszClassName = L"HrpPipVideo";
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassExW(&wc);
}

/* Créé caché avant le démarrage de libVLC (le second lecteur dessine dans g_pipVid). */
static void PipCreate(HWND owner)
{
    g_pip = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"HrpPip", NULL, WS_POPUP | WS_CLIPCHILDREN,
                            0, 0, 10, 10, owner, NULL, g_hi, NULL);
    if (g_pip) g_pipVid = CreateWindowExW(0, L"HrpPipVideo", NULL, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                                          0, 0, 10, 10, g_pip, NULL, g_hi, NULL);
}
