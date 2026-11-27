/*
 * tuner_ui.c — tuner HDHomeRun : modèle, firmware, mise à jour disponible
 * (discover.json « UpgradeAvailable » : elle s'installe depuis la page web du tuner),
 * scan des chaînes (POST lineup.post?scan=start&source=…, suivi par lineup_status.json,
 * arrêt par scan=abort).  Le scan occupe tous les tuners : lecture, PIP et
 * enregistrements sont arrêtés, puis la playlist est relue.        (↔ src/tuner.h, TunerActivity.kt)
 */
#include <string.h>

#include "app.h"

static struct {
    GtkWidget *win, *info, *scan_status, *progress, *btn_scan, *btn_abort, *btn_web;
    char *base, *source;
    guint poll_id;
    gboolean seen, polling;
} U;

static gboolean g_fw_told;

/* ---- infos -------------------------------------------------------------- */
typedef struct { char *base, *disc, *status, *err; gboolean firmware_only; } InfoJob;

static void info_work(gpointer p)
{
    InfoJob *j = p;
    char *u = g_strdup_printf("%s/discover.json", j->base);
    j->disc = net_get(u, 8000, &j->err);
    g_free(u);
    if (j->disc && !j->firmware_only) {
        u = g_strdup_printf("%s/lineup_status.json", j->base);
        j->status = net_get(u, 8000, NULL);
        g_free(u);
    }
}

static void start_polling(void);

static void line(GString *s, int label, const char *v)
{
    if (v && *v) g_string_append_printf(s, "%s : %s\n", T(label), v);
}

static void info_done(gpointer p)
{
    InfoJob *j = p;
    JVal *d = j->disc ? json_parse(j->disc) : NULL;
    if (j->firmware_only) {
        const char *up = json_str(d, "UpgradeAvailable");
        if (up && *up && !g_fw_told) {
            g_fw_told = TRUE;
            char *s = TF(S_firmware_available, up);
            app_flash(s);
            g_free(s);
        }
    } else if (U.win && !g_strcmp0(U.base, j->base)) {
        if (d && json_str(d, "ModelNumber")) {
            GString *s = g_string_new(NULL);
            line(s, S_tuner_name, json_str(d, "FriendlyName"));
            line(s, S_tuner_model, json_str(d, "ModelNumber"));
            line(s, S_tuner_id, json_str(d, "DeviceID"));
            int n = (int)json_num(d, "TunerCount", 0);
            if (n > 0) {
                char b[16];
                g_snprintf(b, sizeof b, "%d", n);
                line(s, S_tuner_count, b);
            }
            char *fw = g_strstrip(g_strdup_printf("%s %s", json_str(d, "FirmwareName") ? json_str(d, "FirmwareName") : "",
                                                  json_str(d, "FirmwareVersion") ? json_str(d, "FirmwareVersion") : ""));
            line(s, S_tuner_fw, fw);
            g_free(fw);
            g_string_append_c(s, '\n');
            const char *up = json_str(d, "UpgradeAvailable");
            if (up && *up) {
                char *t = TF(S_tuner_upgrade, up);
                g_string_append(s, t);
                g_free(t);
            } else g_string_append(s, T(S_tuner_up_to_date));
            gtk_label_set_text(GTK_LABEL(U.info), s->str);
            g_string_free(s, TRUE);
            gtk_widget_set_visible(U.btn_web, TRUE);
            gtk_widget_set_sensitive(U.btn_scan, TRUE);
            JVal *st = j->status ? json_parse(j->status) : NULL;
            const char *src = json_str(st, "Source");
            if (src && *src) { g_free(U.source); U.source = g_strdup(src); }
            if (json_num(st, "ScanInProgress", 0) == 1) start_polling();
            json_free(st);
        } else {
            char *s = TF(S_tuner_unreachable, j->base, j->err ? j->err : "discover.json");
            gtk_label_set_text(GTK_LABEL(U.info), s);
            g_free(s);
        }
    }
    json_free(d);
    g_free(j->base);
    g_free(j->disc);
    g_free(j->status);
    g_free(j->err);
    g_free(j);
}

static void load_info(void)
{
    char *s = TF(S_tuner_loading, U.base);
    gtk_label_set_text(GTK_LABEL(U.info), s);
    g_free(s);
    InfoJob *j = g_new0(InfoJob, 1);
    j->base = g_strdup(U.base);
    bg_run(info_work, info_done, j);
}

