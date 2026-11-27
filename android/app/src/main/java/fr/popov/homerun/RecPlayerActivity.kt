package fr.popov.homerun

import android.content.Context
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.KeyEvent
import android.view.View
import android.view.WindowManager
import android.widget.Button
import android.widget.SeekBar
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import org.videolan.libvlc.Media
import org.videolan.libvlc.MediaPlayer
import org.videolan.libvlc.util.VLCVideoLayout
import java.io.File
import java.util.Locale

/** Lecture d'un enregistrement, avec barre de position ; la position est mémorisée. */
class RecPlayerActivity : AppCompatActivity() {
    private var mp: MediaPlayer? = null
    private lateinit var file: File
    private lateinit var bar: View
    private lateinit var seek: SeekBar
    private lateinit var time: TextView
    private lateinit var btnPlay: Button
    private val h = Handler(Looper.getMainLooper())
    private var resumed = false
    private var dragging = false
    private var attached = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_rec_player)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        WindowCompat.setDecorFitsSystemWindows(window, false)
        WindowInsetsControllerCompat(window, window.decorView).apply {
            hide(WindowInsetsCompat.Type.systemBars())
            systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        }
        file = File(intent.getStringExtra(EXTRA_PATH) ?: run { finish(); return })
        bar = findViewById(R.id.bar)
        seek = findViewById(R.id.seek)
        time = findViewById(R.id.time)
        btnPlay = findViewById(R.id.btnPlay)
        findViewById<TextView>(R.id.title).text = file.nameWithoutExtension

        val p = MediaPlayer(App.libVLC(this))
        p.attachViews(findViewById<VLCVideoLayout>(R.id.video), null, true, false)
        attached = true
        p.setEventListener { ev ->
            when (ev.type) {
                MediaPlayer.Event.Playing -> {
                    btnPlay.setText(R.string.btn_pause)
                    if (!resumed) {
                        resumed = true
                        val pos = savedPos(this, file)
                        if (pos > 5000) p.setTime(pos)
                        h.postDelayed({ Tracks.applyPrefs(this, p) }, 600)
                    }
                }
                MediaPlayer.Event.Paused -> btnPlay.setText(R.string.btn_play)
                MediaPlayer.Event.EndReached -> { forget(this, file); finish() }
                MediaPlayer.Event.ESAdded -> h.postDelayed({ Tracks.applyPrefs(this, p) }, 300)
            }
        }
        mp = p
        val m = Media(p.libVLC, Uri.fromFile(file))
        m.setHWDecoderEnabled(Prefs.get(this).hwDecoding, false)
        p.media = m
        m.release()
        p.setVolume(Prefs.get(this).volume)
        p.play()

        btnPlay.setOnClickListener { togglePlay() }
        findViewById<Button>(R.id.btnBack).setOnClickListener { seekBy(-30_000) }
        findViewById<Button>(R.id.btnFwd).setOnClickListener { seekBy(30_000) }
        findViewById<Button>(R.id.btnLang).setOnClickListener { Tracks.showDialog(this, mp) }
        findViewById<View>(R.id.video).setOnClickListener { toggleBar() }
        findViewById<View>(R.id.touch).setOnClickListener { toggleBar() }
        seek.max = 1000
        seek.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(s: SeekBar?, v: Int, fromUser: Boolean) {
                if (fromUser) { mp?.position = v / 1000f; showBar() }
            }
            override fun onStartTrackingTouch(s: SeekBar?) { dragging = true }
            override fun onStopTrackingTouch(s: SeekBar?) { dragging = false }
        })
        h.post(tick)
        showBar()
    }

    private val tick = object : Runnable {
        override fun run() {
            val p = mp ?: return
            val t = p.time
            val len = p.length
            if (!dragging && len > 0) seek.progress = (t * 1000 / len).toInt()
            time.text = fmt(t) + if (len > 0) " / " + fmt(len) else ""
            if (t > 0) save(t)
            h.postDelayed(this, 1000)
        }
    }

    private fun save(t: Long) = getSharedPreferences("recpos", MODE_PRIVATE).edit().putLong(file.name, t).apply()

    private fun togglePlay() {
        val p = mp ?: return
        if (p.isPlaying) p.pause() else p.play()
        showBar()
    }

    private fun seekBy(ms: Long) {
        val p = mp ?: return
        p.setTime((p.time + ms).coerceAtLeast(0))
        showBar()
    }

    private val hideBar = Runnable { bar.visibility = View.GONE }
    private fun showBar() {
        bar.visibility = View.VISIBLE
        h.removeCallbacks(hideBar)
        h.postDelayed(hideBar, 5000)
    }
    private fun toggleBar() = if (bar.visibility == View.VISIBLE) bar.visibility = View.GONE else showBar()

    override fun onKeyDown(k: Int, e: KeyEvent): Boolean {
        when (k) {
            KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_MEDIA_REWIND -> if (bar.visibility != View.VISIBLE || !bar.hasFocus()) { seekBy(-10_000); return true }
            KeyEvent.KEYCODE_DPAD_RIGHT, KeyEvent.KEYCODE_MEDIA_FAST_FORWARD -> if (bar.visibility != View.VISIBLE || !bar.hasFocus()) { seekBy(30_000); return true }
            KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE, KeyEvent.KEYCODE_SPACE -> { togglePlay(); return true }
            KeyEvent.KEYCODE_DPAD_CENTER, KeyEvent.KEYCODE_ENTER -> if (bar.visibility != View.VISIBLE) { showBar(); btnPlay.requestFocus(); return true }
            KeyEvent.KEYCODE_V, KeyEvent.KEYCODE_MEDIA_AUDIO_TRACK -> { Tracks.cycleAudio(this, mp); return true }
            KeyEvent.KEYCODE_U, KeyEvent.KEYCODE_CAPTIONS -> { Tracks.cycleSpu(this, mp); return true }
        }
        return super.onKeyDown(k, e)
    }

    override fun onStart() {
        super.onStart()
        val p = mp ?: return
        if (!attached) { p.attachViews(findViewById<VLCVideoLayout>(R.id.video), null, true, false); attached = true }
    }

    override fun onStop() {
        super.onStop()
        if (attached) { mp?.detachViews(); attached = false }
    }

    override fun onPause() {
        super.onPause()
        mp?.let { if (it.isPlaying) it.pause(); if (it.time > 0) save(it.time) }
    }

    override fun onDestroy() {
        h.removeCallbacksAndMessages(null)
        mp?.let { it.setEventListener(null); it.stop(); if (attached) it.detachViews(); it.release() }
        mp = null
        super.onDestroy()
    }

    companion object {
        const val EXTRA_PATH = "path"
        fun savedPos(ctx: Context, f: File) = ctx.getSharedPreferences("recpos", MODE_PRIVATE).getLong(f.name, 0)
        fun forget(ctx: Context, f: File) = ctx.getSharedPreferences("recpos", MODE_PRIVATE).edit().remove(f.name).apply()
        fun fmt(ms: Long): String {
            val s = ms / 1000
            return if (s >= 3600) String.format(Locale.US, "%d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60)
            else String.format(Locale.US, "%d:%02d", s / 60, s % 60)
        }
    }
}
