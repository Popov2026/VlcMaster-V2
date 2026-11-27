import Foundation

/**
 * Pistes audio (VF, VO « qaa », audiodescription « qad »…) et sous-titres DVB.
 * Le choix est mémorisé par une clé (texte entre crochets du nom de piste, ex.
 * « [French] », plus audiodescription / malentendants) et réappliqué à chaque
 * nouvelle chaîne qui propose la même piste.                        (↔ src/tracks.h)
 */
public enum TrackNames {
    public static func bracket(_ raw: String) -> String? {
        guard let a = raw.range(of: "[", options: .backwards),
              let b = raw.range(of: "]", range: a.upperBound..<raw.endIndex) else { return nil }
        let s = raw[a.upperBound..<b.lowerBound].trimmingCharacters(in: .whitespaces)
        return s.isEmpty ? nil : s
    }

    public static func isAD(_ raw: String) -> Bool {
        let l = raw.lowercased()
        return l.contains("audio description") || l.contains("audiodescription")
    }

    public static func isHoH(_ raw: String) -> Bool {
        let l = raw.lowercased()
        return l.contains("hard of hearing") || l.contains("hearing impaired") || l.contains("malentendants")
    }

    /** Clé mémorisée : langue + type de piste. */
    public static func key(_ raw: String) -> String {
        (bracket(raw) ?? raw).lowercased() + (isAD(raw) ? "|ad" : "") + (isHoH(raw) ? "|hoh" : "")
    }

    private static let englishNames: [String: String] = {
        var m: [String: String] = [:]
        let en = Locale(identifier: "en")
        for code in Locale.isoLanguageCodes {
            if let n = en.localizedString(forLanguageCode: code), !n.isEmpty { m[n.lowercased()] = code }
        }
        return m
    }()

    /** Code ISO d'un nom de langue anglais (« French ») ou d'un code (« fre », « fra », « fr »). */
    public static func code(_ s: String) -> String? {
        let l = s.lowercased()
        if let c = englishNames[l] { return c }
        let special = ["fre": "fr", "fra": "fr", "eng": "en", "ger": "de", "deu": "de", "spa": "es",
                       "ita": "it", "por": "pt", "dut": "nl", "nld": "nl"]
        if let c = special[l] { return c }
        if (2...3).contains(l.count), l.allSatisfy({ $0.isLetter }) { return l }
        return nil
    }

    /**
     * « Track 1 - [French] » → « Français » (dans la langue `ui`), avec
     * audiodescription / malentendants en suffixe.  `special` traduit qaa, qad…
     */
    public static func pretty(_ raw: String, ui: String, special: (String) -> String?,
                              adLabel: String, hohLabel: String) -> String {
        guard let b = bracket(raw) else { return raw }
        var s: String
        if let sp = special(b.lowercased()) {
            s = sp
        } else if let c = code(b), let n = Locale(identifier: ui).localizedString(forLanguageCode: c),
                  !n.isEmpty, n.lowercased() != c {
            s = n.prefix(1).uppercased() + n.dropFirst()
        } else {
            s = b
        }
        if isAD(raw) && b.lowercased() != "qad" { s += " (\(adLabel))" }
        if isHoH(raw) { s += " (\(hohLabel))" }
        return s
    }
}
