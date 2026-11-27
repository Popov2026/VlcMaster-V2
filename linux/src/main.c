/*
 * main.c — fenêtre principale : lecture de la chaîne, liste des chaînes, zapping
 * (boutons, clavier, chiffres), HUD, volume, plein écran, image dans l'image,
 * nouvelle tentative si le flux coupe, reprise de la dernière chaîne.
 *                                          (↔ src/vlcmaster.c, src/pip.h, MainActivity.kt)
 */
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __GLIBC__
#include <malloc.h>
#endif

#include "app.h"

App app = { .cur = -1, .last = -1, .pip_idx = -1 };
gboolean hr_debug;

static struct {
    GtkWidget *paned, *side, *list, *count_lbl, *overlay, *frame, *pic, *status, *flash;
    GtkWidget *hud, *hud_title, *hud_prog, *hud_bar, *hud_next, *rec_ind;
    GtkWidget *pip_box, *pip_frame, *pip_pic, *pip_lbl;
    GtkWidget *bar, *chan_lbl, *vol, *btn_mute, *btn_rec, *btn_pip, *btn_lang, *banner;
    GPtrArray *row_prog;      /* GtkLabel « programme en cours » de chaque ligne */
    VideoOut *vo, *pvo;
    guint flash_id, hud_id, retry_id, reset_id, tracks_id, digits_id, peek_id, aspect_id;
    int retries, pip_retries;
    gboolean playing, muted, ui_hidden, peek, selecting;
    char digits[8];
    gint64 guide_try_at;
    gboolean guide_busy, playlist_busy;
} ui;

gint64 now_s(void) { return g_get_real_time() / G_USEC_PER_SEC; }

char *hm(gint64 t)
{
    GDateTime *d = g_date_time_new_from_unix_local(t);
    char *s = d ? g_date_time_format(d, "%H:%M") : g_strdup("--:--");
    if (d) g_date_time_unref(d);
    return s;
}

const Prog *app_prog_at(int idx, gint64 t)
{
    if (idx < 0 || idx >= app.ch.n || !cfg.show_guide) return NULL;
    return guide_at(guide_find(&app.guide, app.ch.v[idx].num), t);
}

static char *ch_label(int idx)
{
    const Channel *c = &app.ch.v[idx];
    return *c->num ? g_strdup_printf("%s  %s", c->num, c->name) : g_strdup(c->name);
}

/* ======================================================================= */
/* Messages à l'écran                                                      */
/* ======================================================================= */
static void show_status(const char *msg)
{
    gtk_label_set_text(GTK_LABEL(ui.status), msg);
    gtk_widget_set_visible(ui.status, msg && *msg);
}

static void hide_status(void) { gtk_widget_set_visible(ui.status, FALSE); }

static gboolean flash_off(gpointer p)
{
    (void)p;
    gtk_widget_set_visible(ui.flash, FALSE);
    ui.flash_id = 0;
    return G_SOURCE_REMOVE;
}

void app_flash(const char *msg)
{
    if (!msg || !*msg) return;
    gtk_label_set_text(GTK_LABEL(ui.flash), msg);
    gtk_widget_set_visible(ui.flash, TRUE);
    if (ui.flash_id) g_source_remove(ui.flash_id);
    ui.flash_id = g_timeout_add(3500, flash_off, NULL);
}

static gboolean hud_off(gpointer p)
{
    (void)p;
    gtk_widget_set_visible(ui.hud, FALSE);
    ui.hud_id = 0;
    return G_SOURCE_REMOVE;
}

static void update_hud(void)
{
    if (app.cur < 0 || app.cur >= app.ch.n) return;
    char *t = ch_label(app.cur);
    gtk_label_set_text(GTK_LABEL(ui.hud_title), t);
    gtk_label_set_text(GTK_LABEL(ui.chan_lbl), t);
    g_free(t);
    gint64 now = now_s();
    const Prog *p = app_prog_at(app.cur, now);
    if (p) {
        char *a = hm(p->start), *b = hm(p->end);
        char *s = g_strdup_printf("%s   %s – %s", p->title, a, b);
        gtk_label_set_text(GTK_LABEL(ui.hud_prog), s);
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ui.hud_bar),
                                      CLAMP((double)(now - p->start) / (double)MAX(p->end - p->start, 1), 0, 1));
        g_free(s); g_free(a); g_free(b);
    }
    gtk_widget_set_visible(ui.hud_prog, p != NULL);
    gtk_widget_set_visible(ui.hud_bar, p != NULL);
    const GuideCh *gc = cfg.show_guide ? guide_find(&app.guide, app.ch.v[app.cur].num) : NULL;
    const Prog *n = guide_next(gc, p ? p->end : now);
    if (n) {
        char *a = hm(n->start);
        char *s = TF(S_next_prog, a, n->title);
        gtk_label_set_text(GTK_LABEL(ui.hud_next), s);
        g_free(s); g_free(a);
    }
    gtk_widget_set_visible(ui.hud_next, n != NULL);
}

static void show_hud(void)
{
    if (app.cur < 0) return;
    update_hud();
    gtk_widget_set_visible(ui.hud, TRUE);
    if (ui.hud_id) g_source_remove(ui.hud_id);
    ui.hud_id = g_timeout_add(5000, hud_off, NULL);
}

/* ======================================================================= */
/* Lecture                                                                  */
/* ======================================================================= */
libvlc_media_t *app_new_media(const char *url, gboolean small)
{
    libvlc_media_t *m = g_str_has_prefix(url, "/") ? libvlc_media_new_path(app.vlc, url)
                                                   : libvlc_media_new_location(app.vlc, url);
    if (!m) return NULL;
    char opt[64];
    g_snprintf(opt, sizeof opt, ":network-caching=%d", cfg.caching);
    libvlc_media_add_option(m, opt);
    libvlc_media_add_option(m, cfg.hw ? ":avcodec-hw=any" : ":avcodec-hw=none");
    if (small) libvlc_media_add_option(m, ":avcodec-skiploopfilter=4");   /* petite image : décodage allégé */
    return m;
}

void app_apply_deinterlace(libvlc_media_player_t *mp)
{
    libvlc_video_set_deinterlace(mp, strcmp(cfg.deint, "off") ? cfg.deint : NULL);
}

libvlc_media_player_t *app_sound_player(void)
{
    return app.sound_on_pip && app.pip_idx >= 0 ? app.pip : app.mp;
}

static void apply_volume(void)
{
    int v = ui.muted ? 0 : cfg.volume;
    if (app.mp) libvlc_audio_set_volume(app.mp, app.sound_on_pip && app.pip_idx >= 0 ? 0 : v);
    if (app.pip && app.pip_idx >= 0) libvlc_audio_set_volume(app.pip, app.sound_on_pip ? v : 0);
    gtk_button_set_label(GTK_BUTTON(ui.btn_mute), T(ui.muted ? S_btn_muted : S_btn_sound));
    if (ABS(gtk_range_get_value(GTK_RANGE(ui.vol)) - cfg.volume) > 0.5) {
        ui.selecting = TRUE;
        gtk_range_set_value(GTK_RANGE(ui.vol), cfg.volume);
        ui.selecting = FALSE;
    }
}

