#include "common/crosshair_render.h"
#include "common/i18n.h"
#include "common/ipc.h"
#include "common/logo.h"
#include "settings/app.h"

#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>

namespace sl {

using ui::Align;
using ui::Btn;
using ui::Font;
using ui::MakeId;
using ui::Rc;
using ui::WithAlpha;
namespace icon = ui::icon;

namespace {

// Layout (DIPs): navigation column, top row (brand + page title), content padding, library side card.
constexpr float kNav = 208, kHead = 68, kPad = 32, kGap = 24, kSide = 312;
// Fixed crosshair preview sizes: they never grow with the window.
constexpr float kCanvasMax = 360, kLibPreviewH = 148;
constexpr const char* kCategories[] = {"all",    "fav",      "classic", "dot", "circle",
                                       "sniper", "tactical", "access",  "fun", "mine"};
constexpr int kMaxLayers = 8;
constexpr const char* kNavKeys[] = {"nav.library", "nav.editor", "nav.hotkeys", "nav.profiles", "nav.safety", "nav.settings"};
constexpr const char* kTitleKeys[] = {"title.library", "title.editor",  "title.hotkeys",
                                      "title.profiles", "title.safety", "title.settings"};
constexpr wchar_t kNavIcons[] = {icon::Library, icon::Edit, icon::Keyboard, icon::Game, icon::Shield, icon::Settings};
constexpr float kZooms[] = {2, 4, 8, 12, 16};
constexpr UINT kNudgeMods[] = {0, MOD_CONTROL | MOD_ALT, MOD_CONTROL | MOD_SHIFT, MOD_ALT | MOD_SHIFT};

std::string Key(const char* prefix, std::string_view name) { return std::string(prefix) + std::string(name); }

D2D1_COLOR_F ToD2D(const Color& c) { return D2D1::ColorF(c.r, c.g, c.b, 1.f); }

std::wstring Trim(std::wstring s) {
  while (!s.empty() && iswspace(s.back())) s.pop_back();
  size_t i = 0;
  while (i < s.size() && iswspace(s[i])) ++i;
  return s.substr(i);
}

bool AddExe(std::vector<std::wstring>& list, std::wstring exe) {
  exe = Lower(Trim(std::move(exe)));
  if (exe.empty()) return false;
  if (exe.find(L'.') == std::wstring::npos) exe += L".exe";
  if (std::find(list.begin(), list.end(), exe) != list.end()) return false;
  list.push_back(std::move(exe));
  return true;
}

std::wstring Fmt(const wchar_t* fmt, ...) {
  wchar_t buf[256];
  va_list ap;
  va_start(ap, fmt);
  vswprintf_s(buf, fmt, ap);
  va_end(ap);
  return buf;
}

const Color kFillPalette[] = {
    {1, 1, 1, 1},
    {1.0f, 0.894f, 0.361f, 1},   // yellow
    {1.0f, 0.851f, 0.180f, 1},   // sun
    {1.0f, 1.0f, 0.0f, 1},       // pure yellow
    {1.0f, 0.478f, 0.102f, 1},   // orange
    {1.0f, 0.498f, 0.431f, 1},   // coral
    {1.0f, 0.231f, 0.231f, 1},   // red
    {1.0f, 0.169f, 0.839f, 1},   // magenta
    {0.765f, 0.722f, 0.941f, 1}, // lavender
    {0.184f, 0.420f, 1.0f, 1},   // blue
    {0.247f, 0.878f, 1.0f, 1},   // cyan
    {0.239f, 1.0f, 0.361f, 1},   // neon green
    {0.5f, 0.5f, 0.5f, 1},
    {0.0f, 0.0f, 0.0f, 1},
};

const Color kOutlinePalette[] = {
    {0, 0, 0, 1}, {1, 1, 1, 1}, {0.12f, 0.12f, 0.14f, 1}, {0.10f, 0.10f, 0.30f, 1}, {0.35f, 0.05f, 0.05f, 1}};

bool SameRgb(const Color& a, const Color& b) {
  return std::fabs(a.r - b.r) < 0.01f && std::fabs(a.g - b.g) < 0.01f && std::fabs(a.b - b.b) < 0.01f;
}

bool IsUser(const Crosshair& c) { return FindBuiltin(c.id) == nullptr; }

// Editable content only: identity, favourite, hotkey and category are managed outside the editor.
std::string EditFp(Crosshair c) {
  c.id.clear();
  c.category.clear();
  c.favorite = false;
  c.hotkey = {};
  return CrosshairFingerprint(c);
}

// One numeric shape parameter shown in the inspector's size section.
struct Param {
  const char* id;
  const char* label;
  float Layer::*field;
  float mn, mx, step;
  int dec;
  const wchar_t* unit;
};

std::vector<Param> ShapeParams(const Layer& L) {
  switch (L.shape) {
    case Shape::Cross:
    case Shape::XCross:
    case Shape::TShape:
      return {{"p.len", "lbl.length", &Layer::length, 0, 60, 1, 0, L" px"},
              {"p.thk", "lbl.thickness", &Layer::thickness, 1, 12, 1, 0, L" px"},
              {"p.gap", "lbl.gap", &Layer::gap, 0, 40, 1, 0, L" px"}};
    case Shape::Plus:
      return {{"p.len", "lbl.length", &Layer::length, 1, 60, 1, 0, L" px"},
              {"p.thk", "lbl.thickness", &Layer::thickness, 1, 12, 1, 0, L" px"}};
    case Shape::Dot: return {{"p.size", "lbl.size", &Layer::size, 1, 24, 1, 0, L" px"}};
    case Shape::Ring:
      return {{"p.radius", "lbl.radius", &Layer::size, 2, 60, 0.5f, 1, L" px"},
              {"p.thk", "lbl.thickness", &Layer::thickness, 0.5f, 8, 0.5f, 1, L" px"}};
    case Shape::Box:
    case Shape::Diamond:
    case Shape::Triangle: {
      std::vector<Param> p = {{"p.half", L.shape == Shape::Triangle ? "lbl.circumradius" : "lbl.halfSize", &Layer::size, 1,
                               40, 1, 0, L" px"}};
      if (!L.filled) p.push_back({"p.thk", "lbl.thickness", &Layer::thickness, 1, 8, 1, 0, L" px"});
      return p;
    }
    case Shape::Corners:
      return {{"p.gap", "lbl.distanceShort", &Layer::gap, 2, 60, 1, 0, L" px"},
              {"p.len", "lbl.arm", &Layer::length, 1, 40, 1, 0, L" px"},
              {"p.thk", "lbl.thickness", &Layer::thickness, 1, 8, 1, 0, L" px"}};
    case Shape::Chevron:
      return {{"p.gap", "lbl.gap", &Layer::gap, 0, 40, 1, 0, L" px"},
              {"p.len", "lbl.arm", &Layer::length, 1, 40, 1, 0, L" px"},
              {"p.thk", "lbl.thickness", &Layer::thickness, 0.5f, 8, 0.5f, 1, L" px"}};
    case Shape::Segments:
      return {{"p.radius", "lbl.radius", &Layer::size, 2, 60, 0.5f, 1, L" px"},
              {"p.thk", "lbl.thickness", &Layer::thickness, 0.5f, 8, 0.5f, 1, L" px"},
              {"p.seggap", "lbl.gap", &Layer::segmentGap, 0, 80, 1, 0, L"°"}};
    case Shape::Line:
      return {{"p.len", "lbl.halfLength", &Layer::length, 1, 80, 1, 0, L" px"},
              {"p.thk", "lbl.thickness", &Layer::thickness, 1, 12, 1, 0, L" px"},
              {"p.gap", "lbl.gap", &Layer::gap, 0, 40, 1, 0, L" px"}};
    default: return {};
  }
}

std::wstring Num(float v, int dec) { return Fmt(L"%.*f", dec, v); }

// The "tactical" presets are geometric figures; the settings UI calls the category "Shapes".
const wchar_t* CatLabel(std::string_view category) {
  return category == "tactical" ? T("cat.shapes") : T(Key("cat.", category));
}

bool Inside(D2D1_RECT_F r, D2D1_POINT_2F p) { return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom; }

// Shapes that read as a cross and take an optional centre dot.
bool CrossLike(Shape s) { return s == Shape::Cross || s == Shape::XCross || s == Shape::TShape || s == Shape::Line; }
bool HasVerticalLength(Shape s) { return s == Shape::Cross || s == Shape::XCross || s == Shape::TShape; }
bool IsCenterDot(const Layer& l) { return l.shape == Shape::Dot && l.offsetX == 0 && l.offsetY == 0; }

// h in [0, 360), s and v in [0, 1].
Color HsvToRgb(float h, float s, float v) {
  h = std::fmod(std::max(0.f, h), 360.f) / 60.f;
  const int i = static_cast<int>(h);
  const float f = h - i, p = v * (1 - s), q = v * (1 - s * f), u = v * (1 - s * (1 - f));
  switch (i) {
    case 0: return {v, u, p, 1};
    case 1: return {q, v, p, 1};
    case 2: return {p, v, u, 1};
    case 3: return {p, q, v, 1};
    case 4: return {u, p, v, 1};
    default: return {v, p, q, 1};
  }
}

// Keeps `hsv[0]` when the colour is grey and `hsv[1]` when it is black, so dragging stays stable.
void RgbToHsv(const Color& c, float hsv[3]) {
  const float mx = std::max({c.r, c.g, c.b}), mn = std::min({c.r, c.g, c.b}), d = mx - mn;
  if (d > 1e-4f) {
    float h;
    if (mx == c.r) h = std::fmod((c.g - c.b) / d, 6.f);
    else if (mx == c.g) h = (c.b - c.r) / d + 2;
    else h = (c.r - c.g) / d + 4;
    h *= 60;
    if (h < 0) h += 360;
    hsv[0] = h;
  }
  if (mx > 1e-4f) hsv[1] = d / mx;
  hsv[2] = mx;
}

// Places a popover of size w x h below `anchor` (or above when it does not fit), inside the window.
D2D1_RECT_F PlacePopover(D2D1_RECT_F anchor, float w, float h, D2D1_SIZE_F win, bool alignRight) {
  constexpr float margin = 12, gap = 6;
  float x = alignRight ? anchor.right - w : anchor.left;
  x = std::clamp(x, margin, std::max(margin, win.width - margin - w));
  float y = anchor.bottom + gap;
  if (y + h > win.height - margin) {
    y = anchor.top - gap - h;
    if (y < margin) y = std::max(margin, win.height - margin - h);
  }
  return Rc(std::round(x), std::round(y), std::round(x + w), std::round(y + h));
}

// Rounded rectangle filled with a linear gradient between `from` and `to`.
void FillGradient(ID2D1DeviceContext* dc, D2D1_RECT_F r, float radius, const D2D1_GRADIENT_STOP* stops, UINT32 n,
                  D2D1_POINT_2F from, D2D1_POINT_2F to) {
  ComPtr<ID2D1GradientStopCollection> coll;
  if (FAILED(dc->CreateGradientStopCollection(stops, n, &coll))) return;
  ComPtr<ID2D1LinearGradientBrush> brush;
  if (FAILED(dc->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(from, to), coll.Get(), &brush))) return;
  dc->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
  dc->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), brush.Get());
  dc->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
}

void DrawRing(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush, D2D1_POINT_2F c, float r, D2D1_COLOR_F color, float w) {
  brush->SetColor(color);
  dc->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
  dc->DrawEllipse(D2D1::Ellipse(c, r, r), brush, w);
  dc->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
}

}  // namespace

// ───────────────────────────── library actions ─────────────────────────────

void SettingsApp::DuplicateCrosshair(const Crosshair& c) {
  // Copied now, so the duplicate holds the saved state the user clicked on.
  Crosshair copy = c;
  GuardDraft([this, copy]() mutable {
    copy.id = NewCrosshairId();
    copy.name += Narrow(T("lib.copySuffix"));
    copy.category = "mine";
    copy.builtin = false;
    copy.favorite = false;
    copy.hotkey = {};
    cfg_.crosshairs.push_back(copy);
    cfg_.activeId = copy.id;
    sideScroll_ = 0;
    PushConfig();
  });
}

bool SettingsApp::PresetEdited(const std::string& id) const {
  const Crosshair* builtin = FindBuiltin(id);
  if (!builtin || !IsModifiedPreset(cfg_, id)) return false;
  return EditFp(*FindCrosshair(cfg_, id)) != EditFp(*builtin);
}

void SettingsApp::DeleteOrResetCrosshair(const Crosshair& c) {
  const bool user = IsUser(c);
  if (!user && !PresetEdited(c.id)) return;
  // The draft of a deleted or reset crosshair has nothing left to be saved into.
  if (c.id == draftId_) draftId_.clear();
  if (user) {
    pendingDelete_ = c.id;
    if (c.id == cfg_.activeId) cfg_.activeId = BuiltinPresets().front().id;
    Toast(T("toast.deleted"));
    return;
  }
  // Reset restores the built-in content; favourite and hotkey stored on the override survive.
  Crosshair& stored = EditableCrosshair(cfg_, c.id);
  if (stored.favorite || stored.hotkey.Valid()) {
    Crosshair restored = *FindBuiltin(c.id);
    restored.favorite = stored.favorite;
    restored.hotkey = stored.hotkey;
    stored = restored;  // in place: pointers into the list stay valid for this frame
    PushConfig();
  } else {
    pendingDelete_ = c.id;
  }
  Toast(T("toast.reset"));
}

void SettingsApp::Activate(const std::string& id) {
  if (id == ActiveId()) return;
  GuardDraft([this, id] {
    cfg_.activeId = id;
    sideScroll_ = 0;
    PushConfig();
  });
}

void SettingsApp::ToggleFavorite(const Crosshair& c) {
  // `c` may live in cfg_.crosshairs, which EditableCrosshair can reallocate.
  const std::string id = c.id;
  const bool fav = !c.favorite;
  EditableCrosshair(cfg_, id).favorite = fav;
  PushConfig();
}

void SettingsApp::CopyCrosshairCode(const Crosshair& c) {
  if (SetClipboardText(hwnd_, Widen(EncodeShareCode(c)))) Toast(T("toast.codeCopied"));
}

// ───────────────────────────── editor history ─────────────────────────────

void SettingsApp::EditApply(const Crosshair& next) {
  if (!historyPending_) {
    historyBefore_ = draft_;
    historyPending_ = true;
  }
  draft_ = next;
  draft_.id = draftId_;
  Invalidate();
}

void SettingsApp::CommitHistory(bool force) {
  if (!historyPending_) return;
  if (!force && (in_.down || ui_.AnyFocus() || GetTickCount64() - lastWheel_ < 450)) return;
  historyPending_ = false;
  if (EditFp(draft_) == EditFp(historyBefore_)) return;
  undo_.push_back(historyBefore_);
  if (undo_.size() > 100) undo_.erase(undo_.begin());
  redo_.clear();
}

void SettingsApp::StepHistory(bool redo) {
  CommitHistory(true);
  std::vector<Crosshair>& from = redo ? redo_ : undo_;
  std::vector<Crosshair>& to = redo ? undo_ : redo_;
  if (from.empty()) return;
  Crosshair snap = std::move(from.back());
  from.pop_back();
  to.push_back(draft_);
  if (to.size() > 100) to.erase(to.begin());
  // Snapshots may predate "save to Mine" (new id); identity, favourite and hotkey stay current.
  snap.id = draftId_;
  snap.category = draft_.category;
  snap.favorite = draft_.favorite;
  snap.hotkey = draft_.hotkey;
  draft_ = std::move(snap);
  editName_ = Widen(draft_.name);
  editLayer_ = std::clamp(editLayer_, 0, std::max(0, static_cast<int>(draft_.layers.size()) - 1));
  ui_.ClearFocus();
  Invalidate();
}

