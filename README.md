# HomeRun Player

<img src="res/app.ico" width="64" align="right" alt="">

Page du projet : https://popov2026.github.io/VlcMaster-V2/

Lecteur TV natif pour **HDHomeRun** (ou toute playlist M3U), écrit en **C / Win32**, avec guide TV et enregistrement.
Anciennement *VLC Master*.
La vidéo est rendue **directement dans la fenêtre de l'application** grâce à libVLC :
plus besoin de lancer un VLC externe ni d'activer son interface HTTP.

📺 HomeRun Player 3 — regarder, consulter le guide, enregistrer

## 💻 Autres systèmes

Depuis la 3.8, chaque release contient aussi des versions natives pour **Linux** (GTK + libVLC, paquet `.deb` et archive
`.tar.gz`, dossier `linux/`), **macOS** (Swift/Cocoa + VLCKit, `.dmg` universel non signé, dossier `macos/`) et **Android**
(Kotlin + libVLC, `.apk`, dossier `android/`). Elles sont compilées et jointes à la release automatiquement par GitHub Actions ;
la page du projet propose un bouton de téléchargement par système.

## ✨ Caractéristiques

- **Vidéo intégrée** : libVLC dessine l'image dans la fenêtre (Direct3D 11, décodage matériel).
- **Liste des chaînes automatique** : lue depuis `lineup.m3u` (noms et numéros du tuner), affichable / masquable.
- **Zapping instantané** : ← → , molette, saisie du numéro au clavier ; les appuis rapides sont regroupés.
- **HDHomeRun** (Paramètres › Source) : modèle, nombre de tuners, version du firmware et **mise à jour du firmware disponible**
  (signalée aussi au démarrage) ; bouton **Rechercher les chaînes** (scan du tuner avec progression, liste rechargée à la fin) et
  accès à la page web du tuner.
- **Mises à jour de HomeRun Player** : au démarrage (puis toutes les 12 h), l'appli compare sa version à la dernière release
  GitHub ; si elle est plus récente, un **bandeau** apparaît en haut de la fenêtre avec **Télécharger** (zip) et **Nouveautés**.
  ✕ masque le bandeau pour cette version ; désactivable et « Vérifier maintenant » dans Paramètres › Interface.
- **Langues et sous-titres** (bouton ♫ Langue, ou clic droit sur l'image) : pistes audio de la chaîne (français, version originale,
  audiodescription, autres langues) et sous-titres (dont malentendants). V = piste audio suivante, U = sous-titres suivants.
  Le choix est mémorisé et réappliqué sur les chaînes qui proposent la même piste (ex. : toujours la VO quand elle existe).
- **Image dans l'image (PIP)** (bouton ◲ PIP ou touche X) : une seconde chaîne du tuner dans un encart, dans le coin choisi :
  - **clic sur une image = son de cette image** (liseré orange sur l'encart quand il a le son ; Tab bascule) ;
  - le zapping, la saisie du numéro et la liste agissent sur l'image qui a le son ; double-clic sur l'encart (ou W) pour échanger les deux chaînes ;
  - dans la liste, badges « TV » (image principale) et « PIP », ♪ sur celle qui a le son ; clic droit sur une chaîne › *Regarder en PIP* ;
  - position (4 coins) et taille (15 à 50 % de la largeur) dans Paramètres › Interface ou par clic droit sur l'encart ; repris au démarrage ;
  - utilise un second tuner ; disponible avec le moteur VLC.
- **Pause en différé** (option, ⚙ Paramètres › Lecture) : la pause garde le direct en mémoire ; à la reprise, l'émission
  continue là où on l'avait arrêtée, sans en perdre le fil :
  - un **compteur** en haut à droite de l'image (et dans la barre d'état) indique le retard sur le direct :
    « En pause · différé −05:12 », puis « Différé −05:12 · Direct ▶ » ;
  - **retour au direct** : touche D, clic sur le compteur, clic droit › *Retour au direct*, ou changement de chaîne ;
  - différé **limité à 30 min** : au-delà, la lecture reprend toute seule ;
  - la mémoire est un fichier temporaire (`%TEMP%\HomeRunPlayer-timeshift`) qui ne contient que ce qui n'a pas encore été
    regardé (1 à 4 Go au plus pour 30 min selon la chaîne), effacé au fur et à mesure et à la fermeture ;
  - fonctionnement : l'appli lit le flux du tuner en continu et le sert à libVLC par un petit serveur local (127.0.0.1) ;
    un seul tuner est utilisé, comme sans l'option. Moteur VLC seulement.
