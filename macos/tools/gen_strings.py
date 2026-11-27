#!/usr/bin/env python3
"""Génère Resources/<langue>.lproj/Localizable.strings (6 langues) pour la version macOS.

Ordre des colonnes : fr, en, es, de, it, pt (comme src/lang_table.h de la version Windows,
dont les textes sont repris ; même table que android/tools/gen_strings.py, plus les textes
propres au Mac dans MAC ci-dessous).

Les formats Android « %s / %1$s / %d » deviennent « %@ / %1$@ / %ld » (String(format:) en Swift).

Usage : python tools/gen_strings.py   (depuis le dossier macos/)
"""
import os
import re

LANGS = ["fr", "en", "es", "de", "it", "pt"]

S = {
"app_name": ["HomeRun Player"] * 6,
"channels": ["Chaînes", "Channels", "Canales", "Sender", "Canali", "Canais"],
"btn_list": ["☰ Chaînes", "☰ Channels", "☰ Canales", "☰ Sender", "☰ Canali", "☰ Canais"],
"btn_prev": ["◀", "◀", "◀", "◀", "◀", "◀"],
"btn_next": ["▶", "▶", "▶", "▶", "▶", "▶"],
"btn_sound": ["🔊", "🔊", "🔊", "🔊", "🔊", "🔊"],
"btn_muted": ["🔇", "🔇", "🔇", "🔇", "🔇", "🔇"],
"btn_guide": ["Guide TV", "TV guide", "Guía de TV", "TV-Programm", "Guida TV", "Guia de TV"],
"btn_lang": ["♫ Langue", "♫ Language", "♫ Idioma", "♫ Sprache", "♫ Lingua", "♫ Idioma"],
"btn_rec": ["● Enreg.", "● Rec", "● Grabar", "● Aufn.", "● Reg.", "● Gravar"],
"btn_pip": ["PIP", "PiP", "PiP", "PiP", "PiP", "PiP"],
"btn_recs": ["Enregistrements", "Recordings", "Grabaciones", "Aufnahmen", "Registrazioni", "Gravações"],
"btn_full": ["⛶ Plein écran", "⛶ Full screen", "⛶ Pantalla completa", "⛶ Vollbild", "⛶ Schermo intero", "⛶ Ecrã inteiro"],
"btn_settings": ["⚙ Paramètres", "⚙ Settings", "⚙ Ajustes", "⚙ Einstellungen", "⚙ Impostazioni", "⚙ Definições"],
"btn_play": ["▶ Lecture", "▶ Play", "▶ Reproducir", "▶ Wiedergabe", "▶ Riproduci", "▶ Reproduzir"],
"btn_pause": ["❚❚ Pause", "❚❚ Pause", "❚❚ Pausa", "❚❚ Pause", "❚❚ Pausa", "❚❚ Pausa"],
"btn_back30": ["« 30 s", "« 30 s", "« 30 s", "« 30 s", "« 30 s", "« 30 s"],
"btn_fwd30": ["30 s »", "30 s »", "30 s »", "30 s »", "30 s »", "30 s »"],
"settings": ["Paramètres", "Settings", "Ajustes", "Einstellungen", "Impostazioni", "Definições"],

# ---- lecture, état
"opening": ["Ouverture de « %s »…", "Opening “%s”…", "Abriendo «%s»…", "„%s“ wird geöffnet…", "Apertura di «%s»…", "A abrir «%s»…"],
"loading_playlist": ["Chargement de la playlist…", "Loading the playlist…", "Cargando la lista…", "Playlist wird geladen…", "Caricamento della playlist…", "A carregar a lista…"],
"playlist_empty": ["La playlist ne contient aucune chaîne", "The playlist contains no channels", "La lista no contiene ningún canal", "Die Playlist enthält keine Sender", "La playlist non contiene canali", "A lista não contém canais"],
"playlist_error": ["%s\n\nVérifiez l'adresse de la playlist dans ⚙ Paramètres.", "%s\n\nCheck the playlist address in ⚙ Settings.", "%s\n\nCompruebe la dirección de la lista en ⚙ Ajustes.", "%s\n\nPrüfen Sie die Playlist-Adresse unter ⚙ Einstellungen.", "%s\n\nVerificate l'indirizzo della playlist in ⚙ Impostazioni.", "%s\n\nVerifique o endereço da lista em ⚙ Definições."],
"retrying": ["Flux interrompu — nouvelle tentative (%d)…", "Stream interrupted — retrying (%d)…", "Flujo interrumpido — reintentando (%d)…", "Stream unterbrochen — neuer Versuch (%d)…", "Flusso interrotto — nuovo tentativo (%d)…", "Fluxo interrompido — nova tentativa (%d)…"],
"cannot_play": ["Impossible de lire « %s ».\nTous les tuners sont peut-être occupés.", "Cannot play “%s”.\nAll tuners may be busy.", "No se puede reproducir «%s».\nQuizá todos los sintonizadores estén ocupados.", "„%s“ kann nicht wiedergegeben werden.\nMöglicherweise sind alle Tuner belegt.", "Impossibile riprodurre «%s».\nForse tutti i sintonizzatori sono occupati.", "Não é possível reproduzir «%s».\nTalvez todos os sintonizadores estejam ocupados."],
"volume_pct": ["Volume %d %%", "Volume %d %%", "Volumen %d %%", "Lautstärke %d %%", "Volume %d %%", "Volume %d %%"],
"muted": ["Son coupé", "Sound muted", "Sonido silenciado", "Ton aus", "Audio disattivato", "Som cortado"],
"unmuted": ["Son rétabli", "Sound restored", "Sonido restablecido", "Ton an", "Audio riattivato", "Som reposto"],
"next_prog": ["Ensuite %1$s  %2$s", "Next %1$s  %2$s", "Después %1$s  %2$s", "Danach %1$s  %2$s", "Dopo %1$s  %2$s", "A seguir %1$s  %2$s"],
"no_channel_num": ["Pas de chaîne %s", "No channel %s", "No hay canal %s", "Kein Sender %s", "Nessun canale %s", "Sem canal %s"],
"firmware_available": ["HDHomeRun : mise à jour du firmware disponible (%s)", "HDHomeRun: firmware update available (%s)", "HDHomeRun: actualización de firmware disponible (%s)", "HDHomeRun: Firmware-Update verfügbar (%s)", "HDHomeRun: aggiornamento firmware disponibile (%s)", "HDHomeRun: atualização de firmware disponível (%s)"],
"ctx_play": ["Regarder", "Watch", "Ver", "Ansehen", "Guarda", "Ver"],
"ctx_pip": ["Dans l'encart (PIP)", "In the inset (PiP)", "En el recuadro (PiP)", "Im Bild-im-Bild (PiP)", "Nel riquadro (PiP)", "No recorte (PiP)"],
"ctx_rec": ["Enregistrer…", "Record…", "Grabar…", "Aufnehmen…", "Registra…", "Gravar…"],

# ---- PIP
"pip_title": ["Image dans l'image (PIP)", "Picture-in-picture (PiP)", "Imagen en imagen (PiP)", "Bild im Bild (PiP)", "Picture-in-picture (PiP)", "Imagem na imagem (PiP)"],
"pip_choose": ["Chaîne de l'encart", "Inset channel", "Canal del recuadro", "Sender im Bild-im-Bild", "Canale del riquadro", "Canal do recorte"],
"pip_change": ["Changer de chaîne", "Change channel", "Cambiar de canal", "Sender wechseln", "Cambia canale", "Mudar de canal"],
"pip_swap": ["Inverser les images", "Swap pictures", "Intercambiar imágenes", "Bilder tauschen", "Inverti le immagini", "Trocar as imagens"],
"pip_sound_to_pip": ["Son de l'encart", "Sound from the inset", "Sonido del recuadro", "Ton vom Bild-im-Bild", "Audio del riquadro", "Som do recorte"],
"pip_sound_to_main": ["Son de l'image principale", "Sound from the main picture", "Sonido de la imagen principal", "Ton vom Hauptbild", "Audio dell'immagine principale", "Som da imagem principal"],
"pip_move": ["Déplacer l'encart", "Move the inset", "Mover el recuadro", "Bild-im-Bild verschieben", "Sposta il riquadro", "Mover o recorte"],
"pip_close": ["Fermer l'encart", "Close the inset", "Cerrar el recuadro", "Bild-im-Bild schließen", "Chiudi il riquadro", "Fechar o recorte"],
"pip_sound_pip": ["Son : encart", "Sound: inset", "Sonido: recuadro", "Ton: Bild-im-Bild", "Audio: riquadro", "Som: recorte"],
"pip_sound_main": ["Son : image principale", "Sound: main picture", "Sonido: imagen principal", "Ton: Hauptbild", "Audio: immagine principale", "Som: imagem principal"],

# ---- langues
"lang_title": ["Langue et sous-titres", "Language and subtitles", "Idioma y subtítulos", "Sprache und Untertitel", "Lingua e sottotitoli", "Idioma e legendas"],
"trk_audio_item": ["Langue audio : %s", "Audio language: %s", "Idioma de audio: %s", "Audiosprache: %s", "Lingua audio: %s", "Idioma do áudio: %s"],
"trk_spu_item": ["Sous-titres : %s", "Subtitles: %s", "Subtítulos: %s", "Untertitel: %s", "Sottotitoli: %s", "Legendas: %s"],
"trk_spu_off": ["Désactivés", "Off", "Desactivados", "Aus", "Disattivati", "Desativadas"],
"trk_none": ["Aucune piste audio ni sous-titres sur cette chaîne", "No audio tracks or subtitles on this channel", "Sin pistas de audio ni subtítulos en este canal", "Keine Tonspuren oder Untertitel auf diesem Sender", "Nessuna traccia audio né sottotitoli su questo canale", "Sem faixas de áudio nem legendas neste canal"],
"trk_qaa": ["Version originale", "Original version", "Versión original", "Originalfassung", "Versione originale", "Versão original"],
"trk_qad": ["Audiodescription", "Audio description", "Audiodescripción", "Audiodeskription", "Audiodescrizione", "Audiodescrição"],
"trk_qtz": ["Version multilingue", "Multilingual version", "Versión multilingüe", "Mehrsprachige Fassung", "Versione multilingue", "Versão multilingue"],
"trk_mis": ["Non codée", "Uncoded", "Sin codificar", "Nicht codiert", "Non codificata", "Não codificada"],
"trk_und": ["Indéterminée", "Undetermined", "Indeterminado", "Unbestimmt", "Indeterminata", "Indeterminado"],
"trk_ad": ["audiodescription", "audio description", "audiodescripción", "Audiodeskription", "audiodescrizione", "audiodescrição"],
"trk_hoh": ["malentendants", "hard of hearing", "personas con discapacidad auditiva", "Hörgeschädigte", "non udenti", "deficientes auditivos"],

# ---- guide
"guide_title": ["Guide TV", "TV guide", "Guía de TV", "TV-Programm", "Guida TV", "Guia de TV"],
"guide_loading": ["Guide TV : chargement…", "TV guide: loading…", "Guía de TV: cargando…", "TV-Programm: wird geladen…", "Guida TV: caricamento…", "Guia de TV: a carregar…"],
"guide_refresh": ["Actualiser", "Refresh", "Actualizar", "Aktualisieren", "Aggiorna", "Atualizar"],
"guide_watch": ["Regarder", "Watch", "Ver", "Ansehen", "Guarda", "Ver"],
"guide_record": ["Enregistrer", "Record", "Grabar", "Aufnehmen", "Registra", "Gravar"],
"guide_no_data": ["Pas de programme", "No programme", "Sin programa", "Kein Programm", "Nessun programma", "Sem programa"],
"guide_err_no_tuner": ["Guide TV indisponible : aucun HDHomeRun détecté (%s)", "TV guide unavailable: no HDHomeRun found (%s)", "Guía de TV no disponible: no se ha detectado ningún HDHomeRun (%s)", "TV-Programm nicht verfügbar: kein HDHomeRun gefunden (%s)", "Guida TV non disponibile: nessun HDHomeRun rilevato (%s)", "Guia de TV indisponível: nenhum HDHomeRun detetado (%s)"],
"guide_err_service": ["Guide TV indisponible : %s", "TV guide unavailable: %s", "Guía de TV no disponible: %s", "TV-Programm nicht verfügbar: %s", "Guida TV non disponibile: %s", "Guia de TV indisponível: %s"],
"guide_err_empty": ["Guide TV indisponible : le service guide n'a renvoyé aucun programme", "TV guide unavailable: the guide service returned no programmes", "Guía de TV no disponible: el servicio no devolvió ningún programa", "TV-Programm nicht verfügbar: der Dienst hat keine Sendungen geliefert", "Guida TV non disponibile: il servizio non ha restituito programmi", "Guia de TV indisponível: o serviço não devolveu programas"],

# ---- enregistrement
"recordings": ["Enregistrements", "Recordings", "Grabaciones", "Aufnahmen", "Registrazioni", "Gravações"],
"rec_title": ["Enregistrer %s", "Record %s", "Grabar %s", "%s aufnehmen", "Registra %s", "Gravar %s"],
"rec_until_end": ["Jusqu'à la fin de « %1$s » (%2$s)", "Until the end of “%1$s” (%2$s)", "Hasta el final de «%1$s» (%2$s)", "Bis zum Ende von „%1$s“ (%2$s)", "Fino alla fine di «%1$s» (%2$s)", "Até ao fim de «%1$s» (%2$s)"],
"rec_for": ["Enregistrer %s", "Record %s", "Grabar %s", "%s aufnehmen", "Registra %s", "Gravar %s"],
"rec_unlimited": ["Enregistrer sans limite", "Record without limit", "Grabar sin límite", "Ohne Begrenzung aufnehmen", "Registra senza limite", "Gravar sem limite"],
"rec_stop_one": ["Arrêter l'enregistrement de %s", "Stop recording %s", "Detener la grabación de %s", "Aufnahme von %s stoppen", "Ferma la registrazione di %s", "Parar a gravação de %s"],
"rec_started": ["● Enregistrement de %s (sans limite)", "● Recording %s (no limit)", "● Grabando %s (sin límite)", "● Aufnahme von %s (ohne Begrenzung)", "● Registrazione di %s (senza limite)", "● A gravar %s (sem limite)"],
"rec_started_until": ["● Enregistrement de %1$s jusqu'à %2$s", "● Recording %1$s until %2$s", "● Grabando %1$s hasta las %2$s", "● Aufnahme von %1$s bis %2$s", "● Registrazione di %1$s fino alle %2$s", "● A gravar %1$s até às %2$s"],
"rec_done": ["Enregistrement de %1$s terminé · %2$s · %3$s", "Recording of %1$s finished · %2$s · %3$s", "Grabación de %1$s terminada · %2$s · %3$s", "Aufnahme von %1$s beendet · %2$s · %3$s", "Registrazione di %1$s terminata · %2$s · %3$s", "Gravação de %1$s terminada · %2$s · %3$s"],
"rec_stopped_err": ["Enregistrement de %1$s arrêté (%2$s) · %3$s · %4$s", "Recording of %1$s stopped (%2$s) · %3$s · %4$s", "Grabación de %1$s detenida (%2$s) · %3$s · %4$s", "Aufnahme von %1$s abgebrochen (%2$s) · %3$s · %4$s", "Registrazione di %1$s interrotta (%2$s) · %3$s · %4$s", "Gravação de %1$s parada (%2$s) · %3$s · %4$s"],
"rec_failed": ["Enregistrement de %1$s impossible : %2$s", "Cannot record %1$s: %2$s", "No se puede grabar %1$s: %2$s", "Aufnahme von %1$s nicht möglich: %2$s", "Impossibile registrare %1$s: %2$s", "Não é possível gravar %1$s: %2$s"],
"rec_no_data": ["aucune donnée reçue", "no data received", "no se recibieron datos", "keine Daten empfangen", "nessun dato ricevuto", "nenhum dado recebido"],
"rec_err_no_tuner": ["aucun tuner libre", "no tuner available", "ningún sintonizador libre", "kein Tuner frei", "nessun sintonizzatore libero", "nenhum sintonizador livre"],
"rec_until": ["jusqu'à %s", "until %s", "hasta las %s", "bis %s", "fino alle %s", "até às %s"],
"rec_no_limit": ["sans limite", "no limit", "sin límite", "ohne Begrenzung", "senza limite", "sem limite"],
"rec_starting": ["Démarrage…", "Starting…", "Iniciando…", "Wird gestartet…", "Avvio…", "A iniciar…"],
"rec_stop_all": ["Tout arrêter", "Stop all", "Detener todo", "Alle stoppen", "Ferma tutto", "Parar tudo"],
"rec_stop": ["Arrêter", "Stop", "Detener", "Stoppen", "Ferma", "Parar"],
"rec_stop_confirm": ["Arrêter cet enregistrement ?", "Stop this recording?", "¿Detener esta grabación?", "Diese Aufnahme stoppen?", "Fermare questa registrazione?", "Parar esta gravação?"],
"rec_indicator": ["● ENR", "● REC", "● GRAB", "● AUFN", "● REG", "● GRAV"],
"rec_live_line": ["En cours · %1$s · %2$s", "Recording · %1$s · %2$s", "Grabando · %1$s · %2$s", "Läuft · %1$s · %2$s", "In corso · %1$s · %2$s", "A gravar · %1$s · %2$s"],
"rec_resume_at": ["reprise à %s", "resume at %s", "continuar en %s", "fortsetzen bei %s", "riprendi da %s", "retomar em %s"],
"rec_play": ["Lire", "Play", "Reproducir", "Abspielen", "Riproduci", "Reproduzir"],
"rec_play_start": ["Lire depuis le début", "Play from the start", "Reproducir desde el principio", "Von Anfang an abspielen", "Riproduci dall'inizio", "Reproduzir desde o início"],
"rec_empty": ["Aucun enregistrement", "No recordings", "Ninguna grabación", "Keine Aufnahmen", "Nessuna registrazione", "Nenhuma gravação"],
"rec_folder": ["Dossier : %s", "Folder: %s", "Carpeta: %s", "Ordner: %s", "Cartella: %s", "Pasta: %s"],
"rec_delete_title": ["Supprimer", "Delete", "Eliminar", "Löschen", "Elimina", "Eliminar"],
"rec_delete_msg": ["Supprimer « %s » ?\n\nLe fichier sera définitivement effacé.", "Delete “%s”?\n\nThe file will be permanently erased.", "¿Eliminar «%s»?\n\nEl archivo se borrará definitivamente.", "„%s“ löschen?\n\nDie Datei wird endgültig gelöscht.", "Eliminare «%s»?\n\nIl file sarà cancellato definitivamente.", "Eliminar «%s»?\n\nO ficheiro será apagado definitivamente."],
"delete": ["Supprimer", "Delete", "Eliminar", "Löschen", "Elimina", "Eliminar"],
"notif_channel_rec": ["Enregistrements", "Recordings", "Grabaciones", "Aufnahmen", "Registrazioni", "Gravações"],

# ---- tuner
"tuner_title": ["Tuner HDHomeRun", "HDHomeRun tuner", "Sintonizador HDHomeRun", "HDHomeRun-Tuner", "Sintonizzatore HDHomeRun", "Sintonizador HDHomeRun"],
"tuner_loading": ["Lecture de %s…", "Reading %s…", "Leyendo %s…", "%s wird gelesen…", "Lettura di %s…", "A ler %s…"],
"tuner_unreachable": ["HDHomeRun injoignable (%1$s) : %2$s", "HDHomeRun unreachable (%1$s): %2$s", "HDHomeRun inaccesible (%1$s): %2$s", "HDHomeRun nicht erreichbar (%1$s): %2$s", "HDHomeRun irraggiungibile (%1$s): %2$s", "HDHomeRun inacessível (%1$s): %2$s"],
"tuner_name": ["Nom", "Name", "Nombre", "Name", "Nome", "Nome"],
"tuner_model": ["Modèle", "Model", "Modelo", "Modell", "Modello", "Modelo"],
"tuner_id": ["Identifiant", "Device ID", "Identificador", "Geräte-ID", "Identificativo", "Identificador"],
"tuner_count": ["Tuners", "Tuners", "Sintonizadores", "Tuner", "Sintonizzatori", "Sintonizadores"],
"tuner_fw": ["Firmware", "Firmware", "Firmware", "Firmware", "Firmware", "Firmware"],
"tuner_upgrade": ["Mise à jour du firmware disponible : %s\nElle s'installe depuis la page du tuner ou l'appli HDHomeRun.", "Firmware update available: %s\nInstall it from the tuner's page or the HDHomeRun app.", "Actualización de firmware disponible: %s\nSe instala desde la página del sintonizador o la app HDHomeRun.", "Firmware-Update verfügbar: %s\nInstallation über die Seite des Tuners oder die HDHomeRun-App.", "Aggiornamento firmware disponibile: %s\nSi installa dalla pagina del sintonizzatore o dall'app HDHomeRun.", "Atualização de firmware disponível: %s\nInstala-se a partir da página do sintonizador ou da app HDHomeRun."],
"tuner_up_to_date": ["Firmware à jour", "Firmware up to date", "Firmware actualizado", "Firmware aktuell", "Firmware aggiornato", "Firmware atualizado"],
"tuner_refresh": ["Actualiser", "Refresh", "Actualizar", "Aktualisieren", "Aggiorna", "Atualizar"],
"tuner_web": ["Page du tuner", "Tuner page", "Página del sintonizador", "Tuner-Seite", "Pagina del sintonizzatore", "Página do sintonizador"],
"scan_title": ["Scan des chaînes", "Channel scan", "Búsqueda de canales", "Sendersuchlauf", "Ricerca canali", "Procura de canais"],
"scan_help": ["Recherche toutes les chaînes reçues par l'antenne. Pendant le scan, le tuner ne peut rien diffuser ; la liste des chaînes est remplacée à la fin.", "Searches for all channels received by the antenna. The tuner cannot stream during the scan; the channel list is replaced at the end.", "Busca todos los canales recibidos por la antena. Durante la búsqueda el sintonizador no puede emitir; la lista se sustituye al final.", "Sucht alle über die Antenne empfangenen Sender. Während des Suchlaufs kann der Tuner nichts streamen; die Senderliste wird am Ende ersetzt.", "Cerca tutti i canali ricevuti dall'antenna. Durante la ricerca il sintonizzatore non può trasmettere; l'elenco viene sostituito alla fine.", "Procura todos os canais recebidos pela antena. Durante a procura o sintonizador não pode transmitir; a lista é substituída no fim."],
"scan_confirm": ["Lancer un scan des chaînes ? La lecture en cours et les enregistrements seront interrompus.", "Start a channel scan? Current playback and recordings will be interrupted.", "¿Iniciar una búsqueda de canales? Se interrumpirán la reproducción y las grabaciones.", "Sendersuchlauf starten? Wiedergabe und Aufnahmen werden unterbrochen.", "Avviare una ricerca canali? La riproduzione e le registrazioni saranno interrotte.", "Iniciar uma procura de canais? A reprodução e as gravações serão interrompidas."],
"scan_start": ["Lancer le scan", "Start scan", "Iniciar búsqueda", "Suchlauf starten", "Avvia ricerca", "Iniciar procura"],
"scan_abort": ["Arrêter le scan", "Stop scan", "Detener búsqueda", "Suchlauf abbrechen", "Interrompi ricerca", "Parar procura"],
"scan_progress": ["Scan : %1$d %% · %2$d chaînes trouvées", "Scan: %1$d %% · %2$d channels found", "Búsqueda: %1$d %% · %2$d canales encontrados", "Suchlauf: %1$d %% · %2$d Sender gefunden", "Ricerca: %1$d %% · %2$d canali trovati", "Procura: %1$d %% · %2$d canais encontrados"],
"scan_done": ["Scan terminé. La liste des chaînes sera rechargée.", "Scan finished. The channel list will be reloaded.", "Búsqueda terminada. La lista de canales se recargará.", "Suchlauf beendet. Die Senderliste wird neu geladen.", "Ricerca terminata. L'elenco dei canali sarà ricaricato.", "Procura terminada. A lista de canais será recarregada."],
"scan_failed": ["Le scan n'a pas pu démarrer : %s", "The scan could not start: %s", "No se pudo iniciar la búsqueda: %s", "Suchlauf konnte nicht starten: %s", "Impossibile avviare la ricerca: %s", "Não foi possível iniciar a procura: %s"],

# ---- mises à jour
"update_banner": ["Nouvelle version %s disponible — toucher pour télécharger (appui long : ignorer)", "New version %s available — tap to download (long press: ignore)", "Nueva versión %s disponible — toque para descargar (pulsación larga: ignorar)", "Neue Version %s verfügbar — tippen zum Herunterladen (lange drücken: ignorieren)", "Nuova versione %s disponibile — tocca per scaricare (pressione lunga: ignora)", "Nova versão %s disponível — toque para transferir (toque longo: ignorar)"],
"update_checking": ["Vérification…", "Checking…", "Comprobando…", "Wird geprüft…", "Verifica…", "A verificar…"],
"update_none": ["Version %1$s à jour (dernière : %2$s)", "Version %1$s is up to date (latest: %2$s)", "Versión %1$s actualizada (última: %2$s)", "Version %1$s ist aktuell (neueste: %2$s)", "Versione %1$s aggiornata (ultima: %2$s)", "Versão %1$s atualizada (última: %2$s)"],
"update_failed": ["Vérification impossible : %s", "Check failed: %s", "No se pudo comprobar: %s", "Prüfung fehlgeschlagen: %s", "Verifica non riuscita: %s", "Não foi possível verificar: %s"],
"version_current": ["Version %s", "Version %s", "Versión %s", "Version %s", "Versione %s", "Versão %s"],

# ---- paramètres
"set_cat_play": ["Lecture", "Playback", "Reproducción", "Wiedergabe", "Riproduzione", "Reprodução"],
"set_cat_guide": ["Guide TV et enregistrement", "TV guide and recording", "Guía de TV y grabación", "TV-Programm und Aufnahme", "Guida TV e registrazione", "Guia de TV e gravação"],
"set_cat_ui": ["Interface", "Interface", "Interfaz", "Oberfläche", "Interfaccia", "Interface"],
"set_cat_tuner": ["HDHomeRun et mises à jour", "HDHomeRun and updates", "HDHomeRun y actualizaciones", "HDHomeRun und Updates", "HDHomeRun e aggiornamenti", "HDHomeRun e atualizações"],
"set_playlist": ["Adresse de la playlist (lineup.m3u)", "Playlist address (lineup.m3u)", "Dirección de la lista (lineup.m3u)", "Playlist-Adresse (lineup.m3u)", "Indirizzo della playlist (lineup.m3u)", "Endereço da lista (lineup.m3u)"],
"set_playlist_help": ["Exemple : http://192.168.1.90/lineup.m3u", "Example: http://192.168.1.90/lineup.m3u", "Ejemplo: http://192.168.1.90/lineup.m3u", "Beispiel: http://192.168.1.90/lineup.m3u", "Esempio: http://192.168.1.90/lineup.m3u", "Exemplo: http://192.168.1.90/lineup.m3u"],
"set_resume": ["Reprendre la dernière chaîne au démarrage", "Resume the last channel at startup", "Reanudar el último canal al iniciar", "Beim Start den letzten Sender fortsetzen", "Riprendi l'ultimo canale all'avvio", "Retomar o último canal ao iniciar"],
"set_cache": ["Mémoire tampon réseau", "Network cache", "Caché de red", "Netzwerkpuffer", "Cache di rete", "Cache de rede"],
"set_hw": ["Décodage matériel", "Hardware decoding", "Decodificación por hardware", "Hardware-Dekodierung", "Decodifica hardware", "Descodificação por hardware"],
"set_hw_help": ["Désactivez-le si l'image est verte ou saccadée", "Turn it off if the picture is green or choppy", "Desactívela si la imagen se ve verde o a saltos", "Deaktivieren, wenn das Bild grün ist oder ruckelt", "Disattivatela se l'immagine è verde o scattosa", "Desative-a se a imagem estiver verde ou aos saltos"],
"set_deint": ["Désentrelacement", "Deinterlacing", "Desentrelazado", "Deinterlacing", "Deinterlacciamento", "Desentrelaçamento"],
"set_aspect": ["Format d'image", "Aspect ratio", "Relación de aspecto", "Seitenverhältnis", "Formato immagine", "Proporção da imagem"],
"set_guide": ["Guide TV", "TV guide", "Guía de TV", "TV-Programm", "Guida TV", "Guia de TV"],
"set_guide_help": ["Programme en cours sous chaque chaîne (service guide de SiliconDust)", "Current programme under each channel (SiliconDust guide service)", "Programa actual bajo cada canal (servicio de guía de SiliconDust)", "Aktuelle Sendung unter jedem Sender (Programmdienst von SiliconDust)", "Programma in corso sotto ogni canale (servizio guida di SiliconDust)", "Programa atual sob cada canal (serviço de guia da SiliconDust)"],
"set_margin": ["Marge après la fin de l'émission", "Margin after the end of the programme", "Margen tras el final del programa", "Puffer nach Sendungsende", "Margine dopo la fine del programma", "Margem após o fim do programa"],
"set_lang": ["Langue / Language", "Language", "Idioma", "Sprache", "Lingua", "Idioma"],
"set_tuner_help": ["Modèle, firmware, scan des chaînes", "Model, firmware, channel scan", "Modelo, firmware, búsqueda de canales", "Modell, Firmware, Sendersuchlauf", "Modello, firmware, ricerca canali", "Modelo, firmware, procura de canais"],
"set_updchk": ["Signaler les nouvelles versions", "Report new versions", "Avisar de nuevas versiones", "Neue Versionen melden", "Segnala nuove versioni", "Avisar de novas versões"],
"set_updnow": ["Vérifier maintenant", "Check now", "Comprobar ahora", "Jetzt prüfen", "Verifica ora", "Verificar agora"],
}

