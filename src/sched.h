/*
 * sched.h — enregistrements programmés depuis le guide TV.
 *
 * Une programmation = chaîne (clé d'URL) + début / fin de l'émission + titre.
 * Elles sont gardées dans l'INI (section [Programmations]) et vérifiées toutes
 * les 15 s : l'enregistrement démarre 1 min avant le début et s'arrête à la fin
 * + la marge réglée (Paramètres › Source).
 *
 * IMPORTANT : rien ne tourne quand HomeRun Player est fermé.  L'application
 * doit rester ouverte (et l'ordinateur allumé) à l'heure de l'enregistrement ;
 * tant qu'une programmation est en attente, la mise en veille automatique de
 * Windows est bloquée.  Une émission ratée parce que l'appli était fermée est
 * signalée au démarrage ; une émission déjà commencée est enregistrée tout de suite.
 *
 * Inclus par vlcmaster.c après guide_ui.h, avant record.h.
 */
typedef struct { unsigned long long key; long long st, en; wchar_t ch[128], title[200]; } Sched;
static Sched *g_sch; static int g_nsch;
static int g_schedTold;                   /* avertissement « l'appli doit rester ouverte » déjà montré */

static void RecStartEx(int ch, long long endAt, const wchar_t *title);   /* record.h */
static void RecFill(void);
static void RecExtend(int ri, long long endAt);                            /* record.h */

/* Programmation de la chaîne (clé) pour l'émission qui commence à st, ou -1. */
static int SchedFind(unsigned long long key, long long st)
{
    for (int i = 0; i < g_nsch; i++) if (g_sch[i].key == key && g_sch[i].st == st) return i;
    return -1;
}

static int ChByKey(unsigned long long key)
{
    for (int i = 0; i < g_nch; i++) if (g_ch[i].key == key) return i;
    return -1;
}

/* Enregistre la liste dans l'INI (« p0=clé|début|fin|chaîne|titre »). */
static void SchedSave(void)
{
    size_t cap = (size_t)g_nsch * 420 + 16;
    wchar_t *buf = (wchar_t *)calloc(cap, sizeof(wchar_t)), *p = buf;
    if (!buf) return;
    for (int i = 0; i < g_nsch; i++) {
        wchar_t ch[128], ti[200];
        wcscpy(ch, g_sch[i].ch); wcscpy(ti, g_sch[i].title);
        for (wchar_t *q = ch; *q; q++) if (*q == L'|') *q = L'¦';
        for (wchar_t *q = ti; *q; q++) if (*q == L'|') *q = L'¦';
        p += swprintf(p, cap - (size_t)(p - buf), L"p%d=%016llx|%lld|%lld|%ls|%ls", i, g_sch[i].key, g_sch[i].st, g_sch[i].en, ch, ti) + 1;
    }
    WritePrivateProfileSectionW(L"Programmations", buf, g_ini);
    free(buf);
}

static void SchedLoad(void)
{
    wchar_t *buf = (wchar_t *)calloc(65536, sizeof(wchar_t));
    if (!buf) return;
    GetPrivateProfileSectionW(L"Programmations", buf, 65536, g_ini);
    for (wchar_t *l = buf; *l; l += wcslen(l) + 1) {
        wchar_t *v = wcschr(l, L'=');
        if (!v) continue;
        Sched s; memset(&s, 0, sizeof s);
        wchar_t *f[5]; int n = 0; f[n++] = v + 1;
        for (wchar_t *q = v + 1; *q && n < 5; q++) if (*q == L'|') { *q = 0; f[n++] = q + 1; }
        if (n < 5) continue;
        s.key = wcstoull(f[0], NULL, 16); s.st = _wtoi64(f[1]); s.en = _wtoi64(f[2]);
        wcsncpy(s.ch, f[3], 127); wcsncpy(s.title, f[4], 199);
        Sched *ns = (Sched *)realloc(g_sch, sizeof(Sched) * (size_t)(g_nsch + 1));
        if (!ns) break;
        g_sch = ns; g_sch[g_nsch++] = s;
    }
    free(buf);
}

static int CmpSched(const void *a, const void *b)
{
    long long x = ((const Sched *)a)->st, y = ((const Sched *)b)->st;
    return x < y ? -1 : x > y;
}

static void SchedRemove(int i)
{
    if (i < 0 || i >= g_nsch) return;
    memmove(&g_sch[i], &g_sch[i + 1], sizeof(Sched) * (size_t)(g_nsch - i - 1));
    g_nsch--;
}

/* Bloque la mise en veille automatique tant qu'un enregistrement tourne ou est programmé. */
static void SchedPower(void)
{
    SetThreadExecutionState(ES_CONTINUOUS | ((g_nsch || RecCount()) ? ES_SYSTEM_REQUIRED : 0));
}

/* Après un changement : sauvegarde, grille du guide, fenêtre Enregistrements. */
static void SchedChanged(void)
{
    if (g_nsch > 1) qsort(g_sch, (size_t)g_nsch, sizeof(Sched), CmpSched);
    SchedSave();
    SchedPower();
    if (g_grid) InvalidateRect(g_grid, NULL, FALSE);
    if (g_rw) RecFill();
}

static void SchedDay(long long t, wchar_t *o, int cap)
{
    static const wchar_t *jours[7] = { L"dim.", L"lun.", L"mar.", L"mer.", L"jeu.", L"ven.", L"sam." };
    SYSTEMTIME d; EpochToLocal(t, &d);
    wchar_t hm[8]; FmtHM(t, hm);
    swprintf(o, (size_t)cap, L"%ls %02u/%02u  %ls", TR(jours[d.wDayOfWeek % 7]), d.wDay, d.wMonth, hm);
}

