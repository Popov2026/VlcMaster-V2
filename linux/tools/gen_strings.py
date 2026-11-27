#!/usr/bin/env python3
"""Génère src/strings_gen.h (textes de l'interface en 6 langues) à partir de la table ci-dessous.

Ordre des colonnes : fr, en, es, de, it, pt (comme src/lang_table.h de la version Windows et
android/tools/gen_strings.py, dont les textes sont repris ; LINUX ajoute ceux propres au bureau).
Les chaînes de format gardent les mêmes %s / %d (positions %1$s… acceptées par la glibc).

Usage : python3 tools/gen_strings.py   (depuis le dossier linux/)
"""
import os

NL = chr(10)

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
"cache_labels": [
    ["300 ms", "1 s", "2 s", "3 s", "5 s"]] * 6,
"deint_labels": [
    ["Désactivé", "Yadif", "Yadif 2x (fluide)", "Bob", "Linéaire", "Mélange"],
    ["Off", "Yadif", "Yadif 2x (smooth)", "Bob", "Linear", "Blend"],
    ["Desactivado", "Yadif", "Yadif 2x (fluido)", "Bob", "Lineal", "Mezcla"],
    ["Aus", "Yadif", "Yadif 2x (flüssig)", "Bob", "Linear", "Mischen"],
    ["Disattivato", "Yadif", "Yadif 2x (fluido)", "Bob", "Lineare", "Miscela"],
    ["Desativado", "Yadif", "Yadif 2x (fluido)", "Bob", "Linear", "Mistura"]],
"aspect_labels": [
    [d, "16:9", "4:3", "16:10", "21:9", "2.35:1"] for d in ["Par défaut", "Default", "Predeterminado", "Standard", "Predefinito", "Predefinido"]],
"margin_labels": [
    ["0 min", "5 min", "10 min", "15 min", "30 min"]] * 6,
"lang_labels": [
    [d, "Français", "English", "Español", "Deutsch", "Italiano", "Português"] for d in
    ["Langue du système", "System language", "Idioma del sistema", "Systemsprache", "Lingua di sistema", "Idioma do sistema"]],
}
ARRAY_VALUES = {
"cache_values": ["300", "1000", "2000", "3000", "5000"],
"deint_values": ["off", "yadif", "yadif2x", "bob", "linear", "blend"],
"aspect_values": ["", "16:9", "4:3", "16:10", "21:9", "235:100"],
"margin_values": ["0", "5", "10", "15", "30"],
"lang_values": ["", "fr", "en", "es", "de", "it", "pt"],
}



