#!/bin/sh
# Essai de bout en bout, sans écran ni vrai tuner (utilisé par .github/workflows/linux.yml) :
# faux HDHomeRun servant un flux .ts de test → l'appli doit charger la playlist, reprendre
# la dernière chaîne et afficher des images ; capture d'écran dans $OUT/smoke.png.
#
#   tests/smoke.sh <binaire> [dossier de sortie]      (nécessite xvfb-run, ffmpeg, python3)
set -eu
BIN=$(realpath "$1")
OUT=${2:-/tmp/hr-smoke}
HERE=$(cd "$(dirname "$0")" && pwd)
PORT=8090
mkdir -p "$OUT"

# flux de test : mire 720×576 (pixels non carrés, comme la TNT SD) + son, piste audio « fra »
ffmpeg -loglevel error -y -f lavfi -i testsrc2=size=720x576:rate=25 -f lavfi -i sine=frequency=440 -t 30 \
  -c:v mpeg2video -b:v 4M -aspect 16:9 -c:a mp2 -metadata:s:a:0 language=fra -f mpegts "$OUT/test.ts"

python3 "$HERE/fake_tuner.py" --port $PORT --ts "$OUT/test.ts" > "$OUT/fake_tuner.log" 2>&1 &
FAKE=$!
trap 'kill $FAKE 2>/dev/null || true' EXIT
sleep 1

export XDG_CONFIG_HOME="$OUT/config"
mkdir -p "$XDG_CONFIG_HOME/homerun-player"
cat > "$XDG_CONFIG_HOME/homerun-player/settings.ini" <<EOF
[main]
playlist_url=http://127.0.0.1:$PORT/lineup.m3u
last_channel=http://127.0.0.1:$PORT/stream/2.ts
check_updates=false
show_guide=false
lang=fr
EOF

export HR_DEBUG=1 GSK_RENDERER=cairo GDK_BACKEND=x11 NO_AT_BRIDGE=1
xvfb-run -a -s "-screen 0 1280x800x24" sh -c '
  "$0" > "$1/app.log" 2>&1 &
  pid=$!
  sleep 15
  ffmpeg -loglevel error -y -f x11grab -video_size 1280x800 -i "$DISPLAY" -frames:v 1 "$1/smoke.png" || true
  if ! kill -0 $pid 2>/dev/null; then wait $pid; echo "l appli s est arretee (code $?)"; exit 3; fi
  kill $pid
  wait $pid || true
' "$BIN" "$OUT"

cat "$OUT/app.log"
grep -q "playlist : 6 chaînes" "$OUT/app.log" || { echo "ÉCHEC : playlist non chargée"; exit 1; }
grep -q "images affichées" "$OUT/app.log" || { echo "ÉCHEC : aucune image affichée"; exit 1; }
grep -q "/stream/2.ts" "$OUT/fake_tuner.log" || { echo "ÉCHEC : dernière chaîne non reprise"; exit 1; }
echo "Essai réussi."
