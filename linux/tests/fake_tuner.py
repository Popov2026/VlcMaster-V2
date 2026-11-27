#!/usr/bin/env python3
"""Faux HDHomeRun local pour tester HomeRun Player sans toucher au vrai tuner.

    python3 tests/fake_tuner.py [--port 8090] [--ts fichier.ts] [--real http://192.168.1.90:5004/auto]

Mêmes adresses qu'android/tools/fake_tuner.py : discover.json (avec UpgradeAvailable),
lineup.m3u, lineup_status.json, POST /lineup.post?scan=start|abort (scan simulé ~20 s).
Flux des chaînes :
  --ts F    chaque chaîne sert le fichier F (essais hors réseau, intégration continue) ;
  --real U  chaque chaîne renvoie vers le vrai tuner U/v<n> (lecture seule) ;
  sinon     /stream/<n>.ts répond 503 (« aucun tuner libre »).
Guide : /guide.json renvoie un faux guide (le vrai vient de api.hdhomerun.com).
"""
import argparse
import json
import os
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

CHANNELS = [("1", "TF1"), ("2", "France 2"), ("3", "France 3"), ("5", "France 5"), ("6", "M6"), ("7", "Arte")]
SCAN_SECONDS = 20
scan = {"start": 0.0, "abort": False}
opts = None


def status():
    if scan["start"] and not scan["abort"]:
        el = time.time() - scan["start"]
        if el < SCAN_SECONDS:
            p = int(el * 100 / SCAN_SECONDS)
            return {"ScanInProgress": 1, "Progress": p, "Found": int(p * len(CHANNELS) / 100)}
    return {"ScanInProgress": 0, "ScanPossible": 1, "Source": "Antenna", "SourceList": ["Antenna"]}


def fake_guide():
    now = int(time.time()) // 1800 * 1800
    out = []
    for n, name in CHANNELS:
        progs = [{"StartTime": now + i * 3600, "EndTime": now + (i + 1) * 3600,
                  "Title": f"{name} – émission {i + 1}", "Synopsis": "Programme de test."} for i in range(-1, 24)]
        out.append({"GuideNumber": n, "GuideName": name, "Guide": progs})
    return out


class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def send(self, code, body, ctype="application/json"):
        b = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(b)))
        self.end_headers()
        self.wfile.write(b)

    def do_GET(self):
        u = urlparse(self.path)
        host = self.headers.get("Host", f"127.0.0.1:{opts.port}")
        if u.path == "/discover.json":
            self.send(200, json.dumps({
                "FriendlyName": "Faux HDHomeRun", "ModelNumber": "HDFX-4DT", "FirmwareName": "hdhomerun_fake",
                "FirmwareVersion": "20240101", "UpgradeAvailable": "20251201", "DeviceID": "FAKE0001",
                "DeviceAuth": "fake", "TunerCount": 4, "BaseURL": f"http://{host}",
                "LineupURL": f"http://{host}/lineup.json"}))
        elif u.path == "/lineup_status.json":
            self.send(200, json.dumps(status()))
        elif u.path == "/guide.json":
            self.send(200, json.dumps(fake_guide()))
        elif u.path == "/lineup.m3u":
            lines = ["#EXTM3U"]
            for n, name in CHANNELS:
                lines.append(f'#EXTINF:-1 channel-id="{n}" channel-number="{n}" tvg-name="{name}",{n} {name}')
                lines.append(f"{opts.real}/v{n}" if opts.real else f"http://{host}/stream/{n}.ts")
            self.send(200, "\n".join(lines) + "\n", "audio/x-mpegurl")
        elif u.path.startswith("/stream/"):
            if not opts.ts:
                self.send(503, "no tuner available", "text/plain")
                return
            size = os.path.getsize(opts.ts)
            self.send_response(200)
            self.send_header("Content-Type", "video/mp2t")
            self.send_header("Content-Length", str(size))
            self.end_headers()
            with open(opts.ts, "rb") as f:
                while True:
                    b = f.read(188 * 512)
                    if not b:
                        break
                    try:
                        self.wfile.write(b)
                    except (BrokenPipeError, ConnectionResetError):
                        return
                    time.sleep(0.02)   # à peu près au rythme d'un flux réel
        else:
            self.send(404, "{}")

    def do_POST(self):
        u = urlparse(self.path)
        q = parse_qs(u.query)
        if u.path == "/lineup.post" and q.get("scan") == ["start"]:
            scan["start"] = time.time()
            scan["abort"] = False
            self.send(200, "")
        elif u.path == "/lineup.post" and q.get("scan") == ["abort"]:
            scan["abort"] = True
            self.send(200, "")
        else:
            self.send(400, "{}")

    def log_message(self, fmt, *args):
        print("[fake_tuner]", fmt % args, flush=True)


def main():
    global opts
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8090)
    ap.add_argument("--ts", help="fichier .ts servi pour chaque chaîne")
    ap.add_argument("--real", help="adresse des flux du vrai tuner, ex. http://192.168.1.90:5004/auto")
    opts = ap.parse_args()
    print(f"Faux HDHomeRun : http://127.0.0.1:{opts.port}/lineup.m3u", flush=True)
    ThreadingHTTPServer(("0.0.0.0", opts.port), H).serve_forever()


if __name__ == "__main__":
    main()
