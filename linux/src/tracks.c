/*
 * tracks.c — langues : pistes audio (VF, VO « qaa », audiodescription « qad »…) et
 * sous-titres DVB du lecteur qui a le son (image principale ou encart PIP).
 *
 * Le choix est mémorisé (texte entre crochets du nom de piste, ex. « [French] », plus
 * « audiodescription » / « malentendants ») et réappliqué à chaque nouvelle chaîne qui
 * propose la même piste.                                (↔ src/tracks.h, Tracks.kt)
 */
#include <string.h>

#include "app.h"

/* Noms de langues : anglais (tels que VLC les écrit) / codes ISO → fr, en, es, de, it, pt. */
static const struct { const char *en, *c2, *c3, *c3b; const char *n[6]; } LANGS[] = {
    { "french", "fr", "fra", "fre", { "Français", "French", "Francés", "Französisch", "Francese", "Francês" } },
    { "english", "en", "eng", NULL, { "Anglais", "English", "Inglés", "Englisch", "Inglese", "Inglês" } },
    { "german", "de", "deu", "ger", { "Allemand", "German", "Alemán", "Deutsch", "Tedesco", "Alemão" } },
    { "spanish", "es", "spa", NULL, { "Espagnol", "Spanish", "Español", "Spanisch", "Spagnolo", "Espanhol" } },
    { "italian", "it", "ita", NULL, { "Italien", "Italian", "Italiano", "Italienisch", "Italiano", "Italiano" } },
    { "portuguese", "pt", "por", NULL, { "Portugais", "Portuguese", "Portugués", "Portugiesisch", "Portoghese", "Português" } },
    { "dutch", "nl", "nld", "dut", { "Néerlandais", "Dutch", "Neerlandés", "Niederländisch", "Olandese", "Neerlandês" } },
    { "arabic", "ar", "ara", NULL, { "Arabe", "Arabic", "Árabe", "Arabisch", "Arabo", "Árabe" } },
    { "polish", "pl", "pol", NULL, { "Polonais", "Polish", "Polaco", "Polnisch", "Polacco", "Polaco" } },
    { "russian", "ru", "rus", NULL, { "Russe", "Russian", "Ruso", "Russisch", "Russo", "Russo" } },
    { "turkish", "tr", "tur", NULL, { "Turc", "Turkish", "Turco", "Türkisch", "Turco", "Turco" } },
    { "greek", "el", "ell", "gre", { "Grec", "Greek", "Griego", "Griechisch", "Greco", "Grego" } },
    { "chinese", "zh", "zho", "chi", { "Chinois", "Chinese", "Chino", "Chinesisch", "Cinese", "Chinês" } },
    { "japanese", "ja", "jpn", NULL, { "Japonais", "Japanese", "Japonés", "Japanisch", "Giapponese", "Japonês" } },
    { "catalan", "ca", "cat", NULL, { "Catalan", "Catalan", "Catalán", "Katalanisch", "Catalano", "Catalão" } },
    { "breton", "br", "bre", NULL, { "Breton", "Breton", "Bretón", "Bretonisch", "Bretone", "Bretão" } },
    { "corsican", "co", "cos", NULL, { "Corse", "Corsican", "Corso", "Korsisch", "Corso", "Corso" } },
    { "basque", "eu", "eus", "baq", { "Basque", "Basque", "Euskera", "Baskisch", "Basco", "Basco" } },
};

static const char *lang_name(const char *s)
{
    char *low = g_ascii_strdown(s, -1);
    const char *r = NULL;
    if (!strcmp(low, "qaa") || !strcmp(low, "original audio") || !strcmp(low, "original language")) r = T(S_trk_qaa);
    else if (!strcmp(low, "qad") || !strcmp(low, "audio description")) r = T(S_trk_qad);
    else if (!strcmp(low, "qtz")) r = T(S_trk_qtz);
    else if (!strcmp(low, "mis")) r = T(S_trk_mis);
    else if (!strcmp(low, "und")) r = T(S_trk_und);
    else
        for (size_t i = 0; i < G_N_ELEMENTS(LANGS); i++)
            if (!strcmp(low, LANGS[i].en) || !strcmp(low, LANGS[i].c2) || !strcmp(low, LANGS[i].c3) ||
                (LANGS[i].c3b && !strcmp(low, LANGS[i].c3b))) {
                r = LANGS[i].n[i18n_lang()];
                break;
            }
    g_free(low);
    return r;
}

/* « Track 1 - [French] » → « Français », audiodescription / malentendants en suffixe. */
static char *pretty(const char *raw)
{
    char b[128];
    if (!track_bracket(raw, b, sizeof b)) return g_strdup(raw);
    const char *n = lang_name(b);
    GString *s = g_string_new(n ? n : b);
    if (track_is_ad(raw) && g_ascii_strcasecmp(b, "qad")) g_string_append_printf(s, " (%s)", T(S_trk_ad));
    if (track_is_hoh(raw)) g_string_append_printf(s, " (%s)", T(S_trk_hoh));
    return g_string_free(s, FALSE);
}

typedef struct { int id; char *label, key[160]; } Trk;

