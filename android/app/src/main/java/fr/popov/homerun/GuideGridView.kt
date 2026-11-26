package fr.popov.homerun

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.RectF
import android.text.TextPaint
import android.text.TextUtils
import android.util.AttributeSet
import android.view.GestureDetector
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import android.widget.OverScroller
import androidx.core.content.ContextCompat
import kotlin.math.max
import kotlin.math.min

/**
 * Grille horaire du guide : une ligne par chaîne, une case par programme.
 * Défilement au doigt (deux axes) ou au D-pad (case sélectionnée), colonne des
 * chaînes et réglette des heures fixes, trait rouge sur l'heure actuelle.
 *                                                        (↔ src/guide_ui.h)
 */
class GuideGridView @JvmOverloads constructor(ctx: Context, attrs: AttributeSet? = null) : View(ctx, attrs) {

    var channels: List<Channel> = emptyList()
    var onPick: (Channel, Prog?) -> Unit = { _, _ -> }

    private val d = resources.displayMetrics.density
    private val rowH = 64 * d
    private val headH = 36 * d
    private val chW = 150 * d
    private val pxPerSec = 5 * d / 60f            // 5 dp par minute
    private var t0 = 0L                           // début de la grille (demi-heure précédente)
    private var t1 = 0L

    private var sx = 0f
    private var sy = 0f
    private var selRow = 0
    private var selTime = 0L                      // instant visé dans la ligne sélectionnée

    private val bg = Paint().apply { color = 0xFF14181C.toInt() }
    private val cell = Paint().apply { color = 0xFF232A31.toInt() }
    private val cellNow = Paint().apply { color = 0xFF2C3A48.toInt() }
    private val cellSel = Paint().apply { color = ContextCompat.getColor(ctx, R.color.accent) }
    private val head = Paint().apply { color = 0xFF0C0F12.toInt() }
    private val line = Paint().apply { color = 0xFFE53935.toInt(); strokeWidth = 2 * d }
    private val tick = Paint().apply { color = 0xFF3A444E.toInt(); strokeWidth = d }
    private val txt = TextPaint(Paint.ANTI_ALIAS_FLAG).apply { color = 0xFFFFFFFF.toInt(); textSize = 15 * d }
    private val txt2 = TextPaint(Paint.ANTI_ALIAS_FLAG).apply { color = 0xFFA8B0B8.toInt(); textSize = 12 * d }
    private val curCh = TextPaint(Paint.ANTI_ALIAS_FLAG).apply { color = ContextCompat.getColor(ctx, R.color.accent_light); textSize = 15 * d; isFakeBoldText = true }
    private val r = RectF()
    private var currentUrl: String? = null

    private val scroller = OverScroller(ctx)

    init {
        isFocusable = true
        isFocusableInTouchMode = true
    }

    fun setData(list: List<Channel>, current: String?) {
        channels = list
        currentUrl = current
        val now = nowEpoch()
        t0 = now - now % 1800 - 1800
        t1 = max(Guide.maxEnd, now + 3 * 3600)
        selRow = max(0, list.indexOfFirst { it.url == current })
        selTime = now
        post {
            sx = clampX(((now - t0) * pxPerSec) - (width - chW) / 4)
            sy = clampY(selRow * rowH - (height - headH) / 3)
            invalidate()
        }
    }

    private fun clampX(x: Float) = x.coerceIn(0f, max(0f, (t1 - t0) * pxPerSec - (width - chW)))
    private fun clampY(y: Float) = y.coerceIn(0f, max(0f, channels.size * rowH - (height - headH)))

    private fun progsOf(i: Int): List<Prog> = channels.getOrNull(i)?.let { Guide.forChannel(it)?.progs } ?: emptyList()

    private fun selected(): Prog? = progsOf(selRow).firstOrNull { selTime >= it.start && selTime < it.end }

    override fun onDraw(c: Canvas) {
        c.drawRect(0f, 0f, width.toFloat(), height.toFloat(), bg)
        val now = nowEpoch()
        val first = max(0, (sy / rowH).toInt())
        val last = min(channels.size - 1, ((sy + height - headH) / rowH).toInt())
        val tLeft = t0 + (sx / pxPerSec).toLong()
        val tRight = tLeft + ((width - chW) / pxPerSec).toLong()

        // cases des programmes
        c.save()
        c.clipRect(chW, headH, width.toFloat(), height.toFloat())
        for (i in first..last) {
            val y = headH + i * rowH - sy
            for (p in progsOf(i)) {
                if (p.end < tLeft || p.start > tRight) continue
                val x0 = chW + (p.start - t0) * pxPerSec - sx
                val x1 = chW + (p.end - t0) * pxPerSec - sx
                r.set(x0 + d, y + d, x1 - d, y + rowH - d)
                val isSel = hasFocus() && i == selRow && selTime >= p.start && selTime < p.end
                c.drawRect(r, if (isSel) cellSel else if (now >= p.start && now < p.end) cellNow else cell)
                val tx = max(x0, chW) + 8 * d
                val avail = x1 - tx - 6 * d
                if (avail > 12 * d) {
                    c.drawText(TextUtils.ellipsize(p.title, txt, avail, TextUtils.TruncateAt.END).toString(), tx, y + 26 * d, txt)
                    val sub = hm(p.start) + (p.episode?.let { "  $it" } ?: "")
                    c.drawText(TextUtils.ellipsize(sub, txt2, avail, TextUtils.TruncateAt.END).toString(), tx, y + 48 * d, txt2)
                }
            }
            if (progsOf(i).isEmpty()) {
                c.drawText(context.getString(R.string.guide_no_data), chW + 8 * d, y + 38 * d, txt2)
            }
        }
        val xn = chW + (now - t0) * pxPerSec - sx
        if (xn > chW) c.drawLine(xn, headH, xn, height.toFloat(), line)
        c.restore()

        // colonne des chaînes
        c.drawRect(0f, headH, chW, height.toFloat(), head)
        c.save()
        c.clipRect(0f, headH, chW, height.toFloat())
        for (i in first..last) {
            val y = headH + i * rowH - sy
            val ch = channels[i]
            val p = if (ch.url == currentUrl) curCh else txt
            c.drawText(ch.num, 10 * d, y + 26 * d, p)
            c.drawText(TextUtils.ellipsize(ch.name, txt2, chW - 20 * d, TextUtils.TruncateAt.END).toString(), 10 * d, y + 48 * d, txt2)
            c.drawLine(0f, y + rowH, chW, y + rowH, tick)
        }
        c.restore()

        // réglette des heures (toutes les 30 min)
        c.drawRect(0f, 0f, width.toFloat(), headH, head)
        c.save()
        c.clipRect(chW, 0f, width.toFloat(), headH)
        var t = tLeft - tLeft % 1800
        while (t <= tRight) {
            val x = chW + (t - t0) * pxPerSec - sx
            c.drawLine(x, headH - 8 * d, x, headH, tick)
            c.drawText(hm(t), x + 4 * d, headH - 12 * d, txt2)
            t += 1800
        }
        c.restore()
    }

