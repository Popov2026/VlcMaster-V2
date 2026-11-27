/*
 * core.h — logique sans interface ni réseau (testée par tests/test_core.c) :
 * playlist M3U, guide TV, clés de pistes, versions, noms de fichiers.
 *
 * Équivalents : Net.kt / Guide.kt / Tracks.kt / Updater.kt (Android),
 * vlcmaster.c / guide_data.h / tracks.h / update.h (Windows).
 */
#ifndef HR_CORE_H
#define HR_CORE_H

#include <stddef.h>
#include <time.h>

#include "json.h"

/* ---- Playlist ---------------------------------------------------------- */
typedef struct { char *num, *name, *url; } Channel;
typedef struct { Channel *v; int n; } ChannelList;

/* Analyse lineup.m3u (texte UTF-8) ; renvoie le nombre de chaînes. */
int playlist_parse(const char *text, ChannelList *out);
void playlist_free(ChannelList *l);
/* Index de la chaîne d'adresse url (-1 si absente). */
int playlist_find_url(const ChannelList *l, const char *url);
/* Index de la chaîne de numéro num (-1 si absente). */
int playlist_find_num(const ChannelList *l, const char *num);

/* ---- Guide TV ---------------------------------------------------------- */
typedef struct { long long start, end; char *title, *episode, *synopsis; } Prog;
typedef struct { char *num, *name; Prog *p; int n, cap; } GuideCh;
typedef struct { GuideCh *ch; int n, cap; long long max_end; } Guide;

/* Fonction de téléchargement (bloquante) : texte alloué (free) ou NULL + *err (free). */
typedef char *(*FetchFn)(const char *url, int timeout_ms, char **err, void *ud);

enum { GUIDE_OK, GUIDE_ERR_NO_TUNER, GUIDE_ERR_SERVICE, GUIDE_ERR_EMPTY };

/* discover.json → DeviceAuth → api.hdhomerun.com/api/guide.php, pages &Start= jusqu'à
   now + 24 h.  base = « http://hôte[:port] ».  Renvoie GUIDE_xxx ; *err éventuel (free). */
int guide_load(const char *base, FetchFn fetch, void *ud, long long now, Guide *out, char **err);
/* Ajoute une page (tableau JSON du service guide) ; renvoie la fin la plus tardive. */
long long guide_merge(Guide *g, const JVal *arr);
void guide_free(Guide *g);
const GuideCh *guide_find(const Guide *g, const char *num);
const Prog *guide_at(const GuideCh *c, long long t);     /* programme en cours à t */
const Prog *guide_next(const GuideCh *c, long long t);   /* premier programme commençant après t */

/* ---- Pistes audio / sous-titres ---------------------------------------- */
/* « Track 1 - [French] » → « French » (copie dans out ; 0 si pas de crochets). */
int track_bracket(const char *raw, char *out, size_t cap);
int track_is_ad(const char *raw);    /* audiodescription */
int track_is_hoh(const char *raw);   /* malentendants */
/* Clé mémorisée : langue en minuscules + « |ad » / « |hoh ». */
void track_key(const char *raw, char *out, size_t cap);

/* ---- Divers ------------------------------------------------------------ */
/* « v3.10.2 » comparé à « 3.9 » : <0, 0, >0. */
int version_cmp(const char *a, const char *b);
/* Adresse du tuner (« http://hôte[:port] ») d'après l'adresse de la playlist ; 0 si invalide. */
int tuner_base(const char *playlist_url, char *out, size_t cap);
/* Remplace les caractères interdits dans un nom de fichier, réduit les espaces, coupe à maxlen octets (UTF-8 respecté). */
void sanitize_name(const char *in, char *out, size_t cap, size_t maxlen);
/* « AAAA-MM-JJ HHhMM - Chaîne - Émission » (sans extension). */
void rec_basename(time_t t, const char *channel, const char *title, char *out, size_t cap);
/* Chemin libre « dir/base.ts », sinon « dir/base (2).ts »… (malloc). */
char *rec_unique_path(const char *dir, const char *base);
/* Durée : « 1 h », « 1 h 02 », « 5 min », « 5 min 03 s ». */
void fmt_dur(long long sec, char *out, size_t cap);
/* Encodage %XX pour une URL (malloc). */
char *url_encode(const char *s);
/* Lit le contenu entre les crochets/guillemets d'un attribut M3U (key="…"), copie dans out. */
int m3u_attr(const char *line, const char *key, char *out, size_t cap);

#endif
