package fr.popov.homerun

import android.app.Application
import android.app.NotificationChannel
import android.app.NotificationManager
import android.content.Context
import android.content.SharedPreferences
import android.os.Build
import androidx.preference.PreferenceManager
import org.videolan.libvlc.LibVLC

/**
 * HomeRun Player pour Android — lecteur TV pour tuners HDHomeRun (libVLC).
 *
 * Fichiers (équivalents des modules de la version Windows, src/) :
 *   App.kt              réglages, instance LibVLC partagée, canal de notifications
 *   Net.kt              requêtes HTTP + playlist M3U (Channel)
 *   Guide.kt            guide TV (api.hdhomerun.com)                ↔ guide_data.h
 *   MainActivity.kt     lecture, liste, zapping, PIP, HUD           ↔ vlcmaster.c, pip.h
 *   GuideActivity.kt    grille horaire (GuideGridView.kt)           ↔ guide_ui.h
 *   Tracks.kt           pistes audio et sous-titres                 ↔ tracks.h
 *   RecordService.kt    enregistrement (copie du flux en .ts)       ↔ record.h
 *   RecordingsActivity / RecPlayerActivity : liste et lecture des enregistrements
 *   TunerActivity.kt    infos tuner, firmware, scan des chaînes     ↔ tuner.h
 *   Updater.kt          nouvelle version sur GitHub                 ↔ update.h
 *   SettingsActivity.kt Paramètres ; textes en 6 langues dans res/values-xx
 */
class App : Application() {
    override fun onCreate() {
        super.onCreate()
        instance = this
        if (Build.VERSION.SDK_INT >= 26) {
            val nm = getSystemService(NotificationManager::class.java)
            nm.createNotificationChannel(
                NotificationChannel(CH_REC, getString(R.string.notif_channel_rec), NotificationManager.IMPORTANCE_LOW)
            )
        }
    }

    companion object {
        const val CH_REC = "rec"
        lateinit var instance: App
            private set

        private var vlc: LibVLC? = null
        private var vlcKey = ""

        /** Instance LibVLC partagée (options de Paramètres ; recréée si elles changent). */
        fun libVLC(ctx: Context): LibVLC {
            val p = Prefs.get(ctx)
            val opts = arrayListOf("--http-reconnect", "--no-stats", "--audio-time-stretch")
            if (BuildConfig.DEBUG) opts += "-v"
            val deint = p.deinterlace
            if (deint != "off") {
                opts += "--deinterlace=1"
                opts += "--deinterlace-mode=$deint"
            } else opts += "--deinterlace=0"
            val key = opts.joinToString(" ")
            val cur = vlc
            if (cur != null && vlcKey == key) return cur
            // Réglage changé : nouvelle instance.  L'ancienne n'est pas libérée ici (des
            // lecteurs peuvent encore l'utiliser) ; cela n'arrive qu'après un passage
            // dans Paramètres.
            val n = LibVLC(ctx.applicationContext, opts)
            vlc = n
            vlcKey = key
            return n
        }
    }
}

/** Réglages (SharedPreferences par défaut, partagées avec l'écran Paramètres). */
class Prefs(val sp: SharedPreferences) {
    var playlistUrl: String
        get() = sp.getString("playlist_url", null)?.trim().takeUnless { it.isNullOrEmpty() } ?: DEFAULT_URL
        set(v) = sp.edit().putString("playlist_url", v).apply()
    val caching: Int get() = sp.getString("caching", "1000")!!.toIntOrNull() ?: 1000
    val hwDecoding: Boolean get() = sp.getBoolean("hw_decoding", true)
    val deinterlace: String get() = sp.getString("deinterlace", "yadif")!!
    val aspect: String get() = sp.getString("aspect", "")!!
    val resume: Boolean get() = sp.getBoolean("resume", true)
    val showGuide: Boolean get() = sp.getBoolean("show_guide", true)
    val recMargin: Int get() = sp.getString("rec_margin", "10")!!.toIntOrNull() ?: 10
    val checkUpdates: Boolean get() = sp.getBoolean("check_updates", true)
    var lastChannel: String
        get() = sp.getString("last_channel", "")!!
        set(v) = sp.edit().putString("last_channel", v).apply()
    var volume: Int
        get() = sp.getInt("volume", 100)
        set(v) = sp.edit().putInt("volume", v).apply()
    /** Langue audio / sous-titres choisie (clé de Tracks.key ; « off » = sous-titres désactivés). */
    var audioPref: String
        get() = sp.getString("audio_pref", "")!!
        set(v) = sp.edit().putString("audio_pref", v).apply()
    var spuPref: String
        get() = sp.getString("spu_pref", "")!!
        set(v) = sp.edit().putString("spu_pref", v).apply()
    var skippedVersion: String
        get() = sp.getString("skipped_version", "")!!
        set(v) = sp.edit().putString("skipped_version", v).apply()

    /** Adresse du tuner (http://hôte[:port]) d'après la playlist. */
    val tunerBase: String get() = baseOf(playlistUrl)

    companion object {
        const val DEFAULT_URL = "http://192.168.1.90/lineup.m3u"
        fun get(ctx: Context) = Prefs(PreferenceManager.getDefaultSharedPreferences(ctx))
        fun baseOf(url: String): String {
            val m = Regex("^(https?://[^/]+)", RegexOption.IGNORE_CASE).find(url.trim())
            return m?.groupValues?.get(1) ?: "http://192.168.1.90"
        }
    }
}
