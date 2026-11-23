#pragma once

#include "common/winutil.h"

#include <d2d1_1.h>
#include <dwrite_1.h>

#include <cstdint>
#include <string>
#include <vector>

namespace sl::ui {

using Id = uint64_t;
Id MakeId(std::string_view s, int index = 0);

// Calm, flat visual language: solid paper or asphalt, tonal surfaces instead of lines, one accent.
struct Theme {
  bool dark = false;
  D2D1_COLOR_F bg;             // window background (flat)
  D2D1_COLOR_F panel;          // navigation column
  D2D1_COLOR_F raised;         // cards, selected segment, keycaps
  D2D1_COLOR_F sel;            // hover overlay
  D2D1_COLOR_F field;          // inputs, segmented track, tonal buttons
  D2D1_COLOR_F track;          // slider rail, switch off state
  D2D1_COLOR_F ink, ink2, ink3;
  D2D1_COLOR_F line;           // rare hairline
  D2D1_COLOR_F tile;           // crosshair preview background
  D2D1_COLOR_F tileInk;        // labels drawn on preview tiles
  D2D1_COLOR_F acc, onAcc;     // single accent and text on it
};
const Theme& LightTheme();
const Theme& DarkTheme();

// Corner radii (DIPs).
inline constexpr float kRadiusCard = 8, kRadiusCtl = 6;

enum class Font {
  Label,     // Cascadia Mono 9.5, used upper-case with tracking
  Mono,      // Cascadia Mono 11
  Body,      // Segoe UI Variable Text 13
  BodyBold,  // Segoe UI Variable Text 13 semibold
  Small,     // Segoe UI Variable Text 12
  Tiny,      // Segoe UI Variable Text 11
  Button,    // Segoe UI Variable Text 12.5 semibold
  Title,     // Bahnschrift 26 semibold
  H2,        // Bahnschrift 20 semibold
  Name,      // Bahnschrift 14 semibold
  Wordmark,  // Bahnschrift 15 bold
  Icon,      // Segoe Fluent Icons 12
  IconLg,    // Segoe Fluent Icons 16
  Count
};
enum class Align { Left, Center, Right };
enum class Cursor { Arrow, Hand, IBeam, SizeWE };
// Primary: filled accent. Secondary: quiet tonal fill. Small: tonal, small text. Ghost: text only, tonal on hover.
enum class Btn { Secondary, Primary, Small, Ghost };

// Segoe Fluent Icons code points.
namespace icon {
inline constexpr wchar_t Add = 0xE710, Edit = 0xE70F, Copy = 0xE8C8, Star = 0xE734, StarFill = 0xE735,
                         Export = 0xEDE1, OpenFile = 0xE8E5, Chevron = 0xE76C, ChevronDown = 0xE70D,
                         ChevronUp = 0xE70E, Close = 0xE711, Eye = 0xE7B3, Hidden = 0xED1A, Up = 0xE74A,
                         Down = 0xE74B, Remove = 0xE738, Code = 0xE943, Folder = 0xE838, Reset = 0xE7A7,
                         Library = 0xE8F1, Keyboard = 0xE765, Game = 0xE7FC, Shield = 0xEA18, Settings = 0xE713,
                         Warning = 0xE7BA, Delete = 0xE74D, Search = 0xE721, More = 0xE712, Undo = 0xE7A7,
                         Redo = 0xE7A6, Save = 0xE74E, Check = 0xE73E, Paste = 0xE77F, Brush = 0xE771,
                         Duplicate = 0xF5ED, Apps = 0xF0E2, Minimize = 0xE921, Maximize = 0xE922, Restore = 0xE923,
                         ChromeClose = 0xE8BB, Logs = 0xE9F9;
}

inline D2D1_RECT_F Rc(float l, float t, float r, float b) { return D2D1::RectF(l, t, r, b); }
inline D2D1_COLOR_F WithAlpha(D2D1_COLOR_F c, float a) { return {c.r, c.g, c.b, a}; }

struct Input {
  D2D1_POINT_2F mouse{-1000, -1000};
  bool down = false;
  bool pressed = false;
  bool released = false;
  bool doubleClick = false;
  bool rightClick = false;  // right button released this frame
  float wheel = 0;
  std::wstring chars;
  std::vector<UINT> keys;
  bool ctrl = false;
  bool shift = false;

