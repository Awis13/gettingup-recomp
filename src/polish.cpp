// Small changes that make the port nicer to play than the PC release:
//   - the engine's profiler overlay ("SPIKES" and frame timings, drawn
//     when a frame takes long, as during loading) is not drawn
//     (GU_PROFILER=1 brings it back);
//   - the engine's on-screen debug text (its warnings in red at a level's
//     start: missing bindings, reaction tables, INITFAIL) is not drawn
//     (GU_DEBUGTEXT=1 brings it back; the warnings still go to the log);
//   - cutscenes on screens wider than 4:3 are not letterboxed: the bars
//     were sized for 4:3 and leave a strip on an ultrawide
//     (GU_CUTSCENE_BARS=1 keeps them);
//   - the frame limiter sleeps instead of spinning on the TSC (the main
//     thread was busy all the time, most of it waiting).
// main.cpp adds the gamepad bindings (Back on Circle/B in menus).

#include <bit>
#include <chrono>
#include <cstdint>
#include <thread>

#include "runtime/display.h"
#include "runtime/hle/present.h"
#include "runtime/lifted.h"
#include "runtime/runtime.h"

namespace recomp::gen {

namespace {

bool EnvOn(const char* name) {
  const char* e = Env(name);
  return e && *e && *e != '0';
}

}  // namespace

// GCore: the profiler overlay (thiscall, no arguments).
void f_10049720_lifted(Ctx& c);
void f_10049720(Ctx& c) {
  static const bool on = EnvOn("PROFILER");
  if (on) return f_10049720_lifted(c);
  c.esp += 4;  // ret
}

// GCore TGORender::DebugText and DebugTextVirtual: static cdecl (x, y,
// text, colour, scale); what draws debug text on the screen.
void f_1004E7C0_lifted(Ctx& c);
void f_1004E7C0(Ctx& c) {
  static const bool on = EnvOn("DEBUGTEXT");
  if (on) return f_1004E7C0_lifted(c);
  c.esp += 4;  // ret (cdecl)
}
void f_10054D10_lifted(Ctx& c);
void f_10054D10(Ctx& c) {
  static const bool on = EnvOn("DEBUGTEXT");
  if (on) return f_10054D10_lifted(c);
  c.esp += 4;
}

// The EXE's warning function (0x402400) logs a warning, then hands it to
// the game module's interface (method +0x18), which lists it on the screen
// (the red lines during and after a level's load). That hand-over
// (`mov eax, [esp+4]; mov edx, [ecx]; push eax; call [edx+0x18]`) is
// skipped: the warnings stay in the log only.
void hook_00402445(Ctx& c) {
  static const bool on = EnvOn("DEBUGTEXT");
  c.eax = Rd32(c.esp + 4);
  c.edx = Rd32(c.ecx);
  if (on) CallGuest(c, Rd32(c.edx + 0x18), {c.eax});  // thiscall, ecx: the interface
}

// GCore's settings constructor: `mov [esi + 0xD7], bl`, its "notext"
// ("Disable onscreen debug text") cleared. Set instead: the engine's
// warnings (the list in red during and after a level's load) and other
// debug text are not drawn. The install's vars_pc.cfg has notext=true too,
// but GCore's own copy of the setting never reads it.
void hook_102EF956(Ctx& c) {
  static const bool on = EnvOn("DEBUGTEXT");
  GPtr(c.esi + 0xD7)[0] = on ? 0 : 1;
}

// The same in the EXE's copy of that constructor, which builds the settings
// object the engine actually uses (TGO::UIMessage, the list of warnings on
// the screen, checks its notext).
void hook_0044E5A6(Ctx& c) { hook_102EF956(c); }

// GCore ILetterBox::Activate: `mov eax, [esi + 0x21C]` (its mode); the bar
// sizes at +0x214, +0x218, +0x220, +0x224 become 0 on a wide screen (as the
// WidescreenFix's DisableCutsceneBorders does).
void hook_101463F4(Ctx& c) {
  c.eax = Rd32(c.esi + 0x21C);
  static const bool keep = [] {
    if (EnvOn("CUTSCENE_BARS")) return true;
    int w, h;
    DesktopSize(&w, &h);
    return int64_t(w) * 3 <= int64_t(h) * 4;  // 4:3 or narrower
  }();
  if (keep) return;
  for (uint32_t off : {0x214u, 0x218u, 0x220u, 0x224u}) Wr32(c.esi + off, 0);
}

// GCore's frame limiter (in 0x1002CF90, after a frame) waits until
// 1000/fps ms have passed since the last frame's end by calling its timer
// (TSC / 16 against a calibrated rate) in a loop. Hooked here: the loop's
// `call Interface_GetGameInterface`, which sleeps first until shortly
// before the target, so the loop spins only for the last bit (a sleep can
// overshoot; the margin keeps the frame on time).
void hook_1002D07D(Ctx& c) {
  constexpr double kMarginMs = 1.0;
  c.eax = Rd32(0x104CC054);  // Interface_GetGameInterface
  const float target = std::bit_cast<float>(Rd32(c.esp + 0xC));  // ms
  const float per_ms = std::bit_cast<float>(Rd32(Rd32(c.eax + 0x28) + 0xC84));
  const uint32_t now = uint32_t(Rdtsc(c) >> 4), start = Rd32(c.esi + 0x68);
  const double elapsed = double(now - start) / per_ms;  // wraps as the game's does
  const double left = target - elapsed;
  if (!(left > kMarginMs) || left > 1000.0) return;
  const auto t0 = std::chrono::steady_clock::now();
  std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(left - kMarginMs));
  hle::g_limiter_sleep_ns.fetch_add(
      uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::steady_clock::now() - t0)
                   .count()),
      std::memory_order_relaxed);
}

}  // namespace recomp::gen
