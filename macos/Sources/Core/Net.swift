import Foundation

/*
 * HomeRun Player pour macOS — cœur sans interface (compilé dans l'appli ET dans le
 * paquet Swift « HomeRunCore » testé par `swift test` en CI).
 *
 *   Net.swift        requêtes HTTP (URLSession)
 *   Playlist.swift   playlist M3U du tuner                         ↔ android Net.kt
 *   Guide.swift      guide TV (api.hdhomerun.com)                  ↔ src/guide_data.h
 *   Tuner.swift      discover.json, lineup_status.json, scan       ↔ src/tuner.h
 *   Updater.swift    dernière release GitHub                       ↔ src/update.h
 *   Naming.swift     noms de fichiers d'enregistrement, durées     ↔ src/record.h
 *   TrackNames.swift clés et noms des pistes audio / sous-titres   ↔ src/tracks.h
 */

public enum NetError: Error, LocalizedError {
    case badURL(String)
    case http(Int)
    case badJSON

    public var errorDescription: String? {
        switch self {
        case .badURL(let u): return "URL ? \(u)"
        case .http(let c): return "HTTP \(c)"
        case .badJSON: return "JSON ?"
        }
    }
}

/** Requêtes HTTP simples (async). */
public enum Net {
    public static var userAgent = "HomeRunPlayer-macOS"

    public static func request(_ url: String, method: String = "GET", timeout: TimeInterval = 8) async throws -> Data {
        guard let u = URL(string: url.trimmingCharacters(in: .whitespaces)) else { throw NetError.badURL(url) }
        var r = URLRequest(url: u, cachePolicy: .reloadIgnoringLocalCacheData, timeoutInterval: timeout)
        r.httpMethod = method
        r.setValue(userAgent, forHTTPHeaderField: "User-Agent")
        if method == "POST" { r.httpBody = Data() }
        return try await withCheckedThrowingContinuation { c in
            URLSession.shared.dataTask(with: r) { d, resp, err in
                if let err = err { c.resume(throwing: err); return }
                let code = (resp as? HTTPURLResponse)?.statusCode ?? 0
                guard (200..<300).contains(code) else { c.resume(throwing: NetError.http(code)); return }
                c.resume(returning: d ?? Data())
            }.resume()
        }
    }

    public static func getString(_ url: String, timeout: TimeInterval = 8) async throws -> String {
        let d = try await request(url, timeout: timeout)
        return String(decoding: d, as: UTF8.self)
    }

    public static func getJSON(_ url: String, timeout: TimeInterval = 8) async throws -> Any {
        let d = try await request(url, timeout: timeout)
        do { return try JSONSerialization.jsonObject(with: d) } catch { throw NetError.badJSON }
    }

    public static func getObject(_ url: String, timeout: TimeInterval = 8) async throws -> [String: Any] {
        guard let o = try await getJSON(url, timeout: timeout) as? [String: Any] else { throw NetError.badJSON }
        return o
    }

    @discardableResult
    public static func post(_ url: String, timeout: TimeInterval = 8) async throws -> Data {
        try await request(url, method: "POST", timeout: timeout)
    }

    /** Message d'erreur lisible. */
    public static func describe(_ e: Error) -> String {
        if let n = e as? NetError { return n.errorDescription ?? "\(n)" }
        return e.localizedDescription
    }
}

/** Petits accès typés aux objets JSON. */
public extension Dictionary where Key == String, Value == Any {
    func str(_ k: String) -> String {
        if let s = self[k] as? String { return s }
        if let n = self[k] as? NSNumber { return n.stringValue }
        return ""
    }
    func int(_ k: String) -> Int {
        if let n = self[k] as? NSNumber { return n.intValue }
        if let s = self[k] as? String { return Int(s) ?? 0 }
        return 0
    }
}

/** Adresse du tuner (http://hôte[:port]) d'après l'adresse de la playlist. */
public func tunerBase(of playlistURL: String) -> String {
    let s = playlistURL.trimmingCharacters(in: .whitespaces)
    if let r = s.range(of: "^https?://[^/]+", options: [.regularExpression, .caseInsensitive]) {
        return String(s[r])
    }
    return "http://192.168.1.90"
}
