/*
 * guide_ui.c — guide TV : grille horaire (chaînes de la playlist × heures), dessinée
 * avec cairo.  Souris ou flèches ; une case → détails, Regarder, Enregistrer.
 * La colonne des chaînes et la règle des heures restent visibles en défilant.
 *                                             (↔ src/guide_ui.h, GuideActivity.kt)
 */
#include <string.h>

#include "app.h"

#define NAME_W 180
#define ROW_H  50
#define HDR_H  28
#define PX_MIN 4.0     /* pixels par minute */

static struct {
    GtkWidget *win, *area, *sw, *status;
    gint64 t0, t1;
    int sel_row, sel_prog;
    char *status_msg;
    gboolean scrolled;
} G = { .sel_row = -1, .sel_prog = -1 };

static const GuideCh *row_ch(int r)
{
    return r >= 0 && r < app.ch.n ? guide_find(&app.guide, app.ch.v[r].num) : NULL;
}

static double tx(gint64 t) { return NAME_W + (double)(t - G.t0) / 60.0 * PX_MIN; }

static void compute_range(void)
{
    gint64 now = now_s();
    G.t0 = (now - 1800) / 1800 * 1800;
    gint64 end = MAX(app.guide.max_end, now + 3 * 3600);
    G.t1 = MIN(end, now + 26 * 3600);
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(G.area), (int)tx(G.t1) + 20);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(G.area), HDR_H + MAX(app.ch.n, 1) * ROW_H);
}

static void set_rgb(cairo_t *cr, guint32 c)
{
    cairo_set_source_rgb(cr, ((c >> 16) & 255) / 255.0, ((c >> 8) & 255) / 255.0, (c & 255) / 255.0);
}

static void text(cairo_t *cr, PangoLayout *l, const char *s, double x, double y, double w, guint32 color)
{
    if (w < 8) return;
    pango_layout_set_text(l, s, -1);
    pango_layout_set_width(l, (int)(w * PANGO_SCALE));
    set_rgb(cr, color);
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, l);
}

