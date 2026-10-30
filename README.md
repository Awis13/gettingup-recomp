# Marc Ecko's Getting Up, recompiled for Mac, iPad, iPhone and Apple TV

A native port of the PC game **Marc Ecko's Getting Up: Contents Under
Pressure** (2006) made by statically recompiling the game's own x86 code with
[x86recomp](https://github.com/JTM-rootstorm/x86recomp). The EXE and the
engine DLLs it loads are translated to C++ and compiled for Apple silicon, so
the game runs as it does on Windows; only the platform layer is new: Direct3D 8
on Metal (SDL_GPU), Miles audio and Bink movies (FFmpeg), DirectInput and
XInput (SDL).

**This repository contains no part of the game.** You build it from the Steam
release you own, on your own Mac.

**Beta.** The Mac build has been played through several levels of the story;
the iPad, iPhone and Apple TV builds run the same code and have been played
from a save into the levels. Expect rough edges.

| | Mac | iPad / iPhone | Apple TV 4K |
|---|---|---|---|
| Frame rate | 60 (the game's own cap) | 60 | 60 |
| Picture | the desktop's resolution, ultrawide included (Hor+, the HUD kept 4:3) | the screen's | 1080p, scaled by the box |
| Anti-aliasing | SMAA, 16x anisotropic filtering | same | same |
| Input | controller, keyboard and mouse | controller, keyboard and mouse | controller |
| Tested on | Apple silicon Macs | iPad Air (M4), iPhone 15 Pro Max | Apple TV 4K (3rd generation) |

Controllers (DualSense, DualShock 4, Xbox and others SDL knows) have rumble,
and the menus show the connected pad's buttons.

## What you need

- The Steam release of the game (`Marc Ecko's Getting Up 2`), installed so its
  folder with `_Bin` and `engine` is on your Mac (for example through a
  Windows Steam client under CrossOver). Tested files: `_Bin/GettingUp.exe`
  SHA-256 `55e9e2cf87b862b0...`, `_Bin/GCore.dll` `dac63a65f2013206...`.
- A Mac with Apple silicon, Xcode, and from Homebrew:
  `brew install cmake ninja sdl3 ffmpeg llvm@22 glslang spirv-cross python`.
- For iPad, iPhone and Apple TV: a paid Apple developer account and
  development provisioning profiles (one for iOS, one for tvOS) for an App ID
  with the *Extended Virtual Addressing* capability, and on iOS also
  *Increased Memory Limit*.

## Build for the Mac

```bash
git clone https://github.com/Awis13/gettingup-recomp && cd gettingup-recomp
git clone --branch gettingup https://github.com/Awis13/x86recomp vendor/x86recomp
git -C vendor/x86recomp checkout "$(cut -d' ' -f2 X86RECOMP)"
export GU_GAME_DIR="/path/to/Marc Ecko's Getting Up 2"

cmake --preset release && cmake --build --preset release   # the lifter and runtime
tools/lift.sh                                              # the game's code to C++ (generated/)
cmake --preset release && cmake --build --preset release   # the game: out/build/release/gettingup
tools/make_mac_app.sh                                      # out/Getting Up.app
```

`X86RECOMP` pins the x86recomp commit this tree is tested with. `symbols/` holds
the addresses and names of the game's functions (exported from Ghidra) and
`manifest/` this port's changes to the game, both read by the lifter.
`GU_GAME_DIR` (and the other machine settings) can go in an uncommitted
`tools/local.env` instead, which every script reads.

To play without the app: `out/build/release/gettingup --game-dir "$GU_GAME_DIR"`.
Saves and settings are kept in `~/Library/Application Support/gu`.

## Build for iPad, iPhone and Apple TV

After the Mac build (the lifted code in `generated/` is shared):

```bash
tools/apple_deps.sh                       # SDL3, FFmpeg, glslang, SPIRV-Cross for iOS and tvOS
cmake --preset iphoneos && cmake --build --preset iphoneos
cmake --preset appletvos && cmake --build --preset appletvos
mkdir -p out/apple/profiles               # your profiles, named:
#   out/apple/profiles/gettingup.mobileprovision        (iOS)
#   out/apple/profiles/gettingup-tvos.mobileprovision   (tvOS)
tools/ios_bundle.sh <device id>                  # iPad / iPhone: bundle, sign, install
PLATFORM=tvos tools/ios_bundle.sh <device id>    # Apple TV
```

The device ids are in `xcrun devicectl list devices`. The app's bundle
identifier is taken from the profile, and it is signed with an identity whose
certificate the profile holds. The game's files go into the app (about 3 GB):
install over a cable, Wi-Fi takes an hour.

- **iPad with a mouse:** iPadOS locks the pointer to a game only when the game
  is full screen. With *Windowed Apps* (iPadOS 26's default) the pointer stays
  free and the camera stops at the screen's edges: choose **Full Screen Apps**
  in Settings › Multitasking & Gestures.
- **iPhone and Apple TV:** there are no touch controls; play with a controller
  (or a keyboard on iPhone). On Apple TV the Siri Remote is not used by the
  game.
- **Apple TV:** an app there has no lasting storage of its own, so the saves
  (a few kilobytes) are also kept in the app's preferences and restored if the
  system clears its cache. The box renders at 1080p: its GPU holds 60 fps
  there, not at 4K.

## Icons (optional)

The app icons are made from the game's own textures, which a run saves when
`GU_DUMP_TEXTURES` names a folder: play into a level with it set, then
`tools/icon/make_icons.py <that folder>` (Python with Pillow) writes
`out/icons/` for the Mac app, the iPad and the Apple TV (its layered icon and
top shelf). Without them the apps are built with no icon. On Apple TV,
`actool` needs a tvOS simulator runtime; if your Xcode has none,
`ACTOOL_DEVELOPER_DIR` can name another Xcode for that step.

## Settings

Environment variables for the game (on iPad and Apple TV as launch
environment):

| Variable | Effect |
|---|---|
| `GU_WIDTH`, `GU_HEIGHT` | the resolution (default: the screen's; 1080 lines at most on Apple TV) |
| `GU_AA=0` | no SMAA |
| `GU_ANISO=n` | anisotropic filtering (default 16) |
| `GU_GLYPHS=ps\|xbox\|keyboard` | the button prompts, whatever is connected |
| `GU_CUTSCENE_BARS=1` | the cutscene letterbox on wide screens |
| `GU_WIDE_UI=0`, `GU_PILLARBOXES=0` | the 2D interface stretched, no side bars |
| `GU_DEBUGTEXT=1`, `GU_PROFILER=1` | the engine's on-screen warnings and profiler |
| `GU_VARS="name=value;..."` | more engine settings (`vars_pc.cfg` syntax) |
| `GU_FPS_REPORT=s` | log the frame rate, frame times and memory every `s` seconds |
| `GU_HITCH=ms`, `GU_SLOW_HLE=ms` | log frames, and system calls, that take longer |

## How it works

`tools/lift.sh` runs x86recomp's lifter over the EXE, `GCore.dll` (the engine),
`g_Rhapsody.sgl` and `GScript_Rhapsody.dll` (the game and its scripts) and the
Visual C++ 7.1 runtime: every function becomes C++, the x87 and SSE maths kept
exact. x86recomp's runtime provides the Windows API the game calls. This
project adds the game-specific parts:

- `src/widescreen.cpp`: any resolution with the interface kept 4:3, a port of
  ThirteenAG's WidescreenFix (its hooks are `hook` lines in the manifests);
- `src/polish.cpp`: no debug text or profiler overlay, no letterbox on wide
  screens, and the frame limiter sleeping instead of spinning on the TSC;
- `src/pad_glyphs.cpp`: PlayStation or Xbox button prompts;
- `src/apple_tv.cpp`: the saves on Apple TV;
- `manifest/`: the hooks, replaced functions and entry points.

## Legal

- No game data, no game code (original or recompiled) and no builds are in
  this repository or its history. The tools read the game from your own
  install, and what they produce stays on your machine; do not redistribute
  the builds.
- *Marc Ecko's Getting Up: Contents Under Pressure* is a trademark of its
  owners; this project is not affiliated with them.
- This project is under the MIT License (`LICENSE`), which also carries the
  notice of ThirteenAG's WidescreenFixesPack (MIT). x86recomp, SDL, FFmpeg,
  glslang and SPIRV-Cross have their own licenses.