void tuner_check_firmware(void)
{
    if (g_fw_told) return;
    char *base = cfg_tuner_base();
    if (!base) return;
    InfoJob *j = g_new0(InfoJob, 1);
    j->base = base;
    j->firmware_only = TRUE;
    bg_run(info_work, info_done, j);
}

/* ---- scan --------------------------------------------------------------- */
typedef struct { char *url, *text, *err; gboolean post, ok; int kind; } Req;

enum { R_START, R_ABORT, R_POLL };

static void req_work(gpointer p)
{
    Req *r = p;
    if (r->post) r->ok = net_post(r->url, 10000, &r->err);
    else r->ok = (r->text = net_get(r->url, 5000, &r->err)) != NULL;
}

static void stop_poll_ui(void)
{
    U.polling = FALSE;
    if (!U.win) return;
    gtk_widget_set_sensitive(U.btn_scan, TRUE);
    gtk_widget_set_visible(U.btn_abort, FALSE);
    gtk_widget_set_visible(U.progress, FALSE);
}

static gboolean poll_tick(gpointer p);

static void req_done(gpointer p)
{
    Req *r = p;
    if (r->kind == R_START) {
        if (r->ok) start_polling();
        else {
            app_pause_live(FALSE);
        }
        if (!r->ok && U.win) {
            char *s = TF(S_scan_failed, r->err ? r->err : "?");
            gtk_label_set_text(GTK_LABEL(U.scan_status), s);
            g_free(s);
            stop_poll_ui();
        }
    } else if (r->kind == R_POLL && U.polling) {
        JVal *s = r->text ? json_parse(r->text) : NULL;
        if (s) {
            if (json_num(s, "ScanInProgress", 0) == 1) {
                U.seen = TRUE;
                int pr = (int)json_num(s, "Progress", 0), found = (int)json_num(s, "Found", 0);
                if (U.win) {
                    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(U.progress), CLAMP(pr / 100.0, 0, 1));
                    char *t = TF(S_scan_progress, pr, found);
                    gtk_label_set_text(GTK_LABEL(U.scan_status), t);
                    g_free(t);
                }
            } else {
                if (U.seen) {
                    if (U.win) gtk_label_set_text(GTK_LABEL(U.scan_status), T(S_scan_done));
                    app_reload_playlist();   /* nouvelle liste des chaînes */
                }
                U.seen = FALSE;
                stop_poll_ui();
            }
            json_free(s);
        }
        if (U.polling) U.poll_id = g_timeout_add(1000, poll_tick, NULL);
    }
    g_free(r->url);
    g_free(r->text);
    g_free(r->err);
    g_free(r);
}

static void request(int kind, const char *path, gboolean post)
{
    Req *r = g_new0(Req, 1);
    r->kind = kind;
    r->post = post;
    r->url = g_strdup_printf("%s%s", U.base, path);
    bg_run(req_work, req_done, r);
}

static gboolean poll_tick(gpointer p)
{
    (void)p;
    U.poll_id = 0;
    if (U.polling) request(R_POLL, "/lineup_status.json", FALSE);
    return G_SOURCE_REMOVE;
}

static void start_polling(void)
{
    if (U.polling) return;
    U.polling = TRUE;
    if (U.win) {
        gtk_widget_set_sensitive(U.btn_scan, FALSE);
        gtk_widget_set_visible(U.btn_abort, TRUE);
        gtk_widget_set_visible(U.progress, TRUE);
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(U.progress), 0);
    }
    poll_tick(NULL);
}

static void do_scan(gpointer p)
{
    (void)p;
    /* le scan occupe tous les tuners */
    rec_stop_all();
    app_pause_live(TRUE);
    char *src = g_uri_escape_string(U.source ? U.source : "Antenna", NULL, FALSE);
    char *path = g_strdup_printf("/lineup.post?scan=start&source=%s", src);
    request(R_START, path, TRUE);
    g_free(path);
    g_free(src);
    if (U.win) gtk_label_set_text(GTK_LABEL(U.scan_status), T(S_rec_starting));
}

static void on_scan(GtkButton *b, gpointer ud)
{
    (void)b; (void)ud;
    ui_confirm(GTK_WINDOW(U.win), T(S_scan_title), T(S_scan_confirm), T(S_scan_start), do_scan, NULL, NULL);
}

