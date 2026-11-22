#include "common/model.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sl {

namespace {

float Clampf(float v, float lo, float hi) { return std::min(std::max(v, lo), hi); }

int HexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

constexpr const char* kShapeKeys[] = {"cross",   "plus",     "x",       "t",       "dot",      "ring", "box",
                                      "diamond", "triangle", "corners", "chevron", "segments", "line"};
static_assert(std::size(kShapeKeys) == static_cast<size_t>(Shape::Count));

// Palette shared by presets and the settings UI.
const Color kWhite{1, 1, 1, 1};
const Color kSun = ColorFromHex("#FFD92E");
const Color kNeon = ColorFromHex("#39FF14");
const Color kCyan = ColorFromHex("#00F0FF");
const Color kRed = ColorFromHex("#FF2A2A");
const Color kYellow = ColorFromHex("#FFFF00");
const Color kGreen = ColorFromHex("#00FF00");
const Color kMagenta = ColorFromHex("#FF2BD6");
const Color kCoral = ColorFromHex("#F0524A");
const Color kOrange = ColorFromHex("#FF7A1A");
const Color kLavender = ColorFromHex("#C3B8F0");
const Color kVolt = ColorFromHex("#2F6BFF");
const Color kAmber = ColorFromHex("#FFB000");

Layer Cross(float length, float thickness, float gap, Color fill, int rays = kRayAll, bool outline = true) {
  Layer l;
  l.shape = Shape::Cross;
  l.length = length;
  l.thickness = thickness;
  l.gap = gap;
  l.rays = rays;
  l.fill = fill;
  l.outline = outline;
  return l;
}

Layer Dot(float size, Color fill, bool round = false, bool outline = true) {
  Layer l;
  l.shape = Shape::Dot;
  l.size = size;
  l.round = round;
  l.fill = fill;
  l.outline = outline;
  return l;
}

Layer Ring(float radius, float thickness, Color fill, bool outline = true) {
  Layer l;
  l.shape = Shape::Ring;
  l.size = radius;
  l.thickness = thickness;
  l.fill = fill;
  l.outline = outline;
  return l;
}

Layer Plus(float arm, float thickness, Color fill) {
  Layer l;
  l.shape = Shape::Plus;
  l.length = arm;
  l.thickness = thickness;
  l.fill = fill;
  return l;
}

Layer WithShape(Layer l, Shape s) {
  l.shape = s;
  return l;
}

Layer HLine(float half, float thickness, float gap, Color fill) {
  Layer l;
  l.shape = Shape::Line;
  l.length = half;
  l.thickness = thickness;
  l.gap = gap;
  l.fill = fill;
  return l;
}

Layer Outlined(Layer l, float thickness) {
  l.outline = thickness > 0;
  l.outlineThickness = thickness;
  return l;
}

// Size-based shapes: Dot (diameter), Ring / Segments (radius), Box (half-size), Diamond / Triangle (radius).
Layer Shaped(Shape s, float size, float thickness, Color fill, bool filled = false, bool outline = true) {
  Layer l;
  l.shape = s;
  l.size = size;
  l.thickness = thickness;
  l.fill = fill;
  l.filled = filled;
  l.outline = outline;
  return l;
}

// Gap / arm shapes: Corners (gap = half-size), Chevron (gap below centre).
Layer Armed(Shape s, float gap, float arm, float thickness, Color fill) {
  Layer l;
  l.shape = s;
  l.gap = gap;
  l.length = arm;
  l.thickness = thickness;
  l.fill = fill;
  return l;
}

Layer SegmentRing(float radius, float thickness, int count, float gapDeg, Color fill) {
  Layer l = Shaped(Shape::Segments, radius, thickness, fill);
  l.segments = count;
  l.segmentGap = gapDeg;
  return l;
}

Layer Vertical(Layer l, float lengthV) {
  l.lengthV = lengthV;
  return l;
}

Crosshair Make(const char* id, const char* name, const char* category, std::initializer_list<Layer> layers) {
  Crosshair c;
  c.id = id;
  c.name = name;
  c.category = category;
  c.builtin = true;
  c.layers = layers;
  return c;
}

// Order matters: "All" shows presets in this order, so the styles players use most come first
// (small static crosses with and without outline, dots), then circles, sniper, shapes, accessibility, fun.
// Ids of earlier versions are kept so user overrides stay attached.
std::vector<Crosshair> BuildPresets() {
  std::vector<Crosshair> p;
  const auto add = [&](const char* id, const char* name, const char* cat, std::initializer_list<Layer> layers) {
    p.push_back(Make(id, name, cat, layers));
  };
  constexpr bool kNoOutline = false;
  // Most used: compact static crosses and dots.
  add("classic.pro_green", "Pro Static", "classic", {Cross(4, 2, 1, kNeon, kRayAll, kNoOutline)});
  add("classic.pro_cyan", "Pro Cyan", "classic", {Cross(4, 2, 1, kCyan, kRayAll, kNoOutline)});
  add("classic.cross", "Classic Cross", "classic", {Cross(6, 2, 3, kWhite)});
  add("classic.small", "Small Cross", "classic", {Cross(3, 2, 1, kWhite, kRayAll, kNoOutline)});
  add("dot.micro", "Micro Dot", "dot", {Dot(2, kWhite)});
  add("dot.round", "Dot", "dot", {Dot(4, kWhite, true)});
  add("classic.cross_dot", "Cross + Dot", "classic", {Cross(6, 2, 3, kWhite), Dot(2, kWhite)});
  add("classic.thin", "Thin Cross", "classic", {Cross(5, 1, 2, kWhite)});
  add("classic.thin_clean", "Thin Clean", "classic", {Cross(4, 1, 2, kWhite, kRayAll, kNoOutline)});
  add("classic.yellow", "Yellow Static", "classic", {Cross(4, 2, 1, kYellow, kRayAll, kNoOutline)});
  add("classic.tight", "Tight Cross", "classic", {Cross(3, 2, 2, kRed, kRayAll, kNoOutline)});
  add("classic.green_ol", "Green Outline", "classic", {Cross(5, 2, 2, kGreen)});
  add("classic.cyan_dot", "Cyan Cross + Dot", "classic", {Cross(4, 2, 2, kCyan, kRayAll, kNoOutline), Dot(2, kCyan, false, kNoOutline)});
  add("classic.medium", "Medium Cross", "classic", {Cross(8, 2, 3, kWhite)});
  add("classic.wide", "Wide Cross", "classic", {Cross(10, 2, 6, kWhite)});
  add("classic.thick", "Thick Cross", "classic", {Cross(6, 3, 3, kWhite)});
  add("classic.pink", "Pink Static", "classic", {Cross(4, 2, 1, kMagenta, kRayAll, kNoOutline)});
  add("classic.tall", "Tall Cross", "classic", {Vertical(Cross(4, 2, 2, kNeon, kRayAll, kNoOutline), 7)});
  add("classic.plus", "Plus", "classic", {Plus(5, 2, kWhite)});
  add("classic.plus_mini", "Mini Plus", "classic", {Plus(3, 2, kWhite)});
  add("classic.plus_thin", "Thin Plus", "classic", {Plus(6, 1, kWhite)});
  add("classic.t", "T-Shape", "classic", {WithShape(Cross(6, 2, 3, kWhite), Shape::TShape)});
  add("classic.t_green", "T-Shape Green", "classic", {WithShape(Cross(4, 2, 1, kNeon, kRayAll, kNoOutline), Shape::TShape)});
  // Dots.
  add("dot.micro_clean", "Micro Dot Clean", "dot", {Dot(2, kWhite, false, kNoOutline)});
  add("dot.red", "Red Dot", "dot", {Dot(4, kRed, true)});
  add("dot.green", "Green Dot", "dot", {Dot(4, kGreen, true)});
  add("dot.cyan", "Cyan Dot", "dot", {Dot(3, kCyan)});
  add("dot.pink", "Pink Dot", "dot", {Dot(3, kMagenta, false, kNoOutline)});
  add("dot.big", "Big Dot", "dot", {Dot(6, kWhite, true)});
  // Circles.
  add("circle.ring_dot", "Ring + Dot", "circle", {Ring(10, 1.5f, kWhite), Dot(2, kWhite)});
  add("circle.ring", "Ring", "circle", {Ring(8, 1.5f, kWhite)});
  add("circle.halo", "Halo Dot", "circle", {Ring(4, 1, kWhite), Dot(2, kWhite)});
  add("circle.ring_cross", "Ring + Cross", "circle", {Ring(9, 1.5f, kWhite), Cross(5, 2, 11, kWhite), Dot(2, kWhite)});
  add("circle.segments", "Segment Ring", "circle", {SegmentRing(12, 2, 4, 30, kLavender), Dot(2, kLavender)});
  add("circle.shotgun", "Shotgun Spread", "circle", {Ring(28, 2, kWhite), Dot(2, kWhite)});
  add("circle.target", "Target", "circle", {Ring(12, 1.5f, kCoral), Ring(6, 1.5f, kCoral), Dot(2, kCoral)});
  // Sniper.
  add("sniper.dot_lines", "Dot + Lines", "sniper", {Cross(20, 1, 10, kWhite), Dot(3, kRed)});
  add("sniper.horizon", "Horizon Line", "sniper", {HLine(18, 1, 5, kWhite), Dot(3, kWhite)});
  add("sniper.long", "Long Thin", "sniper", {Cross(40, 1, 6, kWhite)});
  add("sniper.duplex", "Duplex", "sniper", {Cross(24, 3, 16, kWhite), Cross(16, 1, 0, kWhite)});
  add("sniper.chevron", "Chevron Sight", "sniper", {Armed(Shape::Chevron, 1, 6, 1.5f, kOrange), HLine(16, 1, 10, kWhite)});
  // Shapes.
  add("tactical.x", "X Cross", "tactical", {WithShape(Cross(6, 2, 3, kCoral), Shape::XCross)});
  add("tactical.x_dot", "X + Dot", "tactical", {WithShape(Cross(5, 2, 4, kWhite), Shape::XCross), Dot(2, kWhite)});
  add("tactical.chevron", "Chevron", "tactical", {Armed(Shape::Chevron, 2, 7, 2, kOrange)});
  add("tactical.corners", "Corners", "tactical", {Armed(Shape::Corners, 10, 5, 2, kWhite), Dot(2, kWhite)});
  add("tactical.corners_wide", "Wide Corners", "tactical", {Armed(Shape::Corners, 18, 6, 2, kCyan)});
  add("tactical.box", "Box", "tactical", {Shaped(Shape::Box, 6, 1, kWhite), Dot(2, kWhite)});
  add("tactical.triangle", "Triangle", "tactical", {Shaped(Shape::Triangle, 8, 1.5f, kWhite)});
  add("tactical.triangle_dot", "Delta Dot", "tactical", {Shaped(Shape::Triangle, 12, 1.5f, kSun), Dot(2, kSun)});
  add("tactical.diamond", "Diamond Outline", "tactical", {Shaped(Shape::Diamond, 7, 1.5f, kLavender)});
  add("tactical.diamond_dot", "Diamond + Dot", "tactical", {Shaped(Shape::Diamond, 9, 1.5f, kWhite), Dot(2, kWhite)});
  // Accessibility: large, thick, high-contrast.
  add("access.big_sun", "Big Contrast", "access", {Outlined(Cross(12, 4, 5, kSun), 2)});
  add("access.big_plus", "Big Plus", "access", {Outlined(Plus(10, 4, kCyan), 2)});
  add("access.big_dot", "Big Dot Contrast", "access", {Outlined(Dot(10, kCyan, true), 2)});
  add("access.big_ring", "Big Ring", "access", {Outlined(Ring(12, 3, kYellow), 2), Outlined(Dot(4, kYellow), 2)});
  add("access.amber", "Amber Cross", "access", {Outlined(Cross(8, 3, 3, kAmber), 2)});
  // Fun.
  add("fun.sightline", "Sightline", "fun",
      {Cross(7, 2, 3, kSun, kRayUp), Cross(7, 2, 3, kOrange, kRayRight), Cross(7, 2, 3, kVolt, kRayDown),
       Cross(7, 2, 3, kLavender, kRayLeft)});
  add("fun.diamond", "Diamond", "fun", {Shaped(Shape::Diamond, 5, 1, kMagenta, true)});
  add("fun.neon", "Neon Ring", "fun", {Ring(6, 2, kMagenta), Cross(3, 2, 9, kCyan, kRayAll, kNoOutline)});
  return p;
}

std::string B64UrlEncode(std::string_view in) {
  static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::string out;
  size_t i = 0;
  while (i + 2 < in.size()) {
    const unsigned v = (static_cast<unsigned char>(in[i]) << 16) | (static_cast<unsigned char>(in[i + 1]) << 8) |
                       static_cast<unsigned char>(in[i + 2]);
    out += tbl[(v >> 18) & 63];
    out += tbl[(v >> 12) & 63];
    out += tbl[(v >> 6) & 63];
    out += tbl[v & 63];
    i += 3;
  }
  if (i + 1 == in.size()) {
    const unsigned v = static_cast<unsigned char>(in[i]) << 16;
    out += tbl[(v >> 18) & 63];
    out += tbl[(v >> 12) & 63];
  } else if (i + 2 == in.size()) {
    const unsigned v = (static_cast<unsigned char>(in[i]) << 16) | (static_cast<unsigned char>(in[i + 1]) << 8);
    out += tbl[(v >> 18) & 63];
    out += tbl[(v >> 12) & 63];
    out += tbl[(v >> 6) & 63];
  }
  return out;
}

bool B64UrlDecode(std::string_view in, std::string& out) {
  auto val = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '-' || c == '+') return 62;
    if (c == '_' || c == '/') return 63;
    return -1;
  };
  unsigned acc = 0;
  int bits = 0;
  for (const char c : in) {
    if (c == '=' || c == ' ' || c == '\r' || c == '\n' || c == '\t') continue;
    const int v = val(c);
    if (v < 0) return false;
    acc = (acc << 6) | static_cast<unsigned>(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out += static_cast<char>((acc >> bits) & 0xFF);
    }
  }
  return true;
}

