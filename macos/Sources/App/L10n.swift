import AppKit

/*
 * HomeRun Player pour macOS — lecteur TV pour tuners HDHomeRun (Swift + AppKit + VLCKit 3).
 *
 * Fichiers de l'appli (Sources/App) :
 *   AppDelegate.swift       démarrage, menus, fenêtres, sortie
 *   L10n.swift              textes (6 langues, Resources/*.lproj) et réglages (UserDefaults)
 *   TVPlayer.swift          un lecteur VLCKit (image principale, encart, enregistrement lu)
 *   MainWindow.swift        lecture, liste, zapping, volume, PIP, bandeau, clavier ↔ src/vlcmaster.c, pip.h
 *   TracksMenu.swift        langues audio / sous-titres                         ↔ src/tracks.h
 *   GuideWindow.swift       grille du guide TV                                 ↔ src/guide_ui.h
 *   Recorder.swift          enregistrements (copie du flux .ts) et programmation ↔ src/record.h, sched.h
 *   RecordingsWindow.swift  liste des enregistrements et lecteur avec reprise
 *   TunerWindow.swift       infos tuner, firmware, scan des chaînes             ↔ src/tuner.h
 *   SettingsWindow.swift    Paramètres
 * Cœur sans interface : Sources/Core (testé par `swift test`).
 */

/** Textes localisés ; la langue peut être forcée dans Paramètres (au prochain démarrage). */
enum L10n {
    static var bundle: Bundle = .main
    static var lang = "en"

    static func setup() {
        let forced = Prefs.uiLang
        if !forced.isEmpty, let p = Bundle.main.path(forResource: forced, ofType: "lproj"), let b = Bundle(path: p) {
            bundle = b
            lang = forced
        } else {
            bundle = .main
            lang = Bundle.main.preferredLocalizations.first ?? "en"
        }
        if lang.count > 2 { lang = String(lang.prefix(2)) }
    }
}

/** Texte localisé, avec arguments éventuels (%@, %1$@, %ld). */
func L(_ key: String, _ args: CVarArg...) -> String {
    let fmt = L10n.bundle.localizedString(forKey: key, value: nil, table: nil)
    if args.isEmpty { return fmt.replacingOccurrences(of: "%%", with: "%") }
    return String(format: fmt, locale: Locale(identifier: L10n.lang), arguments: args)
}

/** Singulier / pluriel (clés _one / _other). */
func Lplural(_ key: String, _ n: Int) -> String {
    L(n == 1 ? key + "_one" : key + "_other", n)
}

/** Réglages (UserDefaults). */
enum Prefs {
    static let d = UserDefaults.standard
    static let defaultURL = "http://192.168.1.90/lineup.m3u"

    private static func s(_ k: String, _ def: String) -> String { d.string(forKey: k) ?? def }
    private static func b(_ k: String, _ def: Bool) -> Bool { d.object(forKey: k) == nil ? def : d.bool(forKey: k) }
    private static func i(_ k: String, _ def: Int) -> Int { d.object(forKey: k) == nil ? def : d.integer(forKey: k) }

    static var playlistURL: String {
        get {
            let v = s("playlist_url", "").trimmingCharacters(in: .whitespaces)
            return v.isEmpty ? defaultURL : v
        }
        set { d.set(newValue, forKey: "playlist_url") }
    }
    static var tunerBaseURL: String { tunerBase(of: playlistURL) }

