# HomeRun Player — Linux

Version Linux native (C + GTK 4 + libVLC 3) du lecteur TV pour tuners HDHomeRun.
Fonctionne sous X11 et Wayland (l'image de libVLC est rendue en mémoire puis affichée par GTK).
Les fonctions et les textes reprennent la version Windows (`src/`) et la version Android (branche `android`).

## Fonctions

- **Lecture** : playlist `http://<ip>/lineup.m3u` (⚙ Paramètres, défaut `http://192.168.1.90/lineup.m3u` ;
  un fichier `.m3u` local est aussi accepté), liste des chaînes avec programme en cours, zapping
  (◀ ▶, ← →, Page ↑/↓, chiffres), volume (barre, ↑ ↓, molette, M = muet), plein écran (F11, double-clic),
  reprise de la dernière chaîne, nouvelle tentative automatique si le flux coupe (délai croissant, 12 essais).
- **Guide TV** (G) : `discover.json` → `DeviceAuth` → `api.hdhomerun.com/api/guide.php`, pages `&Start=`
  jusqu'à ~24 h ; grille horaire (souris ou flèches), colonne des chaînes et heures fixes ;
  une case → détails, Regarder, Enregistrer (émission en cours). Actualisé toutes les 3 h.
- **Langues** (L, V = audio suivant, U = sous-titres) : VF, VO (qaa), audiodescription (qad), sous-titres DVB ;
  le choix est mémorisé et réappliqué sur les autres chaînes.
- **Enregistrement** (R) : copie du flux en `.ts` « AAAA-MM-JJ HHhMM - Chaîne - Émission.ts », jusqu'à la fin
  de l'émission + marge, 30 min…3 h ou sans limite. Liste (E) : arrêter, lire avec reprise de la position,
  lire depuis le début, supprimer, ouvrir le dossier (par défaut `~/Vidéos/HomeRun`).
- **PIP** (P) : seconde chaîne en encart ; clic sur l'encart (ou Tab) = son de l'encart ; inverser, déplacer, fermer.
- **Tuner** (T, ou ⚙ › Tuner HDHomeRun) : nom, modèle, firmware, mise à jour disponible (elle s'installe
  depuis la page du tuner, bouton « Page du tuner »), scan des chaînes (`lineup.post?scan=start`, suivi
  `lineup_status.json`, arrêt) ; la playlist est relue à la fin.
- **Mises à jour** : bandeau si la dernière release GitHub (`Popov2026/VlcMaster-V2`) est plus récente et
  contient un paquet Linux (`.deb`, `.AppImage` ou `*linux*.tar.gz`) ; « ✕ » = ne plus signaler cette version.
- **6 langues** (fr, en, es, de, it, pt), langue du système par défaut, ⚙ › Langue.

Raccourcis : `?` ou F1 les affiche. Réglages : `~/.config/homerun-player/settings.ini`
(positions de lecture des enregistrements : `positions.ini`).

## Compiler

Debian 13 / Ubuntu 24.04 ou plus récent (GTK ≥ 4.14) :

```sh
sudo apt install meson ninja-build gcc pkg-config libgtk-4-dev libvlc-dev libcurl4-openssl-dev vlc-plugin-base
cd linux
meson setup build
meson compile -C build
meson test -C build          # tests du cœur (playlist, guide, JSON, pistes, noms de fichiers)
./build/homerun-player
```

Fedora : `gtk4-devel vlc-devel libcurl-devel` (VLC depuis RPM Fusion). Arch : `gtk4 vlc curl meson`.

Paquets : `sh packaging/make_dist.sh <version> build dist` → `HomeRunPlayer-<version>-linux-<arch>.tar.gz`
(avec `install.sh`, installe dans `~/.local` par défaut) et `HomeRunPlayer-<version>-linux-<arch>.deb`.

Les textes sont générés par `python3 tools/gen_strings.py` (→ `src/strings_ids.h`, `src/strings_gen.h`) ;
modifier la table du script, pas les fichiers générés.

## Installer

```sh
sudo apt install ./HomeRunPlayer-3.7-linux-amd64.deb
```

ou, depuis l'archive : `tar xzf HomeRunPlayer-3.7-linux-x86_64.tar.gz && ./HomeRunPlayer-3.7-linux-x86_64/install.sh`
(dépendances : `libgtk-4-1 libvlc5 vlc-plugin-base libcurl4t64`).

## Tests

- `tests/test_core.c` (`meson test`) : JSON, playlist M3U, guide paginé (faux réseau), clés de pistes,
  versions, noms de fichiers. Accepte en option un vrai `guide.json` et une vraie `lineup.m3u`.
- `tests/fake_tuner.py` : faux HDHomeRun local (mêmes adresses que `android/tools/fake_tuner.py`, scan
  simulé ~20 s). `--ts fichier.ts` sert un flux de test pour chaque chaîne ; `--real http://192.168.1.90:5004/auto`
  renvoie vers le vrai tuner (lecture seule). Playlist : `http://127.0.0.1:8090/lineup.m3u`.
  **Ne jamais lancer de scan sur le vrai tuner pour tester.**
- `tests/smoke.sh <binaire>` : essai de bout en bout sous Xvfb (faux tuner + flux de test généré par ffmpeg) :
  playlist chargée, dernière chaîne reprise, images affichées, capture d'écran.
- `HR_DEBUG=1 ./build/homerun-player` : messages de diagnostic (événements libVLC, taille de l'image).

GitHub Actions : `.github/workflows/linux.yml` (ubuntu-24.04) compile, lance les tests et l'essai sous Xvfb,
construit le `.tar.gz` et le `.deb`, vérifie l'installation du `.deb` et publie l'artefact `HomeRunPlayer-linux`
(+ `smoke-test` : journaux et capture d'écran). Quand une release est publiée sur GitHub, le même workflow
y joint le `.tar.gz` et le `.deb` (il ne crée jamais de release lui-même).

## Organisation

| Fichier | Rôle | Windows / Android |
|---|---|---|
| `src/main.c` | fenêtre principale, lecture, zapping, PIP, HUD, clavier | `vlcmaster.c`, `pip.h` / `MainActivity.kt` |
| `src/video.c` | image libVLC (vmem RV32) → `GdkMemoryTexture`, format d'image | — |
| `src/core.c`, `src/json.c` | playlist, guide, pistes, versions, noms de fichiers (sans GTK) | `guide_data.h` / `Net.kt`, `Guide.kt` |
| `src/net.c` | HTTP (libcurl), travaux en arrière-plan | WinINet / `Net.kt` |
| `src/tracks.c` | langues audio / sous-titres | `tracks.h` / `Tracks.kt` |
| `src/guide_ui.c` | grille du guide | `guide_ui.h` / `GuideActivity.kt` |
| `src/rec.c` | enregistrement, liste, lecteur | `record.h` / `RecordService.kt`… |
| `src/tuner_ui.c` | tuner, firmware, scan | `tuner.h` / `TunerActivity.kt` |
| `src/settings_ui.c`, `src/config.c` | Paramètres | `SettingsActivity.kt` |
| `src/update.c` | bandeau de nouvelle version | `update.h` / `Updater.kt` |
| `src/i18n.c`, `tools/gen_strings.py` | 6 langues | `lang.h`, `lang_table.h` / `gen_strings.py` |

## Reste à faire / non vérifié

- Développé sous Windows sans WSL : l'interface GTK est compilée et essayée uniquement en intégration
  continue (Xvfb, flux de test : playlist chargée, dernière chaîne reprise, images affichées). Pas encore
  essayée sur un vrai bureau Linux ni avec le vrai HDHomeRun (le cœur — playlist, guide — a été vérifié sur
  les vraies données du tuner).
- Pas d'AppImage (GTK 4 + libVLC et ses plugins à embarquer) : `.deb` et `.tar.gz` utilisant les paquets du système.
- Décodage matériel : avec le rendu en mémoire, VA-API n'est utilisé que si libVLC sait recopier l'image ;
  sinon décodage logiciel. Désactiver « Décodage matériel » si l'image est verte ou saccadée.
- Pas de programmation d'enregistrements futurs (le guide n'enregistre que l'émission en cours) ;
  les enregistrements s'arrêtent si l'appli est fermée (confirmation demandée).
- Pas de télécommande infrarouge / LIRC ; touches multimédia du clavier non gérées.