static void change_volume(int delta)
{
    ui.muted = FALSE;
    cfg.volume = CLAMP(cfg.volume + delta, 0, 150);
    apply_volume();
    char *s = TF(S_volume_pct, cfg.volume);
    app_flash(s);
    g_free(s);
}

static void toggle_mute(void)
{
    ui.muted = !ui.muted;
    apply_volume();
    app_flash(T(ui.muted ? S_muted : S_unmuted));
}

static void select_row(int idx)
{
    GtkListBoxRow *r = idx >= 0 ? gtk_list_box_get_row_at_index(GTK_LIST_BOX(ui.list), idx) : NULL;
    ui.selecting = TRUE;
    if (r) {
        gtk_list_box_select_row(GTK_LIST_BOX(ui.list), r);
        /* fait défiler la liste jusqu'à la chaîne */
        GtkWidget *sw = gtk_widget_get_ancestor(ui.list, GTK_TYPE_SCROLLED_WINDOW);
        graphene_rect_t b;
        if (sw && gtk_widget_compute_bounds(GTK_WIDGET(r), ui.list, &b)) {
            GtkAdjustment *a = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(sw));
            double top = gtk_adjustment_get_value(a), page = gtk_adjustment_get_page_size(a);
            if (b.origin.y < top) gtk_adjustment_set_value(a, b.origin.y);
            else if (b.origin.y + b.size.height > top + page) gtk_adjustment_set_value(a, b.origin.y + b.size.height - page);
        }
    } else gtk_list_box_unselect_all(GTK_LIST_BOX(ui.list));
    ui.selecting = FALSE;
}

static void update_rec_ui(void)
{
    int n = rec_active_count();
    if (n > 1) {
        char *s = g_strdup_printf("%s ×%d", T(S_rec_indicator), n);
        gtk_label_set_text(GTK_LABEL(ui.rec_ind), s);
        g_free(s);
    } else gtk_label_set_text(GTK_LABEL(ui.rec_ind), T(S_rec_indicator));
    gtk_widget_set_visible(ui.rec_ind, n > 0);
    gboolean on = app.cur >= 0 && app.cur < app.ch.n && rec_is_recording(app.ch.v[app.cur].url);
    if (on) gtk_widget_add_css_class(ui.btn_rec, "recording");
    else gtk_widget_remove_css_class(ui.btn_rec, "recording");
}

void app_rec_changed(void) { update_rec_ui(); }

static void play_ex(int idx, gboolean from_retry)
{
    if (idx < 0 || idx >= app.ch.n || !app.mp) return;
    if (!from_retry) {
        ui.retries = 0;
        if (app.cur != idx && app.cur >= 0) app.last = app.cur;
    }
    app.cur = idx;
    app.live_paused = FALSE;
    if (ui.retry_id) { g_source_remove(ui.retry_id); ui.retry_id = 0; }
    const Channel *c = &app.ch.v[idx];
    cfg_set(&cfg.last_channel, c->url);
    libvlc_media_t *m = app_new_media(c->url, FALSE);
    if (!m) return;
    libvlc_media_player_set_media(app.mp, m);
    video_set_media(ui.vo, m);
    libvlc_media_release(m);
    app_apply_deinterlace(app.mp);
    libvlc_media_player_play(app.mp);
    ui.playing = TRUE;
    if (!from_retry) {
        char *s = TF(S_opening, c->name);
        show_status(s);
        g_free(s);
        cfg_save();
    }
    select_row(idx);
    show_hud();
    update_rec_ui();
}

void app_play(int idx) { play_ex(idx, FALSE); }

void app_play_url(const char *url)
{
    int i = playlist_find_url(&app.ch, url);
    if (i >= 0) app_play(i);
}

static void zap(int d)
{
    if (!app.ch.n) return;
    int i = app.cur < 0 ? (d > 0 ? 0 : app.ch.n - 1) : (app.cur + d + app.ch.n) % app.ch.n;
    app_play(i);
}

static void stop_main(void)
{
    ui.playing = FALSE;
    if (ui.retry_id) { g_source_remove(ui.retry_id); ui.retry_id = 0; }
    if (app.mp) libvlc_media_player_stop(app.mp);
    video_clear(ui.vo);
}

static void stop_pip(void);

void app_pause_live(gboolean pause)
{
    if (pause) {
        stop_main();
        stop_pip();
        app.live_paused = TRUE;
    } else if (app.live_paused) {
        app.live_paused = FALSE;
        if (app.cur >= 0) app_play(app.cur);
    }
}

static gboolean retry_cb(gpointer p)
{
    (void)p;
    ui.retry_id = 0;
    if (ui.playing && app.cur >= 0) play_ex(app.cur, TRUE);
    return G_SOURCE_REMOVE;
}

/* Le flux a coupé : nouvelle tentative (délai croissant). */
static void schedule_retry(void)
{
    if (ui.retry_id) return;
    if (ui.retries >= 12) {
        char *s = TF(S_cannot_play, app.cur >= 0 ? app.ch.v[app.cur].name : "");
        show_status(s);
        g_free(s);
        ui.playing = FALSE;
        return;
    }
    ui.retries++;
    char *s = TF(S_retrying, ui.retries);
    show_status(s);
    g_free(s);
    ui.retry_id = g_timeout_add(1500 * MIN(ui.retries, 6), retry_cb, NULL);
}

static gboolean reset_retries(gpointer p)
{
    (void)p;
    ui.retries = 0;
    ui.reset_id = 0;
    return G_SOURCE_REMOVE;
}

static gboolean tracks_cb(gpointer p)
{
    (void)p;
    ui.tracks_id = 0;
    tracks_apply_prefs(app.mp);
    if (app.pip_idx >= 0) tracks_apply_prefs(app.pip);
    return G_SOURCE_REMOVE;
}

static void schedule_tracks(void)
{
    if (ui.tracks_id) g_source_remove(ui.tracks_id);
    ui.tracks_id = g_timeout_add(600, tracks_cb, NULL);
}

static gboolean aspect_cb(gpointer p)
{
    (void)p;
    ui.aspect_id = 0;
    video_update_aspect(ui.vo);
    if (app.pip_idx >= 0) video_update_aspect(ui.pvo);
    return G_SOURCE_REMOVE;
}

/* ---- événements libVLC (thread de VLC → boucle GTK) ---------------------- */
typedef struct { int pip; int type; } VEv;

static gboolean pip_retry_cb(gpointer p)
{
    (void)p;
    if (app.pip_idx >= 0) app_start_pip(app.pip_idx);
    return G_SOURCE_REMOVE;
}