static void draw(GtkDrawingArea *a, cairo_t *cr, int w, int h, gpointer ud)
{
    (void)a; (void)w; (void)ud;
    GtkAdjustment *ha = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(G.sw));
    GtkAdjustment *va = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(G.sw));
    double sx = gtk_adjustment_get_value(ha), sy = gtk_adjustment_get_value(va);
    double pw = gtk_adjustment_get_page_size(ha), ph = gtk_adjustment_get_page_size(va);
    if (pw <= 0) pw = w;
    if (ph <= 0) ph = h;
    gint64 now = now_s();

    set_rgb(cr, 0x1b1b1f);
    cairo_paint(cr);

    PangoLayout *l = pango_cairo_create_layout(cr);
    PangoFontDescription *fd = pango_font_description_from_string("Sans 9.5");
    PangoFontDescription *fb = pango_font_description_from_string("Sans Bold 10");
    pango_layout_set_ellipsize(l, PANGO_ELLIPSIZE_END);

    int r0 = MAX(0, (int)((sy) / ROW_H) - 1), r1 = MIN(app.ch.n, (int)((sy + ph) / ROW_H) + 1);
    gint64 tv0 = G.t0 + (gint64)((sx) / PX_MIN * 60) - 3600, tv1 = G.t0 + (gint64)((sx + pw) / PX_MIN * 60);
    double left = sx + NAME_W;

    /* programmes */
    for (int r = r0; r < r1; r++) {
        double y = HDR_H + r * ROW_H;
        const GuideCh *gc = row_ch(r);
        if (!gc || !gc->n) {
            set_rgb(cr, 0x26262b);
            cairo_rectangle(cr, left + 1, y + 1, pw - NAME_W - 2, ROW_H - 2);
            cairo_fill(cr);
            pango_layout_set_font_description(l, fd);
            text(cr, l, T(S_guide_no_data), left + 8, y + 16, pw - NAME_W - 16, 0x777777);
            continue;
        }
        for (int i = 0; i < gc->n; i++) {
            const Prog *p = &gc->p[i];
            if (p->end <= MAX(tv0, G.t0) || p->start >= tv1) continue;
            double x0 = MAX(tx(p->start), (double)NAME_W), x1 = tx(MIN(p->end, G.t1));
            if (x1 <= x0 + 1) continue;
            gboolean sel = r == G.sel_row && i == G.sel_prog, live = now >= p->start && now < p->end;
            set_rgb(cr, sel ? 0x1565c0 : live ? 0x37474f : 0x2b2b31);
            cairo_rectangle(cr, x0 + 1, y + 1, x1 - x0 - 2, ROW_H - 2);
            cairo_fill(cr);
            /* texte toujours lisible même si la case commence avant le bord gauche visible */
            double tx0 = MAX(x0, left) + 6, tw = x1 - tx0 - 6;
            pango_layout_set_font_description(l, fb);
            text(cr, l, p->title, tx0, y + 6, tw, 0xffffff);
            char *a = hm(p->start), *b = hm(p->end);
            char *s = g_strdup_printf("%s – %s%s%s", a, b, p->episode ? "  ·  " : "", p->episode ? p->episode : "");
            pango_layout_set_font_description(l, fd);
            text(cr, l, s, tx0, y + 27, tw, sel ? 0xe3f2fd : 0xb0b0b8);
            g_free(s); g_free(a); g_free(b);
        }
    }

    /* maintenant */
    double xn = tx(now);
    set_rgb(cr, 0xe53935);
    cairo_rectangle(cr, xn - 1, sy, 2, ph);
    cairo_fill(cr);

    /* règle des heures (fixe en haut) */
    set_rgb(cr, 0x111114);
    cairo_rectangle(cr, sx, sy, pw, HDR_H);
    cairo_fill(cr);
    pango_layout_set_font_description(l, fb);
    for (gint64 t = G.t0; t <= G.t1; t += 1800) {
        double x = tx(t);
        if (x < left - 60 || x > sx + pw) continue;
        set_rgb(cr, 0x444448);
        cairo_rectangle(cr, x, sy + HDR_H - 6, 1, 6);
        cairo_fill(cr);
        char *s = hm(t);
        if (x + 3 >= left) text(cr, l, s, x + 3, sy + 5, 60, 0xdddddd);
        g_free(s);
    }

    /* colonne des chaînes (fixe à gauche) */
    pango_layout_set_font_description(l, fb);
    for (int r = r0; r < r1; r++) {
        double y = HDR_H + r * ROW_H;
        if (y + ROW_H < sy + HDR_H) continue;
        set_rgb(cr, r == app.cur ? 0x263238 : r == G.sel_row ? 0x202a36 : 0x16161a);
        cairo_rectangle(cr, sx, y, NAME_W, ROW_H);
        cairo_fill(cr);
        set_rgb(cr, 0x2e2e34);
        cairo_rectangle(cr, sx, y + ROW_H - 1, NAME_W, 1);
        cairo_fill(cr);
        const Channel *c = &app.ch.v[r];
        char *s = *c->num ? g_strdup_printf("%s  %s", c->num, c->name) : g_strdup(c->name);
        text(cr, l, s, sx + 10, y + 16, NAME_W - 16, r == app.cur ? 0xffb300 : 0xffffff);
        g_free(s);
    }
    set_rgb(cr, 0x111114);
    cairo_rectangle(cr, sx, sy, NAME_W, HDR_H);
    cairo_fill(cr);

    pango_font_description_free(fd);
    pango_font_description_free(fb);
    g_object_unref(l);
}

/* ---- sélection --------------------------------------------------------- */
static void ensure_visible(void)
{
    GtkAdjustment *ha = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(G.sw));
    GtkAdjustment *va = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(G.sw));
    if (G.sel_row >= 0) {
        double y = HDR_H + G.sel_row * ROW_H, v = gtk_adjustment_get_value(va), ph = gtk_adjustment_get_page_size(va);
        if (y - HDR_H < v) gtk_adjustment_set_value(va, y - HDR_H);
        else if (y + ROW_H > v + ph) gtk_adjustment_set_value(va, y + ROW_H - ph);
    }
    const GuideCh *gc = row_ch(G.sel_row);
    if (gc && G.sel_prog >= 0 && G.sel_prog < gc->n) {
        double x0 = tx(gc->p[G.sel_prog].start), v = gtk_adjustment_get_value(ha), pw = gtk_adjustment_get_page_size(ha);
        if (x0 - NAME_W < v || x0 > v + pw - 80) gtk_adjustment_set_value(ha, MAX(0, x0 - NAME_W - 40));
    }
    gtk_widget_queue_draw(G.area);
}

