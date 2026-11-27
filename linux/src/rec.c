/*
 * rec.c — enregistrement : copie du flux HTTP de la chaîne dans un fichier .ts
 * (« AAAA-MM-JJ HHhMM - Chaîne - Émission.ts »), jusqu'à l'heure de fin (fin de
 * l'émission + marge, ou durée choisie) ou sans limite.  Un thread par enregistrement
 * (libcurl) ; quelques nouvelles tentatives si le flux coupe.
 *
 * Fenêtre « Enregistrements » : en cours (arrêter), fichiers (lire avec reprise de
 * la position, lire depuis le début, supprimer), ouvrir le dossier.
 * Lecteur d'enregistrement : fenêtre séparée, barre de position, ±30 s, position
 * mémorisée dans ~/.config/homerun-player/positions.ini.
 *                              (↔ src/record.h, RecordService.kt, RecordingsActivity.kt,
 *                                 RecPlayerActivity.kt)
 */
#include <curl/curl.h>
#include <errno.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"

typedef struct Rec {
    int id;
    char *ch_name, *title, *url, *path;
    gint64 start;
    gint64 end_at;            /* secondes epoch, 0 = sans limite (lu par le thread) */
    volatile gint64 bytes;
    volatile gint stop;
    char *error;
    GThread *th;
} Rec;

static GList *g_recs;
static int g_next_id = 1;

static void recs_window_rebuild(void);

int rec_active_count(void) { return (int)g_list_length(g_recs); }

gboolean rec_is_recording(const char *url)
{
    for (GList *l = g_recs; l; l = l->next)
        if (!strcmp(((Rec *)l->data)->url, url)) return TRUE;
    return FALSE;
}

/* ---- thread de copie ---------------------------------------------------- */
typedef struct { Rec *r; FILE *f; gint64 got; } Ctx;

static size_t on_write(char *d, size_t sz, size_t nm, void *ud)
{
    Ctx *c = ud;
    size_t n = sz * nm;
    if (fwrite(d, 1, n, c->f) != n) return 0;
    c->got += (gint64)n;
    c->r->bytes += (gint64)n;
    return n;
}

static int on_progress(void *ud, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d)
{
    (void)a; (void)b; (void)c; (void)d;
    Ctx *x = ud;
    if (g_atomic_int_get(&x->r->stop)) return 1;
    gint64 end = x->r->end_at;
    return end && now_s() >= end ? 1 : 0;
}

static gboolean rec_finished(gpointer p);

static gpointer rec_thread(gpointer p)
{
    Rec *r = p;
    int tries = 0;
    Ctx c = { r, g_fopen(r->path, "ab"), 0 };
    if (!c.f) {
        r->error = g_strdup(g_strerror(errno));
    } else {
        while (!g_atomic_int_get(&r->stop) && (r->end_at == 0 || now_s() < r->end_at)) {
            CURL *h = curl_easy_init();
            if (!h) break;
            char eb[CURL_ERROR_SIZE] = "";
            c.got = 0;
            curl_easy_setopt(h, CURLOPT_URL, r->url);
            curl_easy_setopt(h, CURLOPT_USERAGENT, net_user_agent());
            curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
            curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 15L);
            curl_easy_setopt(h, CURLOPT_LOW_SPEED_LIMIT, 1L);    /* flux bloqué 15 s : on recommence */
            curl_easy_setopt(h, CURLOPT_LOW_SPEED_TIME, 15L);
            curl_easy_setopt(h, CURLOPT_FAILONERROR, 1L);
            curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, on_write);
            curl_easy_setopt(h, CURLOPT_WRITEDATA, &c);
            curl_easy_setopt(h, CURLOPT_NOPROGRESS, 0L);
            curl_easy_setopt(h, CURLOPT_XFERINFOFUNCTION, on_progress);
            curl_easy_setopt(h, CURLOPT_XFERINFODATA, &c);
            curl_easy_setopt(h, CURLOPT_ERRORBUFFER, eb);
            CURLcode res = curl_easy_perform(h);
            long code = 0;
            curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &code);
            curl_easy_cleanup(h);
            fflush(c.f);
            if (res == CURLE_ABORTED_BY_CALLBACK) break;   /* arrêt demandé ou heure de fin */
            if (res == CURLE_WRITE_ERROR) { g_free(r->error); r->error = g_strdup(g_strerror(errno)); break; }
            if (res != CURLE_OK || c.got == 0) {
                g_free(r->error);
                if (code == 503) r->error = g_strdup(T(S_rec_err_no_tuner));
                else r->error = g_strdup(res != CURLE_OK ? (eb[0] ? eb : curl_easy_strerror(res)) : T(S_rec_no_data));
                if (++tries > 5) break;
                for (int i = 0; i < 20 && !g_atomic_int_get(&r->stop); i++) g_usleep(100000);
            } else tries = 0;
        }
        fclose(c.f);
    }
    g_idle_add(rec_finished, r);
    return NULL;
}

