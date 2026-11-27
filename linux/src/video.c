/*
 * video.c — affichage de l'image libVLC dans GTK 4.
 *
 * GTK 4 ne permet plus d'incruster une fenêtre X11 (libvlc_media_player_set_xwindow)
 * et ne fonctionnerait pas sous Wayland : libVLC décode donc dans un tampon mémoire
 * (callbacks « vmem », format RV32), chaque image est copiée puis transmise au thread
 * GTK sous forme de GdkMemoryTexture affichée par une GtkPicture.  Seule la dernière
 * image est gardée si GTK prend du retard.
 *
 * Le format d'image (16:9, 4:3…) est appliqué par une GtkAspectFrame : choisi dans
 * Paramètres, ou calculé d'après la piste vidéo (pixels non carrés de la TNT SD).
 */
#include <string.h>

#include "app.h"

struct VideoOut {
    GtkPicture *pic;
    GtkAspectFrame *frame;
    int max_h;
    libvlc_media_player_t *mp;

    GMutex lock;               /* protège ce qui suit */
    libvlc_media_t *media;     /* média en cours (piste vidéo → rapport d'aspect) */
    guint8 *buf;               /* tampon de décodage (thread vidéo de VLC) */
    unsigned w, h, src_w, src_h;
    GBytes *latest;            /* dernière image à afficher */
    unsigned lw, lh;
    guint idle_id;
    unsigned frames;
};

static unsigned fmt_cb(void **opaque, char *chroma, unsigned *width, unsigned *height, unsigned *pitches, unsigned *lines)
{
    VideoOut *v = *opaque;
    unsigned w = *width, h = *height;
    if (!w || !h) return 0;
    g_mutex_lock(&v->lock);
    v->src_w = w;
    v->src_h = h;
    /* limite la taille (encart PIP) : VLC met l'image à l'échelle */
    if (v->max_h > 0 && h > (unsigned)v->max_h) {
        w = (unsigned)((double)w * v->max_h / h + 0.5) & ~1u;
        h = (unsigned)v->max_h;
    }
    memcpy(chroma, "RV32", 4);
    *width = w;
    *height = h;
    pitches[0] = w * 4;
    lines[0] = h;
    g_free(v->buf);
    v->buf = g_try_malloc((gsize)w * h * 4);
    v->w = v->buf ? w : 0;
    v->h = v->buf ? h : 0;
    v->frames = 0;
    g_mutex_unlock(&v->lock);
    if (hr_debug) g_message("vidéo : %ux%u décodée en %ux%u", v->src_w, v->src_h, w, h);
    return v->buf ? 1 : 0;
}

static void cleanup_cb(void *opaque)
{
    VideoOut *v = opaque;
    g_mutex_lock(&v->lock);
    g_free(v->buf);
    v->buf = NULL;
    v->w = v->h = 0;
    g_mutex_unlock(&v->lock);
}

static void *lock_cb(void *opaque, void **planes)
{
    VideoOut *v = opaque;
    planes[0] = v->buf;   /* le tampon ne change qu'entre fmt_cb et cleanup_cb (même thread) */
    return NULL;
}

static void unlock_cb(void *opaque, void *picture, void *const *planes)
{
    (void)opaque; (void)picture; (void)planes;
}

static gboolean show_idle(gpointer p)
{
    VideoOut *v = p;
    g_mutex_lock(&v->lock);
    GBytes *b = v->latest;
    unsigned w = v->lw, h = v->lh;
    v->latest = NULL;
    v->idle_id = 0;
    g_mutex_unlock(&v->lock);
    if (b) {
        GdkTexture *t = gdk_memory_texture_new((int)w, (int)h, GDK_MEMORY_B8G8R8X8, b, (gsize)w * 4);
        gtk_picture_set_paintable(v->pic, GDK_PAINTABLE(t));
        g_object_unref(t);
        g_bytes_unref(b);
    }
    return G_SOURCE_REMOVE;
}

