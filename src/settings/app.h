#pragma once

#include "common/model.h"
#include "settings/update.h"
#include "settings/ui.h"

#include <d2d1_1.h>
#include <d3d11.h>
#include <dwrite_1.h>
#include <dxgi1_2.h>

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace sl {

enum class Page : int { Library, Editor, Hotkeys, Profiles, Safety, Settings, Count };
enum class Capture : int { None, Toggle, Next, Prev, Settings, Ads, Crosshair };
enum class PickTarget : int { None, Filter, Safe, Profile };
enum class FileAction : int { None, Export, Import };
// Popovers are drawn last, above the page; while one is open the page underneath is inert.
enum class Popup : int { None, CardMenu, Color, Picker, Unsaved, Running, Language };
// About → updates: what the last check or download found.
enum class UpdState : int { Idle, Checking, UpToDate, Available, CheckFailed, Downloading, Failed };
// Update worker в†’ window. wParam: 0 check failed, 1 checked (lParam: update::Release*),
// 2 download failed, 3 downloaded (lParam: std::wstring* folder). The window frees lParam.
inline constexpr UINT kMsgUpdate = WM_APP + 40;

// One running application offered by the "From running" list (deduplicated by exe name).
struct RunningApp {
  std::wstring exe;    // lowercase file name
  std::wstring path;   // full image path; empty when the process cannot be queried
  std::wstring name;   // file description, else the window title, else the exe name
  bool windowed = false;  // owns a visible top-level window
  bool iconTried = false;
  ComPtr<ID2D1Bitmap1> icon;
};
// Why the unsaved-changes dialog is open. Action: a deferred UI action (switching crosshair, creating
// one) runs after Save / Don't save. Switched: the active crosshair already changed (hotkey, tray);
// Cancel switches back. Close: the window closes after Save / Don't save.
enum class UnsavedKind : int { Action, Switched, Close };

class SettingsApp {
 public:
  int Run(HINSTANCE hinst, int show);

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
  LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);

  // Rendering infrastructure (main.cpp)
  bool CreateDevice();
  void ResizeTarget();
  void Frame();
  void Invalidate() { InvalidateRect(hwnd_, nullptr, FALSE); }
  float Scale() const { return dpi_ / 96.f; }
  void ApplyTheme();
  struct Preview {
    std::string fingerprint;
    ComPtr<ID2D1Bitmap1> bitmap;
    int size = 0;
    bool even = false;  // centre on a pixel corner (CrosshairEvenCenter) instead of a pixel centre
  };
  void UpdatePreviews();
  bool RasterizeInto(Preview& p, const Crosshair& c);
  const Preview* PreviewFor(const Crosshair& c);
  const Preview* DraftPreview();
  const Preview* ShapeIcon(Shape s);
  // Backdrop -1: none, 0: preview tile, 1: dark, 2: light. Returns the integer zoom used (0 when nothing drawn).
  int DrawPreview(const Crosshair& c, D2D1_RECT_F box, float maxZoom, int backdrop, bool grid = false,
                  float radius = ui::kRadiusCtl);
  // `rulers`: centre axes and pixel-offset rulers along the box edges (editor canvas).
  int DrawBitmap(const Preview* p, D2D1_RECT_F box, float maxZoom, int backdrop, bool grid, float fit = 0.86f,
                 float radius = ui::kRadiusCtl, bool rulers = false);
  void DrawRulers(const Preview* p, D2D1_RECT_F box, D2D1_RECT_F dst, float zoom, int backdrop, float radius);
  // Icon of an executable as a bitmap of `sizeDip` DIPs; null when the file has none.
  ComPtr<ID2D1Bitmap1> ExeIcon(const std::wstring& path, float sizeDip);

  // State sync
  void PushConfig();
  void ScheduleSave();
  void Toast(std::wstring text);
  void StartCapture(Capture c, std::string crosshairId = {});
  void FinishCapture(Hotkey hk, bool clear);
  bool HandleCaptureKey(UINT msg, WPARAM wp);
  void ApplyAutostart(bool on);
  bool CoreOnline() const;
  void LaunchCore();
  // Windowed apps first, then background processes; system noise is left out.
  std::vector<RunningApp> RunningApps() const;
  // Adds a lowercase exe name to the list behind `target`; false when it is already there.
  bool AddExeTo(PickTarget target, const std::wstring& exe);
  bool HasExe(PickTarget target, const std::wstring& exe) const;
  void RunFileAction(FileAction a);
  void ImportCode();

  // Library actions shared by the side panel and the card menu.
  void DuplicateCrosshair(const Crosshair& c);
  void DeleteOrResetCrosshair(const Crosshair& c);
  void ToggleFavorite(const Crosshair& c);
  void CopyCrosshairCode(const Crosshair& c);
  // Makes `id` active; asks about unsaved editor changes first.
  void Activate(const std::string& id);
  // A preset whose stored override differs from the built-in in editable content.
  bool PresetEdited(const std::string& id) const;

  // Editor draft (pages.cpp). The editor works on a copy of the active crosshair; edits never touch
  // cfg_ until saved. While the draft differs from the saved state it is previewed in game.
  std::string ActiveId() const;
  bool DraftDirty() const;
  void SyncDraft();
  void RebaseDraft();
  void SaveDraft(bool activate);
  void SaveDraftAsNew();
  void DiscardDraft();
  void SyncPreview();
  // Runs `action` now, or after the unsaved-changes dialog when the draft is dirty.
  void GuardDraft(std::function<void()> action);
  void OpenUnsaved(UnsavedKind kind, std::function<void()> action = {});
  void UnsavedDialog();
  // WM_CLOSE: true when the window may close now.
  bool RequestClose();

  // Editor history (pages.cpp), on the draft. Edits accumulate into one pending step that is
  // committed once the gesture ends: mouse released, no text field focused, no wheel input for a moment.
  void EditApply(const Crosshair& next);
  void CommitHistory(bool force);
  void StepHistory(bool redo);
  void ClearHistory();

  // Hover hint drawn above the page at the end of the frame.
  void Tip(D2D1_RECT_F r, const wchar_t* text);

  // Pages (pages.cpp)
  void Chrome();
  void Shortcuts();
  void Overlays();
  void ClosePopup();
  void CardMenu();
  void ColorPopover();
  void PickerPopover();
  void PageLibrary(D2D1_RECT_F area);
  void ActivePanel(D2D1_RECT_F r);
  void PageEditor(D2D1_RECT_F area);
  // Grid of shape icon buttons; *hovered receives the shape under the mouse or -1.
  float ShapePicker(Layer& L, float x0, float x1, float y, bool& changed, int* hovered);
  // `dotRequest` receives +1 / -1 when the centre-dot switch adds / removes the dot layer.
  // `evenCenter` and `scale` describe the whole crosshair, for the effective pixel-size notes.
  float LayerProps(Layer& L, float x0, float x1, float y, bool& changed, bool hasDot, int* dotRequest, bool evenCenter,
                   float scale);
  // Colour chip that opens the colour popover for the selected layer (0: fill, 1: outline).
  // `anchor` is the field the popover lines up with.
  void ColorChip(D2D1_RECT_F r, const Color& c, int which, D2D1_RECT_F anchor);
  void RunningPopover();
  // Custom caption: minimise / maximise / close drawn in the top-right corner.
  void CaptionButtons();
  D2D1_RECT_F CaptionButtonRect(int index) const;  // 0 minimise, 1 maximise, 2 close (DIPs)
  // WM_NCHITTEST for a client point in DIPs: HTCAPTION, a caption button code or HTCLIENT.
  LRESULT CaptionHit(float x, float y) const;
  // A text field has the keyboard (the layers list focus does not count).
  bool TypingFocus() const;
  // Settings-style row inside a card: title and optional hint left; returns the control column's left edge.
  float RowText(D2D1_RECT_F row, const wchar_t* title, const wchar_t* hint, float controlW);
  void PageHotkeys(D2D1_RECT_F area);
  void PageProfiles(D2D1_RECT_F area);
  float ExeList(const char* key, std::vector<std::wstring>& list, float x0, float x1, float y, PickTarget target,
                std::wstring& draft);
  // "Browse…" / "From running" buttons and a secondary name field; returns the y below the row.
  float ExeAddRow(const char* key, PickTarget target, std::wstring& draft, float x0, float x1, float y);
  // Field showing the chosen crosshair; opens a searchable list popover anchored to it. `id` must stay
  // valid until the end of the frame: the popover writes the choice into it.
  void CrosshairPicker(const std::string& key, D2D1_RECT_F field, std::string& id);
  void PageSafety(D2D1_RECT_F area);
  void PageSettings(D2D1_RECT_F area);
  // Settings → About: version, update check / install, GitHub and author links. Returns the y below it.
  float AboutSection(float x0, float x1, float y);
  void LanguagePopover();
  void StartUpdateCheck();
  void StartUpdateDownload();
  void OnUpdateResult(WPARAM kind, LPARAM payload);

  HINSTANCE hinst_ = nullptr;
  HWND hwnd_ = nullptr;
  float dpi_ = 96;

  ComPtr<ID3D11Device> d3d_;
  ComPtr<IDXGISwapChain1> swap_;
  ComPtr<ID2D1Factory1> d2dFactory_;
  ComPtr<ID2D1Device> d2dDevice_;
  ComPtr<ID2D1DeviceContext> dc_;
  ComPtr<ID2D1Bitmap1> target_;
  ComPtr<IDWriteFactory1> dw_;

  ui::Context ui_;
  ui::Input in_;
  std::unordered_map<std::string, Preview> previews_;

  Config cfg_;
  Page page_ = Page::Library;
  int libCategory_ = 0;
  float scroll_[static_cast<int>(Page::Count)] = {};
  float propsScroll_ = 0;
  float libScroll_ = 0;
  float sideScroll_ = 0;
  float layersScroll_ = 0;

  int editLayer_ = 0;
  bool edOpen_[4] = {true, true, true, false};  // inspector sections: size, colour, outline, position
  int backdrop_ = 0;
  int zoom_ = 2;  // index into zoom table
  Crosshair draft_;         // editor working copy; draft_.id == draftId_
  std::string draftId_;     // saved crosshair the draft belongs to; empty = rebase on the next frame
  std::string sentPreview_; // last kPreviewCrosshair payload sent to the core ("" = no preview)
  UnsavedKind unsavedKind_ = UnsavedKind::Action;
  std::function<void()> unsavedAction_;
  bool pendingClose_ = false;
  std::wstring tip_;
  D2D1_RECT_F tipAnchor_{};
  std::wstring profOffText_;  // text of the focused profile offset field
  std::wstring editName_;
  std::wstring editHex_, editOutlineHex_;
  std::wstring importCode_;
  std::wstring draftFilter_, draftSafe_, draftProfile_;
  PickTarget runTarget_ = PickTarget::None;     // list the running-apps popover adds to
  PickTarget pendingBrowse_ = PickTarget::None; // exe file dialog to run after the frame
  std::vector<RunningApp> running_;
  std::wstring runSearch_;
  float runScroll_ = 0;
  std::string pickerOpen_;  // key of the open crosshair picker
  std::string pendingDelete_;
  FileAction pendingFile_ = FileAction::None;
  bool pendingTheme_ = false;
  bool inFrame_ = false;
  bool confirmReset_ = false;

  // Library search.
  std::wstring search_;

  // Popover state.
  Popup popup_ = Popup::None;
  D2D1_RECT_F popupAnchor_{};  // anchor rectangle (DIP) the popover is placed against
  D2D1_RECT_F popupRect_{};    // last drawn popover bounds
  std::string popupId_;        // card menu: crosshair id
  int popupColor_ = 0;         // colour popover: 0 fill, 1 outline
  float hsv_[3] = {0, 0, 1};   // colour popover state; keeps hue while saturation or value is zero
  std::wstring popupHex_;
  std::wstring pickerSearch_;
  std::string* pickerTarget_ = nullptr;  // set each frame by the visible CrosshairPicker
  bool pickerSeen_ = false;
  float pickerScroll_ = 0;

  // Editor history.
  std::vector<Crosshair> undo_, redo_;
  Crosshair historyBefore_;
  bool historyPending_ = false;
  ULONGLONG lastWheel_ = 0;
  bool dupLayerRequest_ = false;
  int clipRequest_ = 0;  // keyboard request for the editor: 1 copy layer, 2 paste layer, 3 paste style
  Layer layerClip_;      // copied layer, kept across crosshairs
  bool hasLayerClip_ = false;

  // Custom caption buttons: hit-test code under the pointer and the one pressed (0 = none).
  LRESULT capHover_ = 0, capPressed_ = 0;
  float headerRight_ = 0;  // left edge of the header's right-hand controls this frame (DIPs)

  Capture capture_ = Capture::None;
  std::string captureCrosshair_;
  std::wstring toast_;
  ULONGLONG toastUntil_ = 0;

  // Updates (Settings → About). Worker threads post kMsgUpdate with a heap result.
  UpdState updState_ = UpdState::Idle;
  update::Release updRelease_;
  std::string appVersion_;
};

}  // namespace sl