static int prog_index_at(const GuideCh *gc, gint64 t)
{
    if (!gc) return -1;
    for (int i = 0; i < gc->n; i++)
        if (t < gc->p[i].end) return i;
    return gc->n - 1;
}

static void select_cell(int row, gint64 t)
{
    if (row < 0 || row >= app.ch.n) return;
    G.sel_row = row;
    G.sel_prog = prog_index_at(row_ch(row), t);
    ensure_visible();
}

static void details(void);

static void on_click(GtkGestureClick *g, int n, double x, double y, gpointer ud)
{
    (void)g; (void)n; (void)ud;
    GtkAdjustment *ha = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(G.sw));
    GtkAdjustment *va = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(G.sw));
    if (y < gtk_adjustment_get_value(va) + HDR_H) return;
    int row = (int)((y - HDR_H) / ROW_H);
    if (row < 0 || row >= app.ch.n) return;
    if (x < gtk_adjustment_get_value(ha) + NAME_W) {   /* nom de la chaîne : la regarder */
        app_play(row);
        gtk_widget_queue_draw(G.area);
        return;
    }
    gint64 t = G.t0 + (gint64)((x - NAME_W) / PX_MIN * 60);
    const GuideCh *gc = row_ch(row);
    int i = -1;
    for (int k = 0; gc && k < gc->n; k++)
        if (t >= gc->p[k].start && t < gc->p[k].end) { i = k; break; }
    G.sel_row = row;
    G.sel_prog = i;
    gtk_widget_queue_draw(G.area);
    if (i >= 0) details();
}

static gboolean on_key(GtkEventControllerKey *c, guint kv, guint code, GdkModifierType st, gpointer ud)
{
    (void)c; (void)code; (void)st; (void)ud;
    const GuideCh *gc = row_ch(G.sel_row);
    gint64 t = gc && G.sel_prog >= 0 && G.sel_prog < gc->n ? MAX(gc->p[G.sel_prog].start, now_s()) : now_s();
    switch (kv) {
    case GDK_KEY_Up: select_cell(MAX(G.sel_row - 1, 0), t); return TRUE;
    case GDK_KEY_Down: select_cell(MIN(G.sel_row + 1, app.ch.n - 1), t); return TRUE;
    case GDK_KEY_Page_Up: select_cell(MAX(G.sel_row - 8, 0), t); return TRUE;
    case GDK_KEY_Page_Down: select_cell(MIN(G.sel_row + 8, app.ch.n - 1), t); return TRUE;
    case GDK_KEY_Left:
        if (gc && G.sel_prog > 0) { G.sel_prog--; ensure_visible(); }
        return TRUE;
    case GDK_KEY_Right:
        if (gc && G.sel_prog + 1 < gc->n) { G.sel_prog++; ensure_visible(); }
        return TRUE;
    case GDK_KEY_Return: case GDK_KEY_KP_Enter: case GDK_KEY_space:
        if (gc && G.sel_prog >= 0) details();
        else if (G.sel_row >= 0) app_play(G.sel_row);
        return TRUE;
    case GDK_KEY_F5: app_refresh_guide(TRUE); return TRUE;
    default: return FALSE;
    }
}

/* ---- détails d'un programme -------------------------------------------- */
typedef struct { int row; Prog p; GtkWidget *win; } Det;

static void det_free(gpointer p)
{
    Det *d = p;
    g_free(d->p.title);
    g_free(d->p.episode);
    g_free(d->p.synopsis);
    g_free(d);
}

static void on_watch(GtkButton *b, gpointer ud)
{
    (void)b;
    Det *d = ud;
    app_play(d->row);
    gtk_window_destroy(GTK_WINDOW(d->win));
    if (G.win) gtk_window_close(GTK_WINDOW(G.win));
}

