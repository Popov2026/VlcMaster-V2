import AppKit

/** Fenêtre principale : intercepte les touches simples (zapping, volume…) avant la liste. */
final class KeyWindow: NSWindow {
    var keyHandler: ((NSEvent) -> Bool)?
    var mouseMovedHandler: (() -> Void)?

    override func sendEvent(_ event: NSEvent) {
        if event.type == .keyDown, !(firstResponder is NSText), let h = keyHandler, h(event) { return }
        if event.type == .mouseMoved { mouseMovedHandler?() }
        super.sendEvent(event)
    }
}

/** Zone vidéo : l'image principale en plein, l'encart (PIP) dans un coin. */
final class VideoContainer: NSView {
    var onLayout: (() -> Void)?

    override init(frame: NSRect) {
        super.init(frame: frame)
        wantsLayer = true
        layer?.backgroundColor = NSColor.black.cgColor
    }

    required init?(coder: NSCoder) { fatalError() }

    override func layout() {
        super.layout()
        onLayout?()
    }
}

/** Case de la liste : nom de la chaîne + programme en cours. */
final class ChannelCell: NSTableCellView {
    let title = UI.label("", size: 13, bold: true)
    let sub = UI.label("", size: 11)

    override init(frame: NSRect) {
        super.init(frame: frame)
        sub.textColor = .secondaryLabelColor
        title.lineBreakMode = .byTruncatingTail
        sub.lineBreakMode = .byTruncatingTail
        addSubview(title)
        addSubview(sub)
        NSLayoutConstraint.activate([
            title.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 6),
            title.trailingAnchor.constraint(lessThanOrEqualTo: trailingAnchor, constant: -4),
            title.topAnchor.constraint(equalTo: topAnchor, constant: 3),
            sub.leadingAnchor.constraint(equalTo: title.leadingAnchor),
            sub.trailingAnchor.constraint(lessThanOrEqualTo: trailingAnchor, constant: -4),
            sub.topAnchor.constraint(equalTo: title.bottomAnchor, constant: 1),
        ])
        title.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        sub.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
    }

    required init?(coder: NSCoder) { fatalError() }
}

/**
 * Lecture : liste des chaînes, zapping (boutons, clavier, numéros), volume, plein écran,
 * reprise de la dernière chaîne, PIP (encart + fenêtre flottante), bandeau de mise à jour,
 * indicateur d'enregistrement.                              (↔ src/vlcmaster.c, src/pip.h)
 */
