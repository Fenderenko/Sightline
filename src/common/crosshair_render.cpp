#include "common/crosshair_render.h"

#include <algorithm>
#include <cmath>

namespace sl {

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr int kSs = 4;  // supersampling grid per axis

struct Rgba {
  float r = 0, g = 0, b = 0, a = 0;  // straight alpha
};

struct Box {
  float l, t, r, b;
};

struct Pt {
  float x, y;
};

int Round(float v) { return static_cast<int>(std::lround(v)); }

bool AxisAligned(float rotation) {
  const float m = std::fmod(std::fabs(rotation), 90.f);
  return m < 0.01f || m > 89.99f;
}

int RotateRays(int rays, int quarterTurns) {
  quarterTurns = ((quarterTurns % 4) + 4) % 4;
  int r = 0;
  for (int i = 0; i < 4; ++i) {
    if (rays & (1 << i)) r |= 1 << ((i + quarterTurns) % 4);
  }
  return r;
}

// Premultiplied float canvas.
class Canvas {
 public:
  Canvas(int w, int h) : w_(w), h_(h), buf_(static_cast<size_t>(w) * h) {}

  int Width() const { return w_; }
  int Height() const { return h_; }

  // Rotation (degrees, clockwise) is applied around `pivot` by inverse-transforming each sample,
  // so shape tests stay in unrotated coordinates.
  template <class InsideFn, class ColorFn>
  void Fill(Box bounds, float rotation, Pt pivot, InsideFn inside, ColorFn color) {
    const bool rotated = std::fabs(rotation) > 1e-4f;
    const float rad = -rotation * kPi / 180.f;
    const float cs = std::cos(rad), sn = std::sin(rad);
    if (rotated) {
      float reach = 0;
      for (const Pt c : {Pt{bounds.l, bounds.t}, Pt{bounds.r, bounds.t}, Pt{bounds.l, bounds.b}, Pt{bounds.r, bounds.b}}) {
        reach = std::max(reach, std::hypot(c.x - pivot.x, c.y - pivot.y));
      }
      bounds = {pivot.x - reach, pivot.y - reach, pivot.x + reach, pivot.y + reach};
    }
    const int x0 = std::max(0, static_cast<int>(std::floor(bounds.l)) - 1);
    const int y0 = std::max(0, static_cast<int>(std::floor(bounds.t)) - 1);
    const int x1 = std::min(w_, static_cast<int>(std::ceil(bounds.r)) + 1);
    const int y1 = std::min(h_, static_cast<int>(std::ceil(bounds.b)) + 1);
    for (int y = y0; y < y1; ++y) {
      for (int x = x0; x < x1; ++x) {
        int hits = 0;
        for (int sy = 0; sy < kSs; ++sy) {
          for (int sx = 0; sx < kSs; ++sx) {
            float px = x + (sx + 0.5f) / kSs, py = y + (sy + 0.5f) / kSs;
            if (rotated) {
              const float dx = px - pivot.x, dy = py - pivot.y;
              px = pivot.x + dx * cs - dy * sn;
              py = pivot.y + dx * sn + dy * cs;
            }
            if (inside(px, py)) ++hits;
          }
        }
        if (!hits) continue;
        const Rgba c = color(x + 0.5f, y + 0.5f);
        Blend(x, y, c, static_cast<float>(hits) / (kSs * kSs));
      }
    }
  }

  void CompositeOver(const Canvas& src, float opacity) {
    for (size_t i = 0; i < buf_.size(); ++i) {
      const Rgba& s = src.buf_[i];
      if (s.a <= 0) continue;
      Rgba& d = buf_[i];
      const float a = s.a * opacity;
      const float k = 1 - a;
      d.r = s.r * opacity + d.r * k;
      d.g = s.g * opacity + d.g * k;
      d.b = s.b * opacity + d.b * k;
      d.a = a + d.a * k;
    }
  }

