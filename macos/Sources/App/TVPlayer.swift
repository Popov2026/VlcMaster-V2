import AppKit
import VLCKit

/** Vue noire qui reçoit l'image de VLC (drawable) ; transmet les clics à son lecteur. */
final class VideoView: NSView {
    var onClick: ((NSEvent) -> Void)?
    var onDoubleClick: (() -> Void)?
    var onRightClick: ((NSEvent) -> Void)?

    override init(frame: NSRect) {
        super.init(frame: frame)
        wantsLayer = true
        layer?.backgroundColor = NSColor.black.cgColor
    }

    required init?(coder: NSCoder) { fatalError() }

    override func mouseDown(with event: NSEvent) {
        if event.clickCount == 2 { onDoubleClick?() } else { onClick?(event) }
    }

    override func rightMouseDown(with event: NSEvent) {
        if let r = onRightClick { r(event) } else { super.rightMouseDown(with: event) }
    }

    override var acceptsFirstResponder: Bool { false }
}

/**
 * Un lecteur VLCKit et sa vue.  Gère la reprise automatique quand le flux coupe :
 * un minuteur surveille l'état et relance la lecture (5 essais, puis message).
 */
@MainActor
final class TVPlayer: NSObject {
    let view = VideoView(frame: NSRect(x: 0, y: 0, width: 640, height: 360))
    private(set) var mp: VLCMediaPlayer
    private(set) var url: String?
    private(set) var key = Prefs.playbackKey
    /** Direct (chaîne TV) : reprise auto ; fichier : pas de reprise. */
    var live = true

    var onRetry: ((Int) -> Void)?
    var onFailed: (() -> Void)?
    /** Pistes audio / sous-titres apparues (pour réappliquer les choix mémorisés). */
    var onTracksChanged: (() -> Void)?
    var onPlaying: (() -> Void)?

    private var timer: Timer?
    private var retries = 0
    private var playingSince: Date?
    private var startedAt = Date()
    private var lastTrackCount = -1
    private var volume = 100

    static func options() -> [String] {
        var o = ["--no-stats", "--http-reconnect", "--audio-time-stretch", "--no-video-title-show"]
        let deint = Prefs.deinterlace
        if deint != "off" {
            o += ["--deinterlace=1", "--deinterlace-mode=\(deint)"]
        } else {
            o += ["--deinterlace=0"]
        }
        if !Prefs.hwDecoding { o += ["--avcodec-hw=none", "--codec=avcodec,all"] }
        return o
    }

    override init() {
        mp = VLCMediaPlayer(options: TVPlayer.options())
        super.init()
        mp.drawable = view
        timer = Timer.scheduledTimer(timeInterval: 1, target: self, selector: #selector(tick), userInfo: nil, repeats: true)
    }

    /** Recrée le lecteur VLC si les réglages de lecture ont changé (relance le flux en cours). */
    func rebuildIfNeeded() {
        guard key != Prefs.playbackKey else { return }
        let u = url
        mp.stop()
        mp = VLCMediaPlayer(options: TVPlayer.options())
        mp.drawable = view
        key = Prefs.playbackKey
        setVolume(volume)
        if let u = u { play(u) }
    }

    func play(_ u: String, startAt ms: Int = 0) {
        url = u
        retries = 0
        start(startAt: ms)
    }

    private func start(startAt ms: Int = 0) {
        guard let u = url else { return }
        let m: VLCMedia
        if u.hasPrefix("/") {
            m = VLCMedia(url: URL(fileURLWithPath: u))
        } else {
            guard let x = URL(string: u) else { return }
            m = VLCMedia(url: x)
        }
        m.addOption(":network-caching=\(Prefs.caching)")
        if ms > 0 { m.addOption(":start-time=\(Double(ms) / 1000)") }
        mp.media = m
        mp.play()
        startedAt = Date()
        playingSince = nil
        lastTrackCount = -1
        applyAspect()
    }

    func stop() {
        url = nil
        mp.stop()
    }

    func close() {
        stop()
        timer?.invalidate()
        timer = nil
    }

    var isPlaying: Bool { mp.isPlaying }

    func togglePause() {
        if mp.isPlaying { mp.pause() } else { mp.play() }
    }

    /** Volume 0…200 (100 = normal). */
    func setVolume(_ v: Int) {
        volume = v
        let a: VLCAudio? = mp.audio
        a?.volume = Int32(max(0, min(200, v)))
    }

    func applyAspect() {
        let a = Prefs.aspect
        mp.videoAspectRatio = a.isEmpty ? nil : strdup(a)
    }

    // MARK: temps (enregistrements)

    var timeMs: Int { Int(mp.time.intValue) }
    var lengthMs: Int { Int(mp.media?.length.intValue ?? 0) }
    var position: Float {
        get { mp.position }
        set { mp.position = newValue }
    }

    func seek(byMs d: Int) {
        let t = max(0, timeMs + d)
        mp.time = VLCTime(int: Int32(t))
    }

    // MARK: pistes

    struct Track { let id: Int; let name: String }

    private static func list(_ names: [Any]?, _ ids: [Any]?) -> [Track] {
        let n = (names ?? []).map { "\($0)" }
        let i = (ids ?? []).compactMap { ($0 as? NSNumber)?.intValue }
        var out: [Track] = []
        for k in 0..<min(n.count, i.count) where i[k] >= 0 { out.append(Track(id: i[k], name: n[k])) }
        return out
    }

    var audioTracks: [Track] {
        let names: [Any]? = mp.audioTrackNames
        let ids: [Any]? = mp.audioTrackIndexes
        return TVPlayer.list(names, ids)
    }

    var spuTracks: [Track] {
        let names: [Any]? = mp.videoSubTitlesNames
        let ids: [Any]? = mp.videoSubTitlesIndexes
        return TVPlayer.list(names, ids)
    }

    var audioTrack: Int {
        get { Int(mp.currentAudioTrackIndex) }
        set { mp.currentAudioTrackIndex = Int32(newValue) }
    }

    var spuTrack: Int {
        get { Int(mp.currentVideoSubTitleIndex) }
        set { mp.currentVideoSubTitleIndex = Int32(newValue) }
    }

    // MARK: surveillance

    @objc private func tick() {
        guard url != nil else { return }
        let st = mp.state
        if st == .playing {
            if playingSince == nil {
                playingSince = Date()
                onPlaying?()
            }
            if let p = playingSince, Date().timeIntervalSince(p) > 15 { retries = 0 }
        }
        // Pistes : réappliquer les préférences pendant les premières secondes (elles arrivent peu à peu)
        if Date().timeIntervalSince(startedAt) < 20 {
            let n = audioTracks.count + spuTracks.count
            if n != lastTrackCount {
                lastTrackCount = n
                onTracksChanged?()
            }
        }
        guard live else { return }
        let failed = st == .error || st == .ended || (st == .stopped && Date().timeIntervalSince(startedAt) > 3)
        if failed && Date().timeIntervalSince(startedAt) > 2 {
            retries += 1
            if retries > 5 {
                stop()   // la chaîne se relance en zappant
                onFailed?()
                return
            }
            onRetry?(retries)
            start()
        }
    }
}