  bool Key(UINT vk) const {
    for (const UINT k : keys) {
      if (k == vk) return true;
    }
    return false;
  }
  void EndFrame() {
    pressed = released = doubleClick = rightClick = false;
    wheel = 0;
    chars.clear();
    keys.clear();
  }
};

class Context {
 public:
  bool Init(ID2D1DeviceContext* dc, IDWriteFactory1* dw, HWND hwnd);
  void SetTheme(const Theme& t) { th_ = &t; }
  const Theme& th() const { return *th_; }
  void Begin(const Input& in, D2D1_SIZE_F size, float scale);
  void End();

  ID2D1DeviceContext* dc() const { return dc_; }
  const Input& in() const { return *in_; }
  D2D1_SIZE_F size() const { return size_; }
  float px() const { return 1.f / scale_; }  // one physical pixel in DIPs
  float Snap(float v) const;

  // Drawing
  ID2D1SolidColorBrush* Solid(D2D1_COLOR_F c);
  void Fill(D2D1_RECT_F r, D2D1_COLOR_F c);
  void FillRound(D2D1_RECT_F r, float radius, D2D1_COLOR_F c);
  void StrokeRound(D2D1_RECT_F r, float radius, D2D1_COLOR_F c, float width = 1);  // stroke inside r
  void Circle(D2D1_POINT_2F center, float radius, D2D1_COLOR_F c);
  void Stroke(D2D1_RECT_F r, D2D1_COLOR_F c);  // 1 physical px, inside r
  void HLine(float x0, float x1, float y, D2D1_COLOR_F c);
  void VLine(float x, float y0, float y1, D2D1_COLOR_F c);
  void Text(std::wstring_view t, D2D1_RECT_F r, Font f, D2D1_COLOR_F c, Align a = Align::Left, float spacing = 0);
  void Label(std::wstring_view t, D2D1_RECT_F r, D2D1_COLOR_F c, Align a = Align::Left);  // mono caps
  void Glyph(wchar_t g, D2D1_RECT_F r, D2D1_COLOR_F c, bool large = false);
  // Stroked line icon from SVG path data on a 24-unit grid (round caps and joins), fitted into r.
  // Supports M L H V C S A Z in both cases. `path` must be a string literal: geometry is cached by pointer.
  void PathIcon(const char* path, D2D1_RECT_F r, D2D1_COLOR_F c, float strokeDip = 1.6f);
  float Paragraph(std::wstring_view t, D2D1_RECT_F r, Font f, D2D1_COLOR_F c, bool draw = true);
  float Measure(std::wstring_view t, Font f, float spacing = 0);
  float MeasureLabel(std::wstring_view t);
  void Background();
  // Mono caps section heading; returns the y below it.
  float Section(float x0, float x1, float y, std::wstring_view left, std::wstring_view right = {});
  // Draws "Ctrl+Alt+1" as keycaps ending at `right`; returns the left edge.
  float Keycaps(std::wstring_view text, float right, float cy);

  // Interaction
  bool Hover(D2D1_RECT_F r) const;
  bool ClickArea(Id id, D2D1_RECT_F r);
  bool Button(Id id, D2D1_RECT_F r, std::wstring_view label, Btn style = Btn::Secondary, bool enabled = true,
              wchar_t glyph = 0);
  bool Chip(Id id, D2D1_RECT_F r, std::wstring_view label, bool selected);
  bool Switch(Id id, D2D1_RECT_F track, bool* value);
  // Full-width row: label (and optional hint) left, switch right.
  bool Toggle(Id id, D2D1_RECT_F r, std::wstring_view label, bool* value, std::wstring_view hint = {});
  // Pressed inside `r` and still held; keeps reporting while the pointer leaves the area.
  bool DragArea(Id id, D2D1_RECT_F r);
  bool RightClicked(D2D1_RECT_F r) const { return in_->rightClick && Hover(r); }
  // labelW > 0: one-line row "label | rail | value". labelW == 0: label and value above the rail.
  // Clicking the value opens an inline numeric field (Enter applies, Esc cancels); the wheel anywhere
  // over the row moves the value by one step instead of scrolling the enclosing viewport.
  // `note` is drawn quietly after the value (e.g. "→ 3" for an adjusted pixel size); a disabled
  // slider only shows its value.
  bool Slider(Id id, D2D1_RECT_F r, std::wstring_view label, float* value, float min, float max, float step, int decimals,
              std::wstring_view unit = {}, bool centered = false, float labelW = 0, std::wstring_view note = {},
              bool enabled = true);
  // Wheel notches over `r` this frame (up = +1); also claims `r` so the enclosing viewport does not
  // scroll under it. For numeric fields that should react to the wheel like sliders do.
  int WheelSteps(D2D1_RECT_F r);
  bool Swatch(Id id, D2D1_RECT_F r, D2D1_COLOR_F c, bool selected);
  // Compact numeric field: label in the first `labelW` DIPs of `row`, the field after it. The field's
  // tonal fill shows the value within [min, max] (from the middle when `centered`). Dragging
  // horizontally changes the value relative to where the press started, a click without movement
  // opens inline entry (Enter applies, Esc cancels), and the wheel anywhere over the row steps it.
  bool ValueField(Id id, D2D1_RECT_F row, std::wstring_view label, float* value, float min, float max, float step,
                  int decimals, std::wstring_view unit = {}, bool centered = false, float labelW = 0,
                  std::wstring_view note = {});
  // `glyph` draws a leading icon; `padRight` / `padLeft` keep room for controls drawn over the ends.
  bool TextField(Id id, D2D1_RECT_F r, std::wstring* text, std::wstring_view placeholder, size_t maxLen = 64,
                 bool* submitted = nullptr, bool mono = false, wchar_t glyph = 0, float padRight = 0,
                 float padLeft = 0);
  bool Segmented(Id id, D2D1_RECT_F r, const std::vector<std::wstring>& items, int* selected);
  bool Link(Id id, float x, float y, std::wstring_view label, wchar_t glyph, float* width = nullptr);