# ---- textes propres à la version macOS (menus, fenêtres, corbeille…)
MAC = {
"menu_about": ["À propos de HomeRun Player", "About HomeRun Player", "Acerca de HomeRun Player", "Über HomeRun Player", "Informazioni su HomeRun Player", "Acerca do HomeRun Player"],
"menu_settings": ["Paramètres…", "Settings…", "Ajustes…", "Einstellungen…", "Impostazioni…", "Definições…"],
"menu_hide": ["Masquer HomeRun Player", "Hide HomeRun Player", "Ocultar HomeRun Player", "HomeRun Player ausblenden", "Nascondi HomeRun Player", "Ocultar HomeRun Player"],
"menu_hide_others": ["Masquer les autres", "Hide Others", "Ocultar otros", "Andere ausblenden", "Nascondi altre", "Ocultar outros"],
"menu_show_all": ["Tout afficher", "Show All", "Mostrar todo", "Alle einblenden", "Mostra tutte", "Mostrar tudo"],
"menu_quit": ["Quitter HomeRun Player", "Quit HomeRun Player", "Salir de HomeRun Player", "HomeRun Player beenden", "Esci da HomeRun Player", "Sair do HomeRun Player"],
"menu_edit": ["Édition", "Edit", "Edición", "Bearbeiten", "Modifica", "Editar"],
"menu_cut": ["Couper", "Cut", "Cortar", "Ausschneiden", "Taglia", "Cortar"],
"menu_copy": ["Copier", "Copy", "Copiar", "Kopieren", "Copia", "Copiar"],
"menu_paste": ["Coller", "Paste", "Pegar", "Einsetzen", "Incolla", "Colar"],
"menu_select_all": ["Tout sélectionner", "Select All", "Seleccionar todo", "Alles auswählen", "Seleziona tutto", "Selecionar tudo"],
"menu_tv": ["Télévision", "Television", "Televisión", "Fernsehen", "Televisione", "Televisão"],
"menu_next": ["Chaîne suivante", "Next channel", "Canal siguiente", "Nächster Sender", "Canale successivo", "Canal seguinte"],
"menu_prev": ["Chaîne précédente", "Previous channel", "Canal anterior", "Vorheriger Sender", "Canale precedente", "Canal anterior"],
"menu_list": ["Afficher / masquer la liste", "Show / hide the list", "Mostrar / ocultar la lista", "Liste ein-/ausblenden", "Mostra / nascondi l'elenco", "Mostrar / ocultar a lista"],
"menu_mute": ["Couper / rétablir le son", "Mute / unmute", "Silenciar / activar sonido", "Ton aus / an", "Disattiva / riattiva audio", "Cortar / repor o som"],
"menu_vol_up": ["Monter le volume", "Volume up", "Subir volumen", "Lauter", "Alza il volume", "Aumentar o volume"],
"menu_vol_down": ["Baisser le volume", "Volume down", "Bajar volumen", "Leiser", "Abbassa il volume", "Diminuir o volume"],
"menu_audio_next": ["Piste audio suivante", "Next audio track", "Pista de audio siguiente", "Nächste Tonspur", "Traccia audio successiva", "Faixa de áudio seguinte"],
"menu_spu_next": ["Sous-titres suivants", "Next subtitles", "Subtítulos siguientes", "Nächste Untertitel", "Sottotitoli successivi", "Legendas seguintes"],
"menu_reload": ["Recharger la playlist", "Reload the playlist", "Recargar la lista", "Playlist neu laden", "Ricarica la playlist", "Recarregar a lista"],
"menu_window": ["Fenêtre", "Window", "Ventana", "Fenster", "Finestra", "Janela"],
"menu_minimize": ["Placer dans le Dock", "Minimize", "Minimizar", "Im Dock ablegen", "Contrai", "Minimizar"],
"menu_zoom": ["Réduire/agrandir", "Zoom", "Zoom", "Zoomen", "Zoom", "Zoom"],
"menu_close": ["Fermer", "Close", "Cerrar", "Schließen", "Chiudi", "Fechar"],
"menu_player": ["Lecteur", "Player", "Reproductor", "Player", "Lettore", "Leitor"],
"ok": ["OK", "OK", "Aceptar", "OK", "OK", "OK"],
"cancel": ["Annuler", "Cancel", "Cancelar", "Abbrechen", "Annulla", "Cancelar"],
"close": ["Fermer", "Close", "Cerrar", "Schließen", "Chiudi", "Fechar"],
"pip_float": ["Fenêtre flottante (PIP système)", "Floating window (system PiP)", "Ventana flotante (PiP del sistema)", "Schwebendes Fenster (System-PiP)", "Finestra mobile (PiP di sistema)", "Janela flutuante (PiP do sistema)"],
"pip_float_back": ["Remettre l'image dans la fenêtre", "Put the picture back in the window", "Devolver la imagen a la ventana", "Bild ins Fenster zurückholen", "Riporta l'immagine nella finestra", "Devolver a imagem à janela"],
"pip_none": ["Aucune chaîne dans l'encart", "No channel in the inset", "Ningún canal en el recuadro", "Kein Sender im Bild-im-Bild", "Nessun canale nel riquadro", "Nenhum canal no recorte"],
"rec_trash_msg": ["Mettre « %s » à la corbeille ?", "Move “%s” to the Trash?", "¿Mover «%s» a la papelera?", "„%s“ in den Papierkorb legen?", "Spostare «%s» nel cestino?", "Mover «%s» para o lixo?"],
"rec_trash": ["Mettre à la corbeille", "Move to Trash", "Mover a la papelera", "In den Papierkorb", "Sposta nel cestino", "Mover para o lixo"],
"rec_open_folder": ["Ouvrir le dossier", "Open folder", "Abrir carpeta", "Ordner öffnen", "Apri cartella", "Abrir pasta"],
"rec_choose_folder": ["Dossier des enregistrements…", "Recordings folder…", "Carpeta de grabaciones…", "Aufnahmeordner…", "Cartella registrazioni…", "Pasta das gravações…"],
"rec_scheduled": ["Programmé · %1$s · %2$s – %3$s", "Scheduled · %1$s · %2$s – %3$s", "Programada · %1$s · %2$s – %3$s", "Geplant · %1$s · %2$s – %3$s", "Programmata · %1$s · %2$s – %3$s", "Agendada · %1$s · %2$s – %3$s"],
"rec_scheduled_osd": ["Enregistrement programmé : %1$s à %2$s", "Recording scheduled: %1$s at %2$s", "Grabación programada: %1$s a las %2$s", "Aufnahme geplant: %1$s um %2$s", "Registrazione programmata: %1$s alle %2$s", "Gravação agendada: %1$s às %2$s"],
"rec_cancel_sched": ["Annuler la programmation", "Cancel the schedule", "Cancelar la programación", "Planung aufheben", "Annulla la programmazione", "Cancelar o agendamento"],
"rec_sched_note": ["Les enregistrements programmés démarrent si HomeRun Player est ouvert à l'heure prévue.", "Scheduled recordings start if HomeRun Player is open at the planned time.", "Las grabaciones programadas empiezan si HomeRun Player está abierto a la hora prevista.", "Geplante Aufnahmen starten, wenn HomeRun Player zur geplanten Zeit geöffnet ist.", "Le registrazioni programmate partono se HomeRun Player è aperto all'ora prevista.", "As gravações agendadas começam se o HomeRun Player estiver aberto à hora prevista."],
"rec_dur": ["Durée", "Duration", "Duración", "Dauer", "Durata", "Duração"],
"quit_recording": ["Des enregistrements sont en cours. Quitter les arrêtera.", "Recordings are in progress. Quitting will stop them.", "Hay grabaciones en curso. Salir las detendrá.", "Es laufen Aufnahmen. Beenden stoppt sie.", "Ci sono registrazioni in corso. Uscire le fermerà.", "Há gravações em curso. Sair vai pará-las."],
"quit_anyway": ["Quitter", "Quit", "Salir", "Beenden", "Esci", "Sair"],
"update_banner_mac": ["Nouvelle version %s disponible", "New version %s available", "Nueva versión %s disponible", "Neue Version %s verfügbar", "Nuova versione %s disponibile", "Nova versão %s disponível"],
"update_download": ["Télécharger", "Download", "Descargar", "Herunterladen", "Scarica", "Transferir"],
"update_ignore": ["Ignorer", "Ignore", "Ignorar", "Ignorieren", "Ignora", "Ignorar"],
"set_lang_restart": ["La langue de l'interface change au prochain démarrage.", "The interface language changes at the next start.", "El idioma de la interfaz cambia en el próximo inicio.", "Die Oberflächensprache ändert sich beim nächsten Start.", "La lingua dell'interfaccia cambia al prossimo avvio.", "O idioma da interface muda no próximo arranque."],
"set_apply": ["Appliquer", "Apply", "Aplicar", "Übernehmen", "Applica", "Aplicar"],
"set_rec_dir": ["Dossier des enregistrements", "Recordings folder", "Carpeta de grabaciones", "Aufnahmeordner", "Cartella registrazioni", "Pasta das gravações"],
"set_choose": ["Choisir…", "Choose…", "Elegir…", "Auswählen…", "Scegli…", "Escolher…"],
"set_tuner_btn": ["Tuner HDHomeRun…", "HDHomeRun tuner…", "Sintonizador HDHomeRun…", "HDHomeRun-Tuner…", "Sintonizzatore HDHomeRun…", "Sintonizador HDHomeRun…"],
"guide_now": ["Maintenant", "Now", "Ahora", "Jetzt", "Adesso", "Agora"],
"guide_record_sched": ["Programmer l'enregistrement", "Schedule the recording", "Programar la grabación", "Aufnahme planen", "Programma la registrazione", "Agendar a gravação"],
"rec_position": ["%1$s / %2$s", "%1$s / %2$s", "%1$s / %2$s", "%1$s / %2$s", "%1$s / %2$s", "%1$s / %2$s"],
}