  void Clear() { std::fill(buf_.begin(), buf_.end(), Rgba{}); }

  Image ToImage(float opacity) const {
    Image img;
    img.width = w_;
    img.height = h_;
    img.pixels.resize(buf_.size());
    const auto q = [](float v) { return static_cast<uint32_t>(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f); };
    for (size_t i = 0; i < buf_.size(); ++i) {
      const Rgba& p = buf_[i];
      img.pixels[i] = (q(p.a * opacity) << 24) | (q(p.r * opacity) << 16) | (q(p.g * opacity) << 8) | q(p.b * opacity);
    }
    return img;
  }

 private:
  void Blend(int x, int y, const Rgba& c, float coverage) {
    Rgba& d = buf_[static_cast<size_t>(y) * w_ + x];
    const float a = c.a * coverage;
    const float k = 1 - a;
    d.r = c.r * a + d.r * k;
    d.g = c.g * a + d.g * k;
    d.b = c.b * a + d.b * k;
    d.a = a + d.a * k;
  }

  int w_, h_;
  std::vector<Rgba> buf_;  // premultiplied
};

// ───────── shape tests ─────────

struct Rect {
  float l, t, r, b;
  bool operator()(float x, float y) const { return x >= l && x < r && y >= t && y < b; }
};

struct RoundRect {
  float l, t, r, b, rad;
  bool operator()(float x, float y) const {
    if (x < l || x >= r || y < t || y >= b) return false;
    const float cx = std::clamp(x, l + rad, r - rad), cy = std::clamp(y, t + rad, b - rad);
    const float dx = x - cx, dy = y - cy;
    return dx * dx + dy * dy <= rad * rad;
  }
};

struct Disc {
  float cx, cy, r;
  bool operator()(float x, float y) const {
    const float dx = x - cx, dy = y - cy;
    return dx * dx + dy * dy <= r * r;
  }
};

// Annulus, optionally limited to an angular range measured clockwise from 12 o'clock.
struct Band {
  float cx, cy, rin, rout;
  float a0 = 0, a1 = 360;
  bool operator()(float x, float y) const {
    const float dx = x - cx, dy = y - cy;
    const float d2 = dx * dx + dy * dy;
    if (d2 < rin * rin || d2 > rout * rout) return false;
    if (a1 - a0 >= 360.f) return true;
    float a = std::atan2(dx, -dy) * 180.f / kPi;
    if (a < 0) a += 360.f;
    float rel = std::fmod(a - a0, 360.f);
    if (rel < 0) rel += 360.f;
    return rel <= a1 - a0;
  }
};

// Convex polygon (any winding).
struct Convex {
  std::vector<Pt> p;
  bool operator()(float x, float y) const {
    int sign = 0;
    for (size_t i = 0; i < p.size(); ++i) {
      const Pt a = p[i], b = p[(i + 1) % p.size()];
      const float c = (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x);
      const int s = c > 0 ? 1 : (c < 0 ? -1 : 0);
      if (s == 0) continue;
      if (sign == 0) sign = s;
      else if (s != sign) return false;
    }
    return true;
  }
  Box Bounds() const {
    Box b{1e9f, 1e9f, -1e9f, -1e9f};
    for (const Pt q : p) {
      b.l = std::min(b.l, q.x);
      b.t = std::min(b.t, q.y);
      b.r = std::max(b.r, q.x);
      b.b = std::max(b.b, q.y);
    }
    return b;
  }
};

// Thick segment from a to b: half-width hw, extended by `extA` / `extB` along its direction.
Convex Segment(Pt a, Pt b, float hw, float extA, float extB) {
  const float dx = b.x - a.x, dy = b.y - a.y;
  const float len = std::max(1e-4f, std::hypot(dx, dy));
  const float ux = dx / len, uy = dy / len, nx = -uy * hw, ny = ux * hw;
  const Pt s{a.x - ux * extA, a.y - uy * extA}, e{b.x + ux * extB, b.y + uy * extB};
  return Convex{{{s.x + nx, s.y + ny}, {e.x + nx, e.y + ny}, {e.x - nx, e.y - ny}, {s.x - nx, s.y - ny}}};
}

// ───────── crosshair layers ─────────

struct IRect {
  int l, t, r, b;
};

class LayerPainter {
 public:
  // `even`: the crosshair is centred on the pixel corner (cx, cy) instead of the pixel centre.
  // Every layer must share the same centre or mixed elements end up half a pixel apart.
  LayerPainter(Canvas& cv, const Layer& L, float scale, int cx, int cy, bool even)
      : cv_(cv), L_(L), s_(scale), even_(even) {
    cx_ = cx + Round(L.offsetX);
    cy_ = cy + Round(L.offsetY);
    pivot_ = even ? Pt{static_cast<float>(cx_), static_cast<float>(cy_)} : Pt{cx_ + 0.5f, cy_ + 0.5f};
    o_ = L.outline ? std::max(0, Round(L.outlineThickness)) : 0;
    fill_ = {L.fill.r, L.fill.g, L.fill.b, L.fill.a};
    line_ = {L.outlineColor.r, L.outlineColor.g, L.outlineColor.b, L.outlineColor.a * L.outlineOpacity};
  }

