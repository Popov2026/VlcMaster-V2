/*
 * tracks.h — langues : pistes audio (VF, VO, audiodescription…) et sous-titres
 * de l'image qui a le son (image principale ou PIP).
 *
 * Bouton « ♫ Langue » (menu), V = piste audio suivante, U = sous-titres suivants.
 * Le choix est mémorisé (texte entre crochets du nom de piste, ex. « [Anglais] »)
 * et réappliqué à chaque nouvelle chaîne qui propose la même langue.
 * VLC : API libVLC ; MPC-HC / MPC-BE : CMD_GET*TRACKS / CMD_SET*TRACK.
 *
 * Inclus par vlcmaster.c après pip.h.
 */
typedef struct { int id; wchar_t name[128]; } Track;
#define MAXTRK 32
static Track g_mpcTr[2][MAXTRK];          /* listes reçues de MPC : [0] audio, [1] sous-titres */
static int   g_mpcTrN[2], g_mpcTrCur[2];
static int   g_menuTrk[2][MAXTRK + 1];    /* id des entrées du menu */

/* Codes ISO 639-2 réservés utilisés par les chaînes françaises. */
static void TrackClean(wchar_t *s)
{
    static const struct { const wchar_t *code, *label; } R[] = {
        { L"qaa", L"version originale" }, { L"qad", L"audiodescription" }, { L"qtz", L"version multilingue" },
        { L"mis", L"non codée" }, { L"und", L"indéterminée" },
    };
    for (int i = 0; i < (int)(sizeof R / sizeof R[0]); i++) {
        wchar_t *p = s;
        while ((p = wcsstr(p, R[i].code))) {
            int before = p > s && iswalpha(p[-1]), after = iswalpha(p[3]);
            if (before || after) { p += 3; continue; }
            const wchar_t *lab = TR(R[i].label);
            size_t ll = wcslen(lab), rest = wcslen(p + 3);
            if ((size_t)(p - s) + ll + rest >= 127) break;
            memmove(p + ll, p + 3, (rest + 1) * sizeof(wchar_t));
            memcpy(p, lab, ll * sizeof(wchar_t));
            p += ll;
        }
    }
}

/* Libellé lisible : « Piste 1 - [Français] » → « Français »,
   « Audio description for the visually impaired - [Français] » → « Français (audiodescription) »,
   « Sous-titres DVB pour malentendants - [Français] » → « Français (malentendants) ». */
static void TrackPretty(wchar_t *s)
{
    static const struct { const wchar_t *vlc, *label; } K[] = {
        { L"Audio description for the visually impaired", L"audiodescription" },
        { L"Sous-titres DVB pour malentendants", L"malentendants" },
        { L"DVB subtitles for the hard of hearing", L"malentendants" },
        { L"Sous-titres DVB", L"" }, { L"DVB subtitles", L"" },
    };
    wchar_t lang[96] = L"", what[96];
    const wchar_t *a = wcsrchr(s, L'['), *b = a ? wcschr(a, L']') : NULL;
    if (!a || !b || b <= a + 1) {
        /* MPC : « Audio - français, Dolby Digital, 48.0 kHz, … » → « Français » */
        const wchar_t *d = wcsstr(s, L" - ");
        if (!d) return;
        wchar_t t[128]; wcsncpy(t, d + 3, 127); t[127] = 0;
        wchar_t *c = wcsstr(t, L", "); if (c) *c = 0;
        TrimW(t);
        if (!t[0]) return;
        t[0] = towupper(t[0]);
        wcscpy(s, t);
        return;
    }
    int n = (int)(b - a - 1); if (n > 95) n = 95;
    wcsncpy(lang, a + 1, (size_t)n); lang[n] = 0;
    n = (int)(a - s); if (n > 95) n = 95;
    wcsncpy(what, s, (size_t)n); what[n] = 0;
    TrimW(what);
    n = (int)wcslen(what);
    if (n && what[n - 1] == L'-') { what[n - 1] = 0; TrimW(what); }
    /* « Piste 3 », « Track 3 » : pas d'information */
    {
        wchar_t w1[64]; int num = 0, len = 0;
        if (swscanf(what, L"%63[^ 0-9] %d%n", w1, &num, &len) == 2 && !what[len]) what[0] = 0;
    }
    for (int i = 0; i < (int)(sizeof K / sizeof K[0]); i++)
        if (!_wcsicmp(what, K[i].vlc)) { wcscpy(what, K[i].label[0] ? TR(K[i].label) : L""); break; }
    if (what[0]) swprintf(s, 128, L"%ls (%ls)", lang, what);
    else swprintf(s, 128, L"%ls", lang);
}

