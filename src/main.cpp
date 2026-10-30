// gettingup: Marc Ecko's Getting Up (PC, Steam), natively.
//
//   gettingup [--game-dir DIR] [-- GAME ARGS...]
//
// DIR is the game install: the folder with _Bin/ and engine/. Defaults to
// $GU_GAME_DIR. Arguments after `--` go on the game's own command line.
// Set GU_TRACE=1 to log every HLE call.

#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "runtime/log.h"
#include "runtime/runtime.h"

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if TARGET_OS_IOS || TARGET_OS_TV
// iPad / Apple TV: SDL owns UIKit's startup and calls main (SDL_main) on the
// main thread. The game files are in the app bundle (game/), what the game
// writes goes below the container's Library, the log to <data>/log.txt:
// Documents on the iPad; Library/Caches on Apple TV, which has no Documents
// (apple_tv.cpp keeps the saves safe from Caches being emptied).
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#define GU_ON_IOS 1
#endif
#if TARGET_OS_TV
void AppleTvSaves(const std::string& data_dir);  // apple_tv.cpp
#endif

std::string PadGlyphRedirect(const std::string& guest_path);  // pad_glyphs.cpp

int main(int argc, char** argv) {
  std::string game_dir, game_args;
#if GU_ON_IOS
  // (GU_LOG_CONSOLE=1: stderr stays the console, for devicectl --console.)
  const char* console = std::getenv("GU_LOG_CONSOLE");
  const char* home = std::getenv("HOME");
#if TARGET_OS_TV
  const std::string data = std::string(home ? home : "") + "/Library/Caches";
  // The game's data directory (the C: drive, the registry) there too.
  setenv("XDG_DATA_HOME", data.c_str(), 1);
  // The Siri Remote is not a controller for this game (it would take the
  // first pad's slot); a real gamepad plays it.
  SDL_SetHint(SDL_HINT_TV_REMOTE_AS_JOYSTICK, "0");
#else
  const std::string data = std::string(home ? home : "") + "/Documents";
#endif
  if (home && !(console && *console == '1')) {
    const std::string log = data + "/log.txt";
    std::freopen(log.c_str(), "w", stderr);
    std::setvbuf(stderr, nullptr, _IOLBF, 0);
  }
  if (const char* base = SDL_GetBasePath()) game_dir = std::string(base) + "game";
  // Relative paths of the test switches (GU_INPUT_LIVE, its shots,
  // GU_SHOT_DIR) are in the data directory, which devicectl copies to and
  // from.
  if (home) chdir(data.c_str());
#if TARGET_OS_TV
  AppleTvSaves(data + "/gu");
#endif
#endif
  if (const char* e = std::getenv("GU_GAME_DIR")) game_dir = e;
  for (int i = 1; i < argc; i++) {
    if (!std::strcmp(argv[i], "--game-dir") && i + 1 < argc) {
      game_dir = argv[++i];
    } else if (!std::strcmp(argv[i], "--")) {
      for (i++; i < argc; i++) {
        if (!game_args.empty()) game_args += ' ';
        // Quoted, as on a Windows command line, so an argument with spaces
        // stays one.
        bool quote = std::strpbrk(argv[i], " \t");
        if (quote) game_args += '"';
        game_args += argv[i];
        if (quote) game_args += '"';
      }
    } else {
      std::fprintf(stderr, "usage: %s [--game-dir DIR] [-- GAME ARGS...]\n", argv[0]);
      return 2;
    }
  }
  if (game_dir.empty()) {
    std::fprintf(stderr, "gettingup: pass --game-dir or set GU_GAME_DIR\n");
    return 2;
  }
  while (game_dir.size() > 1 && game_dir.back() == '/') game_dir.pop_back();
  if (const char* t = std::getenv("GU_TRACE")) recomp::g_trace = *t && *t != '0';

  std::string exe = game_dir + "/_Bin/GettingUp.exe";
  struct stat st;
  if (stat(exe.c_str(), &st) != 0) {
    std::fprintf(stderr, "gettingup: %s not found\n", exe.c_str());
    return 2;
  }

  recomp::GameConfig game;
  game.id = "gu";
  game.guest_dir = "C:\\Games\\Getting Up";
  game.exe_name = "_Bin\\GettingUp.exe";
  // Steam starts the game in _Bin; its config (default_pc.cfg) finds the
  // data as ..\engine from there.
  game.current_dir = game.guest_dir + "\\_Bin";
  // The engine is split into DLLs that import each other statically, on
  // the DLL CRT: all of them are lifted. g_Rhapsody.sgl (the game module,
  // which the EXE finds as _Bin\g_*.sgl) and GScript_Rhapsody.dll share
  // GCore.dll's preferred base 0x10000000, so they are moved above it.
  game.lifted_dlls = {
      {"_Bin\\msvcr71.dll"},
      {"_Bin\\msvcp71.dll"},
      {"_Bin\\GCore.dll"},
      {"_Bin\\g_Rhapsody.sgl", 0x10600000},
      {"_Bin\\GScript_Rhapsody.dll", 0x10A00000},
  };
  // SMAA on the presented frame (GU_AA=0 turns it off). The resolution is
  // the desktop's unless GU_WIDTH / GU_HEIGHT say otherwise (widescreen.cpp).
  game.post_aa = true;
  // 16x anisotropic filtering (GU_ANISO=0 for the game's own).
  game.anisotropy = 16;
  // Back in menus also on Circle / B (the game has Triangle, as on the
  // PS2): what players expect of a modern pad. Pads 0 and 1 (input streams
  // 0 and 1 read the first pad, stream 2 the second).
  game.file_appends = {{"engine\\binds_pc.cfg",
                        "# Added by the port: menu Back on Circle / B too.\n"
                        "addbind 0 COMMAND_UI_BACK CONTROLLER 0 BUTTON_1\n"
                        "addbind 1 COMMAND_UI_BACK CONTROLLER 0 BUTTON_1\n"
                        "addbind 2 COMMAND_UI_BACK CONTROLLER 1 BUTTON_1\n"},
                       // The engine's settings, from the install's copy at every
                       // start: the copy the game writes back holds none (its
                       // writer finds no settings to save), so from the second
                       // start on it would run on the engine's built-in values.
                       {"engine\\vars_pc.cfg", ""}};
  // Menu prompts show the connected pad's buttons (pad_glyphs.cpp).
  game.read_redirect = &PadGlyphRedirect;
  // GU_VARS="name=value;...": more engine settings, after those.
  if (const char* v = std::getenv("GU_VARS")) {
    std::string extra = v;
    for (char& ch : extra)
      if (ch == ';') ch = '\n';
    game.file_appends.back().text += extra + "\n";
  }
  return recomp::RunProcess(game, exe, game_dir, game_args);
}
