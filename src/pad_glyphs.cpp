// Button prompts for the pad in use. The PC release draws its menu prompts
// with keyboard keys (Interfc\front_end\controls\s_c_pc_*.st: Enter, Esc,
// 1, Page Up/Down, the arrow keys); the console releases' images of the
// same prompts ship next to them (s_c_ps2_*, s_c_xbox_*). While a pad is
// connected, reading a keyboard image reads the pad's instead: PlayStation
// symbols for a DualSense or DualShock, Xbox letters for the others. The
// buttons are the ones the bindings give each prompt (binds_pc.cfg and
// main.cpp's additions): Select Cross / A, Back Circle / B (Triangle / Y
// too), "1" Square / X, Page Up / Down L1 / R1 (LB / RB).
// GU_GLYPHS=ps|xbox|keyboard picks a set whatever is connected.

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>

#include "runtime/hle/xinput.h"
#include "runtime/runtime.h"

namespace {

struct Swap {
  const char* pc;    // keyboard image, after "s_c_pc_"
  const char* ps;    // PlayStation image, after "s_c_"
  const char* xbox;  // Xbox image, after "s_c_"
};
constexpr Swap kSwaps[] = {
    {"enter", "ps2_xbutton01", "xbox_abutton01"},
    {"esc", "ps2_circlebutton01", "xbox_bbutton01"},
    {"1", "ps2_squarebutton01", "xbox_xbutton01"},
    {"pageup", "ps2_L1button", "xbox_Lbutton"},
    {"pagedown", "ps2_R1button", "xbox_Rbutton"},
    {"dpad", "dpad", "dpad"},
    {"dpad_leftright", "dpad_leftright", "dpad_leftright"},
    {"dpad_updown", "dpad_updown", "dpad_updown"},
};

enum class Set { Keyboard, Ps, Xbox };

Set CurrentSet() {
  if (const char* e = std::getenv("GU_GLYPHS")) {
    if (!std::strcmp(e, "ps")) return Set::Ps;
    if (!std::strcmp(e, "xbox")) return Set::Xbox;
    if (!std::strcmp(e, "keyboard")) return Set::Keyboard;
  }
  const auto pads = recomp::hle::XInputPads();
  if (pads.empty()) return Set::Keyboard;
  return pads.front().playstation ? Set::Ps : Set::Xbox;
}

}  // namespace

// GameConfig::read_redirect.
std::string PadGlyphRedirect(const std::string& guest_path) {
  static const std::string kDir = "\\interfc\\front_end\\controls\\s_c_pc_";
  std::string lower = guest_path;
  for (char& ch : lower) ch = char(std::tolower(static_cast<unsigned char>(ch)));
  const size_t at = lower.find(kDir);
  if (at == std::string::npos || lower.size() < 3 || lower.compare(lower.size() - 3, 3, ".st"))
    return "";
  const std::string key = lower.substr(at + kDir.size(), lower.size() - 3 - at - kDir.size());
  for (const Swap& s : kSwaps) {
    if (key != s.pc) continue;
    const Set set = CurrentSet();
    if (set == Set::Keyboard) return "";
    return guest_path.substr(0, at) + "\\Interfc\\front_end\\controls\\s_c_" +
           (set == Set::Ps ? s.ps : s.xbox) + ".st";
  }
  return "";
}