void SettingsApp::ClearHistory() {
  undo_.clear();
  redo_.clear();
  historyPending_ = false;
}

// ───────────────────────────── editor draft ─────────────────────────────

std::string SettingsApp::ActiveId() const { return ResolveCrosshair(cfg_, cfg_.activeId).id; }

bool SettingsApp::DraftDirty() const {
  if (draftId_.empty()) return false;
  const Crosshair* saved = FindCrosshair(cfg_, draftId_);
  return saved && EditFp(*saved) != EditFp(draft_);
}

void SettingsApp::RebaseDraft() {
  draft_ = ResolveCrosshair(cfg_, cfg_.activeId);
  draftId_ = draft_.id;
  editName_ = Widen(draft_.name);
  editLayer_ = 0;
  propsScroll_ = 0;
  layersScroll_ = 0;
  ClearHistory();
  if (popup_ == Popup::Color) ClosePopup();
}

void SettingsApp::SyncDraft() {
  if (popup_ == Popup::Unsaved) return;
  if (draftId_ == ActiveId()) {
    // Favourite, hotkey and category change from the library; the draft follows them.
    if (const Crosshair* s = FindCrosshair(cfg_, draftId_)) {
      draft_.favorite = s->favorite;
      draft_.hotkey = s->hotkey;
      draft_.category = s->category;
    }
    return;
  }
  // The active crosshair changed under a dirty draft (core hotkey, tray): ask before dropping it.
  if (DraftDirty()) {
    OpenUnsaved(UnsavedKind::Switched);
    return;
  }
  RebaseDraft();
}

void SettingsApp::SaveDraft(bool activate) {
  CommitHistory(true);
  const Crosshair* saved = FindCrosshair(cfg_, draftId_);
  if (!saved) return;
  Crosshair d = draft_;
  if (Trim(Widen(d.name)).empty()) d.name = saved->name;
  if (IsUser(*saved)) {
    Crosshair& dst = EditableCrosshair(cfg_, draftId_);
    d.id = dst.id;
    d.category = dst.category;
    d.favorite = dst.favorite;
    d.hotkey = dst.hotkey;
    d.builtin = false;
    dst = d;
    draft_ = d;
    editName_ = Widen(d.name);
    Toast(T("toast.saved"));
  } else {
    // Built-in presets are read-only: the edit becomes the user's own crosshair in "Mine".
    if (d.name == saved->name) d.name += Narrow(T("ed.mineSuffix"));
    d.id = NewCrosshairId();
    d.category = "mine";
    d.builtin = false;
    d.favorite = false;
    d.hotkey = {};
    cfg_.crosshairs.push_back(d);
    if (activate) {
      cfg_.activeId = d.id;
      draftId_ = d.id;
      draft_ = d;
      editName_ = Widen(d.name);
    } else {
      RebaseDraft();
    }
    Toast(T("toast.presetCopy"));
  }
  PushConfig();
}

void SettingsApp::SaveDraftAsNew() {
  CommitHistory(true);
  const Crosshair* saved = FindCrosshair(cfg_, draftId_);
  if (!saved) return;
  Crosshair d = draft_;
  if (Trim(Widen(d.name)).empty()) d.name = saved->name;
  if (d.name == saved->name) d.name += Narrow(IsUser(*saved) ? T("lib.copySuffix") : T("ed.mineSuffix"));
  d.id = NewCrosshairId();
  d.category = "mine";
  d.builtin = false;
  d.favorite = false;
  d.hotkey = {};
  // The original keeps its saved state; the editor continues on the new crosshair.
  cfg_.crosshairs.push_back(d);
  cfg_.activeId = d.id;
  draftId_ = d.id;
  draft_ = d;
  editName_ = Widen(d.name);
  Toast(std::wstring(T("toast.savedNew")) + L" «" + editName_ + L"»");
  PushConfig();
}

void SettingsApp::DiscardDraft() {
  CommitHistory(true);
  const Crosshair* saved = FindCrosshair(cfg_, draftId_);
  if (!saved || !DraftDirty()) return;
  // Undoable: the discarded draft becomes one history step.
  undo_.push_back(draft_);
  if (undo_.size() > 100) undo_.erase(undo_.begin());
  redo_.clear();
  draft_ = *saved;
  editName_ = Widen(draft_.name);
  editLayer_ = std::clamp(editLayer_, 0, std::max(0, static_cast<int>(draft_.layers.size()) - 1));
  ui_.ClearFocus();
  Toast(T("toast.discarded"));
  Invalidate();
}

void SettingsApp::SyncPreview() {
  // The core shows the draft only while it differs from the saved state of the active crosshair.
  std::string want;
  if (DraftDirty() && draftId_ == ActiveId()) want = json::Serialize(ToJson(draft_), false);
  if (want == sentPreview_) return;
  sentPreview_ = want;
  ipc::Send(ipc::CoreWindow(), hwnd_, ipc::kPreviewCrosshair, want);
}

void SettingsApp::GuardDraft(std::function<void()> action) {
  if (DraftDirty()) OpenUnsaved(UnsavedKind::Action, std::move(action));
  else if (action) action();
}

void SettingsApp::OpenUnsaved(UnsavedKind kind, std::function<void()> action) {
  popup_ = Popup::Unsaved;
  unsavedKind_ = kind;
  unsavedAction_ = std::move(action);
  pickerOpen_.clear();
  popupId_.clear();
  ui_.ClearFocus();
  ui_.SetModal(true);  // the rest of this frame is inert as well
  ui_.wantRedraw = true;
  Invalidate();
}

bool SettingsApp::RequestClose() {
  if (!DraftDirty()) return true;
  if (IsIconic(hwnd_)) ShowWindow(hwnd_, SW_RESTORE);
  // A dialog already open for another reason is replaced: closing wins.
  OpenUnsaved(UnsavedKind::Close);
  return false;
}

void SettingsApp::UnsavedDialog() {
  const auto& t = ui_.th();
  const Crosshair* saved = FindCrosshair(cfg_, draftId_);
  if (!saved) {
    popup_ = Popup::None;
    unsavedAction_ = nullptr;
    return;
  }
  const bool preset = !IsUser(*saved);
  const D2D1_SIZE_F win = ui_.size();
  ui_.Fill(Rc(0, 0, win.width, win.height), D2D1::ColorF(0, 0, 0, t.dark ? 0.38f : 0.20f));

  constexpr float w = 452, pad = 24, btnH = 34;
  const std::wstring title = std::wstring(T("dlg.saveTitlePre")) + Widen(saved->name) + T("dlg.saveTitlePost");
  const wchar_t* body = unsavedKind_ == UnsavedKind::Close ? T("dlg.bodyClose") : T("dlg.body");
  const wchar_t* note = preset ? T("dlg.bodyPreset") : nullptr;
  const float bodyH = ui_.Paragraph(body, Rc(0, 0, w - 2 * pad, 400), Font::Body, t.ink2, false);
  const float noteH = note ? ui_.Paragraph(note, Rc(0, 0, w - 2 * pad, 400), Font::Small, t.ink3, false) + 8 : 0.f;
  const float h = pad + 24 + 10 + bodyH + noteH + 24 + btnH + pad;
  const float x = std::round((win.width - w) / 2), y = std::round(std::max(24.f, (win.height - h) / 2 - 20));
  const D2D1_RECT_F r = Rc(x, y, x + w, y + h);
  ui_.Popover(r);
  popupRect_ = r;
  float yy = r.top + pad;
  ui_.Text(title, Rc(r.left + pad, yy, r.right - pad, yy + 24), Font::Name, t.ink);
  yy += 24 + 10;
  ui_.Paragraph(body, Rc(r.left + pad, yy, r.right - pad, yy + bodyH + 4), Font::Body, t.ink2);
  yy += bodyH;
  if (note) {
    yy += 8;
    ui_.Paragraph(note, Rc(r.left + pad, yy, r.right - pad, yy + noteH), Font::Small, t.ink3);
    yy += noteH - 8;
  }

  // Buttons right-aligned: Save is the primary action, Cancel the quiet one.
  float bx = r.right - pad;
  const float by = r.bottom - pad - btnH;
  const auto button = [&](const char* id, const wchar_t* label, Btn style) {
    const float bw = std::ceil(ui_.Measure(label, style == Btn::Ghost ? Font::Small : Font::Button)) + 36;
    const D2D1_RECT_F b = Rc(bx - bw, by, bx, by + btnH);
    bx -= bw + 8;
    return ui_.Button(MakeId(id), b, label, style);
  };
  int choice = -1;  // 0 save, 1 don't save, 2 cancel
  if (button("dlg.save", preset ? T("ed.saveMine") : T("dlg.save"), Btn::Primary)) choice = 0;
  if (button("dlg.dont", T("dlg.dontSave"), Btn::Secondary)) choice = 1;
  if (button("dlg.cancel", T("dlg.cancel"), Btn::Ghost)) choice = 2;
  if (in_.Key(VK_RETURN)) choice = 0;
  if (in_.Key(VK_ESCAPE)) choice = 2;
  if (choice < 0) return;

  const UnsavedKind kind = unsavedKind_;
  std::function<void()> action = std::move(unsavedAction_);
  unsavedAction_ = nullptr;
  popup_ = Popup::None;
  ui_.wantRedraw = true;
  if (choice == 2) {
    // Switched: go back to the crosshair being edited.
    if (kind == UnsavedKind::Switched && cfg_.activeId != draftId_) {
      cfg_.activeId = draftId_;
      PushConfig();
    }
    return;
  }
  if (choice == 0) SaveDraft(false);
  else RebaseDraft();
  if (kind == UnsavedKind::Close) pendingClose_ = true;
  else if (action) action();
}

// ───────────────────────────── chrome ─────────────────────────────

void SettingsApp::Chrome() {
  const auto& t = ui_.th();
  const float W = ui_.size().width, H = ui_.size().height;
  tip_.clear();
  Shortcuts();
  ui_.Background();
  ui_.Fill(Rc(0, 0, kNav, H), t.panel);

  // Brand.
  DrawLogo(dc_.Get(), Rc(24, 17, 46, 39), t.ink, t.acc);
  ui_.Text(L"SIGHTLINE", Rc(56, 18, kNav - 8, 38), Font::Wordmark, t.ink, Align::Left, 2.4f);

  // Top row, right: core status and master switch.
  const float cy = 28;
  const D2D1_RECT_F track = Rc(W - kPad - 36, cy - 10, W - kPad, cy + 10);
  bool enabled = cfg_.settings.enabled;
  if (ui_.Switch(MakeId("master"), track, &enabled)) {
    cfg_.settings.enabled = enabled;
    PushConfig();
  }
  const float ow = std::ceil(ui_.Measure(T("chrome.overlay"), Font::Body));
  float x = track.left - 10 - ow;
  ui_.Text(T("chrome.overlay"), Rc(x, cy - 12, track.left - 8, cy + 12), Font::Body, t.ink);
  x -= 32;
  if (CoreOnline()) {
    const float sw = std::ceil(ui_.Measure(T("chrome.coreOnline"), Font::Small));
    ui_.Text(T("chrome.coreOnline"), Rc(x - sw, cy - 12, x + 2, cy + 12), Font::Small, t.ink3);
    x -= sw + 12;
    ui_.Circle({x + 4, cy}, 3.5f, t.acc);
  } else {
    const float bw = std::ceil(ui_.Measure(T("btn.startCore"), Font::Small)) + 24;
    if (ui_.Button(MakeId("core.start"), Rc(x - bw, cy - 14, x, cy + 14), T("btn.startCore"), Btn::Small)) LaunchCore();
    x -= bw + 10;
    const float sw = std::ceil(ui_.Measure(T("chrome.coreOffline"), Font::Small));
    ui_.Text(T("chrome.coreOffline"), Rc(x - sw, cy - 12, x + 2, cy + 12), Font::Small, t.ink3);
    x -= sw + 12;
    ui_.Circle({x + 4, cy}, 3.5f, t.ink3);
    ui_.Circle({x + 4, cy}, 2.f, t.panel);
  }

  // The one page title.
  ui_.Text(T(kTitleKeys[static_cast<int>(page_)]), Rc(kNav + kPad - 1, 8, x - 24, 50), Font::Title, t.ink);

  // Navigation.
  float y = 88;
  for (int i = 0; i < static_cast<int>(Page::Count); ++i) {
    const D2D1_RECT_F r = Rc(12, y, kNav - 12, y + 38);
    const bool sel = static_cast<int>(page_) == i;
    const bool hover = ui_.Hover(r);
    if (ui_.ClickArea(MakeId("nav", i), r) && !sel) {
      page_ = static_cast<Page>(i);
      pickerOpen_.clear();
      pickTarget_ = PickTarget::None;
      confirmReset_ = false;
      ui_.ClearFocus();
    }
    if (sel) ui_.FillRound(r, ui::kRadiusCtl, t.field);
    else if (hover) ui_.FillRound(r, ui::kRadiusCtl, t.sel);
    ui_.Glyph(kNavIcons[i], Rc(24, y, 40, y + 38), sel ? t.acc : t.ink3);
    ui_.Text(T(kNavKeys[i]), Rc(52, y, kNav - 34, y + 38), sel ? Font::BodyBold : Font::Body, sel || hover ? t.ink : t.ink2);
    // Unsaved editor draft: a small dot on the editor entry, visible from every page.
    if (i == static_cast<int>(Page::Editor) && DraftDirty()) {
      const D2D1_POINT_2F c{kNav - 30, y + 19};
      ui_.Circle(c, 4, t.acc);
      Tip(r, T("ed.unsaved"));
    }
    y += 40;
  }
  ui_.Text(L"v0.2", Rc(24, H - 36, kNav - 16, H - 16), Font::Mono, t.ink3);

  const D2D1_RECT_F area = Rc(kNav, kHead, W, H);
  switch (page_) {
    case Page::Library: PageLibrary(area); break;
    case Page::Editor: PageEditor(area); break;
    case Page::Hotkeys: PageHotkeys(area); break;
    case Page::Profiles: PageProfiles(area); break;
    case Page::Safety: PageSafety(area); break;
    case Page::Settings: PageSettings(area); break;
    default: break;
  }
  Overlays();

  // Hover hint: small inverse pill under (or above) the hovered control.
  if (!tip_.empty() && popup_ == Popup::None) {
    const float tw = std::ceil(ui_.Measure(tip_, Font::Small)) + 20, th = 26;
    const float ax = (tipAnchor_.left + tipAnchor_.right) / 2;
    const float tx = std::round(std::clamp(ax - tw / 2, 8.f, std::max(8.f, W - 8 - tw)));
    float ty = tipAnchor_.bottom + 6;
    if (ty + th > H - 8) ty = tipAnchor_.top - 6 - th;
    const D2D1_RECT_F r = Rc(tx, std::round(ty), tx + tw, std::round(ty) + th);
    ui_.FillRound(r, ui::kRadiusCtl, t.ink);
    ui_.Text(tip_, r, Font::Small, t.bg, Align::Center);
  }

  // Toast: inverse pill near the bottom of the content area.
  if (!toast_.empty() && GetTickCount64() < toastUntil_) {
    const float tw = ui_.Measure(toast_, Font::Small) + 52;
    const float cx = (kNav + W) / 2;
    const D2D1_RECT_F r = Rc(std::round(cx - tw / 2), H - 72, std::round(cx + tw / 2), H - 36);
    ui_.FillRound(r, 18, t.ink);
    ui_.Circle({r.left + 20, (r.top + r.bottom) / 2}, 3.5f, t.acc);
    ui_.Text(toast_, Rc(r.left + 34, r.top, r.right - 14, r.bottom), Font::Small, t.bg);
  }
}

