# VlcMaster

Lecteur IPTV natif pour **HD HomeRun** (ou toute playlist M3U), écrit en **C / Win32**.
La vidéo est rendue **directement dans la fenêtre de l'application** grâce à libVLC :
plus besoin de lancer un VLC externe ni d'activer son interface HTTP.

📺 VLC Master 2 — lecteur IPTV intégré

## ✨ Caractéristiques

- **Vidéo intégrée** : libVLC dessine l'image dans la fenêtre (Direct3D 11, décodage matériel).
- **Liste des chaînes automatique** : lue depuis `lineup.m3u` (noms et numéros du tuner), affichable / masquable.
- **Zapping instantané** : ← → , molette, saisie du numéro au clavier ; les appuis rapides sont regroupés.
- **Bouton ⚙ Paramètres** : fenêtre **redimensionnable** et adaptative (2 colonnes quand elle est large, empilée quand elle est étroite), réglages **appliqués immédiatement** :
  - source : URL ou fichier de la playlist, rechargement à chaud ;
  - lecture : cache réseau, décodage matériel (Auto / D3D11VA / DXVA2 / logiciel), désentrelacement (Yadif, Yadif 2x, Bob…), format d'image, synchro audio ;
  - interface : premier plan, reprise de la dernière chaîne, lecture automatique, liste latérale ;
  - chaînes : renommer (F2), masquer (case à cocher), réordonner (▲ ▼ ou Alt+↑/↓), nom d'origine ;
  - moteur : dossier de VLC (détection automatique sinon).
- **Plein écran** (F, double-clic, Échap pour sortir) ; la barre de commandes réapparaît en approchant la souris du bas de l'écran.
- **Guide TV** (automatique avec un HDHomeRun, environ 24 h de programmes) :
  - fenêtre **Guide TV** en grille horaire, avec la ligne « maintenant », l'avancement de chaque émission, et les détails (épisode, durée, résumé) ; double-clic ou Entrée pour regarder ;
  - programme en cours (avec barre d'avancement) sous chaque chaîne de la liste et dans la barre d'état ;
  - émission en cours et suivante dans le HUD ;
  - source : le service guide de SiliconDust (api.hdhomerun.com), interrogé avec l'identifiant de ton tuner ; on peut le désactiver dans Paramètres.
- **HUD incrusté** sur l'image : numéro et nom de la chaîne, état et résolution, heure, barre de volume (muet, >100 %). Il s'affiche au zapping, au volume, à chaque action, ou avec la touche I ; sa durée se règle dans Paramètres (0 = désactivé).
- **Bordure fine** : fenêtre sans barre de titre, liseré de 2 px ; on la déplace en glissant l'image et on la redimensionne par les bords. La barre de commandes peut se masquer et réapparaître au survol.
- **Bouton ▣ Taille** : règle l'image sur une taille prédéfinie (par défaut 1080p, 720p, 480p, 240p, ou `1280x720`…, à définir dans Paramètres). Un clic passe à la suivante, la flèche ouvre la liste.
- **Bouton Premier plan / Arrière-plan** : garder la fenêtre au-dessus des autres, ou non.
- **Reconnexion automatique** si le flux est coupé.
- **Haute résolution (DPI)** gérée par écran, interface entièrement en français.
- Réglages sauvegardés dans `VlcMaster.ini` (à côté de l'exe, sinon dans `%APPDATA%\VlcMaster`).

## ⌨️ Raccourcis

| Touche | Action |
|---|---|
| ← / → (ou Page préc./suiv.) | Chaîne précédente / suivante |
| 0-9 | Aller à la chaîne n° (validation auto ou Entrée) |
| ↑ / ↓, molette sur l'image | Volume |
| Espace | Pause / lecture |
| S | Stop |
| M | Muet |
| F, F11, double-clic | Plein écran |
| Échap | Quitter le plein écran |
| L | Afficher / masquer la liste des chaînes |
| P, F2 | Paramètres |
| I | Afficher le HUD |
| G | Guide TV (dans le guide : flèches, Entrée = regarder, Début = maintenant, Échap = fermer) |
| B | Bordure fine / fenêtre normale |
| T | Taille prédéfinie suivante |
| A | Premier plan / arrière-plan |
| Clic droit sur l'image | Menu (format d'image, désentrelacement…) |
| Clic droit sur une chaîne | Regarder / renommer / masquer |

## 🚀 Installation

Prérequis : **VLC 3.x 64 bits** installé (https://www.videolan.org).
L'appli trouve VLC toute seule (registre, `Program Files`, dossier de l'exe ou sous-dossier `vlc\`).
Avec la version « zip » de VLC, il suffit d'indiquer son dossier dans ⚙ Paramètres → *Moteur VLC*.
Au premier lancement d'un VLC portable, le cache des plugins (`plugins\plugins.dat`) est créé :
les démarrages suivants sont quasi instantanés.

Lancement : `VlcMaster.exe` (option : `VlcMaster.exe <playlist ou flux>`).

## 🛠️ Compilation

MinGW-w64 64 bits (gcc + windres), aucun SDK VLC nécessaire (libVLC est chargée dynamiquement) :

```bat
build.bat
```

Résultat : `build\VlcMaster.exe` (exécutable autonome, lié statiquement).

## 📂 Structure

- `src/vlcmaster.c` : application (fenêtre, lecture, paramètres, playlist).
- `src/libvlc_dyn.h` : liaison dynamique à l'API libVLC 3.x.
- `src/guide_data.h` / `src/guide_ui.h` : guide TV (téléchargement, mini-lecteur JSON, fenêtre grille).
- `res/` : manifeste (thèmes, DPI) et informations de version.
- `legacy/VlcMaster.pyw` : ancienne télécommande Python/tkinter (pilotage HTTP de VLC).

By Popov ©2026