static void display_cb(void *opaque, void *picture)
{
    VideoOut *v = opaque;
    (void)picture;
    g_mutex_lock(&v->lock);
    if (v->buf && v->w && v->h) {
        GBytes *b = g_bytes_new(v->buf, (gsize)v->w * v->h * 4);
        if (v->latest) g_bytes_unref(v->latest);   /* GTK en retard : image sautée */
        v->latest = b;
        v->lw = v->w;
        v->lh = v->h;
        if (!v->idle_id) v->idle_id = g_idle_add_full(G_PRIORITY_HIGH_IDLE, show_idle, v, NULL);
        if (hr_debug && (++v->frames == 1 || v->frames % 250 == 0)) g_message("vidéo : %u images affichées", v->frames);
    }
    g_mutex_unlock(&v->lock);
}

VideoOut *video_new(GtkPicture *pic, GtkAspectFrame *frame, int max_h)
{
    VideoOut *v = g_new0(VideoOut, 1);
    v->pic = pic;
    v->frame = frame;
    v->max_h = max_h;
    g_mutex_init(&v->lock);
    gtk_picture_set_content_fit(pic, GTK_CONTENT_FIT_FILL);   /* le cadre d'aspect donne la forme */
    gtk_picture_set_can_shrink(pic, TRUE);
    return v;
}

void video_attach(VideoOut *v, libvlc_media_player_t *mp)
{
    v->mp = mp;
    libvlc_video_set_callbacks(mp, lock_cb, unlock_cb, display_cb, v);
    libvlc_video_set_format_callbacks(mp, fmt_cb, cleanup_cb);
}

void video_set_media(VideoOut *v, libvlc_media_t *m)
{
    if (m) libvlc_media_retain(m);
    g_mutex_lock(&v->lock);
    libvlc_media_t *old = v->media;
    v->media = m;
    g_mutex_unlock(&v->lock);
    if (old) libvlc_media_release(old);
    video_update_aspect(v);
}

/* « 16:9 », « 235:100 », « 2.35:1 » → rapport ; 0 si vide ou invalide. */
static double parse_ratio(const char *s)
{
    if (!s || !*s) return 0;
    double a = g_ascii_strtod(s, NULL);
    const char *c = strchr(s, ':');
    double b = c ? g_ascii_strtod(c + 1, NULL) : 1;
    return a > 0 && b > 0 ? a / b : 0;
}

void video_update_aspect(VideoOut *v)
{
    double r = parse_ratio(cfg.aspect);
    if (r <= 0) {
        g_mutex_lock(&v->lock);
        libvlc_media_t *m = v->media;
        if (m) libvlc_media_retain(m);
        unsigned sw = v->src_w, sh = v->src_h;
        g_mutex_unlock(&v->lock);
        double sar = 0;
        if (m) {
            libvlc_media_track_t **tr = NULL;
            unsigned n = libvlc_media_tracks_get(m, &tr);
            for (unsigned i = 0; i < n; i++) {
                if (tr[i]->i_type != libvlc_track_video || !tr[i]->video) continue;
                const libvlc_video_track_t *vt = tr[i]->video;
                if (vt->i_sar_num && vt->i_sar_den) sar = (double)vt->i_sar_num / vt->i_sar_den;
                if (!sw || !sh) { sw = vt->i_width; sh = vt->i_height; }
                break;
            }
            if (tr) libvlc_media_tracks_release(tr, n);
            libvlc_media_release(m);
        }
        if (sw && sh) {
            /* TNT SD (720×576, 704×576) sans information : 16:9 */
            if (sar <= 0) sar = (sh == 576 && (sw == 720 || sw == 704)) ? (16.0 / 9) * sh / sw : 1;
            r = sar * sw / sh;
        }
        if (r <= 0) r = 16.0 / 9;
    }
    gtk_aspect_frame_set_ratio(v->frame, (float)r);
}

void video_clear(VideoOut *v)
{
    g_mutex_lock(&v->lock);
    if (v->latest) { g_bytes_unref(v->latest); v->latest = NULL; }
    if (v->idle_id) { g_source_remove(v->idle_id); v->idle_id = 0; }
    g_mutex_unlock(&v->lock);
    gtk_picture_set_paintable(v->pic, NULL);
}

void video_free(VideoOut *v)
{
    if (!v) return;
    video_clear(v);
    g_mutex_lock(&v->lock);
    if (v->media) libvlc_media_release(v->media);
    g_free(v->buf);
    g_mutex_unlock(&v->lock);
    g_mutex_clear(&v->lock);
    g_free(v);
}