void SettingsApp::Shortcuts() {
  // The draft follows the active crosshair; switching away from unsaved changes asks first.
  SyncDraft();
  CommitHistory(false);
  if (popup_ == Popup::Unsaved) return;  // the dialog handles its own keys

  // An open popover closes on Esc or on a press outside it; that press does not reach the page.
  if (popup_ != Popup::None && ui_.Modal()) {
    if (in_.Key(VK_ESCAPE) || (in_.pressed && !Inside(popupRect_, in_.mouse))) ClosePopup();
  }
  if (capture_ != Capture::None || !in_.ctrl) return;
  for (const UINT k : in_.keys) {
    if (k == 'F') {
      ClosePopup();
      page_ = Page::Library;
      ui_.SetFocus(MakeId("lib.search"));
      continue;
    }
    if (popup_ != Popup::None) continue;
    // Ctrl+S saves the draft from any page, also while the name field is focused.
    if (k == 'S') {
      if (DraftDirty()) SaveDraft(true);
      continue;
    }
    if (page_ != Page::Editor || ui_.AnyFocus()) continue;
    if (k == 'Z') StepHistory(in_.shift);
    else if (k == 'Y') StepHistory(true);
    else if (k == 'D') dupLayerRequest_ = true;
  }
}

void SettingsApp::ClosePopup() {
  if (popup_ == Popup::None) return;
  popup_ = Popup::None;
  pickerOpen_.clear();
  popupId_.clear();
  ui_.ClearFocus();
  ui_.wantRedraw = true;
}

void SettingsApp::Overlays() {
  if (popup_ == Popup::None) return;
  ui_.BeginOverlay();
  switch (popup_) {
    case Popup::CardMenu: CardMenu(); break;
    case Popup::Color: ColorPopover(); break;
    case Popup::Picker: PickerPopover(); break;
    case Popup::Unsaved: UnsavedDialog(); break;
    default: break;
  }
  ui_.EndOverlay();
}

void SettingsApp::Tip(D2D1_RECT_F r, const wchar_t* text) {
  if (text && *text && ui_.Hover(r)) {
    tip_ = text;
    tipAnchor_ = r;
  }
}

void SettingsApp::CardMenu() {
  const auto& t = ui_.th();
  const Crosshair* found = FindCrosshair(cfg_, popupId_);
  if (!found) {
    ClosePopup();
    return;
  }
  const Crosshair c = *found;  // actions below may reallocate the crosshair list
  const bool user = IsUser(c);
  const bool modified = PresetEdited(c.id);
  constexpr float w = 224, rowH = 34, pad = 6;
  constexpr int n = 5;
  const D2D1_RECT_F r = PlacePopover(popupAnchor_, w, pad * 2 + rowH * n + 9, ui_.size(), false);
  ui_.Popover(r);
  popupRect_ = r;
  float y = r.top + pad;
  const auto item = [&](const char* id, wchar_t glyph, const wchar_t* label, bool enabled = true) {
    const D2D1_RECT_F row = Rc(r.left + pad, y, r.right - pad, y + rowH);
    const bool hover = enabled && ui_.Hover(row);
    const bool clicked = enabled && ui_.ClickArea(MakeId(id), row);
    if (hover) ui_.FillRound(row, ui::kRadiusCtl, t.field);
    const D2D1_COLOR_F ink = enabled ? t.ink : t.ink3;
    ui_.Glyph(glyph, Rc(row.left + 8, row.top, row.left + 28, row.bottom), enabled ? (hover ? t.acc : t.ink3) : WithAlpha(t.ink3, 0.6f));
    ui_.Text(label, Rc(row.left + 38, row.top, row.right - 8, row.bottom), Font::Body, ink);
    y += rowH;
    return clicked;
  };
  bool done = false;
  if (item("menu.edit", icon::Edit, T("btn.edit"))) {
    page_ = Page::Editor;
    Activate(c.id);
    done = true;
  }
  if (item("menu.dup", icon::Copy, T("btn.duplicate"))) {
    DuplicateCrosshair(c);
    done = true;
  }
  if (item("menu.fav", c.favorite ? icon::StarFill : icon::Star, c.favorite ? T("menu.unfav") : T("menu.fav"))) {
    ToggleFavorite(c);
    done = true;
  }
  if (item("menu.code", icon::Code, T("btn.copyCodeShort"))) {
    CopyCrosshairCode(c);
    done = true;
  }
  // Destructive action sits apart from the rest.
  ui_.HLine(r.left + pad + 8, r.right - pad - 8, y + 4, t.line);
  y += 9;
  if (item("menu.del", user ? icon::Delete : icon::Reset, user ? T("btn.delete") : T("btn.reset"), user || modified)) {
    DeleteOrResetCrosshair(c);
    done = true;
  }
  // An action may have opened the unsaved-changes dialog in place of this menu.
  if (done && popup_ == Popup::CardMenu) ClosePopup();
}

void SettingsApp::ColorChip(D2D1_RECT_F r, const Color& c, int which) {
  const auto& t = ui_.th();
  const bool open = popup_ == Popup::Color && popupColor_ == which;
  const bool hover = ui_.Hover(Rc(r.left - 2, r.top - 2, r.right + 2, r.bottom + 2));
  if (open) ui_.StrokeRound(Rc(r.left - 3, r.top - 3, r.right + 3, r.bottom + 3), 7, t.acc, 1.5f);
  else if (hover) ui_.StrokeRound(Rc(r.left - 3, r.top - 3, r.right + 3, r.bottom + 3), 7, t.ink3, 1.f);
  ui_.FillRound(r, 5, ToD2D(c));
  ui_.StrokeRound(r, 5, t.dark ? D2D1::ColorF(1, 1, 1, 0.14f) : D2D1::ColorF(0, 0, 0, 0.14f));
  if (ui_.ClickArea(MakeId("chip.color", which), Rc(r.left - 2, r.top - 2, r.right + 2, r.bottom + 2))) {
    popup_ = Popup::Color;
    popupColor_ = which;
    // The chip is followed by a 100-DIP hex field; the popover lines up with the field's right edge.
    popupAnchor_ = Rc(r.left, r.top, r.left + 100, r.bottom);
    RgbToHsv(c, hsv_);
    ui_.ClearFocus();
  }
}

void SettingsApp::ColorPopover() {
  const auto& t = ui_.th();
  if (page_ != Page::Editor) {
    ClosePopup();
    return;
  }
  Crosshair cur = draft_;
  if (cur.layers.empty()) {
    ClosePopup();
    return;
  }
  Layer& L = cur.layers[std::clamp(editLayer_, 0, static_cast<int>(cur.layers.size()) - 1)];
  Color& col = popupColor_ == 1 ? L.outlineColor : L.fill;

  constexpr float w = 256, pad = 14, svH = 150, hueH = 12, rowH = 32;
  const D2D1_RECT_F r = PlacePopover(popupAnchor_, w, pad + svH + 14 + hueH + 14 + rowH + pad, ui_.size(), true);
  ui_.Popover(r);
  popupRect_ = r;
  bool changed = false;

  // Saturation (x) / value (y) square over the current hue.
  const D2D1_RECT_F sv = Rc(r.left + pad, r.top + pad, r.right - pad, r.top + pad + svH);
  if (ui_.DragArea(MakeId("pop.sv"), sv)) {
    const float s = std::clamp((in_.mouse.x - sv.left) / (sv.right - sv.left), 0.f, 1.f);
    const float v = 1.f - std::clamp((in_.mouse.y - sv.top) / (sv.bottom - sv.top), 0.f, 1.f);
    if (s != hsv_[1] || v != hsv_[2]) {
      hsv_[1] = s;
      hsv_[2] = v;
      changed = true;
    }
  }
  {
    const Color hue = HsvToRgb(hsv_[0], 1, 1);
    ui_.FillRound(sv, 6, ToD2D(hue));
    const D2D1_GRADIENT_STOP white[] = {{0, D2D1::ColorF(1, 1, 1, 1)}, {1, D2D1::ColorF(1, 1, 1, 0)}};
    FillGradient(dc_.Get(), sv, 6, white, 2, {sv.left, sv.top}, {sv.right, sv.top});
    const D2D1_GRADIENT_STOP black[] = {{0, D2D1::ColorF(0, 0, 0, 0)}, {1, D2D1::ColorF(0, 0, 0, 1)}};
    FillGradient(dc_.Get(), sv, 6, black, 2, {sv.left, sv.top}, {sv.left, sv.bottom});
    const D2D1_POINT_2F m{sv.left + hsv_[1] * (sv.right - sv.left), sv.top + (1 - hsv_[2]) * (sv.bottom - sv.top)};
    DrawRing(dc_.Get(), ui_.Solid(t.ink), m, 6.5f, D2D1::ColorF(0, 0, 0, 0.35f), 1.f);
    DrawRing(dc_.Get(), ui_.Solid(t.ink), m, 5.f, D2D1::ColorF(1, 1, 1, 1), 2.f);
  }

  // Hue strip.
  const float hy = sv.bottom + 14;
  const D2D1_RECT_F hue = Rc(sv.left, hy, sv.right, hy + hueH);
  if (ui_.DragArea(MakeId("pop.hue"), Rc(hue.left, hue.top - 5, hue.right, hue.bottom + 5))) {
    const float h = std::clamp((in_.mouse.x - hue.left) / (hue.right - hue.left), 0.f, 0.9999f) * 360.f;
    if (h != hsv_[0]) {
      hsv_[0] = h;
      changed = true;
    }
  }
  {
    D2D1_GRADIENT_STOP stops[7];
    for (int i = 0; i < 7; ++i) {
      const Color c = HsvToRgb(i * 60.f, 1, 1);
      stops[i] = {i / 6.f, D2D1::ColorF(c.r, c.g, c.b, 1)};
    }
    FillGradient(dc_.Get(), hue, hueH / 2, stops, 7, {hue.left, hue.top}, {hue.right, hue.top});
    const float kx = hue.left + hsv_[0] / 360.f * (hue.right - hue.left);
    const float cy = (hue.top + hue.bottom) / 2;
    ui_.Circle({kx, cy}, 8, D2D1::ColorF(0, 0, 0, 0.30f));
    ui_.Circle({kx, cy}, 7, D2D1::ColorF(1, 1, 1, 1));
    ui_.Circle({kx, cy}, 4.5f, ToD2D(HsvToRgb(hsv_[0], 1, 1)));
  }

  // Preview and hex.
  const float ry = hue.bottom + 14;
  const D2D1_RECT_F prev = Rc(sv.left, ry, sv.left + rowH, ry + rowH);
  ui_.FillRound(prev, ui::kRadiusCtl, ToD2D(col));
  ui_.StrokeRound(prev, ui::kRadiusCtl, t.dark ? D2D1::ColorF(1, 1, 1, 0.14f) : D2D1::ColorF(0, 0, 0, 0.14f));
  const ui::Id hexId = MakeId("pop.hex");
  if (!ui_.IsFocused(hexId)) popupHex_ = Widen(ColorToHex(col));
  bool submitted = false;
  if (ui_.TextField(hexId, Rc(prev.right + 8, ry, sv.right, ry + rowH), &popupHex_, L"#RRGGBB", 9, &submitted, true)) {
    bool ok = false;
    const Color c = ColorFromHex(Narrow(popupHex_), &ok);
    if (ok) {
      const float a = col.a;
      col = c;
      col.a = a;
      RgbToHsv(col, hsv_);
      EditApply(cur);
    }
  }
  if (changed) {
    const float a = col.a;
    col = HsvToRgb(hsv_[0], hsv_[1], hsv_[2]);
    col.a = a;
    EditApply(cur);
  }
}

void SettingsApp::PickerPopover() {
  const auto& t = ui_.th();
  if (!pickerSeen_ || !pickerTarget_) {
    ClosePopup();
    return;
  }
  const std::wstring needle = Lower(Trim(pickerSearch_));
  std::vector<const Crosshair*> items;
  for (const Crosshair* c : Library(cfg_)) {
    if (needle.empty() || Lower(Widen(c->name)).find(needle) != std::wstring::npos) items.push_back(c);
  }
  constexpr float pad = 8, searchH = 34, rowH = 38;
  const float w = std::max(popupAnchor_.right - popupAnchor_.left, 320.f);
  const float listH = std::min(rowH * 8.5f, std::max(rowH, rowH * static_cast<float>(items.size())));
  const D2D1_RECT_F r = PlacePopover(popupAnchor_, w, pad + searchH + 6 + listH + pad, ui_.size(), false);
  ui_.Popover(r);
  popupRect_ = r;

  bool submitted = false;
  if (ui_.TextField(MakeId("pick.search"), Rc(r.left + pad, r.top + pad, r.right - pad, r.top + pad + searchH),
                    &pickerSearch_, T("lib.search"), 40, &submitted, false, icon::Search)) {
    pickerScroll_ = 0;
  }
  const auto choose = [&](const Crosshair* c) {
    *pickerTarget_ = c->id;
    PushConfig();
    ClosePopup();
  };
  if (submitted && !items.empty()) {
    choose(items.front());
    return;
  }

  const D2D1_RECT_F view = Rc(r.left + pad, r.top + pad + searchH + 6, r.right - 4, r.bottom - pad);
  const float contentH = rowH * static_cast<float>(items.size());
  if (pickerScroll_ < 0) {
    // First frame: bring the current choice into view.
    pickerScroll_ = 0;
    for (size_t i = 0; i < items.size(); ++i) {
      if (items[i]->id == *pickerTarget_) {
        pickerScroll_ = std::floor((rowH * static_cast<float>(i) - (listH - rowH) / 2) / rowH) * rowH;
      }
    }
  }
  const float off = ui_.BeginScroll(view, &pickerScroll_, contentH);
  if (items.empty()) ui_.Text(T("lib.noResults"), Rc(view.left + 10, view.top, view.right, view.top + rowH), Font::Small, t.ink3);
  const Crosshair* picked = nullptr;
  for (size_t i = 0; i < items.size(); ++i) {
    const float y = view.top + rowH * static_cast<float>(i) - off;
    if (y + rowH < view.top || y > view.bottom) continue;
    const Crosshair* c = items[i];
    const D2D1_RECT_F row = Rc(view.left, y + 1, r.right - pad - 6, y + rowH - 1);
    const bool sel = c->id == *pickerTarget_;
    const bool hover = ui_.Hover(row);
    if (sel) ui_.FillRound(row, ui::kRadiusCtl, t.field);
    else if (hover) ui_.FillRound(row, ui::kRadiusCtl, t.sel);
    const D2D1_RECT_F thumb = Rc(row.left + 4, row.top + 4, row.left + 4 + (row.bottom - row.top - 8), row.bottom - 4);
    DrawPreview(*c, thumb, 1, 0, false, 4);
    const std::wstring cat = CatLabel(c->category);
    const float cw = std::ceil(ui_.Measure(cat, Font::Small)) + 2;
    ui_.Text(cat, Rc(row.right - 10 - cw, row.top, row.right - 10, row.bottom), Font::Small, t.ink3, Align::Right);
    ui_.Text(Widen(c->name), Rc(thumb.right + 10, row.top, row.right - 20 - cw, row.bottom), sel ? Font::BodyBold : Font::Body,
             t.ink);
    if (ui_.ClickArea(MakeId("pick.item", static_cast<int>(i)), row)) picked = c;
  }
  ui_.EndScroll(view, off, contentH);
  if (picked) choose(picked);
}

