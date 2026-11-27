import Foundation

/** Une connexion HTTP au flux d'une chaîne, copiée dans un fichier (thread URLSession). */
final class RecTask: NSObject, URLSessionDataDelegate {
    private let lock = NSLock()
    private var _bytes: Int64 = 0
    private var _running = false
    private var _error: String?
    private var handle: FileHandle?
    private var session: URLSession?
    let noTunerText: String

    init(noTunerText: String) {
        self.noTunerText = noTunerText
    }

    var bytes: Int64 { lock.lock(); defer { lock.unlock() }; return _bytes }
    var running: Bool { lock.lock(); defer { lock.unlock() }; return _running }
    var error: String? { lock.lock(); defer { lock.unlock() }; return _error }

    func connect(url: URL, file: URL) {
        if !FileManager.default.fileExists(atPath: file.path) {
            FileManager.default.createFile(atPath: file.path, contents: nil)
        }
        do {
            let h = try FileHandle(forWritingTo: file)
            h.seekToEndOfFile()
            handle = h
        } catch {
            set(running: false, error: error.localizedDescription)
            return
        }
        let cfg = URLSessionConfiguration.ephemeral
        cfg.timeoutIntervalForRequest = 15
        cfg.timeoutIntervalForResource = 7 * 24 * 3600
        cfg.requestCachePolicy = .reloadIgnoringLocalCacheData
        let q = OperationQueue()
        q.maxConcurrentOperationCount = 1
        let s = URLSession(configuration: cfg, delegate: self, delegateQueue: q)
        session = s
        var r = URLRequest(url: url)
        r.setValue(Net.userAgent, forHTTPHeaderField: "User-Agent")
        set(running: true, error: nil)
        s.dataTask(with: r).resume()
    }

    func cancel() {
        session?.invalidateAndCancel()
    }

    private func set(running: Bool, error: String?) {
        lock.lock()
        _running = running
        if let e = error { _error = e } else if running { _error = nil }
        lock.unlock()
    }

    func urlSession(_ session: URLSession, dataTask: URLSessionDataTask, didReceive response: URLResponse,
                    completionHandler: @escaping (URLSession.ResponseDisposition) -> Void) {
        let code = (response as? HTTPURLResponse)?.statusCode ?? 200
        if code == 503 {
            set(running: true, error: noTunerText)
            completionHandler(.cancel)
        } else if !(200..<300).contains(code) {
            set(running: true, error: "HTTP \(code)")
            completionHandler(.cancel)
        } else {
            completionHandler(.allow)
        }
    }

    func urlSession(_ session: URLSession, dataTask: URLSessionDataTask, didReceive data: Data) {
        handle?.write(data)
        lock.lock()
        _bytes += Int64(data.count)
        lock.unlock()
    }

    func urlSession(_ session: URLSession, task: URLSessionTask, didCompleteWithError error: Error?) {
        let prev = self.error
        let msg: String?
        if let e = error as NSError?, e.code != NSURLErrorCancelled { msg = e.localizedDescription } else { msg = prev }
        set(running: false, error: msg ?? "EOF")
        try? handle?.synchronizeFile()
        try? handle?.close()
        handle = nil
        session.finishTasksAndInvalidate()
    }
}

/** Enregistrement en cours. */
@MainActor
final class Rec {
    let id: Int
    let chName: String
    let title: String
    let url: String
    let file: URL
    let start = Date()
    var endAt: Int          // secondes epoch, 0 = sans limite
    var task: RecTask?
    var tries = 0
    var lastAttempt = Date()
    var bytesAtAttempt: Int64 = 0
    var stopRequested = false
    var bytesBefore: Int64 = 0   // octets des connexions précédentes

    init(id: Int, chName: String, title: String, url: String, file: URL, endAt: Int) {
        self.id = id
        self.chName = chName
        self.title = title
        self.url = url
        self.file = file
        self.endAt = endAt
    }

    var bytes: Int64 { bytesBefore + (task?.bytes ?? 0) }
}

/** Enregistrement programmé depuis le guide (démarre si l'appli est ouverte à l'heure). */
struct Sched: Codable, Equatable {
    var id = UUID().uuidString
    let chName: String
    let chURL: String
    let title: String
    let start: Int
    let end: Int
}

/**
 * Enregistrements : copie du flux HTTP de la chaîne dans un fichier .ts
 * (« AAAA-MM-JJ HHhMM - Chaîne - Émission.ts »), jusqu'à l'heure de fin
 * (fin de l'émission + marge, ou durée choisie) ou sans limite ; nouvelles
 * tentatives si le flux coupe.  La mise en veille est empêchée pendant ce temps.
 */
@MainActor
final class Recorder: NSObject {
    static let shared = Recorder()

    private(set) var active: [Rec] = []
    private(set) var schedules: [Sched] = []
    private var nextId = 1
    private var timer: Timer?
    private var activity: NSObjectProtocol?

