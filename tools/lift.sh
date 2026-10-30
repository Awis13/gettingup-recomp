#!/bin/bash
# lift.sh: the game's x86 code (the EXE and the DLLs it loads) lifted to C++
# in generated/, from your own install (GU_GAME_DIR, the folder with _Bin)
# with the symbols in symbols/ and the manifests in manifest/. Run it after
# the first build (which makes the lifter) and again after changing a
# manifest; then build again. Takes a minute or two.
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
[ -f "$here/tools/local.env" ] && . "$here/tools/local.env"
: "${GU_GAME_DIR:?set GU_GAME_DIR to the folder with the game (_Bin, engine)}"
bin="$GU_GAME_DIR/_Bin"
lift="${LIFTER:-$here/out/build/release/x86recomp/tools/lifter/recomp_lift}"
[ -x "$lift" ] || { echo "build first (cmake --preset release && cmake --build --preset release): no $lift"; exit 1; }
cd "$here"
# GCore, msvcr71 and msvcp71 load at their preferred bases; g_Rhapsody.sgl
# and GScript_Rhapsody.dll where the game's loader puts them.
"$lift" --exe "$bin/GettingUp.exe" --symbols symbols/GettingUp --manifest manifest/GettingUp.manifest \
  --module "$bin/msvcr71.dll" --symbols symbols/msvcr71 --manifest manifest/msvcr71.manifest \
  --module "$bin/msvcp71.dll" --symbols symbols/msvcp71 --manifest manifest/msvcp71.manifest \
  --module "$bin/GCore.dll" --symbols symbols/GCore --manifest manifest/GCore.manifest \
  --module "$bin/g_Rhapsody.sgl" --symbols symbols/g_Rhapsody --manifest manifest/g_Rhapsody.manifest --base 10600000 \
  --module "$bin/GScript_Rhapsody.dll" --symbols symbols/GScript_Rhapsody --manifest manifest/GScript_Rhapsody.manifest --base 10A00000 \
  --out generated
