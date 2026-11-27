/* ui.c — petits outils d'interface : menus (popover de boutons), messages, fenêtres. */
#include "app.h"

static int g_menus_open;

gboolean menu_any_open(void) { return g_menus_open > 0; }

static gboolean unparent_idle(gpointer p)
{
    GtkWidget *w = p;
    if (gtk_widget_get_parent(w)) gtk_widget_unparent(w);
    g_object_unref(w);
    return G_SOURCE_REMOVE;
}

static void on_menu_closed(GtkPopover *pop, gpointer ud)
{
    (void)ud;
    g_menus_open--;
    g_idle_add(unparent_idle, g_object_ref(pop));
}

GtkWidget *menu_new(GtkWidget *anchor)
{
    GtkWidget *pop = gtk_popover_new();
    gtk_widget_set_parent(pop, anchor);
    gtk_popover_set_has_arrow(GTK_POPOVER(pop), TRUE);
    GtkWidget *sw = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(sw), TRUE);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(sw), 520);
    gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(sw), TRUE);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), box);
    gtk_popover_set_child(GTK_POPOVER(pop), sw);
    g_object_set_data(G_OBJECT(pop), "box", box);
    g_signal_connect(pop, "closed", G_CALLBACK(on_menu_closed), NULL);
    return pop;
}

typedef struct { MenuFn fn; gpointer data; GDestroyNotify destroy; } MenuItem;

static void item_free(gpointer p)
{
    MenuItem *it = p;
    if (it->destroy && it->data) it->destroy(it->data);
    g_free(it);
}

static void on_item(GtkButton *b, gpointer ud)
{
    MenuItem *it = ud;
    GtkWidget *pop = gtk_widget_get_ancestor(GTK_WIDGET(b), GTK_TYPE_POPOVER);
    MenuFn fn = it->fn;
    gpointer data = it->data;
    /* l'action peut ouvrir une autre fenêtre ou un autre menu : fermer d'abord */
    g_object_ref(b);
    if (pop) gtk_popover_popdown(GTK_POPOVER(pop));
    if (fn) fn(data);
    g_object_unref(b);
}

void menu_add(GtkWidget *menu, const char *label, MenuFn fn, gpointer data, GDestroyNotify destroy)
{
    GtkWidget *box = g_object_get_data(G_OBJECT(menu), "box");
    GtkWidget *b = gtk_button_new_with_label(label);
    gtk_widget_add_css_class(b, "flat");
    gtk_widget_add_css_class(b, "menuitem");
    GtkWidget *lbl = gtk_button_get_child(GTK_BUTTON(b));
    if (GTK_IS_LABEL(lbl)) gtk_label_set_xalign(GTK_LABEL(lbl), 0);
    MenuItem *it = g_new0(MenuItem, 1);
    it->fn = fn;
    it->data = data;
    it->destroy = destroy;
    g_object_set_data_full(G_OBJECT(b), "item", it, item_free);
    g_signal_connect(b, "clicked", G_CALLBACK(on_item), it);
    if (!fn) gtk_widget_set_sensitive(b, FALSE);
    gtk_box_append(GTK_BOX(box), b);
}

void menu_add_separator(GtkWidget *menu)
{
    GtkWidget *box = g_object_get_data(G_OBJECT(menu), "box");
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
}

void menu_popup(GtkWidget *menu)
{
    g_menus_open++;
    gtk_popover_popup(GTK_POPOVER(menu));
}

void menu_popup_at(GtkWidget *menu, double x, double y)
{
    GdkRectangle r = { (int)x, (int)y, 1, 1 };
    gtk_popover_set_pointing_to(GTK_POPOVER(menu), &r);
    gtk_popover_set_position(GTK_POPOVER(menu), GTK_POS_BOTTOM);
    menu_popup(menu);
}

/* ---- messages ----------------------------------------------------------- */
void ui_message(GtkWindow *parent, const char *title, const char *msg)
{
    GtkAlertDialog *d = gtk_alert_dialog_new("%s", title ? title : APP_NAME);
    if (msg) gtk_alert_dialog_set_detail(d, msg);
    gtk_alert_dialog_show(d, parent ? parent : app.win);
    g_object_unref(d);
}

typedef struct { MenuFn fn; gpointer data; GDestroyNotify destroy; } Confirm;

static void on_confirm(GObject *src, GAsyncResult *res, gpointer ud)
{
    Confirm *c = ud;
    int b = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(src), res, NULL);
    if (b == 1 && c->fn) c->fn(c->data);
    if (c->destroy && c->data) c->destroy(c->data);
    g_free(c);
}

void ui_confirm(GtkWindow *parent, const char *title, const char *msg, const char *ok_label, MenuFn fn, gpointer data,
                GDestroyNotify destroy)
{
    GtkAlertDialog *d = gtk_alert_dialog_new("%s", title);
    if (msg) gtk_alert_dialog_set_detail(d, msg);
    const char *buttons[] = { T(S_cancel), ok_label ? ok_label : T(S_ok), NULL };
    gtk_alert_dialog_set_buttons(d, buttons);
    gtk_alert_dialog_set_cancel_button(d, 0);
    gtk_alert_dialog_set_default_button(d, 1);
    Confirm *c = g_new0(Confirm, 1);
    c->fn = fn;
    c->data = data;
    c->destroy = destroy;
    gtk_alert_dialog_choose(d, parent ? parent : app.win, NULL, on_confirm, c);
    g_object_unref(d);
}

void ui_open_uri(GtkWindow *parent, const char *uri)
{
    GtkUriLauncher *l = gtk_uri_launcher_new(uri);
    gtk_uri_launcher_launch(l, parent ? parent : app.win, NULL, NULL, NULL);
    g_object_unref(l);
}

static gboolean on_win_key(GtkEventControllerKey *c, guint keyval, guint keycode, GdkModifierType st, gpointer ud)
{
    (void)c; (void)keycode; (void)st;
    if (keyval == GDK_KEY_Escape) { gtk_window_close(GTK_WINDOW(ud)); return TRUE; }
    return FALSE;
}

GtkWidget *ui_window(const char *title, int w, int h)
{
    GtkWidget *win = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(win), title);
    gtk_window_set_default_size(GTK_WINDOW(win), w, h);
    gtk_window_set_transient_for(GTK_WINDOW(win), app.win);
    gtk_window_set_application(GTK_WINDOW(win), app.gapp);
    gtk_window_set_icon_name(GTK_WINDOW(win), APP_ID);
    GtkEventController *k = gtk_event_controller_key_new();
    g_signal_connect(k, "key-pressed", G_CALLBACK(on_win_key), win);
    gtk_widget_add_controller(win, k);
    return win;
}

char *fmt_size(gint64 bytes)
{
    return g_format_size((guint64)MAX(bytes, 0));
}
