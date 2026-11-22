// Crosshair code importers: Sightline (SL1.), Valorant profile codes and CS2 share codes.
// CS2 layout follows the community-documented format (akiver/csgo-sharecode, doc/code_layout.md).

#include "common/model.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace sl {

namespace {

Color Rgb(int r, int g, int b, int a = 255) { return {r / 255.f, g / 255.f, b / 255.f, a / 255.f}; }

int PrimaryHeight() {
  const int h = GetSystemMetrics(SM_CYSCREEN);
  return h > 0 ? h : 1080;
}

Layer Bars(float length, float thickness, float gap, Color c, int rays, int outlinePx, float outlineOpacity, float opacity) {
  Layer l;
  l.shape = Shape::Cross;
  l.length = std::max(1.f, length);
  l.thickness = std::max(1.f, thickness);
  l.gap = std::max(0.f, gap);
  l.rays = rays;
  l.fill = {c.r, c.g, c.b, 1.f};
  l.opacity = std::clamp(opacity * c.a, 0.05f, 1.f);
  l.outline = outlinePx > 0 && outlineOpacity > 0;
  l.outlineThickness = static_cast<float>(std::max(1, outlinePx));
  l.outlineOpacity = std::clamp(outlineOpacity, 0.f, 1.f);
  return l;
}

// ───────── Valorant ─────────

bool ParseValorant(std::string_view code, Crosshair& out) {
  if (code.find(';') == std::string_view::npos || code.find('P') == std::string_view::npos) return false;
  std::vector<std::string> tok;
  std::string cur;
  for (const char ch : code) {
    if (ch == ';') {
      tok.push_back(cur);
      cur.clear();
    } else if (ch != ' ' && ch != '\r' && ch != '\n') {
      cur += ch;
    }
  }
  if (!cur.empty()) tok.push_back(cur);

  // Collect key/value pairs of the primary ("P") section only.
  std::map<std::string, std::string> kv;
  bool primary = false;
  for (size_t i = 0; i < tok.size(); ++i) {
    const std::string& k = tok[i];
    if (i == 0 && !k.empty() && isdigit(static_cast<unsigned char>(k[0]))) continue;  // leading format version
    if (k == "P") {
      primary = true;
      continue;
    }
    if (k == "A" || k == "S") {
      primary = false;
      continue;
    }
    if (i + 1 >= tok.size()) break;
    if (primary || (kv.empty() && k != "0")) kv[k] = tok[i + 1];
    ++i;
  }
  if (!primary && kv.empty()) return false;

  const auto num = [&](const char* key, double def) {
    const auto it = kv.find(key);
    if (it == kv.end()) return def;
    char* end = nullptr;
    const double v = strtod(it->second.c_str(), &end);
    return end && *end == '\0' ? v : def;
  };

  static const Color kColors[] = {Rgb(255, 255, 255), Rgb(0, 255, 0),   Rgb(127, 255, 0), Rgb(223, 255, 0),
                                  Rgb(255, 255, 0),   Rgb(0, 255, 255), Rgb(255, 0, 255), Rgb(255, 0, 0)};
  Color color = kColors[0];
  const int ci = static_cast<int>(num("c", 0));
  if (ci >= 0 && ci < 8) color = kColors[ci];
  if (ci == 8 || kv.count("u")) {
    bool ok = false;
    const Color c = ColorFromHex(kv.count("u") ? kv["u"] : "", &ok);
    if (ok) color = c;
  }

  const bool outline = num("h", 1) != 0;
  const int outlinePx = outline ? std::max(1, static_cast<int>(std::lround(num("t", 1)))) : 0;
  const float outlineOpacity = static_cast<float>(num("o", 0.5));

  Crosshair c;
  c.name = "Valorant";
  const auto lines = [&](const char* p, double defT, double defL, double defO, double defA) {
    const std::string pre(p);
    if (num((pre + "b").c_str(), 1) == 0) return;
    const float t = static_cast<float>(num((pre + "t").c_str(), defT));
    const float l = static_cast<float>(num((pre + "l").c_str(), defL));
    const float v = static_cast<float>(num((pre + "v").c_str(), l));
    const float o = static_cast<float>(num((pre + "o").c_str(), defO));
    const float a = static_cast<float>(num((pre + "a").c_str(), defA));
    if (t <= 0 || (l <= 0 && v <= 0)) return;
    // Some exporters write "<n>v" without the "<n>g" toggle; a differing vertical length implies it.
    const bool separate =
        (num((pre + "g").c_str(), 0) != 0 || kv.count(pre + "v")) && std::fabs(v - l) > 0.01f;
    if (separate) {
      // One layer with its own vertical length keeps the outline continuous and stays editable.
      Layer bars = Bars(std::max(l, 0.f), t, o, color, kRayAll, outlinePx, outlineOpacity, a);
      bars.length = std::max(l, 0.f);
      bars.lengthV = std::max(v, 0.f);
      if (bars.length <= 0) bars.rays = kRayUp | kRayDown;
      if (bars.lengthV <= 0) bars.rays = kRayLeft | kRayRight;
      c.layers.push_back(bars);
    } else {
      c.layers.push_back(Bars(l, t, o, color, kRayAll, outlinePx, outlineOpacity, a));
    }
  };
  lines("1", 2, 2, 10, 0.35);  // outer lines first so the inner ones draw on top
  lines("0", 2, 6, 3, 0.8);
  if (num("d", 0) != 0) {
    Layer dot;
    dot.shape = Shape::Dot;
    dot.size = static_cast<float>(std::max(1.0, num("z", 2)));
    dot.fill = {color.r, color.g, color.b, 1.f};
    dot.opacity = std::clamp(static_cast<float>(num("a", 1)) * color.a, 0.05f, 1.f);
    dot.outline = outlinePx > 0;
    dot.outlineThickness = static_cast<float>(std::max(1, outlinePx));
    dot.outlineOpacity = outlineOpacity;
    c.layers.push_back(dot);
  }
  if (c.layers.empty()) return false;
  out = std::move(c);
  return true;
}

// ───────── CS2 ─────────

constexpr char kCsDict[] = "ABCDEFGHJKLMNOPQRSTUVWXYZabcdefhijkmnopqrstuvwxyz23456789";

bool CsCharsToBytes(std::string_view s, size_t n, std::vector<uint8_t>& out) {
  std::vector<uint8_t> le(n + 8, 0);  // little-endian working buffer
  for (size_t i = s.size(); i-- > 0;) {  // last char is the most significant digit
    const char* p = std::strchr(kCsDict, s[i]);
    if (!s[i] || !p) return false;
    unsigned carry = static_cast<unsigned>(p - kCsDict);
    for (auto& b : le) {
      const unsigned v = b * 57u + carry;
      b = static_cast<uint8_t>(v & 0xFF);
      carry = v >> 8;
    }
  }
  for (size_t i = n; i < le.size(); ++i) {
    if (le[i]) return false;
  }
  out.assign(n, 0);
  for (size_t i = 0; i < n; ++i) out[i] = le[n - 1 - i];
  unsigned sum = 0;
  for (size_t i = 1; i < n; ++i) sum += out[i];
  return out[0] == (sum & 0xFF);
}

struct CsFields {
  float length = 0, thickness = 0, gap = 0;  // pixels at the target screen
  Color color{1, 1, 1, 1};
  Color outlineColor{0, 0, 0, 1};
  int outlinePx = 0;
  bool dot = false, tStyle = false;
};

Crosshair FromCs(const CsFields& f) {
  Crosshair c;
  c.name = "CS2";
  const int rays = f.tStyle ? (kRayRight | kRayDown | kRayLeft) : kRayAll;
  if (f.length > 0) {
    Layer l = Bars(f.length, f.thickness, f.gap, f.color, rays, f.outlinePx, f.outlineColor.a, 1.f);
    l.outlineColor = {f.outlineColor.r, f.outlineColor.g, f.outlineColor.b, 1.f};
    c.layers.push_back(l);
  }
  if (f.dot || c.layers.empty()) {
    Layer d;
    d.shape = Shape::Dot;
    d.size = std::max(1.f, f.thickness);
    d.fill = {f.color.r, f.color.g, f.color.b, 1.f};
    d.opacity = std::clamp(f.color.a, 0.05f, 1.f);
    d.outline = f.outlinePx > 0;
    d.outlineThickness = static_cast<float>(std::max(1, f.outlinePx));
    d.outlineColor = {f.outlineColor.r, f.outlineColor.g, f.outlineColor.b, 1.f};
    d.outlineOpacity = f.outlineColor.a;
    c.layers.push_back(d);
  }
  return c;
}

bool ParseCs2(std::string_view code, Crosshair& out) {
  std::string s(code);
  s.erase(std::remove_if(s.begin(), s.end(), [](char ch) { return ch == ' ' || ch == '\r' || ch == '\n'; }), s.end());
  const int target = PrimaryHeight();
  CsFields f;
  std::vector<uint8_t> b;

  if (s.rfind("CSGO", 0) == 0) {
    std::string body;
    for (size_t i = 4; i < s.size(); ++i) {
      if (s[i] != '-') body += s[i];
    }
    if (body.size() != 25 || !CsCharsToBytes(body, 18, b)) return false;
    const int version = b[1];
    if (version == 1) {
      // CS:GO-era units: tenths, scaled by YRES (screen height / 480).
      const float k = target / 480.f;
      const float gap = static_cast<int8_t>(b[2]) / 10.f;
      const float outline = b[3] / 2.f;
      const int colorIdx = b[10] & 7;
      const bool outlineOn = (b[10] & 8) != 0;
      const float thickness = b[12] / 10.f;
      const bool alphaOn = (b[13] & 0x40) != 0;
      f.dot = (b[13] & 0x10) != 0;
      f.tStyle = (b[13] & 0x80) != 0;
      const float size = b[14] / 10.f;
      static const Color kPresets[] = {Rgb(250, 50, 50), Rgb(50, 250, 50), Rgb(250, 250, 50), Rgb(50, 50, 250),
                                       Rgb(50, 250, 250)};
      f.color = colorIdx < 5 ? kPresets[colorIdx] : Rgb(b[4], b[5], b[6]);
      f.color.a = alphaOn ? b[7] / 255.f : 1.f;
      f.length = std::round(size * k);
      f.thickness = std::max(1.f, std::round(thickness * k));
      f.gap = std::max(0.f, std::round(4.f * target / 1080.f + gap));
      f.outlinePx = outlineOn ? std::max(1, static_cast<int>(std::lround(outline))) : 0;
    } else if (version == 3 || version == 4) {
      const int height = std::max(240, b[14] | (b[15] << 8));
      const float k = static_cast<float>(target) / height;
      const uint32_t bits = b[10] | (b[11] << 8) | (b[12] << 16) | (static_cast<uint32_t>(b[13]) << 24);
      f.dot = (b[2] & 0x40) != 0;
      f.tStyle = (b[2] & 0x80) != 0;
      const bool outlineOn = version == 3 ? (b[2] & 0x20) != 0 : ((bits >> 28) & 3) != 0;
      f.color = Rgb(b[3], b[4], b[5], b[6]);
      f.gap = std::round(b[7] * k);
      f.length = std::round(b[8] * k);
      f.thickness = std::max(1.f, std::round(((bits >> 23) & 0x1F) * k));
      f.outlinePx = outlineOn ? 1 : 0;
    } else {
      return false;
    }
  } else if (s.rfind("CS", 0) == 0 && s.size() == 46) {
    if (!CsCharsToBytes(std::string_view(s).substr(2), 32, b) || b[1] != 1) return false;
    const int height = std::max(240, b[2] | (b[3] << 8));
    const float k = static_cast<float>(target) / height;
    f.dot = (b[4] & 0x40) != 0;
    f.tStyle = (b[4] & 0x80) != 0;
    f.color = Rgb(b[5], b[6], b[7], b[8]);
    f.outlineColor = Rgb(b[9], b[10], b[11], b[12]);
    f.thickness = std::max(1.f, std::round(std::min(b[13] & 0x3F, 32) * k));
    const int outlineMode = std::min(b[13] >> 6, 2);
    f.outlinePx = outlineMode ? 1 : 0;
    const int gap = std::clamp<int>(static_cast<int16_t>(b[14] | (b[15] << 8)), -3840, 3840);
    f.gap = std::round(gap * k);
    f.length = std::round(b[16] * k);
  } else {
    return false;
  }
  out = FromCs(f);
  return true;
}

}  // namespace

CodeSource ImportCrosshairCode(std::string_view code, Crosshair& out) {
  while (!code.empty() && (code.front() == ' ' || code.front() == '\n' || code.front() == '\r' || code.front() == '\t')) {
    code.remove_prefix(1);
  }
  while (!code.empty() && (code.back() == ' ' || code.back() == '\n' || code.back() == '\r' || code.back() == '\t')) {
    code.remove_suffix(1);
  }
  Crosshair c;
  CodeSource src = CodeSource::None;
  if (DecodeShareCode(code, c)) return out = std::move(c), CodeSource::Sightline;
  if (code.rfind("CS", 0) == 0 && ParseCs2(code, c)) src = CodeSource::Cs2;
  else if (ParseValorant(code, c)) src = CodeSource::Valorant;
  if (src == CodeSource::None) return src;
  c.id = NewCrosshairId();
  c.category = "mine";
  c.builtin = false;
  out = std::move(c);
  return src;
}

}  // namespace sl
