package fr.popov.homerun

import android.Manifest
import android.annotation.SuppressLint
import android.app.PictureInPictureParams
import android.content.Intent
import android.content.pm.PackageManager
import android.content.res.Configuration
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Rational
import android.view.GestureDetector
import android.view.Gravity
import android.view.KeyEvent
import android.view.LayoutInflater
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.Button
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.SeekBar
import android.widget.TextView
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import androidx.lifecycle.lifecycleScope
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject
import org.videolan.libvlc.LibVLC
import org.videolan.libvlc.Media
import org.videolan.libvlc.MediaPlayer
import org.videolan.libvlc.util.VLCVideoLayout
import kotlin.math.abs

/**
 * Écran principal : lecture de la chaîne, liste des chaînes, zapping (boutons,
 * glisser, D-pad / télécommande, chiffres), HUD, volume, image dans l'image,
 * nouvelle tentative si le flux coupe, reprise de la dernière chaîne.
 *                                               (↔ src/vlcmaster.c, src/pip.h)
 */
class MainActivity : AppCompatActivity() {

    private lateinit var prefs: Prefs
    private var libVLC: LibVLC? = null
    private var mp: MediaPlayer? = null
    private var pip: MediaPlayer? = null

    private lateinit var video: VLCVideoLayout
    private lateinit var pipVideo: VLCVideoLayout
    private lateinit var pipBox: FrameLayout
    private lateinit var pipLabel: TextView
    private lateinit var touch: View
    private lateinit var status: TextView
    private lateinit var hud: View
    private lateinit var hudTitle: TextView
    private lateinit var hudProg: TextView
    private lateinit var hudNext: TextView
    private lateinit var hudProgress: ProgressBar
    private lateinit var flashView: TextView
    private lateinit var recIndicator: TextView
    private lateinit var panel: View
    private lateinit var list: RecyclerView
    private lateinit var controls: View
    private lateinit var updBanner: TextView
    private lateinit var vol: SeekBar
    private lateinit var btnMute: Button

    private val h = Handler(Looper.getMainLooper())
    private var channels: List<Channel> = emptyList()
    private var cur = -1
    private var pipIdx = -1
    private var soundOnPip = false
    private var muted = false
    private var retries = 0
    private var pipRetries = 0
    private var playing = false
    private var stoppedByLifecycle = false
    private var loadedUrl: String? = null
    private var digits = ""
    private var firmwareTold = false
    private var inSystemPip = false
    private var scanSeen = System.currentTimeMillis()
    private var viewsAttached = false
    private val adapter = ChannelAdapter()

    // ------------------------------------------------------------------ cycle de vie

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        prefs = Prefs.get(this)
        setContentView(R.layout.activity_main)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        immersive()

        video = findViewById(R.id.video)
        pipVideo = findViewById(R.id.pipVideo)
        pipBox = findViewById(R.id.pipBox)
        pipLabel = findViewById(R.id.pipLabel)
        touch = findViewById(R.id.touch)
        status = findViewById(R.id.status)
        hud = findViewById(R.id.hud)
        hudTitle = findViewById(R.id.hudTitle)
        hudProg = findViewById(R.id.hudProg)
        hudNext = findViewById(R.id.hudNext)
        hudProgress = findViewById(R.id.hudProgress)
        flashView = findViewById(R.id.flash)
        recIndicator = findViewById(R.id.recIndicator)
        panel = findViewById(R.id.panel)
        list = findViewById(R.id.list)
        controls = findViewById(R.id.controls)
        updBanner = findViewById(R.id.updBanner)
        vol = findViewById(R.id.vol)
        btnMute = findViewById(R.id.btnMute)

        list.layoutManager = LinearLayoutManager(this)
        list.adapter = adapter
        list.itemAnimator = null

        setupGestures()
        setupButtons()
        layoutPip()
        applySafeInsets()

        lifecycleScope.launch {
            RecordService.active.collect { l ->
                recIndicator.visibility = if (l.isEmpty()) View.GONE else View.VISIBLE
                recIndicator.text = if (l.size > 1) getString(R.string.rec_indicator) + " ×" + l.size else getString(R.string.rec_indicator)
            }
        }
        lifecycleScope.launch {
            RecordService.lastEvent.collect { m -> if (m != null) { flash(m); RecordService.lastEvent.value = null } }
        }

