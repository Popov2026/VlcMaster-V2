package fr.popov.homerun

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.view.View
import android.widget.Button
import android.widget.ProgressBar
import android.widget.TextView
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject

/**
 * Tuner HDHomeRun : modèle, firmware, mise à jour disponible (discover.json),
 * scan des chaînes (lineup.post?scan=start, suivi par lineup_status.json).  (↔ src/tuner.h)
 */
class TunerActivity : AppCompatActivity() {
    private lateinit var info: TextView
    private lateinit var scanStatus: TextView
    private lateinit var progress: ProgressBar
    private lateinit var btnScan: Button
    private lateinit var btnAbort: Button
    private lateinit var btnWeb: Button
    private var base = ""
    private var source = "Antenna"
    private var poll: Job? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_tuner)
        title = getString(R.string.tuner_title)
        info = findViewById(R.id.info)
        scanStatus = findViewById(R.id.scanStatus)
        progress = findViewById(R.id.progress)
        btnScan = findViewById(R.id.btnScan)
        btnAbort = findViewById(R.id.btnAbort)
        btnWeb = findViewById(R.id.btnWeb)
        base = Prefs.get(this).tunerBase
        btnScan.setOnClickListener { confirmScan() }
        btnAbort.setOnClickListener { abortScan() }
        btnWeb.setOnClickListener { startActivity(Intent(Intent.ACTION_VIEW, Uri.parse("$base/"))) }
        findViewById<Button>(R.id.btnRefresh).setOnClickListener { loadInfo() }
        loadInfo()
    }

    private fun loadInfo() {
        info.text = getString(R.string.tuner_loading, base)
        lifecycleScope.launch {
            val r = withContext(Dispatchers.IO) {
                runCatching {
                    val d = JSONObject(Net.get("$base/discover.json"))
                    val s = runCatching { JSONObject(Net.get("$base/lineup_status.json")) }.getOrNull()
                    d to s
                }
            }
            r.onSuccess { (d, s) ->
                val sb = StringBuilder()
                fun line(label: Int, v: String?) { if (!v.isNullOrEmpty()) sb.append(getString(label)).append(" : ").append(v).append('\n') }
                line(R.string.tuner_name, d.optString("FriendlyName"))
                line(R.string.tuner_model, d.optString("ModelNumber"))
                line(R.string.tuner_id, d.optString("DeviceID"))
                line(R.string.tuner_count, d.optInt("TunerCount").takeIf { it > 0 }?.toString())
                line(R.string.tuner_fw, (d.optString("FirmwareName") + " " + d.optString("FirmwareVersion")).trim())
                val up = d.optString("UpgradeAvailable")
                sb.append('\n')
                sb.append(if (up.isNotEmpty()) getString(R.string.tuner_upgrade, up) else getString(R.string.tuner_up_to_date))
                s?.optString("Source")?.takeIf { it.isNotEmpty() }?.let { source = it }
                info.text = sb.toString().trim()
                btnWeb.visibility = View.VISIBLE
                if (s?.optInt("ScanInProgress") == 1) startPolling()
            }.onFailure { e ->
                info.text = getString(R.string.tuner_unreachable, base, e.message ?: e.javaClass.simpleName)
            }
        }
    }

    private fun confirmScan() {
        AlertDialog.Builder(this).setTitle(R.string.scan_title)
            .setMessage(R.string.scan_confirm)
            .setPositiveButton(R.string.scan_start) { _, _ -> startScan() }
            .setNegativeButton(android.R.string.cancel, null).show()
    }

    private fun startScan() {
        lifecycleScope.launch {
            val r = withContext(Dispatchers.IO) { runCatching { Net.post("$base/lineup.post?scan=start&source=" + Uri.encode(source)) } }
            r.onSuccess { startPolling() }
                .onFailure { e -> scanStatus.text = getString(R.string.scan_failed, e.message ?: "") }
        }
    }

    private fun abortScan() {
        lifecycleScope.launch {
            withContext(Dispatchers.IO) { runCatching { Net.post("$base/lineup.post?scan=abort") } }
        }
    }

    private fun startPolling() {
        poll?.cancel()
        btnScan.isEnabled = false
        btnAbort.visibility = View.VISIBLE
        progress.visibility = View.VISIBLE
        poll = lifecycleScope.launch {
            var seen = false
            while (isActive) {
                val s = withContext(Dispatchers.IO) { runCatching { JSONObject(Net.get("$base/lineup_status.json")) }.getOrNull() }
                if (s != null) {
                    val inProg = s.optInt("ScanInProgress") == 1
                    if (inProg) {
                        seen = true
                        val p = s.optInt("Progress")
                        progress.progress = p
                        scanStatus.text = getString(R.string.scan_progress, p, s.optInt("Found"))
                    } else {
                        if (seen) {
                            scanStatus.text = getString(R.string.scan_done)
                            // la playlist sera relue au retour sur l'écran principal
                            Prefs.get(this@TunerActivity).sp.edit().putLong("scan_done_at", System.currentTimeMillis()).apply()
                        }
                        break
                    }
                }
                delay(1000)
            }
            btnScan.isEnabled = true
            btnAbort.visibility = View.GONE
            progress.visibility = View.GONE
        }
    }
}
