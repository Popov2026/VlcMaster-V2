/* config.c — réglages, dans ~/.config/homerun-player/settings.ini (GKeyFile). */
#include <string.h>

#include "app.h"

Config cfg;

#define GROUP "main"

char *cfg_path(const char *name)
{
    return g_build_filename(g_get_user_config_dir(), "homerun-player", name, NULL);
}

void cfg_set(char **field, const char *value)
{
    char *n = g_strdup(value ? value : "");
    g_free(*field);
    *field = n;
}

static char *get_s(GKeyFile *k, const char *key, const char *def)
{
    char *v = g_key_file_get_string(k, GROUP, key, NULL);
    return v ? v : g_strdup(def);
}

static int get_i(GKeyFile *k, const char *key, int def)
{
    GError *e = NULL;
    int v = g_key_file_get_integer(k, GROUP, key, &e);
    if (e) { g_error_free(e); return def; }
    return v;
}

static gboolean get_b(GKeyFile *k, const char *key, gboolean def)
{
    GError *e = NULL;
    gboolean v = g_key_file_get_boolean(k, GROUP, key, &e);
    if (e) { g_error_free(e); return def; }
    return v;
}

void cfg_load(void)
{
    GKeyFile *k = g_key_file_new();
    char *path = cfg_path("settings.ini");
    g_key_file_load_from_file(k, path, G_KEY_FILE_NONE, NULL);
    g_free(path);
    cfg.playlist_url = get_s(k, "playlist_url", DEFAULT_PLAYLIST);
    if (!*g_strstrip(cfg.playlist_url)) cfg_set(&cfg.playlist_url, DEFAULT_PLAYLIST);
    cfg.resume = get_b(k, "resume", TRUE);
    cfg.caching = get_i(k, "caching", 1000);
    cfg.hw = get_b(k, "hw_decoding", TRUE);
    cfg.deint = get_s(k, "deinterlace", "yadif");
    cfg.aspect = get_s(k, "aspect", "");
    cfg.show_guide = get_b(k, "show_guide", TRUE);
    cfg.rec_margin = get_i(k, "rec_margin", 10);
    cfg.rec_dir = get_s(k, "rec_dir", "");
    cfg.lang = get_s(k, "lang", "");
    cfg.check_updates = get_b(k, "check_updates", TRUE);
    cfg.panel = get_b(k, "panel", TRUE);
    cfg.last_channel = get_s(k, "last_channel", "");
    cfg.volume = CLAMP(get_i(k, "volume", 100), 0, 150);
    cfg.audio_pref = get_s(k, "audio_pref", "");
    cfg.spu_pref = get_s(k, "spu_pref", "");
    cfg.skipped_version = get_s(k, "skipped_version", "");
    cfg.pip_corner = CLAMP(get_i(k, "pip_corner", 3), 0, 3);
    cfg.win_w = get_i(k, "win_w", 1200);
    cfg.win_h = get_i(k, "win_h", 720);
    g_key_file_free(k);
}

void cfg_save(void)
{
    GKeyFile *k = g_key_file_new();
    g_key_file_set_string(k, GROUP, "playlist_url", cfg.playlist_url);
    g_key_file_set_boolean(k, GROUP, "resume", cfg.resume);
    g_key_file_set_integer(k, GROUP, "caching", cfg.caching);
    g_key_file_set_boolean(k, GROUP, "hw_decoding", cfg.hw);
    g_key_file_set_string(k, GROUP, "deinterlace", cfg.deint);
    g_key_file_set_string(k, GROUP, "aspect", cfg.aspect);
    g_key_file_set_boolean(k, GROUP, "show_guide", cfg.show_guide);
    g_key_file_set_integer(k, GROUP, "rec_margin", cfg.rec_margin);
    g_key_file_set_string(k, GROUP, "rec_dir", cfg.rec_dir);
    g_key_file_set_string(k, GROUP, "lang", cfg.lang);
    g_key_file_set_boolean(k, GROUP, "check_updates", cfg.check_updates);
    g_key_file_set_boolean(k, GROUP, "panel", cfg.panel);
    g_key_file_set_string(k, GROUP, "last_channel", cfg.last_channel);
    g_key_file_set_integer(k, GROUP, "volume", cfg.volume);
    g_key_file_set_string(k, GROUP, "audio_pref", cfg.audio_pref);
    g_key_file_set_string(k, GROUP, "spu_pref", cfg.spu_pref);
    g_key_file_set_string(k, GROUP, "skipped_version", cfg.skipped_version);
    g_key_file_set_integer(k, GROUP, "pip_corner", cfg.pip_corner);
    g_key_file_set_integer(k, GROUP, "win_w", cfg.win_w);
    g_key_file_set_integer(k, GROUP, "win_h", cfg.win_h);
    char *path = cfg_path("settings.ini");
    char *dir = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0700);
    GError *e = NULL;
    if (!g_key_file_save_to_file(k, path, &e)) {
        g_warning("settings.ini : %s", e->message);
        g_error_free(e);
    }
    g_free(dir);
    g_free(path);
    g_key_file_free(k);
}

char *cfg_tuner_base(void)
{
    char b[512];
    return tuner_base(cfg.playlist_url, b, sizeof b) ? g_strdup(b) : NULL;
}

char *cfg_rec_dir(void)
{
    if (cfg.rec_dir && *cfg.rec_dir) return g_strdup(cfg.rec_dir);
    const char *v = g_get_user_special_dir(G_USER_DIRECTORY_VIDEOS);
    return g_build_filename(v ? v : g_get_home_dir(), "HomeRun", NULL);
}