static int list_tracks(libvlc_media_player_t *mp, gboolean spu, Trk *out, int cap)
{
    if (!mp) return 0;
    libvlc_track_description_t *d = spu ? libvlc_video_get_spu_description(mp) : libvlc_audio_get_track_description(mp);
    int n = 0;
    for (libvlc_track_description_t *t = d; t && n < cap; t = t->p_next) {
        if (t->i_id < 0) continue;   /* « Désactiver » */
        const char *raw = t->psz_name ? t->psz_name : "";
        out[n].id = t->i_id;
        out[n].label = pretty(raw);
        track_key(raw, out[n].key, sizeof out[n].key);
        n++;
    }
    if (d) libvlc_track_description_list_release(d);
    return n;
}

static void free_tracks(Trk *t, int n)
{
    for (int i = 0; i < n; i++) g_free(t[i].label);
}

void tracks_apply_prefs(libvlc_media_player_t *mp)
{
    if (!mp) return;
    Trk t[32];
    if (*cfg.audio_pref) {
        int n = list_tracks(mp, FALSE, t, 32);
        for (int i = 0; i < n; i++)
            if (!strcmp(t[i].key, cfg.audio_pref)) {
                if (libvlc_audio_get_track(mp) != t[i].id) libvlc_audio_set_track(mp, t[i].id);
                break;
            }
        free_tracks(t, n);
    }
    if (!strcmp(cfg.spu_pref, "off")) {
        if (libvlc_video_get_spu(mp) != -1) libvlc_video_set_spu(mp, -1);
    } else if (*cfg.spu_pref) {
        int n = list_tracks(mp, TRUE, t, 32);
        for (int i = 0; i < n; i++)
            if (!strcmp(t[i].key, cfg.spu_pref)) {
                if (libvlc_video_get_spu(mp) != t[i].id) libvlc_video_set_spu(mp, t[i].id);
                break;
            }
        free_tracks(t, n);
    }
}

char *tracks_cycle_audio(libvlc_media_player_t *mp)
{
    Trk t[32];
    int n = list_tracks(mp, FALSE, t, 32);
    if (!n) return NULL;
    int cur = libvlc_audio_get_track(mp), i = 0;
    while (i < n && t[i].id != cur) i++;
    Trk *x = &t[(i + 1) % n];
    libvlc_audio_set_track(mp, x->id);
    cfg_set(&cfg.audio_pref, x->key);
    char *r = g_strdup(x->label);
    free_tracks(t, n);
    cfg_save();
    return r;
}

char *tracks_cycle_spu(libvlc_media_player_t *mp)
{
    Trk t[32];
    int n = list_tracks(mp, TRUE, t, 32);
    if (!n) return NULL;
    int cur = libvlc_video_get_spu(mp), i = 0;
    while (i < n && t[i].id != cur) i++;
    char *r;
    if (i == n - 1) {   /* après la dernière piste : désactivés */
        libvlc_video_set_spu(mp, -1);
        cfg_set(&cfg.spu_pref, "off");
        r = g_strdup(T(S_trk_spu_off));
    } else {
        Trk *x = &t[i == n ? 0 : i + 1];
        libvlc_video_set_spu(mp, x->id);
        cfg_set(&cfg.spu_pref, x->key);
        r = g_strdup(x->label);
    }
    free_tracks(t, n);
    cfg_save();
    return r;
}

/* ---- menu « Langue » --------------------------------------------------- */
typedef struct { libvlc_media_player_t *mp; int spu, id; char *key, *label; } Choice;

static void choice_free(gpointer p)
{
    Choice *c = p;
    g_free(c->key);
    g_free(c->label);
    g_free(c);
}

static void on_choice(gpointer p)
{
    Choice *c = p;
    if (c->spu) {
        libvlc_video_set_spu(c->mp, c->id);
        cfg_set(&cfg.spu_pref, c->id < 0 ? "off" : c->key);
    } else {
        libvlc_audio_set_track(c->mp, c->id);
        cfg_set(&cfg.audio_pref, c->key);
    }
    cfg_save();
    app_flash(c->label);
}

static void add_choice(GtkWidget *m, libvlc_media_player_t *mp, int spu, int id, const char *key, const char *label, gboolean cur)
{
    Choice *c = g_new0(Choice, 1);
    c->mp = mp;
    c->spu = spu;
    c->id = id;
    c->key = g_strdup(key);
    c->label = g_strdup(label);
    char *txt = TF(spu ? S_trk_spu_item : S_trk_audio_item, label);
    char *full = g_strconcat(cur ? "●  " : "     ", txt, NULL);
    menu_add(m, full, on_choice, c, choice_free);
    g_free(full);
    g_free(txt);
}

void tracks_menu(GtkWidget *anchor, libvlc_media_player_t *mp)
{
    Trk a[32], s[32];
    int na = list_tracks(mp, FALSE, a, 32), ns = list_tracks(mp, TRUE, s, 32);
    GtkWidget *m = menu_new(anchor);
    if (!na && !ns) {
        menu_add(m, T(S_trk_none), NULL, NULL, NULL);
    } else {
        int ca = libvlc_audio_get_track(mp), cs = libvlc_video_get_spu(mp);
        for (int i = 0; i < na; i++) add_choice(m, mp, 0, a[i].id, a[i].key, a[i].label, a[i].id == ca);
        if (ns) {
            menu_add_separator(m);
            add_choice(m, mp, 1, -1, "off", T(S_trk_spu_off), cs < 0);
            for (int i = 0; i < ns; i++) add_choice(m, mp, 1, s[i].id, s[i].key, s[i].label, s[i].id == cs);
        }
    }
    free_tracks(a, na);
    free_tracks(s, ns);
    menu_popup(m);
}
