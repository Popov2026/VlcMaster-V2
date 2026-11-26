package fr.popov.homerun

import android.app.Notification
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.Environment
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.PowerManager
import android.text.format.Formatter
import androidx.core.app.NotificationCompat
import androidx.core.app.ServiceCompat
import androidx.core.content.ContextCompat
import kotlinx.coroutines.flow.MutableStateFlow
import java.io.File
import java.io.FileOutputStream
import java.net.HttpURLConnection
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.atomic.AtomicInteger

/**
 * Enregistrement : copie du flux HTTP de la chaîne dans un fichier .ts
 * (« AAAA-MM-JJ HHhMM - Chaîne - Émission.ts »), jusqu'à l'heure de fin
 * (fin de l'émission + marge, ou durée choisie) ou sans limite.
 * Service au premier plan : l'enregistrement continue appli fermée.   (↔ src/record.h)
 */
class RecordService : Service() {

    class Rec(
        val id: Int, val chName: String, val title: String, val url: String, val file: File,
        val start: Long, @Volatile var endAt: Long,
    ) {
        @Volatile var bytes = 0L
        @Volatile var stop = false
        @Volatile var error: String? = null
        @Volatile var conn: HttpURLConnection? = null
    }

    companion object {
        const val ACTION_START = "start"
        const val ACTION_STOP = "stop"
        private const val NOTIF_ID = 42
        private val nextId = AtomicInteger(1)

        /** Enregistrements en cours (observés par l'interface). */
        val active = MutableStateFlow<List<Rec>>(emptyList())
        /** Dernier message de fin (« terminé · 1 h 02 · 2,1 Go » ou erreur). */
        val lastEvent = MutableStateFlow<String?>(null)

        fun dir(ctx: Context): File {
            val d = ctx.getExternalFilesDir(Environment.DIRECTORY_MOVIES) ?: File(ctx.filesDir, "Recordings")
            d.mkdirs()
            return d
        }

        private fun sanitize(s: String) = s.replace(Regex("[\\\\/:*?\"<>|\\p{Cntrl}]"), " ").replace(Regex("\\s+"), " ").trim()

        fun fileFor(ctx: Context, chName: String, title: String?): File {
            val d = dir(ctx)
            val base = SimpleDateFormat("yyyy-MM-dd HH'h'mm", Locale.US).format(Date()) + " - " +
                sanitize(chName).take(60) + (title?.let { sanitize(it).take(80) }?.takeIf { it.isNotEmpty() }?.let { " - $it" } ?: "")
            var f = File(d, "$base.ts")
            var k = 2
            while (f.exists() && k < 100) { f = File(d, "$base ($k).ts"); k++ }
            return f
        }

        /** Lance un enregistrement ; endAt en secondes epoch (0 = sans limite). */
        fun start(ctx: Context, ch: Channel, title: String?, endAt: Long) {
            val i = Intent(ctx, RecordService::class.java).setAction(ACTION_START)
                .putExtra("url", ch.url).putExtra("name", ch.name).putExtra("title", title ?: "").putExtra("endAt", endAt)
            ContextCompat.startForegroundService(ctx, i)
        }

        fun stop(ctx: Context, id: Int) {
            ctx.startService(Intent(ctx, RecordService::class.java).setAction(ACTION_STOP).putExtra("id", id))
        }

        fun isRecording(url: String) = active.value.any { it.url == url }

        fun fmtDur(sec: Long): String {
            val h = sec / 3600; val m = (sec / 60) % 60; val s = sec % 60
            return if (h > 0) String.format(Locale.US, "%d h %02d", h, m) else String.format(Locale.US, "%d min %02d s", m, s)
        }
    }