  void PushClip(D2D1_RECT_F r);
  void PopClip();
  // Vertical scroll viewport; returns the y offset to subtract from content.
  float BeginScroll(D2D1_RECT_F viewport, float* scroll, float contentHeight);
  void EndScroll(D2D1_RECT_F viewport, float scroll, float contentHeight);

  // Popover surface: raised fill, hairline border, soft shadow.
  void Popover(D2D1_RECT_F r);
  // Modal mode: while set, widgets drawn outside BeginOverlay/EndOverlay get no hover, clicks or wheel.
  void SetModal(bool on) { modal_ = on; }
  bool Modal() const { return modal_; }
  void BeginOverlay() { overlay_ = true; }
  void EndOverlay() { overlay_ = false; }

  // Custom window chrome: rectangles hit-tested for interaction that start above `height` are
  // collected each frame, so the caption strip can tell controls from draggable background.
  void SetCaptionHeight(float height) { captionH_ = height; }
  bool OverControl(D2D1_POINT_2F p) const;

  bool IsFocused(Id id) const { return focus_ == id; }
  bool AnyFocus() const { return focus_ != 0; }
  void ClearFocus() { focus_ = 0; }
  void SetFocus(Id id) {
    focus_ = id;
    focusClaimed_ = true;
  }
  bool IsActive(Id id) const { return active_ == id; }
  bool caretOn = true;
  bool wantRedraw = false;
  Cursor cursor = Cursor::Arrow;

 private:
  IDWriteTextFormat* Format(Font f);
  ComPtr<IDWriteTextLayout> Layout(std::wstring_view t, Font f, float w, float h, Align a, float spacing, bool wrap);
  void SwitchVisual(D2D1_RECT_F track, bool on, bool hover);

  HWND hwnd_ = nullptr;
  ID2D1DeviceContext* dc_ = nullptr;
  IDWriteFactory1* dw_ = nullptr;
  const Theme* th_ = nullptr;
  const Input* in_ = nullptr;
  D2D1_SIZE_F size_{};
  float scale_ = 1;
  ComPtr<ID2D1SolidColorBrush> solid_;
  ComPtr<ID2D1StrokeStyle> roundStroke_;
  std::vector<std::pair<const char*, ComPtr<ID2D1PathGeometry>>> paths_;
  ComPtr<IDWriteTextFormat> formats_[static_cast<int>(Font::Count)];
  std::vector<D2D1_RECT_F> clips_;
  bool InWheelZone(D2D1_POINT_2F p) const;

  Id active_ = 0;
  Id focus_ = 0;
  bool focusClaimed_ = false;
  bool modal_ = false;
  bool overlay_ = false;
  // Slider inline value entry.
  Id valueEdit_ = 0;
  bool valueEditSeen_ = false;
  std::wstring valueText_;
  // Slider rails that take the wheel: collected this frame, consulted by BeginScroll next frame.
  std::vector<D2D1_RECT_F> wheelZones_, prevWheelZones_;
  void AddWheelZone(D2D1_RECT_F r);
  // Value field drag: pointer x and value at the press, and whether the pointer has moved since.
  float dragX_ = 0, dragV_ = 0;
  bool dragMoved_ = false;
  // Interactive rectangles inside the caption strip (this frame / last complete frame).
  float captionH_ = 0;
  mutable std::vector<D2D1_RECT_F> controls_;
  std::vector<D2D1_RECT_F> prevControls_;
};

}  // namespace sl::ui
