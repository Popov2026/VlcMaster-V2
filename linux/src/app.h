/*
 * app.h — HomeRun Player pour Linux (GTK 4 + libVLC 3).
 *
 * Fichiers (équivalents Windows src/ et Android android/…/homerun/) :
 *   main.c         fenêtre principale : lecture, liste, zapping, PIP, HUD, clavier
 *                                                     ↔ vlcmaster.c, pip.h / MainActivity.kt
 *   video.c        image libVLC rendue en mémoire puis affichée par GTK (X11 et Wayland)
 *   core.c         playlist M3U, guide, clés de pistes, versions (sans GTK, testé)
 *   json.c         lecteur JSON
 *   net.c          HTTP (libcurl) et travaux en arrière-plan
 *   config.c       réglages (~/.config/homerun-player/settings.ini)
 *   i18n.c         textes en 6 langues (strings_gen.h, généré par tools/gen_strings.py)
 *   tracks.c       langues audio / sous-titres          ↔ tracks.h / Tracks.kt
 *   guide_ui.c     grille du guide TV                   ↔ guide_ui.h / GuideActivity.kt
 *   rec.c          enregistrement, liste, lecture        ↔ record.h / RecordService.kt…
 *   tuner_ui.c     tuner : infos, firmware, scan         ↔ tuner.h / TunerActivity.kt
 *   settings_ui.c  fenêtre Paramètres                    ↔ SettingsActivity.kt
 *   update.c       bandeau de nouvelle version (GitHub)  ↔ update.h / Updater.kt
 *   ui.c           petits outils d'interface (menus, messages)
 */
#ifndef HR_APP_H
#define HR_APP_H

#include <gtk/gtk.h>
#include <vlc/vlc.h>

#include "core.h"
#include "strings_ids.h"

#ifndef APP_VERSION
#define APP_VERSION "3.7"
#endif
#define APP_ID      "fr.popov.HomeRunPlayer"
#define APP_NAME    "HomeRun Player"
#define UPD_REPO    "Popov2026/VlcMaster-V2"
#define DEFAULT_PLAYLIST "http://192.168.1.90/lineup.m3u"

extern gboolean hr_debug;                        /* HR_DEBUG=1 : messages de diagnostic */

/* ---- config.c ---------------------------------------------------------- */
typedef struct {
    char *playlist_url;
    gboolean resume, hw, show_guide, check_updates, panel;
    int caching, rec_margin, volume, pip_corner, win_w, win_h;
    char *deint, *aspect, *rec_dir, *lang;
    char *last_channel, *audio_pref, *spu_pref, *skipped_version;
} Config;
extern Config cfg;
void cfg_load(void);
void cfg_save(void);
void cfg_set(char **field, const char *value);   /* remplace une chaîne de Config */
char *cfg_tuner_base(void);                      /* « http://hôte[:port] » ou NULL (g_free) */
char *cfg_path(const char *name);                /* ~/.config/homerun-player/<name> (g_free) */
char *cfg_rec_dir(void);                         /* dossier des enregistrements (g_free) */

/* ---- i18n.c ------------------------------------------------------------ */
void i18n_init(const char *lang);                /* "" = langue du système */
int i18n_lang(void);                             /* 0 fr, 1 en, 2 es, 3 de, 4 it, 5 pt */
const char *T(int id);
char *TF(int id, ...);                           /* g_strdup_printf du texte traduit (g_free) */
const char *TP(int id, int n);                   /* pluriel */
const char *const *TA(int id);                   /* libellés d'une liste (NULL à la fin) */
const char *const *TV(int id);                   /* valeurs correspondantes */

/* ---- net.c ------------------------------------------------------------- */
void net_init(void);
char *net_get(const char *url, int timeout_ms, char **err);   /* texte (g_free) ou NULL + *err (g_free) */
gboolean net_post(const char *url, int timeout_ms, char **err);
char *net_fetch(const char *url, int timeout_ms, char **err, void *ud);   /* FetchFn (malloc/free) */
/* Lance work(data) dans un thread puis done(data) dans la boucle GTK. */
void bg_run(void (*work)(gpointer), void (*done)(gpointer), gpointer data);
const char *net_user_agent(void);

