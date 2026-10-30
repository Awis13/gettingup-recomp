#!/bin/bash
# Starts the game for interactive driving from outside: a visible window
# that does not take focus, no sound, user data in run/data. Commands go to
# run/<name>/input.txt while it runs (see tools/live.sh):
#   key <SDL key name> [hold seconds]
#   shot <path.bmp>
# Set GU_HIDDEN=1 to keep the window off screen.
#
#   tools/run-live.sh <name>     (returns at once; log in run/<name>/log.txt)
set -u
here=$(cd "$(dirname "$0")/.." && pwd)
name=$1
# Machine settings (GU_GAME_DIR, device ids, build roots): tools/local.env,
# uncommitted, if it is there.
[ -f "$here/tools/local.env" ] && . "$here/tools/local.env"
game=${GU_GAME_DIR:?set GU_GAME_DIR to the folder with the game (_Bin, engine)}
out="$here/run/$name"
mkdir -p "$out" "$here/run/data"
: > "$out/input.txt"
export GU_BACKGROUND=1 SDL_AUDIODRIVER=dummy GU_INPUT_LIVE="$out/input.txt"
export XDG_DATA_HOME="$here/run/data"
nohup "$here/out/build/release/gettingup" --game-dir "$game" > "$out/log.txt" 2>&1 &
echo $! > "$out/pid"
echo "pid $(cat "$out/pid")"
