#!/bin/bash
# Drives a run started by tools/run-live.sh: sends keys, then saves a frame.
#
#   tools/live.sh <name> [key[:hold] ...]   e.g. Return Left Return:0.3
#
# Each key is followed by a short pause; the frame afterwards is written to
# run/<name>/now.png (and the log's last lines are printed).
set -u
here=$(cd "$(dirname "$0")/.." && pwd)
name=$1
shift
out="$here/run/$name"
kill -0 "$(cat "$out/pid")" 2>/dev/null || { echo "not running"; tail -20 "$out/log.txt"; exit 1; }
for k in "$@"; do
  if [[ $k == wait:* ]]; then
    sleep "${k#wait:}"
    continue
  fi
  key=${k%%:*}
  hold=0.08
  [[ $k == *:* ]] && hold=${k#*:}
  echo "key $key $hold" >> "$out/input.txt"
  sleep 0.6
done
rm -f "$out/now.bmp"
echo "shot $out/now.bmp" >> "$out/input.txt"
for i in $(seq 1 50); do [ -s "$out/now.bmp" ] && break; sleep 0.1; done
sips -s format png "$out/now.bmp" --out "$out/now.png" > /dev/null 2>&1 && echo "frame: $out/now.png"
grep -v -E "^\[guest\] (Locking|CAMERA)" "$out/log.txt" | tail -4