std::wstring_view TrimView(std::wstring_view s) {
  while (!s.empty() && iswspace(s.front())) s.remove_prefix(1);
  while (!s.empty() && iswspace(s.back())) s.remove_suffix(1);
  return s;
}

json::Value StringList(const std::vector<std::wstring>& list) {
  json::Value a = json::Value::Array();
  for (const auto& s : list) a.Push(Narrow(s));
  return a;
}

std::vector<std::wstring> ReadStringList(const json::Value& v) {
  std::vector<std::wstring> out;
  for (size_t i = 0; i < v.Size(); ++i) {
    const std::wstring s = Lower(TrimView(Widen(v.At(i).Str())));
    if (!s.empty()) out.push_back(s);
  }
  return out;
}

json::Value HotkeyJson(const Hotkey& h) {
  json::Value o = json::Value::Object();
  o.Set("mods", h.mods);
  o.Set("vk", h.vk);
  return o;
}

Hotkey ReadHotkey(const json::Value& v, const Hotkey& def) {
  if (!v.IsObject()) return def;
  Hotkey h;
  h.mods = static_cast<UINT>(v["mods"].Int()) & (MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN);
  h.vk = static_cast<UINT>(v["vk"].Int()) & 0xFF;
  return h;
}

}  // namespace

Color ColorFromHex(std::string_view s, bool* ok) {
  if (ok) *ok = false;
  if (!s.empty() && s.front() == '#') s.remove_prefix(1);
  if (s.size() != 6 && s.size() != 8) return {};
  int v[4] = {0, 0, 0, 255};
  for (size_t i = 0; i < s.size() / 2; ++i) {
    const int hi = HexDigit(s[2 * i]);
    const int lo = HexDigit(s[2 * i + 1]);
    if (hi < 0 || lo < 0) return {};
    v[i] = hi * 16 + lo;
  }
  if (ok) *ok = true;
  return {v[0] / 255.f, v[1] / 255.f, v[2] / 255.f, v[3] / 255.f};
}

