/*
 * tuner.h — informations sur le HDHomeRun et recherche des chaînes (scan).
 *
 * discover.json : modèle, firmware, nombre de tuners et « UpgradeAvailable »
 * (présent quand SiliconDust propose un firmware plus récent : l'appli le
 * signale ; l'installation se fait depuis la page web du tuner).
 * Scan : POST lineup.post?scan=start&source=…, suivi par lineup_status.json
 * ({"ScanInProgress":1,"Progress":45,"Found":12}), arrêt par scan=abort.
 * Le scan occupe tous les tuners : lecture, PIP et enregistrements sont arrêtés.
 *
 * Inclus par vlcmaster.c après tracks.h.
 */
typedef struct {
    int ok, tuners;
    wchar_t base[160], name[80], model[40], fw[40], upgrade[40], source[32], err[256];
} TunerInfo;
static TunerInfo g_tuner;
static volatile LONG g_scanAbort;
static int  g_scanning, g_upgradeTold;

/* Adresse du tuner d'après la playlist (http://hôte[:port]). */
static int TunerBase(wchar_t *out, int cap)
{
    const wchar_t *p = C.playlist, *s = wcsstr(p, L"://");
    if (!s || _wcsnicmp(p, L"http", 4)) return 0;
    s += 3;
    size_t n = wcscspn(s, L"/?#");
    if (!n || (size_t)(s - p) + n + 1 > (size_t)cap) return 0;
    swprintf(out, (size_t)cap, L"%.*ls%.*ls", (int)(s - p), p, (int)n, s);
    return 1;
}

static void JStrW(const JV *o, const char *k, wchar_t *out, int cap)
{
    const char *s = JGetS(o, k);
    if (s) Utf8ToW(s, out, cap); else out[0] = 0;
}

static DWORD WINAPI TunerInfoThread(LPVOID pv)
{
    TunerInfo *t = (TunerInfo *)pv;
    wchar_t url[256]; size_t len = 0;
    swprintf(url, 256, L"%ls/discover.json", t->base);
    char *buf = ReadHttp(url, &len, t->err);
    if (buf) {
        JV j; const char *ok = JVal(buf, &j, 0);
        if (ok && j.t == J_OBJ && JGetS(&j, "ModelNumber")) {
            t->ok = 1;
            JStrW(&j, "FriendlyName", t->name, 80);
            JStrW(&j, "ModelNumber", t->model, 40);
            JStrW(&j, "FirmwareVersion", t->fw, 40);
            JStrW(&j, "UpgradeAvailable", t->upgrade, 40);
            t->tuners = (int)JGetN(&j, "TunerCount");
        }
        JFree(&j); free(buf);
    }
    if (t->ok) {
        swprintf(url, 256, L"%ls/lineup_status.json", t->base);
        wchar_t e[256];
        if ((buf = ReadHttp(url, &len, e))) {
            JV j; if (JVal(buf, &j, 0) && j.t == J_OBJ) JStrW(&j, "Source", t->source, 32);
            JFree(&j); free(buf);
        }
    } else if (!t->err[0]) swprintf(t->err, 256, TR(L"aucun HDHomeRun détecté sur %ls"), t->base);
    PostMessageW(g_main, WM_APP_TUNER, 0, (LPARAM)t);
    return 0;
}

static void TunerUI(void);

static void TunerRefresh(void)
{
    TunerInfo *t = (TunerInfo *)calloc(1, sizeof *t);
    if (!t) return;
    if (!TunerBase(t->base, 160)) { free(t); g_tuner.ok = 0; g_tuner.err[0] = 0; TunerUI(); return; }
    HANDLE h = CreateThread(NULL, 0, TunerInfoThread, t, 0, NULL);
    if (h) CloseHandle(h); else free(t);
}

static void OnTuner(TunerInfo *t)
{
    g_tuner = *t; free(t);
    TunerUI();
    if (g_tuner.ok && g_tuner.upgrade[0] && !g_upgradeTold && !g_quitting) {
        g_upgradeTold = 1;
        wchar_t f[160]; swprintf(f, 160, TR(L"HDHomeRun : mise à jour du firmware disponible (%ls)"), g_tuner.upgrade);
        Flash(f);
    }
}