static void on_abort(GtkButton *b, gpointer ud)
{
    (void)b; (void)ud;
    request(R_ABORT, "/lineup.post?scan=abort", TRUE);
}

static void on_web(GtkButton *b, gpointer ud)
{
    (void)b; (void)ud;
    char *u = g_strdup_printf("%s/", U.base);
    ui_open_uri(GTK_WINDOW(U.win), u);
    g_free(u);
}

static void on_refresh(GtkButton *b, gpointer ud)
{
    (void)b; (void)ud;
    load_info();
}

static void on_destroy(GtkWidget *w, gpointer ud)
{
    (void)w; (void)ud;
    if (U.poll_id) g_source_remove(U.poll_id);
    U.poll_id = 0;
    U.polling = FALSE;
    U.win = NULL;
    /* un scan en cours continue sur le tuner ; la liste sera relue à la prochaine ouverture */
}

void tuner_window_show(void)
{
    if (U.win) { gtk_window_present(GTK_WINDOW(U.win)); return; }
    g_free(U.base);
    U.base = cfg_tuner_base();
    if (!U.base) U.base = g_strdup("http://192.168.1.90");
    U.seen = FALSE;
    U.win = ui_window(T(S_tuner_title), 560, -1);
    g_signal_connect(U.win, "destroy", G_CALLBACK(on_destroy), NULL);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(box, 18);
    gtk_widget_set_margin_end(box, 18);
    gtk_widget_set_margin_top(box, 16);
    gtk_widget_set_margin_bottom(box, 16);
    gtk_window_set_child(GTK_WINDOW(U.win), box);

    U.info = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(U.info), 0);
    gtk_label_set_wrap(GTK_LABEL(U.info), TRUE);
    gtk_label_set_selectable(GTK_LABEL(U.info), TRUE);
    gtk_box_append(GTK_BOX(box), U.info);
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *rb = gtk_button_new_with_label(T(S_tuner_refresh));
    g_signal_connect(rb, "clicked", G_CALLBACK(on_refresh), NULL);
    gtk_box_append(GTK_BOX(row), rb);
    U.btn_web = gtk_button_new_with_label(T(S_tuner_web));
    g_signal_connect(U.btn_web, "clicked", G_CALLBACK(on_web), NULL);
    gtk_widget_set_visible(U.btn_web, FALSE);
    gtk_box_append(GTK_BOX(row), U.btn_web);
    gtk_box_append(GTK_BOX(box), row);

    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    GtkWidget *st = gtk_label_new(T(S_scan_title));
    gtk_widget_add_css_class(st, "heading");
    gtk_label_set_xalign(GTK_LABEL(st), 0);
    gtk_box_append(GTK_BOX(box), st);
    GtkWidget *help = gtk_label_new(T(S_scan_help));
    gtk_label_set_wrap(GTK_LABEL(help), TRUE);
    gtk_label_set_xalign(GTK_LABEL(help), 0);
    gtk_widget_add_css_class(help, "dim-label");
    gtk_box_append(GTK_BOX(box), help);
    U.progress = gtk_progress_bar_new();
    gtk_widget_set_visible(U.progress, FALSE);
    gtk_box_append(GTK_BOX(box), U.progress);
    U.scan_status = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(U.scan_status), 0);
    gtk_label_set_wrap(GTK_LABEL(U.scan_status), TRUE);
    gtk_box_append(GTK_BOX(box), U.scan_status);
    GtkWidget *row2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    U.btn_scan = gtk_button_new_with_label(T(S_scan_start));
    gtk_widget_set_sensitive(U.btn_scan, FALSE);   /* tant que le tuner n'a pas répondu */
    g_signal_connect(U.btn_scan, "clicked", G_CALLBACK(on_scan), NULL);
    gtk_box_append(GTK_BOX(row2), U.btn_scan);
    U.btn_abort = gtk_button_new_with_label(T(S_scan_abort));
    gtk_widget_add_css_class(U.btn_abort, "destructive-action");
    gtk_widget_set_visible(U.btn_abort, FALSE);
    g_signal_connect(U.btn_abort, "clicked", G_CALLBACK(on_abort), NULL);
    gtk_box_append(GTK_BOX(row2), U.btn_abort);
    gtk_box_append(GTK_BOX(box), row2);

    gtk_window_present(GTK_WINDOW(U.win));
    load_info();
}