static void rec_free(Rec *r)
{
    g_free(r->ch_name);
    g_free(r->title);
    g_free(r->url);
    g_free(r->path);
    g_free(r->error);
    g_free(r);
}

static gboolean rec_finished(gpointer p)
{
    Rec *r = p;
    if (r->th) { g_thread_join(r->th); r->th = NULL; }
    g_recs = g_list_remove(g_recs, r);
    char dur[64];
    fmt_dur(now_s() - r->start, dur, sizeof dur);
    char *size = fmt_size(r->bytes), *msg;
    if (r->bytes == 0) {
        g_unlink(r->path);
        msg = TF(S_rec_failed, r->ch_name, r->error ? r->error : T(S_rec_no_data));
    } else if (r->error && !r->stop && (r->end_at == 0 || now_s() < r->end_at)) {
        msg = TF(S_rec_stopped_err, r->ch_name, r->error, dur, size);
    } else {
        msg = TF(S_rec_done, r->ch_name, dur, size);
    }
    app_flash(msg);
    g_message("%s", msg);
    g_free(msg);
    g_free(size);
    rec_free(r);
    app_rec_changed();
    recs_window_rebuild();
    return G_SOURCE_REMOVE;
}

void rec_start(int idx, const char *title, gint64 end_at)
{
    if (idx < 0 || idx >= app.ch.n) return;
    const Channel *ch = &app.ch.v[idx];
    char *dir = cfg_rec_dir();
    if (g_mkdir_with_parents(dir, 0755) != 0) {
        char *s = TF(S_rec_dir_error, dir);
        ui_message(app.win, T(S_recordings), s);
        g_free(s);
        g_free(dir);
        return;
    }
    char base[512];
    rec_basename(time(NULL), ch->name, title, base, sizeof base);
    char *path = rec_unique_path(dir, base);
    g_free(dir);
    if (!path) return;
    Rec *r = g_new0(Rec, 1);
    r->id = g_next_id++;
    r->ch_name = g_strdup(ch->name);
    r->title = g_strdup(title ? title : "");
    r->url = g_strdup(ch->url);
    r->path = g_strdup(path);
    free(path);
    r->start = now_s();
    r->end_at = end_at;
    g_recs = g_list_append(g_recs, r);
    r->th = g_thread_new("rec", rec_thread, r);
    char *s;
    if (end_at > 0) {
        char *t = hm(end_at);
        s = TF(S_rec_started_until, ch->name, t);
        g_free(t);
    } else s = TF(S_rec_started, ch->name);
    app_flash(s);
    g_free(s);
    app_rec_changed();
    recs_window_rebuild();
}

static void rec_stop(Rec *r)
{
    g_atomic_int_set(&r->stop, 1);
}

void rec_stop_all(void)
{
    for (GList *l = g_recs; l; l = l->next) rec_stop((Rec *)l->data);
    /* à la fermeture : attendre que les fichiers soient fermés (au plus ~2 s) */
    for (GList *l = g_recs; l; l = l->next) {
        Rec *r = l->data;
        if (r->th) { g_thread_join(r->th); r->th = NULL; }
    }
}

static Rec *rec_by_id(int id)
{
    for (GList *l = g_recs; l; l = l->next)
        if (((Rec *)l->data)->id == id) return l->data;
    return NULL;
}

/* ---- menu « Enregistrer » ------------------------------------------------ */
typedef struct { int idx; char *title; gint64 end; int stop_id; } RecChoice;

static void rc_free(gpointer p)
{
    RecChoice *c = p;
    g_free(c->title);
    g_free(c);
}