  void Paint() {
    switch (L_.shape) {
      case Shape::Cross: CrossShape(L_.rays, L_.rotation, false); break;
      case Shape::Plus: PlusShape(); break;
      case Shape::XCross: CrossShape(L_.rays, L_.rotation + 45.f, false); break;
      case Shape::TShape: CrossShape(kRayRight | kRayDown | kRayLeft, L_.rotation, false); break;
      case Shape::Dot: DotShape(); break;
      case Shape::Ring: RingShape(); break;
      case Shape::Box: BoxShape(); break;
      case Shape::Diamond: PolygonShape(4, 0.f, L_.size * s_); break;
      case Shape::Triangle: PolygonShape(3, -90.f, L_.size * s_); break;
      case Shape::Corners: CornersShape(); break;
      case Shape::Chevron: ChevronShape(); break;
      case Shape::Segments: SegmentsShape(); break;
      case Shape::Line: CrossShape(kRayLeft | kRayRight, L_.rotation, true); break;
      default: break;
    }
  }

 private:
  bool HasOutline() const { return o_ > 0 && line_.a > 0; }

  template <class Fn>
  void Solid(Box bounds, float rotation, Fn inside, const Rgba& c) {
    cv_.Fill(bounds, rotation, pivot_, inside, [&](float, float) { return c; });
  }

  void Rects(const std::vector<IRect>& rects, float rotation, bool round) {
    const auto pass = [&](float grow, const Rgba& c) {
      for (const IRect& r : rects) {
        const float l = r.l - grow, t = r.t - grow, rr = r.r + grow, b = r.b + grow;
        if (round) {
          const float rad = std::min(rr - l, b - t) * 0.5f;
          Solid({l, t, rr, b}, rotation, RoundRect{l, t, rr, b, rad}, c);
        } else {
          Solid({l, t, rr, b}, rotation, Rect{l, t, rr, b}, c);
        }
      }
    };
    if (HasOutline()) pass(static_cast<float>(o_), line_);
    pass(0, fill_);
  }