- **Moteur de lecture au choix** (⚙ Paramètres › Moteur de lecture) : **VLC** (libVLC intégré, par défaut), **MPC-HC** ou **MPC-BE** :
  - MPC est lancé en mode piloté (`/slave`) et son image est incrustée dans la fenêtre, sans ses barres ; ses propres réglages ne sont pas modifiés (rendu vidéo, décodage LAV, madVR… se règlent dans MPC) ;
  - tout le reste est identique : liste, guide, HUD, enregistrement, lecture des enregistrements avec déplacement, bordure fine, tailles ;
  - le volume passe par le mélangeur Windows (100 % maximum) ; format d'image et désentrelacement se règlent dans MPC ;
  - MPC est détecté automatiquement (registre, `Program Files`, K-Lite), sinon indiquez son exécutable ; le changement de moteur relance l'appli ;
  - MPC ouvre les flux réseau plus lentement que VLC : compter environ 10 s par changement de chaîne avec MPC-BE, contre 1 à 2 s avec VLC.
- **Bouton ⚙ Paramètres** : fenêtre compacte à **onglets** (Source, Lecture, Interface, Moteur, Chaînes ; Ctrl+Tab pour passer de l'un à l'autre), toujours ajustée à l'écran, réglages **appliqués immédiatement** :
  - source : URL ou fichier de la playlist, rechargement à chaud ;
  - lecture : cache réseau, décodage matériel (Auto / D3D11VA / DXVA2 / logiciel), désentrelacement (Yadif, Yadif 2x, Bob…), format d'image, synchro audio, pause en différé ;
  - interface : premier plan, reprise de la dernière chaîne, lecture automatique, liste latérale ;
  - chaînes : renommer (F2), masquer (case à cocher), réordonner (▲ ▼ ou Alt+↑/↓), nom d'origine ;
  - moteur : dossier de VLC (détection automatique sinon).