std::string ColorToHex(const Color& c) {
  const auto q = [](float f) { return static_cast<int>(std::lround(Clampf(f, 0, 1) * 255)); };
  char buf[16];
  if (q(c.a) == 255) snprintf(buf, sizeof(buf), "#%02X%02X%02X", q(c.r), q(c.g), q(c.b));
  else snprintf(buf, sizeof(buf), "#%02X%02X%02X%02X", q(c.r), q(c.g), q(c.b), q(c.a));
  return buf;
}

const char* ShapeKey(Shape s) {
  const int i = static_cast<int>(s);
  return (i >= 0 && i < static_cast<int>(Shape::Count)) ? kShapeKeys[i] : kShapeKeys[0];
}

Shape ShapeFromKey(std::string_view key) {
  for (int i = 0; i < static_cast<int>(Shape::Count); ++i) {
    if (key == kShapeKeys[i]) return static_cast<Shape>(i);
  }
  return Shape::Cross;
}

std::wstring KeyName(UINT vk) {
  switch (vk) {
    case 0: return L"";
    case VK_LBUTTON: return L"LMB";
    case VK_RBUTTON: return L"RMB";
    case VK_MBUTTON: return L"MMB";
    case VK_XBUTTON1: return L"Mouse 4";
    case VK_XBUTTON2: return L"Mouse 5";
    default: break;
  }
  UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC_EX);
  bool extended = (sc & 0xFF00) == 0xE000;
  switch (vk) {
    case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: case VK_DIVIDE: case VK_NUMLOCK:
      extended = true;
      break;
    default: break;
  }
  const LONG lparam = static_cast<LONG>(((sc & 0xFF) << 16) | (extended ? (1 << 24) : 0));
  wchar_t buf[64] = {};
  if (GetKeyNameTextW(lparam, buf, static_cast<int>(std::size(buf))) > 0) return buf;
  wchar_t fb[16];
  swprintf_s(fb, L"VK %u", vk);
  return fb;
}