PLURALS = {
"rec_notif_title": [
    ("%d enregistrement en cours", "%d enregistrements en cours"),
    ("%d recording in progress", "%d recordings in progress"),
    ("%d grabación en curso", "%d grabaciones en curso"),
    ("%d Aufnahme läuft", "%d Aufnahmen laufen"),
    ("%d registrazione in corso", "%d registrazioni in corso"),
    ("%d gravação em curso", "%d gravações em curso"),
],
}

ARRAYS = {
"deint_labels": [
    ["Désactivé", "Yadif", "Yadif 2x (fluide)", "Bob", "Linéaire", "Mélange"],
    ["Off", "Yadif", "Yadif 2x (smooth)", "Bob", "Linear", "Blend"],
    ["Desactivado", "Yadif", "Yadif 2x (fluido)", "Bob", "Lineal", "Mezcla"],
    ["Aus", "Yadif", "Yadif 2x (flüssig)", "Bob", "Linear", "Mischen"],
    ["Disattivato", "Yadif", "Yadif 2x (fluido)", "Bob", "Lineare", "Miscela"],
    ["Desativado", "Yadif", "Yadif 2x (fluido)", "Bob", "Linear", "Mistura"]],
"aspect_default": [["Par défaut"], ["Default"], ["Predeterminado"], ["Standard"], ["Predefinito"], ["Predefinido"]],
"lang_system": [["Langue du système"], ["System language"], ["Idioma del sistema"], ["Systemsprache"], ["Lingua di sistema"], ["Idioma do sistema"]],
}


