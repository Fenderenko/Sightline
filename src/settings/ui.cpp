#include "settings/ui.h"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <cwctype>

namespace sl::ui {

namespace {

constexpr D2D1_COLOR_F Hex(uint32_t rgb, float a = 1.f) {
  return {((rgb >> 16) & 0xFF) / 255.f, ((rgb >> 8) & 0xFF) / 255.f, (rgb & 0xFF) / 255.f, a};
}

// Flat warm paper: solid fills only.
const Theme kLight = [] {
  Theme t;
  t.dark = false;
  t.bg = Hex(0xECE8E0);
  t.panel = Hex(0xE4DFD5);
  t.raised = Hex(0xF6F3EE);
  t.sel = Hex(0x17181A, 0.05f);
  t.field = Hex(0x17181A, 0.055f);
  t.track = Hex(0x17181A, 0.14f);
  t.ink = Hex(0x17181A);
  t.ink2 = Hex(0x4B4A45);
  t.ink3 = Hex(0x7C786F);
  t.line = Hex(0x17181A, 0.10f);
  t.tile = Hex(0x26292C);
  t.tileInk = Hex(0xFFFFFF, 0.45f);
  t.acc = Hex(0xC65A32);
  t.onAcc = Hex(0xFBF8F1);
  return t;
}();

// Flat "wet asphalt".
const Theme kDark = [] {
  Theme t;
  t.dark = true;
  t.bg = Hex(0x2B2E31);
  t.panel = Hex(0x25282A);
  t.raised = Hex(0x33373A);
  t.sel = Hex(0xE9E7E1, 0.05f);
  t.field = Hex(0x000000, 0.18f);
  t.track = Hex(0xE9E7E1, 0.16f);
  t.ink = Hex(0xE9E7E1);
  t.ink2 = Hex(0xB0B3B4);
  t.ink3 = Hex(0x868B8E);
  t.line = Hex(0xE9E7E1, 0.08f);
  t.tile = Hex(0x1E2022);
  t.tileInk = Hex(0xFFFFFF, 0.42f);
  t.acc = Hex(0xE07A4F);
  t.onAcc = Hex(0x1B1D1F);
  return t;
}();

struct FontSpec {
  int family;  // 0 UI, 1 display, 2 mono, 3 icons
  DWRITE_FONT_WEIGHT weight;
  float size;
};

constexpr FontSpec kFonts[] = {
    {2, DWRITE_FONT_WEIGHT_NORMAL, 9.5f},      // Label
    {2, DWRITE_FONT_WEIGHT_NORMAL, 11.f},      // Mono
    {0, DWRITE_FONT_WEIGHT_NORMAL, 13.f},      // Body
    {0, DWRITE_FONT_WEIGHT_SEMI_BOLD, 13.f},   // BodyBold
    {0, DWRITE_FONT_WEIGHT_NORMAL, 12.f},      // Small
    {0, DWRITE_FONT_WEIGHT_NORMAL, 11.f},      // Tiny
    {0, DWRITE_FONT_WEIGHT_SEMI_BOLD, 12.5f},  // Button
    {1, DWRITE_FONT_WEIGHT_SEMI_BOLD, 26.f},   // Title
    {1, DWRITE_FONT_WEIGHT_SEMI_BOLD, 20.f},   // H2
    {1, DWRITE_FONT_WEIGHT_SEMI_BOLD, 14.f},   // Name
    {1, DWRITE_FONT_WEIGHT_BOLD, 15.f},        // Wordmark
    {3, DWRITE_FONT_WEIGHT_NORMAL, 12.f},      // Icon
    {3, DWRITE_FONT_WEIGHT_NORMAL, 16.f},      // IconLg
};
static_assert(std::size(kFonts) == static_cast<size_t>(Font::Count));

constexpr float kLabelSpacing = 0.8f;

bool Inside(D2D1_RECT_F r, D2D1_POINT_2F p) { return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom; }

std::wstring PickFamily(IDWriteFactory* dw, std::initializer_list<const wchar_t*> names) {
  ComPtr<IDWriteFontCollection> coll;
  dw->GetSystemFontCollection(&coll);
  for (const wchar_t* n : names) {
    UINT32 index = 0;
    BOOL exists = FALSE;
    if (coll && SUCCEEDED(coll->FindFamilyName(n, &index, &exists)) && exists) return n;
  }
  return *(names.end() - 1);
}

// Composites `over` on top of an opaque `base`.
D2D1_COLOR_F Over(D2D1_COLOR_F base, D2D1_COLOR_F over) {
  const float a = over.a;
  return {base.r + (over.r - base.r) * a, base.g + (over.g - base.g) * a, base.b + (over.b - base.b) * a, 1.f};
}

}  // namespace

const Theme& LightTheme() { return kLight; }
const Theme& DarkTheme() { return kDark; }

Id MakeId(std::string_view s, int index) {
  uint64_t h = 1469598103934665603ull;
  for (const char c : s) {
    h ^= static_cast<unsigned char>(c);
    h *= 1099511628211ull;
  }
  h ^= static_cast<uint64_t>(index) * 0x9E3779B97F4A7C15ull;
  return h ? h : 1;
}

bool Context::Init(ID2D1DeviceContext* dc, IDWriteFactory1* dw, HWND hwnd) {
  dc_ = dc;
  dw_ = dw;
  hwnd_ = hwnd;
  if (!th_) th_ = &kLight;
  solid_.Reset();
  ID2D1RenderTarget* rt = dc_;
  rt->CreateSolidColorBrush(th_->ink, &solid_);

  const std::wstring families[] = {
      PickFamily(dw_, {L"Segoe UI Variable Text", L"Segoe UI"}),
      PickFamily(dw_, {L"Bahnschrift", L"Segoe UI"}),
      PickFamily(dw_, {L"Cascadia Mono", L"Consolas"}),
      PickFamily(dw_, {L"Segoe Fluent Icons", L"Segoe MDL2 Assets"}),
  };
  for (int i = 0; i < static_cast<int>(Font::Count); ++i) {
    formats_[i].Reset();
    dw_->CreateTextFormat(families[kFonts[i].family].c_str(), nullptr, kFonts[i].weight, DWRITE_FONT_STYLE_NORMAL,
                          DWRITE_FONT_STRETCH_NORMAL, kFonts[i].size, L"uk-UA", &formats_[i]);
  }
  return solid_ && formats_[0];
}

void Context::Begin(const Input& in, D2D1_SIZE_F size, float scale) {
  in_ = &in;
  size_ = size;
  scale_ = std::max(0.5f, scale);
  cursor = Cursor::Arrow;
  wantRedraw = false;
  focusClaimed_ = false;
  overlay_ = false;
  clips_.clear();
  prevWheelZones_.swap(wheelZones_);
  wheelZones_.clear();
  // A value entry whose slider was not drawn last frame (page change, collapsed section) is dropped.
  if (!valueEditSeen_) valueEdit_ = 0;
  valueEditSeen_ = false;
  // Rect fills are snapped to device pixels; aliased fills keep them crisp. Rounded shapes switch to AA per call.
  dc_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
}

void Context::End() {
  if (in_->pressed && !focusClaimed_) focus_ = 0;
  if (in_->released || !in_->down) active_ = 0;
  while (!clips_.empty()) PopClip();
}

float Context::Snap(float v) const { return std::round(v * scale_) / scale_; }

ID2D1SolidColorBrush* Context::Solid(D2D1_COLOR_F c) {
  solid_->SetColor(c);
  return solid_.Get();
}

void Context::Fill(D2D1_RECT_F r, D2D1_COLOR_F c) {
  if (c.a <= 0) return;
  dc_->FillRectangle(Rc(Snap(r.left), Snap(r.top), Snap(r.right), Snap(r.bottom)), Solid(c));
}

void Context::FillRound(D2D1_RECT_F r, float radius, D2D1_COLOR_F c) {
  if (c.a <= 0) return;
  const D2D1_RECT_F s = Rc(Snap(r.left), Snap(r.top), Snap(r.right), Snap(r.bottom));
  radius = std::min({radius, (s.right - s.left) / 2, (s.bottom - s.top) / 2});
  dc_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
  dc_->FillRoundedRectangle(D2D1::RoundedRect(s, radius, radius), Solid(c));
  dc_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
}

void Context::StrokeRound(D2D1_RECT_F r, float radius, D2D1_COLOR_F c, float width) {
  if (c.a <= 0) return;
  const float h = width / 2;
  const D2D1_RECT_F s = Rc(Snap(r.left) + h, Snap(r.top) + h, Snap(r.right) - h, Snap(r.bottom) - h);
  radius = std::max(0.f, std::min({radius - h, (s.right - s.left) / 2, (s.bottom - s.top) / 2}));
  dc_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
  dc_->DrawRoundedRectangle(D2D1::RoundedRect(s, radius, radius), Solid(c), width);
  dc_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
}

void Context::Circle(D2D1_POINT_2F center, float radius, D2D1_COLOR_F c) {
  if (c.a <= 0) return;
  dc_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
  dc_->FillEllipse(D2D1::Ellipse(center, radius, radius), Solid(c));
  dc_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
}

void Context::HLine(float x0, float x1, float y, D2D1_COLOR_F c) {
  const float sy = Snap(y);
  dc_->FillRectangle(Rc(Snap(x0), sy, Snap(x1), sy + px()), Solid(c));
}

void Context::VLine(float x, float y0, float y1, D2D1_COLOR_F c) {
  const float sx = Snap(x);
  dc_->FillRectangle(Rc(sx, Snap(y0), sx + px(), Snap(y1)), Solid(c));
}

void Context::Stroke(D2D1_RECT_F r, D2D1_COLOR_F c) {
  const float l = Snap(r.left), t = Snap(r.top), rr = Snap(r.right), b = Snap(r.bottom), p = px();
  auto* br = Solid(c);
  dc_->FillRectangle(Rc(l, t, rr, t + p), br);
  dc_->FillRectangle(Rc(l, b - p, rr, b), br);
  dc_->FillRectangle(Rc(l, t + p, l + p, b - p), br);
  dc_->FillRectangle(Rc(rr - p, t + p, rr, b - p), br);
}

IDWriteTextFormat* Context::Format(Font f) { return formats_[static_cast<int>(f)].Get(); }

ComPtr<IDWriteTextLayout> Context::Layout(std::wstring_view t, Font f, float w, float h, Align a, float spacing, bool wrap) {
  ComPtr<IDWriteTextLayout> layout;
  if (FAILED(dw_->CreateTextLayout(t.data(), static_cast<UINT32>(t.size()), Format(f), std::max(w, 1.f), std::max(h, 1.f),
                                   &layout))) {
    return nullptr;
  }
  layout->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
  layout->SetTextAlignment(a == Align::Center   ? DWRITE_TEXT_ALIGNMENT_CENTER
                           : a == Align::Right ? DWRITE_TEXT_ALIGNMENT_TRAILING
                                               : DWRITE_TEXT_ALIGNMENT_LEADING);
  layout->SetParagraphAlignment(wrap ? DWRITE_PARAGRAPH_ALIGNMENT_NEAR : DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
  if (!wrap) {
    // Ellipsis instead of overflowing the box.
    ComPtr<IDWriteInlineObject> ellipsis;
    if (SUCCEEDED(dw_->CreateEllipsisTrimmingSign(Format(f), &ellipsis))) {
      const DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
      layout->SetTrimming(&trim, ellipsis.Get());
    }
  }
  if (spacing != 0) {
    ComPtr<IDWriteTextLayout1> l1;
    if (SUCCEEDED(layout.As(&l1))) l1->SetCharacterSpacing(0, spacing, 0, {0, static_cast<UINT32>(t.size())});
  }
  return layout;
}

void Context::Text(std::wstring_view t, D2D1_RECT_F r, Font f, D2D1_COLOR_F c, Align a, float spacing) {
  if (t.empty() || r.right - r.left < 1) return;
  if (auto layout = Layout(t, f, r.right - r.left, r.bottom - r.top, a, spacing, false)) {
    dc_->DrawTextLayout(D2D1::Point2F(r.left, r.top), layout.Get(), Solid(c), D2D1_DRAW_TEXT_OPTIONS_CLIP);
  }
}

void Context::Label(std::wstring_view t, D2D1_RECT_F r, D2D1_COLOR_F c, Align a) {
  Text(Upper(t), r, Font::Label, c, a, kLabelSpacing);
}

void Context::Glyph(wchar_t g, D2D1_RECT_F r, D2D1_COLOR_F c, bool large) {
  const wchar_t s[2] = {g, 0};
  Text(std::wstring_view(s, 1), r, large ? Font::IconLg : Font::Icon, c, Align::Center);
}

float Context::Paragraph(std::wstring_view t, D2D1_RECT_F r, Font f, D2D1_COLOR_F c, bool draw) {
  auto layout = Layout(t, f, r.right - r.left, 4000, Align::Left, 0, true);
  if (!layout) return 0;
  layout->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_PROPORTIONAL, 1.2f, 1.0f);
  if (draw) dc_->DrawTextLayout(D2D1::Point2F(r.left, r.top), layout.Get(), Solid(c), D2D1_DRAW_TEXT_OPTIONS_NONE);
  DWRITE_TEXT_METRICS m{};
  layout->GetMetrics(&m);
  return m.height;
}

float Context::Measure(std::wstring_view t, Font f, float spacing) {
  if (t.empty()) return 0;
  auto layout = Layout(t, f, 4000, 100, Align::Left, spacing, false);
  if (!layout) return 0;
  DWRITE_TEXT_METRICS m{};
  layout->GetMetrics(&m);
  return m.widthIncludingTrailingWhitespace;
}

float Context::MeasureLabel(std::wstring_view t) { return Measure(Upper(t), Font::Label, kLabelSpacing); }

void Context::Background() { dc_->Clear(th_->bg); }

float Context::Section(float x0, float x1, float y, std::wstring_view left, std::wstring_view right) {
  Label(left, Rc(x0, y, x1, y + 16), th_->ink3);
  if (!right.empty()) Label(right, Rc(x0, y, x1, y + 16), th_->ink3, Align::Right);
  return y + 24;
}

float Context::Keycaps(std::wstring_view text, float right, float cy) {
  std::vector<std::wstring> parts;
  size_t start = 0;
  while (start <= text.size()) {
    size_t p = text.find(L'+', start);
    // A literal "+" key ends with "++"; keep it as its own cap.
    if (p == start && p != std::wstring_view::npos) p = text.find(L'+', start + 1);
    if (p == std::wstring_view::npos) p = text.size();
    std::wstring part(text.substr(start, p - start));
    while (!part.empty() && part.back() == L' ') part.pop_back();
    while (!part.empty() && part.front() == L' ') part.erase(part.begin());
    if (!part.empty()) parts.push_back(part);
    start = p + 1;
  }
  const float gap = 4, h = 22;
  float x = right;
  for (size_t i = parts.size(); i-- > 0;) {
    const float w = std::max(24.f, std::ceil(Measure(parts[i], Font::Mono)) + 14);
    const D2D1_RECT_F cap = Rc(x - w, cy - h / 2, x, cy + h / 2);
    FillRound(cap, 4, th_->raised);
    Text(parts[i], cap, Font::Mono, th_->ink, Align::Center);
    x -= w + (i ? gap : 0);
  }
  return x;
}

bool Context::Hover(D2D1_RECT_F r) const {
  if (modal_ && !overlay_) return false;
  if (!Inside(r, in_->mouse)) return false;
  return clips_.empty() || Inside(clips_.back(), in_->mouse);
}

bool Context::DragArea(Id id, D2D1_RECT_F r) {
  const bool inside = Hover(r);
  if (inside) cursor = Cursor::Hand;
  if (inside && in_->pressed) active_ = id;
  const bool dragging = active_ == id && (in_->down || in_->pressed);
  if (dragging) wantRedraw = true;
  return dragging;
}

int Context::WheelSteps(D2D1_RECT_F r) {
  D2D1_RECT_F z = r;
  if (!clips_.empty()) {
    const D2D1_RECT_F& c = clips_.back();
    z = Rc(std::max(z.left, c.left), std::max(z.top, c.top), std::min(z.right, c.right), std::min(z.bottom, c.bottom));
  }
  if (z.right > z.left && z.bottom > z.top) wheelZones_.push_back(z);
  if (!Hover(r) || in_->wheel == 0) return 0;
  const float notches = std::round(in_->wheel);
  wantRedraw = true;
  return static_cast<int>(notches != 0 ? notches : (in_->wheel > 0 ? 1.f : -1.f));
}

bool Context::InWheelZone(D2D1_POINT_2F p) const {
  for (const D2D1_RECT_F& z : prevWheelZones_) {
    if (Inside(z, p)) return true;
  }
  return false;
}

void Context::Popover(D2D1_RECT_F r) {
  // Three soft shadow steps instead of a blur effect.
  const float a = th_->dark ? 0.14f : 0.035f;
  for (int i = 3; i >= 1; --i) {
    const float g = static_cast<float>(i) * 1.5f;
    FillRound(Rc(r.left - g, r.top - g + 3, r.right + g, r.bottom + g + 3), kRadiusCard + g, D2D1::ColorF(0, 0, 0, a));
  }
  FillRound(r, kRadiusCard + 2, th_->raised);
  StrokeRound(r, kRadiusCard + 2, th_->dark ? D2D1::ColorF(1, 1, 1, 0.09f) : D2D1::ColorF(0, 0, 0, 0.10f));
}

bool Context::ClickArea(Id id, D2D1_RECT_F r) {
  const bool inside = Hover(r);
  if (inside) cursor = Cursor::Hand;
  if (inside && in_->pressed) active_ = id;
  const bool clicked = active_ == id && in_->released && inside;
  if (clicked) wantRedraw = true;
  return clicked;
}

bool Context::Button(Id id, D2D1_RECT_F r, std::wstring_view label, Btn style, bool enabled, wchar_t glyph) {
  const bool hover = enabled && Hover(r);
  const bool clicked = enabled && ClickArea(id, r);
  D2D1_COLOR_F text = th_->ink;
  switch (style) {
    case Btn::Primary:
      if (enabled) {
        FillRound(r, kRadiusCtl, th_->acc);
        if (hover) FillRound(r, kRadiusCtl, D2D1::ColorF(1, 1, 1, th_->dark ? 0.12f : 0.10f));
        text = th_->onAcc;
      } else {
        FillRound(r, kRadiusCtl, th_->field);
        text = th_->ink3;
      }
      break;
    case Btn::Secondary:
    case Btn::Small:
      FillRound(r, kRadiusCtl, th_->field);
      if (hover) FillRound(r, kRadiusCtl, th_->sel);
      if (!enabled) text = th_->ink3;
      break;
    case Btn::Ghost:
      if (hover) FillRound(r, kRadiusCtl, th_->field);
      text = enabled ? (hover ? th_->ink : th_->ink2) : WithAlpha(th_->ink3, 0.6f);
      break;
  }
  const Font f = style == Btn::Small || style == Btn::Ghost ? Font::Small : Font::Button;
  const float tw = label.empty() ? 0.f : Measure(label, f);
  const float gw = glyph ? 12.f + (label.empty() ? 0.f : 8.f) : 0.f;
  const float total = std::min(tw + gw, r.right - r.left - 8);
  const float x = std::round((r.left + r.right - total) * 0.5f);
  if (glyph) Glyph(glyph, Rc(x, r.top, x + 12, r.bottom), text);
  if (!label.empty()) Text(label, Rc(x + gw, r.top, std::min(r.right - 4, x + gw + tw + 2), r.bottom), f, text);
  return clicked;
}

bool Context::Chip(Id id, D2D1_RECT_F r, std::wstring_view label, bool selected) {
  const bool hover = Hover(r);
  const bool clicked = ClickArea(id, r);
  if (selected) FillRound(r, kRadiusCtl, th_->ink);
  else FillRound(r, kRadiusCtl, hover ? Over(th_->bg, th_->track) : th_->field);
  Text(label, Rc(r.left + 4, r.top, r.right - 4, r.bottom), Font::Small, selected ? th_->bg : (hover ? th_->ink : th_->ink2),
       Align::Center);
  return clicked;
}

void Context::SwitchVisual(D2D1_RECT_F t, bool on, bool hover) {
  const float h = t.bottom - t.top, r = h / 2, knob = r - 3;
  if (on) {
    FillRound(t, r, th_->acc);
    if (hover) FillRound(t, r, D2D1::ColorF(1, 1, 1, 0.10f));
    Circle({t.right - r, t.top + r}, knob, th_->onAcc);
  } else {
    FillRound(t, r, th_->track);
    if (hover) FillRound(t, r, th_->sel);
    Circle({t.left + r, t.top + r}, knob, th_->dark ? th_->ink2 : th_->raised);
  }
}

bool Context::Switch(Id id, D2D1_RECT_F t, bool* value) {
  const bool hover = Hover(t);
  const bool clicked = ClickArea(id, t);
  if (clicked) *value = !*value;
  SwitchVisual(t, *value, hover);
  return clicked;
}

bool Context::Toggle(Id id, D2D1_RECT_F r, std::wstring_view label, bool* value, std::wstring_view hint) {
  const bool hover = Hover(r);
  const bool clicked = ClickArea(id, r);
  if (clicked) *value = !*value;
  if (hover) FillRound(r, kRadiusCtl, th_->sel);
  const float mid = std::round((r.top + r.bottom) * 0.5f);
  const D2D1_RECT_F track = Rc(r.right - 16 - 36, mid - 10, r.right - 16, mid + 10);
  const float textRight = track.left - 24;
  if (hint.empty()) {
    Text(label, Rc(r.left + 16, r.top, textRight, r.bottom), Font::Body, th_->ink);
  } else {
    Text(label, Rc(r.left + 16, mid - 19, textRight, mid + 1), Font::Body, th_->ink);
    Text(hint, Rc(r.left + 16, mid + 1, textRight, mid + 18), Font::Small, th_->ink3);
  }
  SwitchVisual(track, *value, hover);
  return clicked;
}

bool Context::Slider(Id id, D2D1_RECT_F r, std::wstring_view label, float* value, float mn, float mx, float step,
                     int decimals, std::wstring_view unit, bool centered, float labelW, std::wstring_view note,
                     bool enabled) {
  const bool inline_ = labelW > 0;
  wchar_t buf[48];
  swprintf_s(buf, L"%.*f%.*s", decimals, *value, static_cast<int>(unit.size()), unit.data());
  const float noteW = note.empty() ? 0.f : std::ceil(Measure(note, Font::Small)) + 5;
  const float valueW = inline_ ? std::max(52.f, std::ceil(Measure(buf, Font::Mono)) + noteW + 2)
                               : Measure(buf, Font::Mono) + noteW + 4;
  // Rail geometry.
  float tx0, tx1, ty;
  if (inline_) {
    tx0 = r.left + labelW + 6;
    tx1 = r.right - valueW - 8;
    ty = std::round((r.top + r.bottom) * 0.5f);
  } else {
    tx0 = r.left + 6;
    tx1 = r.right - 6;
    ty = r.bottom - 8;
  }
  const D2D1_RECT_F hit = inline_ ? Rc(tx0 - 8, r.top, tx1 + 8, r.bottom) : Rc(r.left, r.top + 16, r.right, r.bottom);
  const D2D1_RECT_F valueR = inline_ ? Rc(r.right - valueW, r.top, r.right, r.bottom)
                                     : Rc(r.right - valueW - 6, r.top, r.right, r.top + 16);
  const bool hover = enabled && Hover(hit);
  if (hover) cursor = Cursor::Hand;
  bool changed = false;
  const auto setValue = [&](float v) {
    v = std::clamp(v, mn, mx);
    if (std::fabs(v - *value) > 1e-5f) {
      *value = v;
      changed = true;
      wantRedraw = true;
    }
  };

  // Wheel: anywhere over the row (label, rail or value), one step per notch. The row is registered
  // so the enclosing viewport does not scroll under it next frame.
  if (enabled) {
    D2D1_RECT_F z = r;
    if (!clips_.empty()) {
      const D2D1_RECT_F& c = clips_.back();
      z = Rc(std::max(z.left, c.left), std::max(z.top, c.top), std::min(z.right, c.right), std::min(z.bottom, c.bottom));
    }
    if (z.right > z.left && z.bottom > z.top) wheelZones_.push_back(z);
    if (Hover(r) && in_->wheel != 0 && step > 0) {
      const float notches = std::round(in_->wheel);
      const float n = notches != 0 ? notches : (in_->wheel > 0 ? 1.f : -1.f);
      setValue(mn + std::round((*value + n * step - mn) / step) * step);
    }
  }

  // Inline numeric entry over the value.
  const Id fieldId = id ^ 0x5DEECE66Dull;
  if (!enabled) {
    if (valueEdit_ == id) valueEdit_ = 0;
  } else if (valueEdit_ == id) {
    valueEditSeen_ = true;
    const float mid = std::round((valueR.top + valueR.bottom) * 0.5f);
    const D2D1_RECT_F fr = inline_ ? Rc(r.right - 56, mid - 12, r.right + 4, mid + 12) : Rc(valueR.left - 8, r.top - 4, r.right + 4, r.top + 20);
    const bool escape = focus_ == fieldId && in_->Key(VK_ESCAPE);
    bool submitted = false;
    TextField(fieldId, fr, &valueText_, {}, 10, &submitted, true);
    valueText_.erase(std::remove_if(valueText_.begin(), valueText_.end(),
                                    [](wchar_t c) { return !(iswdigit(c) || c == L'.' || c == L',' || c == L'-'); }),
                     valueText_.end());
    if (escape) {
      valueEdit_ = 0;
    } else if (submitted || focus_ != fieldId) {
      std::wstring s = valueText_;
      std::replace(s.begin(), s.end(), L',', L'.');
      wchar_t* end = nullptr;
      const float v = std::wcstof(s.c_str(), &end);
      if (end && end != s.c_str()) {
        const float p = std::pow(10.f, static_cast<float>(decimals));
        setValue(std::round(v * p) / p);
      }
      valueEdit_ = 0;
    }
  } else {
    if (Hover(valueR)) cursor = Cursor::IBeam;
    if (ClickArea(id ^ 0xB16B00B5ull, valueR)) {
      valueEdit_ = id;
      valueEditSeen_ = true;
      swprintf_s(buf, L"%.*f", decimals, *value);
      valueText_ = buf;
      SetFocus(fieldId);
    }
  }
  const bool editing = valueEdit_ == id;

  if (hover && in_->pressed) active_ = id;
  const bool dragging = enabled && active_ == id && (in_->down || in_->pressed);
  if (dragging) {
    float t = (in_->mouse.x - tx0) / std::max(1.f, tx1 - tx0);
    t = std::clamp(t, 0.f, 1.f);
    float v = mn + t * (mx - mn);
    if (step > 0) v = mn + std::round((v - mn) / step) * step;
    setValue(v);
  }
  // Double-click returns a centred slider to zero.
  if (centered && hover && in_->doubleClick && *value != 0) setValue(0);
  swprintf_s(buf, L"%.*f%.*s", decimals, *value, static_cast<int>(unit.size()), unit.data());
  const D2D1_COLOR_F labelInk = !enabled ? th_->ink3 : (hover || dragging ? th_->ink : th_->ink2);
  const D2D1_COLOR_F valueInk = enabled ? th_->ink : th_->ink3;
  const bool valueHover = enabled && !editing && Hover(valueR);
  // Value right-aligned, the optional note after it.
  const auto drawValue = [&](D2D1_RECT_F vr) {
    if (noteW > 0) {
      Text(buf, Rc(vr.left, vr.top, vr.right - noteW, vr.bottom), Font::Mono, valueInk, Align::Right);
      Text(note, Rc(vr.right - noteW + 4, vr.top, vr.right + 2, vr.bottom), Font::Small, th_->ink3, Align::Right);
    } else {
      Text(buf, vr, Font::Mono, valueInk, Align::Right);
    }
  };
  if (inline_) {
    Text(label, Rc(r.left, r.top, r.left + labelW, r.bottom), Font::Small, labelInk);
    if (!editing) {
      if (valueHover) {
        const float mid = std::round((r.top + r.bottom) * 0.5f);
        FillRound(Rc(valueR.left - 4, mid - 12, r.right + 4, mid + 12), kRadiusCtl, th_->field);
      }
      drawValue(valueR);
    }
  } else {
    Text(label, Rc(r.left, r.top, r.right - valueW - 6, r.top + 16), Font::Small, labelInk);
    if (!editing) {
      if (valueHover) FillRound(Rc(valueR.left - 8, r.top - 4, r.right + 4, r.top + 20), kRadiusCtl, th_->field);
      drawValue(valueR);
    }
  }
  const float t = std::clamp((*value - mn) / std::max(1e-6f, mx - mn), 0.f, 1.f);
  const float kx = tx0 + t * (tx1 - tx0);
  const D2D1_COLOR_F fillInk = enabled ? th_->acc : WithAlpha(th_->ink3, 0.55f);
  FillRound(Rc(tx0 - 2, ty - 1.5f, tx1 + 2, ty + 1.5f), 1.5f, th_->track);
  if (centered) {
    const float cx = (tx0 + tx1) * 0.5f;
    FillRound(Rc(cx - 1, ty - 4, cx + 1, ty + 4), 1, th_->track);
    if (std::fabs(kx - cx) > 0.5f) FillRound(Rc(std::min(cx, kx), ty - 1.5f, std::max(cx, kx), ty + 1.5f), 1.5f, fillInk);
  } else if (kx > tx0) {
    FillRound(Rc(tx0 - 2, ty - 1.5f, kx, ty + 1.5f), 1.5f, fillInk);
  }
  if (!enabled) {
    Circle({kx, ty}, 4.5f, Over(th_->bg, WithAlpha(th_->ink3, 0.8f)));
    return false;
  }
  const float kr = dragging ? 7.f : (hover ? 6.5f : 6.f);
  Circle({kx, ty}, kr, th_->dark ? th_->ink : th_->raised);
  if (!th_->dark) {
    // Thin ring keeps the light knob visible on paper.
    dc_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    dc_->DrawEllipse(D2D1::Ellipse({kx, ty}, kr - 0.5f, kr - 0.5f), Solid(WithAlpha(th_->ink, 0.28f)), 1.f);
    dc_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
  }
  return changed;
}

bool Context::Swatch(Id id, D2D1_RECT_F r, D2D1_COLOR_F c, bool selected) {
  const bool hover = Hover(Rc(r.left - 2, r.top - 2, r.right + 2, r.bottom + 2));
  const bool clicked = ClickArea(id, Rc(r.left - 2, r.top - 2, r.right + 2, r.bottom + 2));
  const D2D1_RECT_F ring = Rc(r.left - 3, r.top - 3, r.right + 3, r.bottom + 3);
  if (selected) StrokeRound(ring, 7, th_->ink, 1.5f);
  else if (hover) StrokeRound(ring, 7, th_->ink3, 1.f);
  FillRound(r, 5, c);
  StrokeRound(r, 5, th_->dark ? D2D1::ColorF(1, 1, 1, 0.14f) : D2D1::ColorF(0, 0, 0, 0.14f));
  return clicked;
}

bool Context::TextField(Id id, D2D1_RECT_F r, std::wstring* text, std::wstring_view placeholder, size_t maxLen,
                        bool* submitted, bool mono, wchar_t glyph, float padRight) {
  const bool hover = Hover(r);
  if (hover) cursor = Cursor::IBeam;
  if (hover && in_->pressed) {
    focus_ = id;
    focusClaimed_ = true;
    wantRedraw = true;
  }
  bool changed = false;
  if (focus_ == id) {
    for (const wchar_t ch : in_->chars) {
      if (ch >= 32 && ch != 127 && text->size() < maxLen) {
        text->push_back(ch);
        changed = true;
      }
    }
    for (const UINT k : in_->keys) {
      if (k == VK_BACK && !text->empty()) {
        text->pop_back();
        changed = true;
      } else if (k == VK_RETURN) {
        if (submitted) *submitted = true;
        focus_ = 0;
      } else if (k == VK_ESCAPE) {
        focus_ = 0;
      } else if (k == 'V' && in_->ctrl) {
        std::wstring clip = GetClipboardText(hwnd_);
        clip.erase(std::remove_if(clip.begin(), clip.end(), [](wchar_t c) { return c < 32; }), clip.end());
        const size_t room = maxLen > text->size() ? maxLen - text->size() : 0;
        text->append(clip.substr(0, room));
        changed = !clip.empty();
      } else if (k == 'A' && in_->ctrl) {
        text->clear();
        changed = true;
      }
    }
  }
  if (changed) wantRedraw = true;
  const bool focused = focus_ == id;
  FillRound(r, kRadiusCtl, th_->field);
  if (hover && !focused) FillRound(r, kRadiusCtl, th_->sel);
  if (focused) StrokeRound(r, kRadiusCtl, th_->acc, 1.5f);
  const Font f = mono ? Font::Mono : Font::Body;
  if (glyph) Glyph(glyph, Rc(r.left + 10, r.top, r.left + 26, r.bottom), focused ? th_->acc : th_->ink3);
  const D2D1_RECT_F inner = Rc(r.left + (glyph ? 34.f : 10.f), r.top, r.right - 10 - padRight, r.bottom);
  PushClip(inner);
  float shift = 0;
  if (text->empty()) {
    Text(placeholder, inner, f, th_->ink3);
  } else {
    // Keep the tail (and the caret) visible for long values.
    const float tw = Measure(*text, f);
    shift = std::max(0.f, tw - (inner.right - inner.left) + 6);
    if (auto layout = Layout(*text, f, 8000, r.bottom - r.top, Align::Left, 0, false)) {
      dc_->DrawTextLayout(D2D1::Point2F(inner.left - shift, r.top), layout.Get(), Solid(th_->ink),
                          D2D1_DRAW_TEXT_OPTIONS_NONE);
    }
  }
  if (focused && caretOn) {
    const float tw = std::min(Measure(*text, f), inner.right - inner.left - 6);
    const float x = inner.left + tw + 1;
    const float inset = std::max(6.f, (r.bottom - r.top) * 0.25f);
    Fill(Rc(x, r.top + inset, x + px() * std::round(scale_), r.bottom - inset), th_->acc);
  }
  PopClip();
  return changed;
}

bool Context::Segmented(Id id, D2D1_RECT_F r, const std::vector<std::wstring>& items, int* selected) {
  if (items.empty()) return false;
  FillRound(r, kRadiusCtl, th_->field);
  const float pad = 3;
  const float w = (r.right - r.left - 2 * pad) / items.size();
  bool changed = false;
  for (size_t i = 0; i < items.size(); ++i) {
    const D2D1_RECT_F c = Rc(Snap(r.left + pad + w * i), r.top + pad, Snap(r.left + pad + w * (i + 1)), r.bottom - pad);
    const bool hover = Hover(c);
    if (ClickArea(id + i + 1, c) && *selected != static_cast<int>(i)) {
      *selected = static_cast<int>(i);
      changed = true;
    }
    const bool sel = *selected == static_cast<int>(i);
    if (sel) FillRound(c, kRadiusCtl - 2, th_->dark ? Over(th_->bg, D2D1::ColorF(1, 1, 1, 0.13f)) : th_->raised);
    else if (hover) FillRound(c, kRadiusCtl - 2, th_->sel);
    Text(items[i], Rc(c.left + 4, c.top, c.right - 4, c.bottom), Font::Small, sel || hover ? th_->ink : th_->ink2,
         Align::Center);
  }
  return changed;
}

bool Context::Link(Id id, float x, float y, std::wstring_view label, wchar_t glyph, float* width) {
  const float gw = glyph ? 20.f : 0.f;
  const float tw = Measure(label, Font::Small);
  const D2D1_RECT_F r = Rc(x, y, x + gw + tw, y + 20);
  const bool hover = Hover(r);
  const bool clicked = ClickArea(id, r);
  const D2D1_COLOR_F c = hover ? th_->ink : th_->ink2;
  if (glyph) Glyph(glyph, Rc(x, y, x + 12, y + 20), hover ? th_->acc : th_->ink3);
  Text(label, Rc(x + gw, y, x + gw + tw + 2, y + 20), Font::Small, c);
  if (width) *width = gw + tw;
  return clicked;
}

void Context::PushClip(D2D1_RECT_F r) {
  if (!clips_.empty()) {
    const D2D1_RECT_F& p = clips_.back();
    r = Rc(std::max(r.left, p.left), std::max(r.top, p.top), std::min(r.right, p.right), std::min(r.bottom, p.bottom));
    if (r.right < r.left) r.right = r.left;
    if (r.bottom < r.top) r.bottom = r.top;
  }
  clips_.push_back(r);
  dc_->PushAxisAlignedClip(r, D2D1_ANTIALIAS_MODE_ALIASED);
}

void Context::PopClip() {
  if (clips_.empty()) return;
  clips_.pop_back();
  dc_->PopAxisAlignedClip();
}

float Context::BeginScroll(D2D1_RECT_F viewport, float* scroll, float contentHeight) {
  const float maxScroll = std::max(0.f, contentHeight - (viewport.bottom - viewport.top));
  if (Hover(viewport) && in_->wheel != 0 && !InWheelZone(in_->mouse)) {
    *scroll -= in_->wheel * 64.f;
    wantRedraw = true;
  }
  *scroll = std::round(std::clamp(*scroll, 0.f, maxScroll));
  PushClip(viewport);
  return *scroll;
}

void Context::EndScroll(D2D1_RECT_F viewport, float scroll, float contentHeight) {
  PopClip();
  const float vh = viewport.bottom - viewport.top;
  if (contentHeight <= vh + 1) return;
  const float th = std::max(24.f, vh * vh / contentHeight);
  const float ty = viewport.top + (vh - th) * (scroll / (contentHeight - vh));
  FillRound(Rc(viewport.right - 4, ty, viewport.right - 1, ty + th), 1.5f, th_->track);
}

}  // namespace sl::ui