# ---- textes propres à la version Linux (bureau)
LINUX = {
"cancel": ["Annuler", "Cancel", "Cancelar", "Abbrechen", "Annulla", "Cancelar"],
"ok": ["OK"] * 6,
"close": ["Fermer", "Close", "Cerrar", "Schließen", "Chiudi", "Fechar"],
"quit": ["Quitter", "Quit", "Salir", "Beenden", "Esci", "Sair"],
"quit_rec": ["Des enregistrements sont en cours. Quitter les arrête.", "Recordings are in progress. Quitting will stop them.", "Hay grabaciones en curso. Salir las detendrá.", "Es laufen Aufnahmen. Beim Beenden werden sie gestoppt.", "Ci sono registrazioni in corso. Uscire le interromperà.", "Há gravações em curso. Sair vai pará-las."],
"no_playlist": ["Aucune playlist.\nIndiquez l'adresse de votre lineup.m3u dans ⚙ Paramètres.", "No playlist.\nEnter the address of your lineup.m3u in ⚙ Settings.", "Sin lista.\nIndique la dirección de su lineup.m3u en ⚙ Ajustes.", "Keine Playlist.\nGeben Sie die Adresse Ihrer lineup.m3u unter ⚙ Einstellungen an.", "Nessuna playlist.\nIndicate l'indirizzo del vostro lineup.m3u in ⚙ Impostazioni.", "Sem lista.\nIndique o endereço do seu lineup.m3u em ⚙ Definições."],
"choose_channel": ["Choisissez une chaîne dans la liste\n(ou ← → pour zapper).", "Choose a channel from the list\n(or ← → to zap).", "Elija un canal de la lista\n(o ← → para cambiar).", "Wählen Sie einen Sender aus der Liste\n(oder ← → zum Umschalten).", "Scegliete un canale dall'elenco\n(o ← → per cambiare).", "Escolha um canal da lista\n(ou ← → para mudar)."],
"channels_count": ["%d chaînes", "%d channels", "%d canales", "%d Sender", "%d canali", "%d canais"],
"vlc_failed": ["libVLC introuvable ou incomplet.\nInstallez les paquets libvlc5 et vlc-plugin-base (ou VLC 3.x).", "libVLC missing or incomplete.\nInstall the libvlc5 and vlc-plugin-base packages (or VLC 3.x).", "libVLC no encontrado o incompleto.\nInstale los paquetes libvlc5 y vlc-plugin-base (o VLC 3.x).", "libVLC fehlt oder ist unvollständig.\nInstallieren Sie die Pakete libvlc5 und vlc-plugin-base (oder VLC 3.x).", "libVLC mancante o incompleto.\nInstallate i pacchetti libvlc5 e vlc-plugin-base (o VLC 3.x).", "libVLC em falta ou incompleto.\nInstale os pacotes libvlc5 e vlc-plugin-base (ou o VLC 3.x)."],
"update_banner_desk": ["Nouvelle version %s disponible", "New version %s available", "Nueva versión %s disponible", "Neue Version %s verfügbar", "Nuova versione %s disponibile", "Nova versão %s disponível"],
"update_download": ["Télécharger", "Download", "Descargar", "Herunterladen", "Scarica", "Transferir"],
"update_notes": ["Nouveautés", "What's new", "Novedades", "Neuigkeiten", "Novità", "Novidades"],
"update_ignore": ["Ne plus signaler cette version", "Don't report this version again", "No volver a avisar de esta versión", "Diese Version nicht mehr melden", "Non segnalare più questa versione", "Não voltar a avisar desta versão"],
"set_rec_dir": ["Dossier des enregistrements", "Recordings folder", "Carpeta de grabaciones", "Aufnahmeordner", "Cartella delle registrazioni", "Pasta das gravações"],
"choose": ["Choisir…", "Choose…", "Elegir…", "Auswählen…", "Scegli…", "Escolher…"],
"set_lang_restart": ["La nouvelle langue sera appliquée au prochain démarrage.", "The new language will be applied at the next start.", "El nuevo idioma se aplicará en el próximo inicio.", "Die neue Sprache wird beim nächsten Start übernommen.", "La nuova lingua sarà applicata al prossimo avvio.", "O novo idioma será aplicado no próximo arranque."],
"rec_open_folder": ["Ouvrir le dossier", "Open folder", "Abrir carpeta", "Ordner öffnen", "Apri cartella", "Abrir pasta"],
"rec_dir_error": ["Impossible de créer le dossier %s", "Cannot create folder %s", "No se puede crear la carpeta %s", "Ordner %s kann nicht erstellt werden", "Impossibile creare la cartella %s", "Não é possível criar a pasta %s"],
"guide_now": ["Maintenant", "Now", "Ahora", "Jetzt", "Ora", "Agora"],
"keys_title": ["Raccourcis clavier", "Keyboard shortcuts", "Atajos de teclado", "Tastenkürzel", "Scorciatoie da tastiera", "Atalhos de teclado"],
"keys_help": [
 "← → ou Page ↑ ↓ : chaîne précédente / suivante\n0–9 : numéro de chaîne    Retour arrière : chaîne d'avant\n↑ ↓ ou molette : volume    M : muet\nF11, F ou double-clic : plein écran    Échap : sortir du plein écran\nH : masquer / afficher l'interface    I : informations\nG : guide TV    E : enregistrements    R : enregistrer\nL : langue    V : piste audio suivante    U : sous-titres\nP : image dans l'image    Tab : son de l'image principale / de l'encart\nS : paramètres    T : tuner HDHomeRun",
 "← → or Page ↑ ↓: previous / next channel\n0–9: channel number    Backspace: last channel\n↑ ↓ or mouse wheel: volume    M: mute\nF11, F or double-click: full screen    Esc: leave full screen\nH: hide / show the interface    I: information\nG: TV guide    E: recordings    R: record\nL: language    V: next audio track    U: subtitles\nP: picture-in-picture    Tab: sound from main picture / inset\nS: settings    T: HDHomeRun tuner",
 "← → o Re Pág / Av Pág: canal anterior / siguiente\n0–9: número de canal    Retroceso: último canal\n↑ ↓ o rueda: volumen    M: silencio\nF11, F o doble clic: pantalla completa    Esc: salir de pantalla completa\nH: ocultar / mostrar la interfaz    I: información\nG: guía de TV    E: grabaciones    R: grabar\nL: idioma    V: pista de audio siguiente    U: subtítulos\nP: imagen en imagen    Tab: sonido de la imagen principal / del recuadro\nS: ajustes    T: sintonizador HDHomeRun",
 "← → oder Bild ↑ ↓: vorheriger / nächster Sender\n0–9: Sendernummer    Rücktaste: letzter Sender\n↑ ↓ oder Mausrad: Lautstärke    M: stumm\nF11, F oder Doppelklick: Vollbild    Esc: Vollbild verlassen\nH: Oberfläche aus- / einblenden    I: Informationen\nG: TV-Programm    E: Aufnahmen    R: aufnehmen\nL: Sprache    V: nächste Tonspur    U: Untertitel\nP: Bild im Bild    Tab: Ton vom Hauptbild / Bild-im-Bild\nS: Einstellungen    T: HDHomeRun-Tuner",
 "← → o Pag ↑ ↓: canale precedente / successivo\n0–9: numero del canale    Backspace: ultimo canale\n↑ ↓ o rotellina: volume    M: muto\nF11, F o doppio clic: schermo intero    Esc: esci dallo schermo intero\nH: nascondi / mostra l'interfaccia    I: informazioni\nG: guida TV    E: registrazioni    R: registra\nL: lingua    V: traccia audio successiva    U: sottotitoli\nP: picture-in-picture    Tab: audio dell'immagine principale / del riquadro\nS: impostazioni    T: sintonizzatore HDHomeRun",
 "← → ou Page ↑ ↓: canal anterior / seguinte\n0–9: número do canal    Retrocesso: último canal\n↑ ↓ ou roda do rato: volume    M: sem som\nF11, F ou duplo clique: ecrã inteiro    Esc: sair do ecrã inteiro\nH: ocultar / mostrar a interface    I: informações\nG: guia de TV    E: gravações    R: gravar\nL: idioma    V: faixa de áudio seguinte    U: legendas\nP: imagem na imagem    Tab: som da imagem principal / do recorte\nS: definições    T: sintonizador HDHomeRun"],
}