static gboolean on_vlc_event(gpointer p)
{
    VEv *e = p;
    if (hr_debug) g_message("libVLC %s : événement 0x%x", e->pip ? "encart" : "principal", e->type);
    if (!e->pip) {
        libvlc_state_t st = app.mp ? libvlc_media_player_get_state(app.mp) : libvlc_Stopped;
        switch (e->type) {
        case libvlc_MediaPlayerPlaying:
            hide_status();
            if (ui.reset_id) g_source_remove(ui.reset_id);
            ui.reset_id = g_timeout_add_seconds(15, reset_retries, NULL);
            schedule_tracks();
            apply_volume();
            break;
        case libvlc_MediaPlayerVout:
            hide_status();
            video_update_aspect(ui.vo);
            if (!ui.aspect_id) ui.aspect_id = g_timeout_add(2000, aspect_cb, NULL);
            break;
        case libvlc_MediaPlayerESAdded:
            schedule_tracks();
            break;
        case libvlc_MediaPlayerEncounteredError:
        case libvlc_MediaPlayerEndReached:
            /* ignoré si un autre média est déjà en cours d'ouverture (zapping rapide) */
            if (ui.playing && (st == libvlc_Error || st == libvlc_Ended || st == libvlc_Stopped)) schedule_retry();
            break;
        }
    } else if (app.pip_idx >= 0) {
        switch (e->type) {
        case libvlc_MediaPlayerPlaying:
            ui.pip_retries = 0;
            apply_volume();
            if (app.sound_on_pip) schedule_tracks();
            break;
        case libvlc_MediaPlayerVout:
            video_update_aspect(ui.pvo);
            break;
        case libvlc_MediaPlayerESAdded:
            if (app.sound_on_pip) schedule_tracks();
            break;
        case libvlc_MediaPlayerEncounteredError:
        case libvlc_MediaPlayerEndReached:
            if (ui.pip_retries++ < 8) g_timeout_add(2000, pip_retry_cb, NULL);
            break;
        }
    }
    g_free(e);
    return G_SOURCE_REMOVE;
}

static void vlc_event(const libvlc_event_t *ev, void *ud)
{
    VEv *e = g_new(VEv, 1);
    e->pip = GPOINTER_TO_INT(ud);
    e->type = ev->type;
    g_idle_add(on_vlc_event, e);
}

static void attach_events(libvlc_media_player_t *mp, int pip)
{
    libvlc_event_manager_t *em = libvlc_media_player_event_manager(mp);
    const libvlc_event_type_t types[] = { libvlc_MediaPlayerPlaying, libvlc_MediaPlayerVout, libvlc_MediaPlayerESAdded,
                                          libvlc_MediaPlayerEncounteredError, libvlc_MediaPlayerEndReached };
    for (size_t i = 0; i < G_N_ELEMENTS(types); i++) libvlc_event_attach(em, types[i], vlc_event, GINT_TO_POINTER(pip));
}

/* ======================================================================= */
/* Image dans l'image (encart)                                             */
/* ======================================================================= */
static void update_pip_border(void)
{
    if (app.sound_on_pip) gtk_widget_add_css_class(ui.pip_box, "active");
    else gtk_widget_remove_css_class(ui.pip_box, "active");
}

static void place_pip(void)
{
    int c = cfg.pip_corner;
    gtk_widget_set_halign(ui.pip_box, c % 2 == 0 ? GTK_ALIGN_START : GTK_ALIGN_END);
    gtk_widget_set_valign(ui.pip_box, c < 2 ? GTK_ALIGN_START : GTK_ALIGN_END);
}

static gboolean pip_size_cb(gpointer p)
{
    (void)p;
    int w = gtk_widget_get_width(ui.overlay);
    if (w > 0) {
        int pw = MAX(220, w * 30 / 100), ph = pw * 9 / 16;
        int cw, ch;
        gtk_widget_get_size_request(ui.pip_box, &cw, &ch);
        if (cw != pw || ch != ph) gtk_widget_set_size_request(ui.pip_box, pw, ph);
    }
    return G_SOURCE_CONTINUE;
}

void app_start_pip(int idx)
{
    if (idx < 0 || idx >= app.ch.n || !app.pip) return;
    gboolean fresh = app.pip_idx != idx;
    app.pip_idx = idx;
    if (fresh) ui.pip_retries = 0;
    libvlc_media_t *m = app_new_media(app.ch.v[idx].url, TRUE);
    if (!m) return;
    libvlc_media_player_set_media(app.pip, m);
    video_set_media(ui.pvo, m);
    libvlc_media_release(m);
    app_apply_deinterlace(app.pip);
    libvlc_audio_set_volume(app.pip, 0);
    libvlc_media_player_play(app.pip);
    char *l = ch_label(idx);
    gtk_label_set_text(GTK_LABEL(ui.pip_lbl), l);
    g_free(l);
    gtk_widget_set_visible(ui.pip_box, TRUE);
    apply_volume();
    update_pip_border();
}

static void stop_pip(void)
{
    if (app.pip_idx < 0) return;
    app.pip_idx = -1;
    if (app.pip) libvlc_media_player_stop(app.pip);
    video_clear(ui.pvo);
    gtk_widget_set_visible(ui.pip_box, FALSE);
    if (app.sound_on_pip) app.sound_on_pip = FALSE;
    apply_volume();
}

/* Le son suit l'image choisie. */
static void set_sound(gboolean on_pip)
{
    if (app.pip_idx < 0) on_pip = FALSE;
    app.sound_on_pip = on_pip;
    apply_volume();
    update_pip_border();
    app_flash(T(on_pip ? S_pip_sound_pip : S_pip_sound_main));
    schedule_tracks();
}

static void on_pip_pick(gpointer p) { app_start_pip(GPOINTER_TO_INT(p) - 1); }

static void channel_chooser(GtkWidget *anchor, MenuFn fn)
{
    GtkWidget *m = menu_new(anchor);
    for (int i = 0; i < app.ch.n; i++) {
        char *l = ch_label(i);
        menu_add(m, l, fn, GINT_TO_POINTER(i + 1), NULL);
        g_free(l);
    }
    menu_popup(m);
}

static void pip_change(gpointer p) { (void)p; channel_chooser(ui.btn_pip, on_pip_pick); }

static void pip_swap(gpointer p)
{
    (void)p;
    int a = app.cur, b = app.pip_idx;
    if (a < 0 || b < 0) return;
    app_play(b);
    app_start_pip(a);
}

static void pip_sound(gpointer p) { (void)p; set_sound(!app.sound_on_pip); }

static void pip_move(gpointer p)
{
    (void)p;
    static const int next[4] = { 1, 3, 0, 2 };   /* sens des aiguilles d'une montre */
    cfg.pip_corner = next[cfg.pip_corner & 3];
    place_pip();
    cfg_save();
}

static void pip_close(gpointer p) { (void)p; stop_pip(); }