static void on_rec_choice(gpointer p)
{
    RecChoice *c = p;
    if (c->stop_id) {
        Rec *r = rec_by_id(c->stop_id);
        if (r) rec_stop(r);
        return;
    }
    rec_start(c->idx, c->title, c->end);
}

static void add_rc(GtkWidget *m, const char *label, int idx, const char *title, gint64 end, int stop_id)
{
    RecChoice *c = g_new0(RecChoice, 1);
    c->idx = idx;
    c->title = g_strdup(title);
    c->end = end;
    c->stop_id = stop_id;
    menu_add(m, label, on_rec_choice, c, rc_free);
}

void rec_menu(GtkWidget *anchor, int idx)
{
    if (idx < 0 || idx >= app.ch.n) return;
    const Channel *ch = &app.ch.v[idx];
    GtkWidget *m = menu_new(anchor);
    for (GList *l = g_recs; l; l = l->next) {
        Rec *r = l->data;
        if (strcmp(r->url, ch->url)) continue;
        char *s = TF(S_rec_stop_one, r->ch_name);
        add_rc(m, s, idx, NULL, 0, r->id);
        g_free(s);
    }
    gint64 now = now_s();
    const Prog *p = app_prog_at(idx, now);
    if (p) {
        gint64 end = p->end + (gint64)cfg.rec_margin * 60;
        char *t = hm(end), *s = TF(S_rec_until_end, p->title, t);
        add_rc(m, s, idx, p->title, end, 0);
        g_free(s);
        g_free(t);
    }
    static const int mins[] = { 30, 60, 120, 180 };
    for (size_t i = 0; i < G_N_ELEMENTS(mins); i++) {
        char d[32];
        fmt_dur(mins[i] * 60, d, sizeof d);
        char *s = TF(S_rec_for, d);
        add_rc(m, s, idx, p ? p->title : NULL, now + mins[i] * 60, 0);
        g_free(s);
    }
    add_rc(m, T(S_rec_unlimited), idx, p ? p->title : NULL, 0, 0);
    menu_popup(m);
}

/* ======================================================================= */
/* Positions de lecture                                                     */
/* ======================================================================= */
static char *pos_key(const char *path)
{
    char *b = g_path_get_basename(path);
    char *k = g_compute_checksum_for_string(G_CHECKSUM_MD5, b, -1);
    g_free(b);
    return k;
}

static gint64 pos_get(const char *path)
{
    GKeyFile *k = g_key_file_new();
    char *f = cfg_path("positions.ini"), *key = pos_key(path);
    gint64 v = 0;
    if (g_key_file_load_from_file(k, f, G_KEY_FILE_NONE, NULL)) v = g_key_file_get_int64(k, "pos", key, NULL);
    g_free(f);
    g_free(key);
    g_key_file_free(k);
    return v;
}

static void pos_set(const char *path, gint64 ms)
{
    GKeyFile *k = g_key_file_new();
    char *f = cfg_path("positions.ini"), *key = pos_key(path);
    g_key_file_load_from_file(k, f, G_KEY_FILE_NONE, NULL);
    if (ms > 0) g_key_file_set_int64(k, "pos", key, ms);
    else g_key_file_remove_key(k, "pos", key, NULL);
    char *dir = g_path_get_dirname(f);
    g_mkdir_with_parents(dir, 0700);
    g_key_file_save_to_file(k, f, NULL);
    g_free(dir);
    g_free(f);
    g_free(key);
    g_key_file_free(k);
}

static char *fmt_ms(gint64 ms)
{
    gint64 s = MAX(ms, 0) / 1000;
    if (s >= 3600) return g_strdup_printf("%d:%02d:%02d", (int)(s / 3600), (int)(s / 60 % 60), (int)(s % 60));
    return g_strdup_printf("%d:%02d", (int)(s / 60), (int)(s % 60));
}

/* ======================================================================= */
/* Lecteur d'enregistrement                                                 */
/* ======================================================================= */
typedef struct {
    GtkWidget *win, *frame, *pic, *scale, *time, *btn_play;
    VideoOut *vo;
    libvlc_media_player_t *mp;
    char *path;
    gint64 resume;
    gboolean resumed, dragging, ended;
    guint tick_id, save_count;
} RP;

/* Lecteurs ouverts : un événement de VLC arrivé après la fermeture est ignoré. */
static GHashTable *g_rps;

typedef struct { RP *rp; int type; } RPEv;

