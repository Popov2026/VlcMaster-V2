import AppKit

/**
 * Langues : pistes audio (VF, VO « qaa », audiodescription « qad »…) et sous-titres DVB
 * du lecteur qui a le son.  Le choix est mémorisé (Prefs.audioPref / spuPref) et
 * réappliqué sur les autres chaînes.                                (↔ src/tracks.h)
 */
@MainActor
enum Tracks {
    struct T { let id: Int; let label: String; let key: String }

    nonisolated private static func special(_ s: String) -> String? {
        switch s {
        case "qaa", "original audio", "original language": return L("trk_qaa")
        case "qad", "audio description": return L("trk_qad")
        case "qtz": return L("trk_qtz")
        case "mis": return L("trk_mis")
        case "und": return L("trk_und")
        default: return nil
        }
    }

    static func pretty(_ raw: String) -> String {
        TrackNames.pretty(raw, ui: L10n.lang, special: special, adLabel: L("trk_ad"), hohLabel: L("trk_hoh"))
    }

    static func audio(_ p: TVPlayer) -> [T] {
        p.audioTracks.map { T(id: $0.id, label: pretty($0.name), key: TrackNames.key($0.name)) }
    }

    static func spu(_ p: TVPlayer) -> [T] {
        p.spuTracks.map { T(id: $0.id, label: pretty($0.name), key: TrackNames.key($0.name)) }
    }

    /** Réapplique les choix mémorisés sur une nouvelle chaîne. */
    static func applyPrefs(_ p: TVPlayer) {
        let ap = Prefs.audioPref
        if !ap.isEmpty, let t = audio(p).first(where: { $0.key == ap }), p.audioTrack != t.id {
            p.audioTrack = t.id
        }
        let sp = Prefs.spuPref
        if sp == "off" {
            if p.spuTrack != -1 { p.spuTrack = -1 }
        } else if !sp.isEmpty, let t = spu(p).first(where: { $0.key == sp }), p.spuTrack != t.id {
            p.spuTrack = t.id
        }
    }

    /** Piste audio suivante ; renvoie son nom. */
    static func cycleAudio(_ p: TVPlayer) -> String? {
        let l = audio(p)
        if l.isEmpty { return nil }
        let i = l.firstIndex { $0.id == p.audioTrack } ?? -1
        let t = l[(i + 1) % l.count]
        p.audioTrack = t.id
        Prefs.audioPref = t.key
        return t.label
    }

    /** Sous-titres suivants (désactivés compris) ; renvoie leur nom. */
    static func cycleSpu(_ p: TVPlayer) -> String? {
        let l = spu(p)
        if l.isEmpty { return nil }
        let i = l.firstIndex { $0.id == p.spuTrack } ?? -1
        if i == l.count - 1 {
            p.spuTrack = -1
            Prefs.spuPref = "off"
            return L("trk_spu_off")
        }
        let t = l[i + 1]
        p.spuTrack = t.id
        Prefs.spuPref = t.key
        return t.label
    }

    /** Menu « Langue » : pistes audio puis sous-titres.  `done` reçoit le nom choisi. */
    static func menu(_ p: TVPlayer, done: @escaping (String) -> Void) -> NSMenu {
        let m = NSMenu(title: L("lang_title"))
        let aud = audio(p), sub = spu(p)
        if aud.isEmpty && sub.isEmpty {
            let it = NSMenuItem(title: L("trk_none"), action: nil, keyEquivalent: "")
            it.isEnabled = false
            m.addItem(it)
            return m
        }
        let curA = p.audioTrack, curS = p.spuTrack
        for t in aud {
            m.addItem(ClosureMenuItem(L("trk_audio_item", t.label), checked: t.id == curA) {
                p.audioTrack = t.id
                Prefs.audioPref = t.key
                done(t.label)
            })
        }
        if !sub.isEmpty {
            m.addItem(.separator())
            m.addItem(ClosureMenuItem(L("trk_spu_item", L("trk_spu_off")), checked: curS < 0) {
                p.spuTrack = -1
                Prefs.spuPref = "off"
                done(L("trk_spu_off"))
            })
            for t in sub {
                m.addItem(ClosureMenuItem(L("trk_spu_item", t.label), checked: t.id == curS) {
                    p.spuTrack = t.id
                    Prefs.spuPref = t.key
                    done(t.label)
                })
            }
        }
        return m
    }
}

/** Élément de menu qui exécute une fermeture. */
final class ClosureMenuItem: NSMenuItem {
    private let run: () -> Void

    init(_ title: String, checked: Bool = false, enabled: Bool = true, _ run: @escaping () -> Void) {
        self.run = run
        super.init(title: title, action: #selector(fire), keyEquivalent: "")
        target = self
        state = checked ? .on : .off
        isEnabled = enabled
    }

    required init(coder: NSCoder) { fatalError() }

    @objc private func fire() { run() }
}