static void pip_menu(GtkWidget *anchor)
{
    if (!app.ch.n) return;
    if (app.pip_idx < 0) { channel_chooser(anchor, on_pip_pick); return; }
    GtkWidget *m = menu_new(anchor);
    menu_add(m, T(app.sound_on_pip ? S_pip_sound_to_main : S_pip_sound_to_pip), pip_sound, NULL, NULL);
    menu_add(m, T(S_pip_change), pip_change, NULL, NULL);
    menu_add(m, T(S_pip_swap), pip_swap, NULL, NULL);
    menu_add(m, T(S_pip_move), pip_move, NULL, NULL);
    menu_add(m, T(S_pip_close), pip_close, NULL, NULL);
    menu_popup(m);
}

/* ======================================================================= */
/* Playlist et guide                                                        */
/* ======================================================================= */
static void refresh_list_progs(void)
{
    if (!ui.row_prog) return;
    gint64 now = now_s();
    for (guint i = 0; i < ui.row_prog->len && (int)i < app.ch.n; i++) {
        GtkWidget *l = g_ptr_array_index(ui.row_prog, i);
        const Prog *p = app_prog_at((int)i, now);
        gtk_label_set_text(GTK_LABEL(l), p ? p->title : "");
        gtk_widget_set_visible(l, p != NULL);
    }
}

static void build_list(void)
{
    gtk_list_box_remove_all(GTK_LIST_BOX(ui.list));
    if (ui.row_prog) g_ptr_array_free(ui.row_prog, TRUE);
    ui.row_prog = g_ptr_array_new();
    for (int i = 0; i < app.ch.n; i++) {
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
        gtk_widget_add_css_class(box, "chrow");
        char *t = ch_label(i);
        GtkWidget *l1 = gtk_label_new(t);
        g_free(t);
        gtk_label_set_xalign(GTK_LABEL(l1), 0);
        gtk_label_set_ellipsize(GTK_LABEL(l1), PANGO_ELLIPSIZE_END);
        gtk_widget_add_css_class(l1, "chname");
        GtkWidget *l2 = gtk_label_new("");
        gtk_label_set_xalign(GTK_LABEL(l2), 0);
        gtk_label_set_ellipsize(GTK_LABEL(l2), PANGO_ELLIPSIZE_END);
        gtk_widget_add_css_class(l2, "chprog");
        gtk_widget_set_visible(l2, FALSE);
        gtk_box_append(GTK_BOX(box), l1);
        gtk_box_append(GTK_BOX(box), l2);
        gtk_list_box_append(GTK_LIST_BOX(ui.list), box);
        g_ptr_array_add(ui.row_prog, l2);
    }
    char *s = TF(S_channels_count, app.ch.n);
    gtk_label_set_text(GTK_LABEL(ui.count_lbl), s);
    g_free(s);
    refresh_list_progs();
}

typedef struct { char *url, *text, *err; } PlJob;

static void pl_work(gpointer p)
{
    PlJob *j = p;
    j->text = net_get(j->url, 10000, &j->err);
}

static void pl_done(gpointer p)
{
    PlJob *j = p;
    ui.playlist_busy = FALSE;
    if (j->text) {
        ChannelList l;
        playlist_parse(j->text, &l);
        playlist_free(&app.ch);
        app.ch = l;
        app.cur = app.last = -1;
        app.pip_idx = -1;
        build_list();
        g_message("playlist : %d chaînes (%s)", l.n, cfg.playlist_url);
        if (!l.n) show_status(T(S_playlist_empty));
        else {
            int i = cfg.resume ? playlist_find_url(&app.ch, cfg.last_channel) : -1;
            if (i >= 0 && app.mp) app_play(i);
            else if (!app.mp) show_status(T(S_vlc_failed));
            else show_status(T(S_choose_channel));
        }
        app_refresh_guide(FALSE);
        tuner_check_firmware();
        guide_window_refresh();
    } else {
        char *s = TF(S_playlist_error, j->err ? j->err : "?");
        show_status(s);
        g_free(s);
    }
    g_free(j->url);
    g_free(j->text);
    g_free(j->err);
    g_free(j);
}

void app_reload_playlist(void)
{
    stop_main();
    stop_pip();
    app.live_paused = FALSE;
    if (!*cfg.playlist_url) { show_status(T(S_no_playlist)); return; }
    if (ui.playlist_busy) return;
    ui.playlist_busy = TRUE;
    show_status(T(S_loading_playlist));
    PlJob *j = g_new0(PlJob, 1);
    j->url = g_strdup(cfg.playlist_url);
    bg_run(pl_work, pl_done, j);
}

typedef struct { char *base; Guide g; int r; char *err; } GdJob;

static void gd_work(gpointer p)
{
    GdJob *j = p;
    char *e = NULL;
    j->r = guide_load(j->base, net_fetch, NULL, now_s(), &j->g, &e);
    j->err = g_strdup(e);
    free(e);
}

static void gd_done(gpointer p)
{
    GdJob *j = p;
    ui.guide_busy = FALSE;
    if (j->r == GUIDE_OK) {
        guide_free(&app.guide);
        app.guide = j->g;
        app.guide_loaded_at = now_s();
        guide_window_set_status(NULL);
        g_message("guide : %d chaînes, jusqu'à %lld", app.guide.n, (long long)app.guide.max_end);
    } else {
        guide_free(&j->g);
        int id = j->r == GUIDE_ERR_NO_TUNER ? S_guide_err_no_tuner : j->r == GUIDE_ERR_SERVICE ? S_guide_err_service : S_guide_err_empty;
        char *s = TF(id, j->err ? j->err : "");
        guide_window_set_status(s);
        g_message("%s", s);
        g_free(s);
    }
    refresh_list_progs();
    if (gtk_widget_get_visible(ui.hud)) update_hud();
    guide_window_refresh();
    g_free(j->base);
    g_free(j->err);
    g_free(j);
}

void app_refresh_guide(gboolean force)
{
    if (!cfg.show_guide || ui.guide_busy) return;
    gint64 now = now_s();
    gboolean stale = app.guide_loaded_at == 0 || now - app.guide_loaded_at > 3 * 3600 || app.guide.max_end < now + 6 * 3600;
    if (!force && (!stale || now - ui.guide_try_at < 600)) return;
    char *base = cfg_tuner_base();
    if (!base) return;
    ui.guide_try_at = now;
    ui.guide_busy = TRUE;
    guide_window_set_status(T(S_guide_loading));
    GdJob *j = g_new0(GdJob, 1);
    j->base = base;
    bg_run(gd_work, gd_done, j);
}

void app_options_changed(gboolean playlist_changed)
{
    if (playlist_changed) {
        guide_free(&app.guide);
        app.guide_loaded_at = 0;
        ui.guide_try_at = 0;
        app_reload_playlist();
        return;
    }
    if (!cfg.show_guide) refresh_list_progs();
    else app_refresh_guide(FALSE);
    if (app.cur >= 0 && !app.live_paused) app_play(app.cur);
    if (app.pip_idx >= 0) app_start_pip(app.pip_idx);
}