    private val main = Handler(Looper.getMainLooper())
    private var wake: PowerManager.WakeLock? = null
    private val ticker = object : Runnable {
        override fun run() {
            if (active.value.isEmpty()) return
            updateNotif()
            main.postDelayed(this, 5000)
        }
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_START -> {
                // startForeground immédiatement (obligatoire après startForegroundService)
                goForeground()
                val url = intent.getStringExtra("url") ?: return START_NOT_STICKY
                val name = intent.getStringExtra("name") ?: ""
                val title = intent.getStringExtra("title") ?: ""
                val r = Rec(nextId.getAndIncrement(), name, title, url, fileFor(this, name, title), nowEpoch(), intent.getLongExtra("endAt", 0))
                active.value = active.value + r
                Thread({ run(r) }, "rec-${r.id}").start()
                if (wake == null) {
                    wake = (getSystemService(POWER_SERVICE) as PowerManager)
                        .newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "HomeRun:rec").also { it.acquire(24 * 3600_000L) }
                }
                main.removeCallbacks(ticker)
                main.postDelayed(ticker, 5000)
                updateNotif()
            }
            ACTION_STOP -> {
                val id = intent.getIntExtra("id", -1)
                active.value.filter { id < 0 || it.id == id }.forEach { r ->
                    r.stop = true
                    Thread { try { r.conn?.disconnect() } catch (_: Exception) {} }.start()
                }
                if (active.value.isEmpty()) finishIfIdle()
            }
            else -> finishIfIdle()
        }
        return START_NOT_STICKY
    }

    /** Boucle de copie (thread dédié). */
    private fun run(r: Rec) {
        var tries = 0
        val buf = ByteArray(188 * 512)
        try {
            FileOutputStream(r.file, true).use { out ->
                while (!r.stop && (r.endAt == 0L || nowEpoch() < r.endAt)) {
                    try {
                        val c = Net.open(r.url, timeout = 15000)
                        r.conn = c
                        val code = c.responseCode
                        if (code == 503) throw java.io.IOException(getString(R.string.rec_err_no_tuner))
                        if (code !in 200..299) throw java.io.IOException("HTTP $code")
                        c.inputStream.use { ins ->
                            while (!r.stop) {
                                val n = ins.read(buf)
                                if (n < 0) break
                                out.write(buf, 0, n)
                                r.bytes += n
                                if (r.endAt != 0L && nowEpoch() >= r.endAt) break
                            }
                        }
                        c.disconnect()
                        tries = 0
                    } catch (e: Exception) {
                        if (r.stop) break
                        r.error = e.message ?: e.javaClass.simpleName
                        // Coupure : quelques nouvelles tentatives tant que l'heure de fin n'est pas passée
                        if (++tries > 5) break
                        Thread.sleep(2000)
                    }
                }
            }
        } catch (e: Exception) {
            r.error = e.message ?: e.javaClass.simpleName
        }
        main.post { finished(r) }
    }

    private fun finished(r: Rec) {
        active.value = active.value.filter { it !== r }
        val dur = fmtDur(nowEpoch() - r.start)
        val size = Formatter.formatShortFileSize(this, r.bytes)
        lastEvent.value = when {
            r.bytes == 0L -> {
                r.file.delete()
                getString(R.string.rec_failed, r.chName, r.error ?: getString(R.string.rec_no_data))
            }
            r.error != null && !r.stop && (r.endAt == 0L || nowEpoch() < r.endAt) ->
                getString(R.string.rec_stopped_err, r.chName, r.error, dur, size)
            else -> getString(R.string.rec_done, r.chName, dur, size)
        }
        finishIfIdle()
    }

    private fun finishIfIdle() {
        if (active.value.isNotEmpty()) { updateNotif(); return }
        main.removeCallbacks(ticker)
        wake?.let { if (it.isHeld) it.release() }
        wake = null
        ServiceCompat.stopForeground(this, ServiceCompat.STOP_FOREGROUND_REMOVE)
        stopSelf()
    }

    private fun buildNotif(): Notification {
        val list = active.value
        val text = list.joinToString("\n") { r ->
            val end = if (r.endAt > 0) getString(R.string.rec_until, hm(r.endAt)) else getString(R.string.rec_no_limit)
            "● ${r.chName}${if (r.title.isNotEmpty()) " – " + r.title else ""} · ${Formatter.formatShortFileSize(this, r.bytes)} · $end"
        }
        val open = PendingIntent.getActivity(this, 0, Intent(this, RecordingsActivity::class.java),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)
        val stopAll = PendingIntent.getService(this, 1, Intent(this, RecordService::class.java).setAction(ACTION_STOP).putExtra("id", -1),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)
        return NotificationCompat.Builder(this, App.CH_REC)
            .setSmallIcon(R.drawable.ic_rec)
            .setContentTitle(resources.getQuantityString(R.plurals.rec_notif_title, maxOf(list.size, 1), maxOf(list.size, 1)))
            .setContentText(text.lineSequence().firstOrNull() ?: getString(R.string.rec_starting))
            .setStyle(NotificationCompat.BigTextStyle().bigText(text.ifEmpty { getString(R.string.rec_starting) }))
            .setOngoing(true)
            .setOnlyAlertOnce(true)
            .setContentIntent(open)
            .addAction(0, getString(R.string.rec_stop_all), stopAll)
            .build()
    }

    private fun goForeground() {
        val type = if (Build.VERSION.SDK_INT >= 29) ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC else 0
        ServiceCompat.startForeground(this, NOTIF_ID, buildNotif(), type)
    }

    private fun updateNotif() {
        if (active.value.isEmpty()) return
        val nm = getSystemService(NOTIFICATION_SERVICE) as android.app.NotificationManager
        try { nm.notify(NOTIF_ID, buildNotif()) } catch (_: SecurityException) {}
    }

    override fun onDestroy() {
        active.value.forEach { it.stop = true }
        main.removeCallbacks(ticker)
        wake?.let { if (it.isHeld) it.release() }
        super.onDestroy()
    }
}