float SettingsApp::RowText(D2D1_RECT_F row, const wchar_t* title, const wchar_t* hint, float controlW) {
  const auto& t = ui_.th();
  const float cl = row.right - 16 - controlW;
  const D2D1_RECT_F text = Rc(row.left + 16, row.top, cl - 24, row.bottom);
  const float mid = std::round((row.top + row.bottom) / 2);
  if (hint && *hint) {
    const float hh = ui_.Paragraph(hint, Rc(text.left, 0, text.right, 400), Font::Small, t.ink3, false);
    const float top = std::round(mid - (19 + hh) / 2);
    ui_.Text(title, Rc(text.left, top, text.right, top + 19), Font::Body, t.ink);
    ui_.Paragraph(hint, Rc(text.left, top + 20, text.right, row.bottom), Font::Small, t.ink3);
  } else {
    ui_.Text(title, text, Font::Body, t.ink);
  }
  return cl;
}

// ───────────────────────────── library ─────────────────────────────

void SettingsApp::PageLibrary(D2D1_RECT_F full) {
  const auto& t = ui_.th();
  const D2D1_RECT_F side = Rc(full.right - kPad - kSide, full.top, full.right - kPad, full.bottom - kGap);
  const D2D1_RECT_F area = Rc(full.left + kPad, full.top, side.left - kGap - 8, full.bottom);

  // Created only once unsaved editor changes are answered for.
  const auto createNew = [&]() {
    GuardDraft([this] {
      Crosshair c;
      c.id = NewCrosshairId();
      c.name = Narrow(T("lib.newName"));
      c.category = "mine";
      c.layers.push_back(Layer{});
      cfg_.crosshairs.push_back(c);
      cfg_.activeId = c.id;
      page_ = Page::Editor;
      PushConfig();
    });
  };

  // Search sits in the header row, right-aligned with the grid.
  {
    const ui::Id sid = MakeId("lib.search");
    const float titleR = kNav + kPad + std::ceil(ui_.Measure(T("title.library"), Font::Title));
    const D2D1_RECT_F sf = Rc(std::max(titleR + 28, area.right - 300), 11, area.right, 45);
    const bool hasText = !search_.empty();
    if (ui_.TextField(sid, sf, &search_, T("lib.search"), 40, nullptr, false, icon::Search, hasText ? 22.f : 44.f)) {
      libScroll_ = 0;
    }
    if (hasText) {
      const D2D1_RECT_F x = Rc(sf.right - 32, sf.top + 5, sf.right - 6, sf.bottom - 5);
      const bool xh = ui_.Hover(x);
      if (xh) ui_.FillRound(x, ui::kRadiusCtl - 2, t.sel);
      ui_.Glyph(icon::Close, x, xh ? t.ink : t.ink3);
      if (ui_.ClickArea(MakeId("lib.searchClear"), x)) {
        search_.clear();
        libScroll_ = 0;
      }
    } else if (!ui_.IsFocused(sid)) {
      ui_.Label(L"Ctrl+F", Rc(sf.left, sf.top, sf.right - 12, sf.bottom), t.ink3, Align::Right);
    }
  }

  // Category chips; creation lives in the grid's first tile.
  float y = full.top, cx = area.left;
  for (int i = 0; i < static_cast<int>(std::size(kCategories)); ++i) {
    const std::wstring label = CatLabel(kCategories[i]);
    const float w = std::round(ui_.Measure(label, Font::Small) + 22);
    if (cx + w > area.right && cx > area.left) {
      cx = area.left;
      y += 36;
    }
    if (ui_.Chip(MakeId("cat", i), Rc(cx, y, cx + w, y + 30), label, libCategory_ == i)) {
      libCategory_ = i;
      libScroll_ = 0;
    }
    cx += w + 6;
  }
  y += 30 + 20;

  const std::string cat = kCategories[std::clamp(libCategory_, 0, static_cast<int>(std::size(kCategories)) - 1)];
  const std::wstring needle = Lower(Trim(search_));
  const std::vector<const Crosshair*> all = Library(cfg_);
  std::vector<const Crosshair*> items;
  for (const Crosshair* c : all) {
    const bool user = IsUser(*c);
    // "mine" means any non-built-in id, whatever its stored category.
    if (!(cat == "all" || (cat == "fav" && c->favorite) || (cat == "mine" && user) ||
          (cat != "mine" && c->category == cat))) {
      continue;
    }
    if (!needle.empty() && Lower(Widen(c->name)).find(needle) == std::wstring::npos) continue;
    items.push_back(c);
  }

  // Soft tiles; extra width becomes more columns, never bigger previews.
  const bool showNew = (cat == "all" || cat == "mine") && needle.empty();
  const int count = static_cast<int>(items.size()) + (showNew ? 1 : 0);
  constexpr float gap = 12, minW = 136, cardH = 164, tileH = 104;
  const float gw = area.right - area.left;
  const int cols = std::max(1, static_cast<int>((gw + gap) / (minW + gap)));
  const float cw = std::floor((gw - gap * (cols - 1)) / cols);
  const int rows = (count + cols - 1) / cols;
  const D2D1_RECT_F view = Rc(area.left - 2, y, area.right + 14, full.bottom);
  const float contentH = rows * (cardH + gap) + kGap;
  const float off = ui_.BeginScroll(view, &libScroll_, contentH);
  if (count == 0) {
    ui_.Text(needle.empty() ? T("lib.empty") : T("lib.noResults"), Rc(area.left, y, area.right, y + 24), Font::Body, t.ink3);
  }
  for (int i = 0; i < count; ++i) {
    const int col = i % cols, row = i / cols;
    const float left = area.left + col * (cw + gap), top = y + row * (cardH + gap) - off;
    const D2D1_RECT_F r = Rc(left, top, left + cw, top + cardH);
    if (r.bottom < view.top || r.top > view.bottom) continue;
    const bool isNew = showNew && i == 0;
    const bool hover = ui_.Hover(r);
    const Crosshair* c = isNew ? nullptr : items[i - (showNew ? 1 : 0)];
    const bool active = c && c->id == cfg_.activeId;
    if (active) ui_.FillRound(r, ui::kRadiusCard, t.raised);
    else if (hover) ui_.FillRound(r, ui::kRadiusCard, t.sel);
    const D2D1_RECT_F tile = Rc(r.left + 6, r.top + 6, r.right - 6, r.top + 6 + tileH);
    const D2D1_RECT_F nameR = Rc(r.left + 10, tile.bottom + 10, r.right - 10, tile.bottom + 30);
    const D2D1_RECT_F metaR = Rc(r.left + 10, tile.bottom + 30, r.right - 10, tile.bottom + 48);

    if (isNew) {
      ui_.FillRound(tile, ui::kRadiusCtl, t.raised);
      ui_.Glyph(icon::Add, tile, hover ? t.acc : t.ink3, true);
      ui_.Text(T("lib.newCell"), nameR, Font::Name, hover ? t.ink : t.ink2);
      if (ui_.ClickArea(MakeId("lib.newcell"), r)) {
        ui_.EndScroll(view, off, contentH);
        createNew();
        return;
      }
      continue;
    }

    // Card click is evaluated before the "more" button so the button can take over the press.
    const bool cardClick = ui_.ClickArea(MakeId(c->id), r);
    DrawPreview(*c, tile, 3, 0);
    if (c->favorite) ui_.Glyph(icon::StarFill, Rc(tile.left + 4, tile.top + 4, tile.left + 24, tile.top + 22), t.acc);
    bool moreClick = false;
    const bool menuOpen = popup_ == Popup::CardMenu && popupId_ == c->id;
    if (hover || menuOpen) {
      const D2D1_RECT_F more = Rc(tile.right - 30, tile.top + 4, tile.right - 4, tile.top + 26);
      const bool mh = ui_.Hover(more);
      if (mh || menuOpen) ui_.FillRound(more, ui::kRadiusCtl - 2, D2D1::ColorF(1, 1, 1, 0.12f));
      ui_.Glyph(icon::More, more, mh || menuOpen ? D2D1::ColorF(1, 1, 1, 0.92f) : t.tileInk);
      if (ui_.ClickArea(MakeId(c->id, 1), more)) {
        moreClick = true;
        popup_ = Popup::CardMenu;
        popupId_ = c->id;
        popupAnchor_ = more;
      }
    }
    if (ui_.RightClicked(r)) {
      popup_ = Popup::CardMenu;
      popupId_ = c->id;
      popupAnchor_ = Rc(in_.mouse.x, in_.mouse.y, in_.mouse.x, in_.mouse.y);
    }
    ui_.Text(Widen(c->name), nameR, Font::Name, t.ink);

    // Meta line: category left, one quiet status right.
    std::wstring status;
    D2D1_COLOR_F statusInk = t.ink3;
    Font statusFont = Font::Small;
    if (active) {
      status = T("lib.tagActive");
      statusInk = t.acc;
    } else if (c->hotkey.Valid()) {
      status = HotkeyToText(c->hotkey);
      statusFont = Font::Mono;
    } else if (PresetEdited(c->id)) {
      status = T("lib.tagModified");
    }
    float sw = 0;
    if (!status.empty()) {
      sw = std::ceil(ui_.Measure(status, statusFont)) + 2;
      ui_.Text(status, Rc(metaR.right - sw, metaR.top, metaR.right, metaR.bottom), statusFont, statusInk, Align::Right);
    }
    ui_.Text(CatLabel(c->category), Rc(metaR.left, metaR.top, metaR.right - sw - 8, metaR.bottom), Font::Small, t.ink3);

    if (cardClick && !moreClick && !active) Activate(c->id);
  }
  ui_.EndScroll(view, off, contentH);

  ActivePanel(side);
}

void SettingsApp::ActivePanel(D2D1_RECT_F side) {
  const auto& t = ui_.th();
  ui_.FillRound(side, 10, t.raised);
  const float x0 = side.left + 20, x1 = side.right - 20;
  const D2D1_RECT_F view = Rc(side.left, side.top + 2, side.right - 2, side.bottom - 2);
  static float contentH = 700;
  const float off = ui_.BeginScroll(view, &sideScroll_, contentH);
  float y = side.top + 20 - off;

  // Copy: actions below may reallocate the crosshair list.
  const Crosshair cur = ResolveCrosshair(cfg_, cfg_.activeId);
  const bool user = IsUser(cur);
  const bool modified = PresetEdited(cur.id);
  const bool draftPending = draftId_ == cur.id && DraftDirty();

  // Fixed-size magnified preview.
  DrawPreview(cur, Rc(x0, y, x1, y + kLibPreviewH), 6, 0, true, ui::kRadiusCard);
  y += kLibPreviewH + 16;

  // Name, favourite star, one quiet meta line.
  ui_.Text(Widen(cur.name), Rc(x0, y, x1 - 30, y + 28), Font::H2, t.ink);
  const D2D1_RECT_F star = Rc(x1 - 24, y + 2, x1, y + 26);
  const bool starHover = ui_.Hover(star);
  ui_.Glyph(cur.favorite ? icon::StarFill : icon::Star, star, cur.favorite ? t.acc : (starHover ? t.ink : t.ink3), true);
  if (ui_.ClickArea(MakeId("q.star"), star)) ToggleFavorite(cur);
  y += 28;
  const wchar_t* kind = user ? T("lib.kindUser") : (modified ? T("lib.modified") : T("lib.kindPreset"));
  ui_.Text(Fmt(L"%s · %s", CatLabel(cur.category), kind), Rc(x0, y, x1, y + 18), Font::Small, t.ink3);
  y += 18;
  if (draftPending) {
    // In game the unsaved draft is shown, not this saved state.
    ui_.Circle({x0 + 3, y + 11}, 3, t.acc);
    float lw0 = 0;
    if (ui_.Link(MakeId("q.draft"), x0 + 12, y + 1, T("lib.draftPending"), 0, &lw0)) page_ = Page::Editor;
    y += 20;
  }
  y += 14;

  // Quick parameters as compact one-line sliders. User crosshairs save at once; presets are
  // read-only and are changed through the editor (which saves a copy).
  constexpr float labelW = 100, rowH = 28;
  Crosshair edit = cur;
  bool changed = false;
  changed |= ui_.Slider(MakeId("q.scale"), Rc(x0, y, x1, y + rowH), T("lbl.scale"), &edit.scale, 0.5f, 4.f, 0.25f, 2, L"×",
                        false, labelW, {}, user);
  y += rowH + 2;
  float opacity = edit.opacity * 100;
  if (ui_.Slider(MakeId("q.opacity"), Rc(x0, y, x1, y + rowH), T("lbl.opacity"), &opacity, 10, 100, 5, 0, L"%", false,
                 labelW, {}, user)) {
    edit.opacity = opacity / 100.f;
    changed = true;
  }
  y += rowH + 2;
  if (!user) {
    y += ui_.Paragraph(T("lib.presetReadOnly"), Rc(x0, y, x1, y + 60), Font::Small, t.ink3) + 6;
  }
  if (changed && user) {
    Crosshair& dst = EditableCrosshair(cfg_, cur.id);
    dst.scale = edit.scale;
    dst.opacity = edit.opacity;
    // A draft of the same crosshair takes the same values, so its dirty state does not change.
    if (draftId_ == cur.id) {
      draft_.scale = edit.scale;
      draft_.opacity = edit.opacity;
    }
    PushConfig();
  }
  float ox = static_cast<float>(cfg_.settings.offsetX), oy = static_cast<float>(cfg_.settings.offsetY);
  if (ui_.Slider(MakeId("q.ox"), Rc(x0, y, x1, y + rowH), T("lbl.globalOffsetX"), &ox, -100, 100, 1, 0, L" px", true,
                 labelW)) {
    cfg_.settings.offsetX = static_cast<int>(ox);
    PushConfig();
  }
  y += rowH + 2;
  if (ui_.Slider(MakeId("q.oy"), Rc(x0, y, x1, y + rowH), T("lbl.globalOffsetY"), &oy, -100, 100, 1, 0, L" px", true,
                 labelW)) {
    cfg_.settings.offsetY = static_cast<int>(oy);
    PushConfig();
  }
  y += rowH + 14;

  // Actions: one primary, the rest quiet.
  const float half = std::floor((x1 - x0 - 8) / 2);
  if (ui_.Button(MakeId("q.edit"), Rc(x0, y, x0 + half, y + 34), T("btn.edit"), Btn::Primary, true, icon::Edit)) {
    page_ = Page::Editor;
  }
  if (ui_.Button(MakeId("q.dup"), Rc(x1 - half, y, x1, y + 34), T("btn.duplicate"), Btn::Secondary, true, icon::Copy)) {
    DuplicateCrosshair(cur);
  }
  y += 34 + 4;
  if (ui_.Button(MakeId("q.copy"), Rc(x0, y, x0 + half, y + 30), T("btn.copyCodeShort"), Btn::Ghost, true, icon::Code)) {
    CopyCrosshairCode(cur);
  }
  if (ui_.Button(MakeId("q.del"), Rc(x1 - half, y, x1, y + 30), user ? T("btn.delete") : T("btn.reset"), Btn::Ghost,
                 user || modified, user ? icon::Delete : icon::Reset)) {
    DeleteOrResetCrosshair(cur);
  }
  y += 30 + 18;

  // Per-crosshair hotkey.
  {
    const D2D1_RECT_F b = Rc(x0, y, x1, y + 34);
    const bool capturing = capture_ == Capture::Crosshair && captureCrosshair_ == cur.id;
    const bool hover = ui_.Hover(b);
    if (ui_.ClickArea(MakeId("q.bind"), b) && !capturing) StartCapture(Capture::Crosshair, cur.id);
    ui_.FillRound(b, ui::kRadiusCtl, t.field);
    if (hover && !capturing) ui_.FillRound(b, ui::kRadiusCtl, t.sel);
    if (capturing) ui_.StrokeRound(b, ui::kRadiusCtl, t.acc, 1.5f);
    ui_.Text(T("lib.hotkey"), Rc(b.left + 12, b.top, b.right - 100, b.bottom), Font::Small, t.ink2);
    const float cy = (b.top + b.bottom) / 2;
    if (capturing) ui_.Text(T("hk.pressKeyShort"), Rc(b.left, b.top, b.right - 12, b.bottom), Font::Small, t.acc, Align::Right);
    else if (cur.hotkey.Valid()) ui_.Keycaps(HotkeyToText(cur.hotkey), b.right - 6, cy);
    else ui_.Text(T("hk.unbound"), Rc(b.left, b.top, b.right - 12, b.bottom), Font::Small, t.ink3, Align::Right);
    y += 34;
    if (capturing) {
      ui_.Text(T("hk.escClears"), Rc(x0, y + 4, x1, y + 22), Font::Small, t.ink3);
      y += 22;
    }
    y += 12;
  }

  // Sharing: import by code and library files.
  bool submitted = false;
  ui_.TextField(MakeId("q.code"), Rc(x0, y, x1 - 84, y + 34), &importCode_, T("lib.importHint"), 4096, &submitted, true);
  if (ui_.Button(MakeId("q.import"), Rc(x1 - 76, y, x1, y + 34), T("btn.import")) || submitted) ImportCode();
  y += 34 + 14;
  float lw = 0;
  if (ui_.Link(MakeId("q.export"), x0, y, T("btn.exportFile"), icon::Export, &lw)) pendingFile_ = FileAction::Export;
  if (ui_.Link(MakeId("q.importFile"), x0 + lw + 20, y, T("btn.importFile"), icon::OpenFile)) pendingFile_ = FileAction::Import;
  y += 20;

  contentH = y + off - side.top + 20;
  ui_.EndScroll(view, off, contentH);
}

