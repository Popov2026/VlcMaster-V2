import AppKit

/** Ligne de la liste : enregistrement en cours, programmé, ou fichier .ts. */
private enum RecRow {
    case live(Rec)
    case sched(Sched)
    case file(URL, Int64, Date)
}

/**
 * Liste des enregistrements : en cours (arrêter), programmés (annuler), fichiers
 * (lire avec reprise, lire depuis le début, mettre à la corbeille).   (↔ src/record.h)
 */
final class RecordingsWindowController: NSWindowController, NSTableViewDataSource, NSTableViewDelegate {
    private let table = NSTableView()
    private var rows: [RecRow] = []
    private let folder = UI.label("", size: 11)
    private var playBtn: NSButton!
    private var startBtn: NSButton!
    private var stopBtn: NSButton!
    private var delBtn: NSButton!

    init() {
        let w = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 760, height: 460),
                         styleMask: [.titled, .closable, .miniaturizable, .resizable], backing: .buffered, defer: false)
        w.title = L("recordings")
        w.isReleasedWhenClosed = false
        super.init(window: w)
        guard let cv = w.contentView else { return }
        for (id, title, width) in [("name", L("recordings"), 470.0), ("info", "", 220.0)] {
            let c = NSTableColumn(identifier: NSUserInterfaceItemIdentifier(id))
            c.title = title
            c.width = CGFloat(width)
            table.addTableColumn(c)
        }
        table.headerView = nil
        table.rowHeight = 22
        table.dataSource = self
        table.delegate = self
        table.target = self
        table.doubleAction = #selector(play)
        let sc = NSScrollView()
        sc.documentView = table
        sc.hasVerticalScroller = true
        sc.translatesAutoresizingMaskIntoConstraints = false

        let bar = NSStackView()
        bar.orientation = .horizontal
        bar.spacing = 8
        bar.translatesAutoresizingMaskIntoConstraints = false
        playBtn = UI.button(L("rec_play"), self, #selector(play))
        startBtn = UI.button(L("rec_play_start"), self, #selector(playFromStart))
        stopBtn = UI.button(L("rec_stop"), self, #selector(stopRec))
        delBtn = UI.button(L("rec_trash"), self, #selector(trash))
        for b in [playBtn!, startBtn!, stopBtn!, delBtn!] { bar.addArrangedSubview(b) }
        bar.addArrangedSubview(NSView())
        bar.addArrangedSubview(UI.button(L("rec_open_folder"), self, #selector(openFolder)))
        folder.textColor = .secondaryLabelColor
        folder.lineBreakMode = .byTruncatingMiddle

        cv.addSubview(sc)
        cv.addSubview(bar)
        cv.addSubview(folder)
        NSLayoutConstraint.activate([
            sc.leadingAnchor.constraint(equalTo: cv.leadingAnchor),
            sc.trailingAnchor.constraint(equalTo: cv.trailingAnchor),
            sc.topAnchor.constraint(equalTo: cv.topAnchor),
            bar.leadingAnchor.constraint(equalTo: cv.leadingAnchor, constant: 10),
            bar.trailingAnchor.constraint(equalTo: cv.trailingAnchor, constant: -10),
            bar.topAnchor.constraint(equalTo: sc.bottomAnchor, constant: 8),
            folder.leadingAnchor.constraint(equalTo: cv.leadingAnchor, constant: 10),
            folder.trailingAnchor.constraint(lessThanOrEqualTo: cv.trailingAnchor, constant: -10),
            folder.topAnchor.constraint(equalTo: bar.bottomAnchor, constant: 6),
            folder.bottomAnchor.constraint(equalTo: cv.bottomAnchor, constant: -8),
        ])
        Recorder.shared.onChange.append { [weak self] in
            guard let self = self, self.window?.isVisible == true else { return }
            self.refreshLive()
        }
        w.center()
    }

    required init?(coder: NSCoder) { fatalError() }

    func showAndReload() {
        reload()
        showWindow(nil)
        window?.makeKeyAndOrderFront(nil)
    }

    func reload() {
        let dir = Recorder.shared.dir
        folder.stringValue = L("rec_folder", dir.path)
        let liveFiles = Set(Recorder.shared.active.map { $0.file.path })
        var files: [RecRow] = []
        let keys: [URLResourceKey] = [.fileSizeKey, .contentModificationDateKey]
        if let l = try? FileManager.default.contentsOfDirectory(at: dir, includingPropertiesForKeys: keys) {
            for u in l where u.pathExtension.lowercased() == "ts" && !liveFiles.contains(u.path) {
                let v = try? u.resourceValues(forKeys: Set(keys))
                files.append(.file(u, Int64(v?.fileSize ?? 0), v?.contentModificationDate ?? .distantPast))
            }
        }
        files.sort {
            if case .file(let a, _, _) = $0, case .file(let b, _, _) = $1 { return a.lastPathComponent > b.lastPathComponent }
            return false
        }
        rows = Recorder.shared.active.map { .live($0) } + Recorder.shared.schedules.map { .sched($0) } + files
        table.reloadData()
        updateButtons()
    }

    /** Rafraîchit les tailles des enregistrements en cours (et la liste si elle a changé). */
    private func refreshLive() {
        let liveCount = rows.filter { if case .live = $0 { return true }; return false }.count
        let schedCount = rows.filter { if case .sched = $0 { return true }; return false }.count
        if liveCount != Recorder.shared.active.count || schedCount != Recorder.shared.schedules.count {
            let sel = table.selectedRow
            reload()
            if sel >= 0 && sel < rows.count { table.selectRowIndexes(IndexSet(integer: sel), byExtendingSelection: false) }
        } else if liveCount > 0 {
            table.reloadData(forRowIndexes: IndexSet(0..<liveCount), columnIndexes: IndexSet(0..<2))
        }
    }

    func numberOfRows(in tableView: NSTableView) -> Int { rows.count }

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
        let isName = tableColumn?.identifier.rawValue == "name"
        let id = NSUserInterfaceItemIdentifier(isName ? "n" : "i")
        let l = (tableView.makeView(withIdentifier: id, owner: nil) as? NSTextField) ?? {
            let t = NSTextField(labelWithString: "")
            t.identifier = id
            t.lineBreakMode = .byTruncatingTail
            return t
        }()
        l.textColor = .labelColor
        switch rows[row] {
        case .live(let r):
            l.stringValue = isName ? "● \(r.chName)\(r.title.isEmpty ? "" : " – " + r.title)"
                : L("rec_live_line", UI.fileSize(r.bytes), r.endAt > 0 ? L("rec_until", hm(r.endAt)) : L("rec_no_limit"))
            if isName { l.textColor = .systemRed }
        case .sched(let s):
            l.stringValue = isName ? "◷ \(s.chName) – \(s.title)"
                : L("rec_scheduled", dayString(s.start), hm(s.start), hm(s.end))
            if !isName { l.textColor = .secondaryLabelColor }
        case .file(let u, let size, _):
            if isName {
                l.stringValue = u.deletingPathExtension().lastPathComponent
            } else {
                let pos = Prefs.position(u.path)
                l.stringValue = UI.fileSize(size) + (pos > 0 ? " · " + L("rec_resume_at", Naming.clock(pos)) : "")
                l.textColor = .secondaryLabelColor
            }
        }
        return l
    }

    private func dayString(_ t: Int) -> String {
        let f = DateFormatter()
        f.locale = Locale(identifier: L10n.lang)
        f.setLocalizedDateFormatFromTemplate("EEEdMMM")
        return f.string(from: Date(timeIntervalSince1970: TimeInterval(t)))
    }

    func tableViewSelectionDidChange(_ notification: Notification) { updateButtons() }

    private var selected: RecRow? {
        let r = table.selectedRow
        return r >= 0 && r < rows.count ? rows[r] : nil
    }

    private func updateButtons() {
        var file = false, live = false
        switch selected {
        case .file?: file = true
        case .live?, .sched?: live = true
        case nil: break
        }
        playBtn.isEnabled = file
        startBtn.isEnabled = file
        delBtn.isEnabled = file
        stopBtn.isEnabled = live
        if case .sched? = selected { stopBtn.title = L("rec_cancel_sched") } else { stopBtn.title = L("rec_stop") }
    }

    @objc private func play() { open(resume: true) }
    @objc private func playFromStart() { open(resume: false) }

    private func open(resume: Bool) {
        guard case .file(let u, _, _)? = selected else { return }
        AppDelegate.shared.playRecording(u, resume: resume)
    }

    @objc private func stopRec() {
        switch selected {
        case .live(let r)?:
            if UI.alert(L("rec_stop_confirm"), r.chName, buttons: [L("rec_stop"), L("cancel")]) == 0 {
                Recorder.shared.stop(r)
            }
        case .sched(let s)?:
            Recorder.shared.cancel(s)
        default: return
        }
        reload()
    }

    /** Corbeille (récupérable) plutôt qu'effacement définitif. */
    @objc private func trash() {
        guard case .file(let u, _, _)? = selected else { return }
        let name = u.deletingPathExtension().lastPathComponent
        guard UI.alert(L("rec_delete_title"), L("rec_trash_msg", name), buttons: [L("rec_trash"), L("cancel")],
                       style: .warning) == 0 else { return }
        do {
            try FileManager.default.trashItem(at: u, resultingItemURL: nil)
            Prefs.setPosition(u.path, 0)
        } catch {
            UI.alert(L("rec_delete_title"), error.localizedDescription, style: .warning)
        }
        reload()
    }

    @objc private func openFolder() {
        NSWorkspace.shared.open(Recorder.shared.dir)
    }
}

/** Lecture d'un enregistrement, avec reprise de la position. */
final class RecPlayerWindowController: NSWindowController, NSWindowDelegate {
    private let player = TVPlayer()
    private let file: URL
    private let slider = NSSlider(value: 0, minValue: 0, maxValue: 1, target: nil, action: nil)
    private let timeLabel = UI.label("", size: 12)
    private var playBtn: NSButton!
    private var timer: Timer?
    private var dragging = false

    init(file: URL, resume: Bool) {
        self.file = file
        let w = KeyWindow(contentRect: NSRect(x: 0, y: 0, width: 960, height: 600),
                          styleMask: [.titled, .closable, .miniaturizable, .resizable], backing: .buffered, defer: false)
        w.title = file.deletingPathExtension().lastPathComponent
        w.isReleasedWhenClosed = false
        w.collectionBehavior = [.fullScreenPrimary]
        super.init(window: w)
        w.delegate = self
        guard let cv = w.contentView else { return }
        player.live = false
        player.view.translatesAutoresizingMaskIntoConstraints = false
        player.view.onDoubleClick = { [weak self] in self?.window?.toggleFullScreen(nil) }
        player.view.onClick = { [weak self] _ in self?.togglePause() }
        let bar = NSStackView()
        bar.orientation = .horizontal
        bar.spacing = 6
        bar.edgeInsets = NSEdgeInsets(top: 6, left: 8, bottom: 6, right: 8)
        bar.translatesAutoresizingMaskIntoConstraints = false
        bar.addArrangedSubview(UI.button(L("btn_back30"), self, #selector(back)))
        playBtn = UI.button(L("btn_pause"), self, #selector(togglePause))
        bar.addArrangedSubview(playBtn)
        bar.addArrangedSubview(UI.button(L("btn_fwd30"), self, #selector(fwd)))
        slider.target = self
        slider.action = #selector(seek)
        slider.isContinuous = false
        slider.translatesAutoresizingMaskIntoConstraints = false
        bar.addArrangedSubview(slider)
        bar.addArrangedSubview(timeLabel)
        slider.setContentHuggingPriority(.defaultLow, for: .horizontal)
        cv.addSubview(player.view)
        cv.addSubview(bar)
        NSLayoutConstraint.activate([
            player.view.leadingAnchor.constraint(equalTo: cv.leadingAnchor),
            player.view.trailingAnchor.constraint(equalTo: cv.trailingAnchor),
            player.view.topAnchor.constraint(equalTo: cv.topAnchor),
            bar.leadingAnchor.constraint(equalTo: cv.leadingAnchor),
            bar.trailingAnchor.constraint(equalTo: cv.trailingAnchor),
            bar.topAnchor.constraint(equalTo: player.view.bottomAnchor),
            bar.bottomAnchor.constraint(equalTo: cv.bottomAnchor),
        ])
        w.keyHandler = { [weak self] e in self?.key(e) ?? false }
        player.onTracksChanged = { [weak self] in
            if let p = self?.player { Tracks.applyPrefs(p) }
        }
        let start = resume ? Prefs.position(file.path) : 0
        player.setVolume(Prefs.muted ? 0 : Prefs.volume)
        player.play(file.path, startAt: max(0, start - 3000))
        timer = Timer.scheduledTimer(timeInterval: 1, target: self, selector: #selector(tick), userInfo: nil, repeats: true)
        w.center()
    }

    required init?(coder: NSCoder) { fatalError() }

    @objc private func tick() {
        let t = player.timeMs, len = player.lengthMs
        if len > 0 {
            slider.doubleValue = Double(player.position)
            timeLabel.stringValue = L("rec_position", Naming.clock(t), Naming.clock(len))
        }
        playBtn.title = player.isPlaying ? L("btn_pause") : L("btn_play")
        if t > 0 && Int(Date().timeIntervalSince1970) % 5 == 0 { savePosition() }
    }

    private func savePosition() {
        let t = player.timeMs, len = player.lengthMs
        // Fin atteinte (à 30 s près) : la prochaine lecture repart du début
        Prefs.setPosition(file.path, (len > 0 && t > len - 30_000) ? 0 : t)
    }

    @objc private func togglePause() { player.togglePause() }
    @objc private func back() { player.seek(byMs: -30_000) }
    @objc private func fwd() { player.seek(byMs: 30_000) }
    @objc private func seek() { player.position = Float(slider.doubleValue) }

    private func key(_ e: NSEvent) -> Bool {
        if !e.modifierFlags.intersection([.command, .control, .option]).isEmpty { return false }
        switch e.keyCode {
        case 49: togglePause()                  // espace
        case 123: back()                        // ←
        case 124: fwd()                         // →
        case 53:                                // Échap
            if window?.styleMask.contains(.fullScreen) == true { window?.toggleFullScreen(nil) } else { return false }
        default:
            switch e.charactersIgnoringModifiers?.lowercased() {
            case "f": window?.toggleFullScreen(nil)
            case "l": Tracks.menu(player) { _ in }.popUp(positioning: nil, at: NSEvent.mouseLocation, in: nil)
            case "v": _ = Tracks.cycleAudio(player)
            case "u": _ = Tracks.cycleSpu(player)
            default: return false
            }
        }
        return true
    }

    func windowWillClose(_ notification: Notification) {
        savePosition()
        timer?.invalidate()
        player.close()
        AppDelegate.shared.recPlayerClosed(self)
    }
}
