package fr.popov.homerun

import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL

/** Requêtes HTTP simples (à appeler hors du thread principal). */
object Net {
    const val UA = "HomeRunPlayer-Android/" + BuildConfig.VERSION_NAME

    fun open(url: String, method: String = "GET", timeout: Int = 8000): HttpURLConnection {
        val c = URL(url).openConnection() as HttpURLConnection
        c.requestMethod = method
        c.connectTimeout = timeout
        c.readTimeout = timeout
        c.setRequestProperty("User-Agent", UA)
        c.instanceFollowRedirects = true
        return c
    }

    @Throws(IOException::class)
    fun get(url: String, timeout: Int = 8000): String = request(url, "GET", timeout)

    @Throws(IOException::class)
    fun post(url: String, timeout: Int = 8000): String = request(url, "POST", timeout)

    private fun request(url: String, method: String, timeout: Int): String {
        val c = open(url, method, timeout)
        try {
            if (method == "POST") {
                c.doOutput = true
                c.setFixedLengthStreamingMode(0)
                c.outputStream.close()
            }
            val code = c.responseCode
            if (code !in 200..299) throw IOException("HTTP $code")
            return c.inputStream.use { it.readBytes().toString(Charsets.UTF_8) }
        } finally {
            c.disconnect()
        }
    }
}

/** Une chaîne de la playlist. */
data class Channel(val num: String, val name: String, val url: String) {
    val label: String get() = if (num.isNotEmpty()) "$num  $name" else name
}

/** Lecture de la playlist M3U du HDHomeRun (http://<ip>/lineup.m3u). */
object Playlist {
    private val reNum = Regex("(?:channel-number|tvg-chno)=\"([^\"]*)\"")
    private val reName = Regex("tvg-name=\"([^\"]*)\"")
    private val reUrlNum = Regex("/v(\\d+(?:\\.\\d+)?)")

    fun parse(text: String): List<Channel> {
        val out = ArrayList<Channel>()
        var name = ""
        var num = ""
        for (raw in text.lineSequence()) {
            val line = raw.trim().removePrefix("﻿")
            if (line.isEmpty()) continue
            if (line.startsWith("#EXTINF", true)) {
                num = reNum.find(line)?.groupValues?.get(1) ?: ""
                name = line.substringAfterLast(',', "").trim()
                if (name.isEmpty()) name = reName.find(line)?.groupValues?.get(1) ?: ""
            } else if (!line.startsWith("#")) {
                if (num.isEmpty()) num = reUrlNum.find(line)?.groupValues?.get(1) ?: ""
                if (name.isEmpty()) name = line.substringAfterLast('/')
                // « 1 TF1 » : numéro en tête du nom (format HDHomeRun)
                if (num.isNotEmpty() && name.startsWith("$num ")) name = name.removePrefix("$num ").trim()
                out += Channel(num, name, line)
                name = ""
                num = ""
            }
        }
        return out
    }

    fun load(url: String): List<Channel> = parse(Net.get(url, 10000))
}
