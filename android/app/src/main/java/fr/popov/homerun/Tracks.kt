package fr.popov.homerun

import android.content.Context
import androidx.appcompat.app.AlertDialog
import org.videolan.libvlc.MediaPlayer
import java.util.Locale

/**
 * Langues : pistes audio (VF, VO « qaa », audiodescription « qad »…) et sous-titres DVB
 * du lecteur qui a le son (image principale ou PIP).
 *
 * Le choix est mémorisé (texte entre crochets du nom de piste, ex. « [French] », plus
 * « audiodescription » / « malentendants ») et réappliqué à chaque nouvelle chaîne qui
 * propose la même piste.                                    (↔ src/tracks.h)
 */
object Tracks {
    data class T(val id: Int, val raw: String, val label: String, val key: String)

    private val enNames: Map<String, String> by lazy {
        val m = HashMap<String, String>()
        for (code in Locale.getISOLanguages()) {
            val n = Locale(code).getDisplayLanguage(Locale.ENGLISH)
            if (n.isNotEmpty()) m[n.lowercase()] = code
        }
        m
    }

    private fun bracket(raw: String): String? {
        val a = raw.lastIndexOf('[')
        val b = if (a >= 0) raw.indexOf(']', a) else -1
        return if (a >= 0 && b > a + 1) raw.substring(a + 1, b).trim() else null
    }

    private fun isAD(raw: String) = raw.contains("audio description", true) || raw.contains("audiodescription", true)
    private fun isHoH(raw: String) = raw.contains("hard of hearing", true) || raw.contains("hearing impaired", true) ||
        raw.contains("malentendants", true)

    /** Clé mémorisée : langue + type de piste. */
    fun key(raw: String): String {
        val b = bracket(raw) ?: raw
        return b.lowercase() + (if (isAD(raw)) "|ad" else "") + (if (isHoH(raw)) "|hoh" else "")
    }

    /** Nom de langue lisible dans la langue de l'interface. */
    private fun langName(ctx: Context, s: String): String {
        when (s.lowercase()) {
            "qaa", "original audio", "original language" -> return ctx.getString(R.string.trk_qaa)
            "qad", "audio description" -> return ctx.getString(R.string.trk_qad)
            "qtz" -> return ctx.getString(R.string.trk_qtz)
            "mis" -> return ctx.getString(R.string.trk_mis)
            "und" -> return ctx.getString(R.string.trk_und)
        }
        val code = enNames[s.lowercase()] ?: s.takeIf { it.length in 2..3 && it.all(Char::isLetter) }?.lowercase()
        if (code != null) {
            val n = Locale(code).displayLanguage
            if (n.isNotEmpty() && !n.equals(code, true)) return n.replaceFirstChar { it.titlecase() }
        }
        return s
    }

    /** « Track 1 - [French] » → « Français », audiodescription / malentendants en suffixe. */
    fun pretty(ctx: Context, raw: String): String {
        val b = bracket(raw) ?: return raw
        var s = langName(ctx, b)
        if (isAD(raw) && !b.equals("qad", true)) s += " (" + ctx.getString(R.string.trk_ad) + ")"
        if (isHoH(raw)) s += " (" + ctx.getString(R.string.trk_hoh) + ")"
        return s
    }

    fun audio(ctx: Context, mp: MediaPlayer?): List<T> =
        mp?.audioTracks.orEmpty().filter { it.id >= 0 }.map { T(it.id, it.name ?: "", pretty(ctx, it.name ?: ""), key(it.name ?: "")) }

    fun spu(ctx: Context, mp: MediaPlayer?): List<T> =
        mp?.spuTracks.orEmpty().filter { it.id >= 0 }.map { T(it.id, it.name ?: "", pretty(ctx, it.name ?: ""), key(it.name ?: "")) }

    /** Réapplique les choix mémorisés sur une nouvelle chaîne. */
    fun applyPrefs(ctx: Context, mp: MediaPlayer?) {
        mp ?: return
        val p = Prefs.get(ctx)
        if (p.audioPref.isNotEmpty()) {
            val t = audio(ctx, mp).firstOrNull { it.key == p.audioPref }
            if (t != null && mp.audioTrack != t.id) mp.setAudioTrack(t.id)
        }
        val sp = p.spuPref
        if (sp == "off") {
            if (mp.spuTrack != -1) mp.setSpuTrack(-1)
        } else if (sp.isNotEmpty()) {
            val t = spu(ctx, mp).firstOrNull { it.key == sp }
            if (t != null && mp.spuTrack != t.id) mp.setSpuTrack(t.id)
        }
    }

    /** Piste audio suivante (touche « audio » de la télécommande) ; renvoie son nom. */
    fun cycleAudio(ctx: Context, mp: MediaPlayer?): String? {
        val l = audio(ctx, mp)
        if (l.isEmpty() || mp == null) return null
        val i = l.indexOfFirst { it.id == mp.audioTrack }
        val t = l[(i + 1) % l.size]
        mp.setAudioTrack(t.id)
        Prefs.get(ctx).audioPref = t.key
        return t.label
    }

    /** Sous-titres suivants (désactivés compris) ; renvoie leur nom. */
    fun cycleSpu(ctx: Context, mp: MediaPlayer?): String? {
        mp ?: return null
        val l = spu(ctx, mp)
        if (l.isEmpty()) return null
        val i = l.indexOfFirst { it.id == mp.spuTrack }
        return if (i == l.size - 1) {
            mp.setSpuTrack(-1); Prefs.get(ctx).spuPref = "off"; ctx.getString(R.string.trk_spu_off)
        } else {
            val t = l[i + 1]; mp.setSpuTrack(t.id); Prefs.get(ctx).spuPref = t.key; t.label
        }
    }

    /** Fenêtre « Langue » : piste audio puis sous-titres. */
    fun showDialog(ctx: Context, mp: MediaPlayer?, onDone: (String) -> Unit = {}) {
        val aud = audio(ctx, mp)
        val spu = spu(ctx, mp)
        if (mp == null || (aud.isEmpty() && spu.isEmpty())) {
            AlertDialog.Builder(ctx).setTitle(R.string.lang_title).setMessage(R.string.trk_none)
                .setPositiveButton(android.R.string.ok, null).show()
            return
        }
        val labels = ArrayList<String>()
        val actions = ArrayList<() -> String>()
        val curA = mp.audioTrack
        val curS = mp.spuTrack
        for (t in aud) {
            labels += (if (t.id == curA) "● " else "    ") + ctx.getString(R.string.trk_audio_item, t.label)
            actions += { mp.setAudioTrack(t.id); Prefs.get(ctx).audioPref = t.key; t.label }
        }
        if (spu.isNotEmpty()) {
            labels += (if (curS < 0) "● " else "    ") + ctx.getString(R.string.trk_spu_item, ctx.getString(R.string.trk_spu_off))
            actions += { mp.setSpuTrack(-1); Prefs.get(ctx).spuPref = "off"; ctx.getString(R.string.trk_spu_off) }
            for (t in spu) {
                labels += (if (t.id == curS) "● " else "    ") + ctx.getString(R.string.trk_spu_item, t.label)
                actions += { mp.setSpuTrack(t.id); Prefs.get(ctx).spuPref = t.key; t.label }
            }
        }
        AlertDialog.Builder(ctx).setTitle(R.string.lang_title)
            .setItems(labels.toTypedArray()) { _, w -> onDone(actions[w]()) }
            .setNegativeButton(android.R.string.cancel, null).show()
    }
}
