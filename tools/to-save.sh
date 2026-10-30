#!/bin/bash
# Starts the game from a COPY of the player's save and drives it straight
# into a level: intros skipped with Escape, Load Game, the first slot,
# the game info screen, then on the subway map the mission's sub-level
# (SUB Right presses, default 6: the last one unlocked, where the
# cursor stops), Black Book:
# NO, until the level loads. No New Game, no intro mission. Then it watches
# the level for `play` seconds (shots every 5 s) and reports a crash.
#
#   tools/to-save.sh <name> [play seconds]
#
# SAVE_DIR: where the save files are copied from (default: the player's
# own, which is only read). run/<name>/: log.txt, shot_*.png, data/.
set -u
here=$(cd "$(dirname "$0")/.." && pwd)
name=$1 play=${2:-30}
# Machine settings (GU_GAME_DIR, device ids, build roots): tools/local.env,
# uncommitted, if it is there.
[ -f "$here/tools/local.env" ] && . "$here/tools/local.env"
game=${GU_GAME_DIR:?set GU_GAME_DIR to the folder with the game (_Bin, engine)}
saves=${SAVE_DIR:-"$HOME/Library/Application Support/gu/drive_c/Documents and Settings/All Users/Documents/getting up - contents under pressure"}
out="$here/run/$name"
if [ -e "$out" ]; then echo "run/$name exists: pick another name"; exit 2; fi
slot="$out/data/gu/drive_c/Documents and Settings/All Users/Documents/getting up - contents under pressure"
mkdir -p "$slot"
cp -p "$saves"/* "$slot"/ || { echo "no save in $saves"; exit 2; }
: > "$out/input.txt"
log="$out/log.txt"
export GU_BACKGROUND=1 SDL_AUDIODRIVER=dummy GU_INPUT_LIVE="$out/input.txt"
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
count() { grep -c -E "$1" "$log"; }
key() { echo "key $1 ${2:-0.08}" >> "$out/input.txt"; sleep 0.5; }
shotn=0
shot() {
  local f="$out/shot_$(printf %03d $shotn)_$1"
  shotn=$((shotn + 1))
  echo "shot $f.bmp" >> "$out/input.txt"
  for ((i = 0; i < 30; i++)); do [ -s "$f.bmp" ] && break; sleep 0.1; done
  sips -s format png "$f.bmp" --out "$f.png" > /dev/null 2>&1
}
# Presses `k` every `every` seconds until the log matches `pat` once more.
press_until() {
  local k=$1 pat=$2 every=${3:-2} limit=${4:-60} n0
  n0=$(count "$pat")
  for ((t = 0; t < limit; t += every)); do
    alive || died "$pat"
    key "$k"
    for ((i = 0; i < every * 5; i++)); do
      [ "$(count "$pat")" -gt "$n0" ] && return 0
      sleep 0.2
    done
  done
  echo "TIMEOUT pressing $k for: $pat"; shot timeout; exit 2
}
step() { echo "$(date +%T) $1"; }

step "intros: Escape until the shell"
until grep -q "Beginning Shell Session" "$log"; do
  alive || died "intros"
  key Escape
  sleep 0.2
done
step "title: Enter until the menu"
press_until Return "01_start\.bik" 2 90
sleep 1.5
step "Load Game"
key Down
press_until Return "04_lob\.bik"
sleep 3
step "slot 1"
key Return
sleep 3
step "game info"
press_until Return "05_mem\.bik"
sleep 2
step "subway map"
press_until Return "07_flp\.bik"
sleep 3
step "mission 1, sub-level ${SUB:-6}"
key Return
sleep 2
for ((i = 0; i < ${SUB:-6}; i++)); do key Right; sleep 1; done
shot map
loads=$(count "Game load is complete")
key Return
sleep 2
step "Black Book: NO"
key Return
for ((i = 0; i < 180 * 5; i++)); do
  alive || died "level load"
  [ "$(count "Game load is complete")" -gt "$loads" ] && break
  sleep 0.2
done
grep "Setting map" "$log" | tail -1 | sed 's/.*Setting map to //'
sleep 3
shot level
step "level: watching ${play}s"
for ((s = 0; s < play; s++)); do
  alive || died "level at ${s}s"
  for ev in ${PLAY:-}; do
    IFS=: read -r t k h <<< "$ev"
    [ "$t" = "$s" ] && key "$k" "${h:-0.08}"
  done
  sleep 1
  ((s % 5 == 4)) && shot "play_$((s + 1))"
done
alive || died "level end"
step "alive after ${play}s (pid $pid)"
