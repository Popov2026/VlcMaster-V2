/*
 * update.c — nouvelle version de HomeRun Player : compare APP_VERSION à la dernière
 * release GitHub (api.github.com/repos/…/releases/latest).  Le bandeau n'apparaît que si
 * la release est plus récente ET contient un paquet Linux (.deb, .AppImage ou
 * *linux*.tar.gz) : une release Windows ou Android seule ne concerne pas Linux.
 * Au démarrage puis toutes les 12 h ; « ✕ » = ne plus signaler cette version.
 *                                                        (↔ src/update.h, Updater.kt)
 */
#include <string.h>

#include "app.h"

static GtkWidget *g_banner, *g_label;
static char *g_tag, *g_asset, *g_page;
static guint g_timer;

typedef struct {
    char *text, *err;
    void (*cb)(const char *, gpointer);
    gpointer ud;
} UpdJob;

static void upd_work(gpointer p)
{
    UpdJob *j = p;
    j->text = net_get("https://api.github.com/repos/" UPD_REPO "/releases/latest", 10000, &j->err);
}

static gboolean linux_asset(const char *name)
{
    char *n = g_ascii_strdown(name, -1);
    gboolean ok = g_str_has_suffix(n, ".deb") || g_str_has_suffix(n, ".appimage") ||
                  (strstr(n, "linux") && (g_str_has_suffix(n, ".tar.gz") || g_str_has_suffix(n, ".tgz")));
    g_free(n);
    return ok;
}

static gboolean has_dpkg(void)
{
    char *p = g_find_program_in_path("dpkg");
    gboolean r = p != NULL;
    g_free(p);
    return r;
}

static void upd_done(gpointer p)
{
    UpdJob *j = p;
    JVal *v = j->text ? json_parse(j->text) : NULL;
    const char *tag = json_str(v, "tag_name");
    if (tag) {
        const char *asset = NULL;
        const JVal *as = json_get(v, "assets");
        for (int i = 0; as && i < as->cnt; i++) {
            const char *n = json_str(json_at(as, i), "name"), *u = json_str(json_at(as, i), "browser_download_url");
            if (n && u && linux_asset(n)) {
                asset = u;
                if (g_str_has_suffix(n, ".deb") && has_dpkg()) break;   /* préféré sur Debian/Ubuntu */
            }
        }
        gboolean newer = version_cmp(tag, APP_VERSION) > 0 && asset;
        g_free(g_tag);
        g_free(g_asset);
        g_free(g_page);
        g_tag = g_strdup(tag);
        g_asset = g_strdup(asset);
        const char *page = json_str(v, "html_url");
        g_page = g_strdup(page ? page : "https://github.com/" UPD_REPO "/releases/latest");
        const char *ver = tag[0] == 'v' || tag[0] == 'V' ? tag + 1 : tag;
        if (newer && (j->cb || strcmp(cfg.skipped_version, tag))) {
            char *s = TF(S_update_banner_desk, ver);
            gtk_label_set_text(GTK_LABEL(g_label), s);
            g_free(s);
            gtk_widget_set_visible(g_banner, TRUE);
        }
        if (j->cb) {
            char *s = newer ? TF(S_update_banner_desk, ver) : TF(S_update_none, APP_VERSION, ver);
            j->cb(s, j->ud);
            g_free(s);
        }
    } else if (j->cb) {
        char *s = TF(S_update_failed, j->err ? j->err : "?");
        j->cb(s, j->ud);
        g_free(s);
    }
    json_free(v);
    g_free(j->text);
    g_free(j->err);
    g_free(j);
}

void update_check(void (*manual_cb)(const char *msg, gpointer), gpointer ud)
{
    UpdJob *j = g_new0(UpdJob, 1);
    j->cb = manual_cb;
    j->ud = ud;
    bg_run(upd_work, upd_done, j);
}

static gboolean periodic(gpointer p)
{
    (void)p;
    if (cfg.check_updates) update_check(NULL, NULL);
    return G_SOURCE_CONTINUE;
}

static void on_click(GtkButton *b, gpointer ud)
{
    (void)b;
    switch (GPOINTER_TO_INT(ud)) {
    case 0: if (g_asset) ui_open_uri(app.win, g_asset); break;
    case 1: if (g_page) ui_open_uri(app.win, g_page); break;
    case 2:
        if (g_tag) { cfg_set(&cfg.skipped_version, g_tag); cfg_save(); }
        gtk_widget_set_visible(g_banner, FALSE);
        break;
    }
}

static void button(const char *label, int id, const char *tip)
{
    GtkWidget *b = gtk_button_new_with_label(label);
    gtk_widget_add_css_class(b, "flat");
    gtk_widget_set_focus_on_click(b, FALSE);
    if (tip) gtk_widget_set_tooltip_text(b, tip);
    g_signal_connect(b, "clicked", G_CALLBACK(on_click), GINT_TO_POINTER(id));
    gtk_box_append(GTK_BOX(g_banner), b);
}

void update_init(GtkWidget *banner_box)
{
    g_banner = banner_box;
    g_label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(g_label), 0);
    gtk_widget_set_hexpand(g_label, TRUE);
    gtk_box_append(GTK_BOX(g_banner), g_label);
    button(T(S_update_download), 0, NULL);
    button(T(S_update_notes), 1, NULL);
    button("✕", 2, T(S_update_ignore));
    if (!g_timer) g_timer = g_timeout_add_seconds(12 * 3600, periodic, NULL);
}