static gboolean rp_event_idle(gpointer p)
{
    RPEv *e = p;
    RP *rp = e->rp;
    int type = e->type;
    g_free(e);
    if (!g_rps || !g_hash_table_contains(g_rps, rp) || !rp->mp) return G_SOURCE_REMOVE;
    if (type == libvlc_MediaPlayerPlaying && !rp->resumed) {
        rp->resumed = TRUE;
        libvlc_audio_set_volume(rp->mp, cfg.volume);
        gint64 len = libvlc_media_player_get_length(rp->mp);
        if (rp->resume > 0 && (len <= 0 || rp->resume < len - 5000)) libvlc_media_player_set_time(rp->mp, rp->resume);
    } else if (type == libvlc_MediaPlayerVout) {
        video_update_aspect(rp->vo);
    } else if (type == libvlc_MediaPlayerEndReached) {
        rp->ended = TRUE;
        pos_set(rp->path, 0);
    }
    return G_SOURCE_REMOVE;
}

static void rp_event(const libvlc_event_t *ev, void *ud)
{
    RPEv *e = g_new(RPEv, 1);
    e->rp = ud;
    e->type = ev->type;
    g_idle_add(rp_event_idle, e);
}

static void rp_save(RP *rp)
{
    if (!rp->mp || rp->ended) return;
    gint64 t = libvlc_media_player_get_time(rp->mp), len = libvlc_media_player_get_length(rp->mp);
    if (t > 0) pos_set(rp->path, len > 0 && t > len - 10000 ? 0 : t);
}

static gboolean rp_tick(gpointer p)
{
    RP *rp = p;
    if (!rp->mp) return G_SOURCE_REMOVE;
    gint64 t = libvlc_media_player_get_time(rp->mp), len = libvlc_media_player_get_length(rp->mp);
    if (!rp->dragging && len > 0) gtk_range_set_value(GTK_RANGE(rp->scale), (double)t * 1000.0 / (double)len);
    char *a = fmt_ms(t), *b = fmt_ms(len);
    char *s = g_strdup_printf("%s / %s", a, b);
    gtk_label_set_text(GTK_LABEL(rp->time), s);
    g_free(s); g_free(a); g_free(b);
    gtk_button_set_label(GTK_BUTTON(rp->btn_play), T(libvlc_media_player_is_playing(rp->mp) ? S_btn_pause : S_btn_play));
    if (++rp->save_count % 20 == 0) rp_save(rp);   /* toutes les 10 s */
    return G_SOURCE_CONTINUE;
}

static void rp_seek(RP *rp, gint64 delta)
{
    gint64 t = libvlc_media_player_get_time(rp->mp) + delta;
    libvlc_media_player_set_time(rp->mp, MAX(t, 0));
}

static void rp_toggle(RP *rp)
{
    if (rp->ended) {
        rp->ended = FALSE;
        libvlc_media_player_stop(rp->mp);
        libvlc_media_player_play(rp->mp);
    } else libvlc_media_player_pause(rp->mp);
}

static void on_rp_btn(GtkButton *b, gpointer ud)
{
    RP *rp = g_object_get_data(G_OBJECT(b), "rp");
    switch (GPOINTER_TO_INT(ud)) {
    case 0: rp_toggle(rp); break;
    case 1: rp_seek(rp, -30000); break;
    case 2: rp_seek(rp, 30000); break;
    case 3:
        if (gtk_window_is_fullscreen(GTK_WINDOW(rp->win))) gtk_window_unfullscreen(GTK_WINDOW(rp->win));
        else gtk_window_fullscreen(GTK_WINDOW(rp->win));
        break;
    }
}

static gboolean on_rp_change(GtkRange *r, GtkScrollType st, double v, gpointer ud)
{
    (void)r; (void)st;
    RP *rp = ud;
    libvlc_media_player_set_position(rp->mp, (float)CLAMP(v / 1000.0, 0, 1));
    return FALSE;
}

