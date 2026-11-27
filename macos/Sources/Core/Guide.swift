import Foundation

/** Un programme du guide (heures en secondes depuis 1970). */
public struct Prog: Equatable {
    public let start: Int
    public let end: Int
    public let title: String
    public let episode: String?
    public let synopsis: String?

    public init(start: Int, end: Int, title: String, episode: String?, synopsis: String?) {
        self.start = start
        self.end = end
        self.title = title
        self.episode = episode
        self.synopsis = synopsis
    }
}

public struct GuideCh {
    public let num: String
    public let name: String
    public var progs: [Prog] = []
}

public func nowEpoch() -> Int { Int(Date().timeIntervalSince1970) }

public func hm(_ t: Int) -> String {
    let f = DateFormatter()
    f.dateFormat = "HH:mm"
    return f.string(from: Date(timeIntervalSince1970: TimeInterval(t)))
}

public enum GuideError: Error {
    case noTuner(String)     // discover.json illisible ou sans DeviceAuth
    case service(String)     // api.hdhomerun.com en erreur
    case empty               // aucune donnée
}

/** Guide chargé : programmes par numéro de chaîne. */
public struct GuideData {
    public var channels: [String: GuideCh] = [:]
    public var order: [String] = []
    public var maxEnd = 0
    public var loadedAt = Date.distantPast

    public init() {}

    public func at(_ ch: Channel, _ t: Int = nowEpoch()) -> Prog? {
        channels[ch.num]?.progs.first { t >= $0.start && t < $0.end }
    }

    public func next(_ ch: Channel, _ t: Int = nowEpoch()) -> Prog? {
        channels[ch.num]?.progs.first { $0.start >= t }
    }

    /** À rafraîchir : jamais chargé, plus de 3 h, ou fin proche. */
    public var stale: Bool {
        Date().timeIntervalSince(loadedAt) > 3 * 3600 || maxEnd < nowEpoch() + 6 * 3600
    }

    /**
     * Ajoute une page de programmes (réponse de guide.php), sans doublons ;
     * renvoie la fin la plus tardive de la page.
     */
    @discardableResult
    public mutating func merge(_ arr: [[String: Any]]) -> Int {
        var maxE = 0
        for c in arr {
            let num = c.str("GuideNumber")
            guard !num.isEmpty, let progs = c["Guide"] as? [[String: Any]] else { continue }
            if channels[num] == nil {
                channels[num] = GuideCh(num: num, name: c.str("GuideName"))
                order.append(num)
            }
            for p in progs {
                let st = p.int("StartTime")
                let en = p.int("EndTime")
                if en <= st { continue }
                if let last = channels[num]!.progs.last, st < last.end { continue } // chevauchement entre pages
                let et = p.str("EpisodeTitle")
                let enr = p.str("EpisodeNumber")
                let ep: String?
                if !et.isEmpty && !enr.isEmpty { ep = "\(et) (\(enr))" } else { ep = !et.isEmpty ? et : (!enr.isEmpty ? enr : nil) }
                let syn = p.str("Synopsis")
                channels[num]!.progs.append(Prog(start: st, end: en, title: p.str("Title"), episode: ep,
                                                 synopsis: syn.isEmpty ? nil : syn))
                maxE = max(maxE, en)
            }
        }
        if maxE > maxEnd { maxEnd = maxE }
        return maxE
    }
}

/**
 * Guide TV — service guide de SiliconDust (api.hdhomerun.com), interrogé avec le
 * DeviceAuth publié par le tuner (http://<tuner>/discover.json).  Chaque requête
 * renvoie quelques heures de programmes ; on enchaîne (&Start=) jusqu'à ~24 h.
 */
public enum Guide {
    public static var apiBase = "https://api.hdhomerun.com/api/guide.php"

    public static func load(base: String) async throws -> GuideData {
        let auth: String
        do {
            auth = try await Net.getObject("\(base)/discover.json").str("DeviceAuth")
        } catch {
            throw GuideError.noTuner(base)
        }
        if auth.isEmpty { throw GuideError.noTuner(base) }
        var g = GuideData()
        let now = nowEpoch()
        var start = 0
        var end = 0
        var firstError: Error?
        let enc = auth.addingPercentEncoding(withAllowedCharacters: .alphanumerics) ?? auth
        for _ in 0..<8 {
            var url = "\(apiBase)?DeviceAuth=\(enc)"
            if start > 0 { url += "&Start=\(start)" }
            let arr: [[String: Any]]
            do {
                guard let a = try await Net.getJSON(url, timeout: 15) as? [[String: Any]] else { throw NetError.badJSON }
                arr = a
            } catch {
                if g.channels.isEmpty { firstError = error }
                break
            }
            let before = end
            end = max(end, g.merge(arr))
            if end <= before { break }
            start = end
            if end >= now + 24 * 3600 { break }
        }
        if g.channels.isEmpty {
            if let e = firstError { throw GuideError.service(Net.describe(e)) }
            throw GuideError.empty
        }
        g.loadedAt = Date()
        return g
    }
}
