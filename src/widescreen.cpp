// Widescreen: any resolution (by default the host desktop's, e.g. an
// ultrawide), with the 2D interface kept 4:3 in the middle. A port of
// ThirteenAG's GettingUp.WidescreenFix (WidescreenFixesPack, MIT), whose
// .asi ships with the Steam build but cannot load here: its inline hooks
// are `hook` lines in the manifests, its function replacements `replace`
// lines, at the addresses its byte patterns find in this build. The 3D
// view follows the resolution by itself (Hor+); the rest is:
//   - the mode: every place the game stores or picks the display size gets
//     the chosen one (GU_WIDTH / GU_HEIGHT, else the desktop's; on Apple TV
//     at most 1080 lines: its GPU holds 60 fps there, not at 4K);
//   - 2D: positions of the screen-space quads go through 0x4B6940, which
//     squeezes them into the centred 4:3 area (full-screen quads and the
//     weapon wheel aside), as do the GUI manager's and Bink's quads;
//   - black pillarboxes left and right of that area outside menus.

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#include <bit>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

#include "runtime/display.h"
#include "runtime/lifted.h"
#include "runtime/runtime.h"
#include "runtime/x87.h"

namespace recomp::gen {

namespace {

// g_Rhapsody.sgl is mapped 0x600000 above its preferred base (main.cpp).
constexpr uint32_t kRhapsody = 0x600000;

// The game's current display size (TGORender), written where it changes.
constexpr uint32_t kResX0 = 0xA8F0C0, kResY0 = 0xA8F0C4, kResX1 = 0xA8F0C8, kResY1 = 0xA8F0CC;
// The 2D vertex array 0x4B6940 fills: 7 floats per vertex, its index.
constexpr uint32_t kQuadIndex = 0xA90A1C, kQuadX = 0xA909E4;
// IDirect3DDevice8* of the renderer.
constexpr uint32_t kDevicePtr = 0xA8F100;

struct Screen {
  int32_t w = 0, h = 0;
  float fw = 0, fh = 0, aspect = 0, w43 = 0, hud_scale = 1, hud_offset = 0;
  bool fix_2d = true, pillarboxes = true;
};

const Screen& S() {
  static const Screen s = [] {
    Screen s;
    if (const char* e = Env("WIDTH")) s.w = std::atoi(e);
    if (const char* e = Env("HEIGHT")) s.h = std::atoi(e);
    if (s.w <= 0 || s.h <= 0) {
      DesktopSize(&s.w, &s.h);
#if TARGET_OS_TV
      if (s.h > 1080) s.w = s.w * 1080 / s.h, s.h = 1080;
#endif
    }
    if (const char* e = Env("WIDE_UI")) s.fix_2d = *e != '0';
    if (const char* e = Env("PILLARBOXES")) s.pillarboxes = *e != '0';
    s.fw = float(s.w);
    s.fh = float(s.h);
    s.aspect = s.fw / s.fh;
    s.w43 = float(int32_t(s.fh * (4.0f / 3.0f)));
    s.hud_offset = (s.fw - s.fh * (4.0f / 3.0f)) / 2.0f;
    s.hud_scale = s.aspect / (4.0f / 3.0f);
    return s;
  }();
  return s;
}

float RdF(uint32_t va) { return std::bit_cast<float>(Rd32(va)); }
void WrF(uint32_t va, float v) { Wr32(va, std::bit_cast<uint32_t>(v)); }

void SetAllRes() {
  Wr32(kResX0, uint32_t(S().w));
  Wr32(kResY0, uint32_t(S().h));
  Wr32(kResX1, uint32_t(S().w));
  Wr32(kResY1, uint32_t(S().h));
}

// Return addresses of the 2D draws 0x4B6940 leaves alone: full-screen
// quads, and the weapon wheel (positioned by the GUI hook instead).
constexpr uint32_t kFullscreenQuads[] = {0x4E51DA, 0x4E516B, 0x4E5249, 0x4E52C5};
constexpr uint32_t kHudQuads[] = {
    0x102C7DB8, 0x102C7E21, 0x102C7E8B, 0x102C7EF5,  // GCore: weapon icons
    0x102C8CEA, 0x102C8D53, 0x102C8DBD, 0x102C8E27,  // their background
    0x100E4F04 + kRhapsody, 0x100E4F70 + kRhapsody,  // g_Rhapsody: the red
    0x100E4FD6 + kRhapsody, 0x100E5042 + kRhapsody,  // weapon bar
};

template <size_t N>
bool In(const uint32_t (&list)[N], uint32_t v) {
  for (uint32_t x : list)
    if (x == v) return true;
  return false;
}

}  // namespace

// ---- the display mode ---------------------------------------------------------------

// The window size passed to SetWindowPos (eax height, ecx width).
void hook_004B8F44(Ctx& c) {
  Wr32(c.edi + 8, uint32_t(S().w));
  Wr32(c.edi + 0xC, uint32_t(S().h));
  c.eax = uint32_t(S().h);
  c.ecx = uint32_t(S().w);
}
// The window and back buffer sizes the renderer stores (TGORender).
void hook_004B8F61(Ctx& c) {  // mov [kResX1], eax
  Wr32(c.edi + 8, uint32_t(S().w));
  Wr32(kResX1, uint32_t(S().w));
}
void hook_004B8F6F(Ctx& c) {  // mov [kResY1], ecx
  Wr32(c.edi + 0xC, uint32_t(S().h));
  Wr32(kResY1, uint32_t(S().h));
}
void hook_004B9055(Ctx& c) {  // mov [kResX1], eax (a mode from the list)
  Wr32(c.ecx + c.edx * 8, uint32_t(S().w));
  Wr32(kResX1, uint32_t(S().w));
}
void hook_004B9067(Ctx& c) {  // mov [kResY1], eax
  Wr32(c.ecx + c.edx * 8 + 4, uint32_t(S().h));
  Wr32(kResY1, uint32_t(S().h));
}
void hook_004B82E8(Ctx&) { Wr32(kResX0, uint32_t(S().w)); }
void hook_004B82EE(Ctx&) { Wr32(kResY0, uint32_t(S().h)); }
void hook_004F2625(Ctx&) { Wr32(kResX0, uint32_t(S().w)); }
void hook_004F2641(Ctx&) { Wr32(kResX0, uint32_t(S().w)); }
void hook_004F2659(Ctx&) { Wr32(kResY0, uint32_t(S().h)); }
// The mode the device is created with (two locals; ecx ends as the height).
void hook_004B7E41(Ctx& c) {
  Wr32(c.esp + 0x2C, uint32_t(S().w));
  Wr32(c.esp + 0x30, uint32_t(S().h));
  c.ecx = uint32_t(S().h);
}
// GCore TGORender::ChangeResolution: its width-to-mode table, whatever the
// width asked for, gives the chosen size.
void hook_1005846E(Ctx& c) {
  SetAllRes();
  c.esi = uint32_t(S().w);
  c.edi = uint32_t(S().h);
}

// TGORender's window mode setup (thiscall, 2 arguments; "Initiating window
// mode"): after the first, the game redoes it when a level loads, and the
// device it resets is still used by the render thread then (a crash in
// 0x4EA200 on a null device). As the WidescreenFix does, it is redone only
// in the menus: a 2D frame drawn (0xA909F9) and the game's in-play flag
// (0x65B45C, the one the pillarboxes check) clear.
void f_004B8EC0_lifted(Ctx& c);
void f_004B8EC0(Ctx& c) {
  static bool done = false;
  const uint32_t in_play = Rd32(0x4DBC19) + 0x564;
  if (done && !(GPtr(0xA909F9)[0] != 0 && Rd32(in_play) == 0)) {
    c.eax = (c.eax & ~0xFFu) | 1;  // "done"
    c.esp += 4 + 8;                 // ret 8
    return;
  }
  f_004B8EC0_lifted(c);
  if ((c.eax & 0xFF) == 1) done = true;
}

// ---- 2D -----------------------------------------------------------------------------------

// 0x4B6940: stdcall (x, y, z): the next 2D vertex's position.
void f_004B6940(Ctx& c) {
  const uint32_t ret = Rd32(c.esp);
  float x = RdF(c.esp + 4), y = RdF(c.esp + 8);
  const float z = RdF(c.esp + 12);
  if (In(kHudQuads, ret)) {
    // Centred around the middle of the screen (the GUI hook scales them).
    x += S().fw / 2.0f;
    y += S().fh / 2.0f;
  } else if (!In(kFullscreenQuads, ret) && S().fix_2d) {
    x = x / S().hud_scale + S().hud_offset;
  }
  const uint32_t v = kQuadX + 0x1C * Rd32(kQuadIndex);
  WrF(v, x);
  WrF(v + 4, y);
  WrF(v + 8, z);
  WrF(v + 12, 1.0f);
  c.esp += 4 + 12;  // ret 0xC
}

// Bink: the movie quad inside the 4:3 area (its x coordinates on the stack).
void hook_0045C3DA(Ctx& c) {
  c.eax = Rd32(c.edx + 4);
  c.ecx = Rd32(c.eax + 0x14);
  if (!S().fix_2d) return;
  WrF(c.esp + 0x14, RdF(c.esp + 0x14) + S().hud_offset);
  WrF(c.esp + 0x44, RdF(c.esp + 0x44) + S().hud_offset);
  WrF(c.esp + 0x2C, RdF(c.esp + 0x2C) - S().hud_offset);
  WrF(c.esp + 0x5C, RdF(c.esp + 0x5C) - S().hud_offset);
}

// GCore IGuiManager::PostRender: the scale of 640x480 GUI coordinates to the
// screen, and the GUI's origin, for the 4:3 area. Replaces the x87 code
// that computed the scale (its operand is left on the stack: popped here).
void hook_102BCBBA(Ctx& c) {
  if (S().fix_2d) {
    WrF(c.esp + 0x34, S().fw * RdF(c.esp + 0x34) * ((1.0f / 640.0f) / S().hud_scale));
    WrF(c.esp + 0x38, S().fh * RdF(c.esp + 0x38) * (1.0f / 480.0f));
    WrF(c.esp + 0x3C, RdF(c.esp + 0x3C) + S().hud_offset);
  } else {
    WrF(c.esp + 0x34, S().fw * RdF(c.esp + 0x34) * (1.0f / 640.0f));
    WrF(c.esp + 0x38, S().fh * RdF(c.esp + 0x38) * (1.0f / 480.0f));
  }
  x87::Pop(c.fpu);
}

// GCore: the screen size the GUI positions by reads as 0 x 0 (things placed
// from it break at high resolutions). cdecl (int* x, int* y).
void hook_102BCB30(Ctx& c) {
  Wr32(Rd32(c.esp), 0);
  Wr32(Rd32(c.esp + 4), 0);
}

// GCore GUI XML: the held item's icon and arc are anchored to the screen
// centre, and the security-camera crop overlay is hidden.
void hook_102CBE67(Ctx& c) {
  c.eax = c.esp + 0x18;
  Wr32(c.esp + 0x18, c.ecx);
  const std::string name = GStr(Rd32(c.esi + 8));
  if (name == "item_arc-color" || name == "item_arc-trail" || name == "item-bak" ||
      name == "item_icon") {
    float x = float(int16_t(Rd16(c.esi + 0x8C))) / 32.0f + 320.0f;
    float y = float(int16_t(Rd16(c.esi + 0x8E))) / 32.0f - 240.0f;
    if (S().w > 2560) x -= 60.0f;  // clear of the right edge
    if (name == "item_arc-color" || name == "item_arc-trail") y -= 1.0f;
    Wr16(c.esi + 0x8C, uint16_t(int16_t(x * 32.0f)));
    Wr16(c.esi + 0x8E, uint16_t(int16_t(y * 32.0f)));
  } else if (name == "crop01" || name == "crop02" || name == "crop03" || name == "crop04") {
    Wr32(c.esi + 0xA0, 0);  // its colour: transparent
  }
}

// g_Rhapsody: the security camera's rotating marker, centred (an fsub of a
// screen offset becomes an fsub of 0, so the operand stays as it is).
void hook_106E7245(Ctx&) {}
void hook_106E725A(Ctx&) {}

// EndScene: black pillarboxes left and right of the 4:3 area while playing
// (not in the menus, which fill the screen).
void hook_004EB285(Ctx& c) {
  c.eax = Rd32(kDevicePtr);
  c.edx = Rd32(c.eax);
  static const uint32_t menu_flag = Rd32(0x4DBC19) + 0x564;
  if (!S().fix_2d || !S().pillarboxes || Rd32(menu_flag) != 0 || S().hud_offset <= 0) return;
  const uint32_t dev = c.eax, clear = Rd32(c.edx + 0x90);  // IDirect3DDevice8::Clear
  const int32_t off = int32_t(S().hud_offset), right = int32_t(S().w43 + S().hud_offset);
  const uint32_t eax = c.eax, ecx = c.ecx, edx = c.edx;
  c.esp -= 32;
  const uint32_t rects = c.esp;
  const int32_t r[8] = {0, 0, off, S().h, right, 0, S().w, S().h};
  std::memcpy(GPtr(rects), r, sizeof r);
  // (Count, pRects, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, black, z 0, stencil 0)
  CallGuest(c, clear, {dev, 2, rects, 3, 0xFF000000u, 0, 0});
  c.esp += 32;
  c.eax = eax;
  c.ecx = ecx;
  c.edx = edx;
}

}  // namespace recomp::gen
