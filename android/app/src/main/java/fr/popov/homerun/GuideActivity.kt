package fr.popov.homerun

import android.content.Intent
import android.os.Bundle
import android.view.View
import android.widget.TextView
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/** Guide TV : grille horaire ; toucher une case → détails, « Regarder », « Enregistrer ». */
class GuideActivity : AppCompatActivity() {
    private lateinit var grid: GuideGridView
    private lateinit var info: TextView
    private var channels: List<Channel> = emptyList()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_guide)
        title = getString(R.string.guide_title)
        grid = findViewById(R.id.grid)
        info = findViewById(R.id.info)
        grid.onPick = { ch, p -> pick(ch, p) }
        load(false)
    }

    private fun load(force: Boolean) {
        val prefs = Prefs.get(this)
        info.visibility = View.VISIBLE
        info.text = getString(R.string.guide_loading)
        lifecycleScope.launch {
            val r = withContext(Dispatchers.IO) {
                runCatching {
                    val l = Playlist.load(prefs.playlistUrl)
                    if (force || Guide.stale()) Guide.load(prefs.tunerBase)
                    l
                }
            }
            r.onSuccess { l ->
                channels = l
                info.visibility = View.GONE
                grid.setData(l, intent.getStringExtra(EXTRA_CURRENT))
                grid.requestFocus()
            }.onFailure { e ->
                info.text = when (e) {
                    is Guide.GuideException -> getString(e.kind, e.message)
                    else -> getString(R.string.guide_err_service, e.message ?: e.javaClass.simpleName)
                }
                if (channels.isEmpty() && Guide.channels.isNotEmpty()) grid.setData(channels, null)
            }
        }
    }

    private fun pick(ch: Channel, p: Prog?) {
        val now = nowEpoch()
        val b = AlertDialog.Builder(this)
        if (p == null) {
            b.setTitle(ch.label).setMessage(R.string.guide_no_data)
        } else {
            val msg = StringBuilder()
            msg.append(p.timeRange())
            p.episode?.let { msg.append("\n").append(it) }
            p.synopsis?.let { msg.append("\n\n").append(it) }
            b.setTitle("${ch.label} — ${p.title}").setMessage(msg)
            if (now >= p.start && now < p.end) {
                b.setNeutralButton(R.string.guide_record) { _, _ ->
                    val end = p.end + Prefs.get(this).recMargin * 60L
                    RecordService.start(this, ch, p.title, end)
                    info.visibility = View.VISIBLE
                    info.text = getString(R.string.rec_started_until, ch.name, hm(end))
                }
            }
        }
        b.setPositiveButton(R.string.guide_watch) { _, _ ->
            startActivity(Intent(this, MainActivity::class.java).putExtra(MainActivity.EXTRA_PLAY, ch.url)
                .addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_SINGLE_TOP))
            finish()
        }
        b.setNegativeButton(android.R.string.cancel, null).show()
    }

    override fun onCreateOptionsMenu(menu: android.view.Menu): Boolean {
        menu.add(0, 1, 0, R.string.guide_refresh)
        return true
    }

    override fun onOptionsItemSelected(item: android.view.MenuItem): Boolean {
        if (item.itemId == 1) { load(true); return true }
        return super.onOptionsItemSelected(item)
    }

    companion object { const val EXTRA_CURRENT = "current" }
}