    /** Changement de la liste (interface à rafraîchir). */
    var onChange: [() -> Void] = []
    /** Message de fin (« terminé · 1 h 02 · 2,1 Go » ou erreur). */
    var onEvent: ((String) -> Void)?

    override init() {
        super.init()
        if let d = Prefs.d.data(forKey: "schedules"), let l = try? JSONDecoder().decode([Sched].self, from: d) {
            schedules = l.filter { $0.end > nowEpoch() }
        }
        timer = Timer.scheduledTimer(timeInterval: 1, target: self, selector: #selector(tick), userInfo: nil, repeats: true)
    }

    var dir: URL {
        let d = Prefs.recDir
        try? FileManager.default.createDirectory(at: d, withIntermediateDirectories: true)
        return d
    }

    func isRecording(_ url: String) -> Bool { active.contains { $0.url == url } }

    /** Lance un enregistrement ; endAt en secondes epoch (0 = sans limite). */
    @discardableResult
    func start(channel: Channel, title: String?, endAt: Int) -> Rec {
        let base = Naming.baseName(date: Date(), channel: channel.name, title: title)
        let f = Naming.freeFile(dir: dir, base: base) { FileManager.default.fileExists(atPath: $0.path) }
        let r = Rec(id: nextId, chName: channel.name, title: title ?? "", url: channel.url, file: f, endAt: endAt)
        nextId += 1
        active.append(r)
        connect(r)
        updateActivity()
        changed()
        return r
    }

    func stop(_ r: Rec) {
        r.stopRequested = true
        tick()
    }

    func stopAll() {
        for r in active { r.stopRequested = true }
        tick()
    }

    private func connect(_ r: Rec) {
        if let old = r.task { r.bytesBefore += old.bytes }
        let t = RecTask(noTunerText: L("rec_err_no_tuner"))
        r.task = t
        r.lastAttempt = Date()
        r.bytesAtAttempt = r.bytes
        guard let u = URL(string: r.url) else { return }
        t.connect(url: u, file: r.file)
    }

    // MARK: programmation

    func schedule(_ s: Sched) {
        schedules.append(s)
        schedules.sort { $0.start < $1.start }
        saveSchedules()
        changed()
    }

    func cancel(_ s: Sched) {
        schedules.removeAll { $0.id == s.id }
        saveSchedules()
        changed()
    }

    func isScheduled(url: String, start: Int) -> Bool {
        schedules.contains { $0.chURL == url && $0.start == start }
    }

    private func saveSchedules() {
        if let d = try? JSONEncoder().encode(schedules) { Prefs.d.set(d, forKey: "schedules") }
    }

    // MARK: surveillance

    @objc private func tick() {
        let now = nowEpoch()
        // Programmations arrivées à l'heure (30 s d'avance)
        var due: [Sched] = []
        schedules.removeAll { s in
            if now >= s.end { return true }
            if now >= s.start - 30 { due.append(s); return true }
            return false
        }
        if !due.isEmpty {
            saveSchedules()
            for s in due {
                start(channel: Channel(num: "", name: s.chName, url: s.chURL), title: s.title,
                      endAt: s.end + Prefs.recMargin * 60)
            }
        }
        var finished: [Rec] = []
        for r in active {
            guard let t = r.task else { continue }
            let timeUp = r.endAt != 0 && now >= r.endAt
            if r.stopRequested || timeUp {
                t.cancel()
                finished.append(r)
                continue
            }
            if t.running {
                if r.bytes > r.bytesAtAttempt + 1_000_000 { r.tries = 0 }
                continue
            }
            // Coupure : quelques nouvelles tentatives tant que l'heure de fin n'est pas passée
            if Date().timeIntervalSince(r.lastAttempt) < 2 { continue }
            r.tries += 1
            if r.tries > 5 {
                finished.append(r)
            } else {
                connect(r)
            }
        }
        for r in finished { finish(r) }
        if !finished.isEmpty || !active.isEmpty { changed() }
    }

    private func finish(_ r: Rec) {
        active.removeAll { $0 === r }
        let dur = Naming.duration(Int(Date().timeIntervalSince(r.start)))
        let size = UI.fileSize(r.bytes)
        let err = r.task?.error
        let msg: String
        if r.bytes == 0 {
            try? FileManager.default.removeItem(at: r.file)   // fichier vide créé par nous
            msg = L("rec_failed", r.chName, err ?? L("rec_no_data"))
        } else if !r.stopRequested && r.tries > 5 {
            msg = L("rec_stopped_err", r.chName, err ?? "?", dur, size)
        } else {
            msg = L("rec_done", r.chName, dur, size)
        }
        updateActivity()
        onEvent?(msg)
    }

    private func updateActivity() {
        if active.isEmpty {
            if let a = activity { ProcessInfo.processInfo.endActivity(a) }
            activity = nil
        } else if activity == nil {
            activity = ProcessInfo.processInfo.beginActivity(options: [.idleSystemSleepDisabled, .userInitiated],
                                                             reason: "HomeRun Player recording")
        }
    }

    private func changed() {
        for f in onChange { f() }
    }
}