- **Plein écran** (F, double-clic, Échap pour sortir) ; la barre de commandes réapparaît en approchant la souris du bas de l'écran.
- **Guide TV** (automatique avec un HDHomeRun, environ 24 h de programmes) :
  - fenêtre **Guide TV** en grille horaire, avec la ligne « maintenant », l'avancement de chaque émission, et les détails (épisode, durée, résumé) ; double-clic ou Entrée pour regarder ;
  - programme en cours (avec barre d'avancement) sous chaque chaîne de la liste et dans la barre d'état ;
  - émission en cours et suivante dans le HUD ;
  - source : le service guide de SiliconDust (api.hdhomerun.com), interrogé avec l'identifiant de ton tuner ; on peut le désactiver dans Paramètres.
- **Enregistrement** (bouton **● Enregistrer** ou touche R) :
  - le flux du tuner est copié tel quel dans un fichier `.ts` (aucun réencodage, qualité d'origine) ;
  - par défaut jusqu'à la fin de l'émission en cours (selon le guide, + marge réglable), ou 30 min / 1 h / 2 h / 3 h / sans limite via la flèche du bouton ;
  - on peut enregistrer une chaîne et en regarder une autre, ou plusieurs chaînes à la fois (chaque flux occupe un tuner) ; clic droit sur une chaîne → *Enregistrer* ;
  - témoins : point rouge dans la liste, « ● REC » dans la barre d'état et le HUD ; reconnexion automatique si le flux est coupé.
- **Enregistrements programmés depuis le guide TV** : clic droit sur une émission › *Programmer l'enregistrement*, touche R
  ou bouton ● Programmer. L'émission est encadrée en rouge dans la grille et apparaît en tête de la fenêtre Enregistrements
  (Supprimer = annuler). L'enregistrement démarre 1 min avant et s'arrête à la fin + la marge ; une émission en cours est
  enregistrée tout de suite.
  ⚠ **HomeRun Player doit rester ouvert** (il peut être réduit) **et l'ordinateur allumé** à l'heure prévue : rien ne se fait
  appli fermée. La mise en veille automatique est bloquée tant qu'une programmation attend ; une émission manquée est signalée
  au démarrage, et la fermeture de l'appli demande confirmation.
- **Fenêtre Enregistrements** (bouton ▤ ou touche E) : émission, chaîne, date, durée, taille, état (en cours, en lecture) ;
  - lecture dans le lecteur principal, même pendant l'enregistrement ; barre de position, pause, −10 s / +30 s (← → aussi dans la fenêtre principale), *Retour au direct* ;
  - suppression vers la Corbeille, ouverture du dossier ;
  - dossier : `Vidéos\HomeRun Player` par défaut, modifiable dans Paramètres. Si Windows le protège (*Accès contrôlé aux dossiers*), l'appli bascule d'elle-même sur un dossier autorisé (`Enregistrements\` à côté de l'exe) et le signale.
- **HUD incrusté** sur l'image : numéro et nom de la chaîne, état et résolution, heure, barre de volume (muet, >100 %). Il s'affiche au zapping, au volume, à chaque action, ou avec la touche I ; sa durée se règle dans Paramètres (0 = désactivé).
- **Bordure fine** : fenêtre sans barre de titre, liseré de 2 px ; on la déplace en glissant l'image et on la redimensionne par les bords. La barre de commandes peut se masquer et réapparaître au survol.
- **Barre de commandes adaptative** : quand la fenêtre est étroite (petite taille, bordure fine), les boutons passent en icônes plutôt que de disparaître ; Taille, Enregistrer, Paramètres et le zapping restent toujours accessibles.
- **Bouton ▣ Taille** : règle l'image sur une taille prédéfinie (par défaut 1080p, 720p, 480p, 240p, ou `1280x720`…, à définir dans Paramètres). Un clic passe à la suivante, la flèche ouvre la liste.
- **Bouton Premier plan / Arrière-plan** : garder la fenêtre au-dessus des autres, ou non.
- **Reconnexion automatique** si le flux est coupé.
- **6 langues** : français, anglais, espagnol, allemand, italien, portugais (Paramètres › Interface › Langue ; par défaut celle de Windows).
- **Haute résolution (DPI)** gérée par écran.
- Réglages sauvegardés dans `HomeRunPlayer.ini` (à côté de l'exe, sinon dans `%APPDATA%\HomeRunPlayer`) ; les réglages d'un ancien `VlcMaster.ini` sont repris automatiquement.

## ⌨️ Raccourcis

| Touche | Action |
|---|---|
| ← / → (ou Page préc./suiv.) | Chaîne précédente / suivante (pendant la lecture d'un enregistrement : −10 s / +30 s) |
| R | Enregistrer la chaîne / arrêter l'enregistrement |
| E | Fenêtre Enregistrements |
| 0-9 | Aller à la chaîne n° (validation auto ou Entrée) |
| ↑ / ↓, molette sur l'image | Volume |
| Espace | Pause / lecture (avec la pause en différé : l'émission reprend là où elle s'était arrêtée) |
| D | Retour au direct (pause en différé) |
| S | Stop |
| M | Muet |
| F, F11, double-clic | Plein écran |
| Échap | Quitter le plein écran |
| L | Afficher / masquer la liste des chaînes |
| P, F2 | Paramètres (Ctrl+Tab : onglet suivant) |
| I | Afficher le HUD |
| V · U | Piste audio suivante (VF, VO, audiodescription…) · sous-titres suivants |
| X · W · Tab | PIP oui / non · échanger PIP et image principale · donner le son à l'autre image |
| G | Guide TV (dans le guide : flèches, Entrée = regarder, Début = maintenant, Échap = fermer) |
| B | Bordure fine / fenêtre normale |
| T | Taille prédéfinie suivante |
| A | Premier plan / arrière-plan |
| Clic droit sur l'image | Menu (format d'image, désentrelacement…) |
| Clic droit sur une chaîne | Regarder / renommer / masquer |

## 🚀 Installation

Prérequis : **VLC 3.x 64 bits** installé (https://www.videolan.org).
L'appli trouve VLC toute seule (registre, `Program Files`, dossier de l'exe ou sous-dossier `vlc\`).
**VLC absent ?** L'image affiche un bouton **Télécharger VLC (gratuit)** (page officielle de VideoLAN) et un bouton
**Réessayer** ; une fois VLC installé, il suffit de revenir dans HomeRun Player : il est détecté automatiquement, sans redémarrer.
Le même bouton se trouve dans ⚙ Paramètres › Moteur.
Avec la version « zip » de VLC, il suffit d'indiquer son dossier dans ⚙ Paramètres → *Moteur VLC*.
Au premier lancement d'un VLC portable, le cache des plugins (`plugins\plugins.dat`) est créé :
les démarrages suivants sont quasi instantanés.

Lancement : `HomeRunPlayer.exe` (option : `HomeRunPlayer.exe <playlist ou flux>`).

## 🙏 Remerciements

- **SiliconDust**, fabricant des tuners [HDHomeRun](https://www.silicondust.com/) : des tuners réseau ouverts (flux HTTP, liste des
  chaînes en `lineup.m3u`, informations en JSON, service guide TV) qui permettent à n'importe quel programme de regarder la télé.
- **VideoLAN** et les bénévoles de [VLC](https://www.videolan.org/) pour la bibliothèque **libVLC** (LGPL 2.1+), qui décode et affiche
  chaque image de HomeRun Player : libVLC sous Windows et Linux, VLCKit sous macOS, libVLC pour Android.
- Les projets **MPC-HC** et **MPC-BE**, moteurs de lecture possibles sous Windows.

HDHomeRun est une marque de SiliconDust USA Inc. ; VLC et VideoLAN sont des marques de VideoLAN. HomeRun Player est un projet
indépendant, sans lien avec SiliconDust ni VideoLAN.

## 🛠️ Compilation

MinGW-w64 64 bits (gcc + windres), aucun SDK VLC nécessaire (libVLC est chargée dynamiquement) :

```bat
build.bat
```

Résultat : `build\HomeRunPlayer.exe` (exécutable autonome, lié statiquement).

## 📂 Structure

- `src/vlcmaster.c` : application (fenêtre, lecture, paramètres, playlist).
- `src/libvlc_dyn.h` : liaison dynamique à l'API libVLC 3.x.
- `src/guide_data.h` / `src/guide_ui.h` : guide TV (téléchargement, mini-lecteur JSON, fenêtre grille).
- `src/lang.h` / `src/lang_table.h` : traduction de l'interface (6 langues).
- `src/mpc.h` : moteurs MPC-HC / MPC-BE (API /slave, incrustation, volume par le mélangeur Windows).
- `src/record.h` : enregistrement (threads de copie du flux) et fenêtre Enregistrements.
- `res/` : icône, manifeste (thèmes, DPI) et informations de version.
- `legacy/VlcMaster.pyw` : ancienne télécommande Python/tkinter (pilotage HTTP de VLC).

By Popov ©2026