final class MainWindowController: NSWindowController, NSWindowDelegate, NSTableViewDataSource,
                                  NSTableViewDelegate, NSMenuDelegate {
    private(set) var channels: [Channel] = []
    private(set) var guide = GuideData()
    private var guideLoading = false

    let main = TVPlayer()
    private(set) var pip: TVPlayer?
    private(set) var current: Channel?
    private(set) var pipChannel: Channel?
    private var soundOnPip = false
    private var pipCorner = 0
    private var volume = Prefs.volume
    private var muted = Prefs.muted
    private var digits = ""
    private var floatPanel: NSPanel?
    private var fullScreen = false

    // vues
    private let root = NSStackView()
    private let middle = NSStackView()
    private let listScroll = NSScrollView()
    private let table = NSTableView()
    private let video = VideoContainer(frame: .zero)
    private let osd = UI.label("", size: 15)
    private let recBadge = UI.label("", size: 13, bold: true)
    private let bar = NSStackView()
    private let banner = NSStackView()
    private let bannerText = UI.label("", size: 13, bold: true)
    private var bannerURL = ""
    private var bannerVersion = ""
    private let volSlider = NSSlider(value: 100, minValue: 0, maxValue: 200, target: nil, action: nil)
    private var muteBtn: NSButton!
    private var recBtn: NSButton!
    private var pipBtn: NSButton!
    private var langBtn: NSButton!

    init() {
        let w = KeyWindow(contentRect: NSRect(x: 0, y: 0, width: 1180, height: 700),
                          styleMask: [.titled, .closable, .miniaturizable, .resizable],
                          backing: .buffered, defer: false)
        w.title = "HomeRun Player"
        w.minSize = NSSize(width: 640, height: 400)
        w.collectionBehavior = [.fullScreenPrimary]
        w.acceptsMouseMovedEvents = true
        super.init(window: w)
        w.delegate = self
        w.keyHandler = { [weak self] e in self?.handleKey(e) ?? false }
        w.mouseMovedHandler = { [weak self] in self?.mouseMoved() }
        buildUI()
        setupPlayers()
        Recorder.shared.onChange.append { [weak self] in self?.updateRecBadge() }
        Recorder.shared.onEvent = { [weak self] msg in self?.showOSD(msg, seconds: 8) }
        Timer.scheduledTimer(timeInterval: 60, target: self, selector: #selector(minuteTick), userInfo: nil, repeats: true)
        if !w.setFrameUsingName("MainWindow") { w.center() }
        w.setFrameAutosaveName("MainWindow")
    }

    required init?(coder: NSCoder) { fatalError() }

    // MARK: interface

    private func buildUI() {
        guard let cv = window?.contentView else { return }
        root.orientation = .vertical
        root.spacing = 0
        root.alignment = .leading
        root.distribution = .fill
        root.translatesAutoresizingMaskIntoConstraints = false
        cv.addSubview(root)
        NSLayoutConstraint.activate([
            root.leadingAnchor.constraint(equalTo: cv.leadingAnchor),
            root.trailingAnchor.constraint(equalTo: cv.trailingAnchor),
            root.topAnchor.constraint(equalTo: cv.topAnchor),
            root.bottomAnchor.constraint(equalTo: cv.bottomAnchor),
        ])

        // Bandeau de mise à jour
        banner.orientation = .horizontal
        banner.spacing = 10
        banner.edgeInsets = NSEdgeInsets(top: 6, left: 12, bottom: 6, right: 12)
        banner.wantsLayer = true
        banner.layer?.backgroundColor = NSColor.systemYellow.withAlphaComponent(0.85).cgColor
        bannerText.textColor = .black
        banner.addArrangedSubview(bannerText)
        banner.addArrangedSubview(NSView())
        banner.addArrangedSubview(UI.button(L("update_download"), self, #selector(bannerDownload)))
        banner.addArrangedSubview(UI.button(L("update_ignore"), self, #selector(bannerIgnore)))
        banner.isHidden = true

        // Liste des chaînes
        let col = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("ch"))
        col.title = L("channels")
        table.addTableColumn(col)
        table.headerView = nil
        table.rowHeight = 38
        table.dataSource = self
        table.delegate = self
        table.target = self
        table.action = #selector(tableClicked)
        table.doubleAction = #selector(tableClicked)
        let tm = NSMenu()
        tm.delegate = self
        table.menu = tm
        listScroll.documentView = table
        listScroll.hasVerticalScroller = true
        listScroll.translatesAutoresizingMaskIntoConstraints = false
        listScroll.widthAnchor.constraint(equalToConstant: 270).isActive = true

        // Vidéo
        video.translatesAutoresizingMaskIntoConstraints = false
        video.onLayout = { [weak self] in self?.layoutVideo() }
        video.addSubview(main.view)
        osd.textColor = .white
        osd.maximumNumberOfLines = 6
        osd.wantsLayer = true
        osd.drawsBackground = true
        osd.backgroundColor = NSColor.black.withAlphaComponent(0.65)
        osd.isHidden = true
        recBadge.textColor = .systemRed
        recBadge.isHidden = true

        middle.orientation = .horizontal
        middle.spacing = 0
        middle.distribution = .fill
        middle.alignment = .height
        middle.addArrangedSubview(listScroll)
        middle.addArrangedSubview(video)
        video.setContentHuggingPriority(.defaultLow, for: .horizontal)
        listScroll.isHidden = !Prefs.showList

        // Barre de commandes
        bar.orientation = .horizontal
        bar.spacing = 6
        bar.edgeInsets = NSEdgeInsets(top: 6, left: 8, bottom: 6, right: 8)
        func add(_ t: String, _ a: Selector, _ tip: String? = nil) -> NSButton {
            let b = UI.button(t, self, a)
            b.toolTip = tip
            bar.addArrangedSubview(b)
            return b
        }
        _ = add(L("btn_list"), #selector(toggleList), L("menu_list") + " (C)")
        _ = add("◀", #selector(prevChannel), L("menu_prev") + " (↑)")
        _ = add("▶", #selector(nextChannel), L("menu_next") + " (↓)")
        muteBtn = add("🔊", #selector(toggleMute), L("menu_mute") + " (M)")
        volSlider.target = self
        volSlider.action = #selector(volumeChanged)
        volSlider.translatesAutoresizingMaskIntoConstraints = false
        volSlider.widthAnchor.constraint(equalToConstant: 110).isActive = true
        bar.addArrangedSubview(volSlider)
        _ = add(L("btn_guide"), #selector(openGuide), "G")
        langBtn = add(L("btn_lang"), #selector(showLangMenu), "L · V · U")
        recBtn = add(L("btn_rec"), #selector(recordClicked), "R")
        pipBtn = add(L("btn_pip"), #selector(pipClicked), "P")
        _ = add(L("btn_recs"), #selector(openRecordings), "E")
        _ = add(L("btn_full"), #selector(toggleFullScreen), "F")
        _ = add(L("btn_settings"), #selector(openSettings), "S")

        for v in [banner, middle, bar] as [NSView] {
            root.addArrangedSubview(v)
            v.translatesAutoresizingMaskIntoConstraints = false
            v.leadingAnchor.constraint(equalTo: root.leadingAnchor).isActive = true
            v.trailingAnchor.constraint(equalTo: root.trailingAnchor).isActive = true
        }
        middle.setContentHuggingPriority(.defaultLow, for: .vertical)
        bar.setContentHuggingPriority(.required, for: .vertical)
        banner.setContentHuggingPriority(.required, for: .vertical)

        video.addSubview(osd)
        video.addSubview(recBadge)
        NSLayoutConstraint.activate([
            osd.leadingAnchor.constraint(equalTo: video.leadingAnchor, constant: 20),
            osd.topAnchor.constraint(equalTo: video.topAnchor, constant: 20),
            osd.trailingAnchor.constraint(lessThanOrEqualTo: video.trailingAnchor, constant: -20),
            recBadge.trailingAnchor.constraint(equalTo: video.trailingAnchor, constant: -16),
            recBadge.topAnchor.constraint(equalTo: video.topAnchor, constant: 16),
        ])
        volSlider.integerValue = volume
        updateMuteButton()
    }

    private func setupPlayers() {
        main.view.onClick = { [weak self] _ in self?.setSound(pip: false) }
        main.view.onDoubleClick = { [weak self] in self?.toggleFullScreen() }
        main.onRetry = { [weak self] n in self?.showOSD(L("retrying", n)) }
        main.onFailed = { [weak self] in
            guard let self = self else { return }
            self.showOSD(L("cannot_play", self.current?.name ?? ""), seconds: 30)
        }
        main.onTracksChanged = { [weak self] in
            guard let self = self, !self.soundOnPip else { return }
            Tracks.applyPrefs(self.main)
        }
        applyVolumes()
    }

    private func layoutVideo() {
        let b = video.bounds
        if main.view.superview === video { main.view.frame = b }
        guard let p = pip else { return }
        let w = max(160, b.width * 0.3), h = w * 9 / 16, m: CGFloat = 16
        var r = NSRect(x: b.maxX - w - m, y: m, width: w, height: h)          // bas droite
        switch pipCorner {
        case 1: r.origin.x = m                                                 // bas gauche
        case 2: r.origin = NSPoint(x: m, y: b.maxY - h - m)                    // haut gauche
        case 3: r.origin.y = b.maxY - h - m                                    // haut droite
        default: break
        }
        p.view.frame = r
    }

    // MARK: playlist et guide

    func loadPlaylist() {
        showOSD(L("loading_playlist"), seconds: 30)
        let url = Prefs.playlistURL
        Task {
            do {
                let l = try await Playlist.load(url)
                self.channels = l
                self.table.reloadData()
                if l.isEmpty {
                    self.showOSD(L("playlist_empty"), seconds: 30)
                    return
                }
                self.hideOSD()
                if self.current == nil || !l.contains(where: { $0.url == self.current?.url }) {
                    let last = Prefs.lastChannel
                    let found = Prefs.resume ? l.first(where: { $0.url == last || $0.label == last }) : nil
                    let ch = found ?? l[0]
                    self.play(ch)
                } else {
                    self.selectCurrentRow()
                }
                if Prefs.showGuide { self.loadGuide(force: true) }
            } catch {
                self.showOSD(L("playlist_error", Net.describe(error)), seconds: 60)
            }
        }
    }

    func loadGuide(force: Bool = false, then: (() -> Void)? = nil) {
        if guideLoading || (!force && !guide.stale) { then?(); return }
        guideLoading = true
        let base = Prefs.tunerBaseURL
        Task {
            defer { self.guideLoading = false }
            do {
                self.guide = try await Guide.load(base: base)
                self.table.reloadData()
                self.selectCurrentRow()
                self.updateTitle()
            } catch let e as GuideError {
                switch e {
                case .noTuner(let b): self.guideError = L("guide_err_no_tuner", b)
                case .service(let m): self.guideError = L("guide_err_service", m)
                case .empty: self.guideError = L("guide_err_empty")
                }
            } catch {
                self.guideError = L("guide_err_service", Net.describe(error))
            }
            then?()
        }
    }

    /** Dernière erreur du guide (affichée par la fenêtre Guide). */
    var guideError: String?

    var guideData: GuideData { guide }

    @objc private func minuteTick() {
        table.reloadData()
        selectCurrentRow()
        updateTitle()
        if Prefs.showGuide && guide.stale { loadGuide() }
    }

    func checkFirmware() {
        let base = Prefs.tunerBaseURL
        Task {
            if let i = try? await Tuner.info(base: base), !i.upgradeAvailable.isEmpty {
                self.showOSD(L("firmware_available", i.upgradeAvailable), seconds: 8)
            }
        }
    }

    // MARK: lecture

    func play(_ ch: Channel) {
        current = ch
        main.rebuildIfNeeded()
        main.play(ch.url)
        Prefs.lastChannel = ch.url
        selectCurrentRow()
        updateTitle()
        updateRecBadge()
        showChannelOSD(ch)
    }

    private func showChannelOSD(_ ch: Channel) {
        var s = ch.label
        if let p = guide.at(ch) {
            s += "\n\(p.title)  \(hm(p.start)) – \(hm(p.end))"
            if let n = guide.next(ch) { s += "\n" + L("next_prog", hm(n.start), n.title) }
        } else {
            s = L("opening", ch.label)
        }
        showOSD(s)
    }

    private func updateTitle() {
        guard let ch = current else { window?.title = "HomeRun Player"; return }
        var t = "HomeRun Player — \(ch.label)"
        if let p = guide.at(ch) { t += " — \(p.title) (\(hm(p.start)) – \(hm(p.end)))" }
        window?.title = t
    }

    private func selectCurrentRow() {
        guard let c = current, let i = channels.firstIndex(where: { $0.url == c.url }) else { return }
        table.selectRowIndexes(IndexSet(integer: i), byExtendingSelection: false)
        table.scrollRowToVisible(i)
    }

    private func zap(_ d: Int) {
        guard !channels.isEmpty else { return }
        let i = current.flatMap { c in channels.firstIndex { $0.url == c.url } } ?? -1
        play(channels[((i + d) % channels.count + channels.count) % channels.count])
    }

    @objc func nextChannel() { zap(1) }
    @objc func prevChannel() { zap(-1) }

    @objc private func tableClicked() {
        let r = table.clickedRow >= 0 ? table.clickedRow : table.selectedRow
        guard r >= 0, r < channels.count, channels[r].url != current?.url else { return }
        play(channels[r])
    }

    private func zapToNumber(_ n: String) {
        if let ch = channels.first(where: { $0.num == n }) ?? channels.first(where: { $0.num.hasPrefix(n + ".") }) {
            play(ch)
        } else {
            showOSD(L("no_channel_num", n))
        }
    }

    @objc private func digitsTimeout() {
        let d = digits
        digits = ""
        if !d.isEmpty { zapToNumber(d) }
    }

    // MARK: volume

    private func applyVolumes() {
        let v = muted ? 0 : volume
        main.setVolume(soundOnPip ? 0 : v)
        pip?.setVolume(soundOnPip ? v : 0)
    }

    private func updateMuteButton() {
        muteBtn?.title = muted ? "🔇" : "🔊"
    }

    @objc private func volumeChanged() {
        volume = volSlider.integerValue
        muted = false
        Prefs.volume = volume
        Prefs.muted = false
        updateMuteButton()
        applyVolumes()
    }

    func changeVolume(_ d: Int) {
        volume = max(0, min(200, volume + d))
        muted = false
        volSlider.integerValue = volume
        Prefs.volume = volume
        Prefs.muted = false
        updateMuteButton()
        applyVolumes()
        showOSD(L("volume_pct", volume))
    }

    @objc func toggleMute() {
        muted.toggle()
        Prefs.muted = muted
        updateMuteButton()
        applyVolumes()
        showOSD(muted ? L("muted") : L("unmuted"))
    }

    // MARK: OSD

    func showOSD(_ text: String, seconds: TimeInterval = 4) {
        osd.stringValue = " " + text.replacingOccurrences(of: "\n", with: " \n ") + " "
        osd.isHidden = false
        NSObject.cancelPreviousPerformRequests(withTarget: self, selector: #selector(hideOSD), object: nil)
        perform(#selector(hideOSD), with: nil, afterDelay: seconds)
    }

    @objc private func hideOSD() { osd.isHidden = true }

    private func updateRecBadge() {
        let n = Recorder.shared.active.count
        let here = current.map { Recorder.shared.isRecording($0.url) } ?? false
        recBadge.stringValue = here ? L("rec_indicator") : (n > 0 ? "● \(n)" : "")
        recBadge.isHidden = n == 0
        recBtn?.contentTintColor = here ? .systemRed : nil
    }

    // MARK: liste (NSTableView)

    func numberOfRows(in tableView: NSTableView) -> Int { channels.count }

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
        let id = NSUserInterfaceItemIdentifier("cell")
        let c = (tableView.makeView(withIdentifier: id, owner: nil) as? ChannelCell) ?? {
            let n = ChannelCell(frame: .zero)
            n.identifier = id
            return n
        }()
        let ch = channels[row]
        let rec = Recorder.shared.isRecording(ch.url) ? "● " : ""
        c.title.stringValue = rec + ch.label
        if Prefs.showGuide, let p = guide.at(ch) {
            c.sub.stringValue = "\(hm(p.start))  \(p.title)"
        } else {
            c.sub.stringValue = ""
        }
        return c
    }

    func menuNeedsUpdate(_ menu: NSMenu) {
        menu.removeAllItems()
        let r = table.clickedRow
        guard r >= 0, r < channels.count else { return }
        let ch = channels[r]
        menu.addItem(ClosureMenuItem(L("ctx_play")) { [weak self] in self?.play(ch) })
        menu.addItem(ClosureMenuItem(L("ctx_pip")) { [weak self] in self?.openPip(ch) })
        menu.addItem(ClosureMenuItem(L("ctx_rec")) { [weak self] in
            guard let self = self else { return }
            self.recordMenu(for: ch).popUp(positioning: nil, at: NSEvent.mouseLocation, in: nil)
        })
    }

    @objc func toggleList() {
        listScroll.isHidden.toggle()
        if !fullScreen { Prefs.showList = !listScroll.isHidden }
    }

    // MARK: enregistrement

    /** Menu des durées d'enregistrement d'une chaîne (ou arrêt si elle est déjà enregistrée). */
    func recordMenu(for ch: Channel) -> NSMenu {
        let m = NSMenu(title: L("rec_title", ch.name))
        if let r = Recorder.shared.active.first(where: { $0.url == ch.url }) {
            m.addItem(ClosureMenuItem(L("rec_stop_one", ch.name)) { Recorder.shared.stop(r) })
            return m
        }
        let now = nowEpoch()
        if let p = guide.at(ch) {
            let end = p.end + Prefs.recMargin * 60
            m.addItem(ClosureMenuItem(L("rec_until_end", p.title, hm(end))) { [weak self] in
                self?.startRecording(ch, title: p.title, endAt: end)
            })
        }
        for min in [30, 60, 120, 180] {
            m.addItem(ClosureMenuItem(L("rec_for", Naming.duration(min * 60))) { [weak self] in
                self?.startRecording(ch, title: self?.guide.at(ch)?.title, endAt: now + min * 60)
            })
        }
        m.addItem(ClosureMenuItem(L("rec_unlimited")) { [weak self] in
            self?.startRecording(ch, title: self?.guide.at(ch)?.title, endAt: 0)
        })
        return m
    }

    func startRecording(_ ch: Channel, title: String?, endAt: Int) {
        Recorder.shared.start(channel: ch, title: title, endAt: endAt)
        showOSD(endAt > 0 ? L("rec_started_until", ch.name, hm(endAt)) : L("rec_started", ch.name))
        table.reloadData()
        selectCurrentRow()
    }

    @objc func recordClicked() {
        guard let ch = current else { return }
        if let r = Recorder.shared.active.first(where: { $0.url == ch.url }) {
            if UI.alert(L("rec_stop_confirm"), r.chName, buttons: [L("rec_stop"), L("cancel")]) == 0 {
                Recorder.shared.stop(r)
            }
            return
        }
        recordMenu(for: ch).popUp(positioning: nil, at: NSPoint(x: 0, y: recBtn.bounds.height + 4), in: recBtn)
    }

    // MARK: langues

    var soundPlayer: TVPlayer { soundOnPip ? (pip ?? main) : main }

    @objc func showLangMenu() {
        let m = Tracks.menu(soundPlayer) { [weak self] name in self?.showOSD(name) }
        m.popUp(positioning: nil, at: NSPoint(x: 0, y: langBtn.bounds.height + 4), in: langBtn)
    }

    @objc func cycleAudio() {
        showOSD(Tracks.cycleAudio(soundPlayer) ?? L("trk_none"))
    }

    @objc func cycleSpu() {
        showOSD(Tracks.cycleSpu(soundPlayer) ?? L("trk_none"))
    }

    // MARK: PIP

    func openPip(_ ch: Channel) {
        if pip == nil {
            let p = TVPlayer()
            p.view.onClick = { [weak self] _ in self?.setSound(pip: true) }
            p.view.onDoubleClick = { [weak self] in self?.swapPip() }
            p.view.onRightClick = { [weak self] e in
                guard let self = self else { return }
                NSMenu.popUpContextMenu(self.pipMenu(), with: e, for: p.view)
            }
            p.onRetry = { [weak self] n in self?.showOSD(L("pip_title") + " — " + L("retrying", n)) }
            p.onFailed = { [weak self] in self?.showOSD(L("cannot_play", self?.pipChannel?.name ?? ""), seconds: 10) }
            p.onTracksChanged = { [weak self, weak p] in
                guard let self = self, let p = p, self.soundOnPip else { return }
                Tracks.applyPrefs(p)
            }
            p.view.wantsLayer = true
            p.view.layer?.borderColor = NSColor.white.withAlphaComponent(0.7).cgColor
            p.view.layer?.borderWidth = 1
            video.addSubview(p.view, positioned: .below, relativeTo: osd)
            pip = p
        }
        pipChannel = ch
        pip?.rebuildIfNeeded()
        pip?.play(ch.url)
        applyVolumes()
        layoutVideo()
    }

    func closePip() {
        pip?.close()
        pip?.view.removeFromSuperview()
        pip = nil
        pipChannel = nil
        soundOnPip = false
        applyVolumes()
    }

    func setSound(pip toPip: Bool) {
        guard toPip != soundOnPip, toPip == false || pip != nil else { return }
        soundOnPip = toPip
        applyVolumes()
        showOSD(toPip ? L("pip_sound_pip") : L("pip_sound_main"))
    }

    /** Inverse les chaînes de l'image principale et de l'encart. */
    func swapPip() {
        guard let p = pip, let pc = pipChannel, let mc = current else { return }
        p.play(mc.url)
        pipChannel = mc
        current = pc
        main.play(pc.url)
        Prefs.lastChannel = pc.url
        soundOnPip = false
        applyVolumes()
        selectCurrentRow()
        updateTitle()
        updateRecBadge()
    }

    private func channelsMenu(_ pick: @escaping (Channel) -> Void) -> NSMenu {
        let m = NSMenu()
        for ch in channels {
            m.addItem(ClosureMenuItem(ch.label, checked: ch.url == pipChannel?.url) { pick(ch) })
        }
        return m
    }

    private func pipMenu() -> NSMenu {
        let m = NSMenu(title: L("pip_title"))
        if pip == nil {
            let h = NSMenuItem(title: L("pip_choose"), action: nil, keyEquivalent: "")
            h.isEnabled = false
            m.autoenablesItems = false
            m.addItem(h)
            for ch in channels {
                m.addItem(ClosureMenuItem("   " + ch.label) { [weak self] in self?.openPip(ch) })
            }
        } else {
            m.addItem(ClosureMenuItem(soundOnPip ? L("pip_sound_to_main") : L("pip_sound_to_pip")) { [weak self] in
                guard let self = self else { return }
                self.setSound(pip: !self.soundOnPip)
            })
            m.addItem(.separator())
            let change = NSMenuItem(title: L("pip_change"), action: nil, keyEquivalent: "")
            change.submenu = channelsMenu { [weak self] ch in self?.openPip(ch) }
            m.addItem(change)
            m.addItem(ClosureMenuItem(L("pip_swap")) { [weak self] in self?.swapPip() })
            m.addItem(ClosureMenuItem(L("pip_move")) { [weak self] in
                guard let self = self else { return }
                self.pipCorner = (self.pipCorner + 1) % 4
                self.layoutVideo()
            })
            m.addItem(ClosureMenuItem(L("pip_close")) { [weak self] in self?.closePip() })
        }
        m.addItem(.separator())
        m.addItem(ClosureMenuItem(floatPanel == nil ? L("pip_float") : L("pip_float_back")) { [weak self] in
            self?.toggleFloating()
        })
        return m
    }

    @objc func pipClicked() {
        pipMenu().popUp(positioning: nil, at: NSPoint(x: 0, y: pipBtn.bounds.height + 4), in: pipBtn)
    }

    /**
     * « PIP système » : VLCKit 3 ne gère pas le PIP de macOS (AVKit) ; l'image principale
     * passe dans une petite fenêtre flottante, au-dessus des autres applis et des espaces.
     */
    @objc func toggleFloating() {
        if let p = floatPanel {
            p.close()   // windowWillClose remet l'image en place
            return
        }
        let p = NSPanel(contentRect: NSRect(x: 0, y: 0, width: 480, height: 270),
                        styleMask: [.titled, .closable, .resizable, .utilityWindow, .nonactivatingPanel],
                        backing: .buffered, defer: false)
        p.title = current?.label ?? "HomeRun Player"
        p.level = .floating
        p.isFloatingPanel = true
        p.hidesOnDeactivate = false
        p.collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary]
        p.isReleasedWhenClosed = false
        p.delegate = self
        p.contentAspectRatio = NSSize(width: 16, height: 9)
        if let scr = NSScreen.main?.visibleFrame {
            p.setFrameOrigin(NSPoint(x: scr.maxX - 500, y: scr.minY + 20))
        }
        main.view.removeFromSuperview()
        main.view.frame = p.contentView?.bounds ?? .zero
        main.view.autoresizingMask = [.width, .height]
        p.contentView?.addSubview(main.view)
        floatPanel = p
        p.orderFrontRegardless()
    }

    /** Fermer la fenêtre principale quitte l'appli (avec confirmation si enregistrements). */
    func windowShouldClose(_ sender: NSWindow) -> Bool {
        if sender === window {
            NSApp.terminate(nil)
            return false
        }
        return true
    }

    func windowWillClose(_ notification: Notification) {
        guard let p = notification.object as? NSPanel, p === floatPanel else { return }
        main.view.removeFromSuperview()
        main.view.autoresizingMask = []
        video.addSubview(main.view, positioned: .below, relativeTo: nil)
        floatPanel = nil
        layoutVideo()
    }

    // MARK: plein écran

    @objc func toggleFullScreen() {
        window?.toggleFullScreen(nil)
    }

    func windowWillEnterFullScreen(_ notification: Notification) {
        fullScreen = true
        listScroll.isHidden = true
        bar.isHidden = true
    }

    func windowDidExitFullScreen(_ notification: Notification) {
        fullScreen = false
        listScroll.isHidden = !Prefs.showList
        bar.isHidden = false
    }

    private func mouseMoved() {
        guard fullScreen else { return }
        bar.isHidden = false
        NSObject.cancelPreviousPerformRequests(withTarget: self, selector: #selector(autoHideBar), object: nil)
        perform(#selector(autoHideBar), with: nil, afterDelay: 3)
    }

    @objc private func autoHideBar() {
        if fullScreen { bar.isHidden = true }
    }

    // MARK: bandeau de mise à jour

    func checkUpdates(manual: Bool = false) {
        Task {
            do {
                let i = try await Updater.check(current: Prefs.appVersion)
                if i.newer, let dmg = i.dmgURL, manual || i.version != Prefs.skippedVersion {
                    self.bannerURL = dmg
                    self.bannerVersion = i.version
                    self.bannerText.stringValue = L("update_banner_mac", i.version)
                    self.banner.isHidden = false
                } else if manual {
                    UI.alert(L("update_none", Prefs.appVersion, i.version))
                }
            } catch {
                if manual { UI.alert(L("update_failed", Net.describe(error))) }
            }
        }
    }

    @objc private func bannerDownload() {
        if let u = URL(string: bannerURL) { NSWorkspace.shared.open(u) }
        banner.isHidden = true
    }

    @objc private func bannerIgnore() {
        Prefs.skippedVersion = bannerVersion
        banner.isHidden = true
    }

    // MARK: fenêtres

    @objc func openGuide() { AppDelegate.shared.showGuide() }
    @objc func openRecordings() { AppDelegate.shared.showRecordings() }
    @objc func openSettings() { AppDelegate.shared.showSettings() }

    /** Après Paramètres : recrée les lecteurs si besoin, recharge la playlist si l'adresse a changé. */
    func settingsChanged(playlistChanged: Bool) {
        main.rebuildIfNeeded()
        pip?.rebuildIfNeeded()
        main.applyAspect()
        pip?.applyAspect()
        table.reloadData()
        selectCurrentRow()
        if playlistChanged {
            guide = GuideData()
            loadPlaylist()
        } else if Prefs.showGuide && guide.channels.isEmpty {
            loadGuide(force: true)
        }
    }

    // MARK: clavier

    /** Touches simples (sans ⌘) ; renvoie true si la touche est traitée. */
    private func handleKey(_ e: NSEvent) -> Bool {
        let mods = e.modifierFlags.intersection([.command, .control, .option])
        if !mods.isEmpty { return false }
        switch e.keyCode {
        case 126, 116: zap(-1); return true          // ↑, Page préc.
        case 125, 121: zap(1); return true           // ↓, Page suiv.
        case 123: changeVolume(-5); return true      // ←
        case 124: changeVolume(5); return true       // →
        case 53:                                     // Échap
            if fullScreen { toggleFullScreen(); return true }
            return false
        case 36, 76:                                 // Entrée
            if !digits.isEmpty { digitsTimeout(); return true }
            return false
        default: break
        }
        guard let c = e.charactersIgnoringModifiers?.lowercased(), c.count == 1 else { return false }
        if let ch = c.first, ch.isNumber {
            digits += c
            NSObject.cancelPreviousPerformRequests(withTarget: self, selector: #selector(digitsTimeout), object: nil)
            perform(#selector(digitsTimeout), with: nil, afterDelay: 1.5)
            showOSD(digits + "_", seconds: 2)
            return true
        }
        switch c {
        case "+", "=": changeVolume(5)
        case "-": changeVolume(-5)
        case "m": toggleMute()
        case "f": toggleFullScreen()
        case "c": toggleList()
        case "g": openGuide()
        case "l": showLangMenu()
        case "v": cycleAudio()
        case "u": cycleSpu()
        case "r": recordClicked()
        case "e": openRecordings()
        case "p": pipClicked()
        case "t": AppDelegate.shared.showTuner()
        case "s": openSettings()
        default: return false
        }
        return true
    }
}
