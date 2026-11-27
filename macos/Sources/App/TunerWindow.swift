import AppKit

/**
 * Tuner HDHomeRun : modèle, firmware, mise à jour disponible (discover.json, à installer
 * depuis la page du tuner), scan des chaînes (lineup.post?scan=start, suivi par
 * lineup_status.json, arrêt).                                            (↔ src/tuner.h)
 */
final class TunerWindowController: NSWindowController, NSWindowDelegate {
    private let info = UI.label("", size: 13)
    private let scanStatus = UI.label("", size: 13)
    private let progress = NSProgressIndicator()
    private var scanBtn: NSButton!
    private var abortBtn: NSButton!
    private var webBtn: NSButton!
    private var base = ""
    private var source = "Antenna"
    private var poll: Task<Void, Never>?

    init() {
        let w = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 560, height: 420),
                         styleMask: [.titled, .closable, .miniaturizable], backing: .buffered, defer: false)
        w.title = L("tuner_title")
        w.isReleasedWhenClosed = false
        super.init(window: w)
        w.delegate = self
        guard let cv = w.contentView else { return }
        info.maximumNumberOfLines = 0
        info.lineBreakMode = .byWordWrapping
        info.isSelectable = true
        info.preferredMaxLayoutWidth = 520
        scanStatus.maximumNumberOfLines = 0
        scanStatus.preferredMaxLayoutWidth = 520
        let help = UI.label(L("scan_help"), size: 11)
        help.textColor = .secondaryLabelColor
        help.maximumNumberOfLines = 0
        help.preferredMaxLayoutWidth = 520
        progress.isIndeterminate = false
        progress.minValue = 0
        progress.maxValue = 100
        progress.isHidden = true
        progress.translatesAutoresizingMaskIntoConstraints = false

        let row1 = NSStackView(views: [UI.button(L("tuner_refresh"), self, #selector(loadInfo))])
        webBtn = UI.button(L("tuner_web"), self, #selector(openWeb))
        webBtn.isHidden = true
        row1.addArrangedSubview(webBtn)
        scanBtn = UI.button(L("scan_start"), self, #selector(confirmScan))
        abortBtn = UI.button(L("scan_abort"), self, #selector(abortScan))
        abortBtn.isHidden = true
        let row2 = NSStackView(views: [scanBtn, abortBtn])
        let title2 = UI.label(L("scan_title"), size: 13, bold: true)

        let sep = NSBox()
        sep.boxType = .separator
        let st = NSStackView(views: [info, row1, sep, title2, help, row2, progress, scanStatus])
        st.orientation = .vertical
        st.alignment = .leading
        st.spacing = 10
        st.edgeInsets = NSEdgeInsets(top: 16, left: 20, bottom: 16, right: 20)
        st.translatesAutoresizingMaskIntoConstraints = false
        cv.addSubview(st)
        NSLayoutConstraint.activate([
            st.leadingAnchor.constraint(equalTo: cv.leadingAnchor),
            st.trailingAnchor.constraint(equalTo: cv.trailingAnchor),
            st.topAnchor.constraint(equalTo: cv.topAnchor),
            st.bottomAnchor.constraint(lessThanOrEqualTo: cv.bottomAnchor),
            progress.widthAnchor.constraint(equalToConstant: 500),
        ])
        w.center()
    }

    required init?(coder: NSCoder) { fatalError() }

    func showAndLoad() {
        showWindow(nil)
        window?.makeKeyAndOrderFront(nil)
        loadInfo()
    }

    @objc private func loadInfo() {
        base = Prefs.tunerBaseURL
        info.stringValue = L("tuner_loading", base)
        let b = base
        Task {
            do {
                let d = try await Tuner.info(base: b)
                let s = try? await Tuner.status(base: b)
                var lines: [String] = []
                func line(_ k: String, _ v: String) { if !v.isEmpty { lines.append("\(L(k)) : \(v)") } }
                line("tuner_name", d.friendlyName)
                line("tuner_model", d.model)
                line("tuner_id", d.deviceID)
                line("tuner_count", d.tunerCount > 0 ? "\(d.tunerCount)" : "")
                line("tuner_fw", d.firmware)
                lines.append("")
                lines.append(d.upgradeAvailable.isEmpty ? L("tuner_up_to_date") : L("tuner_upgrade", d.upgradeAvailable))
                if let src = s?.source, !src.isEmpty { self.source = src }
                self.info.stringValue = lines.joined(separator: "\n")
                self.webBtn.isHidden = false
                if s?.inProgress == true { self.startPolling() }
            } catch {
                self.info.stringValue = L("tuner_unreachable", b, Net.describe(error))
            }
        }
    }

    @objc private func openWeb() {
        if let u = URL(string: base + "/") { NSWorkspace.shared.open(u) }
    }

    @objc private func confirmScan() {
        guard UI.alert(L("scan_title"), L("scan_confirm"), buttons: [L("scan_start"), L("cancel")], style: .warning) == 0
        else { return }
        let b = base, src = source
        Task {
            do {
                try await Tuner.startScan(base: b, source: src)
                self.startPolling()
            } catch {
                self.scanStatus.stringValue = L("scan_failed", Net.describe(error))
            }
        }
    }

    @objc private func abortScan() {
        let b = base
        Task { try? await Tuner.abortScan(base: b) }
    }

    private func startPolling() {
        poll?.cancel()
        scanBtn.isEnabled = false
        abortBtn.isHidden = false
        progress.isHidden = false
        progress.doubleValue = 0
        let b = base
        poll = Task {
            let done = await Tuner.follow(base: b) { s in
                if s.inProgress {
                    self.progress.doubleValue = Double(s.progress)
                    self.scanStatus.stringValue = L("scan_progress", s.progress, s.found)
                }
            }
            if done {
                self.scanStatus.stringValue = L("scan_done")
                AppDelegate.shared.mainWC.loadPlaylist()
            }
            self.scanBtn.isEnabled = true
            self.abortBtn.isHidden = true
            self.progress.isHidden = true
        }
    }

    func windowWillClose(_ notification: Notification) {
        poll?.cancel()   // le scan continue sur le tuner ; la fenêtre reprend le suivi à sa réouverture
    }
}