  // Rays around the center block. `fillCenterAtZeroGap` closes the center when gap is 0 (Line).
  void CrossShape(int rayMask, float rotation, bool fillCenterAtZeroGap) {
    const int t = Match(std::max(1, Round(L_.thickness * s_)));
    const int g = std::max(0, Round(L_.gap * s_));
    const int len = std::max(0, Round(L_.length * s_));
    // Vertical rays may have their own length (Valorant-style "separate vertical length").
    const int lenV = L_.lengthV > 0 ? std::max(0, Round(L_.lengthV * s_)) : len;
    if (len <= 0 && lenV <= 0) return;
    const bool aligned = AxisAligned(rotation);
    const int rays = aligned ? RotateRays(rayMask, Round(rotation / 90.f)) : rayMask;
    const int lo = cx_ - t / 2, hi = lo + t;
    const int loy = cy_ - t / 2, hiy = loy + t;
    std::vector<IRect> r;
    if ((rays & kRayUp) && lenV > 0) r.push_back({lo, loy - g - lenV, hi, loy - g});
    if ((rays & kRayRight) && len > 0) r.push_back({hi + g, loy, hi + g + len, hiy});
    if ((rays & kRayDown) && lenV > 0) r.push_back({lo, hiy + g, hi, hiy + g + lenV});
    if ((rays & kRayLeft) && len > 0) r.push_back({lo - g - len, loy, lo - g, hiy});
    if (fillCenterAtZeroGap && g == 0) r.push_back({lo, loy, hi, hiy});
    Rects(r, aligned ? 0.f : rotation, L_.round);
  }

  // Solid "+": one horizontal and one vertical bar through a filled center.
  void PlusShape() {
    const int t = Match(std::max(1, Round(L_.thickness * s_)));
    const int len = std::max(0, Round(L_.length * s_));
    const int lo = cx_ - t / 2, hi = lo + t;
    const int loy = cy_ - t / 2, hiy = loy + t;
    const std::vector<IRect> r = {{lo - len, loy, hi + len, hiy}, {lo, loy - len, hi, hiy + len}};
    Rects(r, AxisAligned(L_.rotation) ? 0.f : L_.rotation, L_.round);
  }

  // Regular polygon (diamond = 4 sides, triangle = 3) with circumradius `radius`.
  // Outline mode is the ring between the outer polygon and one inset by the stroke width.
  void PolygonShape(int sides, float startDeg, float radius) {
    radius = std::max(1.f, radius);
    const float t = std::max(0.5f, L_.thickness * s_);
    const float inradiusRatio = std::cos(kPi / sides);  // inradius / circumradius
    const auto poly = [&](float r) {
      Convex c;
      for (int i = 0; i < sides; ++i) {
        const float a = (startDeg + 360.f * i / sides) * kPi / 180.f;
        c.p.push_back({pivot_.x + r * std::cos(a), pivot_.y + r * std::sin(a)});
      }
      return c;
    };
    // Growing the inradius by d grows the circumradius by d / inradiusRatio.
    const auto grow = [&](float r, float d) { return r + d / inradiusRatio; };
    const auto pass = [&](float outerR, float innerR, const Rgba& c) {
      const Convex outer = poly(outerR);
      const Convex inner = poly(std::max(0.f, innerR));
      const bool hollow = innerR > 0.5f;
      Solid(outer.Bounds(), L_.rotation, [&](float x, float y) { return outer(x, y) && !(hollow && inner(x, y)); }, c);
    };
    const float o = static_cast<float>(o_);
    if (L_.filled) {
      if (HasOutline()) pass(grow(radius, o), 0, line_);
      pass(radius, 0, fill_);
    } else {
      if (HasOutline()) pass(grow(radius, o), grow(radius, -t - o), line_);
      pass(radius, grow(radius, -t), fill_);
    }
  }

  void DotShape() {
    const int d = std::max(1, Round(L_.size * s_));
    if (L_.round && d >= 3) {
      // Anti-aliased, so it can sit exactly on the shared centre whatever its parity.
      const float r = d * 0.5f, ccx = pivot_.x, ccy = pivot_.y;
      const float ro = r + o_;
      if (HasOutline()) Solid({ccx - ro, ccy - ro, ccx + ro, ccy + ro}, 0, Disc{ccx, ccy, ro}, line_);
      Solid({ccx - r, ccy - r, ccx + r, ccy + r}, 0, Disc{ccx, ccy, r}, fill_);
      return;
    }
    const int sq = Match(d);
    const int slo = cx_ - sq / 2, sloy = cy_ - sq / 2;
    Rects({{slo, sloy, slo + sq, sloy + sq}}, AxisAligned(L_.rotation) ? 0.f : L_.rotation, false);
  }