def cstr(s: str) -> str:
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


def main():
    all_s = dict(S)
    for k, v in LINUX.items():
        assert len(v) == 6, k
        all_s[k] = v
    ids = ["/* strings_ids.h — généré par tools/gen_strings.py : modifier la table du script, pas ce fichier. */",
           "#ifndef HR_STRINGS_IDS_H", "#define HR_STRINGS_IDS_H", "", "enum {"]
    ids += [f"    S_{k}," for k in all_s] + ["    S_COUNT", "};", "", "enum {"]
    ids += [f"    P_{k}," for k in PLURALS] + ["    P_COUNT", "};", "", "enum {"]
    ids += [f"    A_{k.removesuffix('_labels')}," for k in ARRAYS] + ["    A_COUNT", "};", "", "#endif"]
    out = ["/* strings_gen.h — généré par tools/gen_strings.py : modifier la table du script, pas ce fichier. */",
           "/* Colonnes : fr, en, es, de, it, pt.  Inclus uniquement par i18n.c. */", ""]
    out.append("static const char *const STRINGS[S_COUNT][6] = {")
    for k, v in all_s.items():
        out.append("    { " + ", ".join(cstr(x) for x in v) + " },   /* " + k + " */")
    out += ["};", ""]
    out.append("static const char *const PLURALS_T[P_COUNT][6][2] = {")
    for k, v in PLURALS.items():
        out.append("    { " + ", ".join("{ " + cstr(a) + ", " + cstr(b) + " }" for a, b in v) + " },")
    out += ["};", ""]
    width = max(len(v[0]) for v in ARRAYS.values()) + 1
    out.append(f"static const char *const ARRAYS_T[A_COUNT][6][{width}] = {{")
    for k, v in ARRAYS.items():
        out.append("    {   /* " + k + " */")
        for row in v:
            assert len(row) == len(v[0]), k
            out.append("        { " + ", ".join(cstr(x) for x in row) + ", NULL },")
        out.append("    },")
    out += ["};", ""]
    out.append(f"static const char *const VALUES_T[A_COUNT][{width}] = {{")
    for k in ARRAYS:
        vals = ARRAY_VALUES[k.replace("_labels", "_values")]
        assert len(vals) == len(ARRAYS[k][0]), k
        out.append("    { " + ", ".join(cstr(x) for x in vals) + ", NULL },   /* " + k + " */")
    out += ["};"]
    d = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src")
    for name, lines in (("strings_ids.h", ids), ("strings_gen.h", out)):
        with open(os.path.join(d, name), "w", encoding="utf-8", newline=NL) as f:
            f.write(NL.join(lines) + NL)
    print("ok:", len(all_s), "textes,", len(LANGS), "langues")


if __name__ == "__main__":
    main()
