import Foundation

/** Une chaîne de la playlist. */
public struct Channel: Equatable {
    public let num: String
    public let name: String
    public let url: String

    public init(num: String, name: String, url: String) {
        self.num = num
        self.name = name
        self.url = url
    }

    public var label: String { num.isEmpty ? name : "\(num)  \(name)" }
}

/** Lecture de la playlist M3U du HDHomeRun (http://<ip>/lineup.m3u). */
public enum Playlist {
    private static func capture(_ pattern: String, in s: String) -> String? {
        guard let re = try? NSRegularExpression(pattern: pattern) else { return nil }
        let ns = s as NSString
        guard let m = re.firstMatch(in: s, range: NSRange(location: 0, length: ns.length)), m.numberOfRanges > 1,
              m.range(at: 1).location != NSNotFound else { return nil }
        return ns.substring(with: m.range(at: 1))
    }

    public static func parse(_ text: String) -> [Channel] {
        var out: [Channel] = []
        var name = ""
        var num = ""
        for raw in text.components(separatedBy: .newlines) {
            var line = raw.trimmingCharacters(in: .whitespaces)
            if line.hasPrefix("\u{FEFF}") { line.removeFirst() }
            if line.isEmpty { continue }
            if line.uppercased().hasPrefix("#EXTINF") {
                num = capture("(?:channel-number|tvg-chno)=\"([^\"]*)\"", in: line) ?? ""
                if let comma = line.lastIndex(of: ",") {
                    name = String(line[line.index(after: comma)...]).trimmingCharacters(in: .whitespaces)
                } else {
                    name = ""
                }
                if name.isEmpty { name = capture("tvg-name=\"([^\"]*)\"", in: line) ?? "" }
            } else if !line.hasPrefix("#") {
                if num.isEmpty { num = capture("/v(\\d+(?:\\.\\d+)?)", in: line) ?? "" }
                if name.isEmpty { name = line.components(separatedBy: "/").last ?? line }
                // « 1 TF1 » : numéro en tête du nom (format HDHomeRun)
                if !num.isEmpty, name.hasPrefix(num + " ") {
                    name = String(name.dropFirst(num.count + 1)).trimmingCharacters(in: .whitespaces)
                }
                out.append(Channel(num: num, name: name, url: line))
                name = ""
                num = ""
            }
        }
        return out
    }

    public static func load(_ url: String) async throws -> [Channel] {
        parse(try await Net.getString(url, timeout: 10))
    }
}
