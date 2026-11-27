import AppKit

/** Grille horaire : une ligne par chaîne, une case par programme (colonne et en-tête fixes). */
final class GuideGridView: NSView {
    var channels: [Channel] = []
    var guide = GuideData()
    var onPick: ((Channel, Prog) -> Void)?

    let colW: CGFloat = 170
    let headerH: CGFloat = 28
    let rowH: CGFloat = 48
    let pxPerMin: CGFloat = 4
    private(set) var t0 = 0
    private var t1 = 0

    override var isFlipped: Bool { true }

    func reload() {
        let now = nowEpoch()
        t0 = now / 1800 * 1800 - 1800
        t1 = max(guide.maxEnd, now + 3 * 3600)
        let w = colW + CGFloat(t1 - t0) / 60 * pxPerMin
        let h = headerH + CGFloat(channels.count) * rowH
        setFrameSize(NSSize(width: max(w, 600), height: max(h, 200)))
        needsDisplay = true
    }

    func x(of t: Int) -> CGFloat { colW + CGFloat(t - t0) / 60 * pxPerMin }

    private func draw(_ s: String, in r: NSRect, font: NSFont, color: NSColor) {
        let p = NSMutableParagraphStyle()
        p.lineBreakMode = .byTruncatingTail
        (s as NSString).draw(with: r, options: [.usesLineFragmentOrigin, .truncatesLastVisibleLine],
                             attributes: [.font: font, .foregroundColor: color, .paragraphStyle: p])
    }

    override func draw(_ dirtyRect: NSRect) {
        NSColor.windowBackgroundColor.setFill()
        dirtyRect.fill()
        let vis = visibleRect
        let now = nowEpoch()
        let small = NSFont.systemFont(ofSize: 11)
        let bold = NSFont.boldSystemFont(ofSize: 12)
        let first = max(0, Int((dirtyRect.minY - headerH) / rowH))
        let last = min(channels.count - 1, Int((dirtyRect.maxY - headerH) / rowH))
        if first <= last {
            for i in first...last {
                let ch = channels[i]
                let y = headerH + CGFloat(i) * rowH
                for p in guide.channels[ch.num]?.progs ?? [] where p.end > t0 && p.start < t1 {
                    let x0 = max(x(of: p.start), colW), x1 = x(of: p.end)
                    if x1 < dirtyRect.minX || x0 > dirtyRect.maxX { continue }
                    let r = NSRect(x: x0, y: y + 1, width: max(1, x1 - x0 - 1), height: rowH - 2)
                    let onAir = now >= p.start && now < p.end
                    let sched = Recorder.shared.isScheduled(url: ch.url, start: p.start)
                    (sched ? NSColor.systemRed.withAlphaComponent(0.25)
                        : onAir ? NSColor.controlAccentColor.withAlphaComponent(0.22)
                        : NSColor.controlBackgroundColor).setFill()
                    r.fill()
                    NSColor.separatorColor.setStroke()
                    NSBezierPath(rect: r).stroke()
                    let tx = max(r.minX, vis.minX + colW) + 4
                    let tr = NSRect(x: tx, y: r.minY + 4, width: max(0, r.maxX - tx - 4), height: 16)
                    draw((sched ? "● " : "") + p.title, in: tr, font: bold, color: .labelColor)
                    draw("\(hm(p.start)) – \(hm(p.end))", in: tr.offsetBy(dx: 0, dy: 18), font: small, color: .secondaryLabelColor)
                }
            }
        }
        // Heure actuelle
        let nx = x(of: now)
        NSColor.systemRed.setFill()
        NSRect(x: nx, y: vis.minY, width: 2, height: vis.height).fill()

        // En-tête des heures (fixe en haut)
        let hr = NSRect(x: vis.minX, y: vis.minY, width: vis.width, height: headerH)
        NSColor.underPageBackgroundColor.setFill()
        hr.fill()
        var t = t0
        while t < t1 {
            let xx = x(of: t)
            if xx > vis.minX + colW - 60 && xx < vis.maxX {
                draw(hm(t), in: NSRect(x: xx + 3, y: vis.minY + 7, width: 60, height: 16), font: small, color: .labelColor)
                NSColor.separatorColor.setFill()
                NSRect(x: xx, y: vis.minY, width: 1, height: headerH).fill()
            }
            t += 1800
        }
        // Colonne des chaînes (fixe à gauche)
        let cr = NSRect(x: vis.minX, y: vis.minY, width: colW, height: vis.height)
        NSColor.underPageBackgroundColor.setFill()
        cr.fill()
        if first <= last {
            for i in first...last {
                let y = headerH + CGFloat(i) * rowH
                if y + rowH < vis.minY + headerH { continue }
                draw(channels[i].label, in: NSRect(x: vis.minX + 8, y: y + 15, width: colW - 12, height: 18), font: bold, color: .labelColor)
                NSColor.separatorColor.setFill()
                NSRect(x: vis.minX, y: y + rowH - 1, width: colW, height: 1).fill()
            }
        }
        NSColor.underPageBackgroundColor.setFill()
        NSRect(x: vis.minX, y: vis.minY, width: colW, height: headerH).fill()
    }