def conv(s: str) -> str:
    """Formats Android → Foundation."""
    s = re.sub(r"%(\d+)\$s", r"%\1$@", s)
    s = re.sub(r"%(\d+)\$d", r"%\1$ld", s)
    s = s.replace("%s", "%@").replace("%d", "%ld")
    return s


def esc(s: str) -> str:
    return s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")


def main():
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Resources")
    for li, lang in enumerate(LANGS):
        d = os.path.join(root, lang + ".lproj")
        os.makedirs(d, exist_ok=True)
        out = ["/* Généré par tools/gen_strings.py : modifier la table du script, pas ce fichier. */", ""]
        for table in (S, MAC):
            for k, v in table.items():
                out.append(f'"{k}" = "{esc(conv(v[li]))}";')
        for k, v in PLURALS.items():
            one, other = v[li]
            out.append(f'"{k}_one" = "{esc(conv(one))}";')
            out.append(f'"{k}_other" = "{esc(conv(other))}";')
        for k, v in ARRAYS.items():
            for i, x in enumerate(v[li]):
                out.append(f'"{k}_{i}" = "{esc(conv(x))}";')
        # UTF-8 accepté par Xcode pour les .strings (converti à la compilation)
        with open(os.path.join(d, "Localizable.strings"), "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(out) + "\n")
    print("ok:", len(S) + len(MAC), "textes,", len(LANGS), "langues")


if __name__ == "__main__":
    main()