static gboolean minute_tick(gpointer p)
{
    (void)p;
    refresh_list_progs();
    if (gtk_widget_get_visible(ui.hud)) update_hud();
    app_refresh_guide(FALSE);
    guide_window_refresh();
    return G_SOURCE_CONTINUE;
}

/* ======================================================================= */
/* Interface : plein écran, visibilité, commandes                          */
/* ======================================================================= */
static void apply_visibility(void)
{
    gboolean fs = gtk_window_is_fullscreen(app.win);
    gtk_widget_set_visible(ui.side, cfg.panel && !fs && !ui.ui_hidden);
    gtk_widget_set_visible(ui.bar, (!fs && !ui.ui_hidden) || ui.peek);
}

static void toggle_fullscreen(void)
{
    if (gtk_window_is_fullscreen(app.win)) gtk_window_unfullscreen(app.win);
    else gtk_window_fullscreen(app.win);
}

static void on_fullscreen_changed(GObject *o, GParamSpec *ps, gpointer ud)
{
    (void)o; (void)ps; (void)ud;
    gboolean fs = gtk_window_is_fullscreen(app.win);
    if (fs) gtk_widget_add_css_class(ui.overlay, "fullscreen");
    else gtk_widget_remove_css_class(ui.overlay, "fullscreen");
    apply_visibility();
}

static gboolean peek_off(gpointer p)
{
    (void)p;
    ui.peek = FALSE;
    ui.peek_id = 0;
    apply_visibility();
    if (gtk_window_is_fullscreen(app.win)) gtk_widget_set_cursor_from_name(ui.overlay, "none");
    return G_SOURCE_REMOVE;
}

static void on_motion(GtkEventControllerMotion *c, double x, double y, gpointer ud)
{
    (void)c; (void)ud;
    static double lx = -1, ly = -1;
    if (ABS(x - lx) < 2 && ABS(y - ly) < 2) return;
    lx = x; ly = y;
    gtk_widget_set_cursor(ui.overlay, NULL);
    if (!gtk_window_is_fullscreen(app.win) && !ui.ui_hidden) return;
    ui.peek = TRUE;
    apply_visibility();
    if (ui.peek_id) g_source_remove(ui.peek_id);
    ui.peek_id = g_timeout_add(2500, peek_off, NULL);
}

static void toggle_panel(void)
{
    if (gtk_window_is_fullscreen(app.win) || ui.ui_hidden) { ui.ui_hidden = FALSE; cfg.panel = TRUE; }
    else cfg.panel = !cfg.panel;
    apply_visibility();
    cfg_save();
}

void app_show_keys(void) { ui_message(app.win, T(S_keys_title), T(S_keys_help)); }

static void on_play_digits(void)
{
    int i = playlist_find_num(&app.ch, ui.digits);
    if (i >= 0) app_play(i);
    else {
        char *s = TF(S_no_channel_num, ui.digits);
        app_flash(s);
        g_free(s);
    }
    ui.digits[0] = 0;
}

static gboolean digits_cb(gpointer p)
{
    (void)p;
    ui.digits_id = 0;
    on_play_digits();
    return G_SOURCE_REMOVE;
}

static void act_guide(gpointer p) { (void)p; guide_window_show(); }
static void act_lang(gpointer p) { (void)p; tracks_menu(ui.btn_lang, app_sound_player()); }
static void act_rec(gpointer p) { (void)p; if (app.cur >= 0) rec_menu(ui.btn_rec, app.cur); }
static void act_pip(gpointer p) { (void)p; pip_menu(ui.btn_pip); }
static void act_full(gpointer p) { (void)p; toggle_fullscreen(); }

static gboolean on_key(GtkEventControllerKey *c, guint kv, guint code, GdkModifierType st, gpointer ud)
{
    (void)c; (void)code; (void)ud;
    if (menu_any_open()) return FALSE;
    if (st & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SUPER_MASK)) return FALSE;
    guint k = gdk_keyval_to_lower(kv);
    if ((k >= GDK_KEY_0 && k <= GDK_KEY_9) || (k >= GDK_KEY_KP_0 && k <= GDK_KEY_KP_9)) {
        int d = k >= GDK_KEY_KP_0 ? (int)(k - GDK_KEY_KP_0) : (int)(k - GDK_KEY_0);
        size_t n = strlen(ui.digits);
        if (n < 5) { ui.digits[n] = (char)('0' + d); ui.digits[n + 1] = 0; }
        app_flash(ui.digits);
        if (ui.digits_id) g_source_remove(ui.digits_id);
        ui.digits_id = g_timeout_add(1500, digits_cb, NULL);
        return TRUE;
    }
    switch (k) {
    case GDK_KEY_Left: case GDK_KEY_Page_Down: case GDK_KEY_KP_Subtract: zap(-1); return TRUE;
    case GDK_KEY_Right: case GDK_KEY_Page_Up: case GDK_KEY_KP_Add: zap(1); return TRUE;
    case GDK_KEY_Up: change_volume(5); return TRUE;
    case GDK_KEY_Down: change_volume(-5); return TRUE;
    case GDK_KEY_Return: case GDK_KEY_KP_Enter:
        if (ui.digits_id) { g_source_remove(ui.digits_id); ui.digits_id = 0; on_play_digits(); return TRUE; }
        return FALSE;
    case GDK_KEY_BackSpace: if (app.last >= 0) app_play(app.last); return TRUE;
    case GDK_KEY_F11: case GDK_KEY_f: toggle_fullscreen(); return TRUE;
    case GDK_KEY_Escape:
        if (gtk_window_is_fullscreen(app.win)) { gtk_window_unfullscreen(app.win); return TRUE; }
        if (ui.ui_hidden) { ui.ui_hidden = FALSE; apply_visibility(); return TRUE; }
        return FALSE;
    case GDK_KEY_h: ui.ui_hidden = !ui.ui_hidden; ui.peek = FALSE; apply_visibility(); return TRUE;
    case GDK_KEY_i: case GDK_KEY_space: show_hud(); return TRUE;
    case GDK_KEY_g: guide_window_show(); return TRUE;
    case GDK_KEY_e: recordings_window_show(); return TRUE;
    case GDK_KEY_r: act_rec(NULL); return TRUE;
    case GDK_KEY_l: act_lang(NULL); return TRUE;
    case GDK_KEY_v: {
        char *s = tracks_cycle_audio(app_sound_player());
        app_flash(s ? s : T(S_trk_none));
        g_free(s);
        return TRUE;
    }
    case GDK_KEY_u: {
        char *s = tracks_cycle_spu(app_sound_player());
        app_flash(s ? s : T(S_trk_none));
        g_free(s);
        return TRUE;
    }
    case GDK_KEY_p: act_pip(NULL); return TRUE;
    case GDK_KEY_Tab: if (app.pip_idx >= 0) set_sound(!app.sound_on_pip); return TRUE;
    case GDK_KEY_m: toggle_mute(); return TRUE;
    case GDK_KEY_s: settings_window_show(); return TRUE;
    case GDK_KEY_t: tuner_window_show(); return TRUE;
    case GDK_KEY_question: case GDK_KEY_F1: app_show_keys(); return TRUE;
    default: return FALSE;
    }
}