  // A pixel-snapped size whose parity differs from the shared centre cannot be symmetric;
  // grow it by one pixel instead of drawing it half a pixel off-centre.
  int Match(int px) const { return SymmetricPixels(px, even_); }

  void RingShape() {
    const float r = std::max(1.f, L_.size * s_);
    const float hw = std::max(0.25f, L_.thickness * s_ * 0.5f);
    const float ro = r + hw + o_;
    const Box bounds{pivot_.x - ro, pivot_.y - ro, pivot_.x + ro, pivot_.y + ro};
    if (HasOutline()) Solid(bounds, 0, Band{pivot_.x, pivot_.y, std::max(0.f, r - hw - o_), r + hw + o_}, line_);
    Solid(bounds, 0, Band{pivot_.x, pivot_.y, std::max(0.f, r - hw), r + hw}, fill_);
  }

  void BoxShape() {
    const int h = std::max(1, Round(L_.size * s_));
    const int t = std::max(1, Round(L_.thickness * s_));
    const int odd = even_ ? 0 : 1;  // odd width around a pixel centre, even around a corner
    const int x0 = cx_ - h, x1 = cx_ + h + odd, y0 = cy_ - h, y1 = cy_ + h + odd;
    std::vector<IRect> r;
    if (L_.filled || 2 * t >= x1 - x0) {
      r.push_back({x0, y0, x1, y1});
    } else {
      r.push_back({x0, y0, x1, y0 + t});
      r.push_back({x0, y1 - t, x1, y1});
      r.push_back({x0, y0 + t, x0 + t, y1 - t});
      r.push_back({x1 - t, y0 + t, x1, y1 - t});
    }
    Rects(r, AxisAligned(L_.rotation) ? 0.f : L_.rotation, false);
  }

  void CornersShape() {
    const int h = std::max(1, Round(L_.gap * s_));
    const int t = std::max(1, Round(L_.thickness * s_));
    const int a = std::clamp(Round(L_.length * s_), t, 2 * h + 1);
    const int odd = even_ ? 0 : 1;
    const int x0 = cx_ - h, x1 = cx_ + h + odd, y0 = cy_ - h, y1 = cy_ + h + odd;
    const std::vector<IRect> r = {
        {x0, y0, x0 + a, y0 + t}, {x0, y0 + t, x0 + t, y0 + a},   // top-left
        {x1 - a, y0, x1, y0 + t}, {x1 - t, y0 + t, x1, y0 + a},   // top-right
        {x0, y1 - t, x0 + a, y1}, {x0, y1 - a, x0 + t, y1 - t},   // bottom-left
        {x1 - a, y1 - t, x1, y1}, {x1 - t, y1 - a, x1, y1 - t}};  // bottom-right
    Rects(r, AxisAligned(L_.rotation) ? 0.f : L_.rotation, false);
  }

  void ChevronShape() {
    const float g = std::max(0.f, L_.gap * s_);
    const float a = std::max(1.f, L_.length * s_);
    const float hw = std::max(0.25f, L_.thickness * s_ * 0.5f);
    const float k = std::sin(kPi / 4);
    // Apex below the center, arms opening downwards.
    const Pt apex{pivot_.x, pivot_.y + g};
    const Pt left{apex.x - a * k, apex.y + a * k}, right{apex.x + a * k, apex.y + a * k};
    const auto arms = [&](float grow, const Rgba& c) {
      const Convex l = Segment(apex, left, hw + grow, hw + grow, grow);
      const Convex r = Segment(apex, right, hw + grow, hw + grow, grow);
      Box b = l.Bounds();
      const Box rb = r.Bounds();
      b = {std::min(b.l, rb.l), std::min(b.t, rb.t), std::max(b.r, rb.r), std::max(b.b, rb.b)};
      Solid(b, L_.rotation, [&](float x, float y) { return l(x, y) || r(x, y); }, c);
    };
    if (HasOutline()) arms(static_cast<float>(o_), line_);
    arms(0, fill_);
  }

