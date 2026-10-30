#!/bin/bash
# Starts the game and drives it from the title to the first mission by
# itself: intros skipped with Escape, then New Game on an empty slot (saved),
# Mission Select, the first mission, no Black Book. Each step waits for the
# log line of the screen it is on (the menus' transition movies), not for a
# fixed time. Then it watches the mission for `play` seconds, saving a
# frame every few seconds, and reports a crash or hang.
#
#   tools/to-mission.sh <name> [play seconds]    (window visible; GU_HIDDEN=1 hides it)
#
# run/<name>/: log.txt, shot_NNN.png, data/ (a fresh user data dir).
set -u
here=$(cd "$(dirname "$0")/.." && pwd)
name=$1 play=${2:-60}
# Machine settings (GU_GAME_DIR, device ids, build roots): tools/local.env,
# uncommitted, if it is there.
[ -f "$here/tools/local.env" ] && . "$here/tools/local.env"
game=${GU_GAME_DIR:?set GU_GAME_DIR to the folder with the game (_Bin, engine)}
out="$here/run/$name"
mkdir -p "$out/data"
: > "$out/input.txt"
log="$out/log.txt"
export GU_BACKGROUND=1 SDL_AUDIODRIVER=dummy GU_INPUT_LIVE="$out/input.txt"
# Metal performance HUD (FPS, frame time) in the window, for whoever watches.
export MTL_HUD_ENABLED=${MTL_HUD_ENABLED:-1}
export XDG_DATA_HOME="$out/data"
"$here/out/build/release/gettingup" --game-dir "$game" > "$log" 2>&1 &
pid=$!
echo "$pid" > "$out/pid"

alive() { kill -0 "$pid" 2>/dev/null; }
died() {
  echo "DIED at step: $1"
  grep -n -A3 -E "FATAL|watchdog" "$log" | head -8
  exit 1
}
# Waits until the log matches (grep -E) the n-th time (default first).
wait_log() {
  local pat=$1 limit=${2:-120} n=${3:-1}
  for ((i = 0; i < limit * 5; i++)); do
    alive || died "$pat"
    [ "$(grep -c -E "$pat" "$log")" -ge "$n" ] && return 0
    sleep 0.2
  done
  echo "TIMEOUT waiting for: $pat"
  shot timeout
  exit 2
}
key() { echo "key $1 ${2:-0.08}" >> "$out/input.txt"; sleep 0.5; }
# Presses `k` every `every` seconds until the log matches `pat` (a screen
# change): keys sent while a screen is still fading in are dropped.
press_until() {
  local k=$1 pat=$2 every=${3:-2} limit=${4:-60}
  local n0
  n0=$(grep -c -E "$pat" "$log")
  for ((t = 0; t < limit; t += every)); do
    alive || died "$pat"
    key "$k"
    for ((i = 0; i < every * 5; i++)); do
      [ "$(grep -c -E "$pat" "$log")" -gt "$n0" ] && return 0
      sleep 0.2
    done
  done
  echo "TIMEOUT pressing $k for: $pat"
  shot timeout
  exit 2
}
shotn=0
shot() {
  local f="$out/shot_$(printf %03d $shotn)_$1"
  shotn=$((shotn + 1))
  echo "shot $f.bmp" >> "$out/input.txt"
  for ((i = 0; i < 30; i++)); do [ -s "$f.bmp" ] && break; sleep 0.1; done
  sips -s format png "$f.bmp" --out "$f.png" > /dev/null 2>&1 && rm -f "$f.bmp"
}
step() { echo "$(date +%T) $1"; }

step "boot"
wait_log "legal\.bik"
step "intros: Escape until the shell"
until grep -q "Beginning Shell Session" "$log"; do
  alive || died "intros"
  key Escape
  sleep 0.5
done
step "title: Enter until the menu"
press_until Return "01_start\.bik" 2 90
sleep 1.5
shot menu
step "New Game"
press_until Return "04_lob\.bik"
sleep 2.5
shot slots
step "slot 1"
key Return
sleep 1.5
shot save_prompt
step "save: YES"
key Left
key Return
wait_log "05_mem\.bik" 60
sleep 2
shot game_info
step "Mission Select"
press_until Return "07_flp\.bik"
sleep 2.5
shot missions
step "first mission, then Black Book: NO, until the level loads"
press_until Return "queue Map Change" 2 40
wait_log "Game load is complete" 180 2
shot level_loaded
step "mission: watching ${play}s"
# PLAY="sec:key[:hold] ...": keys at those seconds after the level loaded.
for ((s = 0; s < play; s++)); do
  alive || died "mission at ${s}s"
  for ev in ${PLAY:-}; do
    IFS=: read -r t k h <<< "$ev"
    [ "$t" = "$s" ] && key "$k" "${h:-0.08}"
  done
  sleep 1
  ((s % 5 == 4)) && shot "play_$((s + 1))"
done
alive || died "mission end"
grep -E "not emulated|not supported" "$log" | sort | uniq -c
step "alive after ${play}s of mission (pid $pid)"
