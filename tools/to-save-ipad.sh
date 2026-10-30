#!/bin/zsh
# to-save-ipad.sh <name> [play seconds]
# The to-save.sh drive on the iPad (PLATFORM=tvos: the Apple TV): starts the
# installed app with its log on the console (devicectl --console), and plays
# the same steps through a GU_INPUT_LIVE file in the app's data directory
# (Documents; Library/Caches on Apple TV) (rewritten there by devicectl
# after every line): intros skipped, Load Game, slot 1 (the player's save,
# copied in first), the subway map's last unlocked sub-level (SUB Right presses, default 6), Black Book: NO,
# until the level loads; then shots every 5 s for `play` seconds, pulled
# into run/<name>/. Needs the app installed (tools/ios_bundle.sh).
# APP_ENV: more environment for the app, as JSON members
# ('"GU_FPS_REPORT":"5","GU_TEST_PAD":"1"').
set -u
here=${0:A:h:h}
name=$1 play=${2:-20}
[[ -f $here/tools/local.env ]] && . $here/tools/local.env
w=${GU_APPLE_BUILD:-$here/out/apple}
if [[ ${PLATFORM:-ios} == tvos ]]; then
  : ${DEVICE:=${GU_APPLE_TV:?set DEVICE (or GU_APPLE_TV) to the Apple TV's id}}
  data=Library/Caches built=$w/out-tvos/GettingUp.app
else
  : ${DEVICE:=${GU_IPAD:?set DEVICE (or GU_IPAD) to the device's id}}
  data=Documents built=$w/out/GettingUp.app
fi
[[ -n ${DEVELOPER_DIR:-} ]] && export DEVELOPER_DIR
# The bundle identifier the app was installed with (tools/ios_bundle.sh).
app=$(/usr/libexec/PlistBuddy -c "Print :CFBundleIdentifier" $built/Info.plist)
out=$here/run/$name
[[ -e $out ]] && { echo "run/$name exists: pick another name"; exit 2; }
mkdir -p $out
log=$out/log.txt input=$out/input.txt
: > $input
dc() { xcrun devicectl device copy "$@" --device $DEVICE --domain-type appDataContainer \
         --domain-identifier $app > /dev/null 2>&1; }
push() { dc to --source $input --destination $data/input.txt; }
# The player's save (SAVE_DIR, default the Mac's), copied in while the app
# is not running (a running game rewrites it) and checked byte for byte.
: ${SAVE_DIR:="$HOME/Library/Application Support/gu/drive_c/Documents and Settings/All Users/Documents/getting up - contents under pressure"}
[[ $data == Documents ]] && gu="Library/Application Support/gu" || gu="$data/gu"
slot="$gu/drive_c/Documents and Settings/All Users/Documents/getting up - contents under pressure"
for pid in $(xcrun devicectl device info processes --device $DEVICE 2>/dev/null | awk '/GettingUp.app\/gettingup/ {print $1}'); do
  xcrun devicectl device process terminate --device $DEVICE --pid $pid > /dev/null 2>&1
done
sleep 2
mkdir -p $out/save
for f in "$SAVE_DIR"/*; do
  dc to --source "$f" --destination "$slot/${f:t}"
  dc from --source "$slot/${f:t}" --destination "$out/save/${f:t}"
  cmp -s "$f" "$out/save/${f:t}" || { echo "save ${f:t} did not arrive intact"; exit 2; }
done
push
xcrun devicectl device process launch --device $DEVICE --terminate-existing --console \
  --environment-variables "{\"GU_LOG_CONSOLE\":\"1\",\"GU_INPUT_LIVE\":\"input.txt\"${APP_ENV:+,$APP_ENV}}" \
  $app > $log 2>&1 &
launcher=$!

count() { grep -c -E "$1" $log; }
key() { echo "key $1 ${2:-0.08}" >> $input; push; sleep 0.3; }
shotn=0
shot() {
  local f=shot_$(printf %03d $shotn)_$1
  shotn=$((shotn + 1))
  echo "shot $f.bmp" >> $input; push; sleep 1.5
  dc from --source $data/$f.bmp --destination $out/$f.bmp
  sips -s format png $out/$f.bmp --out $out/$f.png > /dev/null 2>&1
}
press_until() {
  local k=$1 pat=$2 every=${3:-3} limit=${4:-90} n0
  n0=$(count "$pat")
  for ((t = 0; t < limit; t += every)); do
    key $k
    for ((i = 0; i < every * 5; i++)); do
      [[ $(count "$pat") -gt $n0 ]] && return 0
      sleep 0.2
    done
  done
  echo "TIMEOUT pressing $k for: $pat"; shot timeout; exit 2
}
step() { echo "$(date +%T) $1"; }
alive() { kill -0 $launcher 2>/dev/null; }

step "intros: Escape until the shell"
until grep -q "Beginning Shell Session" $log; do
  alive || { echo "app ended"; tail -5 $log; exit 1; }
  key Escape; sleep 0.5
done
step "title"
press_until Return "01_start\.bik"
sleep 1.5
step "Load Game"
key Down
press_until Return "04_lob\.bik"
sleep 3
key Return; sleep 3
step "game info"
press_until Return "05_mem\.bik"
sleep 2
step "subway map"
press_until Return "07_flp\.bik"
sleep 3
# MAP_ONLY=1: stop on the subway map (to see which station is selected).
if [[ -n ${MAP_ONLY:-} ]]; then shot map; step "on the map (app left running)"; exit 0; fi
key Return; sleep 4
for ((i = 0; i < ${SUB:-6}; i++)); do key Right; sleep 1.5; done
shot map
loads=$(count "Game load is complete")
key Return; sleep 2
step "Black Book: NO"
key Return
for ((i = 0; i < 900; i++)); do
  [[ $(count "Game load is complete") -gt $loads ]] && break
  sleep 0.2
done
grep "Setting map" $log | tail -1 | sed 's/.*Setting map to //'
sleep 3; shot level
# SHOT_EVERY: seconds between shots (a 2732x2048 readback is a hitch of its
# own: keep it long for measurements).
every=${SHOT_EVERY:-5}
for ((s = every; s <= play; s += every)); do sleep $every; shot play_$s; done
((play % every)) && sleep $((play % every))
step "done (app left running)"