// ───────────────────────────── editor ─────────────────────────────

void SettingsApp::PageEditor(D2D1_RECT_F full) {
  const auto& t = ui_.th();
  const D2D1_RECT_F page = Rc(full.left + kPad, full.top, full.right - kPad, full.bottom - kGap);

  // Undo / redo live in the header row, next to the page title.
  {
    const float tx = kNav + kPad + std::ceil(ui_.Measure(T("title.editor"), Font::Title)) + 20;
    const D2D1_RECT_F ub = Rc(tx, 14, tx + 32, 44), rb = Rc(tx + 34, 14, tx + 66, 44);
    if (ui_.Button(MakeId("ed.undo"), ub, {}, Btn::Ghost, !undo_.empty() || historyPending_, icon::Undo)) StepHistory(false);
    if (ui_.Button(MakeId("ed.redo"), rb, {}, Btn::Ghost, !redo_.empty(), icon::Redo)) StepHistory(true);
    Tip(ub, T("ed.undoTip"));
    Tip(rb, T("ed.redoTip"));
  }

  const Crosshair* saved = FindCrosshair(cfg_, draftId_);
  const bool preset = saved && !IsUser(*saved);
  const bool dirty = DraftDirty();

  // Save bar: state on the left, actions on the right. Built-in presets are read-only, so their
  // primary action saves a copy to "Mine".
  constexpr float barH = 48;
  {
    const D2D1_RECT_F bar = Rc(page.left, page.top, page.right, page.top + barH);
    ui_.FillRound(bar, ui::kRadiusCard, t.raised);
    float bx = bar.right - 8;
    const auto button = [&](const char* id, const wchar_t* label, Btn style, bool enabled, wchar_t glyph) {
      const Font f = style == Btn::Ghost ? Font::Small : Font::Button;
      const float w = std::ceil(ui_.Measure(label, f)) + (glyph ? 20.f : 0.f) + 32;
      const D2D1_RECT_F r = Rc(bx - w, bar.top + 7, bx, bar.bottom - 7);
      bx -= w + 8;
      return ui_.Button(MakeId(id), r, label, style, enabled, glyph);
    };
    if (button("ed.save", preset ? T("ed.saveMine") : T("ed.save"), Btn::Primary, dirty, icon::Save)) SaveDraft(true);
    if (button("ed.saveNew", T("ed.saveAsNew"), Btn::Secondary, saved != nullptr, 0)) SaveDraftAsNew();
    if (button("ed.discard", T("ed.discard"), Btn::Ghost, dirty, 0)) DiscardDraft();
    const float sx = bar.left + 18;
    const float mid = std::round((bar.top + bar.bottom) / 2);
    const float ty = preset ? mid - 19 : mid - 10;  // preset: status above, note below
    if (dirty) {
      ui_.Circle({sx + 4, ty + 10}, 4, t.acc);
      ui_.Text(T("ed.unsaved"), Rc(sx + 16, ty, bx - 8, ty + 20), Font::BodyBold, t.ink);
    } else {
      ui_.Glyph(icon::Check, Rc(sx - 3, ty, sx + 11, ty + 20), t.ink3);
      ui_.Text(T("ed.saved"), Rc(sx + 16, ty, bx - 8, ty + 20), Font::Body, t.ink3);
    }
    if (preset) ui_.Text(T("ed.presetLine"), Rc(sx + 16, ty + 20, bx - 8, ty + 38), Font::Small, t.ink3);
  }
  const D2D1_RECT_F area = Rc(page.left, page.top + barH + 18, page.right, page.bottom);

  Crosshair cur = draft_;
  bool changed = false;
  editLayer_ = std::clamp(editLayer_, 0, std::max(0, static_cast<int>(cur.layers.size()) - 1));
  // Shared pixel centre of the crosshair, decided by its first pixel-snapped element.
  const bool even = CrosshairEvenCenter(cur);
  const auto duplicateLayer = [&]() {
    if (cur.layers.empty()) return;
    if (static_cast<int>(cur.layers.size()) >= kMaxLayers) {
      Toast(T("toast.layerLimit"));
      return;
    }
    cur.layers.insert(cur.layers.begin() + editLayer_ + 1, cur.layers[editLayer_]);
    ++editLayer_;
    changed = true;
  };
  const auto deleteLayer = [&](int i) {
    if (cur.layers.size() <= 1) {
      Toast(T("ed.lastLayer"));
      return;
    }
    cur.layers.erase(cur.layers.begin() + i);
    if (editLayer_ > i) --editLayer_;
    editLayer_ = std::clamp(editLayer_, 0, static_cast<int>(cur.layers.size()) - 1);
    changed = true;
  };
  if (dupLayerRequest_) {
    dupLayerRequest_ = false;
    duplicateLayer();
  }
  // Delete removes the selected layer while no field has the keyboard.
  if (popup_ == Popup::None && capture_ == Capture::None && !ui_.AnyFocus() && in_.Key(VK_DELETE)) deleteLayer(editLayer_);

  // Columns: layers | fixed canvas centred in the free space | inspector.
  constexpr float lw = 188, gap = 28;
  const float aw = area.right - area.left, ah = area.bottom - area.top;
  const float iw = std::clamp(aw - lw - 2 * gap - kCanvasMax, 300.f, 380.f);
  const float lx0 = area.left, lx1 = lx0 + lw;
  const float rx1 = area.right, rx0 = rx1 - iw;
  const float colL = lx1 + gap, colR = rx0 - gap;
  const float S = std::floor(std::max(160.f, std::min({kCanvasMax, colR - colL, ah - 76})));
  const float cx0 = std::round((colL + colR - S) / 2);

  // Left column: name and layers.
  {
    const D2D1_RECT_F view = Rc(lx0 - 4, area.top, lx1 + 10, area.bottom);
    static float contentH = 500;
    const float off = ui_.BeginScroll(view, &layersScroll_, contentH);
    float y = area.top - off;
    if (ui_.TextField(MakeId("ed.name"), Rc(lx0, y, lx1, y + 34), &editName_, T("ed.namePlaceholder"), 40)) {
      cur.name = Narrow(editName_);
      changed = true;
    }
    y += 34 + 24;

    // Heading with icon-only layer actions.
    ui_.Label(T("ed.layers"), Rc(lx0, y, lx1, y + 24), t.ink3);
    const bool canDelete = cur.layers.size() > 1;
    {
      float bx = lx1;
      const auto iconBtn = [&](const char* id, wchar_t g, bool enabled, const wchar_t* tip) {
        bx -= 26;
        const D2D1_RECT_F r = Rc(bx, y, bx + 24, y + 24);
        Tip(r, tip);
        return ui_.Button(MakeId(id), r, {}, Btn::Ghost, enabled, g);
      };
      if (iconBtn("ed.down", icon::Down, editLayer_ + 1 < static_cast<int>(cur.layers.size()), T("ed.layerDown"))) {
        std::swap(cur.layers[editLayer_], cur.layers[editLayer_ + 1]);
        ++editLayer_;
        changed = true;
      }
      if (iconBtn("ed.up", icon::Up, editLayer_ > 0, T("ed.layerUp"))) {
        std::swap(cur.layers[editLayer_], cur.layers[editLayer_ - 1]);
        --editLayer_;
        changed = true;
      }
      if (iconBtn("ed.remove", icon::Delete, canDelete, canDelete ? T("ed.layerDelete") : T("ed.lastLayer"))) {
        deleteLayer(editLayer_);
      }
      if (iconBtn("ed.dupLayer", icon::Copy, static_cast<int>(cur.layers.size()) < kMaxLayers, T("ed.layerDup"))) {
        duplicateLayer();
      }
      if (iconBtn("ed.add", icon::Add, static_cast<int>(cur.layers.size()) < kMaxLayers, T("ed.layerAdd"))) {
        Layer l;
        l.shape = Shape::Dot;
        l.size = even ? 2.f : 3.f;  // symmetric on the shared centre
        cur.layers.push_back(l);
        editLayer_ = static_cast<int>(cur.layers.size()) - 1;
        changed = true;
      }
    }
    y += 24 + 6;

    int deleteRow = -1;
    for (size_t i = 0; i < cur.layers.size(); ++i) {
      Layer& L = cur.layers[i];
      const int idx = static_cast<int>(i);
      const D2D1_RECT_F row = Rc(lx0, y, lx1, y + 36);
      const bool sel = idx == editLayer_;
      const D2D1_RECT_F eye = Rc(lx1 - 54, y + 6, lx1 - 30, y + 30);
      const D2D1_RECT_F bin = Rc(lx1 - 28, y + 6, lx1 - 4, y + 30);
      const bool hover = ui_.Hover(row);
      const bool icons = hover || sel;
      // The row is evaluated first: a later ClickArea takes over the press, so the icons win.
      const bool rowClick = ui_.ClickArea(MakeId("ed.layer", idx), row);
      const bool eyeClick = (icons || L.hidden) && ui_.ClickArea(MakeId("ed.eye", idx), eye);
      const bool binClick = icons && canDelete && ui_.ClickArea(MakeId("ed.del", idx), bin);
      if (sel) ui_.FillRound(row, ui::kRadiusCtl, t.field);
      else if (hover) ui_.FillRound(row, ui::kRadiusCtl, t.sel);
      const D2D1_POINT_2F dot{lx0 + 17, y + 18};
      ui_.Circle(dot, 5.5f, t.dark ? D2D1::ColorF(1, 1, 1, 0.16f) : D2D1::ColorF(0, 0, 0, 0.16f));
      ui_.Circle(dot, 4.5f, L.hidden ? t.track : ToD2D(L.fill));
      ui_.Text(T(Key("shape.", ShapeKey(L.shape))), Rc(lx0 + 32, y, eye.left - 4, y + 36), sel ? Font::BodyBold : Font::Body,
               L.hidden ? WithAlpha(t.ink3, 0.75f) : (sel ? t.ink : t.ink2));
      if (icons || L.hidden) {
        const bool eh = ui_.Hover(eye);
        if (eh) ui_.FillRound(eye, ui::kRadiusCtl - 2, t.sel);
        // A hidden layer always shows the crossed-out eye.
        ui_.Glyph(L.hidden ? icon::Hidden : icon::Eye, eye, eh ? t.ink : (L.hidden ? t.ink2 : t.ink3));
        Tip(eye, L.hidden ? T("ed.layerShow") : T("ed.layerHide"));
      }
      if (icons) {
        const bool bh = canDelete && ui_.Hover(bin);
        if (bh) ui_.FillRound(bin, ui::kRadiusCtl - 2, t.sel);
        ui_.Glyph(icon::Delete, bin, !canDelete ? WithAlpha(t.ink3, 0.45f) : (bh ? t.acc : t.ink3));
        Tip(bin, canDelete ? T("ed.layerDelete") : T("ed.lastLayer"));
      }
      if (eyeClick) {
        L.hidden = !L.hidden;
        changed = true;
      } else if (binClick) {
        deleteRow = idx;
      } else if (rowClick) {
        editLayer_ = idx;
      }
      y += 38;
    }
    if (deleteRow >= 0) deleteLayer(deleteRow);
    contentH = y + off - area.top + 8;
    ui_.EndScroll(view, off, contentH);
  }

  // Centre: fixed-size canvas with its controls underneath.
  {
    const D2D1_RECT_F canvas = Rc(cx0, area.top, cx0 + S, area.top + S);
    // The canvas shows the draft (hidden layers excluded), drawn by the overlay's rasteriser.
    const Preview* dp = DraftPreview();
    DrawBitmap(dp, canvas, kZooms[zoom_], backdrop_, backdrop_ == 0, 0.86f, ui::kRadiusCard);
    const float y2 = canvas.bottom + 12;
    const D2D1_RECT_F inset = Rc(cx0, y2, cx0 + 64, y2 + 64);
    DrawBitmap(dp, inset, 1, backdrop_ == 2 ? 2 : 1, false);
    ui_.Label(L"1:1", Rc(inset.left + 7, inset.top + 4, inset.right, inset.top + 18),
              backdrop_ == 2 ? D2D1::ColorF(0, 0, 0, 0.45f) : t.tileInk);
    const std::vector<std::wstring> backs = {T("ed.bgGrid"), T("ed.bgDark"), T("ed.bgLight")};
    std::vector<std::wstring> zooms;
    for (const float z : kZooms) zooms.push_back(std::to_wstring(static_cast<int>(z)) + L"×");
    const float segL = inset.right + 12;
    ui_.Segmented(MakeId("ed.backdrop"), Rc(segL, y2, canvas.right, y2 + 30), backs, &backdrop_);
    ui_.Segmented(MakeId("ed.zoom"), Rc(segL, y2 + 34, canvas.right, y2 + 64), zooms, &zoom_);
  }

  // Right column: layer inspector.
  {
    const D2D1_RECT_F view = Rc(rx0 - 4, area.top, rx1 + 14, area.bottom);
    static float contentH = 600;
    const float off = ui_.BeginScroll(view, &propsScroll_, contentH);
    if (!cur.layers.empty()) {
      const bool hasDot = std::any_of(cur.layers.begin(), cur.layers.end(), IsCenterDot);
      int dotRequest = 0;
      const float end =
          LayerProps(cur.layers[editLayer_], rx0, rx1, area.top - off, changed, hasDot, &dotRequest, even, cur.scale);
      contentH = end + off - area.top + 8;
      // Applied after LayerProps: the layer reference it holds would not survive a reallocation.
      if (dotRequest > 0 && !hasDot) {
        if (static_cast<int>(cur.layers.size()) >= kMaxLayers) {
          Toast(T("toast.layerLimit"));
        } else {
          const Layer& base = cur.layers.front();
          Layer dot;
          dot.shape = Shape::Dot;
          // Parity of the shared centre: odd lines take a 3 px dot, even lines a 2 px one.
          dot.size = even ? 2.f : 3.f;
          dot.round = false;
          dot.fill = base.fill;
          dot.opacity = base.opacity;
          dot.outline = base.outline;
          dot.outlineThickness = base.outlineThickness;
          dot.outlineColor = base.outlineColor;
          dot.outlineOpacity = base.outlineOpacity;
          cur.layers.push_back(dot);
          changed = true;
        }
      } else if (dotRequest < 0) {
        int sel = editLayer_;
        for (int i = static_cast<int>(cur.layers.size()) - 1; i >= 0; --i) {
          if (IsCenterDot(cur.layers[i]) && cur.layers.size() > 1) {
            cur.layers.erase(cur.layers.begin() + i);
            if (i < sel) --sel;
          }
        }
        editLayer_ = std::clamp(sel, 0, static_cast<int>(cur.layers.size()) - 1);
        changed = true;
      }
    }
    ui_.EndScroll(view, off, contentH);
  }

  if (changed) EditApply(cur);
}