std::wstring HotkeyToText(const Hotkey& h) {
  if (!h.Valid()) return L"";
  std::wstring s;
  if (h.mods & MOD_CONTROL) s += L"Ctrl+";
  if (h.mods & MOD_SHIFT) s += L"Shift+";
  if (h.mods & MOD_ALT) s += L"Alt+";
  if (h.mods & MOD_WIN) s += L"Win+";
  return s + KeyName(h.vk);
}

const std::vector<Crosshair>& BuiltinPresets() {
  static const std::vector<Crosshair> presets = BuildPresets();
  return presets;
}

const Crosshair* FindBuiltin(std::string_view id) {
  for (const auto& p : BuiltinPresets()) {
    if (p.id == id) return &p;
  }
  return nullptr;
}

std::vector<const Crosshair*> Library(const Config& cfg) {
  std::vector<const Crosshair*> out;
  out.reserve(BuiltinPresets().size() + cfg.crosshairs.size());
  for (const auto& p : BuiltinPresets()) {
    const Crosshair* chosen = &p;
    for (const auto& u : cfg.crosshairs) {
      if (u.id == p.id) chosen = &u;
    }
    out.push_back(chosen);
  }
  for (const auto& u : cfg.crosshairs) {
    if (!FindBuiltin(u.id)) out.push_back(&u);
  }
  return out;
}