static void on_row_activated(GtkListBox *b, GtkListBoxRow *r, gpointer ud)
{
    (void)b; (void)ud;
    if (!ui.selecting) app_play(gtk_list_box_row_get_index(r));
}

static void on_vol(GtkRange *r, gpointer ud)
{
    (void)ud;
    if (ui.selecting) return;
    cfg.volume = (int)gtk_range_get_value(r);
    ui.muted = FALSE;
    apply_volume();
}

static void video_menu(double x, double y)
{
    GtkWidget *m = menu_new(ui.overlay);
    menu_add(m, T(S_btn_guide), act_guide, NULL, NULL);
    menu_add(m, T(S_btn_lang), act_lang, NULL, NULL);
    if (app.cur >= 0) menu_add(m, T(S_ctx_rec), act_rec, NULL, NULL);
    menu_add(m, T(S_btn_pip), act_pip, NULL, NULL);
    menu_add(m, T(S_btn_full), act_full, NULL, NULL);
    menu_popup_at(m, x, y);
}

static void on_video_click(GtkGestureClick *g, int n, double x, double y, gpointer ud)
{
    (void)ud;
    guint btn = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(g));
    if (btn == GDK_BUTTON_SECONDARY) { video_menu(x, y); return; }
    if (btn != GDK_BUTTON_PRIMARY) return;
    if (n == 2) toggle_fullscreen();
    else show_hud();
    gtk_widget_grab_focus(ui.overlay);
}

static void on_pip_click(GtkGestureClick *g, int n, double x, double y, gpointer ud)
{
    (void)n; (void)x; (void)y; (void)ud;
    guint btn = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(g));
    gtk_gesture_set_state(GTK_GESTURE(g), GTK_EVENT_SEQUENCE_CLAIMED);   /* ne pas transmettre à l'image principale */
    if (btn == GDK_BUTTON_SECONDARY) pip_menu(ui.pip_box);
    else set_sound(!app.sound_on_pip);
}

static gboolean on_scroll(GtkEventControllerScroll *c, double dx, double dy, gpointer ud)
{
    (void)c; (void)dx; (void)ud;
    if (dy < 0) change_volume(5);
    else if (dy > 0) change_volume(-5);
    return TRUE;
}

static void on_btn(GtkButton *b, gpointer ud)
{
    (void)b;
    switch (GPOINTER_TO_INT(ud)) {
    case 1: toggle_panel(); break;
    case 2: zap(-1); break;
    case 3: zap(1); break;
    case 4: guide_window_show(); break;
    case 5: act_lang(NULL); break;
    case 6: act_rec(NULL); break;
    case 7: act_pip(NULL); break;
    case 8: recordings_window_show(); break;
    case 9: toggle_mute(); break;
    case 10: toggle_fullscreen(); break;
    case 11: settings_window_show(); break;
    case 12: app_show_keys(); break;
    }
}

static GtkWidget *bar_button(const char *label, int id, const char *tip)
{
    GtkWidget *b = gtk_button_new_with_label(label);
    gtk_widget_add_css_class(b, "flat");
    gtk_widget_set_focus_on_click(b, FALSE);
    if (tip) gtk_widget_set_tooltip_text(b, tip);
    g_signal_connect(b, "clicked", G_CALLBACK(on_btn), GINT_TO_POINTER(id));
    gtk_box_append(GTK_BOX(ui.bar), b);
    return b;
}

static const char *CSS =
    ".video { background: black; }\n"
    ".status { color: white; font-size: 15pt; background: rgba(0,0,0,0.55); border-radius: 10px; padding: 14px 22px; }\n"
    ".flash { color: white; font-size: 13pt; background: rgba(0,0,0,0.70); border-radius: 8px; padding: 8px 16px; }\n"
    ".hud { background: rgba(0,0,0,0.68); border-radius: 10px; padding: 10px 16px; color: white; }\n"
    ".hud .title { font-size: 17pt; font-weight: bold; }\n"
    ".hud .prog { font-size: 12pt; }\n"
    ".hud .next { font-size: 10pt; color: #c8c8c8; }\n"
    ".rec { color: white; background: #c62828; font-weight: bold; border-radius: 6px; padding: 4px 10px; }\n"
    ".pip { border: 2px solid rgba(255,255,255,0.35); background: black; }\n"
    ".pip.active { border-color: #ffb300; }\n"
    ".pip .piplbl { color: white; background: rgba(0,0,0,0.6); padding: 2px 6px; font-size: 9pt; }\n"
    ".chname { font-weight: bold; }\n"
    ".chprog { font-size: 9pt; opacity: 0.7; }\n"
    ".chrow { padding: 3px 6px; }\n"
    ".controls { padding: 2px 4px; }\n"
    ".chanlbl { font-weight: bold; margin: 0 8px; }\n"
    "button.recording { color: #e53935; }\n"
    ".banner { background: #1565c0; color: white; padding: 4px 10px; }\n"
    ".banner button { color: white; }\n"
    "button.menuitem { padding: 4px 12px; }\n"
    ".guide-status { padding: 6px 10px; }\n";

static GtkWidget *overlay_label(const char *css, GtkAlign h, GtkAlign v)
{
    GtkWidget *l = gtk_label_new("");
    gtk_widget_add_css_class(l, css);
    gtk_widget_set_halign(l, h);
    gtk_widget_set_valign(l, v);
    gtk_widget_set_can_target(l, FALSE);
    gtk_label_set_wrap(GTK_LABEL(l), TRUE);
    gtk_label_set_justify(GTK_LABEL(l), GTK_JUSTIFY_CENTER);
    gtk_widget_set_visible(l, FALSE);
    gtk_overlay_add_overlay(GTK_OVERLAY(ui.overlay), l);
    return l;
}