static void on_record(GtkButton *b, gpointer ud)
{
    (void)b;
    Det *d = ud;
    rec_start(d->row, d->p.title, d->p.end + (gint64)cfg.rec_margin * 60);
    gtk_window_destroy(GTK_WINDOW(d->win));
}

static void on_det_close(GtkButton *b, gpointer ud)
{
    (void)b;
    gtk_window_destroy(GTK_WINDOW(ud));
}

static void details(void)
{
    const GuideCh *gc = row_ch(G.sel_row);
    if (!gc || G.sel_prog < 0 || G.sel_prog >= gc->n) return;
    const Prog *p = &gc->p[G.sel_prog];
    Det *d = g_new0(Det, 1);
    d->row = G.sel_row;
    d->p.start = p->start;
    d->p.end = p->end;
    d->p.title = g_strdup(p->title);
    d->p.episode = g_strdup(p->episode);
    d->p.synopsis = g_strdup(p->synopsis);

    d->win = ui_window(p->title, 520, -1);
    gtk_window_set_transient_for(GTK_WINDOW(d->win), GTK_WINDOW(G.win));
    gtk_window_set_modal(GTK_WINDOW(d->win), TRUE);
    g_object_set_data_full(G_OBJECT(d->win), "det", d, det_free);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(box, 18);
    gtk_widget_set_margin_end(box, 18);
    gtk_widget_set_margin_top(box, 16);
    gtk_widget_set_margin_bottom(box, 16);
    gtk_window_set_child(GTK_WINDOW(d->win), box);

    GtkWidget *t = gtk_label_new(p->title);
    gtk_widget_add_css_class(t, "title-2");
    gtk_label_set_wrap(GTK_LABEL(t), TRUE);
    gtk_label_set_xalign(GTK_LABEL(t), 0);
    gtk_box_append(GTK_BOX(box), t);
    char *a = hm(p->start), *b = hm(p->end);
    const Channel *c = &app.ch.v[d->row];
    char *info = g_strdup_printf("%s %s  ·  %s – %s", c->num, c->name, a, b);
    GtkWidget *li = gtk_label_new(info);
    gtk_label_set_xalign(GTK_LABEL(li), 0);
    gtk_widget_add_css_class(li, "dim-label");
    gtk_box_append(GTK_BOX(box), li);
    g_free(info); g_free(a); g_free(b);
    if (p->episode) {
        GtkWidget *e = gtk_label_new(p->episode);
        gtk_label_set_xalign(GTK_LABEL(e), 0);
        gtk_label_set_wrap(GTK_LABEL(e), TRUE);
        gtk_box_append(GTK_BOX(box), e);
    }
    if (p->synopsis) {
        GtkWidget *s = gtk_label_new(p->synopsis);
        gtk_label_set_xalign(GTK_LABEL(s), 0);
        gtk_label_set_wrap(GTK_LABEL(s), TRUE);
        gtk_label_set_max_width_chars(GTK_LABEL(s), 60);
        gtk_label_set_selectable(GTK_LABEL(s), TRUE);
        gtk_box_append(GTK_BOX(box), s);
    }
    GtkWidget *btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(btns, GTK_ALIGN_END);
    gtk_widget_set_margin_top(btns, 10);
    gint64 now = now_s();
    if (now >= p->start && now < p->end) {   /* enregistrement de l'émission en cours */
        GtkWidget *r = gtk_button_new_with_label(T(S_guide_record));
        g_signal_connect(r, "clicked", G_CALLBACK(on_record), d);
        gtk_box_append(GTK_BOX(btns), r);
    }
    GtkWidget *w = gtk_button_new_with_label(T(S_guide_watch));
    gtk_widget_add_css_class(w, "suggested-action");
    g_signal_connect(w, "clicked", G_CALLBACK(on_watch), d);
    gtk_box_append(GTK_BOX(btns), w);
    GtkWidget *cl = gtk_button_new_with_label(T(S_close));
    g_signal_connect(cl, "clicked", G_CALLBACK(on_det_close), d->win);
    gtk_box_append(GTK_BOX(btns), cl);
    gtk_box_append(GTK_BOX(box), btns);
    gtk_window_present(GTK_WINDOW(d->win));
    gtk_widget_grab_focus(w);
}

