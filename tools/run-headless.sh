#!/bin/bash
# Automated run with no window on screen: the host window stays hidden,
# sound goes to the null driver, frames are saved as BMPs and input comes
# from a TEST_INPUT script ("<seconds> key <SDL key name> [hold]" lines).
#
#   tools/run-headless.sh <name> <input file> [seconds] [shot every N frames]
#
# Output in run/<name>/: log.txt, frame_NNNNN.bmp. User data (saves,
# settings) in run/data, apart from a normal run's.
set -u
here=$(cd "$(dirname "$0")/.." && pwd)
name=$1 input=$2 secs=${3:-120} every=${4:-300}
# Machine settings (GU_GAME_DIR, device ids, build roots): tools/local.env,
# uncommitted, if it is there.
[ -f "$here/tools/local.env" ] && . "$here/tools/local.env"
game=${GU_GAME_DIR:?set GU_GAME_DIR to the folder with the game (_Bin, engine)}
out="$here/run/$name"
mkdir -p "$out" "$here/run/data"
export GU_HIDDEN=1 GU_BACKGROUND=1 SDL_AUDIODRIVER=dummy
export GU_TEST_INPUT="$input" GU_SHOT_DIR="$out" GU_SHOT_EVERY="$every"
export XDG_DATA_HOME="$here/run/data"
"$here/out/build/release/gettingup" --game-dir "$game" > "$out/log.txt" 2>&1 &
pid=$!
echo "$pid" > "$out/pid"
for ((i = 0; i < secs; i++)); do
  kill -0 "$pid" 2>/dev/null || break
  sleep 1
done
if kill -0 "$pid" 2>/dev/null; then
  kill "$pid"
  echo "stopped after ${secs}s (still running)"
else
  wait "$pid"
  echo "exited with $?"
fi
grep -c "" "$out/log.txt" | sed 's/^/log lines: /'
ls "$out" | grep -c bmp | sed 's/^/frames: /'
