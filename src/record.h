/*
 * record.h — enregistrement des chaînes et fenêtre « Enregistrements ».
 *
 * Le flux HTTP du tuner (MPEG-TS) est copié tel quel dans un fichier .ts,
 * sans réencodage, par un thread par enregistrement : on peut enregistrer
 * une chaîne et en regarder une autre (chaque flux occupe un tuner).
 * Les fichiers sont nommés « AAAA-MM-JJ HHhMM - Chaîne - Émission.ts » ;
 * la liste les relit depuis le dossier (durée = dernière écriture − création).
 *
 * Inclus par vlcmaster.c après guide_ui.h.
 */
#define MAXREC 8

typedef struct {
    int used;
    HANDLE th;
    volatile LONG stop;
    HINTERNET volatile hreq;          /* fermé par le thread principal pour interrompre la lecture */
    unsigned long long key;
    char url[1024];
    wchar_t path[MAX_PATH], name[128], title[200];
    long long start, endAt;           /* epoch ; endAt = 0 : sans limite */
    volatile LONGLONG bytes;
    wchar_t err[160];
} Rec;
static Rec g_rec[MAXREC];

typedef struct {
    wchar_t path[MAX_PATH], title[200], ch[128], date[32];
    ULONGLONG size; long long secs; FILETIME ct; int rec;
} RFile;
static RFile *g_rf; static int g_nrf;

enum { IDR_PLAY = 800, IDR_STOPREC, IDR_DEL, IDR_FOLDER, IDR_REFRESH, IDR_INFO, IDR_LV,
       IDR_PAUSE, IDR_BACK, IDR_FWD, IDR_SEEK, IDR_TIME, IDR_LIVE };
static HWND g_rw, g_rlv, g_rinfo, g_rseek, g_rtime;
static HWND g_rbtn[5], g_rpb[4];
static int  g_seekDrag;

/* ------------------------------------------------------------------ */
/* Utilitaires                                                         */
/* ------------------------------------------------------------------ */
static void RecDir(wchar_t *out)
{
    if (C.recdir[0]) { wcsncpy(out, C.recdir, MAX_PATH - 1); out[MAX_PATH - 1] = 0; return; }
    wchar_t v[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_MYVIDEO, NULL, SHGFP_TYPE_CURRENT, v) == S_OK)
        swprintf(out, MAX_PATH, L"%ls\\" APP_NAME, v);
    else
        swprintf(out, MAX_PATH, L"%ls\\Enregistrements", g_exedir);
}

/* Le dossier existe (ou est créé) et on peut y écrire ?  L'« Accès contrôlé aux
   dossiers » de Windows peut bloquer Vidéos pour une appli non autorisée. */