    static var caching: Int { get { i("caching", 1000) } set { d.set(newValue, forKey: "caching") } }
    static var hwDecoding: Bool { get { b("hw_decoding", true) } set { d.set(newValue, forKey: "hw_decoding") } }
    static var deinterlace: String { get { s("deinterlace", "yadif") } set { d.set(newValue, forKey: "deinterlace") } }
    static var aspect: String { get { s("aspect", "") } set { d.set(newValue, forKey: "aspect") } }
    static var resume: Bool { get { b("resume", true) } set { d.set(newValue, forKey: "resume") } }
    static var showGuide: Bool { get { b("show_guide", true) } set { d.set(newValue, forKey: "show_guide") } }
    static var recMargin: Int { get { i("rec_margin", 10) } set { d.set(newValue, forKey: "rec_margin") } }
    static var checkUpdates: Bool { get { b("check_updates", true) } set { d.set(newValue, forKey: "check_updates") } }
    static var lastChannel: String { get { s("last_channel", "") } set { d.set(newValue, forKey: "last_channel") } }
    static var volume: Int { get { i("volume", 100) } set { d.set(newValue, forKey: "volume") } }
    static var muted: Bool { get { b("muted", false) } set { d.set(newValue, forKey: "muted") } }
    static var showList: Bool { get { b("show_list", true) } set { d.set(newValue, forKey: "show_list") } }
    /** Langue audio / sous-titres choisie (clé TrackNames.key ; « off » = sous-titres désactivés). */
    static var audioPref: String { get { s("audio_pref", "") } set { d.set(newValue, forKey: "audio_pref") } }
    static var spuPref: String { get { s("spu_pref", "") } set { d.set(newValue, forKey: "spu_pref") } }
    static var skippedVersion: String { get { s("skipped_version", "") } set { d.set(newValue, forKey: "skipped_version") } }
    static var uiLang: String { get { s("ui_lang", "") } set { d.set(newValue, forKey: "ui_lang") } }

    /** Dossier des enregistrements (par défaut ~/Movies/HomeRun Player). */
    static var recDir: URL {
        get {
            if let p = d.string(forKey: "rec_dir"), !p.isEmpty { return URL(fileURLWithPath: p, isDirectory: true) }
            let movies = FileManager.default.urls(for: .moviesDirectory, in: .userDomainMask).first
                ?? URL(fileURLWithPath: NSHomeDirectory()).appendingPathComponent("Movies")
            return movies.appendingPathComponent("HomeRun Player", isDirectory: true)
        }
        set { d.set(newValue.path, forKey: "rec_dir") }
    }

    /** Positions de reprise des enregistrements (ms), par chemin. */
    static func position(_ path: String) -> Int {
        (d.dictionary(forKey: "positions")?[path] as? Int) ?? 0
    }
    static func setPosition(_ path: String, _ ms: Int) {
        var m = d.dictionary(forKey: "positions") ?? [:]
        if ms <= 0 { m.removeValue(forKey: path) } else { m[path] = ms }
        d.set(m, forKey: "positions")
    }

    /** Clé des options qui imposent de recréer les lecteurs VLC. */
    static var playbackKey: String { "\(hwDecoding)|\(deinterlace)|\(caching)" }

    static var appVersion: String {
        Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "0"
    }
}

/** Petites aides d'interface. */
enum UI {
    static func label(_ text: String = "", size: CGFloat = 13, bold: Bool = false) -> NSTextField {
        let l = NSTextField(labelWithString: text)
        l.font = bold ? .boldSystemFont(ofSize: size) : .systemFont(ofSize: size)
        l.translatesAutoresizingMaskIntoConstraints = false
        return l
    }

    static func button(_ title: String, _ target: AnyObject?, _ action: Selector) -> NSButton {
        let b = NSButton(title: title, target: target, action: action)
        b.bezelStyle = .rounded
        b.translatesAutoresizingMaskIntoConstraints = false
        return b
    }

    @discardableResult
    static func alert(_ title: String, _ text: String = "", buttons: [String] = [], style: NSAlert.Style = .informational) -> Int {
        let a = NSAlert()
        a.messageText = title
        a.informativeText = text
        a.alertStyle = style
        for b in buttons { a.addButton(withTitle: b) }
        if buttons.isEmpty { a.addButton(withTitle: L("ok")) }
        let r = a.runModal()
        return r.rawValue - NSApplication.ModalResponse.alertFirstButtonReturn.rawValue
    }

    static func fileSize(_ n: Int64) -> String {
        ByteCountFormatter.string(fromByteCount: n, countStyle: .file)
    }
}
