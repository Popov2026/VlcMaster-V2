/*
 * mpc.h — moteurs de lecture MPC-HC et MPC-BE (au choix de libVLC).
 *
 * MPC est lancé en mode piloté (« /slave <hwnd> ») : il nous envoie ses
 * notifications par WM_COPYDATA (API MpcApi.h, commune aux deux lecteurs) et
 * reçoit nos commandes de la même façon.  Sa fenêtre est incrustée dans la
 * zone vidéo (SetParent) et désactivée, pour que souris et clavier restent à
 * l'appli ; elle est dimensionnée de sorte que ses barres (recherche, outils,
 * état) débordent hors de la zone visible : on ne voit que l'image, sans
 * toucher aux réglages de MPC.  Le volume passe par le mélangeur Windows
 * (session audio du processus MPC), MPC-BE n'ayant pas de commande « volume ».
 *
 * Inclus par vlcmaster.c après le moteur libVLC.
 */
enum { ENG_VLC = 0, ENG_MPCHC, ENG_MPCBE, ENG_COUNT };
static const wchar_t *ENG_NAMES[ENG_COUNT] = { L"VLC (libVLC intégré)", L"MPC-HC", L"MPC-BE" };

/* MpcApi.h */
enum {
    MLS_CLOSED, MLS_LOADING, MLS_LOADED, MLS_CLOSING, MLS_FAILING
};
enum { PS_PLAY = 0, PS_PAUSE = 1, PS_STOP = 2 };
#define CMD_CONNECT            0x50000000u
#define CMD_STATE              0x50000001u
#define CMD_PLAYMODE           0x50000002u
#define CMD_NOWPLAYING         0x50000003u
#define CMD_CURRENTPOSITION    0x50000007u
#define CMD_NOTIFYENDOFSTREAM  0x50000009u
#define CMD_VERSION            0x5000000Au
#define CMD_DISCONNECT         0x5000000Bu
#define CMD_OPENFILE           0xA0000000u
#define CMD_STOP               0xA0000001u
#define CMD_PLAY               0xA0000004u
#define CMD_PAUSE              0xA0000005u
#define CMD_SETPOSITION        0xA0002000u
#define CMD_SETAUDIODELAY      0xA0002001u
#define CMD_GETCURRENTPOSITION 0xA0003004u
#define CMD_GETVERSION         0xA0003006u
#define CMD_INCREASEVOLUME     0xA0004003u
#define CMD_CLOSEAPP           0xA0004006u

static int       g_engine;                 /* moteur effectif (C.engine au démarrage) */
static HANDLE    g_mpcProc;
static DWORD     g_mpcPid;
static HWND      g_mpcWnd;
static double    g_mpcPos, g_mpcDur;
static DWORD     g_mpcStart, g_mpcAudioUntil;
static wchar_t   g_mpcExe[MAX_PATH];
static int       g_mpcDelay;               /* délai audio déjà transmis (MPC démarre à 0) */

/* ------------------------------------------------------------------ */
/* Recherche de l'exécutable                                           */
/* ------------------------------------------------------------------ */
static int FileExists(const wchar_t *p) { DWORD a = GetFileAttributesW(p); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY); }

static int RegExe(HKEY root, const wchar_t *key, const wchar_t *val, wchar_t *out)
{
    DWORD sz = MAX_PATH * sizeof(wchar_t);
    if (RegGetValueW(root, key, val, RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, NULL, out, &sz) != ERROR_SUCCESS) return 0;
    return FileExists(out);
}

