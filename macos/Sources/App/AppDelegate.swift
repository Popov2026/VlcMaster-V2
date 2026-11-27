import AppKit

/** Démarrage, barre de menus, fenêtres secondaires, confirmation de sortie. */
@main
final class AppDelegate: NSObject, NSApplicationDelegate {
    static var shared: AppDelegate!

    static func main() {
        let app = NSApplication.shared
        let d = AppDelegate()
        shared = d
        app.delegate = d
        app.setActivationPolicy(.regular)
        app.run()
    }

    private(set) var mainWC: MainWindowController!
    private var guideWC: GuideWindowController?
    private var recsWC: RecordingsWindowController?
    private var tunerWC: TunerWindowController?
    private var settingsWC: SettingsWindowController?
    private var recPlayers: [RecPlayerWindowController] = []

    func applicationDidFinishLaunching(_ notification: Notification) {
        L10n.setup()
        Net.userAgent = "HomeRunPlayer-macOS/" + Prefs.appVersion
        buildMenu()
        _ = Recorder.shared
        mainWC = MainWindowController()
        mainWC.showWindow(nil)
        mainWC.window?.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
        mainWC.loadPlaylist()
        mainWC.checkFirmware()
        if Prefs.checkUpdates { mainWC.checkUpdates() }
    }

    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        if !Recorder.shared.active.isEmpty {
            let r = UI.alert(Lplural("rec_notif_title", Recorder.shared.active.count), L("quit_recording"),
                             buttons: [L("quit_anyway"), L("cancel")], style: .warning)
            if r != 0 { return .terminateCancel }
            Recorder.shared.stopAll()
        }
        return .terminateNow
    }

    func applicationWillTerminate(_ notification: Notification) {
        for p in recPlayers { p.window?.close() }
        mainWC?.main.close()
        mainWC?.closePip()
    }

    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool {
        mainWC?.window?.makeKeyAndOrderFront(nil)
        return true
    }

    // MARK: fenêtres

    func showGuide() {
        if guideWC == nil { guideWC = GuideWindowController() }
        guideWC?.showAndLoad()
    }

    func showRecordings() {
        if recsWC == nil { recsWC = RecordingsWindowController() }
        recsWC?.showAndReload()
    }

    func showTuner() {
        if tunerWC == nil { tunerWC = TunerWindowController() }
        tunerWC?.showAndLoad()
    }

    func showSettings() {
        if settingsWC == nil { settingsWC = SettingsWindowController() }
        settingsWC?.showAndLoad()
    }

    func playRecording(_ file: URL, resume: Bool) {
        let p = RecPlayerWindowController(file: file, resume: resume)
        recPlayers.append(p)
        p.showWindow(nil)
        p.window?.makeKeyAndOrderFront(nil)
    }

    func recPlayerClosed(_ p: RecPlayerWindowController) {
        recPlayers.removeAll { $0 === p }
        recsWC?.reload()
    }

    // MARK: menus

    @objc private func menuGuide() { showGuide() }
    @objc private func menuRecordings() { showRecordings() }
    @objc private func menuTuner() { showTuner() }
    @objc private func menuSettings() { showSettings() }
    @objc private func menuCheckUpdates() { mainWC.checkUpdates(manual: true) }
    @objc private func menuNext() { mainWC.nextChannel() }
    @objc private func menuPrev() { mainWC.prevChannel() }
    @objc private func menuList() { mainWC.toggleList() }
    @objc private func menuMute() { mainWC.toggleMute() }
    @objc private func menuVolUp() { mainWC.changeVolume(5) }
    @objc private func menuVolDown() { mainWC.changeVolume(-5) }
    @objc private func menuLang() { mainWC.showLangMenu() }
    @objc private func menuAudio() { mainWC.cycleAudio() }
    @objc private func menuSpu() { mainWC.cycleSpu() }
    @objc private func menuRec() { mainWC.recordClicked() }
    @objc private func menuPip() { mainWC.pipClicked() }
    @objc private func menuFloat() { mainWC.toggleFloating() }
    @objc private func menuFull() { mainWC.toggleFullScreen() }
    @objc private func menuReload() { mainWC.loadPlaylist() }

    @objc private func menuAbout() {
        NSApp.orderFrontStandardAboutPanel(options: [
            .applicationName: "HomeRun Player",
            .credits: NSAttributedString(string: "HDHomeRun · VLCKit (LGPL 2.1)\nhttps://github.com/\(Updater.repo)"),
        ])
    }

    private func item(_ title: String, _ sel: Selector?, _ key: String = "",
                      _ mods: NSEvent.ModifierFlags = .command, target: AnyObject? = nil) -> NSMenuItem {
        let i = NSMenuItem(title: title, action: sel, keyEquivalent: key)
        i.keyEquivalentModifierMask = mods
        i.target = target ?? self
        return i
    }

    private func buildMenu() {
        let bar = NSMenu()

        let app = NSMenu()
        app.addItem(item(L("menu_about"), #selector(menuAbout)))
        app.addItem(item(L("set_updnow"), #selector(menuCheckUpdates)))
        app.addItem(.separator())
        app.addItem(item(L("menu_settings"), #selector(menuSettings), ","))
        app.addItem(.separator())
        let hide = NSMenuItem(title: L("menu_hide"), action: #selector(NSApplication.hide(_:)), keyEquivalent: "h")
        app.addItem(hide)
        let others = NSMenuItem(title: L("menu_hide_others"), action: #selector(NSApplication.hideOtherApplications(_:)), keyEquivalent: "h")
        others.keyEquivalentModifierMask = [.command, .option]
        app.addItem(others)
        app.addItem(NSMenuItem(title: L("menu_show_all"), action: #selector(NSApplication.unhideAllApplications(_:)), keyEquivalent: ""))
        app.addItem(.separator())
        app.addItem(NSMenuItem(title: L("menu_quit"), action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q"))
        add(app, "HomeRun Player", to: bar)

        let edit = NSMenu(title: L("menu_edit"))
        edit.addItem(NSMenuItem(title: L("menu_cut"), action: #selector(NSText.cut(_:)), keyEquivalent: "x"))
        edit.addItem(NSMenuItem(title: L("menu_copy"), action: #selector(NSText.copy(_:)), keyEquivalent: "c"))
        edit.addItem(NSMenuItem(title: L("menu_paste"), action: #selector(NSText.paste(_:)), keyEquivalent: "v"))
        edit.addItem(NSMenuItem(title: L("menu_select_all"), action: #selector(NSText.selectAll(_:)), keyEquivalent: "a"))
        add(edit, L("menu_edit"), to: bar)

        let tv = NSMenu(title: L("menu_tv"))
        tv.addItem(item(L("menu_prev"), #selector(menuPrev), String(UnicodeScalar(NSUpArrowFunctionKey)!)))
        tv.addItem(item(L("menu_next"), #selector(menuNext), String(UnicodeScalar(NSDownArrowFunctionKey)!)))
        tv.addItem(item(L("menu_list"), #selector(menuList), "k"))
        tv.addItem(.separator())
        tv.addItem(item(L("menu_vol_up"), #selector(menuVolUp), String(UnicodeScalar(NSRightArrowFunctionKey)!)))
        tv.addItem(item(L("menu_vol_down"), #selector(menuVolDown), String(UnicodeScalar(NSLeftArrowFunctionKey)!)))
        tv.addItem(item(L("menu_mute"), #selector(menuMute), "m", [.command, .shift]))
        tv.addItem(.separator())
        tv.addItem(item(L("lang_title") + "…", #selector(menuLang), "l"))
        tv.addItem(item(L("menu_audio_next"), #selector(menuAudio), "l", [.command, .shift]))
        tv.addItem(item(L("menu_spu_next"), #selector(menuSpu), "u", [.command, .shift]))
        tv.addItem(.separator())
        tv.addItem(item(L("guide_title"), #selector(menuGuide), "g"))
        tv.addItem(item(L("ctx_rec"), #selector(menuRec), "r"))
        tv.addItem(item(L("recordings"), #selector(menuRecordings), "e"))
        tv.addItem(.separator())
        tv.addItem(item(L("pip_title") + "…", #selector(menuPip), "p", [.command, .shift]))
        tv.addItem(item(L("pip_float"), #selector(menuFloat), "p", [.command, .option]))
        tv.addItem(item(L("btn_full").replacingOccurrences(of: "⛶ ", with: ""), #selector(menuFull), "f", [.command, .control]))
        tv.addItem(.separator())
        tv.addItem(item(L("tuner_title") + "…", #selector(menuTuner), "t", [.command, .shift]))
        tv.addItem(item(L("menu_reload"), #selector(menuReload), "r", [.command, .shift]))
        add(tv, L("menu_tv"), to: bar)

        let win = NSMenu(title: L("menu_window"))
        win.addItem(NSMenuItem(title: L("menu_minimize"), action: #selector(NSWindow.performMiniaturize(_:)), keyEquivalent: "m"))
        win.addItem(NSMenuItem(title: L("menu_zoom"), action: #selector(NSWindow.performZoom(_:)), keyEquivalent: ""))
        win.addItem(NSMenuItem(title: L("menu_close"), action: #selector(NSWindow.performClose(_:)), keyEquivalent: "w"))
        add(win, L("menu_window"), to: bar)
        NSApp.windowsMenu = win

        NSApp.mainMenu = bar
    }

    private func add(_ m: NSMenu, _ title: String, to bar: NSMenu) {
        let i = NSMenuItem(title: title, action: nil, keyEquivalent: "")
        m.title = title
        i.submenu = m
        bar.addItem(i)
    }
}
