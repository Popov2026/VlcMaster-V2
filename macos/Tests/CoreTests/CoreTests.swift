import XCTest
@testable import HomeRunCore

final class CoreTests: XCTestCase {

    func testPlaylistHDHomeRun() {
        let m3u = """
        \u{FEFF}#EXTM3U
        #EXTINF:-1 channel-id="1" channel-number="1" tvg-name="TF1",1 TF1
        http://192.168.1.90:5004/auto/v1
        #EXTINF:-1 tvg-chno="5.1" tvg-name="France 5",France 5
        http://192.168.1.90:5004/auto/v5.1

        #EXTINF:-1,
        http://192.168.1.90:5004/auto/v7
        """
        let l = Playlist.parse(m3u)
        XCTAssertEqual(l.count, 3)
        XCTAssertEqual(l[0], Channel(num: "1", name: "TF1", url: "http://192.168.1.90:5004/auto/v1"))
        XCTAssertEqual(l[1].num, "5.1")
        XCTAssertEqual(l[1].name, "France 5")
        XCTAssertEqual(l[2].num, "7")
        XCTAssertEqual(l[2].name, "v7")
        XCTAssertEqual(l[0].label, "1  TF1")
    }

    func testTunerBase() {
        XCTAssertEqual(tunerBase(of: "http://192.168.1.90/lineup.m3u"), "http://192.168.1.90")
        XCTAssertEqual(tunerBase(of: " http://127.0.0.1:8090/lineup.m3u"), "http://127.0.0.1:8090")
        XCTAssertEqual(tunerBase(of: "n'importe quoi"), "http://192.168.1.90")
    }

    func testGuideMergeAndLookup() {
        let t0 = nowEpoch() / 1800 * 1800
        var g = GuideData()
        let page1: [[String: Any]] = [[
            "GuideNumber": "1", "GuideName": "TF1",
            "Guide": [
                ["StartTime": t0 - 1800, "EndTime": t0 + 1800, "Title": "Journal", "EpisodeTitle": "Édition", "EpisodeNumber": "S01E02"],
                ["StartTime": t0 + 1800, "EndTime": t0 + 5400, "Title": "Film", "Synopsis": "Un film."],
            ] as [[String: Any]],
        ]]
        let page2: [[String: Any]] = [[
            "GuideNumber": "1", "GuideName": "TF1",
            "Guide": [
                ["StartTime": t0 + 1800, "EndTime": t0 + 5400, "Title": "Film"],      // doublon
                ["StartTime": t0 + 5400, "EndTime": t0 + 9000, "Title": "Série"],
                ["StartTime": t0 + 9000, "EndTime": t0 + 9000, "Title": "Vide"],      // durée nulle
            ] as [[String: Any]],
        ]]
        XCTAssertEqual(g.merge(page1), t0 + 5400)
        XCTAssertEqual(g.merge(page2), t0 + 9000)
        XCTAssertEqual(g.channels["1"]?.progs.count, 3)
        let ch = Channel(num: "1", name: "TF1", url: "")
        XCTAssertEqual(g.at(ch)?.title, "Journal")
        XCTAssertEqual(g.at(ch)?.episode, "Édition (S01E02)")
        XCTAssertEqual(g.next(ch)?.title, "Film")
        XCTAssertEqual(g.next(ch)?.synopsis, "Un film.")
        XCTAssertNil(g.at(Channel(num: "9", name: "", url: "")))
        XCTAssertEqual(g.order, ["1"])
    }

    func testUpdater() {
        XCTAssertGreaterThan(Updater.compare("3.10", "3.9"), 0)
        XCTAssertEqual(Updater.compare("v3.7", "3.7.0"), 0)
        XCTAssertLessThan(Updater.compare("3.6.2", "3.7"), 0)
        let j: [String: Any] = ["tag_name": "v3.8", "assets": [
            ["name": "HomeRunPlayer-3.8.zip", "browser_download_url": "https://x/zip"],
            ["name": "HomeRunPlayer-3.8-macos.dmg", "browser_download_url": "https://x/dmg"],
        ]]
        let i = Updater.parse(j, current: "3.7")
        XCTAssertEqual(i?.version, "3.8")
        XCTAssertEqual(i?.dmgURL, "https://x/dmg")
        XCTAssertEqual(i?.newer, true)
        let noDmg = Updater.parse(["tag_name": "3.9", "assets": [Any]()], current: "3.7")
        XCTAssertNil(noDmg?.dmgURL)
    }

