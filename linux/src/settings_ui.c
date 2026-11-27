/* settings_ui.c — fenêtre Paramètres (enregistrés à la fermeture).   (↔ SettingsActivity.kt) */
#include <stdlib.h>
#include <string.h>

#include "app.h"

static struct {
    GtkWidget *win, *playlist, *resume, *cache, *hw, *deint, *aspect, *guide, *margin, *recdir, *lang, *upd, *upd_status;
} S;

static GtkWidget *dropdown(int arr, const char *cur, int defidx)
{
    GtkWidget *d = gtk_drop_down_new_from_strings((const char *const *)TA(arr));
    const char *const *v = TV(arr);
    guint sel = (guint)defidx;
    for (guint i = 0; v[i]; i++)
        if (!strcmp(v[i], cur)) sel = i;
    gtk_drop_down_set_selected(GTK_DROP_DOWN(d), sel);
    return d;
}

static const char *dd_value(GtkWidget *d, int arr)
{
    guint i = gtk_drop_down_get_selected(GTK_DROP_DOWN(d));
    const char *const *v = TV(arr);
    for (guint k = 0; v[k]; k++)
        if (k == i) return v[k];
    return v[0];
}

static int row_n;

static void add_row(GtkWidget *grid, const char *label, GtkWidget *w, const char *help)
{
    GtkWidget *l = gtk_label_new(label);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    gtk_label_set_wrap(GTK_LABEL(l), TRUE);
    gtk_widget_set_hexpand(w, TRUE);
    if (GTK_IS_SWITCH(w)) { gtk_widget_set_halign(w, GTK_ALIGN_START); gtk_widget_set_hexpand(w, FALSE); }
    gtk_grid_attach(GTK_GRID(grid), l, 0, row_n, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), w, 1, row_n, 1, 1);
    row_n++;
    if (help) {
        GtkWidget *h = gtk_label_new(help);
        gtk_label_set_xalign(GTK_LABEL(h), 0);
        gtk_label_set_wrap(GTK_LABEL(h), TRUE);
        gtk_widget_add_css_class(h, "dim-label");
        gtk_grid_attach(GTK_GRID(grid), h, 1, row_n++, 1, 1);
    }
}

static void add_title(GtkWidget *grid, const char *t)
{
    GtkWidget *l = gtk_label_new(t);
    gtk_widget_add_css_class(l, "heading");
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    gtk_widget_set_margin_top(l, row_n ? 14 : 0);
    gtk_grid_attach(GTK_GRID(grid), l, 0, row_n++, 2, 1);
}

static GtkWidget *sw_new(gboolean on)
{
    GtkWidget *s = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(s), on);
    return s;
}

static void on_folder_chosen(GObject *src, GAsyncResult *res, gpointer ud)
{
    (void)ud;
    GFile *f = gtk_file_dialog_select_folder_finish(GTK_FILE_DIALOG(src), res, NULL);
    if (f && S.recdir) {
        char *p = g_file_get_path(f);
        if (p) gtk_editable_set_text(GTK_EDITABLE(S.recdir), p);
        g_free(p);
    }
    if (f) g_object_unref(f);
}

static void on_choose(GtkButton *b, gpointer ud)
{
    (void)b; (void)ud;
    GtkFileDialog *d = gtk_file_dialog_new();
    gtk_file_dialog_set_title(d, T(S_set_rec_dir));
    char *cur = cfg_rec_dir();
    GFile *f = g_file_new_for_path(cur);
    gtk_file_dialog_set_initial_folder(d, f);
    g_object_unref(f);
    g_free(cur);
    gtk_file_dialog_select_folder(d, GTK_WINDOW(S.win), NULL, on_folder_chosen, NULL);
    g_object_unref(d);
}

static void on_tuner(GtkButton *b, gpointer ud)
{
    (void)b; (void)ud;
    tuner_window_show();
}

static void on_upd_result(const char *msg, gpointer ud)
{
    (void)ud;
    if (S.upd_status) gtk_label_set_text(GTK_LABEL(S.upd_status), msg);
}

static void on_check_now(GtkButton *b, gpointer ud)
{
    (void)b; (void)ud;
    gtk_label_set_text(GTK_LABEL(S.upd_status), T(S_update_checking));
    update_check(on_upd_result, NULL);
}

static void on_keys(GtkButton *b, gpointer ud)
{
    (void)b; (void)ud;
    app_show_keys();
}

static gboolean on_close(GtkWindow *w, gpointer ud)
{
    (void)w; (void)ud;
    char *url = g_strstrip(g_strdup(gtk_editable_get_text(GTK_EDITABLE(S.playlist))));
    if (!*url) cfg_set(&url, DEFAULT_PLAYLIST);
    gboolean pl_changed = strcmp(url, cfg.playlist_url) != 0;
    const char *lang = dd_value(S.lang, A_lang);
    gboolean lang_changed = strcmp(lang, cfg.lang) != 0;
    int caching = atoi(dd_value(S.cache, A_cache));
    gboolean hw = gtk_switch_get_active(GTK_SWITCH(S.hw)), guide = gtk_switch_get_active(GTK_SWITCH(S.guide));
    const char *deint = dd_value(S.deint, A_deint), *aspect = dd_value(S.aspect, A_aspect);
    gboolean play_changed = caching != cfg.caching || hw != cfg.hw || strcmp(deint, cfg.deint) || strcmp(aspect, cfg.aspect) ||
                            guide != cfg.show_guide;
    cfg_set(&cfg.playlist_url, url);
    g_free(url);
    cfg.resume = gtk_switch_get_active(GTK_SWITCH(S.resume));
    cfg.caching = caching;
    cfg.hw = hw;
    cfg_set(&cfg.deint, deint);
    cfg_set(&cfg.aspect, aspect);
    cfg.show_guide = guide;
    cfg.rec_margin = atoi(dd_value(S.margin, A_margin));
    char *dir = g_strstrip(g_strdup(gtk_editable_get_text(GTK_EDITABLE(S.recdir))));
    cfg_set(&cfg.rec_dir, dir);
    g_free(dir);
    cfg_set(&cfg.lang, lang);
    cfg.check_updates = gtk_switch_get_active(GTK_SWITCH(S.upd));
    cfg_save();
    if (lang_changed) ui_message(app.win, T(S_set_lang), T(S_set_lang_restart));
    if (pl_changed || play_changed) app_options_changed(pl_changed);
    memset(&S, 0, sizeof S);
    return FALSE;
}

