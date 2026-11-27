import AppKit

/** Paramètres (lecture, guide et enregistrement, interface, HDHomeRun et mises à jour). */
final class SettingsWindowController: NSWindowController, NSWindowDelegate {
    private let url = NSTextField(string: "")
    private let resume = NSButton(checkboxWithTitle: L("set_resume"), target: nil, action: nil)
    private let cache = NSPopUpButton()
    private let hw = NSButton(checkboxWithTitle: L("set_hw"), target: nil, action: nil)
    private let deint = NSPopUpButton()
    private let aspect = NSPopUpButton()
    private let showGuide = NSButton(checkboxWithTitle: L("set_guide"), target: nil, action: nil)
    private let margin = NSPopUpButton()
    private let recDir = UI.label("", size: 12)
    private let lang = NSPopUpButton()
    private let updChk = NSButton(checkboxWithTitle: L("set_updchk"), target: nil, action: nil)

    private let cacheValues = [300, 1000, 2000, 3000, 5000]
    private let deintValues = ["off", "yadif", "yadif2x", "bob", "linear", "blend"]
    private let aspectValues = ["", "16:9", "4:3", "16:10", "21:9", "235:100"]
    private let marginValues = [0, 5, 10, 15, 30]
    private let langValues = ["", "fr", "en", "es", "de", "it", "pt"]
    private var chosenDir: URL?

