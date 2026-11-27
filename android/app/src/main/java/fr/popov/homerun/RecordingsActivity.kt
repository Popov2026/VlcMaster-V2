package fr.popov.homerun

import android.content.Intent
import android.os.Bundle
import android.text.format.DateFormat
import android.text.format.Formatter
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import kotlinx.coroutines.launch
import java.io.File
import java.util.Date

/** Liste des enregistrements : en cours (arrêter) et terminés (lire, supprimer). */
class RecordingsActivity : AppCompatActivity() {
    private sealed class Item {
        class Live(val r: RecordService.Rec) : Item()
        class Done(val f: File) : Item()
    }

    private var items: List<Item> = emptyList()
    private lateinit var empty: TextView
    private val adapter = Adapter()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_list)
        title = getString(R.string.recordings)
        empty = findViewById(R.id.empty)
        val list = findViewById<RecyclerView>(R.id.list)
        list.layoutManager = LinearLayoutManager(this)
        list.adapter = adapter
        findViewById<TextView>(R.id.footer).text = getString(R.string.rec_folder, RecordService.dir(this).absolutePath)
        lifecycleScope.launch { RecordService.active.collect { refresh() } }
    }

    override fun onResume() {
        super.onResume()
        refresh()
    }

    private fun refresh() {
        val live = RecordService.active.value
        val liveFiles = live.map { it.file.absolutePath }.toSet()
        val files = RecordService.dir(this).listFiles { f -> f.isFile && f.name.endsWith(".ts", true) }.orEmpty()
            .filter { it.absolutePath !in liveFiles }.sortedByDescending { it.lastModified() }
        items = live.map { Item.Live(it) } + files.map { Item.Done(it) }
        empty.visibility = if (items.isEmpty()) View.VISIBLE else View.GONE
        adapter.notifyDataSetChanged()
    }

    private fun confirmDelete(f: File) {
        AlertDialog.Builder(this).setTitle(R.string.rec_delete_title)
            .setMessage(getString(R.string.rec_delete_msg, f.nameWithoutExtension))
            .setPositiveButton(R.string.delete) { _, _ ->
                f.delete()
                RecPlayerActivity.forget(this, f)
                refresh()
            }
            .setNegativeButton(android.R.string.cancel, null).show()
    }

    private class VH(v: View) : RecyclerView.ViewHolder(v) {
        val t1: TextView = v.findViewById(R.id.t1)
        val t2: TextView = v.findViewById(R.id.t2)
    }

    private inner class Adapter : RecyclerView.Adapter<VH>() {
        override fun getItemCount() = items.size
        override fun onCreateViewHolder(p: ViewGroup, vt: Int) =
            VH(LayoutInflater.from(p.context).inflate(R.layout.item_two_lines, p, false))

        override fun onBindViewHolder(vh: VH, pos: Int) {
            val ctx = this@RecordingsActivity
            when (val it = items[pos]) {
                is Item.Live -> {
                    val r = it.r
                    vh.t1.text = "● " + r.file.nameWithoutExtension
                    val end = if (r.endAt > 0) getString(R.string.rec_until, hm(r.endAt)) else getString(R.string.rec_no_limit)
                    vh.t2.text = getString(R.string.rec_live_line, Formatter.formatShortFileSize(ctx, r.bytes), end)
                    vh.itemView.setOnClickListener {
                        AlertDialog.Builder(ctx).setTitle(r.file.nameWithoutExtension)
                            .setMessage(R.string.rec_stop_confirm)
                            .setPositiveButton(R.string.rec_stop) { _, _ -> RecordService.stop(ctx, r.id) }
                            .setNegativeButton(android.R.string.cancel, null).show()
                    }
                    vh.itemView.setOnLongClickListener(null)
                }
                is Item.Done -> {
                    val f = it.f
                    vh.t1.text = f.nameWithoutExtension
                    val pos2 = RecPlayerActivity.savedPos(ctx, f)
                    val date = DateFormat.getMediumDateFormat(ctx).format(Date(f.lastModified())) + " " +
                        DateFormat.getTimeFormat(ctx).format(Date(f.lastModified()))
                    vh.t2.text = date + " · " + Formatter.formatShortFileSize(ctx, f.length()) +
                        (if (pos2 > 0) " · " + getString(R.string.rec_resume_at, RecPlayerActivity.fmt(pos2)) else "")
                    vh.itemView.setOnClickListener {
                        startActivity(Intent(ctx, RecPlayerActivity::class.java).putExtra(RecPlayerActivity.EXTRA_PATH, f.absolutePath))
                    }
                    vh.itemView.setOnLongClickListener {
                        AlertDialog.Builder(ctx).setTitle(f.nameWithoutExtension)
                            .setItems(arrayOf(getString(R.string.rec_play), getString(R.string.rec_play_start), getString(R.string.delete))) { _, w ->
                                when (w) {
                                    0 -> vh.itemView.performClick()
                                    1 -> { RecPlayerActivity.forget(ctx, f); vh.itemView.performClick() }
                                    2 -> confirmDelete(f)
                                }
                            }.show()
                        true
                    }
                }
            }
        }
    }
}