    func testNaming() {
        var c = DateComponents()
        c.year = 2026; c.month = 3; c.day = 4; c.hour = 20; c.minute = 5
        let d = Calendar.current.date(from: c)!
        XCTAssertEqual(Naming.baseName(date: d, channel: "France 2", title: "Le 20h: édition/spéciale?"),
                       "2026-03-04 20h05 - France 2 - Le 20h édition spéciale")
        XCTAssertEqual(Naming.baseName(date: d, channel: "M6", title: nil), "2026-03-04 20h05 - M6")
        XCTAssertEqual(Naming.duration(3720), "1 h 02")
        XCTAssertEqual(Naming.duration(7200), "2 h")
        XCTAssertEqual(Naming.duration(1800), "30 min")
        XCTAssertEqual(Naming.duration(95), "1 min 35 s")
        XCTAssertEqual(Naming.clock(3_723_000), "1:02:03")
        XCTAssertEqual(Naming.clock(123_000), "2:03")
        let dir = URL(fileURLWithPath: "/tmp")
        var taken: Set<String> = ["/tmp/a.ts", "/tmp/a (2).ts"]
        XCTAssertEqual(Naming.freeFile(dir: dir, base: "a") { taken.contains($0.path) }.lastPathComponent, "a (3).ts")
        taken = []
        XCTAssertEqual(Naming.freeFile(dir: dir, base: "a") { taken.contains($0.path) }.lastPathComponent, "a.ts")
    }

    func testTrackNames() {
        XCTAssertEqual(TrackNames.key("Track 1 - [French]"), "french")
        XCTAssertEqual(TrackNames.key("Piste 3 - [qad] audio description"), "qad|ad")
        XCTAssertEqual(TrackNames.key("Sous-titres - [French] malentendants"), "french|hoh")
        let sp: (String) -> String? = { $0 == "qaa" ? "Version originale" : nil }
        XCTAssertEqual(TrackNames.pretty("Track 1 - [French]", ui: "fr", special: sp, adLabel: "AD", hohLabel: "HoH"), "Français")
        XCTAssertEqual(TrackNames.pretty("Track 2 - [qaa]", ui: "fr", special: sp, adLabel: "AD", hohLabel: "HoH"), "Version originale")
        XCTAssertEqual(TrackNames.pretty("Track 4 - [eng] hard of hearing", ui: "en", special: sp, adLabel: "AD", hohLabel: "HoH"), "English (HoH)")
        XCTAssertEqual(TrackNames.pretty("Sans crochets", ui: "fr", special: sp, adLabel: "AD", hohLabel: "HoH"), "Sans crochets")
    }

    /**
     * Logique du scan, contre le faux tuner (tools/fake_tuner.py) : jamais contre un vrai.
     * Lancé en CI avec FAKE_TUNER=http://127.0.0.1:8090 ; ignoré sinon.
     */
    @MainActor
    func testScanAgainstFakeTuner() async throws {
        guard let base = ProcessInfo.processInfo.environment["FAKE_TUNER"] else {
            throw XCTSkip("FAKE_TUNER non défini")
        }
        XCTAssertTrue(base.hasPrefix("http://127.0.0.1") || base.hasPrefix("http://localhost"), "faux tuner local uniquement")
        let info = try await Tuner.info(base: base)
        XCTAssertEqual(info.model, "HDFX-4DT")
        XCTAssertEqual(info.upgradeAvailable, "20251201")
        XCTAssertEqual(info.tunerCount, 4)
        let before = try await Tuner.status(base: base)
        XCTAssertFalse(before.inProgress)
        XCTAssertEqual(before.source, "Antenna")
        let chans = try await Playlist.load("\(base)/lineup.m3u")
        XCTAssertEqual(chans.count, 6)

        try await Tuner.startScan(base: base, source: before.source)
        var seen: [ScanStatus] = []
        let done = await Tuner.follow(base: base, interval: 0.5) { seen.append($0) }
        XCTAssertTrue(done, "le scan doit être vu en cours puis terminé")
        XCTAssertTrue(seen.contains { $0.inProgress && $0.progress > 0 })
        XCTAssertFalse(seen.last!.inProgress)

        // Arrêt d'un scan
        try await Tuner.startScan(base: base, source: "Antenna")
        try await Task.sleep(nanoseconds: 1_500_000_000)
        let during = try await Tuner.status(base: base)
        XCTAssertTrue(during.inProgress)
        try await Tuner.abortScan(base: base)
        let after = try await Tuner.status(base: base)
        XCTAssertFalse(after.inProgress)
    }

    func testGuideAgainstFakeTuner() async throws {
        guard let base = ProcessInfo.processInfo.environment["FAKE_TUNER"] else {
            throw XCTSkip("FAKE_TUNER non défini")
        }
        // Le faux tuner publie DeviceAuth=fake ; le service guide est remplacé par le faux aussi.
        let saved = Guide.apiBase
        Guide.apiBase = "\(base)/fake_guide.php"
        defer { Guide.apiBase = saved }
        let g = try await Guide.load(base: base)
        XCTAssertFalse(g.channels.isEmpty)
        XCTAssertGreaterThanOrEqual(g.maxEnd, nowEpoch() + 24 * 3600 - 3 * 3600)
    }
}