  void SegmentsShape() {
    const float r = std::max(1.f, L_.size * s_);
    const float hw = std::max(0.25f, L_.thickness * s_ * 0.5f);
    const int n = std::max(1, L_.segments);
    const float step = 360.f / n;
    const float half = std::min(L_.segmentGap, step - 1.f) * 0.5f;
    const float extra = o_ > 0 ? (o_ / r) * 180.f / kPi : 0.f;
    const float ro = r + hw + o_;
    const Box bounds{pivot_.x - ro, pivot_.y - ro, pivot_.x + ro, pivot_.y + ro};
    for (int i = 0; i < n; ++i) {
      const float a0 = i * step + half, a1 = (i + 1) * step - half;
      if (a1 <= a0) continue;
      if (HasOutline()) {
        Solid(bounds, L_.rotation, Band{pivot_.x, pivot_.y, std::max(0.f, r - hw - o_), r + hw + o_, a0 - extra, a1 + extra},
              line_);
      }
      Solid(bounds, L_.rotation, Band{pivot_.x, pivot_.y, std::max(0.f, r - hw), r + hw, a0, a1}, fill_);
    }
  }

  Canvas& cv_;
  const Layer& L_;
  float s_;
  bool even_ = false;
  int cx_ = 0, cy_ = 0;
  Pt pivot_{};
  int o_ = 0;
  Rgba fill_, line_;
};

}  // namespace

int CrosshairExtent(const Crosshair& c) {
  float m = 2;
  const float s = c.scale;
  for (const auto& L : c.layers) {
    if (L.hidden) continue;
    float e = 0;
    bool rotated = !AxisAligned(L.rotation);
    switch (L.shape) {
      case Shape::Cross:
      case Shape::TShape:
      case Shape::Line: e = L.thickness * s * 0.5f + L.gap * s + std::max(L.length, L.lengthV) * s + 1; break;
      case Shape::XCross:
        e = L.thickness * s * 0.5f + L.gap * s + std::max(L.length, L.lengthV) * s + 1;
        rotated = true;
        break;
      case Shape::Plus: e = L.thickness * s * 0.5f + L.length * s + 1; break;
      case Shape::Diamond:
      case Shape::Triangle: e = L.size * s + 1; break;
      case Shape::Dot: e = L.size * s * 0.5f + 1; break;
      case Shape::Ring:
      case Shape::Segments: e = L.size * s + L.thickness * s * 0.5f + 1; break;
      case Shape::Box: e = L.size * s + 1; break;
      case Shape::Corners: e = L.gap * s + 1; break;
      case Shape::Chevron: e = L.gap * s + L.length * s + L.thickness * s + 1; break;
      default: break;
    }
    // Polygons grow their outline along the inradius, which costs up to 2× on the circumradius.
    const bool polygon = L.shape == Shape::Diamond || L.shape == Shape::Triangle;
    if (L.outline) e += L.outlineThickness * (polygon ? 2.f : 1.f);
    if (rotated || L.shape == Shape::Corners || L.shape == Shape::Box) e *= 1.4143f;
    e += std::sqrt(L.offsetX * L.offsetX + L.offsetY * L.offsetY);
    m = std::max(m, e);
  }
  return std::min(static_cast<int>(std::ceil(m)) + 2, 1000);
}