float SettingsApp::ShapePicker(Layer& L, float x0, float x1, float y, bool& changed, int* hovered) {
  const auto& t = ui_.th();
  constexpr float cell = 34, gap = 4;
  const int cols = std::max(1, static_cast<int>((x1 - x0 + gap) / (cell + gap)));
  *hovered = -1;
  for (int i = 0; i < static_cast<int>(Shape::Count); ++i) {
    const int col = i % cols, row = i / cols;
    const D2D1_RECT_F r = Rc(x0 + col * (cell + gap), y + row * (cell + gap), x0 + col * (cell + gap) + cell,
                             y + row * (cell + gap) + cell);
    const bool sel = L.shape == static_cast<Shape>(i);
    const bool hover = ui_.Hover(r);
    if (hover) *hovered = i;
    if (ui_.ClickArea(MakeId("ed.shape", i), r) && !sel) {
      L.shape = static_cast<Shape>(i);
      changed = true;
    }
    ui_.FillRound(r, ui::kRadiusCtl, t.tile);
    if (hover && !sel) ui_.FillRound(r, ui::kRadiusCtl, D2D1::ColorF(1, 1, 1, 0.08f));
    DrawBitmap(ShapeIcon(static_cast<Shape>(i)), Rc(r.left + 3, r.top + 3, r.right - 3, r.bottom - 3), 2, -1, false, 1.f, 0);
    if (sel) ui_.StrokeRound(Rc(r.left - 2, r.top - 2, r.right + 2, r.bottom + 2), ui::kRadiusCtl + 2, t.acc, 2.f);
  }
  const int rows = (static_cast<int>(Shape::Count) + cols - 1) / cols;
  return y + rows * (cell + gap) - gap;
}

float SettingsApp::LayerProps(Layer& L, float x0, float x1, float y, bool& changed, bool hasDot, int* dotRequest,
                              bool evenCenter, float scale) {
  const auto& t = ui_.th();
  const float labelW = std::clamp((x1 - x0) * 0.34f, 96.f, 124.f);
  constexpr float rowH = 28, stride = 30;

  const auto slider = [&](const char* id, const wchar_t* label, float* v, float mn, float mx, float step, int dec,
                          const wchar_t* unit, bool centered = false, const std::wstring& note = {}) {
    if (ui_.Slider(MakeId(id), Rc(x0, y, x1, y + rowH), label, v, mn, mx, step, dec, unit, centered, labelW, note)) {
      changed = true;
    }
    y += stride;
  };
  // Pixel-snapped sizes of the other parity than the shared centre are drawn 1 px larger
  // (SymmetricPixels); the inspector shows the size actually drawn.
  const auto effectiveNote = [&](float* field) -> std::wstring {
    const bool line = L.shape == Shape::Cross || L.shape == Shape::Plus || L.shape == Shape::XCross ||
                      L.shape == Shape::TShape || L.shape == Shape::Line;
    const bool squareDot = L.shape == Shape::Dot && !(L.round && std::lround(L.size * scale) >= 3);
    if (!((line && field == &L.thickness) || (squareDot && field == &L.size))) return {};
    const int px = std::max(1, static_cast<int>(std::lround(*field * scale)));
    const int eff = SymmetricPixels(px, evenCenter);
    return eff != px ? Fmt(L"→ %d", eff) : std::wstring();
  };
  const auto switchRow = [&](const char* id, const wchar_t* label, bool* v) {
    const float mid = std::round(y + rowH / 2);
    ui_.Text(label, Rc(x0, y, x1 - 48, y + rowH), Font::Small, t.ink2);
    if (ui_.Switch(MakeId(id), Rc(x1 - 32, mid - 9, x1, mid + 9), v)) changed = true;
    y += stride;
  };
  // Collapsible section heading; `reserve` keeps room on the right for an inline control.
  const auto header = [&](int idx, const wchar_t* title, const std::wstring& summary, float reserve, bool collapsible = true) {
    const D2D1_RECT_F hit = Rc(x0, y, x1 - reserve, y + 28);
    const bool hover = collapsible && ui_.Hover(hit);
    if (collapsible && ui_.ClickArea(MakeId("ed.sec", idx), hit)) edOpen_[idx] = !edOpen_[idx];
    const bool open = edOpen_[idx];
    ui_.Label(title, Rc(x0, y, x1, y + 28), hover ? t.ink : t.ink2);
    const float chevR = x1 - reserve;
    if (collapsible) {
      ui_.Glyph(open ? icon::ChevronDown : icon::Chevron, Rc(chevR - 16, y, chevR, y + 28), hover ? t.ink : t.ink3);
    }
    if (!open && !summary.empty()) ui_.Text(summary, Rc(x0 + 90, y, chevR - 24, y + 28), Font::Mono, t.ink3, Align::Right);
    y += 32;
    return open;
  };

  // Shape: compact icon grid, the chosen (or hovered) name shown once.
  {
    int hovered = -1;
    const float gridTop = y + 26;
    const float end = ShapePicker(L, x0 + 2, x1, gridTop, changed, &hovered);
    const Shape shown = hovered >= 0 ? static_cast<Shape>(hovered) : L.shape;
    ui_.Label(T("ed.shape"), Rc(x0, y, x1, y + 18), t.ink2);
    ui_.Text(T(Key("shape.", ShapeKey(shown))), Rc(x0, y, x1, y + 18), Font::Small, hovered >= 0 ? t.ink3 : t.ink,
             Align::Right);
    y = end + 22;
  }

  // Size: geometry of the current shape.
  {
    const std::vector<Param> params = ShapeParams(L);
    std::wstring summary;
    const bool splitV = HasVerticalLength(L.shape) && L.lengthV > 0;
    for (const Param& p : params) {
      summary += (summary.empty() ? L"" : L" · ") + Num(L.*(p.field), p.dec);
      if (splitV && p.field == &Layer::length) summary += L"/" + Num(L.lengthV, 0);
    }
    if (L.shape == Shape::Segments) summary += Fmt(L" · ×%d", L.segments);
    if (header(0, T("lbl.size"), summary, 0)) {
      for (const Param& p : params) {
        const bool isLength = HasVerticalLength(L.shape) && p.field == &Layer::length;
        float* field = &(L.*(p.field));
        slider(p.id, isLength && splitV ? T("lbl.lengthH") : T(p.label), field, p.mn, p.mx, p.step, p.dec, p.unit, false,
               effectiveNote(field));
        if (isLength) {
          // Separate vertical rays: lengthV == 0 means "same as length".
          bool split = splitV;
          if (ui_.Switch(MakeId("p.splitv"), Rc(x1 - 32, std::round(y + rowH / 2) - 9, x1, std::round(y + rowH / 2) + 9), &split)) {
            L.lengthV = split ? std::max(1.f, L.length) : 0.f;
            changed = true;
          }
          ui_.Text(T("lbl.splitV"), Rc(x0, y, x1 - 48, y + rowH), Font::Small, t.ink2);
          y += stride;
          if (L.lengthV > 0) slider("p.lenv", T("lbl.lengthV"), &L.lengthV, 1, 60, 1, 0, L" px");
        }
        if (L.shape == Shape::Segments && p.field == &Layer::thickness) {
          float n = static_cast<float>(L.segments);
          if (ui_.Slider(MakeId("p.segs"), Rc(x0, y, x1, y + rowH), T("lbl.segments"), &n, 2, 12, 1, 0, {}, false, labelW)) {
            L.segments = static_cast<int>(n);
            changed = true;
          }
          y += stride;
        }
      }
      if (L.shape == Shape::Cross || L.shape == Shape::XCross) {
        ui_.Text(T("lbl.rays"), Rc(x0, y, x0 + labelW, y + rowH), Font::Small, t.ink2);
        const wchar_t* arrows[] = {L"↑", L"→", L"↓", L"←"};
        for (int i = 0; i < 4; ++i) {
          const float bx = x0 + labelW + 6 + i * 32;
          if (ui_.Chip(MakeId("p.ray", i), Rc(bx, y + 2, bx + 28, y + rowH - 2), arrows[i], (L.rays & (1 << i)) != 0)) {
            L.rays ^= 1 << i;
            changed = true;
          }
        }
        y += stride;
      }
      switch (L.shape) {
        case Shape::Cross:
        case Shape::XCross:
        case Shape::TShape:
        case Shape::Plus: switchRow("p.round", T("lbl.roundCaps"), &L.round); break;
        case Shape::Dot: switchRow("p.round", T("lbl.roundDot"), &L.round); break;
        case Shape::Box:
        case Shape::Diamond:
        case Shape::Triangle: switchRow("p.filled", T("lbl.filled"), &L.filled); break;
        default: break;
      }
      if (CrossLike(L.shape)) {
        bool dot = hasDot;
        const float mid = std::round(y + rowH / 2);
        ui_.Text(T("lbl.centerDot"), Rc(x0, y, x1 - 48, y + rowH), Font::Small, t.ink2);
        if (ui_.Switch(MakeId("p.cdot"), Rc(x1 - 32, mid - 9, x1, mid + 9), &dot)) *dotRequest = dot ? 1 : -1;
        y += stride;
      }
    }
    y += 10;
  }

  // Colour: hex in the heading, one row of swatches, opacity.
  {
    const std::wstring hex = Widen(ColorToHex(L.fill));
    const bool open = edOpen_[1];
    const float hexW = 100;
    if (open) {
      if (!ui_.IsFocused(MakeId("p.hex"))) editHex_ = hex;
      const D2D1_RECT_F hf = Rc(x1 - hexW, y + 1, x1, y + 27);
      if (ui_.TextField(MakeId("p.hex"), Rc(hf.left + 26, hf.top, hf.right, hf.bottom), &editHex_, L"#RRGGBB", 9, nullptr,
                        true)) {
        bool ok = false;
        const Color c = ColorFromHex(Narrow(editHex_), &ok);
        if (ok) {
          L.fill = c;
          changed = true;
        }
      }
      ColorChip(Rc(hf.left, hf.top + 3, hf.left + 20, hf.bottom - 3), L.fill, 0);
    } else {
      const float tw = std::ceil(ui_.Measure(hex, Font::Mono));
      ui_.FillRound(Rc(x1 - 24 - tw - 22, y + 9, x1 - 24 - tw - 12, y + 19), 3, ToD2D(L.fill));
    }
    if (header(1, T("ed.color"), open ? std::wstring() : hex, open ? hexW + 12 : 0)) {
      const int n = static_cast<int>(std::size(kFillPalette));
      constexpr float sg = 5;
      const float sw = std::min(22.f, std::floor((x1 - x0 - 6 - sg * (n - 1)) / n));
      const float step = std::floor((x1 - x0 - 6 - sw) / (n - 1));
      for (int i = 0; i < n; ++i) {
        const float sx = x0 + 3 + i * step;
        if (ui_.Swatch(MakeId("p.fill", i), Rc(sx, y + 3, sx + sw, y + 3 + sw), ToD2D(kFillPalette[i]),
                       SameRgb(L.fill, kFillPalette[i]))) {
          L.fill = kFillPalette[i];
          changed = true;
        }
      }
      y += sw + 12;
      float op = L.opacity * 100;
      if (ui_.Slider(MakeId("p.op"), Rc(x0, y, x1, y + rowH), T("lbl.opacity"), &op, 5, 100, 5, 0, L"%", false, labelW)) {
        L.opacity = op / 100.f;
        changed = true;
      }
      y += stride;
    }
    y += 10;
  }

  // Outline: the switch lives in the heading; controls appear only when it is on.
  {
    const float hy = y;
    const std::wstring summary =
        L.outline ? Fmt(L"%s px · %s", Num(L.outlineThickness, 0).c_str(), Widen(ColorToHex(L.outlineColor)).c_str())
                  : std::wstring();
    // Open and on: colour chip and hex sit in the heading, left of the switch (same as the fill section).
    const bool showHex = edOpen_[2] && L.outline;
    constexpr float hexW = 100;
    if (showHex) {
      const ui::Id hexId = MakeId("p.ohex");
      if (!ui_.IsFocused(hexId)) editOutlineHex_ = Widen(ColorToHex(L.outlineColor));
      const D2D1_RECT_F hf = Rc(x1 - 44 - hexW, y + 1, x1 - 44, y + 27);
      if (ui_.TextField(hexId, Rc(hf.left + 26, hf.top, hf.right, hf.bottom), &editOutlineHex_, L"#RRGGBB", 9, nullptr,
                        true)) {
        bool ok = false;
        const Color c = ColorFromHex(Narrow(editOutlineHex_), &ok);
        if (ok) {
          L.outlineColor = c;
          changed = true;
        }
      }
      ColorChip(Rc(hf.left, hf.top + 3, hf.left + 20, hf.bottom - 3), L.outlineColor, 1);
    }
    const bool open = header(2, T("ed.outline"), summary, showHex ? 44 + hexW + 12 : 44, L.outline);
    const float mid = std::round(hy + 14);
    if (ui_.Switch(MakeId("p.ol"), Rc(x1 - 32, mid - 9, x1, mid + 9), &L.outline)) {
      changed = true;
      if (L.outline) edOpen_[2] = true;
    }
    if (open && L.outline) {
      slider("p.olt", T("lbl.thickness"), &L.outlineThickness, 1, 4, 1, 0, L" px");
      ui_.Text(T("ed.color"), Rc(x0, y, x0 + labelW, y + rowH), Font::Small, t.ink2);
      for (int i = 0; i < static_cast<int>(std::size(kOutlinePalette)); ++i) {
        const float sx = x0 + labelW + 9 + i * 28;
        const D2D1_RECT_F r = Rc(sx, y + 5, sx + 18, y + 23);
        if (ui_.Swatch(MakeId("p.olc", i), r, ToD2D(kOutlinePalette[i]), SameRgb(L.outlineColor, kOutlinePalette[i]))) {
          L.outlineColor = kOutlinePalette[i];
          changed = true;
        }
      }
      y += stride;
      float oop = L.outlineOpacity * 100;
      if (ui_.Slider(MakeId("p.oop"), Rc(x0, y, x1, y + rowH), T("lbl.opacity"), &oop, 5, 100, 5, 0, L"%", false, labelW)) {
        L.outlineOpacity = oop / 100.f;
        changed = true;
      }
      y += stride;
    }
    y += 10;
  }

  // Position: rotation and per-layer offset.
  {
    const std::wstring summary = Fmt(L"%d° · %d, %d", static_cast<int>(L.rotation), static_cast<int>(L.offsetX),
                                     static_cast<int>(L.offsetY));
    if (header(3, T("ed.position"), summary, 0)) {
      slider("p.rot", T("lbl.rotation"), &L.rotation, 0, 359, 1, 0, L"°");
      slider("p.ox", T("lbl.offsetX"), &L.offsetX, -40, 40, 1, 0, L" px", true);
      slider("p.oy", T("lbl.offsetY"), &L.offsetY, -40, 40, 1, 0, L" px", true);
    }
  }
  return y;
}