/* Programme / déprogramme une émission du guide (chaîne ci).  Émission en cours : enregistrée tout de suite. */
static void SchedToggle(int ci, const Prog *p)
{
    if (ci < 0 || ci >= g_nch || !p) return;
    long long now = NowEpoch();
    if (p->en <= now) { Flash(TR(L"Cette émission est terminée")); return; }
    if (p->st <= now) {                                   /* en cours */
        if (RecForCh(ci) >= 0) { Flash(TR(L"Cette chaîne est déjà en cours d'enregistrement")); return; }
        RecStartEx(ci, p->en + (long long)C.recMargin * 60, p->title);
        return;
    }
    int i = SchedFind(g_ch[ci].key, p->st);
    wchar_t t[400], when[48];
    SchedDay(p->st, when, 48);
    if (i >= 0) {
        SchedRemove(i);
        swprintf(t, 400, TR(L"Programmation annulée : %ls"), p->title ? p->title : L"");
        Flash(t);
        SchedChanged();
        return;
    }
    Sched s; memset(&s, 0, sizeof s);
    s.key = g_ch[ci].key; s.st = p->st; s.en = p->en;
    wcsncpy(s.ch, g_ch[ci].name, 127);
    if (p->title) wcsncpy(s.title, p->title, 199);
    Sched *ns = (Sched *)realloc(g_sch, sizeof(Sched) * (size_t)(g_nsch + 1));
    if (!ns) return;
    g_sch = ns; g_sch[g_nsch++] = s;
    SchedChanged();
    swprintf(t, 400, TR(L"● Programmé : %ls — %ls (HomeRun Player doit rester ouvert)"), s.title, when);
    Flash(t);
    if (!g_schedTold) {                                   /* une fois par session : explication claire */
        g_schedTold = 1;
        swprintf(t, 400, TR(L"Enregistrement programmé : %ls\n%ls, %ls\n\nImportant : HomeRun Player doit rester ouvert (il peut être réduit) et l'ordinateur allumé à cette heure-là. Tant qu'une programmation est en attente, la mise en veille automatique est bloquée."),
                 s.title, s.ch, when);
        MessageBoxW(g_gw ? g_gw : g_main, t, APP_NAME, MB_ICONINFORMATION);
    }
}

/* Toutes les 15 s : démarre les programmations arrivées à l'heure, signale celles qui ont été manquées. */
static void SchedTick(void)
{
    if (!g_nch) return;                                   /* liste des chaînes pas encore chargée */
    long long now = NowEpoch();
    int changed = 0;
    for (int i = 0; i < g_nsch; ) {
        Sched s = g_sch[i];
        if (now >= s.en) {                                /* fini sans avoir été enregistré */
            wchar_t t[400]; swprintf(t, 400, TR(L"Enregistrement programmé manqué (application fermée ?) : %ls — %ls"), s.ch, s.title);
            Flash(t);
            SchedRemove(i); changed = 1; continue;
        }
        if (now >= s.st - 60) {                           /* 1 min avant le début */
            int ci = ChByKey(s.key);
            long long endAt = s.en + (long long)C.recMargin * 60;
            int ri = ci >= 0 ? RecForCh(ci) : -1;
            if (ci < 0) {
                wchar_t t[400]; swprintf(t, 400, TR(L"Enregistrement programmé impossible, chaîne introuvable : %ls"), s.ch);
                Flash(t);
            } else if (ri >= 0) {                         /* déjà en cours : on prolonge si besoin */
                RecExtend(ri, endAt);
            } else RecStartEx(ci, endAt, s.title);
            SchedRemove(i); changed = 1; continue;
        }
        i++;
    }
    if (changed) SchedChanged();
    else SchedPower();
}

/* Menu du clic droit sur une émission du guide. */
static void SchedGridMenu(int x, int y, int ci, const Prog *p)
{
    if (ci < 0 || !p) return;
    long long now = NowEpoch();
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | MF_DEFAULT, IDG_MWATCH, TR(L"▶  Regarder la chaîne"));
    if (p->en > now) {
        if (p->st <= now) {
            wchar_t t[120], hm[8]; FmtHM(p->en + (long long)C.recMargin * 60, hm);
            swprintf(t, 120, TR(L"●  Enregistrer maintenant (jusqu'à %ls)"), hm);
            AppendMenuW(m, MF_STRING | (RecForCh(ci) >= 0 ? MF_GRAYED : 0), IDG_MREC, t);
        } else if (SchedFind(g_ch[ci].key, p->st) >= 0)
            AppendMenuW(m, MF_STRING, IDG_MREC, TR(L"✕  Annuler l'enregistrement programmé\tR"));
        else
            AppendMenuW(m, MF_STRING, IDG_MREC, TR(L"●  Programmer l'enregistrement\tR"));
        AppendMenuW(m, MF_SEPARATOR, 0, NULL);
        AppendMenuW(m, MF_STRING | MF_GRAYED, 0, TR(L"(HomeRun Player doit rester ouvert à l'heure de l'enregistrement)"));
    }
    int cmd = TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_RETURNCMD, x, y, 0, g_gw, NULL);
    DestroyMenu(m);
    if (cmd == IDG_MWATCH) PlayActive(ci);
    else if (cmd == IDG_MREC) SchedToggle(ci, p);
}
