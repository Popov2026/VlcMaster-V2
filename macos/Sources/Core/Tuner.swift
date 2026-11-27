import Foundation

/** Infos du tuner (discover.json). */
public struct TunerInfo {
    public var friendlyName = ""
    public var model = ""
    public var deviceID = ""
    public var tunerCount = 0
    public var firmware = ""
    public var upgradeAvailable = ""

    public init() {}

    public init(_ d: [String: Any]) {
        friendlyName = d.str("FriendlyName")
        model = d.str("ModelNumber")
        deviceID = d.str("DeviceID")
        tunerCount = d.int("TunerCount")
        firmware = (d.str("FirmwareName") + " " + d.str("FirmwareVersion")).trimmingCharacters(in: .whitespaces)
        upgradeAvailable = d.str("UpgradeAvailable")
    }
}

/** État du scan (lineup_status.json). */
public struct ScanStatus: Equatable {
    public var inProgress = false
    public var progress = 0
    public var found = 0
    public var source = ""

    public init() {}

    public init(_ d: [String: Any]) {
        inProgress = d.int("ScanInProgress") == 1
        progress = d.int("Progress")
        found = d.int("Found")
        source = d.str("Source")
    }
}

/**
 * Tuner HDHomeRun : infos, mise à jour firmware disponible, scan des chaînes
 * (lineup.post?scan=start, suivi par lineup_status.json).          (↔ src/tuner.h)
 */
public enum Tuner {
    public static func info(base: String) async throws -> TunerInfo {
        TunerInfo(try await Net.getObject("\(base)/discover.json"))
    }

    public static func status(base: String) async throws -> ScanStatus {
        ScanStatus(try await Net.getObject("\(base)/lineup_status.json"))
    }

    public static func startScan(base: String, source: String) async throws {
        let src = source.isEmpty ? "Antenna" : source
        let enc = src.addingPercentEncoding(withAllowedCharacters: .alphanumerics) ?? src
        try await Net.post("\(base)/lineup.post?scan=start&source=\(enc)")
    }

    public static func abortScan(base: String) async throws {
        try await Net.post("\(base)/lineup.post?scan=abort")
    }

    /**
     * Suit un scan jusqu'à la fin.  `onProgress` reçoit chaque état lu ; renvoie
     * true si un scan a été vu en cours puis s'est terminé (liste à recharger).
     * S'arrête si la tâche est annulée.  Sur le fil principal (onProgress met à jour l'interface).
     */
    @MainActor
    public static func follow(base: String, interval: TimeInterval = 1,
                              onProgress: (ScanStatus) -> Void) async -> Bool {
        var seen = false
        var failures = 0
        while !Task.isCancelled {
            if let s = try? await status(base: base) {
                failures = 0
                onProgress(s)
                if s.inProgress {
                    seen = true
                } else {
                    return seen
                }
            } else {
                failures += 1
                if failures > 30 { return false }
            }
            try? await Task.sleep(nanoseconds: UInt64(interval * 1_000_000_000))
        }
        return false
    }
}