// ───────────────────────────── shared pickers ─────────────────────────────

void SettingsApp::CrosshairPicker(const std::string& key, D2D1_RECT_F f, std::string& id) {
  const auto& t = ui_.th();
  const bool hover = ui_.Hover(f);
  if (ui_.ClickArea(MakeId(key), f)) {
    popup_ = Popup::Picker;
    pickerOpen_ = key;
    popupAnchor_ = f;
    pickerSearch_.clear();
    pickerScroll_ = -1;  // scroll to the current choice on the first frame
    ui_.SetFocus(MakeId("pick.search"));
  }
  const bool open = popup_ == Popup::Picker && pickerOpen_ == key;
  if (open) {
    pickerTarget_ = &id;
    pickerSeen_ = true;
  }
  ui_.FillRound(f, ui::kRadiusCtl, t.field);
  if (hover && !open) ui_.FillRound(f, ui::kRadiusCtl, t.sel);
  if (open) ui_.StrokeRound(f, ui::kRadiusCtl, t.acc, 1.5f);
  const Crosshair& c = ResolveCrosshair(cfg_, id);
  const float h = f.bottom - f.top;
  const D2D1_RECT_F thumb = Rc(f.left + 4, f.top + 4, f.left + h - 4, f.bottom - 4);
  DrawPreview(c, thumb, 1, 0, false, 4);
  ui_.Text(Widen(c.name), Rc(thumb.right + 10, f.top, f.right - 28, f.bottom), Font::Body, t.ink);
  ui_.Glyph(open ? icon::ChevronUp : icon::ChevronDown, Rc(f.right - 26, f.top, f.right - 8, f.bottom), t.ink3);
}

// ───────────────────────────── hotkeys ─────────────────────────────

void SettingsApp::PageHotkeys(D2D1_RECT_F full) {
  const auto& t = ui_.th();
  const D2D1_RECT_F area = Rc(full.left + kPad, full.top, full.right - kPad, full.bottom);
  Settings& s = cfg_.settings;
  const float x0 = area.left, x1 = std::min(area.right, area.left + 860);
  static float contentH = 900;
  const D2D1_RECT_F view = Rc(x0 - 4, area.top, x1 + 14, area.bottom);
  const float off = ui_.BeginScroll(view, &scroll_[static_cast<int>(Page::Hotkeys)], contentH);
  float y = area.top - off;
  constexpr float bindW = 240, rowH = 60;

  const auto bind = [&](Capture which, const wchar_t* title, const wchar_t* hint, const std::wstring& value) {
    const D2D1_RECT_F row = Rc(x0, y, x1, y + rowH);
    const float cl = RowText(row, title, hint, bindW);
    const float mid = std::round((row.top + row.bottom) / 2);
    const D2D1_RECT_F b = Rc(cl, mid - 17, row.right - 16, mid + 17);
    const bool capturing = capture_ == which;
    const bool hover = ui_.Hover(b);
    if (ui_.ClickArea(MakeId("hk", static_cast<int>(which)), b) && !capturing) StartCapture(which);
    ui_.FillRound(b, ui::kRadiusCtl, t.field);
    if (hover && !capturing) ui_.FillRound(b, ui::kRadiusCtl, t.sel);
    if (capturing) ui_.StrokeRound(b, ui::kRadiusCtl, t.acc, 1.5f);
    if (capturing) ui_.Text(T("hk.pressKey"), Rc(b.left + 8, b.top, b.right - 8, b.bottom), Font::Small, t.acc, Align::Center);
    else if (value.empty()) ui_.Text(T("hk.unbound"), b, Font::Small, t.ink3, Align::Center);
    else ui_.Keycaps(value, b.right - 6, mid);
    y += rowH;
  };
  const auto card = [&](float h) { ui_.FillRound(Rc(x0, y, x1, y + h), ui::kRadiusCard, t.raised); };

  // Global shortcuts, nudge included.
  y = ui_.Section(x0, x1, y, T("hk.global"));
  card(5 * rowH + 8);
  y += 4;
  bind(Capture::Toggle, T("hk.toggle"), T("hk.toggleHint"), HotkeyToText(s.hkToggle));
  bind(Capture::Next, T("hk.next"), T("hk.nextHint"), HotkeyToText(s.hkNext));
  bind(Capture::Prev, T("hk.prev"), T("hk.prevHint"), HotkeyToText(s.hkPrev));
  bind(Capture::Settings, T("hk.settings"), T("hk.settingsHint"), HotkeyToText(s.hkSettings));
  {
    const D2D1_RECT_F row = Rc(x0, y, x1, y + rowH);
    const float nudgeW = std::min(340.f, (x1 - x0) * 0.45f);
    const float cl = RowText(row, T("hk.nudge"), T("hk.nudgeHint"), nudgeW);
    const float mid = std::round((row.top + row.bottom) / 2);
    int sel = -1;
    for (int i = 0; i < static_cast<int>(std::size(kNudgeMods)); ++i) {
      if (s.nudgeMods == kNudgeMods[i]) sel = i;
    }
    if (ui_.Segmented(MakeId("hk.nudge"), Rc(cl, mid - 16, row.right - 16, mid + 16),
                      {T("hk.nudgeOff"), L"Ctrl+Alt", L"Ctrl+Shift", L"Alt+Shift"}, &sel)) {
      s.nudgeMods = kNudgeMods[sel];
      PushConfig();
    }
    y += rowH;
  }
  y += 4 + 28;

  // Aim down sights.
  y = ui_.Section(x0, x1, y, T("hk.ads"));
  const bool sw = s.adsAction == AdsAction::Switch;
  constexpr float segH = 52;
  card(rowH + 2 * segH + (sw ? segH : 0) + 8);
  y += 4;
  bind(Capture::Ads, T("hk.adsKey"), T("hk.adsKeyHint"), s.adsKey ? KeyName(s.adsKey) : std::wstring());
  const float actionW = std::min(440.f, x1 - x0 - 260);
  {
    const D2D1_RECT_F row = Rc(x0, y, x1, y + segH);
    const float cl = RowText(row, T("hk.adsMode"), nullptr, 240);
    const float mid = std::round((row.top + row.bottom) / 2);
    int mode = static_cast<int>(s.adsMode);
    if (ui_.Segmented(MakeId("hk.mode"), Rc(cl, mid - 16, row.right - 16, mid + 16), {T("hk.hold"), T("hk.toggleMode")}, &mode)) {
      s.adsMode = static_cast<AdsMode>(mode);
      PushConfig();
    }
    y += segH;
  }
  {
    const D2D1_RECT_F row = Rc(x0, y, x1, y + segH);
    const float cl = RowText(row, T("hk.adsAction"), nullptr, actionW);
    const float mid = std::round((row.top + row.bottom) / 2);
    int action = static_cast<int>(s.adsAction);
    if (ui_.Segmented(MakeId("hk.action"), Rc(cl, mid - 16, row.right - 16, mid + 16),
                      {T("hk.actNone"), T("hk.actHide"), T("hk.actShow"), T("hk.actSwitch")}, &action)) {
      s.adsAction = static_cast<AdsAction>(action);
      PushConfig();
    }
    y += segH;
  }
  if (sw) {
    const D2D1_RECT_F row = Rc(x0, y, x1, y + segH);
    const float cl = RowText(row, T("hk.actSwitch"), nullptr, actionW);
    const float mid = std::round((row.top + row.bottom) / 2);
    CrosshairPicker("hk.adsPick", Rc(cl, mid - 17, row.right - 16, mid + 17), s.adsCrosshairId);
    y += segH;
  }
  y += 4 + 24;

  y += ui_.Paragraph(T("hk.note"), Rc(x0 + 2, y, std::min(x1, x0 + 720), y + 400), Font::Small, t.ink3) + 24;
  contentH = y + off - area.top;
  ui_.EndScroll(view, off, contentH);
}

// ───────────────────────────── profiles ─────────────────────────────

float SettingsApp::ExeList(const char* key, std::vector<std::wstring>& list, float x0, float x1, float y,
                           PickTarget target, std::wstring& draft) {
  const auto& t = ui_.th();
  float cx = x0;
  int removeIdx = -1;
  for (size_t i = 0; i < list.size(); ++i) {
    const float w = std::round(ui_.Measure(list[i], Font::Mono) + 44);
    if (cx + w > x1 && cx > x0) {
      cx = x0;
      y += 36;
    }
    const D2D1_RECT_F r = Rc(cx, y, cx + w, y + 30);
    ui_.FillRound(r, ui::kRadiusCtl, t.raised);
    ui_.Text(list[i], Rc(cx + 12, y, cx + w - 26, y + 30), Font::Mono, t.ink);
    const D2D1_RECT_F x = Rc(cx + w - 28, y + 3, cx + w - 4, y + 27);
    ui_.Glyph(icon::Close, x, ui_.Hover(x) ? t.acc : t.ink3);
    if (ui_.ClickArea(MakeId(key, 100 + static_cast<int>(i)), x)) removeIdx = static_cast<int>(i);
    cx += w + 8;
  }
  if (list.empty()) ui_.Text(T("prof.empty"), Rc(x0, y, x1, y + 30), Font::Small, t.ink3);
  y += 30 + 14;

  bool submitted = false;
  ui_.TextField(MakeId(key, 1), Rc(x0, y, x0 + 240, y + 34), &draft, T("prof.exePlaceholder"), 64, &submitted, true);
  if (ui_.Button(MakeId(key, 2), Rc(x0 + 248, y, x0 + 352, y + 34), T("btn.add"), Btn::Secondary, true, icon::Add) ||
      submitted) {
    if (Trim(draft).empty()) {
      // Nothing typed: offer the running apps instead of silently doing nothing.
      pickTarget_ = target;
      running_ = RunningApps();
      Toast(T("toast.pickExe"));
    } else if (AddExe(list, draft)) {
      PushConfig();
    }
    draft.clear();
  }
  if (ui_.Button(MakeId(key, 3), Rc(x0 + 360, y, x0 + 520, y + 34), T("btn.fromRunning"),
                 pickTarget_ == target ? Btn::Secondary : Btn::Ghost)) {
    pickTarget_ = pickTarget_ == target ? PickTarget::None : target;
    if (pickTarget_ != PickTarget::None) running_ = RunningApps();
  }
  y += 34 + 12;
  if (pickTarget_ == target) {
    cx = x0;
    for (size_t i = 0; i < running_.size(); ++i) {
      const float w = std::round(ui_.Measure(running_[i], Font::Small) + 24);
      if (cx + w > x1 && cx > x0) {
        cx = x0;
        y += 34;
      }
      if (ui_.Chip(MakeId(key, 1000 + static_cast<int>(i)), Rc(cx, y, cx + w, y + 28), running_[i], false)) {
        if (AddExe(list, running_[i])) PushConfig();
        pickTarget_ = PickTarget::None;
      }
      cx += w + 6;
    }
    if (running_.empty()) ui_.Text(T("prof.noRunning"), Rc(x0, y, x1, y + 28), Font::Small, t.ink3);
    y += 28 + 16;
  }
  if (removeIdx >= 0) {
    list.erase(list.begin() + removeIdx);
    PushConfig();
  }
  return y;
}

void SettingsApp::PageProfiles(D2D1_RECT_F full) {
  const auto& t = ui_.th();
  const D2D1_RECT_F area = Rc(full.left + kPad, full.top, full.right - kPad, full.bottom);
  Settings& s = cfg_.settings;
  const float x0 = area.left, x1 = std::min(area.right, area.left + 880);
  static float contentH = 1000;
  const D2D1_RECT_F view = Rc(x0 - 4, area.top, x1 + 14, area.bottom);
  const float off = ui_.BeginScroll(view, &scroll_[static_cast<int>(Page::Profiles)], contentH);
  float y = area.top - off;

  // Per-game profiles as soft rows.
  ui_.Text(T("prof.gamesHint"), Rc(x0, y, x1, y + 20), Font::Body, t.ink2);
  y += 20 + 16;
  const float colPick = x0 + std::min(280.f, (x1 - x0) * 0.34f), colOff = x1 - 182;
  int removeIdx = -1;
  for (size_t i = 0; i < s.profiles.size(); ++i) {
    GameProfile& p = s.profiles[i];
    const float rowTop = y;
    ui_.FillRound(Rc(x0, y, x1, y + 52), ui::kRadiusCard, t.raised);
    ui_.Text(p.exe, Rc(x0 + 16, y, colPick - 16, y + 52), Font::Mono, t.ink);
    CrosshairPicker("prof.pick." + std::to_string(i), Rc(colPick, y + 8, colOff - 16, y + 44), p.crosshairId);
    // Per-game offset on top of the global one (the nudge hotkeys change the same values).
    for (int axis = 0; axis < 2; ++axis) {
      int& v = axis == 0 ? p.offsetX : p.offsetY;
      const float fx = colOff + axis * 66;
      ui_.Text(axis == 0 ? L"X" : L"Y", Rc(fx, rowTop, fx + 14, rowTop + 52), Font::Mono, t.ink3);
      const ui::Id id = MakeId("prof.off", static_cast<int>(i) * 2 + axis);
      const bool was = ui_.IsFocused(id);
      std::wstring shown = std::to_wstring(v);
      std::wstring* text = was ? &profOffText_ : &shown;
      const D2D1_RECT_F field = Rc(fx + 14, rowTop + 11, fx + 60, rowTop + 41);
      // Wheel over the X/Y field (or its letter) nudges the offset by 1 px.
      if (!was) {
        if (const int steps = ui_.WheelSteps(Rc(fx, rowTop + 6, fx + 60, rowTop + 46))) {
          v = std::clamp(v + steps, -2000, 2000);
          PushConfig();
        }
      }
      const bool edited = ui_.TextField(id, field, text, L"0", 6, nullptr, true);
      if (!was && ui_.IsFocused(id)) profOffText_ = shown;  // focus just arrived: edit a copy of the value
      if (edited && was) {
        profOffText_.erase(std::remove_if(profOffText_.begin(), profOffText_.end(),
                                          [](wchar_t c) { return !(iswdigit(c) || c == L'-'); }),
                           profOffText_.end());
        const int n = profOffText_.empty() || profOffText_ == L"-" ? 0 : std::clamp(_wtoi(profOffText_.c_str()), -2000, 2000);
        if (n != v) {
          v = n;
          PushConfig();
        }
      }
    }
    if (ui_.Button(MakeId("prof.del", static_cast<int>(i)), Rc(x1 - 40, rowTop + 12, x1 - 12, rowTop + 40), {}, Btn::Ghost,
                   true, icon::Close)) {
      removeIdx = static_cast<int>(i);
    }
    y = rowTop + 52 + 6;
  }
  if (s.profiles.empty()) {
    ui_.FillRound(Rc(x0, y, x1, y + 52), ui::kRadiusCard, t.raised);
    ui_.Text(T("prof.empty"), Rc(x0 + 16, y, x1, y + 52), Font::Small, t.ink3);
    y += 52 + 6;
  }
  if (removeIdx >= 0) {
    s.profiles.erase(s.profiles.begin() + removeIdx);
    pickerOpen_.clear();
    PushConfig();
  }
  y += 10;
  {
    std::vector<std::wstring> scratch;
    for (const auto& p : s.profiles) scratch.push_back(p.exe);
    const size_t before = scratch.size();
    bool submitted = false;
    ui_.TextField(MakeId("prof.new"), Rc(x0, y, x0 + 240, y + 34), &draftProfile_, T("prof.exePlaceholder"), 64,
                  &submitted, true);
    bool add = ui_.Button(MakeId("prof.add"), Rc(x0 + 248, y, x0 + 352, y + 34), T("btn.add"), Btn::Secondary, true,
                          icon::Add) ||
               submitted;
    if (add && Trim(draftProfile_).empty()) {
      // Nothing typed: offer the running apps instead of silently doing nothing.
      add = false;
      pickTarget_ = PickTarget::Profile;
      running_ = RunningApps();
      Toast(T("toast.pickExe"));
    }
    if (ui_.Button(MakeId("prof.run"), Rc(x0 + 360, y, x0 + 520, y + 34), T("btn.fromRunning"),
                   pickTarget_ == PickTarget::Profile ? Btn::Secondary : Btn::Ghost)) {
      pickTarget_ = pickTarget_ == PickTarget::Profile ? PickTarget::None : PickTarget::Profile;
      if (pickTarget_ != PickTarget::None) running_ = RunningApps();
    }
    y += 34 + 12;
    if (add) AddExe(scratch, draftProfile_);
    if (pickTarget_ == PickTarget::Profile) {
      float cx = x0;
      for (size_t i = 0; i < running_.size(); ++i) {
        const float w = std::round(ui_.Measure(running_[i], Font::Small) + 24);
        if (cx + w > x1 && cx > x0) {
          cx = x0;
          y += 34;
        }
        if (ui_.Chip(MakeId("prof.r", static_cast<int>(i)), Rc(cx, y, cx + w, y + 28), running_[i], false)) {
          AddExe(scratch, running_[i]);
          pickTarget_ = PickTarget::None;
        }
        cx += w + 6;
      }
      if (running_.empty()) ui_.Text(T("prof.noRunning"), Rc(x0, y, x1, y + 28), Font::Small, t.ink3);
      y += 28 + 16;
    }
    if (scratch.size() > before) {
      GameProfile p;
      p.exe = scratch.back();
      p.crosshairId = cfg_.activeId;
      s.profiles.push_back(p);
      draftProfile_.clear();
      PushConfig();
    }
  }
  y += 28;

  // Process filter.
  y = ui_.Section(x0, x1, y, T("prof.filter"));
  int fm = static_cast<int>(s.filterMode);
  if (ui_.Segmented(MakeId("prof.fm"), Rc(x0, y, x0 + std::min(420.f, x1 - x0), y + 34),
                    {T("prof.fmAll"), T("prof.fmWhite"), T("prof.fmBlack")}, &fm)) {
    s.filterMode = static_cast<FilterMode>(fm);
    PushConfig();
  }
  y += 34 + 10;
  ui_.Text(s.filterMode == FilterMode::Whitelist   ? T("prof.fmWhiteHint")
           : s.filterMode == FilterMode::Blacklist ? T("prof.fmBlackHint")
                                                   : T("prof.fmAllHint"),
           Rc(x0, y, x1, y + 18), Font::Small, t.ink3);
  y += 18 + 16;
  if (s.filterMode != FilterMode::All) y = ExeList("prof.flist", s.filterList, x0, x1, y, PickTarget::Filter, draftFilter_);
  y += 16;

  // Competitive-safe list.
  y = ui_.Section(x0, x1, y, T("prof.safe"));
  y += ui_.Paragraph(T("prof.safeHint"), Rc(x0, y, std::min(x1, x0 + 720), y + 80), Font::Small, t.ink3) + 14;
  y = ExeList("prof.slist", s.safeList, x0, x1, y, PickTarget::Safe, draftSafe_);

  contentH = y + off - area.top + 16;
  ui_.EndScroll(view, off, contentH);
}