static int MpcFind(int eng, wchar_t *out)
{
    if (C.mpcPath[0]) { wcsncpy(out, C.mpcPath, MAX_PATH - 1); out[MAX_PATH - 1] = 0; return FileExists(out); }
    wchar_t pf[MAX_PATH], pf86[MAX_PATH];
    if (!GetEnvironmentVariableW(L"ProgramW6432", pf, MAX_PATH) && !GetEnvironmentVariableW(L"ProgramFiles", pf, MAX_PATH)) wcscpy(pf, L"C:\\Program Files");
    if (!GetEnvironmentVariableW(L"ProgramFiles(x86)", pf86, MAX_PATH)) wcscpy(pf86, L"C:\\Program Files (x86)");
    if (eng == ENG_MPCHC) {
        if (RegExe(HKEY_CURRENT_USER, L"Software\\MPC-HC\\MPC-HC", L"ExePath", out)) return 1;
        if (RegExe(HKEY_LOCAL_MACHINE, L"Software\\MPC-HC\\MPC-HC", L"ExePath", out)) return 1;
        const wchar_t *rel[] = { L"%ls\\MPC-HC\\mpc-hc64.exe", L"%ls\\K-Lite Codec Pack\\MPC-HC64\\mpc-hc64.exe",
                                 L"%ls\\MPC-HC\\mpc-hc.exe", L"%ls\\K-Lite Codec Pack\\MPC-HC\\mpc-hc.exe" };
        for (int i = 0; i < 4; i++) for (int k = 0; k < 2; k++) {
            swprintf(out, MAX_PATH, rel[i], k ? pf86 : pf);
            if (FileExists(out)) return 1;
        }
    } else {
        if (RegExe(HKEY_CURRENT_USER, L"Software\\MPC-BE", L"ExePath", out)) return 1;
        if (RegExe(HKEY_LOCAL_MACHINE, L"Software\\MPC-BE", L"ExePath", out)) return 1;
        const wchar_t *rel[] = { L"%ls\\MPC-BE\\mpc-be64.exe", L"%ls\\MPC-BE x64\\mpc-be64.exe",
                                 L"%ls\\MPC-BE\\mpc-be.exe" };
        for (int i = 0; i < 3; i++) for (int k = 0; k < 2; k++) {
            swprintf(out, MAX_PATH, rel[i], k ? pf86 : pf);
            if (FileExists(out)) return 1;
        }
    }
    out[0] = 0;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Volume : session audio Windows du processus MPC                     */
/* ------------------------------------------------------------------ */
static const GUID MY_CLSID_MMDeviceEnumerator = { 0xBCDE0395, 0xE52F, 0x467C, { 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E } };
static const GUID MY_IID_IMMDeviceEnumerator  = { 0xA95664D2, 0x9614, 0x4F35, { 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6 } };
static const GUID MY_IID_IAudioSessionManager2 = { 0x77AA99A0, 0x1BD6, 0x484F, { 0x8B, 0xC7, 0x2C, 0x65, 0x4C, 0x9A, 0x9B, 0x6F } };
static const GUID MY_IID_IAudioSessionControl2 = { 0xBFB7FF88, 0x7239, 0x4FC9, { 0x8F, 0xA2, 0x07, 0xC9, 0x50, 0xBE, 0x9C, 0x6D } };
static const GUID MY_IID_ISimpleAudioVolume    = { 0x87CE5498, 0x68D6, 0x44E5, { 0x92, 0x15, 0x6D, 0xA4, 0x7E, 0xF8, 0x83, 0xD8 } };

/* Renvoie 1 si une session du processus a été réglée. */
static int SessionVolume(DWORD pid, float vol, int mute)
{
    int done = 0;
    IMMDeviceEnumerator *en = NULL; IMMDevice *dev = NULL;
    IAudioSessionManager2 *mgr = NULL; IAudioSessionEnumerator *se = NULL;
    if (FAILED(CoCreateInstance(&MY_CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &MY_IID_IMMDeviceEnumerator, (void **)&en))) return 0;
    if (SUCCEEDED(en->lpVtbl->GetDefaultAudioEndpoint(en, eRender, eMultimedia, &dev))
        && SUCCEEDED(dev->lpVtbl->Activate(dev, &MY_IID_IAudioSessionManager2, CLSCTX_ALL, NULL, (void **)&mgr))
        && SUCCEEDED(mgr->lpVtbl->GetSessionEnumerator(mgr, &se))) {
        int n = 0;
        se->lpVtbl->GetCount(se, &n);
        for (int i = 0; i < n; i++) {
            IAudioSessionControl *c = NULL; IAudioSessionControl2 *c2 = NULL; ISimpleAudioVolume *sv = NULL;
            if (FAILED(se->lpVtbl->GetSession(se, i, &c))) continue;
            if (SUCCEEDED(c->lpVtbl->QueryInterface(c, &MY_IID_IAudioSessionControl2, (void **)&c2))) {
                DWORD p = 0;
                if (SUCCEEDED(c2->lpVtbl->GetProcessId(c2, &p)) && p == pid
                    && SUCCEEDED(c->lpVtbl->QueryInterface(c, &MY_IID_ISimpleAudioVolume, (void **)&sv))) {
                    sv->lpVtbl->SetMasterVolume(sv, vol, NULL);
                    sv->lpVtbl->SetMute(sv, mute, NULL);
                    sv->lpVtbl->Release(sv);
                    done = 1;
                }
                c2->lpVtbl->Release(c2);
            }
            c->lpVtbl->Release(c);
        }
    }
    if (se) se->lpVtbl->Release(se);
    if (mgr) mgr->lpVtbl->Release(mgr);
    if (dev) dev->lpVtbl->Release(dev);
    en->lpVtbl->Release(en);
    return done;
}

/* ------------------------------------------------------------------ */
/* Communication                                                       */
/* ------------------------------------------------------------------ */
static void MpcSend(DWORD cmd, const wchar_t *s)
{
    if (!g_mpcWnd) return;
    if (!s) s = L"";
    COPYDATASTRUCT cd;
    cd.dwData = cmd; cd.cbData = (DWORD)((wcslen(s) + 1) * sizeof(wchar_t)); cd.lpData = (void *)s;
    SendMessageTimeoutW(g_mpcWnd, WM_COPYDATA, (WPARAM)g_main, (LPARAM)&cd, SMTO_ABORTIFHUNG, 3000, NULL);
}

static void MpcAudio(void)
{
    if (!g_mpcWnd) return;
    int v = C.volume > 100 ? 100 : C.volume;
    SessionVolume(g_mpcPid, v / 100.0f, C.muted);
    /* MPC affiche un message à chaque réglage du délai : seulement s'il change. */
    if (C.delay != g_mpcDelay) {
        wchar_t b[24]; swprintf(b, 24, L"%d", C.delay);
        MpcSend(CMD_SETAUDIODELAY, b);
        g_mpcDelay = C.delay;
    }
}

/* Ne laisse voir que la vue vidéo de MPC : ses barres débordent hors de la zone. */
typedef struct { HWND parent, best; long area; RECT r; } MpcView;
static BOOL CALLBACK MpcViewProc(HWND h, LPARAM l)
{
    MpcView *v = (MpcView *)l;
    if (GetParent(h) != v->parent || !IsWindowVisible(h)) return TRUE;
    RECT r; GetWindowRect(h, &r);
    MapWindowPoints(NULL, v->parent, (POINT *)&r, 2);
    long a = (long)(r.right - r.left) * (r.bottom - r.top);
    if (a > v->area) { v->area = a; v->best = h; v->r = r; }
    return TRUE;
}

static void MpcFit(void)
{
    if (!g_mpcWnd || !g_host) return;
    RECT hc, mc, wr;
    GetClientRect(g_host, &hc);
    GetClientRect(g_mpcWnd, &mc);
    GetWindowRect(g_mpcWnd, &wr);
    MpcView v = { g_mpcWnd, NULL, 0, { 0, 0, mc.right, mc.bottom } };
    EnumChildWindows(g_mpcWnd, MpcViewProc, (LPARAM)&v);
    if (!v.best) SetRect(&v.r, 0, 0, mc.right, mc.bottom);
    /* bord non client éventuel */
    POINT o = { 0, 0 }; ClientToScreen(g_mpcWnd, &o);
    int ncl = o.x - wr.left, nct = o.y - wr.top;
    int ncw = (wr.right - wr.left) - mc.right, nch = (wr.bottom - wr.top) - mc.bottom;
    int W = hc.right + (mc.right - (v.r.right - v.r.left)) + ncw;
    int H = hc.bottom + (mc.bottom - (v.r.bottom - v.r.top)) + nch;
    int x = -v.r.left - ncl, y = -v.r.top - nct;
    POINT cur = { wr.left, wr.top }; ScreenToClient(g_host, &cur);
    if (cur.x != x || cur.y != y || wr.right - wr.left != W || wr.bottom - wr.top != H)
        SetWindowPos(g_mpcWnd, HWND_BOTTOM, x, y, W, H, SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

static void MpcEmbed(void)
{
    HWND w = g_mpcWnd;
    LONG_PTR st = GetWindowLongPtrW(w, GWL_STYLE);
    st &= ~(LONG_PTR)(WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX
                      | WS_BORDER | WS_DLGFRAME | WS_MAXIMIZE | WS_MINIMIZE);
    SetWindowLongPtrW(w, GWL_STYLE, st | WS_CHILD | WS_CLIPCHILDREN);
    LONG_PTR ex = GetWindowLongPtrW(w, GWL_EXSTYLE);
    ex &= ~(LONG_PTR)(WS_EX_APPWINDOW | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_DLGMODALFRAME | WS_EX_TOPMOST | WS_EX_STATICEDGE);
    SetWindowLongPtrW(w, GWL_EXSTYLE, ex | WS_EX_TOOLWINDOW);
    SetParent(w, g_host);
    SetWindowPos(w, HWND_BOTTOM, 0, 0, 10, 10, SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_HIDEWINDOW);
    EnableWindow(w, FALSE);           /* clics et molette => notre zone vidéo */
    MpcFit();
    if (GetForegroundWindow() != g_main && !g_quitting) SetForegroundWindow(g_main);
}

static void MpcShow(int on)
{
    if (!g_mpcWnd) return;
    if (on) { MpcFit(); ShowWindow(g_mpcWnd, SW_SHOWNA); EnumChildWindows(g_host, DisableProc, 0); }
    else ShowWindow(g_mpcWnd, SW_HIDE);
}

/* ------------------------------------------------------------------ */
/* Démarrage / arrêt                                                   */
/* ------------------------------------------------------------------ */
static void MpcStart(void)
{
    const wchar_t *nm = TR(ENG_NAMES[g_engine]);
    if (!MpcFind(g_engine, g_mpcExe)) {
        swprintf(g_vlcInfo, 512, TR(L"%ls introuvable.\nInstallez-le ou indiquez son exécutable dans \u2699 Paramètres \u203A Moteur de lecture."), nm);
        PostMessageW(g_main, WM_APP_VLCREADY, 0, 0);
        return;
    }
    wchar_t cmd[MAX_PATH + 96];
    swprintf(cmd, MAX_PATH + 96, L"\"%ls\" /new /slave %llu%ls", g_mpcExe,
             (unsigned long long)(ULONG_PTR)g_main, g_engine == ENG_MPCHC ? L" /nofocus" : L"");
    STARTUPINFOW si; memset(&si, 0, sizeof si); si.cb = sizeof si;
    si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_SHOWMINNOACTIVE;
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(g_mpcExe, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        swprintf(g_vlcInfo, 512, TR(L"Impossible de lancer %ls (erreur %lu).\n%ls"), nm, GetLastError(), g_mpcExe);
        PostMessageW(g_main, WM_APP_VLCREADY, 0, 0);
        return;
    }
    CloseHandle(pi.hThread);
    g_mpcProc = pi.hProcess; g_mpcPid = pi.dwProcessId;
    g_mpcStart = GetTickCount();
    g_workerAlive = 0;
    swprintf(g_vlcInfo, 512, TR(L"Démarrage de %ls…\n%ls"), nm, g_mpcExe);
    SetTimer(g_main, TMR_MPC, 500, NULL);
}

static void MpcQuit(void)
{
    if (!g_mpcProc) return;
    if (g_mpcWnd) {
        HWND w = g_mpcWnd;
        ShowWindow(w, SW_HIDE);
        EnableWindow(w, TRUE);
        SetParent(w, NULL);               /* détaché : la fermeture ne dépend plus de notre fenêtre */
        MpcSend(CMD_CLOSEAPP, NULL);
        g_mpcWnd = NULL;
    }
    if (WaitForSingleObject(g_mpcProc, 4000) != WAIT_OBJECT_0) TerminateProcess(g_mpcProc, 0);
    CloseHandle(g_mpcProc); g_mpcProc = NULL;
}

/* Perte de MPC (fermé, planté). */
static void MpcLost(const wchar_t *why)
{
    g_mpcWnd = NULL;
    if (g_mpcProc) { CloseHandle(g_mpcProc); g_mpcProc = NULL; }
    KillTimer(g_main, TMR_MPC);
    if (g_quitting) return;
    g_vlcState = -1; g_playing = 0; g_paused = 0;
    swprintf(g_vlcInfo, 512, TR(L"%ls %ls.\nRedémarrez l'application pour le relancer."), TR(ENG_NAMES[g_engine]), why);
    SetHostMsg(g_vlcInfo);
    SetState(TR(L"Lecteur arrêté"));
    SettingsSync();
}

/* Toutes les 500 ms : attente de connexion, position, recadrage, volume. */
static void MpcTick(void)
{
    if (g_mpcProc && WaitForSingleObject(g_mpcProc, 0) == WAIT_OBJECT_0) { MpcLost(TR(L"s'est fermé")); return; }
    if (!g_mpcWnd) {
        if (g_vlcState == 0 && GetTickCount() - g_mpcStart > 20000) {
            if (g_mpcProc) TerminateProcess(g_mpcProc, 0);
            MpcLost(TR(L"ne répond pas (pas de connexion /slave)"));
        }
        return;
    }
    MpcFit();
    if (g_playing && g_fileMode) MpcSend(CMD_GETCURRENTPOSITION, NULL);
    if (g_mpcAudioUntil && (LONG)(g_mpcAudioUntil - GetTickCount()) > 0) MpcAudio();
    else g_mpcAudioUntil = 0;
}

static void MpcOpen(const wchar_t *url)
{
    g_mpcPos = 0; g_mpcDur = 0;
    MpcSend(CMD_OPENFILE, url);
}

/* Notifications de MPC (WM_COPYDATA). */
static void MpcOnCopyData(const COPYDATASTRUCT *cd)
{
    wchar_t s[1024] = L"";
    if (cd->lpData && cd->cbData) {
        size_t n = cd->cbData / sizeof(wchar_t); if (n > 1023) n = 1023;
        memcpy(s, cd->lpData, n * sizeof(wchar_t)); s[n] = 0;
    }
    switch ((DWORD)cd->dwData) {
    case CMD_CONNECT:
        if (g_mpcWnd) break;
        g_mpcWnd = (HWND)(ULONG_PTR)_wcstoui64(s, NULL, 10);
        if (!IsWindow(g_mpcWnd)) { g_mpcWnd = NULL; break; }
        MpcEmbed();
        /* Volume interne de MPC au maximum (il peut avoir été laissé à 0) :
           c'est le mélangeur Windows qui règle ensuite le niveau.  Pas de
           commande « volume = x » dans l'API de MPC-BE : 25 crans de +5 %. */
        for (int i = 0; i < 25; i++) MpcSend(CMD_INCREASEVOLUME, NULL);
        swprintf(g_vlcInfo, 512, L"%ls\n%ls", TR(ENG_NAMES[g_engine]), g_mpcExe);
        MpcSend(CMD_GETVERSION, NULL);
        PostMessageW(g_main, WM_APP_VLCREADY, 1, 0);
        break;
    case CMD_VERSION:
        swprintf(g_vlcInfo, 512, L"%ls %ls\n%ls", TR(ENG_NAMES[g_engine]), s, g_mpcExe);
        SettingsSync();
        break;
    case CMD_STATE: {
        int st = _wtoi(s);
        if (st == MLS_LOADING) OnVlcEvent(EV_Opening, 0);
        else if (st == MLS_FAILING) { MpcShow(0); OnVlcEvent(EV_Error, 0); }
        break; }
    case CMD_PLAYMODE: {
        int pm = _wtoi(s);
        if (pm == PS_PLAY) {
            MpcShow(1);
            if (!g_playing) g_mpcAudioUntil = GetTickCount() + 6000;
            OnVlcEvent(EV_Playing, 0);
        } else if (pm == PS_PAUSE) OnVlcEvent(EV_Paused, 0);
        else if (pm == PS_STOP) { if (g_userStopped) MpcShow(0); OnVlcEvent(EV_Stopped, 0); }
        break; }
    case CMD_NOWPLAYING: {
        wchar_t *last = wcsrchr(s, L'|');
        g_mpcDur = last ? _wtof(last + 1) : 0;
        break; }
    case CMD_CURRENTPOSITION: g_mpcPos = _wtof(s); break;
    case CMD_NOTIFYENDOFSTREAM: OnVlcEvent(EV_EndReached, 0); break;
    case CMD_DISCONNECT: MpcLost(TR(L"s'est fermé")); break;
    }
}

/* ------------------------------------------------------------------ */
/* Interface commune aux moteurs                                       */
/* ------------------------------------------------------------------ */
static void EngStart(void)
{
    if (g_engine == ENG_VLC) { StartVlc(); return; }
    if (g_mpcProc || g_vlcState == 1) return;
    g_vlcState = 0;
    MpcStart();
}

static long long EngTime(void)
{
    if (g_vlcState != 1) return 0;
    return g_engine ? (long long)(g_mpcPos * 1000) : p_libvlc_media_player_get_time(g_mp);
}

static long long EngLength(void)
{
    if (g_vlcState != 1) return 0;
    return g_engine ? (long long)(g_mpcDur * 1000) : p_libvlc_media_player_get_length(g_mp);
}

static void EngSetTime(long long ms)
{
    if (g_vlcState != 1) return;
    if (ms < 0) ms = 0;
    if (g_engine) {
        wchar_t b[32]; swprintf(b, 32, L"%lld", ms / 1000);
        MpcSend(CMD_SETPOSITION, b);
        g_mpcPos = ms / 1000.0;
    } else p_libvlc_media_player_set_time(g_mp, ms);
}

static void EngSetPause(int on)
{
    if (g_vlcState != 1) return;
    if (g_engine) MpcSend(on ? CMD_PAUSE : CMD_PLAY, NULL);
    else p_libvlc_media_player_set_pause(g_mp, on);
}

static int EngVideoSize(unsigned *w, unsigned *h)
{
    if (g_vlcState != 1 || g_engine) return -1;
    return p_libvlc_video_get_size(g_mp, 0, w, h);
}