static void build_ui(void)
{
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_string(css, CSS);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(css),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(app.win, root);

    /* bandeau de mise à jour */
    ui.banner = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(ui.banner, "banner");
    gtk_widget_set_visible(ui.banner, FALSE);
    gtk_box_append(GTK_BOX(root), ui.banner);

    ui.paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_vexpand(ui.paned, TRUE);
    gtk_box_append(GTK_BOX(root), ui.paned);

    /* liste des chaînes */
    ui.side = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *hdr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_start(hdr, 8);
    gtk_widget_set_margin_end(hdr, 8);
    gtk_widget_set_margin_top(hdr, 6);
    gtk_widget_set_margin_bottom(hdr, 6);
    GtkWidget *ht = gtk_label_new(T(S_channels));
    gtk_widget_add_css_class(ht, "heading");
    gtk_box_append(GTK_BOX(hdr), ht);
    ui.count_lbl = gtk_label_new("");
    gtk_widget_add_css_class(ui.count_lbl, "dim-label");
    gtk_widget_set_hexpand(ui.count_lbl, TRUE);
    gtk_label_set_xalign(GTK_LABEL(ui.count_lbl), 1);
    gtk_box_append(GTK_BOX(hdr), ui.count_lbl);
    gtk_box_append(GTK_BOX(ui.side), hdr);
    GtkWidget *sw = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(sw, TRUE);
    ui.list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(ui.list), GTK_SELECTION_SINGLE);
    gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(ui.list), TRUE);
    gtk_widget_add_css_class(ui.list, "navigation-sidebar");
    g_signal_connect(ui.list, "row-activated", G_CALLBACK(on_row_activated), NULL);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), ui.list);
    gtk_box_append(GTK_BOX(ui.side), sw);
    gtk_widget_set_size_request(ui.side, 200, -1);
    gtk_paned_set_start_child(GTK_PANED(ui.paned), ui.side);
    gtk_paned_set_resize_start_child(GTK_PANED(ui.paned), FALSE);
    gtk_paned_set_shrink_start_child(GTK_PANED(ui.paned), FALSE);
    gtk_paned_set_position(GTK_PANED(ui.paned), 270);

    /* image */
    ui.overlay = gtk_overlay_new();
    gtk_widget_add_css_class(ui.overlay, "video");
    gtk_widget_set_hexpand(ui.overlay, TRUE);
    gtk_widget_set_focusable(ui.overlay, TRUE);
    ui.frame = gtk_aspect_frame_new(0.5f, 0.5f, 16.0f / 9, FALSE);
    ui.pic = gtk_picture_new();
    gtk_aspect_frame_set_child(GTK_ASPECT_FRAME(ui.frame), ui.pic);
    gtk_overlay_set_child(GTK_OVERLAY(ui.overlay), ui.frame);
    gtk_paned_set_end_child(GTK_PANED(ui.paned), ui.overlay);
    gtk_paned_set_shrink_end_child(GTK_PANED(ui.paned), FALSE);

    ui.status = overlay_label("status", GTK_ALIGN_CENTER, GTK_ALIGN_CENTER);
    ui.flash = overlay_label("flash", GTK_ALIGN_CENTER, GTK_ALIGN_END);
    gtk_widget_set_margin_bottom(ui.flash, 40);
    ui.rec_ind = overlay_label("rec", GTK_ALIGN_END, GTK_ALIGN_START);
    gtk_widget_set_margin_top(ui.rec_ind, 14);
    gtk_widget_set_margin_end(ui.rec_ind, 14);

    ui.hud = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(ui.hud, "hud");
    gtk_widget_set_halign(ui.hud, GTK_ALIGN_START);
    gtk_widget_set_valign(ui.hud, GTK_ALIGN_START);
    gtk_widget_set_margin_start(ui.hud, 20);
    gtk_widget_set_margin_top(ui.hud, 20);
    gtk_widget_set_can_target(ui.hud, FALSE);
    gtk_widget_set_size_request(ui.hud, 360, -1);
    ui.hud_title = gtk_label_new("");
    gtk_widget_add_css_class(ui.hud_title, "title");
    ui.hud_prog = gtk_label_new("");
    gtk_widget_add_css_class(ui.hud_prog, "prog");
    ui.hud_bar = gtk_progress_bar_new();
    ui.hud_next = gtk_label_new("");
    gtk_widget_add_css_class(ui.hud_next, "next");
    GtkWidget *hl[] = { ui.hud_title, ui.hud_prog, ui.hud_next };
    for (int i = 0; i < 3; i++) {
        gtk_label_set_xalign(GTK_LABEL(hl[i]), 0);
        gtk_label_set_ellipsize(GTK_LABEL(hl[i]), PANGO_ELLIPSIZE_END);
        gtk_label_set_max_width_chars(GTK_LABEL(hl[i]), 60);
    }
    gtk_box_append(GTK_BOX(ui.hud), ui.hud_title);
    gtk_box_append(GTK_BOX(ui.hud), ui.hud_prog);
    gtk_box_append(GTK_BOX(ui.hud), ui.hud_bar);
    gtk_box_append(GTK_BOX(ui.hud), ui.hud_next);
    gtk_widget_set_visible(ui.hud, FALSE);
    gtk_overlay_add_overlay(GTK_OVERLAY(ui.overlay), ui.hud);

    /* encart PIP */
    ui.pip_box = gtk_overlay_new();
    gtk_widget_add_css_class(ui.pip_box, "pip");
    gtk_widget_set_margin_start(ui.pip_box, 16);
    gtk_widget_set_margin_end(ui.pip_box, 16);
    gtk_widget_set_margin_top(ui.pip_box, 16);
    gtk_widget_set_margin_bottom(ui.pip_box, 16);
    ui.pip_frame = gtk_aspect_frame_new(0.5f, 0.5f, 16.0f / 9, FALSE);
    ui.pip_pic = gtk_picture_new();
    gtk_aspect_frame_set_child(GTK_ASPECT_FRAME(ui.pip_frame), ui.pip_pic);
    gtk_overlay_set_child(GTK_OVERLAY(ui.pip_box), ui.pip_frame);
    ui.pip_lbl = gtk_label_new("");
    gtk_widget_add_css_class(ui.pip_lbl, "piplbl");
    gtk_widget_set_halign(ui.pip_lbl, GTK_ALIGN_START);
    gtk_widget_set_valign(ui.pip_lbl, GTK_ALIGN_START);
    gtk_widget_set_can_target(ui.pip_lbl, FALSE);
    gtk_overlay_add_overlay(GTK_OVERLAY(ui.pip_box), ui.pip_lbl);
    gtk_widget_set_visible(ui.pip_box, FALSE);
    gtk_overlay_add_overlay(GTK_OVERLAY(ui.overlay), ui.pip_box);
    place_pip();
    GtkGesture *pg = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(pg), 0);
    g_signal_connect(pg, "pressed", G_CALLBACK(on_pip_click), NULL);
    gtk_widget_add_controller(ui.pip_box, GTK_EVENT_CONTROLLER(pg));

    GtkGesture *vg = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(vg), 0);
    g_signal_connect(vg, "pressed", G_CALLBACK(on_video_click), NULL);
    gtk_widget_add_controller(ui.frame, GTK_EVENT_CONTROLLER(vg));
    GtkEventController *sc = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect(sc, "scroll", G_CALLBACK(on_scroll), NULL);
    gtk_widget_add_controller(ui.overlay, sc);
    GtkEventController *mo = gtk_event_controller_motion_new();
    g_signal_connect(mo, "motion", G_CALLBACK(on_motion), NULL);
    gtk_widget_add_controller(ui.overlay, mo);

    /* barre de commandes */
    ui.bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_add_css_class(ui.bar, "controls");
    gtk_widget_add_css_class(ui.bar, "toolbar");
    bar_button(T(S_btn_list), 1, NULL);
    bar_button(T(S_btn_prev), 2, NULL);
    bar_button(T(S_btn_next), 3, NULL);
    ui.chan_lbl = gtk_label_new("");
    gtk_widget_add_css_class(ui.chan_lbl, "chanlbl");
    gtk_label_set_ellipsize(GTK_LABEL(ui.chan_lbl), PANGO_ELLIPSIZE_END);
    gtk_label_set_xalign(GTK_LABEL(ui.chan_lbl), 0);
    gtk_widget_set_hexpand(ui.chan_lbl, TRUE);
    gtk_box_append(GTK_BOX(ui.bar), ui.chan_lbl);
    bar_button(T(S_btn_guide), 4, "G");
    ui.btn_lang = bar_button(T(S_btn_lang), 5, "L");
    ui.btn_rec = bar_button(T(S_btn_rec), 6, "R");
    ui.btn_pip = bar_button(T(S_btn_pip), 7, "P");
    bar_button(T(S_btn_recs), 8, "E");
    ui.btn_mute = bar_button(T(S_btn_sound), 9, "M");
    ui.vol = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 150, 1);
    gtk_scale_set_draw_value(GTK_SCALE(ui.vol), FALSE);
    gtk_widget_set_size_request(ui.vol, 110, -1);
    gtk_widget_set_focusable(ui.vol, FALSE);
    gtk_range_set_value(GTK_RANGE(ui.vol), cfg.volume);
    g_signal_connect(ui.vol, "value-changed", G_CALLBACK(on_vol), NULL);
    gtk_box_append(GTK_BOX(ui.bar), ui.vol);
    bar_button(T(S_btn_full), 10, "F11");
    bar_button(T(S_btn_settings), 11, "S");
    bar_button("?", 12, T(S_keys_title));
    gtk_box_append(GTK_BOX(root), ui.bar);

    GtkEventController *k = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(k, GTK_PHASE_CAPTURE);
    g_signal_connect(k, "key-pressed", G_CALLBACK(on_key), NULL);
    gtk_widget_add_controller(GTK_WIDGET(app.win), k);
    g_signal_connect(app.win, "notify::fullscreened", G_CALLBACK(on_fullscreen_changed), NULL);

    ui.vo = video_new(GTK_PICTURE(ui.pic), GTK_ASPECT_FRAME(ui.frame), 1080);
    ui.pvo = video_new(GTK_PICTURE(ui.pip_pic), GTK_ASPECT_FRAME(ui.pip_frame), 360);
    apply_visibility();
    g_timeout_add(300, pip_size_cb, NULL);
}

