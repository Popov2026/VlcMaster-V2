import Foundation

/** Noms de fichiers et durées des enregistrements.                     (↔ src/record.h) */
public enum Naming {
    /** Retire les caractères interdits dans un nom de fichier. */
    public static func sanitize(_ s: String) -> String {
        var t = s.replacingOccurrences(of: "[\\\\/:*?\"<>|\\p{Cc}]", with: " ", options: .regularExpression)
        t = t.replacingOccurrences(of: "\\s+", with: " ", options: .regularExpression)
        return t.trimmingCharacters(in: .whitespaces)
    }

    /** « AAAA-MM-JJ HHhMM - Chaîne - Émission » (sans extension). */
    public static func baseName(date: Date, channel: String, title: String?) -> String {
        let f = DateFormatter()
        f.locale = Locale(identifier: "en_US_POSIX")
        f.dateFormat = "yyyy-MM-dd HH'h'mm"
        var s = f.string(from: date) + " - " + String(sanitize(channel).prefix(60))
        if let t = title {
            let c = String(sanitize(t).prefix(80))
            if !c.isEmpty { s += " - " + c }
        }
        return s
    }

    /** Fichier .ts libre dans `dir` (ajoute « (2) », « (3) »… si besoin). */
    public static func freeFile(dir: URL, base: String, exists: (URL) -> Bool) -> URL {
        var f = dir.appendingPathComponent(base + ".ts")
        var k = 2
        while exists(f) && k < 100 {
            f = dir.appendingPathComponent("\(base) (\(k)).ts")
            k += 1
        }
        return f
    }

    /** 3720 → « 1 h 02 », 1800 → « 30 min », 95 → « 1 min 35 s ». */
    public static func duration(_ sec: Int) -> String {
        let h = sec / 3600, m = (sec / 60) % 60, s = sec % 60
        if h > 0 && m == 0 { return "\(h) h" }
        if h > 0 { return String(format: "%ld h %02ld", h, m) }
        if s == 0 { return "\(m) min" }
        return String(format: "%ld min %02ld s", m, s)
    }

    /** Position de lecture « 1:02:03 » ou « 2:03 ». */
    public static func clock(_ ms: Int) -> String {
        let t = max(0, ms / 1000)
        let h = t / 3600, m = (t / 60) % 60, s = t % 60
        return h > 0 ? String(format: "%ld:%02ld:%02ld", h, m, s) : String(format: "%ld:%02ld", m, s)
    }
}
