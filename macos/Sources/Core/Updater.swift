import Foundation

/**
 * Mise à jour de l'appli : compare la version à la dernière release GitHub.
 * Le bandeau n'est proposé que si la release est plus récente ET contient un .dmg
 * (une release Windows ou Android seule ne concerne pas le Mac).    (↔ src/update.h)
 */
public enum Updater {
    public static let repo = "Popov2026/VlcMaster-V2"
    public static var api = "https://api.github.com/repos/\(repo)/releases/latest"
    public static let page = "https://github.com/\(repo)/releases/latest"

    public struct Info {
        public let tag: String
        public let version: String
        public let dmgURL: String?
        public let newer: Bool
    }

    /** Comparaison numérique « 3.10 » > « 3.9 ». */
    public static func compare(_ a: String, _ b: String) -> Int {
        func parts(_ s: String) -> [Int] {
            let t = s.trimmingCharacters(in: CharacterSet(charactersIn: "vV"))
            return t.components(separatedBy: CharacterSet(charactersIn: ".- ")).map { Int($0) ?? 0 }
        }
        let x = parts(a), y = parts(b)
        for i in 0..<max(x.count, y.count) {
            let d = (i < x.count ? x[i] : 0) - (i < y.count ? y[i] : 0)
            if d != 0 { return d }
        }
        return 0
    }

    /** Interprète la réponse JSON de l'API GitHub. */
    public static func parse(_ j: [String: Any], current: String) -> Info? {
        let tag = j.str("tag_name")
        if tag.isEmpty { return nil }
        var dmg: String?
        for a in (j["assets"] as? [[String: Any]]) ?? [] where a.str("name").lowercased().hasSuffix(".dmg") {
            dmg = a.str("browser_download_url")
            break
        }
        let ver = tag.trimmingCharacters(in: CharacterSet(charactersIn: "vV"))
        return Info(tag: tag, version: ver, dmgURL: dmg, newer: compare(ver, current) > 0)
    }

    public static func check(current: String) async throws -> Info {
        guard let i = parse(try await Net.getObject(api, timeout: 10), current: current) else { throw NetError.badJSON }
        return i
    }
}