static gboolean on_rp_key(GtkEventControllerKey *c, guint kv, guint code, GdkModifierType st, gpointer ud)
{
    (void)c; (void)code; (void)st;
    RP *rp = ud;
    switch (kv) {
    case GDK_KEY_space: case GDK_KEY_k: rp_toggle(rp); return TRUE;
    case GDK_KEY_Left: rp_seek(rp, -10000); return TRUE;
    case GDK_KEY_Right: rp_seek(rp, 10000); return TRUE;
    case GDK_KEY_Page_Down: case GDK_KEY_Down: rp_seek(rp, -60000); return TRUE;
    case GDK_KEY_Page_Up: case GDK_KEY_Up: rp_seek(rp, 60000); return TRUE;
    case GDK_KEY_f: case GDK_KEY_F11: on_rp_btn(GTK_BUTTON(rp->btn_play), GINT_TO_POINTER(3)); return TRUE;
    case GDK_KEY_l: tracks_menu(rp->btn_play, rp->mp); return TRUE;
    case GDK_KEY_Escape:
        if (gtk_window_is_fullscreen(GTK_WINDOW(rp->win))) { gtk_window_unfullscreen(GTK_WINDOW(rp->win)); return TRUE; }
        gtk_window_close(GTK_WINDOW(rp->win));
        return TRUE;
    default: return FALSE;
    }
}

static void on_rp_dbl(GtkGestureClick *g, int n, double x, double y, gpointer ud)
{
    (void)g; (void)x; (void)y;
    RP *rp = ud;
    if (n == 2) on_rp_btn(GTK_BUTTON(rp->btn_play), GINT_TO_POINTER(3));
}

static void on_rp_destroy(GtkWidget *w, gpointer ud)
{
    (void)w;
    RP *rp = ud;
    if (rp->tick_id) g_source_remove(rp->tick_id);   /* plus de mise à jour des widgets détruits */
    rp->tick_id = 0;
}

static void rp_free(gpointer p)
{
    RP *rp = p;
    if (rp->tick_id) g_source_remove(rp->tick_id);
    g_hash_table_remove(g_rps, rp);
    if (rp->mp) {
        rp_save(rp);
        libvlc_media_player_stop(rp->mp);
        libvlc_media_player_release(rp->mp);
        rp->mp = NULL;
    }
    video_free(rp->vo);
    g_free(rp->path);
    g_free(rp);
    app_pause_live(FALSE);   /* reprend le direct */
    recs_window_rebuild();
}

static GtkWidget *rp_button(RP *rp, GtkWidget *bar, const char *label, int id)
{
    GtkWidget *b = gtk_button_new_with_label(label);
    gtk_widget_add_css_class(b, "flat");
    gtk_widget_set_focus_on_click(b, FALSE);
    g_object_set_data(G_OBJECT(b), "rp", rp);
    g_signal_connect(b, "clicked", G_CALLBACK(on_rp_btn), GINT_TO_POINTER(id));
    gtk_box_append(GTK_BOX(bar), b);
    return b;
}