    init() {
        let w = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 620, height: 560),
                         styleMask: [.titled, .closable], backing: .buffered, defer: false)
        w.title = L("settings")
        w.isReleasedWhenClosed = false
        super.init(window: w)
        w.delegate = self
        build()
        w.center()
    }

    required init?(coder: NSCoder) { fatalError() }

    private func section(_ t: String) -> NSTextField {
        let l = UI.label(t, size: 13, bold: true)
        return l
    }

    private func row(_ title: String, _ v: NSView) -> NSStackView {
        let l = UI.label(title)
        l.alignment = .right
        l.widthAnchor.constraint(equalToConstant: 230).isActive = true
        let s = NSStackView(views: [l, v])
        s.spacing = 8
        return s
    }

    private func help(_ t: String) -> NSTextField {
        let l = UI.label(t, size: 11)
        l.textColor = .secondaryLabelColor
        l.maximumNumberOfLines = 0
        l.preferredMaxLayoutWidth = 560
        return l
    }

    private func build() {
        guard let cv = window?.contentView else { return }
        url.placeholderString = Prefs.defaultURL
        url.translatesAutoresizingMaskIntoConstraints = false
        url.widthAnchor.constraint(equalToConstant: 320).isActive = true
        cache.addItems(withTitles: ["300 ms", "1 s", "2 s", "3 s", "5 s"])
        deint.addItems(withTitles: (0..<6).map { L("deint_labels_\($0)") })
        aspect.addItems(withTitles: [L("aspect_default_0"), "16:9", "4:3", "16:10", "21:9", "2.35:1"])
        margin.addItems(withTitles: marginValues.map { "\($0) min" })
        lang.addItems(withTitles: [L("lang_system_0"), "Français", "English", "Español", "Deutsch", "Italiano", "Português"])
        recDir.lineBreakMode = .byTruncatingMiddle
        recDir.widthAnchor.constraint(equalToConstant: 230).isActive = true
        let dirRow = NSStackView(views: [recDir, UI.button(L("set_choose"), self, #selector(chooseDir))])
        dirRow.spacing = 8

        let views: [NSView] = [
            section(L("set_cat_play")),
            row(L("set_playlist"), url),
            help(L("set_playlist_help")),
            resume,
            row(L("set_cache"), cache),
            hw,
            help(L("set_hw_help")),
            row(L("set_deint"), deint),
            row(L("set_aspect"), aspect),
            section(L("set_cat_guide")),
            showGuide,
            help(L("set_guide_help")),
            row(L("set_margin"), margin),
            row(L("set_rec_dir"), dirRow),
            section(L("set_cat_ui")),
            row(L("set_lang"), lang),
            help(L("set_lang_restart")),
            section(L("set_cat_tuner")),
            NSStackView(views: [UI.button(L("set_tuner_btn"), self, #selector(openTuner)), help(L("set_tuner_help"))]),
            NSStackView(views: [updChk, UI.button(L("set_updnow"), self, #selector(checkNow))]),
            NSStackView(views: [NSView(), UI.button(L("cancel"), self, #selector(cancel)),
                                UI.button(L("set_apply"), self, #selector(apply))]),
        ]
        let st = NSStackView(views: views)
        st.orientation = .vertical
        st.alignment = .leading
        st.spacing = 8
        st.edgeInsets = NSEdgeInsets(top: 16, left: 20, bottom: 16, right: 20)
        st.translatesAutoresizingMaskIntoConstraints = false
        cv.addSubview(st)
        NSLayoutConstraint.activate([
            st.leadingAnchor.constraint(equalTo: cv.leadingAnchor),
            st.trailingAnchor.constraint(equalTo: cv.trailingAnchor),
            st.topAnchor.constraint(equalTo: cv.topAnchor),
            st.bottomAnchor.constraint(equalTo: cv.bottomAnchor),
        ])
        if let last = views.last {
            last.translatesAutoresizingMaskIntoConstraints = false
            last.trailingAnchor.constraint(equalTo: st.trailingAnchor, constant: -20).isActive = true
        }
    }

    func showAndLoad() {
        url.stringValue = Prefs.d.string(forKey: "playlist_url") ?? ""
        resume.state = Prefs.resume ? .on : .off
        cache.selectItem(at: cacheValues.firstIndex(of: Prefs.caching) ?? 1)
        hw.state = Prefs.hwDecoding ? .on : .off
        deint.selectItem(at: deintValues.firstIndex(of: Prefs.deinterlace) ?? 1)
        aspect.selectItem(at: aspectValues.firstIndex(of: Prefs.aspect) ?? 0)
        showGuide.state = Prefs.showGuide ? .on : .off
        margin.selectItem(at: marginValues.firstIndex(of: Prefs.recMargin) ?? 2)
        chosenDir = nil
        recDir.stringValue = Prefs.recDir.path
        lang.selectItem(at: langValues.firstIndex(of: Prefs.uiLang) ?? 0)
        updChk.state = Prefs.checkUpdates ? .on : .off
        showWindow(nil)
        window?.makeKeyAndOrderFront(nil)
    }

    @objc private func chooseDir() {
        let p = NSOpenPanel()
        p.canChooseDirectories = true
        p.canChooseFiles = false
        p.canCreateDirectories = true
        p.directoryURL = chosenDir ?? Prefs.recDir
        if p.runModal() == .OK, let u = p.url {
            chosenDir = u
            recDir.stringValue = u.path
        }
    }

    @objc private func openTuner() { AppDelegate.shared.showTuner() }

    @objc private func checkNow() { AppDelegate.shared.mainWC.checkUpdates(manual: true) }

    @objc private func cancel() { window?.close() }

    @objc private func apply() {
        let oldURL = Prefs.playlistURL
        Prefs.playlistURL = url.stringValue.trimmingCharacters(in: .whitespaces)
        Prefs.resume = resume.state == .on
        Prefs.caching = cacheValues[max(0, cache.indexOfSelectedItem)]
        Prefs.hwDecoding = hw.state == .on
        Prefs.deinterlace = deintValues[max(0, deint.indexOfSelectedItem)]
        Prefs.aspect = aspectValues[max(0, aspect.indexOfSelectedItem)]
        Prefs.showGuide = showGuide.state == .on
        Prefs.recMargin = marginValues[max(0, margin.indexOfSelectedItem)]
        if let d = chosenDir { Prefs.recDir = d }
        Prefs.uiLang = langValues[max(0, lang.indexOfSelectedItem)]
        Prefs.checkUpdates = updChk.state == .on
        AppDelegate.shared.mainWC.settingsChanged(playlistChanged: oldURL != Prefs.playlistURL)
        window?.close()
    }
}