static int DirWritable(const wchar_t *dir)
{
    int e = SHCreateDirectoryExW(NULL, dir, NULL);
    if (e != ERROR_SUCCESS && e != ERROR_ALREADY_EXISTS && e != ERROR_FILE_EXISTS) return 0;
    wchar_t t[MAX_PATH + 32];
    swprintf(t, MAX_PATH + 32, L"%ls\\~hrp%lu.tmp", dir, GetCurrentProcessId());
    HANDLE f = CreateFileW(t, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    CloseHandle(f);
    return 1;
}

/* Dossier d'enregistrement utilisable.  Sans dossier choisi et si Vidéos est
   refusé, repli (mémorisé) sur un dossier autorisé, avec explication. */
static int RecEnsureDir(wchar_t *dir)
{
    RecDir(dir);
    if (DirWritable(dir)) return 1;
    if (C.recdir[0]) return 0;
    wchar_t alt[2][MAX_PATH], la[MAX_PATH];
    swprintf(alt[0], MAX_PATH, L"%ls\\Enregistrements", g_exedir);
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", la, MAX_PATH)) swprintf(alt[1], MAX_PATH, L"%ls\\" INI_NAME L"\\Enregistrements", la);
    else alt[1][0] = 0;
    for (int k = 0; k < 2; k++) {
        if (!alt[k][0] || !DirWritable(alt[k])) continue;
        wchar_t t[2 * MAX_PATH + 400];
        swprintf(t, 2 * MAX_PATH + 400, TR(L"Windows refuse l'écriture dans :\n%ls\n(protection « Accès contrôlé aux dossiers » de Sécurité Windows).\n\n"
                 L"Les enregistrements iront dans :\n%ls\n\n"
                 L"Vous pouvez choisir un autre dossier dans ⚙ Paramètres, ou autoriser " APP_NAME
                 L" dans Sécurité Windows › Protection contre les ransomware."), dir, alt[k]);
        wcscpy(C.recdir, alt[k]);
        wcscpy(dir, alt[k]);
        SaveConfig();
        SettingsSync();
        MessageBoxW(g_main, t, APP_NAME, MB_ICONINFORMATION);
        return 1;
    }
    return 0;
}

static void SanitizeFn(wchar_t *s)
{
    /* caractères interdits -> équivalents visuels (« 12/13 » reste lisible) */
    static const wchar_t bad[] = L"\\/:*?\"<>|", good[] = L"⧵∕꞉∗？”‹›∣";
    for (; *s; s++) {
        const wchar_t *k = wcschr(bad, *s);
        if (*s < 32) *s = L' ';
        else if (k) *s = good[k - bad];
    }
}

static void FmtSize(ULONGLONG b, wchar_t *o, int cap)
{
    if (b >= 1000000000ull) swprintf(o, cap, TR(L"%.2f Go"), b / 1e9);
    else swprintf(o, cap, TR(L"%.0f Mo"), b / 1e6);
}

static void FmtDur(long long s, wchar_t *o, int cap)
{
    if (s < 0) s = 0;
    if (s >= 3600) swprintf(o, cap, L"%lld:%02lld:%02lld", s / 3600, s / 60 % 60, s % 60);
    else swprintf(o, cap, L"%lld:%02lld", s / 60, s % 60);
}

static int RecCount(void)
{
    int n = 0;
    for (int i = 0; i < MAXREC; i++) n += g_rec[i].used;
    return n;
}

static int RecForCh(int ch)
{
    if (ch < 0 || ch >= g_nch) return -1;
    for (int i = 0; i < MAXREC; i++) if (g_rec[i].used && g_rec[i].key == g_ch[ch].key) return i;
    return -1;
}

static void RecFill(void);
static void RecRowsUpdate(void);

/* ------------------------------------------------------------------ */
/* Thread d'enregistrement                                             */
/* ------------------------------------------------------------------ */
#define REC_BUF (256 * 1024)

static DWORD WINAPI RecThread(LPVOID pv)
{
    Rec *r = (Rec *)pv;
    HINTERNET net = NULL;
    BYTE *buf = NULL;
    HANDLE f = CreateFileW(r->path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        swprintf(r->err, 160, TR(L"impossible de créer le fichier (erreur %lu)"), GetLastError());
        goto done;
    }
    net = InternetOpenW(L"HomeRunPlayer/" APP_VER, INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    buf = (BYTE *)malloc(REC_BUF);
    if (!net || !buf) { swprintf(r->err, 160, TR(L"initialisation réseau impossible")); goto done; }
    DWORD to = 6000;  InternetSetOptionW(net, INTERNET_OPTION_CONNECT_TIMEOUT, &to, sizeof to);
    to = 15000;       InternetSetOptionW(net, INTERNET_OPTION_RECEIVE_TIMEOUT, &to, sizeof to);
    wchar_t wurl[1024]; Utf8ToW(r->url, wurl, 1024);

    int fails = 0;
    while (!r->stop) {
        HINTERNET q = InternetOpenUrlW(net, wurl, NULL, 0,
                                       INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_UI, 0);
        if (q) {
            DWORD code = 0, cl = sizeof code;
            HttpQueryInfoW(q, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &code, &cl, NULL);
            if (code && code != 200) {
                if (code == 503) swprintf(r->err, 160, TR(L"aucun tuner libre (HTTP 503)"));
                else swprintf(r->err, 160, TR(L"erreur HTTP %lu"), code);
                InternetCloseHandle(q); q = NULL;
            }
        } else if (!r->stop) swprintf(r->err, 160, TR(L"tuner injoignable (erreur %lu)"), GetLastError());
        if (q) {
            InterlockedExchangePointer((PVOID volatile *)&r->hreq, q);
            DWORD n = 0;
            while (!r->stop && InternetReadFile(q, buf, REC_BUF, &n) && n) {
                DWORD wr = 0;
                if (!WriteFile(f, buf, n, &wr, NULL) || wr != n) {
                    swprintf(r->err, 160, TR(L"écriture impossible (disque plein ?)"));
                    r->stop = 1; break;
                }
                InterlockedExchangeAdd64(&r->bytes, (LONGLONG)n);
                fails = 0; r->err[0] = 0;
            }
            HINTERNET q2 = (HINTERNET)InterlockedExchangePointer((PVOID volatile *)&r->hreq, NULL);
            if (q2) InternetCloseHandle(q2);
        }
        if (r->stop) break;
        /* Flux coupé : on se reconnecte (le fichier continue). */
        if (++fails > 8) { if (!r->err[0]) swprintf(r->err, 160, TR(L"flux interrompu")); break; }
        for (int i = 0; i < 20 && !r->stop; i++) Sleep(100);
    }
done:
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    free(buf);
    if (net) InternetCloseHandle(net);
    PostMessageW(g_main, WM_APP_REC, (WPARAM)(r - g_rec), 0);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Démarrer / arrêter                                                  */
/* ------------------------------------------------------------------ */
static void RecChanged(void)
{
    if (g_rw) RecFill();
    if (g_list) InvalidateRect(g_list, NULL, FALSE);
    UpdateStatus();
}

/* endAt : epoch de fin, 0 = sans limite, -1 = fin de l'émission en cours (si connue). */
static void RecStart(int ch, long long endAt)
{
    if (ch < 0 || ch >= g_nch) { Flash(TR(L"Choisissez d'abord une chaîne à enregistrer")); return; }
    if (RecForCh(ch) >= 0) { Flash(TR(L"Cette chaîne est déjà en cours d'enregistrement")); return; }
    const Channel *c = &g_ch[ch];
    if (_strnicmp(c->url, "http://", 7) && _strnicmp(c->url, "https://", 8)) {
        Flash(TR(L"Enregistrement impossible : flux non HTTP")); return;
    }
    int i = 0;
    while (i < MAXREC && g_rec[i].used) i++;
    if (i == MAXREC) { Flash(TR(L"Trop d'enregistrements simultanés")); return; }

    wchar_t dir[MAX_PATH];
    if (!RecEnsureDir(dir)) {
        wchar_t t[MAX_PATH + 400];
        swprintf(t, MAX_PATH + 400, TR(L"Impossible d'écrire dans le dossier des enregistrements :\n%ls\n\n"
                 L"Si « Accès contrôlé aux dossiers » est activé dans Sécurité Windows, autorisez " APP_NAME
                 L", ou choisissez un autre dossier dans ⚙ Paramètres."), dir);
        MessageBoxW(g_main, t, APP_NAME, MB_ICONWARNING); return;
    }
    long long now = NowEpoch();
    const Prog *pr = ProgAt(GuideForCh(ch), now, NULL);
    if (endAt < 0) endAt = pr ? pr->en + (long long)C.recMargin * 60 : 0;

    Rec *r = &g_rec[i];
    memset(r, 0, sizeof *r);
    r->key = c->key;
    memcpy(r->url, c->url, sizeof r->url);
    wcsncpy(r->name, c->name, 127);
    if (pr && pr->title) wcsncpy(r->title, pr->title, 199);
    r->start = now; r->endAt = endAt;

    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t nm[128], ti[96], base[260];
    wcsncpy(nm, c->name, 60); nm[60] = 0; SanitizeFn(nm); TrimW(nm);
    wcsncpy(ti, r->title, 80); ti[80] = 0; SanitizeFn(ti); TrimW(ti);
    swprintf(base, 260, L"%04u-%02u-%02u %02uh%02u - %ls%ls%ls", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
             nm, ti[0] ? L" - " : L"", ti);
    for (int k = 1; k < 100; k++) {
        if (k == 1) swprintf(r->path, MAX_PATH, L"%ls\\%ls.ts", dir, base);
        else        swprintf(r->path, MAX_PATH, L"%ls\\%ls (%d).ts", dir, base, k);
        if (GetFileAttributesW(r->path) == INVALID_FILE_ATTRIBUTES) break;
    }
    r->used = 1;
    r->th = CreateThread(NULL, 0, RecThread, r, 0, NULL);
    if (!r->th) { r->used = 0; Flash(TR(L"Impossible de lancer l'enregistrement")); return; }
    SetTimer(g_main, TMR_REC, 1000, NULL);

    wchar_t t[200], hm[8] = L"";
    if (endAt) FmtHM(endAt, hm);
    swprintf(t, 200, TR(L"● Enregistrement de %ls%ls%ls"), c->name, endAt ? TR(L" jusqu'à ") : TR(L" (sans limite)"), hm);
    Flash(t);
    RecChanged();
}

static void RecStop(int i)
{
    if (i < 0 || i >= MAXREC || !g_rec[i].used) return;
    g_rec[i].stop = 1;
    HINTERNET q = (HINTERNET)InterlockedExchangePointer((PVOID volatile *)&g_rec[i].hreq, NULL);
    if (q) InternetCloseHandle(q);
}

/* Fin du thread (WM_APP_REC). */
static void RecFinished(int i)
{
    if (i < 0 || i >= MAXREC || !g_rec[i].used) return;
    Rec *r = &g_rec[i];
    if (r->th) { WaitForSingleObject(r->th, 3000); CloseHandle(r->th); r->th = NULL; }
    wchar_t t[400];
    if (r->bytes == 0) {
        DeleteFileW(r->path);
        swprintf(t, 400, TR(L"Enregistrement de %ls impossible : %ls"), r->name, r->err[0] ? r->err : TR(L"aucune donnée reçue"));
        if (!g_quitting) MessageBoxW(g_rw ? g_rw : g_main, t, APP_NAME, MB_ICONWARNING);
    } else {
        wchar_t sz[32], du[32]; FmtSize((ULONGLONG)r->bytes, sz, 32); FmtDur(NowEpoch() - r->start, du, 32);
        if (r->err[0] && !r->stop) swprintf(t, 400, TR(L"Enregistrement de %ls arrêté (%ls) · %ls · %ls"), r->name, r->err, du, sz);
        else swprintf(t, 400, TR(L"Enregistrement de %ls terminé · %ls · %ls"), r->name, du, sz);
        if (!g_quitting) Flash(t);
    }
    r->used = 0;
    if (!g_quitting) RecChanged();
}

/* Toutes les secondes tant qu'un enregistrement tourne. */
static void RecTick(void)
{
    long long now = NowEpoch();
    int any = 0;
    for (int i = 0; i < MAXREC; i++) {
        if (!g_rec[i].used) continue;
        any = 1;
        if (g_rec[i].endAt && now >= g_rec[i].endAt && !g_rec[i].stop) RecStop(i);
    }
    if (!any) KillTimer(g_main, TMR_REC);
    if (g_rw) RecRowsUpdate();
    UpdateStatus();
}

static void RecStopAll(void)
{
    HANDLE hs[MAXREC]; int n = 0;
    for (int i = 0; i < MAXREC; i++) if (g_rec[i].used) { RecStop(i); if (g_rec[i].th) hs[n++] = g_rec[i].th; }
    if (n) WaitForMultipleObjects((DWORD)n, hs, TRUE, 4000);
}

/* Bouton ● Enregistrer : démarre (jusqu'à la fin de l'émission) ou arrête la chaîne en cours. */
static void RecToggleCur(void)
{
    int i = RecForCh(g_cur);
    if (i >= 0) { RecStop(i); Flash(TR(L"Arrêt de l'enregistrement…")); }
    else RecStart(g_cur, -1);
}

static void ShowRecMenu(int x, int y, UINT align)
{
    HMENU m = CreatePopupMenu();
    int ri = RecForCh(g_cur);
    if (g_cur >= 0 && ri < 0) {
        wchar_t t[200];
        const Prog *pr = ProgAt(GuideForCh(g_cur), NowEpoch(), NULL);
        if (pr) {
            wchar_t hm[8]; FmtHM(pr->en + (long long)C.recMargin * 60, hm);
            swprintf(t, 200, TR(L"Enregistrer jusqu'à la fin de l'émission  (%ls)"), hm);
            AppendMenuW(m, MF_STRING | MF_DEFAULT, IDM_RECEND, t);
        }
        static const wchar_t *lbl[4] = { L"Enregistrer 30 min", L"Enregistrer 1 h", L"Enregistrer 2 h", L"Enregistrer 3 h" };
        for (int k = 0; k < 4; k++) AppendMenuW(m, MF_STRING, IDM_RECDUR0 + k, TR(lbl[k]));
        AppendMenuW(m, MF_STRING | (pr ? 0 : MF_DEFAULT), IDM_RECINF, TR(L"Enregistrer sans limite"));
    }
    int any = 0;
    for (int i = 0; i < MAXREC; i++) {
        if (!g_rec[i].used) continue;
        if (!any) { if (GetMenuItemCount(m)) AppendMenuW(m, MF_SEPARATOR, 0, NULL); any = 1; }
        wchar_t t[300];
        swprintf(t, 300, TR(L"■  Arrêter : %ls%ls%ls"), g_rec[i].name, g_rec[i].title[0] ? L" — " : L"", g_rec[i].title);
        AppendMenuW(m, MF_STRING | (g_rec[i].stop ? MF_GRAYED : 0), IDM_RECSTOP0 + i, t);
    }
    if (GetMenuItemCount(m)) AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, IDC_RECLIST, TR(L"Enregistrements…\tE"));
    TrackPopupMenu(m, TPM_RIGHTBUTTON | align, x, y, 0, g_main, NULL);
    DestroyMenu(m);
}

/* Commandes du menu d'enregistrement (renvoie 1 si traitée). */
static int RecCommand(int id)
{
    static const int mins[4] = { 30, 60, 120, 180 };
    if (id == IDM_RECEND) { RecStart(g_cur, -1); return 1; }
    if (id == IDM_RECINF) { RecStart(g_cur, 0); return 1; }
    if (id >= IDM_RECDUR0 && id < IDM_RECDUR0 + 4) { RecStart(g_cur, NowEpoch() + mins[id - IDM_RECDUR0] * 60LL); return 1; }
    if (id >= IDM_RECSTOP0 && id < IDM_RECSTOP0 + MAXREC) { RecStop(id - IDM_RECSTOP0); return 1; }
    if (id == IDM_RECCH) { RecStart(g_menuCh, -1); return 1; }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Lecture d'un fichier enregistré dans le lecteur principal           */
/* ------------------------------------------------------------------ */
static void PlayFile(const wchar_t *path, const wchar_t *title)
{
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) { Flash(TR(L"Fichier introuvable")); return; }
    g_fileMode = 1;
    wcsncpy(g_file, path, MAX_PATH - 1); g_file[MAX_PATH - 1] = 0;
    wcsncpy(g_fileTitle, title, 255); g_fileTitle[255] = 0;
    g_retries = 0; g_userStopped = 0;
    KillTimer(g_main, TMR_RETRY);
    SendMessageW(g_list, LB_SETCURSEL, (WPARAM)-1, 0);
    wchar_t m[400]; swprintf(m, 400, TR(L"Lecture de « %ls »…"), title);
    SetHostMsg(g_vlcState == -1 ? g_vlcInfo : m);
    g_size[0] = 0;
    SetState(TR(L"Ouverture…"));
    RequestPlayCur();
    ShowHud();
    if (g_rw) RecRowsUpdate();
}

static void SeekRel(int ms)
{
    if (!g_fileMode || g_vlcState != 1 || !g_playing) return;
    long long t = EngTime() + ms, len = EngLength();
    if (t < 0) t = 0;
    if (len > 0 && t > len - 1000) t = len - 1000;
    EngSetTime(t);
    wchar_t a[24], b[24], f[80];
    FmtDur(t / 1000, a, 24); FmtDur(len / 1000, b, 24);
    swprintf(f, 80, TR(L"%ls %d s   ·   %ls / %ls"), ms < 0 ? L"◀◀" : L"▶▶", (ms < 0 ? -ms : ms) / 1000, a, b);
    Flash(f);
}

/* ------------------------------------------------------------------ */
/* Fenêtre « Enregistrements »                                         */
/* ------------------------------------------------------------------ */
static int CmpRF(const void *a, const void *b)
{
    return -CompareFileTime(&((const RFile *)a)->ct, &((const RFile *)b)->ct);
}

static void RecScan(void)
{
    free(g_rf); g_rf = NULL; g_nrf = 0;
    int cap = 0;
    wchar_t dir[MAX_PATH], pat[MAX_PATH + 8];
    RecDir(dir);
    swprintf(pat, MAX_PATH + 8, L"%ls\\*.ts", dir);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            if (g_nrf == cap) {
                cap = cap ? cap * 2 : 64;
                RFile *n = (RFile *)realloc(g_rf, sizeof(RFile) * (size_t)cap);
                if (!n) break;
                g_rf = n;
            }
            RFile *f = &g_rf[g_nrf++];
            memset(f, 0, sizeof *f);
            swprintf(f->path, MAX_PATH, L"%ls\\%ls", dir, fd.cFileName);
            f->size = ((ULONGLONG)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
            f->ct = fd.ftCreationTime;
            ULONGLONG c = ((ULONGLONG)fd.ftCreationTime.dwHighDateTime << 32) | fd.ftCreationTime.dwLowDateTime;
            ULONGLONG w = ((ULONGLONG)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime;
            f->secs = w > c ? (long long)((w - c) / 10000000ull) : -1;
            wchar_t nm[MAX_PATH]; wcsncpy(nm, fd.cFileName, MAX_PATH - 1); nm[MAX_PATH - 1] = 0;
            size_t L = wcslen(nm); if (L > 3 && !_wcsicmp(nm + L - 3, L".ts")) nm[L - 3] = 0;
            unsigned Y, M, D, hh, mi; int n = 0;
            if (swscanf(nm, L"%4u-%2u-%2u %2uh%2u - %n", &Y, &M, &D, &hh, &mi, &n) == 5 && n > 0) {
                swprintf(f->date, 32, L"%02u/%02u/%04u  %02u:%02u", D, M, Y, hh, mi);
                wchar_t *rest = nm + n, *sep = wcsstr(rest, L" - ");
                if (sep) { *sep = 0; wcsncpy(f->title, sep + 3, 199); }
                wcsncpy(f->ch, rest, 127);
            } else {
                FILETIME lt; SYSTEMTIME st;
                FileTimeToLocalFileTime(&fd.ftCreationTime, &lt); FileTimeToSystemTime(&lt, &st);
                swprintf(f->date, 32, L"%02u/%02u/%04u  %02u:%02u", st.wDay, st.wMonth, st.wYear, st.wHour, st.wMinute);
                wcsncpy(f->title, nm, 199);
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    if (g_nrf > 1) qsort(g_rf, (size_t)g_nrf, sizeof(RFile), CmpRF);
}

static int RecOfFile(const RFile *f)
{
    for (int i = 0; i < MAXREC; i++) if (g_rec[i].used && !_wcsicmp(g_rec[i].path, f->path)) return i;
    return -1;
}

static void RecRow(int i)
{
    RFile *f = &g_rf[i];
    int ri = RecOfFile(f);
    wchar_t du[32] = L"", sz[32], stt[96] = L"";
    ULONGLONG size = f->size; long long secs = f->secs;
    if (ri >= 0) {
        size = (ULONGLONG)g_rec[ri].bytes; secs = NowEpoch() - g_rec[ri].start;
        if (g_rec[ri].stop) wcscpy(stt, TR(L"● Arrêt…"));
        else if (g_rec[ri].endAt) { wchar_t hm[8]; FmtHM(g_rec[ri].endAt, hm); swprintf(stt, 96, TR(L"● Enregistrement (fin %ls)"), hm); }
        else wcscpy(stt, TR(L"● Enregistrement"));
    } else if (g_fileMode && !_wcsicmp(g_file, f->path)) wcscpy(stt, g_paused ? TR(L"‖ En pause") : TR(L"▶ En lecture"));
    if (secs >= 0) FmtDur(secs, du, 32);
    FmtSize(size, sz, 32);
    ListView_SetItemText(g_rlv, i, 3, du);
    ListView_SetItemText(g_rlv, i, 4, sz);
    ListView_SetItemText(g_rlv, i, 5, stt);
}

static void RecRowsUpdate(void)
{
    if (!g_rw) return;
    for (int i = 0; i < g_nrf; i++) RecRow(i);
}

static int RecSel(void)
{
    int s = g_rlv ? ListView_GetNextItem(g_rlv, -1, LVNI_SELECTED) : -1;
    return (s >= 0 && s < g_nrf) ? s : -1;
}

static void RecFill(void)
{
    if (!g_rw) return;
    wchar_t keep[MAX_PATH] = L"";
    int s = RecSel();
    if (s >= 0) wcscpy(keep, g_rf[s].path);
    RecScan();
    SendMessageW(g_rlv, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(g_rlv);
    ULONGLONG tot = 0;
    int sel = -1;
    for (int i = 0; i < g_nrf; i++) {
        LVITEMW it; memset(&it, 0, sizeof it);
        it.mask = LVIF_TEXT; it.iItem = i;
        it.pszText = g_rf[i].title[0] ? g_rf[i].title : (LPWSTR)TR(L"(sans titre)");
        ListView_InsertItem(g_rlv, &it);
        ListView_SetItemText(g_rlv, i, 1, g_rf[i].ch);
        ListView_SetItemText(g_rlv, i, 2, g_rf[i].date);
        RecRow(i);
        tot += g_rf[i].size;
        if (keep[0] && !_wcsicmp(keep, g_rf[i].path)) sel = i;
    }
    if (sel < 0 && g_nrf) sel = 0;
    if (sel >= 0) ListView_SetItemState(g_rlv, sel, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    SendMessageW(g_rlv, WM_SETREDRAW, TRUE, 0);

    wchar_t dir[MAX_PATH], t[MAX_PATH + 160], a[32], b[32] = L"?";
    RecDir(dir);
    FmtSize(tot, a, 32);
    ULARGE_INTEGER fr;
    if (GetDiskFreeSpaceExW(dir, &fr, NULL, NULL)) FmtSize(fr.QuadPart, b, 32);
    if (g_nrf) swprintf(t, MAX_PATH + 160, TR(L"%d enregistrement%ls · %ls · %ls libres     %ls"),
                        g_nrf, g_nrf > 1 ? TR(L"s") : L"", a, b, dir);
    else swprintf(t, MAX_PATH + 160, TR(L"Aucun enregistrement pour l'instant (bouton ● Enregistrer ou touche R)     %ls"), dir);
    SetWindowTextW(g_rinfo, t);
}

static void RecPlaySel(void)
{
    int s = RecSel();
    if (s < 0) return;
    wchar_t t[400];
    if (g_rf[s].ch[0] && g_rf[s].title[0]) swprintf(t, 400, L"%ls — %ls", g_rf[s].ch, g_rf[s].title);
    else wcscpy(t, g_rf[s].title[0] ? g_rf[s].title : g_rf[s].ch);
    PlayFile(g_rf[s].path, t);
}

static void RecDeleteSel(void)
{
    int s = RecSel();
    if (s < 0) return;
    RFile *f = &g_rf[s];
    if (RecOfFile(f) >= 0) {
        MessageBoxW(g_rw, TR(L"Cet enregistrement est en cours.\nArrêtez-le d'abord."), APP_NAME, MB_ICONINFORMATION); return;
    }
    wchar_t q[600];
    swprintf(q, 600, TR(L"Supprimer « %ls » ?\n\nLe fichier sera placé dans la Corbeille."), f->title[0] ? f->title : f->ch);
    if (MessageBoxW(g_rw, q, APP_NAME, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return;
    if (g_fileMode && !_wcsicmp(g_file, f->path)) {   /* libère le fichier ouvert par libVLC */
        StopPlayback();
        g_fileMode = 0;
        Sleep(400);
    }
    wchar_t from[MAX_PATH + 2]; memset(from, 0, sizeof from);
    wcsncpy(from, f->path, MAX_PATH);
    int ok = 0;
    for (int k = 0; k < 3 && !ok; k++) {
        SHFILEOPSTRUCTW op; memset(&op, 0, sizeof op);
        op.hwnd = g_rw; op.wFunc = FO_DELETE; op.pFrom = from;
        op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
        ok = SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted;
        if (!ok) Sleep(500);
    }
    if (!ok) MessageBoxW(g_rw, TR(L"Suppression impossible (fichier utilisé ?)."), APP_NAME, MB_ICONWARNING);
    RecFill();
}

static void RecOpenFolder(void)
{
    int s = RecSel();
    if (s >= 0) {
        wchar_t a[MAX_PATH + 16]; swprintf(a, MAX_PATH + 16, L"/select,\"%ls\"", g_rf[s].path);
        ShellExecuteW(g_rw, NULL, L"explorer.exe", a, NULL, SW_SHOWNORMAL);
    } else {
        wchar_t dir[MAX_PATH]; RecDir(dir);
        SHCreateDirectoryExW(NULL, dir, NULL);
        ShellExecuteW(g_rw, L"open", dir, NULL, NULL, SW_SHOWNORMAL);
    }
}

/* Barre de lecture (position du fichier en cours de lecture). */
static void RecPosUpdate(void)
{
    if (!g_rw) return;
    int on = g_fileMode && g_vlcState == 1;
    for (int i = 0; i < 4; i++) EnableWindow(g_rpb[i], i == 3 ? (g_fileMode && g_cur >= 0) : on);
    EnableWindow(g_rseek, on);
    SetWindowTextW(g_rpb[0], g_paused ? TR(L"▶  Lecture") : TR(L"‖  Pause"));
    if (!on) { SetWindowTextW(g_rtime, L"–:–– / –:––"); SendMessageW(g_rseek, TBM_SETPOS, TRUE, 0); return; }
    long long len = EngLength(), t = EngTime();
    if (g_seekDrag && len > 0) t = len * SendMessageW(g_rseek, TBM_GETPOS, 0, 0) / 1000;
    else if (len > 0) SendMessageW(g_rseek, TBM_SETPOS, TRUE, (LPARAM)(t * 1000 / len));
    wchar_t a[24], b[24], s[80];
    FmtDur(t < 0 ? 0 : t / 1000, a, 24); FmtDur(len < 0 ? 0 : len / 1000, b, 24);
    swprintf(s, 80, L"%ls / %ls", a, b);
    SetWindowTextW(g_rtime, s);
}

static void RecLayout(int W, int H)
{
    int m = S(10), bh = S(30), g = S(6), x = m;
    static const int bw[5] = { 96, 200, 100, 140, 100 };
    HDWP d = BeginDeferWindowPos(16);
    for (int i = 0; i < 5; i++) {
        d = DeferWindowPos(d, g_rbtn[i], NULL, x, m, S(bw[i]), bh, SWP_NOZORDER | SWP_NOACTIVATE);
        x += S(bw[i]) + g;
    }
    int py = H - m - bh, ly = m + bh + S(8);
    d = DeferWindowPos(d, g_rinfo, NULL, m, ly, W - 2 * m, S(20), SWP_NOZORDER | SWP_NOACTIVATE);
    ly += S(24);
    d = DeferWindowPos(d, g_rlv, NULL, m, ly, W - 2 * m, py - S(10) - ly, SWP_NOZORDER | SWP_NOACTIVATE);
    static const int pw[4] = { 96, 64, 64, 150 };
    x = m;
    for (int i = 0; i < 3; i++) { d = DeferWindowPos(d, g_rpb[i], NULL, x, py, S(pw[i]), bh, SWP_NOZORDER | SWP_NOACTIVATE); x += S(pw[i]) + g; }
    int xr = W - m - S(pw[3]);
    d = DeferWindowPos(d, g_rpb[3], NULL, xr, py, S(pw[3]), bh, SWP_NOZORDER | SWP_NOACTIVATE);
    xr -= g + S(170);
    d = DeferWindowPos(d, g_rtime, NULL, xr, py, S(170), bh, SWP_NOZORDER | SWP_NOACTIVATE);
    d = DeferWindowPos(d, g_rseek, NULL, x, py, xr - g - x, bh, SWP_NOZORDER | SWP_NOACTIVATE);
    EndDeferWindowPos(d);
    int cw = W - 2 * m - GetSystemMetrics(SM_CXVSCROLL) - S(4);
    static const int pc[6] = { 31, 15, 14, 8, 9, 23 };
    for (int i = 0; i < 6; i++) ListView_SetColumnWidth(g_rlv, i, cw * pc[i] / 100);
}

static void RecFonts(void)
{
    if (g_rw) EnumChildWindows(g_rw, SetFontProc, (LPARAM)g_font);
}

static LRESULT CALLBACK RecWinProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    switch (msg) {
    case WM_CREATE: {
        g_rw = h;
        static const wchar_t *lb[5] = { L"▶  Lire", L"■  Arrêter l'enregistrement", L"Supprimer", L"Ouvrir le dossier", L"Actualiser" };
        for (int i = 0; i < 5; i++) g_rbtn[i] = Mk(h, WC_BUTTONW, TR(lb[i]), BS_PUSHBUTTON | WS_TABSTOP, 0, IDR_PLAY + i);
        g_rinfo = Mk(h, WC_STATICW, L"", SS_LEFT | SS_CENTERIMAGE | SS_PATHELLIPSIS | SS_NOPREFIX, 0, IDR_INFO);
        g_rlv = Mk(h, WC_LISTVIEWW, NULL, LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL | WS_TABSTOP, WS_EX_CLIENTEDGE, IDR_LV);
        SetWindowTheme(g_rlv, L"Explorer", NULL);
        ListView_SetExtendedListViewStyle(g_rlv, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
        static const wchar_t *cols[6] = { L"Émission", L"Chaîne", L"Date", L"Durée", L"Taille", L"État" };
        for (int i = 0; i < 6; i++) {
            LVCOLUMNW c; memset(&c, 0, sizeof c);
            c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT; c.cx = 100; c.pszText = (LPWSTR)TR(cols[i]);
            c.fmt = (i == 3 || i == 4) ? LVCFMT_RIGHT : LVCFMT_LEFT;
            ListView_InsertColumn(g_rlv, i, &c);
        }
        static const wchar_t *pb[4] = { L"‖  Pause", L"◀◀ 10 s", L"30 s ▶▶", L"Retour au direct" };
        for (int i = 0; i < 4; i++) g_rpb[i] = Mk(h, WC_BUTTONW, TR(pb[i]), BS_PUSHBUTTON | WS_TABSTOP, 0, i == 3 ? IDR_LIVE : IDR_PAUSE + i);
        g_rseek = Mk(h, TRACKBAR_CLASSW, NULL, TBS_HORZ | TBS_NOTICKS | TBS_BOTH | WS_TABSTOP, 0, IDR_SEEK);
        SendMessageW(g_rseek, TBM_SETRANGE, TRUE, MAKELPARAM(0, 1000));
        SendMessageW(g_rseek, TBM_SETPAGESIZE, 0, 50);
        g_rtime = Mk(h, WC_STATICW, L"", SS_CENTER | SS_CENTERIMAGE | SS_NOPREFIX, 0, IDR_TIME);
        RecFonts();
        RecFill();
        RecPosUpdate();
        SetTimer(h, 1, 500, NULL);
        return 0; }
    case WM_SIZE: if (w != SIZE_MINIMIZED) RecLayout(LOWORD(l), HIWORD(l)); return 0;
    case WM_GETMINMAXINFO: { MINMAXINFO *mm = (MINMAXINFO *)l; mm->ptMinTrackSize.x = S(720); mm->ptMinTrackSize.y = S(320); return 0; }
    case WM_DPICHANGED: {
        RecFonts();
        RECT *r = (RECT *)l;
        SetWindowPos(h, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0; }
    case WM_ACTIVATE: if (LOWORD(w) != WA_INACTIVE && g_rlv) SetFocus(g_rlv); return 0;
    case WM_TIMER: RecPosUpdate(); return 0;
    case WM_HSCROLL:
        if ((HWND)l == g_rseek) {
            int code = LOWORD(w);
            if (code == TB_THUMBTRACK) { g_seekDrag = 1; RecPosUpdate(); }
            else if (code == TB_ENDTRACK) {
                g_seekDrag = 0;
                if (g_fileMode && g_vlcState == 1) {
                    long long len = EngLength();
                    if (len > 0) EngSetTime(len * SendMessageW(g_rseek, TBM_GETPOS, 0, 0) / 1000);
                }
            }
        }
        return 0;
    case WM_NOTIFY: {
        NMHDR *nh = (NMHDR *)l;
        if (nh->idFrom != IDR_LV) break;
        if (nh->code == LVN_ITEMACTIVATE) RecPlaySel();
        else if (nh->code == LVN_KEYDOWN) {
            WORD k = ((NMLVKEYDOWN *)l)->wVKey;
            if (k == VK_DELETE) RecDeleteSel();
            else if (k == VK_F5) RecFill();
            else if (k == VK_ESCAPE) DestroyWindow(h);
            else if (k == VK_SPACE) TogglePause();
            else if (k == VK_LEFT) SeekRel(-10000);
            else if (k == VK_RIGHT) SeekRel(30000);
        }
        return 0; }
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case IDR_PLAY: RecPlaySel(); break;
        case IDR_STOPREC: {
            int s = RecSel(), ri = s >= 0 ? RecOfFile(&g_rf[s]) : -1;
            if (ri < 0) for (int i = 0; i < MAXREC; i++) if (g_rec[i].used) { ri = i; break; }
            if (ri >= 0) { RecStop(ri); RecRowsUpdate(); }
            break; }
        case IDR_DEL: RecDeleteSel(); break;
        case IDR_FOLDER: RecOpenFolder(); break;
        case IDR_REFRESH: RecFill(); break;
        case IDR_PAUSE: TogglePause(); break;
        case IDR_BACK: SeekRel(-10000); break;
        case IDR_FWD: SeekRel(30000); break;
        case IDR_LIVE: if (g_cur >= 0) PlayChannel(g_cur); break;
        case IDCANCEL: DestroyWindow(h); return 0;
        default: return 0;
        }
        RecPosUpdate();
        if (g_rlv && LOWORD(w) != IDR_FOLDER) SetFocus(g_rlv);
        return 0;
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY: {
        WINDOWPLACEMENT wp = { sizeof wp };
        GetWindowPlacement(h, &wp);
        C.recrc = wp.rcNormalPosition;
        g_rw = NULL; g_rlv = NULL;
        if (g_main && !g_quitting) SetFocus(g_main);
        return 0; }
    }
    return DefWindowProcW(h, msg, w, l);
}

static void RecRegister(HINSTANCE hi, HICON ico)
{
    WNDCLASSEXW wc; memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc; wc.hInstance = hi; wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.lpfnWndProc = RecWinProc; wc.lpszClassName = L"HrpRecordings"; wc.hIcon = ico;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassExW(&wc);
}

static void OpenRecordings(void)
{
    if (g_rw) {
        if (IsIconic(g_rw)) ShowWindow(g_rw, SW_RESTORE);
        SetForegroundWindow(g_rw);
        RecFill();
        return;
    }
    CreateWindowExW(0, L"HrpRecordings", TR(L"Enregistrements — " APP_NAME), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                    CW_USEDEFAULT, CW_USEDEFAULT, 100, 100, g_main, NULL, g_hi, NULL);
    if (!g_rw) return;
    WINDOWPLACEMENT wp = { sizeof wp };
    GetWindowPlacement(g_rw, &wp);
    if (RectOnScreen(&C.recrc)) wp.rcNormalPosition = C.recrc;
    else {
        RECT mr; GetWindowRect(g_main, &mr);
        int w = S(980), hh = S(560);
        int x = (mr.left + mr.right - w) / 2, y = (mr.top + mr.bottom - hh) / 2;
        if (y < 0) y = 0;
        if (x < 0) x = 0;
        SetRect(&wp.rcNormalPosition, x, y, x + w, y + hh);
    }
    wp.showCmd = SW_SHOWNORMAL;
    SetWindowPlacement(g_rw, &wp);
    SetFocus(g_rlv);
}