static void rec_play(const char *path, gboolean from_start)
{
    if (!app.vlc) return;
    app_pause_live(TRUE);   /* libère le tuner et le son du direct */
    RP *rp = g_new0(RP, 1);
    rp->path = g_strdup(path);
    rp->resume = from_start ? 0 : pos_get(path);
    char *name = g_path_get_basename(path);
    rp->win = ui_window(name, 960, 600);
    g_free(name);
    gtk_window_set_transient_for(GTK_WINDOW(rp->win), NULL);
    g_object_set_data_full(G_OBJECT(rp->win), "rp", rp, rp_free);
    g_signal_connect(rp->win, "destroy", G_CALLBACK(on_rp_destroy), rp);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(rp->win), box);
    GtkWidget *bg = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(bg, "video");
    gtk_widget_set_vexpand(bg, TRUE);
    rp->frame = gtk_aspect_frame_new(0.5f, 0.5f, 16.0f / 9, FALSE);
    gtk_widget_set_vexpand(rp->frame, TRUE);
    rp->pic = gtk_picture_new();
    gtk_aspect_frame_set_child(GTK_ASPECT_FRAME(rp->frame), rp->pic);
    gtk_box_append(GTK_BOX(bg), rp->frame);
    gtk_box_append(GTK_BOX(box), bg);
    GtkGesture *g = gtk_gesture_click_new();
    g_signal_connect(g, "pressed", G_CALLBACK(on_rp_dbl), rp);
    gtk_widget_add_controller(bg, GTK_EVENT_CONTROLLER(g));

    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_add_css_class(bar, "controls");
    rp->btn_play = rp_button(rp, bar, T(S_btn_pause), 0);
    rp_button(rp, bar, T(S_btn_back30), 1);
    rp_button(rp, bar, T(S_btn_fwd30), 2);
    rp->scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 1000, 1);
    gtk_scale_set_draw_value(GTK_SCALE(rp->scale), FALSE);
    gtk_widget_set_hexpand(rp->scale, TRUE);
    gtk_widget_set_focusable(rp->scale, FALSE);
    g_signal_connect(rp->scale, "change-value", G_CALLBACK(on_rp_change), rp);
    gtk_box_append(GTK_BOX(bar), rp->scale);
    rp->time = gtk_label_new("0:00 / 0:00");
    gtk_box_append(GTK_BOX(bar), rp->time);
    rp_button(rp, bar, T(S_btn_full), 3);
    gtk_box_append(GTK_BOX(box), bar);

    GtkEventController *k = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(k, GTK_PHASE_CAPTURE);
    g_signal_connect(k, "key-pressed", G_CALLBACK(on_rp_key), rp);
    gtk_widget_add_controller(rp->win, k);

    rp->vo = video_new(GTK_PICTURE(rp->pic), GTK_ASPECT_FRAME(rp->frame), 1080);
    rp->mp = libvlc_media_player_new(app.vlc);
    video_attach(rp->vo, rp->mp);
    if (!g_rps) g_rps = g_hash_table_new(NULL, NULL);
    g_hash_table_add(g_rps, rp);
    libvlc_event_manager_t *em = libvlc_media_player_event_manager(rp->mp);
    libvlc_event_attach(em, libvlc_MediaPlayerPlaying, rp_event, rp);
    libvlc_event_attach(em, libvlc_MediaPlayerVout, rp_event, rp);
    libvlc_event_attach(em, libvlc_MediaPlayerEndReached, rp_event, rp);
    libvlc_media_t *m = libvlc_media_new_path(app.vlc, path);
    libvlc_media_player_set_media(rp->mp, m);
    video_set_media(rp->vo, m);
    libvlc_media_release(m);
    app_apply_deinterlace(rp->mp);
    libvlc_media_player_play(rp->mp);
    rp->tick_id = g_timeout_add(500, rp_tick, rp);
    gtk_window_present(GTK_WINDOW(rp->win));
}

/* ======================================================================= */
/* Fenêtre « Enregistrements »                                              */
/* ======================================================================= */
static struct { GtkWidget *win, *list, *folder; guint tick_id; GHashTable *live; } W;

static void on_stop_confirmed(gpointer p)
{
    Rec *r = rec_by_id(GPOINTER_TO_INT(p));
    if (r) rec_stop(r);
}

static void on_stop_clicked(GtkButton *b, gpointer ud)
{
    (void)b;
    ui_confirm(GTK_WINDOW(W.win), T(S_rec_stop), T(S_rec_stop_confirm), T(S_rec_stop), on_stop_confirmed, ud, NULL);
}

static void on_play_clicked(GtkButton *b, gpointer ud)
{
    const char *path = g_object_get_data(G_OBJECT(b), "path");
    rec_play(path, GPOINTER_TO_INT(ud));
}

static void on_delete_confirmed(gpointer p)
{
    const char *path = p;
    if (g_unlink(path) != 0) ui_message(GTK_WINDOW(W.win), T(S_rec_delete_title), g_strerror(errno));
    else pos_set(path, 0);
    recs_window_rebuild();
}

static void on_delete_clicked(GtkButton *b, gpointer ud)
{
    (void)ud;
    const char *path = g_object_get_data(G_OBJECT(b), "path");
    char *name = g_path_get_basename(path);
    char *msg = TF(S_rec_delete_msg, name);
    ui_confirm(GTK_WINDOW(W.win), T(S_rec_delete_title), msg, T(S_delete), on_delete_confirmed, g_strdup(path), g_free);
    g_free(msg);
    g_free(name);
}

