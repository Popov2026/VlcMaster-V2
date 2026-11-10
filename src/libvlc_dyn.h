/*
 * libvlc_dyn.h — liaison dynamique minimale à libVLC 3.x
 *
 * Aucun SDK VLC n'est nécessaire pour compiler : les fonctions sont
 * résolues à l'exécution dans libvlc.dll (LoadLibrary / GetProcAddress).
 * Seules les signatures de l'API libVLC 3.x utilisées par l'appli sont
 * déclarées ici.
 */
#pragma once
#include <stdint.h>

typedef struct libvlc_instance_t      libvlc_instance_t;
typedef struct libvlc_media_t         libvlc_media_t;
typedef struct libvlc_media_player_t  libvlc_media_player_t;
typedef struct libvlc_event_manager_t libvlc_event_manager_t;

/* Disposition identique à libvlc_event_t (VLC 3.x) pour les champs lus. */
typedef struct vlc_event_s {
    int   type;
    void *p_obj;
    union {
        struct { float new_cache; } buffering;
        char pad[64];
    } u;
} vlc_event_t;

typedef void (*vlc_callback_t)(const vlc_event_t *, void *);

/* libvlc_event_e (VLC 3.x) */
enum {
    EV_Opening   = 0x102,
    EV_Buffering = 0x103,
    EV_Playing   = 0x104,
    EV_Paused    = 0x105,
    EV_Stopped   = 0x106,
    EV_EndReached= 0x109,
    EV_Error     = 0x10A,
    EV_Vout      = 0x112
};

#define VLC_FUNCS(X) \
 X(libvlc_instance_t*,      libvlc_new,                     (int, const char *const *)) \
 X(void,                    libvlc_release,                 (libvlc_instance_t*)) \
 X(const char*,             libvlc_get_version,             (void)) \
 X(const char*,             libvlc_errmsg,                  (void)) \
 X(libvlc_media_t*,         libvlc_media_new_location,      (libvlc_instance_t*, const char*)) \
 X(libvlc_media_t*,         libvlc_media_new_path,          (libvlc_instance_t*, const char*)) \
 X(void,                    libvlc_media_add_option,        (libvlc_media_t*, const char*)) \
 X(void,                    libvlc_media_release,           (libvlc_media_t*)) \
 X(libvlc_media_player_t*,  libvlc_media_player_new,        (libvlc_instance_t*)) \
 X(void,                    libvlc_media_player_release,    (libvlc_media_player_t*)) \
 X(void,                    libvlc_media_player_set_media,  (libvlc_media_player_t*, libvlc_media_t*)) \
 X(int,                     libvlc_media_player_play,       (libvlc_media_player_t*)) \
 X(void,                    libvlc_media_player_stop,       (libvlc_media_player_t*)) \
 X(void,                    libvlc_media_player_set_pause,  (libvlc_media_player_t*, int)) \
 X(void,                    libvlc_media_player_set_hwnd,   (libvlc_media_player_t*, void*)) \
 X(libvlc_event_manager_t*, libvlc_media_player_event_manager, (libvlc_media_player_t*)) \
 X(int,                     libvlc_event_attach,            (libvlc_event_manager_t*, int, vlc_callback_t, void*)) \
 X(int,                     libvlc_audio_set_volume,        (libvlc_media_player_t*, int)) \
 X(void,                    libvlc_audio_set_mute,          (libvlc_media_player_t*, int)) \
 X(int,                     libvlc_audio_set_delay,         (libvlc_media_player_t*, int64_t)) \
 X(void,                    libvlc_video_set_mouse_input,   (libvlc_media_player_t*, unsigned)) \
 X(void,                    libvlc_video_set_key_input,     (libvlc_media_player_t*, unsigned)) \
 X(void,                    libvlc_video_set_aspect_ratio,  (libvlc_media_player_t*, const char*)) \
 X(void,                    libvlc_video_set_deinterlace,   (libvlc_media_player_t*, const char*)) \
 X(int,                     libvlc_video_get_size,          (libvlc_media_player_t*, unsigned, unsigned*, unsigned*))

#define X(r, n, a) static r (*p_##n) a;
VLC_FUNCS(X)
#undef X

/* Résout toutes les fonctions ; renvoie 0 si l'une manque (ex. VLC 4.x). */
static int VlcBind(HMODULE h)
{
#define X(r, n, a) if (!(p_##n = (r (*) a)(void (*)(void))GetProcAddress(h, #n))) return 0;
    VLC_FUNCS(X)
#undef X
    return 1;
}