const Crosshair* FindCrosshair(const Config& cfg, std::string_view id) {
  for (const auto& u : cfg.crosshairs) {
    if (u.id == id) return &u;
  }
  return FindBuiltin(id);
}

const Crosshair& ResolveCrosshair(const Config& cfg, std::string_view id) {
  if (const Crosshair* c = FindCrosshair(cfg, id)) return *c;
  return BuiltinPresets().front();
}

bool SameContent(const Crosshair& a, const Crosshair& b) {
  // Everything except per-user metadata (favourite, hotkey) and identity.
  Crosshair x = a, y = b;
  x.favorite = y.favorite = false;
  x.hotkey = y.hotkey = {};
  x.id = y.id;
  x.category = y.category;
  return CrosshairFingerprint(x) == CrosshairFingerprint(y);
}

bool IsModifiedPreset(const Config& cfg, std::string_view id) {
  const Crosshair* builtin = FindBuiltin(id);
  if (!builtin) return false;
  for (const auto& u : cfg.crosshairs) {
    if (u.id == id) return !SameContent(u, *builtin);
  }
  return false;
}

Crosshair& EditableCrosshair(Config& cfg, std::string_view id) {
  for (auto& u : cfg.crosshairs) {
    if (u.id == id) return u;
  }
  if (const Crosshair* b = FindBuiltin(id)) {
    cfg.crosshairs.push_back(*b);
    return cfg.crosshairs.back();
  }
  // Unknown id: materialise the fallback preset under that id so edits are never lost.
  Crosshair c = BuiltinPresets().front();
  c.id = std::string(id);
  c.builtin = false;
  c.category = "mine";
  cfg.crosshairs.push_back(std::move(c));
  return cfg.crosshairs.back();
}

void RemoveUserCrosshair(Config& cfg, std::string_view id) {
  cfg.crosshairs.erase(std::remove_if(cfg.crosshairs.begin(), cfg.crosshairs.end(),
                                      [&](const Crosshair& c) { return c.id == id; }),
                       cfg.crosshairs.end());
}

std::string NewCrosshairId() {
  LARGE_INTEGER qpc{};
  QueryPerformanceCounter(&qpc);
  const unsigned long long v = static_cast<unsigned long long>(qpc.QuadPart) ^ (GetTickCount64() << 20) ^
                               (static_cast<unsigned long long>(GetCurrentProcessId()) << 40);
  char buf[32];
  snprintf(buf, sizeof(buf), "u-%012llx", v & 0xFFFFFFFFFFFFull);
  return buf;
}

json::Value ToJson(const Layer& l) {
  json::Value o = json::Value::Object();
  o.Set("shape", ShapeKey(l.shape));
  o.Set("length", l.length);
  if (l.lengthV > 0) o.Set("lengthV", l.lengthV);
  o.Set("thickness", l.thickness);
  o.Set("gap", l.gap);
  o.Set("size", l.size);
  o.Set("rays", l.rays);
  o.Set("segments", l.segments);
  o.Set("segmentGap", l.segmentGap);
  o.Set("rotation", l.rotation);
  o.Set("round", l.round);
  o.Set("filled", l.filled);
  o.Set("fill", ColorToHex(l.fill));
  o.Set("opacity", l.opacity);
  o.Set("outline", l.outline);
  o.Set("outlineThickness", l.outlineThickness);
  o.Set("outlineColor", ColorToHex(l.outlineColor));
  o.Set("outlineOpacity", l.outlineOpacity);
  o.Set("offsetX", l.offsetX);
  o.Set("offsetY", l.offsetY);
  o.Set("hidden", l.hidden);
  return o;
}

