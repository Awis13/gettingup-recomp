#!/bin/zsh
# ios_bundle.sh [device id]
# Turns the iOS build (cmake --preset ios), or with PLATFORM=tvos the Apple TV
# one (cmake --preset tvos), into a signed Getting Up.app and, given a device
# id, installs it:
#   - the runtime's dylibs and SDL3 into Frameworks/;
#   - the game's files (GAME_DIR, the Steam install) into game/;
#   - the icon through an asset catalog (tools/icon/make_icons.py makes them
#     in ICONS, default out/icons: icon1024.png for the iPad; for Apple TV
#     Assets-tvos.xcassets, a layered icon and top shelf images);
#   - the provisioning profile (PROFILE) and entitlements, then the signature.
# Machine settings come from the environment (or tools/local.env):
#   GU_GAME_DIR    the game (the folder with _Bin and engine)
#   GU_APPLE_BUILD the root of the Apple builds (default out/apple): the
#                  presets' build-ios / build-tvos, the dependency prefixes
#                  prefix-ios / prefix-tvos, profiles/gettingup.mobileprovision
#                  and gettingup-tvos.mobileprovision, and the apps made here
#   BUILD, PREFIX, OUT, PROFILE  any one of those, set apart
# The app's bundle identifier is the profile's (its App ID without the team).
#   SIGN_ID  the signing identity's SHA-1 (default: one in the profile)
#   ICONS    the icons (default out/icons)
set -e
here=${0:A:h:h}
device=$1
[[ -f $here/tools/local.env ]] && . $here/tools/local.env
w=${GU_APPLE_BUILD:-$here/out/apple}
if [[ ${PLATFORM:-ios} == tvos ]]; then
  : ${BUILD:=$w/build-tvos} ${PREFIX:=$w/prefix-tvos} ${OUT:=$w/out-tvos}
  : ${PROFILE:=$w/profiles/gettingup-tvos.mobileprovision}
  entitlements=$here/port/ios/GettingUp-tvOS.entitlements
else
  : ${BUILD:=$w/build-ios} ${PREFIX:=$w/prefix-ios} ${OUT:=$w/out}
  : ${PROFILE:=$w/profiles/gettingup.mobileprovision}
  entitlements=$here/port/ios/GettingUp.entitlements
fi
: ${ICONS:=$here/out/icons}
: ${GAME_DIR:=${GU_GAME_DIR:?set GU_GAME_DIR to the folder with the game (_Bin, engine)}}
# Default identity: a valid one whose certificate is in the profile.
if [[ -z $SIGN_ID ]]; then
  certs=$(security cms -D -i $PROFILE | python3 -I -c 'import plistlib,sys,hashlib
for c in plistlib.loads(sys.stdin.buffer.read())["DeveloperCertificates"]: print(hashlib.sha1(c).hexdigest().upper())')
  for id in $(security find-identity -v -p codesigning | awk '/Apple Development/ {print $2}'); do
    if print -r -- $certs | grep -qx $id; then SIGN_ID=$id; break; fi
  done
fi
[[ -n $SIGN_ID ]] || { echo "no Apple Development signing identity; set SIGN_ID" >&2; exit 1; }
[[ -d $BUILD/gettingup.app ]] || { echo "build first: $BUILD/gettingup.app" >&2; exit 1; }

app=$OUT/GettingUp.app
mkdir -p $OUT
rsync -a --delete --exclude Frameworks --exclude game $BUILD/gettingup.app/ $app/

# Runtime libraries: the build's dylibs (install names @rpath/...) and SDL.
mkdir -p $app/Frameworks
rsync -a --delete $BUILD/x86recomp/librecomp_*.dylib $PREFIX/lib/libSDL3.0.dylib $app/Frameworks/

# The game: _Bin and engine as installed (read-only in the bundle; what the
# game writes goes into the container).
mkdir -p $app/game
rsync -a --delete --exclude '*.asi' --exclude 'd3d8.dll' --exclude 'd3d8.ini' \
      --exclude 'videomode.ini' "$GAME_DIR/_Bin" "$GAME_DIR/engine" $app/game/

# Icon: an asset catalog compiled by actool, its keys merged in (none
# without the icons: tools/icon/make_icons.py).
icons=$OUT/icon
mkdir -p $icons
rm -f $icons/partial.plist
if [[ ! -f $ICONS/icon1024.png ]]; then
  echo "no icons in $ICONS (tools/icon/make_icons.py): the app has none"
elif [[ ${PLATFORM:-ios} == tvos ]]; then
  # actool wants a tvOS simulator runtime, which the build's Xcode may lack
  # (ACTOOL_DEVELOPER_DIR: an Xcode that has one).
  DEVELOPER_DIR=${ACTOOL_DEVELOPER_DIR:-${DEVELOPER_DIR:-}} \
  xcrun actool $ICONS/Assets-tvos.xcassets --compile $app --platform appletvos \
      --minimum-deployment-target 17.0 --app-icon "App Icon & Top Shelf Image" \
      --target-device tv --output-partial-info-plist $icons/partial.plist \
      --errors --warnings > $icons/actool.log
else
mkdir -p $icons/Assets.xcassets/AppIcon.appiconset
cp $ICONS/icon1024.png $icons/Assets.xcassets/AppIcon.appiconset/icon1024.png
cat > $icons/Assets.xcassets/Contents.json <<'JSON'
{"info":{"author":"xcode","version":1}}
JSON
cat > $icons/Assets.xcassets/AppIcon.appiconset/Contents.json <<'JSON'
{"images":[{"filename":"icon1024.png","idiom":"universal","platform":"ios","size":"1024x1024"}],
 "info":{"author":"xcode","version":1}}
JSON
xcrun actool $icons/Assets.xcassets --compile $app --platform iphoneos \
    --minimum-deployment-target 17.0 --app-icon AppIcon --target-device iphone \
    --target-device ipad --output-partial-info-plist $icons/partial.plist \
    --errors --warnings > $icons/actool.log
fi
[[ -f $icons/partial.plist ]] && /usr/libexec/PlistBuddy -c "Merge $icons/partial.plist" $app/Info.plist > /dev/null

# Signature: the profile's app and team identifiers into the entitlements
# (nothing team-specific lives in the repository), the dylibs first.
cp $PROFILE $app/embedded.mobileprovision
ent=$OUT/entitlements.plist
cp $entitlements $ent
prof_ent=$OUT/profile-entitlements.plist
security cms -D -i $PROFILE | plutil -extract Entitlements xml1 -o $prof_ent -
# The bundle identifier: the profile's App ID without its team prefix.
appid=$(/usr/libexec/PlistBuddy -c "Print :application-identifier" $prof_ent)
/usr/libexec/PlistBuddy -c "Set :CFBundleIdentifier ${appid#*.}" $app/Info.plist
for k in application-identifier com.apple.developer.team-identifier; do
  v=$(/usr/libexec/PlistBuddy -c "Print :$k" $prof_ent)
  /usr/libexec/PlistBuddy -c "Delete :$k" $ent 2>/dev/null || true
  /usr/libexec/PlistBuddy -c "Add :$k string $v" $ent
done
for d in $app/Frameworks/*.dylib; do
  codesign --force --sign $SIGN_ID --timestamp=none $d
done
codesign --force --sign $SIGN_ID --entitlements $ent --timestamp=none \
         --generate-entitlement-der $app
codesign --verify --strict $app
echo "signed: $app ($(du -sh $app | cut -f1))"
if [[ -n $device ]]; then
  xcrun devicectl device install app --device $device $app
fi