// The first pixel-snapped element decides where the shared centre lies: lines and square dots
// of even width are symmetric around a pixel corner, odd ones around a pixel centre.
// Anti-aliased shapes (rings, polygons, round dots, arcs) follow that centre.
bool CrosshairEvenCenter(const Crosshair& c) {
  for (const Layer& L : c.layers) {
    if (L.hidden) continue;
    switch (L.shape) {
      case Shape::Cross:
      case Shape::Plus:
      case Shape::XCross:
      case Shape::TShape:
      case Shape::Line: return std::max(1, Round(L.thickness * c.scale)) % 2 == 0;
      case Shape::Dot:
        if (L.round && Round(L.size * c.scale) >= 3) break;
        return std::max(1, Round(L.size * c.scale)) % 2 == 0;
      default: break;
    }
  }
  return false;
}

Image RasterizeCrosshair(const Crosshair& c) {
  const int size = CrosshairSurfaceSize(c);
  const bool even = CrosshairEvenCenter(c);
  Canvas out(size, size);
  Canvas layer(size, size);
  for (const Layer& L : c.layers) {
    if (L.hidden) continue;
    layer.Clear();
    LayerPainter(layer, L, c.scale, size / 2, size / 2, even).Paint();
    out.CompositeOver(layer, std::clamp(L.opacity, 0.f, 1.f));
  }
  return out.ToImage(std::clamp(c.opacity, 0.f, 1.f));
}

// Icon variant of the mark: light monogram S with a dark centre dot on a rounded accent tile.
// Same geometry as DrawLogo (48-unit grid); strokes are thickened at tray sizes to stay legible.
Image RasterizeLogo(int size) {
  Canvas cv(size, size);
  const auto hex = [](unsigned v) {
    return Rgba{((v >> 16) & 0xFF) / 255.f, ((v >> 8) & 0xFF) / 255.f, (v & 0xFF) / 255.f, 1.f};
  };
  const Rgba tile = hex(0xC65A32), ink = hex(0xFBF8F1), dot = hex(0x17181A);
  const float n = static_cast<float>(size);
  const auto solid = [&](const Rgba& c) { return [c](float, float) { return c; }; };

  // Rounded tile.
  const float rad = n * 0.22f;
  cv.Fill({0, 0, n, n}, 0, {0, 0}, RoundRect{0, 0, n, n, rad}, solid(tile));

  // Glyph units 10..38 mapped onto ~72% of the tile.
  const float s = n * 0.72f / 28.f;
  const float o = (n - 28.f * s) * 0.5f - 10.f * s;
  const auto U = [&](float u) { return o + u * s; };
  const float hw = std::max(3.2f * s, size <= 20 ? 1.7f : 2.f) * 0.5f;
  const auto capsule = [&](float x0, float y0, float x1, float y1) {
    return [=](float x, float y) {
      const float dx = x1 - x0, dy = y1 - y0;
      const float t = std::clamp(((x - x0) * dx + (y - y0) * dy) / (dx * dx + dy * dy), 0.f, 1.f);
      const float ex = x - (x0 + t * dx), ey = y - (y0 + t * dy);
      return ex * ex + ey * ey <= hw * hw;
    };
  };
  const auto barTop = capsule(U(22), U(13), U(33), U(13));
  const auto barBottom = capsule(U(15), U(35), U(26), U(35));
  const Band arcLeft{U(22), U(20), 7 * s - hw, 7 * s + hw, 180.f, 360.f};
  const Band arcRight{U(26), U(28), 7 * s - hw, 7 * s + hw, 0.f, 180.f};
  cv.Fill({0, 0, n, n}, 0, {0, 0},
          [&](float x, float y) { return barTop(x, y) || barBottom(x, y) || arcLeft(x, y) || arcRight(x, y); },
          solid(ink));
  const float dr = std::max(3.f * s, size <= 20 ? 1.6f : 2.f);
  cv.Fill({0, 0, n, n}, 0, {0, 0}, Disc{U(24), U(24), dr}, solid(dot));
  return cv.ToImage(1.f);
}

}  // namespace sl
