# HomeRun Player — macOS

Version macOS native (Swift + AppKit + VLCKit 3.7) du lecteur TV pour tuners HDHomeRun.
macOS 11 Big Sur ou plus récent, Mac Apple Silicon et Intel (appli universelle arm64 + x86_64).

## Fonctions

- **Lecture** : playlist `http://<ip>/lineup.m3u` (⚙ Paramètres, par défaut `http://192.168.1.90/lineup.m3u`),
  liste des chaînes avec le programme en cours, zapping (◀ ▶, ↑ ↓, Page préc./suiv., numéro au clavier),
  volume (barre, ← →, + −, M = muet), plein écran (F, double-clic, Échap pour sortir ; la barre réapparaît
  quand la souris bouge), reprise de la dernière chaîne, nouvelle tentative automatique si le flux coupe
  (5 essais, puis message « tous les tuners sont peut-être occupés »).
- **Guide TV** (G) : `discover.json` → `DeviceAuth` → `api.hdhomerun.com/api/guide.php`, pages enchaînées
  avec `&Start=` jusqu'à ~24 h ; grille horaire (colonne des chaînes et heures fixes) ; une case → détails,
  Regarder, Enregistrer (émission en cours) ou Programmer l'enregistrement (émission à venir).
- **Langues** (L = menu, V = audio suivant, U = sous-titres suivants) : VF, VO (`qaa`), audiodescription
  (`qad`), sous-titres DVB ; le choix est mémorisé et réappliqué sur les autres chaînes.
- **Enregistrement** (R) : copie du flux en `.ts` « AAAA-MM-JJ HHhMM - Chaîne - Émission.ts », jusqu'à la fin
  de l'émission + marge, 30 min…3 h ou sans limite ; nouvelles tentatives si le flux coupe ; la mise en veille
  est empêchée pendant l'enregistrement. Liste (E) : arrêter, lire avec reprise de la position, lire depuis le
  début, mettre à la corbeille (récupérable), ouvrir le dossier. Dossier par défaut : `~/Movies/HomeRun Player`
  (modifiable dans ⚙). Les enregistrements programmés démarrent si l'appli est ouverte à l'heure prévue.