static GtkWidget *row_box(const char *l1, const char *l2, GtkWidget **lbl2)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_start(row, 8);
    gtk_widget_set_margin_end(row, 8);
    gtk_widget_set_margin_top(row, 4);
    gtk_widget_set_margin_bottom(row, 4);
    GtkWidget *txt = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(txt, TRUE);
    GtkWidget *a = gtk_label_new(l1);
    gtk_label_set_xalign(GTK_LABEL(a), 0);
    gtk_label_set_ellipsize(GTK_LABEL(a), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_add_css_class(a, "chname");
    GtkWidget *b = gtk_label_new(l2);
    gtk_label_set_xalign(GTK_LABEL(b), 0);
    gtk_widget_add_css_class(b, "dim-label");
    gtk_box_append(GTK_BOX(txt), a);
    gtk_box_append(GTK_BOX(txt), b);
    gtk_box_append(GTK_BOX(row), txt);
    if (lbl2) *lbl2 = b;
    return row;
}

static char *live_line(Rec *r)
{
    char *size = fmt_size(r->bytes), *end;
    if (r->end_at > 0) {
        char *t = hm(r->end_at);
        end = TF(S_rec_until, t);
        g_free(t);
    } else end = g_strdup(T(S_rec_no_limit));
    char *s = TF(S_rec_live_line, size, end);
    g_free(size);
    g_free(end);
    return s;
}

typedef struct { char *path; gint64 mtime, size; } RecFile;

static gint cmp_files(gconstpointer a, gconstpointer b)
{
    const RecFile *x = a, *y = b;
    return x->mtime < y->mtime ? 1 : x->mtime > y->mtime ? -1 : 0;
}

static void recs_window_rebuild(void)
{
    if (!W.win) return;
    gtk_list_box_remove_all(GTK_LIST_BOX(W.list));
    g_hash_table_remove_all(W.live);
    char *dir = cfg_rec_dir();
    char *f = TF(S_rec_folder, dir);
    gtk_label_set_text(GTK_LABEL(W.folder), f);
    g_free(f);

    for (GList *l = g_recs; l; l = l->next) {
        Rec *r = l->data;
        char *t1 = *r->title ? g_strdup_printf("● %s – %s", r->ch_name, r->title) : g_strdup_printf("● %s", r->ch_name);
        char *t2 = live_line(r);
        GtkWidget *l2 = NULL, *row = row_box(t1, t2, &l2);
        g_hash_table_insert(W.live, GINT_TO_POINTER(r->id), l2);
        GtkWidget *b = gtk_button_new_with_label(T(S_rec_stop));
        gtk_widget_add_css_class(b, "destructive-action");
        gtk_widget_set_valign(b, GTK_ALIGN_CENTER);
        g_signal_connect(b, "clicked", G_CALLBACK(on_stop_clicked), GINT_TO_POINTER(r->id));
        gtk_box_append(GTK_BOX(row), b);
        gtk_list_box_append(GTK_LIST_BOX(W.list), row);
        g_free(t1);
        g_free(t2);
    }

    GArray *files = g_array_new(FALSE, FALSE, sizeof(RecFile));
    GDir *d = g_dir_open(dir, 0, NULL);
    const char *n;
    while (d && (n = g_dir_read_name(d))) {
        if (!g_str_has_suffix(n, ".ts")) continue;
        char *p = g_build_filename(dir, n, NULL);
        gboolean busy = FALSE;
        for (GList *l = g_recs; l; l = l->next)
            if (!strcmp(((Rec *)l->data)->path, p)) busy = TRUE;
        GStatBuf st;
        if (busy || g_stat(p, &st) != 0) { g_free(p); continue; }
        RecFile rf = { p, (gint64)st.st_mtime, (gint64)st.st_size };
        g_array_append_val(files, rf);
    }
    if (d) g_dir_close(d);
    g_array_sort(files, cmp_files);
    for (guint i = 0; i < files->len; i++) {
        RecFile *rf = &g_array_index(files, RecFile, i);
        char *base = g_path_get_basename(rf->path);
        base[strlen(base) - 3] = 0;   /* sans « .ts » */
        char *size = fmt_size(rf->size);
        gint64 pos = pos_get(rf->path);
        char *t2;
        if (pos > 0) {
            char *pm = fmt_ms(pos), *ra = TF(S_rec_resume_at, pm);
            t2 = g_strdup_printf("%s · %s", size, ra);
            g_free(pm);
            g_free(ra);
        } else t2 = g_strdup(size);
        GtkWidget *row = row_box(base, t2, NULL);
        GtkWidget *bp = gtk_button_new_with_label(T(S_rec_play));
        gtk_widget_add_css_class(bp, "suggested-action");
        g_object_set_data_full(G_OBJECT(bp), "path", g_strdup(rf->path), g_free);
        g_signal_connect(bp, "clicked", G_CALLBACK(on_play_clicked), GINT_TO_POINTER(0));
        gtk_widget_set_valign(bp, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(row), bp);
        if (pos > 0) {
            GtkWidget *bs = gtk_button_new_with_label(T(S_rec_play_start));
            g_object_set_data_full(G_OBJECT(bs), "path", g_strdup(rf->path), g_free);
            g_signal_connect(bs, "clicked", G_CALLBACK(on_play_clicked), GINT_TO_POINTER(1));
            gtk_widget_set_valign(bs, GTK_ALIGN_CENTER);
            gtk_box_append(GTK_BOX(row), bs);
        }
        GtkWidget *bd = gtk_button_new_with_label(T(S_delete));
        g_object_set_data_full(G_OBJECT(bd), "path", g_strdup(rf->path), g_free);
        g_signal_connect(bd, "clicked", G_CALLBACK(on_delete_clicked), NULL);
        gtk_widget_set_valign(bd, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(row), bd);
        gtk_list_box_append(GTK_LIST_BOX(W.list), row);
        g_free(t2);
        g_free(size);
        g_free(base);
        g_free(rf->path);
    }
    if (!g_recs && files->len == 0) {
        GtkWidget *e = gtk_label_new(T(S_rec_empty));
        gtk_widget_add_css_class(e, "dim-label");
        gtk_widget_set_margin_top(e, 30);
        gtk_widget_set_margin_bottom(e, 30);
        gtk_list_box_append(GTK_LIST_BOX(W.list), e);
    }
    g_array_free(files, TRUE);
    g_free(dir);
}

