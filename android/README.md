# HomeRun Player — Android

Version Android native (Kotlin + libVLC `org.videolan.android:libvlc-all` 3.x) du lecteur TV
pour tuners HDHomeRun. Téléphones, tablettes et Android TV (télécommande / D-pad), Android 5.0+.

## Fonctions

- **Lecture** : playlist `http://<ip>/lineup.m3u` (⚙ Paramètres, défaut `http://192.168.1.90/lineup.m3u`),
  liste des chaînes avec programme en cours, zapping (◀ ▶, glisser horizontal, D-pad haut/bas,
  CH+/CH−, chiffres), volume (barre, glisser vertical à droite, M = muet), plein écran,
  reprise de la dernière chaîne, nouvelle tentative automatique si le flux coupe.
- **Guide TV** (G) : `discover.json` → `DeviceAuth` → `api.hdhomerun.com/api/guide.php` (jusqu'à ~24 h),
  grille horaire (doigt ou D-pad) ; une case → détails, Regarder, Enregistrer.
- **Langues** (L, V = audio suivant, U = sous-titres) : VF, VO (qaa), audiodescription (qad), sous-titres DVB ;
  le choix est mémorisé et réappliqué sur les autres chaînes.
- **Enregistrement** (R) : copie du flux en `.ts` « AAAA-MM-JJ HHhMM - Chaîne - Émission.ts », jusqu'à la fin
  de l'émission + marge, 30 min…3 h ou sans limite ; service au premier plan (continue appli fermée).
  Liste (E) : arrêter, lire avec reprise de position, supprimer. Dossier :
  `Android/data/fr.popov.homerun/files/Movies` (effacé si l'appli est désinstallée).
- **PIP** (P) : seconde chaîne en encart (2ᵉ tuner) ; toucher une image lui donne le son ; inverser, déplacer.
  Bouton Accueil → PIP système Android.
- **Tuner** (⚙ › Tuner HDHomeRun) : modèle, firmware, mise à jour disponible, scan des chaînes
  (`lineup.post?scan=start`, suivi `lineup_status.json`, arrêt).
- **Mises à jour** : bandeau si la dernière release GitHub est plus récente et contient un APK.
- **6 langues** (fr, en, es, de, it, pt) ; choix dans ⚙ › Langue (ou réglages Android de l'appli).
  Les textes sont générés par `tools/gen_strings.py` (repris de `src/lang_table.h`).

Raccourcis clavier/télécommande : S = Paramètres, F = afficher/masquer l'interface, OK = interface,
D-pad gauche = liste, D-pad droite = son principal/encart.

## Compiler

Prérequis : JDK 17 ou 21, SDK Android (plateforme 36). Depuis `android/` :

```powershell
.\gradlew.bat assembleRelease
```

APK : `app/build/outputs/apk/release/app-release.apk` (signé avec `debug.keystore`, clé de test versionnée :
les APK compilés localement et par GitHub Actions s'installent les uns par-dessus les autres).
`local.properties` doit contenir `sdk.dir=C:/Android/sdk` (ou définir `ANDROID_HOME`).

Si Gradle échoue avec « Unable to establish loopback connection » (Windows), définir avant :
`$env:JAVA_TOOL_OPTIONS='-Djdk.net.unixdomain.tmpdir=C:/Android/tmp'`.

GitHub Actions : `.github/workflows/android.yml` compile l'APK et le publie comme artefact
`HomeRunPlayer-android`.

## Installer

Copier l'APK sur l'appareil et l'ouvrir (autoriser « sources inconnues »), ou :

```powershell
adb install -r app\build\outputs\apk\release\app-release.apk
```

## Tests

- `tools/fake_tuner.py` : faux HDHomeRun local (scan simulé) ; dans l'émulateur, playlist
  `http://10.0.2.2:8090/lineup.m3u`.
- Émulateur : le décodage matériel donne une image noire (limite de l'émulateur) — désactiver
  « Décodage matériel » dans ⚙ pour tester.

## Reste à faire / non vérifié

- Scan réel non lancé (volontairement) ; scan simulé non encore exercé jusqu'au bout dans l'émulateur.
- Non testé sur Android TV ni sur un vrai appareil (décodage matériel).
- Sur téléphone en paysage, la barre de commandes défile horizontalement.
- Pas de programmation d'enregistrements futurs ; pas de renommage/masquage de chaînes.
- APK universel (~100 Mo, toutes architectures) ; des APK par ABI réduiraient la taille.