void FromJson(const json::Value& v, Layer& l) {
  const Layer d;
  l.shape = ShapeFromKey(v["shape"].Str());
  l.length = Clampf(v["length"].Float(d.length), 0, 400);
  l.lengthV = Clampf(v["lengthV"].Float(0), 0, 400);
  l.thickness = Clampf(v["thickness"].Float(d.thickness), 0.5f, 64);
  l.gap = Clampf(v["gap"].Float(d.gap), 0, 400);
  l.size = Clampf(v["size"].Float(d.size), 0.5f, 400);
  l.rays = v["rays"].Int(d.rays) & kRayAll;
  l.segments = std::clamp(v["segments"].Int(d.segments), 1, 36);
  l.segmentGap = Clampf(v["segmentGap"].Float(d.segmentGap), 0, 170);
  l.rotation = v["rotation"].Float(d.rotation);
  l.round = v["round"].Bool(d.round);
  l.filled = v["filled"].Bool(d.filled);
  l.fill =ColorFromHex(v["fill"].Str("#FFFFFF"));
  l.opacity = Clampf(v["opacity"].Float(d.opacity), 0, 1);
  l.outline = v["outline"].Bool(d.outline);
  l.outlineThickness = Clampf(v["outlineThickness"].Float(d.outlineThickness), 0, 16);
  l.outlineColor = ColorFromHex(v["outlineColor"].Str("#000000"));
  l.outlineOpacity = Clampf(v["outlineOpacity"].Float(d.outlineOpacity), 0, 1);
  l.offsetX = Clampf(v["offsetX"].Float(0), -400, 400);
  l.offsetY = Clampf(v["offsetY"].Float(0), -400, 400);
  l.hidden = v["hidden"].Bool(false);
}

json::Value ToJson(const Crosshair& c) {
  json::Value o = json::Value::Object();
  o.Set("id", c.id);
  o.Set("name", c.name);
  o.Set("category", c.category);
  o.Set("favorite", c.favorite);
  o.Set("scale", c.scale);
  o.Set("opacity", c.opacity);
  json::Value layers = json::Value::Array();
  for (const auto& l : c.layers) layers.Push(ToJson(l));
  o.Set("layers", std::move(layers));
  if (c.hotkey.Valid()) o.Set("hotkey", HotkeyJson(c.hotkey));
  return o;
}

void FromJson(const json::Value& v, Crosshair& c) {
  c.id = v["id"].Str();
  c.name = v["name"].Str("Crosshair");
  c.category = v["category"].Str("mine");
  c.favorite = v["favorite"].Bool(false);
  c.scale = Clampf(v["scale"].Float(1), 0.25f, 8);
  c.opacity = Clampf(v["opacity"].Float(1), 0.05f, 1);
  c.builtin = FindBuiltin(c.id) != nullptr;
  c.hotkey = ReadHotkey(v["hotkey"], {});
  c.layers.clear();
  const auto& layers = v["layers"];
  for (size_t i = 0; i < layers.Size() && i < 16; ++i) {
    Layer l;
    FromJson(layers.At(i), l);
    c.layers.push_back(l);
  }
}

json::Value ToJson(const Config& c) {
  const Settings& s = c.settings;
  json::Value st = json::Value::Object();
  st.Set("language", s.language);
  st.Set("theme", s.theme);
  st.Set("monitor", s.monitor);
  st.Set("nudgeMods", s.nudgeMods);
  st.Set("hideWhenCursor", s.hideWhenCursor);
  st.Set("adsCrosshairId", s.adsCrosshairId);
  st.Set("enabled", s.enabled);
  st.Set("startHidden", s.startHidden);
  st.Set("autostart", s.autostart);
  st.Set("centerMode", s.centerMode == CenterMode::Window ? "window" : "monitor");
  st.Set("hideOnDesktop", s.hideOnDesktop);
  st.Set("onlyFullscreen", s.onlyFullscreen);
  st.Set("hideOverSettings", s.hideOverSettings);
  st.Set("excludeFromCapture", s.excludeFromCapture);
  st.Set("offsetX", s.offsetX);
  st.Set("offsetY", s.offsetY);
  st.Set("hkToggle", HotkeyJson(s.hkToggle));
  st.Set("hkNext", HotkeyJson(s.hkNext));
  st.Set("hkPrev", HotkeyJson(s.hkPrev));
  st.Set("hkSettings", HotkeyJson(s.hkSettings));
  st.Set("adsKey", s.adsKey);
  st.Set("adsMode", s.adsMode == AdsMode::Toggle ? "toggle" : "hold");
  st.Set("adsAction", s.adsAction == AdsAction::None       ? "none"
                      : s.adsAction == AdsAction::ShowOnly ? "showOnly"
                      : s.adsAction == AdsAction::Switch   ? "switch"
                                                           : "hide");
  st.Set("filterMode",
         s.filterMode == FilterMode::Whitelist ? "whitelist" : s.filterMode == FilterMode::Blacklist ? "blacklist" : "all");
  st.Set("filterList", StringList(s.filterList));
  st.Set("safeList", StringList(s.safeList));
  json::Value profiles = json::Value::Array();
  for (const auto& p : s.profiles) {
    json::Value o = json::Value::Object();
    o.Set("exe", Narrow(p.exe));
    o.Set("crosshair", p.crosshairId);
    o.Set("offsetX", p.offsetX);
    o.Set("offsetY", p.offsetY);
    profiles.Push(std::move(o));
  }
  st.Set("profiles", std::move(profiles));

  json::Value root = json::Value::Object();
  root.Set("version", c.version);
  root.Set("activeId", c.activeId);
  root.Set("settings", std::move(st));
  json::Value list = json::Value::Array();
  for (const auto& x : c.crosshairs) {
    const Crosshair* builtin = FindBuiltin(x.id);
    if (builtin && SameContent(x, *builtin)) {
      // Preset with only favourite / hotkey set: store the metadata, not a frozen copy, so the
      // preset keeps following its built-in definition.
      if (!x.favorite && !x.hotkey.Valid()) continue;
      json::Value meta = json::Value::Object();
      meta.Set("id", x.id);
      meta.Set("meta", true);
      meta.Set("favorite", x.favorite);
      if (x.hotkey.Valid()) meta.Set("hotkey", HotkeyJson(x.hotkey));
      list.Push(std::move(meta));
    } else {
      list.Push(ToJson(x));
    }
  }
  root.Set("crosshairs", std::move(list));
  return root;
}