/* Repère mémorisé = libellé lisible complet (« Français » ≠ « Français (audiodescription) »). */
static void TrackToken(const wchar_t *name, wchar_t *tok, int cap)
{
    wcsncpy(tok, name, (size_t)cap - 1); tok[cap - 1] = 0;
}

static libvlc_media_player_t *TrackPlayer(void) { return (PipHasSound() && g_mp2) ? g_mp2 : g_mp; }

/* Pistes de l'image qui a le son ; *cur = id de la piste active. */
static int TracksGet(int spu, Track *out, int max, int *cur)
{
    int n = 0;
    *cur = -9999;
    if (g_vlcState != 1) return 0;
    if (g_engine) {
        g_mpcTrN[spu] = 0;
        MpcSend(spu ? CMD_GETSUBTITLETRACKS : CMD_GETAUDIOTRACKS, NULL);   /* réponse reçue pendant l'envoi */
        n = g_mpcTrN[spu] < max ? g_mpcTrN[spu] : max;
        memcpy(out, g_mpcTr[spu], sizeof(Track) * (size_t)n);
        *cur = g_mpcTrCur[spu];
        return n;
    }
    libvlc_media_player_t *mp = TrackPlayer();
    if (!mp) return 0;
    libvlc_track_description_t *l = spu ? p_libvlc_video_get_spu_description(mp) : p_libvlc_audio_get_track_description(mp);
    for (libvlc_track_description_t *t = l; t && n < max; t = t->p_next) {
        if (t->i_id < 0) continue;                       /* « Désactiver » : ajouté par nous pour les sous-titres */
        out[n].id = t->i_id;
        Utf8ToW(t->psz_name ? t->psz_name : "?", out[n].name, 128);
        TrackClean(out[n].name); TrackPretty(out[n].name);
        n++;
    }
    if (l) p_libvlc_track_description_list_release(l);
    *cur = spu ? p_libvlc_video_get_spu(mp) : p_libvlc_audio_get_track(mp);
    return n;
}

/* Sélectionne une piste audio ou de sous-titres par son identifiant. */
static void TrackSet(int spu, int id)
{
    if (g_vlcState != 1) return;
    if (g_engine) {
        wchar_t b[16]; swprintf(b, 16, L"%d", id);
        MpcSend(spu ? CMD_SETSUBTITLETRACK : CMD_SETAUDIOTRACK, b);
        return;
    }
    libvlc_media_player_t *mp = TrackPlayer();
    if (!mp) return;
    if (spu) p_libvlc_video_set_spu(mp, id); else p_libvlc_audio_set_track(mp, id);
}

/* Liste envoyée par MPC : « nom0|nom1|…|index actif » (\| = | littéral). */
static void TracksOnMpc(int spu, const wchar_t *s)
{
    wchar_t part[MAXTRK + 2][128]; int np = 0, k = 0;
    for (const wchar_t *p = s; ; p++) {
        if (*p == L'\\' && p[1] == L'|') { if (k < 127) part[np][k++] = L'|'; p++; continue; }
        if (*p == L'|' || !*p) {
            part[np][k] = 0; k = 0;
            if (np < MAXTRK + 1) np++;
            if (!*p) break;
            continue;
        }
        if (k < 127) part[np][k++] = *p;
    }
    static Track keep[MAXTRK]; static int keepN;
    if (g_mpcNewFile) { keepN = 0; g_mpcNewFile = 0; }  /* nouvelle chaîne : oublie la liste gardée */
    g_mpcTrN[spu] = 0; g_mpcTrCur[spu] = -1;
    if (np < 2) {                                        /* -1 : aucune piste, -2 : pas de fichier */
        /* MPC-BE répond « aucune piste » une fois les sous-titres désactivés : on garde la liste */
        if (spu && keepN && _wtoi(part[0]) == -1) { memcpy(g_mpcTr[1], keep, sizeof(Track) * (size_t)keepN); g_mpcTrN[1] = keepN; }
        return;
    }
    g_mpcTrCur[spu] = _wtoi(part[np - 1]);
    for (int i = 0; i < np - 1 && i < MAXTRK; i++) {
        g_mpcTr[spu][i].id = i;
        wcscpy(g_mpcTr[spu][i].name, part[i]);
        TrackClean(g_mpcTr[spu][i].name); TrackPretty(g_mpcTr[spu][i].name);
        g_mpcTrN[spu] = i + 1;
    }
    if (spu) { keepN = g_mpcTrN[1]; memcpy(keep, g_mpcTr[1], sizeof(Track) * (size_t)keepN); }
}