- **PIP** (P) : seconde chaîne en encart (2ᵉ tuner) ; cliquer une image lui donne le son ; inverser
  (double-clic sur l'encart), déplacer, fermer. « Fenêtre flottante » : l'image principale passe dans une
  petite fenêtre toujours au-dessus, sur tous les espaces (voir limites).
- **Tuner** (T, ou ⚙ › Tuner HDHomeRun) : nom, modèle, firmware, mise à jour disponible (à installer depuis la
  page du tuner), scan des chaînes (`lineup.post?scan=start`, suivi `lineup_status.json`, arrêt) ; la liste des
  chaînes est rechargée à la fin du scan.
- **Mises à jour** : bandeau si la dernière release GitHub (`Popov2026/VlcMaster-V2`) est plus récente **et**
  contient un `.dmg` ; « Télécharger » ouvre le navigateur, « Ignorer » masque cette version.
- **6 langues** (fr, en, es, de, it, pt) : langue du système, ou forcée dans ⚙ (au prochain démarrage).
  Textes générés par `tools/gen_strings.py` (table reprise de `src/lang_table.h`, comme la version Android).

Raccourcis avec ⌘ dans le menu « Télévision » (⌘G guide, ⌘E enregistrements, ⌘R enregistrer, ⌘L langue,
⌃⌘F plein écran, ⌥⌘P fenêtre flottante, ⇧⌘T tuner, ⇧⌘R recharger la playlist, ⌘, paramètres).

## Fichiers

| Fichier | Rôle | Équivalent Windows |
|---|---|---|
| `Sources/Core/*.swift` | cœur sans interface : HTTP, playlist, guide, tuner/scan, mises à jour, noms de fichiers, pistes | `guide_data.h`, `tuner.h`, `update.h`, `record.h`, `tracks.h` |
| `Sources/App/MainWindow.swift` | lecture, liste, zapping, volume, PIP, bandeau, clavier | `vlcmaster.c`, `pip.h` |
| `Sources/App/GuideWindow.swift` | grille du guide | `guide_ui.h` |
| `Sources/App/Recorder.swift` | enregistrements et programmation | `record.h`, `sched.h` |
| `Sources/App/RecordingsWindow.swift` | liste et lecteur des enregistrements | `record.h` |
| `Sources/App/TunerWindow.swift`, `SettingsWindow.swift`, `TracksMenu.swift`, `TVPlayer.swift`, `AppDelegate.swift`, `L10n.swift` | tuner, paramètres, langues, lecteur VLCKit, menus, textes et réglages | |
| `Resources/*.lproj/Localizable.strings` | textes (générés) | `lang_table.h` |
| `project.yml` | projet Xcode (XcodeGen) | `build.bat` |
| `Package.swift`, `Tests/` | tests du cœur (`swift test`) | |
| `tools/` | `fetch_vlckit.sh`, `make_icon.sh`, `gen_strings.py`, `fake_tuner.py` | |

## Compiler

**GitHub Actions** : `.github/workflows/macos.yml` (runner `macos-15`) lance les tests du cœur contre le faux
tuner, compile l'appli universelle et publie l'artefact `HomeRunPlayer-macos` (`HomeRunPlayer-<version>-macos.dmg`).
Lancement manuel possible (« Run workflow ») avec un numéro de version.

**Sur un Mac** (Xcode 15+ et `brew install xcodegen`), depuis `macos/` :

```bash
./build.sh            # ou ./build.sh 3.7
```

Le script télécharge VLCKit 3.7.3 (archive officielle VideoLAN, SHA-256 vérifié) dans `Vendor/`, crée l'icône
depuis `res/app.ico`, génère `HomeRunPlayer.xcodeproj`, compile avec `xcodebuild`, signe ad hoc et crée
`dist/HomeRunPlayer-<version>-macos.dmg`. Pour travailler dans Xcode : `tools/fetch_vlckit.sh && xcodegen generate`,
puis ouvrir `HomeRunPlayer.xcodeproj`.

Après modification de la table des textes : `python3 tools/gen_strings.py`.

## Installer

Ouvrir le `.dmg` et glisser *HomeRun Player* dans *Applications*. L'appli n'est **pas signée par Apple**
(signature ad hoc seulement) : au premier lancement, macOS la bloque. Ouvrir alors
*Réglages Système › Confidentialité et sécurité* › « Ouvrir quand même » (macOS 15), ou clic droit › Ouvrir
(macOS 14 et avant). En Terminal : `xattr -dr com.apple.quarantine "/Applications/HomeRun Player.app"`.
macOS demande aussi l'accès au **réseau local** (nécessaire pour joindre le tuner).

## Tests

- `swift test` : playlist M3U, fusion des pages du guide, comparaison de versions, noms de fichiers,
  clés de pistes. Avec `FAKE_TUNER=http://127.0.0.1:8090` (faux tuner lancé par
  `python3 tools/fake_tuner.py 8090`) : infos du tuner, scan complet, arrêt du scan, guide paginé.
  Le test refuse toute adresse autre que `127.0.0.1` / `localhost` : jamais de scan sur un vrai tuner.
- `tools/fake_tuner.py` est une copie de celui de la version Android, avec en plus `/fake_guide.php`
  (imitation du service guide).

## Limites et points non vérifiés

Développé **sans Mac** : seule la CI compile et teste. Rien n'a été lancé sur un vrai Mac.

- Non testés (aucun Mac) : l'interface elle-même, la lecture VLCKit (image, son, décodage matériel
  VideoToolbox, désentrelacement), le plein écran, le clavier, le PIP, la fenêtre flottante, la lecture des
  enregistrements et la reprise, la mise en veille empêchée, la demande d'accès au réseau local,
  l'installation du `.dmg` non signé.
- Testé en CI (après le premier push) : compilation universelle, logique du cœur, logique du scan et du guide
  contre le faux tuner. Scan réel volontairement jamais lancé.
- **PIP système** : VLCKit 3 ne permet pas le PIP natif de macOS (AVKit) ; remplacé par une fenêtre
  flottante toujours au-dessus. Le PIP natif demanderait VLCKit 4 (encore en alpha).
- Enregistrements programmés : seulement si l'appli est ouverte (pas d'agent en arrière-plan).
- La mise à jour du firmware du tuner se fait depuis sa page web (bouton « Page du tuner »), comme sous Android.
- Appli non signée/notarisée par Apple : avertissement Gatekeeper au premier lancement.