/* ---- video.c ----------------------------------------------------------- */
typedef struct VideoOut VideoOut;
/* pic : image GTK ; frame : cadre d'aspect (ratio) ; max_h : hauteur maxi décodée. */
VideoOut *video_new(GtkPicture *pic, GtkAspectFrame *frame, int max_h);
void video_attach(VideoOut *v, libvlc_media_player_t *mp);
void video_set_media(VideoOut *v, libvlc_media_t *m);
void video_update_aspect(VideoOut *v);           /* format choisi ou d'après la piste vidéo */
void video_clear(VideoOut *v);
void video_free(VideoOut *v);

/* ---- main.c ------------------------------------------------------------ */
typedef struct {
    GtkApplication *gapp;
    GtkWindow *win;
    libvlc_instance_t *vlc;
    libvlc_media_player_t *mp, *pip;
    ChannelList ch;
    int cur, last, pip_idx;
    gboolean sound_on_pip, live_paused;
    Guide guide;
    gint64 guide_loaded_at;   /* secondes (temps réel), 0 = jamais */
} App;
extern App app;

void app_play(int idx);
void app_play_url(const char *url);
void app_flash(const char *msg);
void app_pause_live(gboolean pause);             /* coupe / reprend le direct (lecture d'un enregistrement, scan) */
void app_reload_playlist(void);
void app_refresh_guide(gboolean force);
void app_options_changed(gboolean playlist_changed);
void app_rec_changed(void);                      /* enregistrements commencés / finis */
void app_start_pip(int idx);
libvlc_media_t *app_new_media(const char *url, gboolean small);
void app_apply_deinterlace(libvlc_media_player_t *mp);
libvlc_media_player_t *app_sound_player(void);
const Prog *app_prog_at(int idx, gint64 t);
gint64 now_s(void);
char *hm(gint64 t);                              /* « HH:MM » (g_free) */
void app_show_keys(void);

/* ---- ui.c -------------------------------------------------------------- */
typedef void (*MenuFn)(gpointer data);
GtkWidget *menu_new(GtkWidget *anchor);          /* popover vertical de boutons */
void menu_add(GtkWidget *menu, const char *label, MenuFn fn, gpointer data, GDestroyNotify destroy);
void menu_add_separator(GtkWidget *menu);
void menu_popup(GtkWidget *menu);
void menu_popup_at(GtkWidget *menu, double x, double y);
gboolean menu_any_open(void);
void ui_message(GtkWindow *parent, const char *title, const char *msg);
/* Question oui / non ; fn(data) si l'utilisateur choisit ok_label. */
void ui_confirm(GtkWindow *parent, const char *title, const char *msg, const char *ok_label, MenuFn fn, gpointer data,
                GDestroyNotify destroy);
void ui_open_uri(GtkWindow *parent, const char *uri);
GtkWidget *ui_window(const char *title, int w, int h);   /* fenêtre secondaire (Échap ferme) */
char *fmt_size(gint64 bytes);

/* ---- tracks.c ---------------------------------------------------------- */
void tracks_apply_prefs(libvlc_media_player_t *mp);
char *tracks_cycle_audio(libvlc_media_player_t *mp);   /* nom de la piste choisie ou NULL (g_free) */
char *tracks_cycle_spu(libvlc_media_player_t *mp);
void tracks_menu(GtkWidget *anchor, libvlc_media_player_t *mp);

/* ---- guide_ui.c -------------------------------------------------------- */
void guide_window_show(void);
void guide_window_refresh(void);
void guide_window_set_status(const char *msg);

/* ---- rec.c ------------------------------------------------------------- */
void rec_menu(GtkWidget *anchor, int idx);
void rec_start(int idx, const char *title, gint64 end_at);
gboolean rec_is_recording(const char *url);
int rec_active_count(void);
void rec_stop_all(void);
void recordings_window_show(void);

/* ---- tuner_ui.c -------------------------------------------------------- */
void tuner_window_show(void);
void tuner_check_firmware(void);

/* ---- settings_ui.c ----------------------------------------------------- */
void settings_window_show(void);

/* ---- update.c ---------------------------------------------------------- */
void update_init(GtkWidget *banner_box);         /* bandeau (GtkBox horizontale, cachée) */
void update_check(void (*manual_cb)(const char *msg, gpointer), gpointer ud);

#endif