/* Choix d'une piste (menu, touche) : appliqué et mémorisé. */
static void TrackChoose(int spu, const Track *t, int index, int count)
{
    TrackSet(spu, t ? t->id : -1);
    wchar_t *pref = spu ? C.prefSpu : C.prefAudio;
    if (!t || (!spu && index == 0)) pref[0] = 0;        /* 1re piste audio ou sous-titres désactivés : défaut */
    else TrackToken(t->name, pref, 64);
    wchar_t f[200];
    if (!t) swprintf(f, 200, TR(L"Sous-titres : désactivés"));
    else swprintf(f, 200, spu ? TR(L"Sous-titres : %ls  (%d/%d)") : TR(L"Audio : %ls  (%d/%d)"), t->name, index + 1, count);
    Flash(f);
}

/* V / U : piste suivante (pour les sous-titres, « désactivés » fait partie du cycle). */
static void TrackCycle(int spu)
{
    Track t[MAXTRK]; int cur, n = TracksGet(spu, t, MAXTRK, &cur);
    if (!n) { Flash(spu ? TR(L"Pas de sous-titres sur cette chaîne") : TR(L"Une seule piste audio sur cette chaîne")); return; }
    int p = -1;
    for (int i = 0; i < n; i++) if (t[i].id == cur) p = i;
    if (!spu) {
        if (n < 2) { Flash(TR(L"Une seule piste audio sur cette chaîne")); return; }
        p = (p + 1) % n;
        TrackChoose(0, &t[p], p, n);
    } else {
        p = p + 1;                                       /* -1 (désactivés) → 0 → … → n-1 → désactivés */
        if (p >= n) TrackChoose(1, NULL, -1, n);
        else TrackChoose(1, &t[p], p, n);
    }
}

/* Après le démarrage d'une chaîne : réapplique la langue mémorisée si elle existe. */
static void TracksApplyPref(void)
{
    for (int spu = 0; spu < 2; spu++) {
        const wchar_t *pref = spu ? C.prefSpu : C.prefAudio;
        if (!pref[0]) continue;
        Track t[MAXTRK]; int cur, n = TracksGet(spu, t, MAXTRK, &cur);
        for (int i = 0; i < n; i++) {
            wchar_t tok[64]; TrackToken(t[i].name, tok, 64);
            if (!_wcsicmp(tok, pref)) { if (t[i].id != cur) TrackSet(spu, t[i].id); break; }
        }
    }
}

/* Menu : pistes audio puis sous-titres. */
static void TracksMenuFill(HMENU m)
{
    Track a[MAXTRK], s[MAXTRK]; int ca, cs;
    int na = TracksGet(0, a, MAXTRK, &ca), ns = TracksGet(1, s, MAXTRK, &cs);
    AppendMenuW(m, MF_STRING | MF_GRAYED, 0, TR(L"Langue audio"));
    if (!na) AppendMenuW(m, MF_STRING | MF_GRAYED, 0, TR(L"   (aucune piste : lecture arrêtée ?)"));
    for (int i = 0; i < na; i++) {
        g_menuTrk[0][i] = a[i].id;
        AppendMenuW(m, MF_STRING | (a[i].id == ca ? MF_CHECKED : 0), IDM_AUD0 + i, a[i].name);
    }
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING | MF_GRAYED, 0, TR(L"Sous-titres"));
    g_menuTrk[1][0] = -1;
    AppendMenuW(m, MF_STRING | (cs < 0 || !ns ? MF_CHECKED : 0), IDM_SPU0, TR(L"Désactivés"));
    for (int i = 0; i < ns; i++) {
        g_menuTrk[1][i + 1] = s[i].id;
        AppendMenuW(m, MF_STRING | (s[i].id == cs ? MF_CHECKED : 0), IDM_SPU0 + 1 + i, s[i].name);
    }
}

/* Menu du bouton Langue. */
static void ShowTracksMenu(int x, int y, UINT align)
{
    HMENU m = CreatePopupMenu();
    TracksMenuFill(m);
    TrackPopupMenu(m, TPM_RIGHTBUTTON | align, x, y, 0, g_main, NULL);
    DestroyMenu(m);
}

/* Commandes du menu des pistes ; renvoie 1 si traitée. */
static int TracksCommand(int id)
{
    for (int spu = 0; spu < 2; spu++) {
        int base = spu ? IDM_SPU0 : IDM_AUD0, max = spu ? MAXTRK + 1 : MAXTRK;
        if (id < base || id >= base + max) continue;
        Track t[MAXTRK]; int cur, n = TracksGet(spu, t, MAXTRK, &cur);
        int want = g_menuTrk[spu][id - base];
        if (spu && want < 0) { TrackChoose(1, NULL, -1, n); return 1; }
        for (int i = 0; i < n; i++) if (t[i].id == want) { TrackChoose(spu, &t[i], i, n); return 1; }
        return 1;
    }
    return 0;
}