void settings_window_show(void)
{
    if (S.win) { gtk_window_present(GTK_WINDOW(S.win)); return; }
    S.win = ui_window(T(S_settings), 640, 640);
    g_signal_connect(S.win, "close-request", G_CALLBACK(on_close), NULL);
    GtkWidget *sw = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_window_set_child(GTK_WINDOW(S.win), sw);
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 14);
    gtk_widget_set_margin_start(grid, 18);
    gtk_widget_set_margin_end(grid, 18);
    gtk_widget_set_margin_top(grid, 16);
    gtk_widget_set_margin_bottom(grid, 16);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), grid);
    row_n = 0;

    add_title(grid, T(S_set_cat_play));
    S.playlist = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(S.playlist), cfg.playlist_url);
    add_row(grid, T(S_set_playlist), S.playlist, T(S_set_playlist_help));
    S.resume = sw_new(cfg.resume);
    add_row(grid, T(S_set_resume), S.resume, NULL);
    char cache[16];
    g_snprintf(cache, sizeof cache, "%d", cfg.caching);
    S.cache = dropdown(A_cache, cache, 1);
    add_row(grid, T(S_set_cache), S.cache, NULL);
    S.hw = sw_new(cfg.hw);
    add_row(grid, T(S_set_hw), S.hw, T(S_set_hw_help));
    S.deint = dropdown(A_deint, cfg.deint, 1);
    add_row(grid, T(S_set_deint), S.deint, NULL);
    S.aspect = dropdown(A_aspect, cfg.aspect, 0);
    add_row(grid, T(S_set_aspect), S.aspect, NULL);

    add_title(grid, T(S_set_cat_guide));
    S.guide = sw_new(cfg.show_guide);
    add_row(grid, T(S_set_guide), S.guide, T(S_set_guide_help));
    char margin[16];
    g_snprintf(margin, sizeof margin, "%d", cfg.rec_margin);
    S.margin = dropdown(A_margin, margin, 2);
    add_row(grid, T(S_set_margin), S.margin, NULL);
    GtkWidget *dr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    S.recdir = gtk_entry_new();
    char *dir = cfg_rec_dir();
    gtk_editable_set_text(GTK_EDITABLE(S.recdir), dir);
    g_free(dir);
    gtk_widget_set_hexpand(S.recdir, TRUE);
    gtk_box_append(GTK_BOX(dr), S.recdir);
    GtkWidget *ch = gtk_button_new_with_label(T(S_choose));
    g_signal_connect(ch, "clicked", G_CALLBACK(on_choose), NULL);
    gtk_box_append(GTK_BOX(dr), ch);
    add_row(grid, T(S_set_rec_dir), dr, NULL);

    add_title(grid, T(S_set_cat_ui));
    S.lang = dropdown(A_lang, cfg.lang, 0);
    add_row(grid, T(S_set_lang), S.lang, NULL);
    GtkWidget *kb = gtk_button_new_with_label(T(S_keys_title));
    gtk_widget_set_halign(kb, GTK_ALIGN_START);
    g_signal_connect(kb, "clicked", G_CALLBACK(on_keys), NULL);
    gtk_grid_attach(GTK_GRID(grid), kb, 1, row_n++, 1, 1);

    add_title(grid, T(S_set_cat_tuner));
    GtkWidget *tb = gtk_button_new_with_label(T(S_tuner_title));
    gtk_widget_set_halign(tb, GTK_ALIGN_START);
    g_signal_connect(tb, "clicked", G_CALLBACK(on_tuner), NULL);
    add_row(grid, T(S_set_tuner_help), tb, NULL);
    S.upd = sw_new(cfg.check_updates);
    add_row(grid, T(S_set_updchk), S.upd, NULL);
    GtkWidget *ur = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *ub = gtk_button_new_with_label(T(S_set_updnow));
    g_signal_connect(ub, "clicked", G_CALLBACK(on_check_now), NULL);
    gtk_box_append(GTK_BOX(ur), ub);
    char *v = TF(S_version_current, APP_VERSION);
    S.upd_status = gtk_label_new(v);
    g_free(v);
    gtk_label_set_wrap(GTK_LABEL(S.upd_status), TRUE);
    gtk_label_set_xalign(GTK_LABEL(S.upd_status), 0);
    gtk_box_append(GTK_BOX(ur), S.upd_status);
    gtk_grid_attach(GTK_GRID(grid), ur, 1, row_n++, 1, 1);

    gtk_window_present(GTK_WINDOW(S.win));
}