static gboolean recs_tick(gpointer p)
{
    (void)p;
    for (GList *l = g_recs; l; l = l->next) {
        Rec *r = l->data;
        GtkWidget *lbl = g_hash_table_lookup(W.live, GINT_TO_POINTER(r->id));
        if (!lbl) continue;
        char *s = live_line(r);
        gtk_label_set_text(GTK_LABEL(lbl), s);
        g_free(s);
    }
    return G_SOURCE_CONTINUE;
}

static void on_open_folder(GtkButton *b, gpointer ud)
{
    (void)b; (void)ud;
    char *dir = cfg_rec_dir();
    g_mkdir_with_parents(dir, 0755);
    char *uri = g_filename_to_uri(dir, NULL, NULL);
    if (uri) ui_open_uri(GTK_WINDOW(W.win), uri);
    g_free(uri);
    g_free(dir);
}

static void on_recs_destroy(GtkWidget *w, gpointer ud)
{
    (void)w; (void)ud;
    if (W.tick_id) g_source_remove(W.tick_id);
    g_hash_table_destroy(W.live);
    memset(&W, 0, sizeof W);
}

void recordings_window_show(void)
{
    if (W.win) { gtk_window_present(GTK_WINDOW(W.win)); return; }
    W.win = ui_window(T(S_recordings), 820, 520);
    g_signal_connect(W.win, "destroy", G_CALLBACK(on_recs_destroy), NULL);
    W.live = g_hash_table_new(NULL, NULL);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(W.win), box);
    GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(top, 10);
    gtk_widget_set_margin_end(top, 10);
    gtk_widget_set_margin_top(top, 8);
    gtk_widget_set_margin_bottom(top, 8);
    W.folder = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(W.folder), 0);
    gtk_label_set_ellipsize(GTK_LABEL(W.folder), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_hexpand(W.folder, TRUE);
    gtk_box_append(GTK_BOX(top), W.folder);
    GtkWidget *of = gtk_button_new_with_label(T(S_rec_open_folder));
    g_signal_connect(of, "clicked", G_CALLBACK(on_open_folder), NULL);
    gtk_box_append(GTK_BOX(top), of);
    gtk_box_append(GTK_BOX(box), top);
    GtkWidget *sw = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(sw, TRUE);
    W.list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(W.list), GTK_SELECTION_NONE);
    gtk_widget_add_css_class(W.list, "rich-list");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), W.list);
    gtk_box_append(GTK_BOX(box), sw);
    recs_window_rebuild();
    W.tick_id = g_timeout_add_seconds(2, recs_tick, NULL);
    gtk_window_present(GTK_WINDOW(W.win));
}