        createPlayers()
        loadPlaylist()
        if (prefs.checkUpdates) checkUpdate()
        h.postDelayed(minuteTick, 60_000)
        handleIntent(intent)
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        handleIntent(intent)
    }

    /** Depuis le guide : « Regarder » une chaîne. */
    private fun handleIntent(i: Intent?) {
        val url = i?.getStringExtra(EXTRA_PLAY) ?: return
        i.removeExtra(EXTRA_PLAY)
        val idx = channels.indexOfFirst { it.url == url }
        if (idx >= 0) play(idx) else prefs.lastChannel = url
    }

    override fun onStart() {
        super.onStart()
        immersive()
        // La surface vidéo est recréée à chaque retour au premier plan : on rattache libVLC.
        if (!viewsAttached) { mp?.attachViews(video, null, true, false); viewsAttached = mp != null }
        // Retour de Paramètres : options libVLC ou adresse de la playlist changées ?
        if (App.libVLC(this) !== libVLC) {
            releasePlayers()
            createPlayers()
            if (cur >= 0) play(cur)
        }
        val scanAt = prefs.sp.getLong("scan_done_at", 0)
        if (loadedUrl != null && (loadedUrl != prefs.playlistUrl || scanAt > scanSeen)) { scanSeen = scanAt; loadPlaylist() }
        else if (stoppedByLifecycle && cur >= 0) play(cur)
        stoppedByLifecycle = false
        adapter.notifyDataSetChanged()
        refreshGuide(false)
    }

    override fun onStop() {
        super.onStop()
        if (!isInPip()) {
            // Libère les tuners quand l'appli n'est plus visible.
            playing = false
            h.removeCallbacks(retryRun)
            mp?.stop()
            stopPip()
            if (viewsAttached) { mp?.detachViews(); viewsAttached = false }
            stoppedByLifecycle = true
        }
    }

    override fun onDestroy() {
        h.removeCallbacksAndMessages(null)
        releasePlayers()
        super.onDestroy()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) immersive()
    }

    private fun immersive() {
        WindowCompat.setDecorFitsSystemWindows(window, false)
        val c = WindowInsetsControllerCompat(window, window.decorView)
        c.hide(WindowInsetsCompat.Type.systemBars())
        c.systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
    }

    // ------------------------------------------------------------------ PIP Android (système)

    private fun isInPip() = Build.VERSION.SDK_INT >= 24 && isInPictureInPictureMode

    override fun onUserLeaveHint() {
        super.onUserLeaveHint()
        if (Build.VERSION.SDK_INT >= 26 && playing &&
            packageManager.hasSystemFeature(PackageManager.FEATURE_PICTURE_IN_PICTURE)) {
            try {
                enterPictureInPictureMode(PictureInPictureParams.Builder().setAspectRatio(Rational(16, 9)).build())
            } catch (_: Exception) {}
        }
    }

    override fun onPictureInPictureModeChanged(inPip: Boolean, newConfig: Configuration) {
        super.onPictureInPictureModeChanged(inPip, newConfig)
        inSystemPip = inPip
        if (inPip) {
            hideUi()
            hud.visibility = View.GONE
            flashView.visibility = View.GONE
            if (pip != null) { if (soundOnPip) setSound(false); stopPip() }
        }
    }

    // ------------------------------------------------------------------ lecteurs libVLC

    private fun createPlayers() {
        val lib = App.libVLC(this)
        libVLC = lib
        val p = MediaPlayer(lib)
        p.attachViews(video, null, true, false)
        viewsAttached = true
        p.setEventListener { ev -> onMainEvent(ev) }
        mp = p
        applyVolume()
    }

    private fun releasePlayers() {
        stopPip()
        mp?.let { it.setEventListener(null); it.stop(); if (viewsAttached) it.detachViews(); it.release() }
        viewsAttached = false
        mp = null
    }

    private fun newMedia(url: String): Media {
        val m = Media(libVLC, Uri.parse(url))
        m.setHWDecoderEnabled(prefs.hwDecoding, false)
        m.addOption(":network-caching=" + prefs.caching)
        return m
    }

    /** Lit la chaîne idx sur l'image principale. */
    private fun play(idx: Int, fromRetry: Boolean = false) {
        if (idx !in channels.indices) return
        val p = mp ?: return
        cur = idx
        if (!fromRetry) retries = 0
        h.removeCallbacks(retryRun)
        val ch = channels[idx]
        prefs.lastChannel = ch.url
        val m = newMedia(ch.url)
        p.media = m
        m.release()
        val a = prefs.aspect
        p.aspectRatio = a.ifEmpty { null }
        p.play()
        playing = true
        if (!fromRetry) showStatus(getString(R.string.opening, ch.name))
        adapter.setCurrent(idx)
        showHud()
    }

    private fun onMainEvent(ev: MediaPlayer.Event) {
        if (BuildConfig.DEBUG && ev.type != MediaPlayer.Event.TimeChanged && ev.type != MediaPlayer.Event.PositionChanged &&
            ev.type != MediaPlayer.Event.Buffering) {
            android.util.Log.i("HomeRun", "event 0x" + Integer.toHexString(ev.type) + " vout=" + ev.voutCount +
                " audio=" + mp?.audioTracks?.joinToString { "${it.id}:${it.name}" } +
                " spu=" + mp?.spuTracks?.joinToString { "${it.id}:${it.name}" } + " cur=" + mp?.audioTrack + "/" + mp?.spuTrack)
        }
        when (ev.type) {
            MediaPlayer.Event.Playing -> {
                hideStatus()
                h.removeCallbacks(resetRetries)
                h.postDelayed(resetRetries, 15_000)
                scheduleTracks()
            }
            MediaPlayer.Event.Vout -> if (ev.voutCount > 0) hideStatus()
            MediaPlayer.Event.ESAdded -> scheduleTracks()
            MediaPlayer.Event.EncounteredError, MediaPlayer.Event.EndReached -> if (playing) scheduleRetry()
        }
    }

    private val resetRetries = Runnable { retries = 0 }
    private val retryRun = Runnable { if (playing && cur >= 0) play(cur, true) }

    /** Le flux a coupé : nouvelle tentative (délai croissant). */
    private fun scheduleRetry() {
        h.removeCallbacks(retryRun)
        if (retries >= 12) {
            showStatus(getString(R.string.cannot_play, channels.getOrNull(cur)?.name ?: ""))
            return
        }
        retries++
        showStatus(getString(R.string.retrying, retries))
        h.postDelayed(retryRun, 1500L * minOf(retries, 6))
    }

    private val tracksRun = Runnable { Tracks.applyPrefs(this, mp); Tracks.applyPrefs(this, pip) }
    private fun scheduleTracks() { h.removeCallbacks(tracksRun); h.postDelayed(tracksRun, 600) }

    private fun applyVolume() {
        val v = if (muted) 0 else prefs.volume
        mp?.setVolume(if (soundOnPip) 0 else v)
        pip?.setVolume(if (soundOnPip) v else 0)
        btnMute.text = getString(if (muted) R.string.btn_muted else R.string.btn_sound)
    }

    private fun changeVolume(delta: Int) {
        muted = false
        prefs.volume = (prefs.volume + delta).coerceIn(0, 150)
        vol.progress = prefs.volume
        applyVolume()
        flash(getString(R.string.volume_pct, prefs.volume))
    }

    // ------------------------------------------------------------------ image dans l'image (encart)

    private fun layoutPip() {
        findViewById<View>(R.id.root).addOnLayoutChangeListener { v, _, _, _, _, _, _, _, _ ->
            val w = (v.width * 0.32f).toInt().coerceAtLeast(200)
            val lp = pipBox.layoutParams as FrameLayout.LayoutParams
            val nh = w * 9 / 16
            val corner = prefs.sp.getInt("pip_corner", 3)
            val g = (if (corner % 2 == 0) Gravity.START else Gravity.END) or (if (corner < 2) Gravity.TOP else Gravity.BOTTOM)
            if (lp.width != w || lp.height != nh || lp.gravity != g) {
                lp.width = w; lp.height = nh; lp.gravity = g
                pipBox.post { pipBox.layoutParams = lp }
            }
        }
    }

    /** Garde panneau, barre de commandes et encart hors de l'encoche et des barres système. */
    private fun applySafeInsets() {
        val base = (16 * resources.displayMetrics.density).toInt()
        androidx.core.view.ViewCompat.setOnApplyWindowInsetsListener(findViewById(R.id.root)) { _, wi ->
            val i = wi.getInsetsIgnoringVisibility(WindowInsetsCompat.Type.systemBars() or WindowInsetsCompat.Type.displayCutout())
            panel.setPadding(i.left, i.top, 0, 0)
            controls.setPadding(i.left, 0, i.right, i.bottom)
            (pipBox.layoutParams as FrameLayout.LayoutParams).setMargins(base + i.left, base + i.top, base + i.right, base + i.bottom)
            (hud.layoutParams as FrameLayout.LayoutParams).setMargins(base + 8 + i.left, base + 8 + i.top, base, base)
            (recIndicator.layoutParams as FrameLayout.LayoutParams).setMargins(base, base + i.top, base + i.right, base)
            pipBox.requestLayout()
            wi
        }
    }

    private fun startPip(idx: Int) {
        if (idx !in channels.indices) return
        val lib = libVLC ?: return
        var p = pip
        if (p == null) {
            p = MediaPlayer(lib)
            // SurfaceView placée au-dessus de l'image principale (moins coûteux qu'une TextureView).
            val sid = resources.getIdentifier("surface_video", "id", packageName)
            if (sid != 0) pipVideo.findViewById<android.view.SurfaceView>(sid)?.setZOrderMediaOverlay(true)
            p.attachViews(pipVideo, null, false, false)
            p.setEventListener { ev ->
                when (ev.type) {
                    MediaPlayer.Event.Playing -> pipRetries = 0
                    MediaPlayer.Event.ESAdded -> if (soundOnPip) scheduleTracks()
                    MediaPlayer.Event.EncounteredError, MediaPlayer.Event.EndReached ->
                        if (pip != null && pipRetries++ < 8) h.postDelayed({ if (pip != null) startPip(pipIdx) }, 2000)
                }
            }
            pip = p
        }
        pipIdx = idx
        val m = newMedia(channels[idx].url)
        // petite image : on allège le décodage (filtre anti-blocs sauté)
        m.addOption(":avcodec-skiploopfilter=4")
        p.media = m
        m.release()
        p.play()
        pipBox.visibility = View.VISIBLE
        pipLabel.text = channels[idx].label
        applyVolume()
        updatePipBorder()
    }

    private fun stopPip() {
        val p = pip ?: return
        pip = null
        p.setEventListener(null)
        p.stop()
        p.detachViews()
        p.release()
        pipIdx = -1
        if (soundOnPip) { soundOnPip = false; applyVolume() }
        pipBox.visibility = View.GONE
    }

    /** Le son suit l'image touchée. */
    private fun setSound(onPip: Boolean) {
        if (onPip && pip == null) return
        soundOnPip = onPip
        applyVolume()
        updatePipBorder()
        scheduleTracks()
        flash(getString(if (onPip) R.string.pip_sound_pip else R.string.pip_sound_main))
    }

    private fun updatePipBorder() {
        pipBox.setBackgroundResource(if (soundOnPip) R.drawable.pip_border_active else R.drawable.pip_border)
    }

    private fun swapPip() {
        if (pip == null || cur < 0) return
        val a = cur
        val b = pipIdx
        play(b)
        startPip(a)
    }

    private fun pipMenu() {
        if (channels.isEmpty()) return
        if (pip == null) { chooseChannel(getString(R.string.pip_choose)) { startPip(it) }; return }
        val items = arrayOf(
            getString(R.string.pip_change), getString(R.string.pip_swap),
            getString(if (soundOnPip) R.string.pip_sound_to_main else R.string.pip_sound_to_pip),
            getString(R.string.pip_move), getString(R.string.pip_close),
        )
        AlertDialog.Builder(this).setTitle(R.string.pip_title).setItems(items) { _, w ->
            when (w) {
                0 -> chooseChannel(getString(R.string.pip_choose)) { startPip(it) }
                1 -> swapPip()
                2 -> setSound(!soundOnPip)
                3 -> { prefs.sp.edit().putInt("pip_corner", (prefs.sp.getInt("pip_corner", 3) + 1) % 4).apply(); findViewById<View>(R.id.root).requestLayout() }
                4 -> stopPip()
            }
        }.show()
    }

    private fun chooseChannel(title: String, cb: (Int) -> Unit) {
        AlertDialog.Builder(this).setTitle(title)
            .setItems(channels.map { it.label }.toTypedArray()) { _, w -> cb(w) }
            .setNegativeButton(android.R.string.cancel, null).show()
    }

    // ------------------------------------------------------------------ playlist, guide, mises à jour

    private fun loadPlaylist() {
        val url = prefs.playlistUrl
        loadedUrl = url
        showStatus(getString(R.string.loading_playlist))
        lifecycleScope.launch {
            val res = withContext(Dispatchers.IO) { runCatching { Playlist.load(url) } }
            res.onSuccess { l ->
                channels = l
                adapter.notifyDataSetChanged()
                if (l.isEmpty()) { showStatus(getString(R.string.playlist_empty)); showUi(true); return@onSuccess }
                hideStatus()
                var idx = if (prefs.resume) l.indexOfFirst { it.url == prefs.lastChannel } else -1
                if (idx < 0) idx = 0
                play(idx)
                refreshGuide(true)
                checkFirmware()
            }.onFailure { e ->
                showStatus(getString(R.string.playlist_error, e.message ?: e.javaClass.simpleName))
                showUi(false)
            }
        }
    }

    private var guideLoading = false
    private fun refreshGuide(force: Boolean) {
        if (!prefs.showGuide || guideLoading || channels.isEmpty()) return
        if (!force && !Guide.stale()) return
        guideLoading = true
        val base = prefs.tunerBase
        lifecycleScope.launch {
            val r = withContext(Dispatchers.IO) { runCatching { Guide.load(base) } }
            guideLoading = false
            if (r.isSuccess) { adapter.notifyDataSetChanged(); if (hud.visibility == View.VISIBLE) fillHud() }
        }
    }

    private val minuteTick = object : Runnable {
        override fun run() {
            adapter.notifyDataSetChanged()
            refreshGuide(false)
            h.postDelayed(this, 60_000)
        }
    }

    private fun checkFirmware() {
        if (firmwareTold) return
        val base = prefs.tunerBase
        lifecycleScope.launch {
            val up = withContext(Dispatchers.IO) {
                runCatching { JSONObject(Net.get("$base/discover.json")).optString("UpgradeAvailable", "") }.getOrDefault("")
            }
            if (up.isNotEmpty()) { firmwareTold = true; flash(getString(R.string.firmware_available, up)) }
        }
    }

    private fun checkUpdate() {
        lifecycleScope.launch {
            val info = withContext(Dispatchers.IO) { runCatching { Updater.check() }.getOrNull() } ?: return@launch
            if (!info.newer || info.apkUrl == null || prefs.skippedVersion == info.version) return@launch
            updBanner.text = getString(R.string.update_banner, info.version)
            updBanner.visibility = View.VISIBLE
            updBanner.setOnClickListener {
                startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(info.apkUrl)))
            }
            updBanner.setOnLongClickListener {
                prefs.skippedVersion = info.version
                updBanner.visibility = View.GONE
                true
            }
        }
    }

    // ------------------------------------------------------------------ interface

    private fun setupButtons() {
        findViewById<Button>(R.id.btnList).setOnClickListener { togglePanel() }
        findViewById<Button>(R.id.btnPrev).setOnClickListener { zap(-1) }
        findViewById<Button>(R.id.btnNext).setOnClickListener { zap(1) }
        btnMute.setOnClickListener { muted = !muted; applyVolume(); flash(getString(if (muted) R.string.muted else R.string.unmuted)) }
        vol.progress = prefs.volume
        vol.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(s: SeekBar?, v: Int, fromUser: Boolean) {
                if (!fromUser) return
                muted = false; prefs.volume = v; applyVolume(); bumpUi()
            }
            override fun onStartTrackingTouch(s: SeekBar?) {}
            override fun onStopTrackingTouch(s: SeekBar?) {}
        })
        findViewById<Button>(R.id.btnGuide).setOnClickListener { openGuide() }
        findViewById<Button>(R.id.btnLang).setOnClickListener { langMenu() }
        findViewById<Button>(R.id.btnRec).setOnClickListener { recMenu() }
        findViewById<Button>(R.id.btnPip).setOnClickListener { pipMenu() }
        findViewById<Button>(R.id.btnRecs).setOnClickListener { startActivity(Intent(this, RecordingsActivity::class.java)) }
        findViewById<Button>(R.id.btnFull).setOnClickListener { hideUi() }
        findViewById<Button>(R.id.btnSettings).setOnClickListener { startActivity(Intent(this, SettingsActivity::class.java)) }
        findViewById<View>(R.id.pipTouch).setOnClickListener { setSound(true) }
        findViewById<View>(R.id.pipTouch).setOnLongClickListener { pipMenu(); true }
    }

    @SuppressLint("ClickableViewAccessibility")
    private fun setupGestures() {
        val gd = GestureDetector(this, object : GestureDetector.SimpleOnGestureListener() {
            private var volAcc = 0f
            override fun onDown(e: MotionEvent): Boolean { volAcc = 0f; return true }
            override fun onSingleTapConfirmed(e: MotionEvent): Boolean {
                if (soundOnPip) setSound(false) else toggleUi()
                return true
            }
            override fun onDoubleTap(e: MotionEvent): Boolean { hideUi(); return true }
            override fun onFling(e1: MotionEvent?, e2: MotionEvent, vx: Float, vy: Float): Boolean {
                val s = e1 ?: return false
                val dx = e2.x - s.x
                val dy = e2.y - s.y
                if (abs(dx) > abs(dy) * 1.5f && abs(dx) > 120) { zap(if (dx < 0) 1 else -1); return true }
                return false
            }
            override fun onScroll(e1: MotionEvent?, e2: MotionEvent, dx: Float, dy: Float): Boolean {
                val s = e1 ?: return false
                // glisser verticalement sur la moitié droite : volume
                if (s.x > touch.width / 2 && abs(e2.y - s.y) > abs(e2.x - s.x)) {
                    volAcc += dy
                    val step = touch.height / 60f
                    if (abs(volAcc) >= step) {
                        changeVolume((volAcc / step).toInt() * 2)
                        volAcc %= step
                    }
                    return true
                }
                return false
            }
        })
        touch.setOnTouchListener { _, e -> gd.onTouchEvent(e) }
    }

    private fun zap(d: Int) {
        if (channels.isEmpty()) return
        val n = channels.size
        play(((if (cur < 0) 0 else cur) + d + n) % n)
    }

    private var uiVisible = false
    private val hideUiRun = Runnable { hideUi() }

    private fun showUi(focusList: Boolean) {
        uiVisible = true
        panel.visibility = View.VISIBLE
        controls.visibility = View.VISIBLE
        hud.visibility = View.GONE
        if (focusList) {
            val pos = cur.coerceAtLeast(0)
            (list.layoutManager as LinearLayoutManager).scrollToPositionWithOffset(pos, list.height / 3)
            list.post { list.findViewHolderForAdapterPosition(pos)?.itemView?.requestFocus() ?: list.requestFocus() }
        } else {
            findViewById<View>(R.id.btnList).requestFocus()
        }
        bumpUi()
    }

    private fun hideUi() {
        uiVisible = false
        h.removeCallbacks(hideUiRun)
        panel.visibility = View.GONE
        controls.visibility = View.GONE
        touch.requestFocus()
    }

    private fun toggleUi() = if (uiVisible) hideUi() else showUi(false)
    private fun togglePanel() {
        if (panel.visibility == View.VISIBLE) panel.visibility = View.GONE else showUi(true)
    }

    /** Masque l'interface après 10 s sans action. */
    private fun bumpUi() {
        h.removeCallbacks(hideUiRun)
        if (uiVisible && channels.isNotEmpty()) h.postDelayed(hideUiRun, 10_000)
    }

    private val hideHudRun = Runnable { hud.visibility = View.GONE }
    private fun showHud() {
        if (inSystemPip || cur !in channels.indices || panel.visibility == View.VISIBLE) return
        fillHud()
        hud.visibility = View.VISIBLE
        h.removeCallbacks(hideHudRun)
        h.postDelayed(hideHudRun, 5000)
    }

    private fun fillHud() {
        val ch = channels.getOrNull(cur) ?: return
        hudTitle.text = if (digits.isNotEmpty()) "${digits}_" else ch.label
        val p = Guide.at(ch)
        if (p != null) {
            hudProg.text = "${p.timeRange()}  ${p.title}" + (p.episode?.let { " — $it" } ?: "")
            hudProg.visibility = View.VISIBLE
            hudProgress.visibility = View.VISIBLE
            hudProgress.progress = ((nowEpoch() - p.start) * 1000 / (p.end - p.start)).toInt()
            val n = Guide.next(ch)
            hudNext.text = n?.let { getString(R.string.next_prog, hm(it.start), it.title) } ?: ""
            hudNext.visibility = if (n != null) View.VISIBLE else View.GONE
        } else {
            hudProg.visibility = View.GONE
            hudProgress.visibility = View.GONE
            hudNext.visibility = View.GONE
        }
    }

    private val hideFlashRun = Runnable { flashView.visibility = View.GONE }
    private fun flash(msg: String) {
        if (inSystemPip) return
        flashView.text = msg
        flashView.visibility = View.VISIBLE
        h.removeCallbacks(hideFlashRun)
        h.postDelayed(hideFlashRun, 3500)
    }

    private fun showStatus(msg: String) { status.text = msg; status.visibility = View.VISIBLE }
    private fun hideStatus() { status.visibility = View.GONE }

    private fun openGuide() {
        startActivity(Intent(this, GuideActivity::class.java).putExtra(GuideActivity.EXTRA_CURRENT, channels.getOrNull(cur)?.url))
    }

    private fun langMenu() {
        Tracks.showDialog(this, if (soundOnPip) pip else mp) { flash(it) }
    }

    // ------------------------------------------------------------------ enregistrement

    private fun recMenu() {
        val ch = channels.getOrNull(cur) ?: return
        if (Build.VERSION.SDK_INT >= 33 &&
            ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS), 1)
        }
        val recs = RecordService.active.value
        val mine = recs.filter { it.url == ch.url }
        val p = Guide.at(ch)
        val labels = ArrayList<String>()
        val acts = ArrayList<() -> Unit>()
        val margin = prefs.recMargin * 60L
        for (r in mine) {
            labels += getString(R.string.rec_stop_one, r.chName)
            acts += { RecordService.stop(this, r.id) }
        }
        if (p != null) {
            labels += getString(R.string.rec_until_end, p.title, hm(p.end + margin))
            acts += { startRec(ch, p.title, p.end + margin) }
        }
        for (m in intArrayOf(30, 60, 120, 180)) {
            labels += getString(R.string.rec_for, RecordService.fmtDur(m * 60L))
            acts += { startRec(ch, p?.title, nowEpoch() + m * 60L) }
        }
        labels += getString(R.string.rec_unlimited)
        acts += { startRec(ch, p?.title, 0) }
        labels += getString(R.string.recordings)
        acts += { startActivity(Intent(this, RecordingsActivity::class.java)) }
        AlertDialog.Builder(this).setTitle(getString(R.string.rec_title, ch.name))
            .setItems(labels.toTypedArray()) { _, w -> acts[w]() }
            .setNegativeButton(android.R.string.cancel, null).show()
    }

    private fun startRec(ch: Channel, title: String?, endAt: Long) {
        RecordService.start(this, ch, title, endAt)
        flash(if (endAt > 0) getString(R.string.rec_started_until, ch.name, hm(endAt)) else getString(R.string.rec_started, ch.name))
    }

    // ------------------------------------------------------------------ télécommande / clavier

    private val digitsRun = Runnable { goDigits() }

    private fun goDigits() {
        val d = digits
        digits = ""
        if (d.isEmpty()) return
        var idx = channels.indexOfFirst { it.num == d }
        if (idx < 0) idx = channels.indexOfFirst { it.num.startsWith(d) }
        if (idx >= 0) play(idx) else { flash(getString(R.string.no_channel_num, d)); fillHud() }
    }

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (event.action == KeyEvent.ACTION_DOWN) {
            bumpUi()
            if (handleKey(event.keyCode, event)) return true
        } else if (event.action == KeyEvent.ACTION_UP && isHandledKey(event.keyCode)) return true
        return super.dispatchKeyEvent(event)
    }

    private val ownKeys = setOf(
        KeyEvent.KEYCODE_CHANNEL_UP, KeyEvent.KEYCODE_CHANNEL_DOWN, KeyEvent.KEYCODE_PAGE_UP, KeyEvent.KEYCODE_PAGE_DOWN,
        KeyEvent.KEYCODE_GUIDE, KeyEvent.KEYCODE_CAPTIONS, KeyEvent.KEYCODE_MEDIA_AUDIO_TRACK, KeyEvent.KEYCODE_MEDIA_RECORD,
        KeyEvent.KEYCODE_PROG_RED, KeyEvent.KEYCODE_PROG_GREEN, KeyEvent.KEYCODE_PROG_YELLOW, KeyEvent.KEYCODE_PROG_BLUE,
        KeyEvent.KEYCODE_LAST_CHANNEL, KeyEvent.KEYCODE_G, KeyEvent.KEYCODE_L, KeyEvent.KEYCODE_V, KeyEvent.KEYCODE_U,
        KeyEvent.KEYCODE_R, KeyEvent.KEYCODE_P, KeyEvent.KEYCODE_M, KeyEvent.KEYCODE_F, KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE,
        KeyEvent.KEYCODE_S, KeyEvent.KEYCODE_SETTINGS, KeyEvent.KEYCODE_E,
    )

    private fun isHandledKey(k: Int) = k in ownKeys || isDigit(k) ||
        (!uiVisible && k in setOf(KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_DPAD_DOWN, KeyEvent.KEYCODE_DPAD_LEFT,
            KeyEvent.KEYCODE_DPAD_RIGHT, KeyEvent.KEYCODE_DPAD_CENTER, KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_MENU))

    private fun isDigit(k: Int) = k in KeyEvent.KEYCODE_0..KeyEvent.KEYCODE_9 || k in KeyEvent.KEYCODE_NUMPAD_0..KeyEvent.KEYCODE_NUMPAD_9

    private var lastCh = -1
    private fun handleKey(k: Int, e: KeyEvent): Boolean {
        if (isDigit(k)) {
            val d = if (k >= KeyEvent.KEYCODE_NUMPAD_0) k - KeyEvent.KEYCODE_NUMPAD_0 else k - KeyEvent.KEYCODE_0
            digits += d
            hudTitle.text = "${digits}_"
            hud.visibility = View.VISIBLE
            h.removeCallbacks(hideHudRun)
            h.postDelayed(hideHudRun, 4000)
            h.removeCallbacks(digitsRun)
            h.postDelayed(digitsRun, if (digits.length >= 3) 200 else 1500)
            return true
        }
        val prev = cur
        when (k) {
            KeyEvent.KEYCODE_CHANNEL_UP, KeyEvent.KEYCODE_PAGE_UP -> zap(1)
            KeyEvent.KEYCODE_CHANNEL_DOWN, KeyEvent.KEYCODE_PAGE_DOWN -> zap(-1)
            KeyEvent.KEYCODE_LAST_CHANNEL -> if (lastCh >= 0) play(lastCh)
            KeyEvent.KEYCODE_GUIDE, KeyEvent.KEYCODE_G, KeyEvent.KEYCODE_PROG_BLUE -> openGuide()
            KeyEvent.KEYCODE_CAPTIONS, KeyEvent.KEYCODE_U -> flash(Tracks.cycleSpu(this, if (soundOnPip) pip else mp) ?: getString(R.string.trk_none))
            KeyEvent.KEYCODE_MEDIA_AUDIO_TRACK, KeyEvent.KEYCODE_V -> flash(Tracks.cycleAudio(this, if (soundOnPip) pip else mp) ?: getString(R.string.trk_none))
            KeyEvent.KEYCODE_L, KeyEvent.KEYCODE_PROG_YELLOW -> langMenu()
            KeyEvent.KEYCODE_MEDIA_RECORD, KeyEvent.KEYCODE_R, KeyEvent.KEYCODE_PROG_RED -> recMenu()
            KeyEvent.KEYCODE_P, KeyEvent.KEYCODE_PROG_GREEN -> pipMenu()
            KeyEvent.KEYCODE_M -> btnMute.performClick()
            KeyEvent.KEYCODE_S, KeyEvent.KEYCODE_SETTINGS -> startActivity(Intent(this, SettingsActivity::class.java))
            KeyEvent.KEYCODE_E -> startActivity(Intent(this, RecordingsActivity::class.java))
            KeyEvent.KEYCODE_F -> toggleUi()
            KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE -> showHud()
            else -> {
                if (uiVisible) {
                    if (k == KeyEvent.KEYCODE_BACK || k == KeyEvent.KEYCODE_ESCAPE) { hideUi(); return true }
                    return false
                }
                when (k) {
                    KeyEvent.KEYCODE_DPAD_UP -> zap(1)
                    KeyEvent.KEYCODE_DPAD_DOWN -> zap(-1)
                    KeyEvent.KEYCODE_DPAD_LEFT -> showUi(true)
                    KeyEvent.KEYCODE_DPAD_RIGHT -> if (pip != null) setSound(!soundOnPip) else showHud()
                    KeyEvent.KEYCODE_DPAD_CENTER, KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_MENU -> showUi(false)
                    else -> return false
                }
            }
        }
        if (cur != prev && prev >= 0) lastCh = prev
        return true
    }

    // ------------------------------------------------------------------ liste des chaînes

    private inner class VH(v: View) : RecyclerView.ViewHolder(v) {
        val name: TextView = v.findViewById(R.id.name)
        val prog: TextView = v.findViewById(R.id.prog)
        val progress: ProgressBar = v.findViewById(R.id.progress)
    }

    private inner class ChannelAdapter : RecyclerView.Adapter<VH>() {
        private var current = -1
        fun setCurrent(i: Int) {
            val old = current
            current = i
            if (old >= 0) notifyItemChanged(old)
            notifyItemChanged(i)
        }
        override fun getItemCount() = channels.size
        override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): VH {
            val v = LayoutInflater.from(parent.context).inflate(R.layout.item_channel, parent, false)
            return VH(v)
        }
        override fun onBindViewHolder(vh: VH, pos: Int) {
            val ch = channels[pos]
            val rec = RecordService.isRecording(ch.url)
            vh.name.text = (if (rec) "● " else "") + ch.label
            vh.name.setTextColor(if (pos == current) ContextCompat.getColor(this@MainActivity, R.color.accent_light) else 0xFFFFFFFF.toInt())
            val p = if (prefs.showGuide) Guide.at(ch) else null
            if (p != null) {
                vh.prog.text = "${hm(p.start)}  ${p.title}"
                vh.prog.visibility = View.VISIBLE
                vh.progress.visibility = View.VISIBLE
                vh.progress.progress = ((nowEpoch() - p.start) * 1000 / (p.end - p.start)).toInt()
            } else {
                vh.prog.visibility = View.GONE
                vh.progress.visibility = View.GONE
            }
            vh.itemView.setOnClickListener {
                if (pos != cur) { lastCh = cur; play(pos) }
                bumpUi()
            }
            vh.itemView.setOnLongClickListener {
                AlertDialog.Builder(this@MainActivity).setTitle(ch.label)
                    .setItems(arrayOf(getString(R.string.ctx_play), getString(R.string.ctx_pip), getString(R.string.ctx_rec))) { _, w ->
                        when (w) {
                            0 -> play(pos)
                            1 -> startPip(pos)
                            2 -> { play(pos); recMenu() }
                        }
                    }.show()
                true
            }
        }
    }

    companion object {
        const val EXTRA_PLAY = "play_url"
    }
}