/* ------------------------------------------------------------------ */
/* Scan                                                                */
/* ------------------------------------------------------------------ */
static int HttpPost(const wchar_t *base, const wchar_t *pathq, wchar_t *err)
{
    URL_COMPONENTSW uc; memset(&uc, 0, sizeof uc); uc.dwStructSize = sizeof uc;
    wchar_t host[128];
    uc.lpszHostName = host; uc.dwHostNameLength = 128;
    if (!InternetCrackUrlW(base, 0, 0, &uc)) { swprintf(err, 256, TR(L"adresse invalide : %ls"), base); return 0; }
    int ok = 0;
    HINTERNET net = InternetOpenW(L"HomeRunPlayer/" APP_VER, INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    HINTERNET con = net ? InternetConnectW(net, host, uc.nPort, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0) : NULL;
    HINTERNET req = con ? HttpOpenRequestW(con, L"POST", pathq, NULL, NULL, NULL,
                                           INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_UI, 0) : NULL;
    if (req && HttpSendRequestW(req, NULL, 0, NULL, 0)) {
        DWORD code = 0, sz = sizeof code;
        HttpQueryInfoW(req, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &code, &sz, NULL);
        if (code < 400) ok = 1; else swprintf(err, 256, TR(L"réponse HTTP %lu"), code);
    } else swprintf(err, 256, TR(L"tuner injoignable (erreur %lu)"), GetLastError());
    if (req) InternetCloseHandle(req);
    if (con) InternetCloseHandle(con);
    if (net) InternetCloseHandle(net);
    return ok;
}

typedef struct { wchar_t base[160], source[32], err[256]; int found; } ScanArg;

/* WM_APP_SCAN : wParam = progression (0-100), lParam = chaînes trouvées ;
   wParam = -1 : fin (lParam = ScanArg*). */
static DWORD WINAPI ScanThread(LPVOID pv)
{
    ScanArg *a = (ScanArg *)pv;
    wchar_t path[96];
    swprintf(path, 96, L"/lineup.post?scan=start&source=%ls", a->source[0] ? a->source : L"Antenna");
    if (HttpPost(a->base, path, a->err)) {
        wchar_t url[256]; swprintf(url, 256, L"%ls/lineup_status.json", a->base);
        int started = 0, fails = 0;
        for (int i = 0; i < 1800; i++) {                     /* 30 min au plus */
            Sleep(1000);
            if (g_scanAbort) { HttpPost(a->base, L"/lineup.post?scan=abort", a->err); swprintf(a->err, 256, TR(L"scan arrêté")); break; }
            size_t len; wchar_t e[256];
            char *buf = ReadHttp(url, &len, e);
            if (!buf) { if (++fails > 20) { wcscpy(a->err, e); break; } continue; }
            fails = 0;
            JV j; int inprog = 0, prog = 0, found = 0;
            if (JVal(buf, &j, 0) && j.t == J_OBJ) {
                inprog = (int)JGetN(&j, "ScanInProgress");
                prog = (int)JGetN(&j, "Progress");
                found = (int)JGetN(&j, "Found");
            }
            JFree(&j); free(buf);
            if (inprog) { started = 1; a->found = found; PostMessageW(g_main, WM_APP_SCAN, (WPARAM)prog, (LPARAM)found); }
            else if (started || i > 5) break;               /* terminé (ou déjà fini) */
        }
    }
    PostMessageW(g_main, WM_APP_SCAN, (WPARAM)-1, (LPARAM)a);
    return 0;
}

static void ScanStart(HWND owner)
{
    if (g_scanning) { InterlockedExchange(&g_scanAbort, 1); SetWindowTextW(U.scanInfo, TR(L"Arrêt du scan…")); return; }
    ScanArg *a = (ScanArg *)calloc(1, sizeof *a);
    if (!a) return;
    if (!TunerBase(a->base, 160) || !g_tuner.ok) {
        free(a);
        MessageBoxW(owner, TR(L"Aucun HDHomeRun détecté : vérifiez l'adresse de la playlist (onglet Source)."), APP_NAME, MB_ICONINFORMATION);
        return;
    }
    int nrec = RecCount();
    wchar_t q[700];
    swprintf(q, 700, TR(L"Le HDHomeRun va rechercher toutes les chaînes (source : %ls). Cela prend quelques minutes et occupe tous les tuners :\nla lecture%ls sera arrêtée.\n\nLa liste des chaînes sera rechargée à la fin. Lancer le scan ?"),
             g_tuner.source[0] ? g_tuner.source : L"Antenna", nrec ? TR(L", le PIP et les enregistrements en cours") : TR(L" et le PIP"));
    if (MessageBoxW(owner, q, APP_NAME, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) { free(a); return; }
    wcsncpy(a->source, g_tuner.source, 31);
    if (nrec) RecStopAll();
    if (g_pipOn) { int keep = C.pip; PipOff(); C.pip = keep; }
    StopPlayback();
    g_scanning = 1; InterlockedExchange(&g_scanAbort, 0);
    HANDLE h = CreateThread(NULL, 0, ScanThread, a, 0, NULL);
    if (h) CloseHandle(h); else { free(a); g_scanning = 0; }
    SetHostMsg(TR(L"Recherche des chaînes en cours sur le HDHomeRun…"));
    TunerUI();
}

static void OnScan(WPARAM w, LPARAM l)
{
    wchar_t t[200];
    if ((int)w >= 0) {
        swprintf(t, 200, TR(L"Scan : %d %% · %d chaînes trouvées"), (int)w, (int)l);
        if (g_set) SetWindowTextW(U.scanInfo, t);
        SetState(t);
        return;
    }
    ScanArg *a = (ScanArg *)l;
    g_scanning = 0;
    if (a->err[0]) swprintf(t, 200, TR(L"Scan interrompu : %ls"), a->err);
    else swprintf(t, 200, TR(L"Scan terminé : %d chaînes trouvées"), a->found);
    free(a);
    if (g_set) SetWindowTextW(U.scanInfo, t);
    Flash(t);
    g_cur = -1;                 /* rechargement : la dernière chaîne est reprise */
    LoadPlaylist();
    GuideLoad();
    TunerRefresh();
    TunerUI();
}

/* Paramètres › Source : ligne d'information et boutons. */
static void TunerUI(void)
{
    if (!g_set) return;
    wchar_t t[400];
    if (g_tuner.ok) {
        if (g_tuner.upgrade[0])
            swprintf(t, 400, TR(L"%ls (%ls) · %d tuners · firmware %ls\n⚠ Mise à jour disponible : %ls (à installer depuis la page du tuner)"),
                     g_tuner.name, g_tuner.model, g_tuner.tuners, g_tuner.fw, g_tuner.upgrade);
        else
            swprintf(t, 400, TR(L"%ls (%ls) · %d tuners · firmware %ls\n✔ Firmware à jour"), g_tuner.name, g_tuner.model, g_tuner.tuners, g_tuner.fw);
    } else if (g_tuner.err[0]) swprintf(t, 400, L"%ls", g_tuner.err);
    else wcscpy(t, TR(L"HDHomeRun : recherche…"));
    SetWindowTextW(U.tunerInfo, t);
    SetWindowTextW(U.scanBtn, g_scanning ? TR(L"■  Arrêter le scan") : TR(L"⟳  Rechercher les chaînes"));
    EnableWindow(U.scanBtn, g_tuner.ok || g_scanning);
    EnableWindow(U.tunerPage, g_tuner.ok);
    InvalidateRect(U.tunerInfo, NULL, TRUE);
}

static void TunerOpenPage(void)
{
    wchar_t b[160];
    if (TunerBase(b, 160)) ShellExecuteW(g_set ? g_set : g_main, L"open", b, NULL, NULL, SW_SHOWNORMAL);
}
