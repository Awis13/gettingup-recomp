#!/bin/bash
# Wraps the Mac build into "out/Getting Up.app", to play from the Finder:
# double-click, fullscreen at the desktop's resolution, sound on, a pad if
# one is connected. The app holds the game binary and this project's
# runtime libraries (Contents/Frameworks); SDL, FFmpeg and the shader
# compilers come from Homebrew, the game files from the Steam install
# (GU_GAME_DIR when the app starts, else the one given when it was made).
# Saves and settings stay where the
# command-line build keeps them (~/Library/Application Support/gu); the
# log goes to ~/Library/Logs/Getting Up.log.
#
#   tools/make_mac_app.sh      (after building out/build/release)
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
[ -f "$root/tools/local.env" ] && . "$root/tools/local.env"
: "${GU_GAME_DIR:?set GU_GAME_DIR to the folder with the game (_Bin, engine)}"
build="$root/out/build/release"
app="$root/out/Getting Up.app"
stage="$root/out/.Getting Up.app.new"
[ -x "$build/gettingup" ] || { echo "build first: $build/gettingup"; exit 1; }
[ -f "$root/out/icons/AppIcon.icns" ] || echo "no out/icons/AppIcon.icns (tools/icon/make_icons.py): the app has no icon"

# Built next to the old bundle, then swapped in.
mkdir -p "$stage/Contents/MacOS" "$stage/Contents/Frameworks" "$stage/Contents/Resources"
cp "$build/gettingup" "$stage/Contents/MacOS/gettingup"
cp "$build"/x86recomp/librecomp_*.dylib "$stage/Contents/Frameworks/"
[ -f "$root/out/icons/AppIcon.icns" ] && cp "$root/out/icons/AppIcon.icns" "$stage/Contents/Resources/AppIcon.icns"

# The build's rpath (its own tree) becomes the bundle's Frameworks.
fix_rpath() {
  local f=$1 new=$2
  otool -l "$f" | awk '/LC_RPATH/{r=1} r&&/ path /{print $2; r=0}' | while read -r p; do
    case $p in /opt/homebrew/*) ;; *) install_name_tool -delete_rpath "$p" "$f" ;; esac
  done
  install_name_tool -add_rpath "$new" "$f"
}
fix_rpath "$stage/Contents/MacOS/gettingup" "@executable_path/../Frameworks"
for d in "$stage"/Contents/Frameworks/*.dylib; do fix_rpath "$d" "@loader_path"; done

# The launcher; the game folder given now is its default (Resources/game-dir).
printf '%s\n' "$GU_GAME_DIR" > "$stage/Contents/Resources/game-dir"
cat > "$stage/Contents/MacOS/Getting Up" <<'LAUNCH'
#!/bin/sh
here="$(cd "$(dirname "$0")" && pwd)"
game="${GU_GAME_DIR:-$(cat "$here/../Resources/game-dir")}"
if [ ! -f "$game/_Bin/GettingUp.exe" ]; then
  osascript -e "display alert \"Getting Up\" message \"The game files were not found in:
$game

Connect the disk with the Steam install, or set GU_GAME_DIR.\"" >/dev/null 2>&1
  exit 1
fi
mkdir -p "$HOME/Library/Logs"
exec "$here/gettingup" --game-dir "$game" > "$HOME/Library/Logs/Getting Up.log" 2>&1
LAUNCH
chmod 755 "$stage/Contents/MacOS/Getting Up"

cat > "$stage/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleDevelopmentRegion</key><string>en</string>
	<key>CFBundleDisplayName</key><string>Getting Up</string>
	<key>CFBundleExecutable</key><string>Getting Up</string>
	<key>CFBundleIconFile</key><string>AppIcon</string>
	<key>CFBundleIdentifier</key><string>io.github.awis13.gettingup-recomp</string>
	<key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
	<key>CFBundleName</key><string>Getting Up</string>
	<key>CFBundlePackageType</key><string>APPL</string>
	<key>CFBundleShortVersionString</key><string>0.1</string>
	<key>CFBundleVersion</key><string>1</string>
	<key>GCSupportsControllerUserInteraction</key><true/>
	<key>LSApplicationCategoryType</key><string>public.app-category.action-games</string>
	<key>LSMinimumSystemVersion</key><string>14.0</string>
	<key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
PLIST

codesign --force --deep --sign - "$stage" >/dev/null
# The new bundle replaces the old one (built aside first, so a failed
# build leaves the old one in place).
rm -rf "${app:?}"
mv "$stage" "$app"
echo "$app"