// ───────────────────────────── safety ─────────────────────────────

void SettingsApp::PageSafety(D2D1_RECT_F full) {
  const auto& t = ui_.th();
  const D2D1_RECT_F area = Rc(full.left + kPad, full.top, full.right - kPad, full.bottom);
  const float x0 = area.left, x1 = std::min(area.right, area.left + 960);
  static float contentH = 800;
  const D2D1_RECT_F view = Rc(x0 - 4, area.top, x1 + 14, area.bottom);
  const float off = ui_.BeginScroll(view, &scroll_[static_cast<int>(Page::Safety)], contentH);
  float y = area.top - off;
  const float mid = std::round((x0 + x1) / 2);

  // Two soft cards; heights are measured first so the backgrounds are drawn under the text.
  const auto column = [&](float cx0, float cx1, const wchar_t* title, std::initializer_list<const char*> keys, wchar_t mark,
                          bool draw, float cardH) {
    if (draw) ui_.FillRound(Rc(cx0, y, cx1, y + cardH), ui::kRadiusCard, t.raised);
    float yy = y + 20;
    if (draw) ui_.Text(title, Rc(cx0 + 20, yy, cx1 - 20, yy + 20), Font::BodyBold, t.ink);
    yy += 20 + 14;
    for (const char* k : keys) {
      if (draw) ui_.Text(std::wstring(1, mark), Rc(cx0 + 20, yy, cx0 + 36, yy + 18), Font::Body, mark == L'+' ? t.acc : t.ink3);
      const float h = ui_.Paragraph(T(k), Rc(cx0 + 40, yy, cx1 - 20, yy + 120), Font::Body, t.ink2, draw);
      yy += std::max(18.f, h) + 12;
    }
    return yy + 8 - y;
  };
  const auto does = {"safe.does1", "safe.does2", "safe.does3", "safe.does4"};
  const auto nots = {"safe.not1", "safe.not2", "safe.not3", "safe.not4", "safe.not5"};
  const float h = std::max(column(x0, mid - 8, T("safe.doesTitle"), does, L'+', false, 0),
                           column(mid + 8, x1, T("safe.notTitle"), nots, L'−', false, 0));
  column(x0, mid - 8, T("safe.doesTitle"), does, L'+', true, h);
  column(mid + 8, x1, T("safe.notTitle"), nots, L'−', true, h);
  y += h + 16;

  // Honest risk note.
  {
    const float ph = ui_.Paragraph(T("safe.warn"), Rc(x0 + 52, 0, x1 - 24, 400), Font::Body, t.ink2, false);
    const D2D1_RECT_F warn = Rc(x0, y, x1, y + 20 + 22 + ph + 20);
    ui_.FillRound(warn, ui::kRadiusCard, t.field);
    ui_.Glyph(icon::Warning, Rc(x0 + 20, y + 20, x0 + 36, y + 40), t.acc, true);
    ui_.Text(T("safe.warnTitle"), Rc(x0 + 52, y + 20, x1 - 24, y + 40), Font::BodyBold, t.ink);
    ui_.Paragraph(T("safe.warn"), Rc(x0 + 52, y + 42, x1 - 24, y + 400), Font::Body, t.ink2);
    y = warn.bottom + 28;
  }

  // Live core status.
  y = ui_.Section(x0, x1, y, T("safe.status"));
  std::wstring status;
  bool running = false;
  if (HWND core = ipc::CoreWindow()) {
    running = true;
    DWORD pid = 0;
    GetWindowThreadProcessId(core, &pid);
    bool elevated = false;
    if (HANDLE hp = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
      elevated = IsProcessElevated(hp);
      CloseHandle(hp);
    }
    status = Fmt(L"%s  ·  PID %lu  ·  %s", T("safe.coreRunning"), pid, elevated ? T("safe.elevated") : T("safe.normal"));
  } else {
    status = T("safe.coreStopped");
  }
  ui_.FillRound(Rc(x0, y, x1, y + 48), ui::kRadiusCard, t.raised);
  ui_.Circle({x0 + 24, y + 24}, 4, running ? t.acc : t.ink3);
  ui_.Text(status, Rc(x0 + 40, y, x1 - 16, y + 48), Font::Body, t.ink);
  y += 48 + 24;

  contentH = y + off - area.top;
  ui_.EndScroll(view, off, contentH);
}

// ───────────────────────────── settings ─────────────────────────────

void SettingsApp::PageSettings(D2D1_RECT_F full) {
  const auto& t = ui_.th();
  const D2D1_RECT_F area = Rc(full.left + kPad, full.top, full.right - kPad, full.bottom);
  Settings& s = cfg_.settings;
  const float x0 = area.left, x1 = std::min(area.right, area.left + 780);
  static float contentH = 900;
  const D2D1_RECT_F view = Rc(x0 - 4, area.top, x1 + 14, area.bottom);
  const float off = ui_.BeginScroll(view, &scroll_[static_cast<int>(Page::Settings)], contentH);
  float y = area.top - off;
  const auto card = [&](float h) { ui_.FillRound(Rc(x0, y, x1, y + h), ui::kRadiusCard, t.raised); };
  constexpr float rowH = 56;

  // Interface.
  y = ui_.Section(x0, x1, y, T("set.interface"));
  card(2 * rowH + 8);
  y += 4;
  {
    const D2D1_RECT_F row = Rc(x0, y, x1, y + rowH);
    const float cl = RowText(row, T("set.language"), nullptr, 260);
    const float mid = std::round((row.top + row.bottom) / 2);
    int lang = s.language == "en" ? 1 : 0;
    if (ui_.Segmented(MakeId("set.lang"), Rc(cl, mid - 16, row.right - 16, mid + 16), {L"Українська", L"English"}, &lang)) {
      s.language = lang == 1 ? "en" : "uk";
      SetLanguage(s.language);
      PushConfig();
    }
    y += rowH;
  }
  {
    const D2D1_RECT_F row = Rc(x0, y, x1, y + rowH);
    const float cl = RowText(row, T("set.theme"), nullptr, 320);
    const float mid = std::round((row.top + row.bottom) / 2);
    int theme = s.theme == "light" ? 1 : (s.theme == "dark" ? 2 : 0);
    if (ui_.Segmented(MakeId("set.theme"), Rc(cl, mid - 16, row.right - 16, mid + 16),
                      {T("set.themeSystem"), T("set.themeLight"), T("set.themeDark")}, &theme)) {
      s.theme = theme == 1 ? "light" : (theme == 2 ? "dark" : "system");
      pendingTheme_ = true;  // applied after EndDraw: DWM calls may repaint synchronously
      PushConfig();
    }
    y += rowH;
  }
  y += 4 + 28;

  // Position.
  y = ui_.Section(x0, x1, y, T("set.position"));
  const std::vector<MonitorDesc> mons = ListMonitors();
  // Settings::monitor: -1 game's monitor, 0 primary, n >= 1 ListMonitors()[n - 1]. The list starts with
  // the primary, so extra entries begin at "Monitor 2" (value 2); value 1 is shown as "Primary".
  std::vector<std::wstring> monItems = {T("set.monitorAuto"), T("set.monitorPrimary")};
  std::vector<int> monValues = {-1, 0};
  for (size_t i = 1; i < mons.size(); ++i) {
    monItems.push_back(Fmt(L"%s %zu", T("set.monitorN"), i + 1));
    monValues.push_back(static_cast<int>(i) + 1);
  }
  const float monW = std::min(x1 - x0 - 260, 112.f * static_cast<float>(monItems.size()));
  constexpr float posH = 68;
  card(2 * posH + 8);
  y += 4;
  {
    const D2D1_RECT_F row = Rc(x0, y, x1, y + posH);
    const float cl = RowText(row, T("set.centerMode"), T("set.centerHint"), std::max(300.f, monW));
    const float mid = std::round((row.top + row.bottom) / 2);
    int center = static_cast<int>(s.centerMode);
    if (ui_.Segmented(MakeId("set.center"), Rc(cl, mid - 16, row.right - 16, mid + 16),
                      {T("set.centerMonitor"), T("set.centerWindow")}, &center)) {
      s.centerMode = static_cast<CenterMode>(center);
      PushConfig();
    }
    y += posH;
  }
  {
    const D2D1_RECT_F row = Rc(x0, y, x1, y + posH);
    const float cl = RowText(row, T("set.monitor"), T("set.monitorHint"), std::max(300.f, monW));
    const float mid = std::round((row.top + row.bottom) / 2);
    // A monitor that is no longer connected falls back to the last one, as the core does.
    const int want = s.monitor == 1 ? 0 : s.monitor;
    int sel = static_cast<int>(monValues.size()) - 1;
    for (size_t i = 0; i < monValues.size(); ++i) {
      if (monValues[i] == want) sel = static_cast<int>(i);
    }
    if (ui_.Segmented(MakeId("set.monitor"), Rc(cl, mid - 16, row.right - 16, mid + 16), monItems, &sel)) {
      s.monitor = monValues[sel];
      PushConfig();
    }
    y += posH;
  }
  y += 4 + 28;

  // Behaviour toggles.
  y = ui_.Section(x0, x1, y, T("set.behaviour"));
  struct Item {
    const char* id;
    const char* label;
    const char* hint;
    bool* v;
  };
  const Item items[] = {
      {"set.autostart", "set.autostart", "set.autostartHint", &s.autostart},
      {"set.sleep", "set.sleep", "set.sleepHint", &s.startHidden},
      {"set.desktop", "set.hideDesktop", "set.hideDesktopHint", &s.hideOnDesktop},
      {"set.fullscreen", "set.onlyFullscreen", "set.onlyFullscreenHint", &s.onlyFullscreen},
      {"set.cursor", "set.hideCursor", "set.hideCursorHint", &s.hideWhenCursor},
      {"set.overapp", "set.hideOverApp", "set.hideOverAppHint", &s.hideOverSettings},
      {"set.capture", "set.capture", "set.captureHint", &s.excludeFromCapture},
  };
  card(static_cast<float>(std::size(items)) * rowH + 8);
  y += 4;
  for (const Item& it : items) {
    if (ui_.Toggle(MakeId(it.id), Rc(x0, y, x1, y + rowH), T(it.label), it.v, T(it.hint))) {
      if (it.v == &s.autostart) ApplyAutostart(s.autostart);
      PushConfig();
    }
    y += rowH;
  }
  y += 4 + 28;

  // Files.
  y = ui_.Section(x0, x1, y, T("set.files"));
  const float fw = std::ceil(ui_.Measure(T("btn.openFolder"), Font::Button)) + 56;
  if (ui_.Button(MakeId("set.folder"), Rc(x0, y, x0 + fw, y + 34), T("btn.openFolder"), Btn::Secondary, true, icon::Folder)) {
    ShellExecuteW(hwnd_, L"open", ConfigDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
  }
  const wchar_t* resetLabel = confirmReset_ ? T("btn.confirmReset") : T("btn.resetAll");
  const float rw = std::ceil(ui_.Measure(resetLabel, Font::Button)) + 48;
  if (ui_.Button(MakeId("set.reset"), Rc(x0 + fw + 8, y, x0 + fw + 8 + rw, y + 34), resetLabel,
                 confirmReset_ ? Btn::Primary : Btn::Ghost, true, icon::Reset)) {
    if (confirmReset_) {
      const std::string keepLanguage = cfg_.settings.language;
      const std::string keepTheme = cfg_.settings.theme;
      cfg_ = Config{};
      cfg_.settings.language = keepLanguage;
      cfg_.settings.theme = keepTheme;
      draftId_.clear();  // nothing left to save the draft into
      ApplyAutostart(false);
      confirmReset_ = false;
      Toast(T("toast.resetAll"));
      PushConfig();
    } else {
      confirmReset_ = true;
    }
  }
  y += 34 + 24;
  contentH = y + off - area.top;
  ui_.EndScroll(view, off, contentH);
}

}  // namespace sl
