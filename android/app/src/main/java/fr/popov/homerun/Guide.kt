package fr.popov.homerun

import org.json.JSONArray
import org.json.JSONObject
import java.net.URLEncoder
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/** Un programme du guide (heures en secondes depuis 1970, UTC). */
data class Prog(val start: Long, val end: Long, val title: String, val episode: String?, val synopsis: String?) {
    fun timeRange(): String = "${hm(start)} – ${hm(end)}"
}

class GuideCh(val num: String, val name: String) {
    val progs = ArrayList<Prog>()
}

fun nowEpoch(): Long = System.currentTimeMillis() / 1000
fun hm(t: Long): String = SimpleDateFormat("HH:mm", Locale.getDefault()).format(Date(t * 1000))

/**
 * Guide TV — service guide de SiliconDust (api.hdhomerun.com), interrogé avec le
 * DeviceAuth publié par le tuner (http://<tuner>/discover.json).  Chaque requête
 * renvoie quelques heures de programmes ; on enchaîne (&Start=) jusqu'à ~24 h.
 */
object Guide {
    @Volatile var channels: Map<String, GuideCh> = emptyMap()
        private set
    @Volatile var loadedAt = 0L
        private set
    @Volatile var maxEnd = 0L
        private set

    class GuideException(val kind: Int, msg: String) : Exception(msg)

    /** Télécharge le guide (bloquant). Lève GuideException en cas d'échec. */
    fun load(base: String) {
        val auth = try {
            JSONObject(Net.get("$base/discover.json")).optString("DeviceAuth", "")
        } catch (e: Exception) {
            throw GuideException(R.string.guide_err_no_tuner, e.message ?: "")
        }
        if (auth.isEmpty()) throw GuideException(R.string.guide_err_no_tuner, base)
        val res = LinkedHashMap<String, GuideCh>()
        val now = nowEpoch()
        var start = 0L
        var end = 0L
        var firstError: Exception? = null
        for (i in 0 until 8) {
            var url = "https://api.hdhomerun.com/api/guide.php?DeviceAuth=" + URLEncoder.encode(auth, "UTF-8")
            if (start > 0) url += "&Start=$start"
            val arr = try {
                JSONArray(Net.get(url, 15000))
            } catch (e: Exception) {
                if (res.isEmpty()) firstError = e
                break
            }
            val before = end
            end = maxOf(end, merge(res, arr))
            if (end <= before) break
            start = end
            if (end >= now + 24 * 3600) break
        }
        if (res.isEmpty()) {
            firstError?.let { throw GuideException(R.string.guide_err_service, it.message ?: "") }
            throw GuideException(R.string.guide_err_empty, "")
        }
        channels = res
        maxEnd = end
        loadedAt = System.currentTimeMillis()
    }

    /** Ajoute une page de programmes, sans doublons, triée par heure ; renvoie la fin la plus tardive. */
    private fun merge(res: MutableMap<String, GuideCh>, arr: JSONArray): Long {
        var maxE = 0L
        for (i in 0 until arr.length()) {
            val c = arr.optJSONObject(i) ?: continue
            val num = c.optString("GuideNumber", "")
            val progs = c.optJSONArray("Guide") ?: continue
            if (num.isEmpty()) continue
            val g = res.getOrPut(num) { GuideCh(num, c.optString("GuideName", "")) }
            for (j in 0 until progs.length()) {
                val p = progs.optJSONObject(j) ?: continue
                val st = p.optLong("StartTime")
                val en = p.optLong("EndTime")
                if (en <= st) continue
                if (g.progs.isNotEmpty() && st < g.progs.last().end) continue // chevauchement entre pages
                val et = p.optString("EpisodeTitle").takeIf { it.isNotEmpty() }
                val enr = p.optString("EpisodeNumber").takeIf { it.isNotEmpty() }
                val ep = if (et != null && enr != null) "$et ($enr)" else et ?: enr
                g.progs += Prog(st, en, p.optString("Title", ""), ep, p.optString("Synopsis").takeIf { it.isNotEmpty() })
                if (en > maxE) maxE = en
            }
        }
        return maxE
    }

    fun forChannel(ch: Channel): GuideCh? = channels[ch.num]

    fun at(ch: Channel, t: Long = nowEpoch()): Prog? = forChannel(ch)?.progs?.firstOrNull { t >= it.start && t < it.end }

    fun next(ch: Channel, t: Long = nowEpoch()): Prog? = forChannel(ch)?.progs?.firstOrNull { it.start >= t }

    /** Guide à rafraîchir (jamais chargé, plus de 3 h, ou fin proche). */
    fun stale(): Boolean =
        loadedAt == 0L || System.currentTimeMillis() - loadedAt > 3 * 3600_000L || maxEnd < nowEpoch() + 6 * 3600
}