    // ---------------------------------------------------------------- toucher

    private val gd = GestureDetector(ctx, object : GestureDetector.SimpleOnGestureListener() {
        override fun onDown(e: MotionEvent): Boolean { scroller.forceFinished(true); return true }
        override fun onScroll(e1: MotionEvent?, e2: MotionEvent, dx: Float, dy: Float): Boolean {
            sx = clampX(sx + dx); sy = clampY(sy + dy); invalidate(); return true
        }
        override fun onFling(e1: MotionEvent?, e2: MotionEvent, vx: Float, vy: Float): Boolean {
            scroller.fling(sx.toInt(), sy.toInt(), -vx.toInt(), -vy.toInt(), 0, clampX(Float.MAX_VALUE).toInt(), 0, clampY(Float.MAX_VALUE).toInt())
            postInvalidateOnAnimation(); return true
        }
        override fun onSingleTapUp(e: MotionEvent): Boolean {
            if (e.y < headH) return false
            val row = ((e.y - headH + sy) / rowH).toInt()
            val ch = channels.getOrNull(row) ?: return false
            if (e.x < chW) { onPick(ch, null); return true }
            val t = t0 + ((e.x - chW + sx) / pxPerSec).toLong()
            selRow = row; selTime = t
            onPick(ch, progsOf(row).firstOrNull { t >= it.start && t < it.end })
            return true
        }
    })

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean = gd.onTouchEvent(e) || super.onTouchEvent(e)

    override fun computeScroll() {
        if (scroller.computeScrollOffset()) {
            sx = clampX(scroller.currX.toFloat()); sy = clampY(scroller.currY.toFloat())
            postInvalidateOnAnimation()
        }
    }

    // ---------------------------------------------------------------- D-pad

    override fun onKeyDown(k: Int, e: KeyEvent): Boolean {
        val progs = progsOf(selRow)
        val p = selected()
        when (k) {
            KeyEvent.KEYCODE_DPAD_UP -> if (selRow > 0) selRow-- else return false
            KeyEvent.KEYCODE_DPAD_DOWN -> if (selRow < channels.size - 1) selRow++ else return false
            KeyEvent.KEYCODE_DPAD_RIGHT -> {
                val n = if (p != null) progs.firstOrNull { it.start >= p.end } else progs.firstOrNull { it.start > selTime }
                selTime = n?.start ?: (selTime + 1800)
            }
            KeyEvent.KEYCODE_DPAD_LEFT -> {
                val pr = if (p != null) progs.lastOrNull { it.end <= p.start } else progs.lastOrNull { it.end <= selTime }
                selTime = max(t0, pr?.start ?: (selTime - 1800))
            }
            KeyEvent.KEYCODE_CHANNEL_UP, KeyEvent.KEYCODE_PAGE_UP -> selRow = max(0, selRow - 8)
            KeyEvent.KEYCODE_CHANNEL_DOWN, KeyEvent.KEYCODE_PAGE_DOWN -> selRow = min(channels.size - 1, selRow + 8)
            KeyEvent.KEYCODE_DPAD_CENTER, KeyEvent.KEYCODE_ENTER -> {
                channels.getOrNull(selRow)?.let { onPick(it, p) }; return true
            }
            else -> return super.onKeyDown(k, e)
        }
        // garder la case visible (programme sélectionné, sinon l'instant visé)
        val q = selected()
        val y = selRow * rowH
        if (y < sy) sy = clampY(y)
        if (y + rowH > sy + height - headH) sy = clampY(y + rowH - (height - headH))
        val xs = ((q?.start ?: selTime) - t0) * pxPerSec
        val xe = ((q?.end ?: selTime) - t0) * pxPerSec
        if (xs < sx) sx = clampX(xs)
        else if (xe > sx + width - chW && xs > sx + (width - chW) / 2) sx = clampX(xs - 40 * d)
        // garder l'instant visé dans la case affichée quand on change de ligne
        if (k == KeyEvent.KEYCODE_DPAD_RIGHT || k == KeyEvent.KEYCODE_DPAD_LEFT) selTime = max(selTime, t0)
        q?.let { if (selTime < it.start || selTime >= it.end) selTime = it.start }
        invalidate()
        return true
    }
}
