#!/usr/bin/env python3
"""Faux HDHomeRun local pour tester l'écran Tuner (scan des chaînes) sans toucher au vrai tuner.

    python tools/fake_tuner.py [port]          (port par défaut : 8090)

Copie de android/tools/fake_tuner.py pour la version macOS (tests `swift test` en CI) :
mettre http://127.0.0.1:8090/lineup.m3u comme adresse de playlist dans ⚙ Paramètres.
Ajout : /fake_guide.php imite api.hdhomerun.com/api/guide.php (pages de 4 h, &Start=).

Sert discover.json (avec UpgradeAvailable), lineup.m3u, lineup_status.json, et
accepte POST /lineup.post?scan=start|abort (le scan simulé dure ~20 s).
Les flux des chaînes renvoient l'adresse du vrai tuner (lecture seule).
"""
import json
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

REAL = "http://192.168.1.90:5004/auto"
CHANNELS = [("1", "TF1"), ("2", "France 2"), ("3", "France 3"), ("5", "France 5"), ("6", "M6"), ("7", "Arte")]
scan = {"start": 0.0, "abort": False}
SCAN_SECONDS = 20


def status():
    if scan["start"] and not scan["abort"]:
        el = time.time() - scan["start"]
        if el < SCAN_SECONDS:
            p = int(el * 100 / SCAN_SECONDS)
            return {"ScanInProgress": 1, "Progress": p, "Found": int(p * len(CHANNELS) / 100)}
    return {"ScanInProgress": 0, "ScanPossible": 1, "Source": "Antenna", "SourceList": ["Antenna"]}


class H(BaseHTTPRequestHandler):
    def send(self, code, body, ctype="application/json"):
        b = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(b)))
        self.end_headers()
        self.wfile.write(b)

    def do_GET(self):
        u = urlparse(self.path)
        host = self.headers.get("Host", "127.0.0.1:8090")
        if u.path == "/discover.json":
            self.send(200, json.dumps({
                "FriendlyName": "Faux HDHomeRun", "ModelNumber": "HDFX-4DT", "FirmwareName": "hdhomerun_fake",
                "FirmwareVersion": "20240101", "UpgradeAvailable": "20251201", "DeviceID": "FAKE0001",
                "DeviceAuth": "fake", "TunerCount": 4, "BaseURL": f"http://{host}",
                "LineupURL": f"http://{host}/lineup.json"}))
        elif u.path == "/lineup_status.json":
            self.send(200, json.dumps(status()))
        elif u.path == "/fake_guide.php":
            q = parse_qs(u.query)
            start = int(q.get("Start", ["0"])[0]) or int(time.time()) // 1800 * 1800
            out = []
            for n, name in CHANNELS:
                progs = [{"StartTime": start + i * 3600, "EndTime": start + (i + 1) * 3600,
                          "Title": f"{name} {i}", "Synopsis": "Programme simulé"} for i in range(4)]
                out.append({"GuideNumber": n, "GuideName": name, "Guide": progs})
            self.send(200, json.dumps(out))
        elif u.path == "/lineup.m3u":
            lines = ["#EXTM3U"]
            for n, name in CHANNELS:
                lines.append(f'#EXTINF:-1 channel-id="{n}" channel-number="{n}" tvg-name="{name}",{n} {name}')
                lines.append(f"{REAL}/v{n}")
            self.send(200, "\n".join(lines) + "\n", "audio/x-mpegurl")
        else:
            self.send(404, "{}")

    def do_POST(self):
        u = urlparse(self.path)
        q = parse_qs(u.query)
        if u.path == "/lineup.post":
            if q.get("scan") == ["start"]:
                scan["start"] = time.time(); scan["abort"] = False
                print("scan start, source =", q.get("source"))
            elif q.get("scan") == ["abort"]:
                scan["abort"] = True
                print("scan abort")
            self.send(200, "")
        else:
            self.send(404, "{}")


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8090
    print(f"Faux tuner sur http://0.0.0.0:{port}/")
    ThreadingHTTPServer(("0.0.0.0", port), H).serve_forever()