/* ---- fenêtre ----------------------------------------------------------- */
static void on_scroll_changed(GtkAdjustment *a, gpointer ud)
{
    (void)a; (void)ud;
    gtk_widget_queue_draw(G.area);
}

static gboolean scroll_to_now(gpointer p)
{
    (void)p;
    if (!G.win || G.scrolled) return G_SOURCE_REMOVE;
    G.scrolled = TRUE;
    GtkAdjustment *ha = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(G.sw));
    gtk_adjustment_set_value(ha, MAX(0, tx(now_s()) - NAME_W - 120));
    ensure_visible();
    return G_SOURCE_REMOVE;
}

static void on_refresh(GtkButton *b, gpointer ud)
{
    (void)b; (void)ud;
    app_refresh_guide(TRUE);
}

static void on_destroy(GtkWidget *w, gpointer ud)
{
    (void)w; (void)ud;
    G.win = G.area = G.sw = G.status = NULL;
}

void guide_window_show(void)
{
    if (G.win) { gtk_window_present(GTK_WINDOW(G.win)); return; }
    G.win = ui_window(T(S_guide_title), 1100, 680);
    g_signal_connect(G.win, "destroy", G_CALLBACK(on_destroy), NULL);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(G.win), box);
    GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(top, "guide-status");
    G.status = gtk_label_new(G.status_msg ? G.status_msg : "");
    gtk_label_set_xalign(GTK_LABEL(G.status), 0);
    gtk_label_set_wrap(GTK_LABEL(G.status), TRUE);
    gtk_widget_set_hexpand(G.status, TRUE);
    gtk_box_append(GTK_BOX(top), G.status);
    GtkWidget *rb = gtk_button_new_with_label(T(S_guide_refresh));
    gtk_widget_set_focus_on_click(rb, FALSE);
    g_signal_connect(rb, "clicked", G_CALLBACK(on_refresh), NULL);
    gtk_box_append(GTK_BOX(top), rb);
    gtk_box_append(GTK_BOX(box), top);

    G.sw = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(G.sw, TRUE);
    G.area = gtk_drawing_area_new();
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(G.area), draw, NULL, NULL);
    gtk_widget_set_focusable(G.area, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(G.sw), G.area);
    gtk_box_append(GTK_BOX(box), G.sw);
    g_signal_connect(gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(G.sw)), "value-changed",
                     G_CALLBACK(on_scroll_changed), NULL);
    g_signal_connect(gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(G.sw)), "value-changed",
                     G_CALLBACK(on_scroll_changed), NULL);

    GtkGesture *g = gtk_gesture_click_new();
    g_signal_connect(g, "pressed", G_CALLBACK(on_click), NULL);
    gtk_widget_add_controller(G.area, GTK_EVENT_CONTROLLER(g));
    GtkEventController *k = gtk_event_controller_key_new();
    g_signal_connect(k, "key-pressed", G_CALLBACK(on_key), NULL);
    gtk_widget_add_controller(G.win, k);

    compute_range();
    G.scrolled = FALSE;
    int row = app.cur >= 0 ? app.cur : 0;
    G.sel_row = app.ch.n ? row : -1;
    G.sel_prog = prog_index_at(row_ch(G.sel_row), now_s());
    gtk_window_present(GTK_WINDOW(G.win));
    gtk_widget_grab_focus(G.area);
    g_timeout_add(150, scroll_to_now, NULL);
    app_refresh_guide(FALSE);
}

void guide_window_refresh(void)
{
    if (!G.win) return;
    compute_range();
    const GuideCh *gc = row_ch(G.sel_row);
    if (G.sel_prog >= (gc ? gc->n : 0)) G.sel_prog = gc && gc->n ? gc->n - 1 : -1;
    if (G.sel_prog < 0 && gc) G.sel_prog = prog_index_at(gc, now_s());
    gtk_widget_queue_draw(G.area);
}

void guide_window_set_status(const char *msg)
{
    g_free(G.status_msg);
    G.status_msg = g_strdup(msg);
    if (G.status) gtk_label_set_text(GTK_LABEL(G.status), msg ? msg : "");
}