    override func mouseDown(with event: NSEvent) {
        let p = convert(event.locationInWindow, from: nil)
        let vis = visibleRect
        guard p.x > vis.minX + colW, p.y > vis.minY + headerH else { return }
        let row = Int((p.y - headerH) / rowH)
        guard row >= 0, row < channels.count else { return }
        let t = t0 + Int((p.x - colW) / pxPerMin * 60)
        let ch = channels[row]
        if let prog = guide.channels[ch.num]?.progs.first(where: { t >= $0.start && t < $0.end }) {
            onPick?(ch, prog)
        }
    }
}

/** Fenêtre du guide TV (grille ; une case → détails, Regarder, Enregistrer). (↔ src/guide_ui.h) */
final class GuideWindowController: NSWindowController {
    private let grid = GuideGridView(frame: .zero)
    private let scroll = NSScrollView()
    private let status = UI.label("", size: 12)

    init() {
        let w = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 1000, height: 640),
                         styleMask: [.titled, .closable, .miniaturizable, .resizable], backing: .buffered, defer: false)
        w.title = L("guide_title")
        w.isReleasedWhenClosed = false
        super.init(window: w)
        guard let cv = w.contentView else { return }
        let top = NSStackView()
        top.orientation = .horizontal
        top.spacing = 8
        top.edgeInsets = NSEdgeInsets(top: 8, left: 10, bottom: 8, right: 10)
        top.translatesAutoresizingMaskIntoConstraints = false
        top.addArrangedSubview(UI.button(L("guide_now"), self, #selector(scrollToNow)))
        top.addArrangedSubview(UI.button(L("guide_refresh"), self, #selector(refresh)))
        top.addArrangedSubview(status)
        status.textColor = .secondaryLabelColor
        scroll.documentView = grid
        scroll.hasVerticalScroller = true
        scroll.hasHorizontalScroller = true
        scroll.translatesAutoresizingMaskIntoConstraints = false
        scroll.contentView.postsBoundsChangedNotifications = true
        NotificationCenter.default.addObserver(self, selector: #selector(scrolled),
                                               name: NSView.boundsDidChangeNotification, object: scroll.contentView)
        cv.addSubview(top)
        cv.addSubview(scroll)
        NSLayoutConstraint.activate([
            top.leadingAnchor.constraint(equalTo: cv.leadingAnchor),
            top.trailingAnchor.constraint(equalTo: cv.trailingAnchor),
            top.topAnchor.constraint(equalTo: cv.topAnchor),
            scroll.leadingAnchor.constraint(equalTo: cv.leadingAnchor),
            scroll.trailingAnchor.constraint(equalTo: cv.trailingAnchor),
            scroll.topAnchor.constraint(equalTo: top.bottomAnchor),
            scroll.bottomAnchor.constraint(equalTo: cv.bottomAnchor),
        ])
        grid.onPick = { [weak self] ch, p in self?.pick(ch, p) }
        w.center()
    }

    required init?(coder: NSCoder) { fatalError() }

    private var mw: MainWindowController { AppDelegate.shared.mainWC }

    @objc private func scrolled() { grid.needsDisplay = true }

    func showAndLoad() {
        showWindow(nil)
        window?.makeKeyAndOrderFront(nil)
        fill()
        if mw.guideData.channels.isEmpty || mw.guideData.stale { load(force: true) }
    }

    private func fill() {
        grid.channels = mw.channels
        grid.guide = mw.guideData
        grid.reload()
        if mw.guideData.channels.isEmpty {
            status.stringValue = mw.guideError ?? ""
        } else {
            status.stringValue = ""
        }
    }

    private func load(force: Bool) {
        status.stringValue = L("guide_loading")
        mw.loadGuide(force: force) { [weak self] in
            guard let self = self else { return }
            self.fill()
            self.scrollToNow()
        }
    }

    @objc private func refresh() { load(force: true) }

    @objc func scrollToNow() {
        let x = max(0, grid.x(of: nowEpoch()) - grid.colW - 60)
        scroll.contentView.scroll(to: NSPoint(x: x, y: scroll.contentView.bounds.minY))
        scroll.reflectScrolledClipView(scroll.contentView)
        grid.needsDisplay = true
    }

    /** Détails d'un programme : Regarder, Enregistrer (en cours) ou programmer (à venir). */
    private func pick(_ ch: Channel, _ p: Prog) {
        let now = nowEpoch()
        var info = "\(ch.label)\n\(hm(p.start)) – \(hm(p.end))"
        if let e = p.episode { info += "\n\(e)" }
        if let s = p.synopsis { info += "\n\n\(s)" }
        let future = p.start > now
        let sched = Recorder.shared.schedules.first { $0.chURL == ch.url && $0.start == p.start }
        var buttons = [L("guide_watch")]
        if p.end > now {
            buttons.append(sched != nil ? L("rec_cancel_sched") : future ? L("guide_record_sched") : L("guide_record"))
        }
        buttons.append(L("close"))
        if future && sched == nil { info += "\n\n" + L("rec_sched_note") }
        let r = UI.alert(p.title, info, buttons: buttons)
        if r == 0 {
            mw.play(ch)
            mw.window?.makeKeyAndOrderFront(nil)
        } else if r == 1 && p.end > now {
            if let s = sched {
                Recorder.shared.cancel(s)
            } else if future {
                Recorder.shared.schedule(Sched(chName: ch.name, chURL: ch.url, title: p.title, start: p.start, end: p.end))
                mw.showOSD(L("rec_scheduled_osd", p.title, hm(p.start)))
            } else {
                mw.startRecording(ch, title: p.title, endAt: p.end + Prefs.recMargin * 60)
            }
            grid.needsDisplay = true
        }
    }
}
