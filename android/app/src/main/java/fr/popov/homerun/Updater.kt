package fr.popov.homerun

import org.json.JSONObject

/**
 * Mise à jour de l'appli : compare la version à la dernière release GitHub.
 * Un bandeau n'est proposé que si la release est plus récente ET contient un APK
 * (une release Windows seule ne concerne pas Android).          (↔ src/update.h)
 */
object Updater {
    const val REPO = "Popov2026/VlcMaster-V2"
    const val API = "https://api.github.com/repos/$REPO/releases/latest"
    const val PAGE = "https://github.com/$REPO/releases/latest"

    data class Info(val tag: String, val version: String, val apkUrl: String?, val newer: Boolean)

    /** Comparaison numérique « 3.10 » > « 3.9 ». */
    fun compare(a: String, b: String): Int {
        val x = a.trimStart('v', 'V').split('.', '-', ' ').map { it.toIntOrNull() ?: 0 }
        val y = b.trimStart('v', 'V').split('.', '-', ' ').map { it.toIntOrNull() ?: 0 }
        for (i in 0 until maxOf(x.size, y.size)) {
            val d = x.getOrElse(i) { 0 } - y.getOrElse(i) { 0 }
            if (d != 0) return d
        }
        return 0
    }

    /** Bloquant : lit la dernière release. */
    fun check(): Info {
        val j = JSONObject(Net.get(API, 10000))
        val tag = j.getString("tag_name")
        var apk: String? = null
        val assets = j.optJSONArray("assets")
        if (assets != null) for (i in 0 until assets.length()) {
            val a = assets.getJSONObject(i)
            if (a.optString("name").endsWith(".apk", true)) { apk = a.optString("browser_download_url"); break }
        }
        val ver = tag.trimStart('v', 'V')
        return Info(tag, ver, apk, compare(ver, BuildConfig.VERSION_NAME) > 0)
    }
}