void FromJson(const json::Value& v, Config& c) {
  c = Config{};
  c.version = v["version"].Int(kConfigVersion);
  c.activeId = v["activeId"].Str(c.activeId);
  const auto& st = v["settings"];
  Settings& s = c.settings;
  const Settings d;
  s.language = st["language"].Str(d.language);
  s.theme = st["theme"].Str(d.theme);
  if (s.theme != "light" && s.theme != "dark") s.theme = "system";
  s.monitor = std::clamp(st["monitor"].Int(d.monitor), -1, 16);
  s.nudgeMods = static_cast<UINT>(st["nudgeMods"].Int(static_cast<int>(d.nudgeMods))) & (MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN);
  if (c.version < 2) s.nudgeMods = 0;  // v1 stored the old Ctrl+Alt default without the user choosing it
  s.hideWhenCursor = st["hideWhenCursor"].Bool(d.hideWhenCursor);
  s.adsCrosshairId = st["adsCrosshairId"].Str(d.adsCrosshairId);
  s.enabled = st["enabled"].Bool(d.enabled);
  s.startHidden = st["startHidden"].Bool(d.startHidden);
  s.autostart = st["autostart"].Bool(d.autostart);
  s.centerMode = st["centerMode"].Str() == "window" ? CenterMode::Window : CenterMode::Monitor;
  s.hideOnDesktop = st["hideOnDesktop"].Bool(d.hideOnDesktop);
  s.onlyFullscreen = st["onlyFullscreen"].Bool(d.onlyFullscreen);
  s.hideOverSettings = st["hideOverSettings"].Bool(d.hideOverSettings);
  s.excludeFromCapture = st["excludeFromCapture"].Bool(d.excludeFromCapture);
  s.offsetX = std::clamp(st["offsetX"].Int(0), -2000, 2000);
  s.offsetY = std::clamp(st["offsetY"].Int(0), -2000, 2000);
  s.hkToggle = ReadHotkey(st["hkToggle"], d.hkToggle);
  s.hkNext = ReadHotkey(st["hkNext"], d.hkNext);
  s.hkPrev = ReadHotkey(st["hkPrev"], d.hkPrev);
  s.hkSettings = ReadHotkey(st["hkSettings"], d.hkSettings);
  s.adsKey = static_cast<UINT>(st["adsKey"].Int(0)) & 0xFF;
  s.adsMode = st["adsMode"].Str() == "toggle" ? AdsMode::Toggle : AdsMode::Hold;
  const std::string action = st["adsAction"].Str("hide");
  s.adsAction = action == "none"       ? AdsAction::None
                : action == "showOnly" ? AdsAction::ShowOnly
                : action == "switch"   ? AdsAction::Switch
                                       : AdsAction::Hide;
  const std::string fm = st["filterMode"].Str("all");
  s.filterMode = fm == "whitelist" ? FilterMode::Whitelist : fm == "blacklist" ? FilterMode::Blacklist : FilterMode::All;
  s.filterList = ReadStringList(st["filterList"]);
  s.safeList = ReadStringList(st["safeList"]);
  const auto& profiles = st["profiles"];
  for (size_t i = 0; i < profiles.Size(); ++i) {
    const auto& o = profiles.At(i);
    GameProfile p;
    p.exe = Lower(TrimView(Widen(o["exe"].Str())));
    p.crosshairId = o["crosshair"].Str();
    p.offsetX = std::clamp(o["offsetX"].Int(0), -2000, 2000);
    p.offsetY = std::clamp(o["offsetY"].Int(0), -2000, 2000);
    if (!p.exe.empty()) s.profiles.push_back(std::move(p));
  }
  const auto& list = v["crosshairs"];
  for (size_t i = 0; i < list.Size(); ++i) {
    const json::Value& item = list.At(i);
    if (item["meta"].Bool(false)) {
      // Metadata for a built-in preset: rebuild it from the current definition.
      if (const Crosshair* builtin = FindBuiltin(item["id"].Str())) {
        Crosshair x = *builtin;
        x.favorite = item["favorite"].Bool(false);
        x.hotkey = ReadHotkey(item["hotkey"], {});
        c.crosshairs.push_back(std::move(x));
      }
      continue;
    }
    Crosshair x;
    FromJson(item, x);
    if (!x.id.empty() && !x.layers.empty()) c.crosshairs.push_back(std::move(x));
  }
  c.version = kConfigVersion;  // migrations above are done; the next save writes the current version
}