/* ======================================================================= */
/* Démarrage / fin                                                          */
/* ======================================================================= */
static void really_quit(gpointer p)
{
    (void)p;
    rec_stop_all();
    g_application_quit(G_APPLICATION(app.gapp));   /* ferme aussi les autres fenêtres */
}

static gboolean on_close(GtkWindow *w, gpointer ud)
{
    (void)ud;
    if (!gtk_window_is_fullscreen(w) && !gtk_window_is_maximized(w)) {
        gtk_window_get_default_size(w, &cfg.win_w, &cfg.win_h);
    }
    cfg_save();
    if (rec_active_count() > 0) {
        ui_confirm(w, T(S_quit), T(S_quit_rec), T(S_quit), really_quit, NULL, NULL);
        return TRUE;
    }
    g_application_quit(G_APPLICATION(app.gapp));
    return TRUE;
}

static void create_players(void)
{
    const char *args[] = { "--no-video-title-show", "--no-stats", "--http-reconnect", "--audio-time-stretch",
                           "--no-xlib", "--no-snapshot-preview", "--quiet" };
    app.vlc = libvlc_new((int)G_N_ELEMENTS(args), args);
    if (!app.vlc) {
        g_warning("libvlc_new: %s", libvlc_errmsg() ? libvlc_errmsg() : "?");
        show_status(T(S_vlc_failed));
        return;
    }
    libvlc_set_user_agent(app.vlc, APP_NAME, net_user_agent());
    libvlc_set_app_id(app.vlc, APP_ID, APP_VERSION, APP_ID);
    app.mp = libvlc_media_player_new(app.vlc);
    app.pip = libvlc_media_player_new(app.vlc);
    if (!app.mp || !app.pip) {
        show_status(T(S_vlc_failed));
        return;
    }
    video_attach(ui.vo, app.mp);
    video_attach(ui.pvo, app.pip);
    attach_events(app.mp, 0);
    attach_events(app.pip, 1);
    libvlc_video_set_key_input(app.mp, 0);
    libvlc_video_set_mouse_input(app.mp, 0);
}

static void on_activate(GtkApplication *ga, gpointer ud)
{
    (void)ud;
    if (app.win) { gtk_window_present(app.win); return; }
    app.win = GTK_WINDOW(gtk_application_window_new(ga));
    gtk_window_set_title(app.win, APP_NAME);
    gtk_window_set_icon_name(app.win, APP_ID);
    gtk_window_set_default_size(app.win, MAX(cfg.win_w, 640), MAX(cfg.win_h, 400));
    g_signal_connect(app.win, "close-request", G_CALLBACK(on_close), NULL);
    build_ui();
    create_players();
    apply_volume();
    gtk_window_present(app.win);
    gtk_widget_grab_focus(ui.overlay);
    if (app.vlc) app_reload_playlist();
    update_init(ui.banner);
    if (cfg.check_updates) update_check(NULL, NULL);
    g_timeout_add_seconds(60, minute_tick, NULL);
}

static void on_shutdown(GApplication *ga, gpointer ud)
{
    (void)ga; (void)ud;
    rec_stop_all();
    if (app.mp) { libvlc_media_player_stop(app.mp); libvlc_media_player_release(app.mp); app.mp = NULL; }
    if (app.pip) { libvlc_media_player_stop(app.pip); libvlc_media_player_release(app.pip); app.pip = NULL; }
    if (app.vlc) { libvlc_release(app.vlc); app.vlc = NULL; }
    cfg_save();
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--version")) {
            printf("%s %s (libVLC %s)\n", APP_NAME, APP_VERSION, libvlc_get_version());
            return 0;
        }
    }
    setlocale(LC_ALL, "");
    hr_debug = g_getenv("HR_DEBUG") && *g_getenv("HR_DEBUG") && strcmp(g_getenv("HR_DEBUG"), "0");
#ifdef __GLIBC__
    /* images de 8 Mo copiées à chaque trame : éviter un mmap/munmap à chaque fois */
    mallopt(M_MMAP_THRESHOLD, 64 * 1024 * 1024);
#endif
    net_init();
    cfg_load();
    i18n_init(cfg.lang);
    g_set_application_name(APP_NAME);
    app.gapp = gtk_application_new(APP_ID, G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app.gapp, "activate", G_CALLBACK(on_activate), NULL);
    g_signal_connect(app.gapp, "shutdown", G_CALLBACK(on_shutdown), NULL);
    int r = g_application_run(G_APPLICATION(app.gapp), 1, argv);   /* options déjà traitées */
    g_object_unref(app.gapp);
    playlist_free(&app.ch);
    guide_free(&app.guide);
    return r;
}