std::string ConfigToString(const Config& c, bool pretty) { return json::Serialize(ToJson(c), pretty); }

bool ConfigFromString(std::string_view text, Config& c) {
  json::Value v;
  if (!json::Parse(text, v) || !v.IsObject()) return false;
  FromJson(v, c);
  return true;
}

std::string CrosshairFingerprint(const Crosshair& c) { return json::Serialize(ToJson(c), false); }

std::wstring ConfigPath() { return ConfigDir() + L"\\config.json"; }

bool LoadConfig(Config& c) {
  const std::wstring path = ConfigPath();
  for (const std::wstring& candidate : {path, path + L".bak"}) {
    std::string text;
    if (ReadFileUtf8(candidate, text) && ConfigFromString(text, c)) return true;
  }
  c = Config{};
  return false;
}

bool SaveConfig(const Config& c) { return WriteFileAtomic(ConfigPath(), ConfigToString(c, true)); }

std::string ExportLibrary(const Config& cfg) {
  json::Value root = json::Value::Object();
  root.Set("format", "sightline-library");
  root.Set("version", 1);
  json::Value list = json::Value::Array();
  for (const auto& c : cfg.crosshairs) {
    // Unchanged presets exist in every install; only real content is exported.
    const Crosshair* builtin = FindBuiltin(c.id);
    if (builtin && SameContent(c, *builtin)) continue;
    Crosshair out = c;
    out.hotkey = {};
    list.Push(ToJson(out));
  }
  root.Set("crosshairs", std::move(list));
  return json::Serialize(root, true);
}

int ImportLibrary(std::string_view text, Config& cfg) {
  json::Value v;
  if (!json::Parse(text, v)) return -1;
  // Accept a library file, a whole config.json, or a bare array of crosshairs.
  const json::Value& list = v.IsArray() ? v : v["crosshairs"];
  if (!list.IsArray()) return -1;
  int count = 0;
  for (size_t i = 0; i < list.Size(); ++i) {
    if (list.At(i)["meta"].Bool(false)) continue;  // preset metadata carries no crosshair
    Crosshair c;
    FromJson(list.At(i), c);
    if (c.layers.empty()) continue;
    // Presets are read-only: an edited preset from the file becomes the user's own crosshair,
    // an unchanged one is skipped.
    if (const Crosshair* builtin = FindBuiltin(c.id)) {
      if (SameContent(c, *builtin)) continue;
    }
    c.id = NewCrosshairId();
    c.category = "mine";
    c.builtin = false;
    c.favorite = false;
    c.hotkey = {};  // avoid clashing with hotkeys already bound here
    cfg.crosshairs.push_back(std::move(c));
    ++count;
  }
  return count;
}

std::string EncodeShareCode(const Crosshair& c) {
  // Share only the look: no id, favourite or hotkey of the sender.
  Crosshair shared = c;
  shared.id.clear();
  shared.favorite = false;
  shared.hotkey = {};
  json::Value v = ToJson(shared);
  return "SL1." + B64UrlEncode(json::Serialize(v, false));
}

bool DecodeShareCode(std::string_view code, Crosshair& out) {
  while (!code.empty() && (code.front() == ' ' || code.front() == '\n' || code.front() == '\r')) code.remove_prefix(1);
  while (!code.empty() && (code.back() == ' ' || code.back() == '\n' || code.back() == '\r')) code.remove_suffix(1);
  if (code.size() < 5 || (code.substr(0, 4) != "SL1." && code.substr(0, 4) != "SL1-")) return false;
  std::string raw;
  if (!B64UrlDecode(code.substr(4), raw)) return false;
  json::Value v;
  if (!json::Parse(raw, v) || !v.IsObject()) return false;
  Crosshair c;
  FromJson(v, c);
  if (c.layers.empty()) return false;
  c.id = NewCrosshairId();
  c.builtin = false;
  c.category = "mine";
  c.favorite = false;
  c.hotkey = {};  // codes from older builds may still carry the sender's hotkey
  out = std::move(c);
  return true;
}

}  // namespace sl
