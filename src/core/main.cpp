#include "common/i18n.h"
#include "common/ipc.h"
#include "common/logo.h"
#include "common/model.h"
#include "core/overlay.h"

#include <shellapi.h>

#include <algorithm>

namespace sl {

namespace {

constexpr UINT kMsgTray = WM_APP + 1;
constexpr UINT_PTR kTimerTopmost1 = 1;
constexpr UINT_PTR kTimerTopmost2 = 2;
constexpr UINT_PTR kTimerSave = 3;
constexpr int kHotkeyToggle = 1, kHotkeyNext = 2, kHotkeyPrev = 3, kHotkeySettings = 4;
constexpr int kHotkeyNudgeBase = 20;      // 20..23: up, right, down, left
constexpr int kHotkeyCrosshairBase = 1000;  // + index in Library()
constexpr UINT kCmdToggle = 100, kCmdSettings = 101, kCmdQuit = 102, kCmdCrosshairBase = 1000;
constexpr LONG kAdsPeriodMs = 8;       // ~125 Hz, only while an ADS key is bound
constexpr LONG kCursorPeriodMs = 100;  // 10 Hz, only while "hide when cursor visible" is on
constexpr UINT kNudgeKeys[] = {VK_UP, VK_RIGHT, VK_DOWN, VK_LEFT};

bool IsShellClass(const wchar_t* cls) {
  static const wchar_t* kShell[] = {L"Progman",          L"WorkerW",          L"Shell_TrayWnd",
                                    L"Shell_SecondaryTrayWnd", L"MultitaskingViewFrame", L"XamlExplorerHostIslandWindow",
                                    L"ForegroundStaging", L"TaskSwitcherWnd", L"Windows.UI.Core.CoreWindow",
                                    L"NotifyIconOverflowWindow", L"TopLevelWindowForOverflowXamlIsland"};
  for (const wchar_t* s : kShell) {
    if (wcscmp(cls, s) == 0) return true;
  }
  return false;
}

// True when the window covers its whole monitor: exclusive-style borderless games and
// fullscreen video. Maximised windows stop at the work area and do not count.
bool IsFullscreen(HWND hwnd) {
  if (!hwnd || !IsWindowVisible(hwnd) || IsIconic(hwnd)) return false;
  wchar_t cls[128] = {};
  GetClassNameW(hwnd, cls, static_cast<int>(std::size(cls)));
  if (IsShellClass(cls)) return false;
  RECT wr{};
  if (!GetWindowRect(hwnd, &wr)) return false;
  MONITORINFO mi{sizeof(mi)};
  if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi)) return false;
  const RECT& m = mi.rcMonitor;
  return wr.left <= m.left && wr.top <= m.top && wr.right >= m.right && wr.bottom >= m.bottom;
}

bool Contains(const std::vector<std::wstring>& list, const std::wstring& exe) {
  return !exe.empty() && std::find(list.begin(), list.end(), exe) != list.end();
}

}  // namespace

class CoreApp {
 public:
  int Run(HINSTANCE hinst);

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
  static void CALLBACK WinEvent(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD);
  LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);

  void ApplyConfig();
  void RegisterHotkeys();
  void UnregisterHotkeys();
  void OnForeground(HWND fg);
  void ApplyCrosshair();
  void UpdateCenter();
  void UpdateVisibility();
  void UpdatePolling();
  void PollKeys();
  bool IsMyGame(const std::wstring& exe) const;
  bool FilterAllows(const std::wstring& exe) const;
  int ProfileIndex() const;
  std::string EffectiveId() const;
  void Cycle(int dir);
  void SelectCrosshair(const std::string& id);
  void Nudge(int dx, int dy);
  bool CursorVisible() const;
  void ToggleEnabled();
  void OpenSettings();
  void ScheduleSave();
  void NotifySettings();
  void TrayAdd();
  void TrayRemove();
  void TrayMenu();

  static CoreApp* self_;

  HINSTANCE hinst_ = nullptr;
  HWND wnd_ = nullptr;
  Overlay overlay_;
  Config cfg_;
  std::vector<HWINEVENTHOOK> hooks_;
  HANDLE pollTimer_ = nullptr;
  bool polling_ = false;
  LONG pollPeriod_ = 0;
  ULONGLONG lastCursorCheck_ = 0;
  bool cursorShown_ = false;
  bool hotkeysSuspended_ = false;
  std::vector<std::string> crosshairHotkeys_;  // index = hotkey id - kHotkeyCrosshairBase

  HWND gameHwnd_ = nullptr;  // last foreground window that is not shell / Sightline
  std::wstring gameExe_;
  bool fgDesktop_ = false;
  bool fgSettings_ = false;
  bool fgFullscreen_ = false;
  HWND fgHwnd_ = nullptr;

  bool adsActive_ = false;
  bool adsPrevDown_ = false;
  bool sleeping_ = false;
  Crosshair preview_;
  bool hasPreview_ = false;
  std::wstring hotkeySig_;
  bool excludeApplied_ = false;

  UINT taskbarCreated_ = 0;
  HICON icon_ = nullptr;
  bool trayAdded_ = false;
};

CoreApp* CoreApp::self_ = nullptr;

int CoreApp::Run(HINSTANCE hinst) {
  self_ = this;
  hinst_ = hinst;
  RemoveUpdateLeftovers();

  const bool loaded = LoadConfig(cfg_);
  if (!loaded) SaveConfig(cfg_);
  SetLanguage(cfg_.settings.language);
  sleeping_ = cfg_.settings.startHidden;

  WNDCLASSEXW wc{sizeof(wc)};
  wc.lpfnWndProc = &CoreApp::WndProc;
  wc.hInstance = hinst;
  wc.lpszClassName = ipc::kCoreClass;
  RegisterClassExW(&wc);
  // Hidden top-level window: receives broadcasts (display change, TaskbarCreated) unlike HWND_MESSAGE.
  wnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, ipc::kCoreClass, L"Sightline Core", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                         hinst, nullptr);
  if (!wnd_) return 1;
  ChangeWindowMessageFilterEx(wnd_, WM_COPYDATA, MSGFLT_ALLOW, nullptr);
  ChangeWindowMessageFilterEx(wnd_, ipc::kMsgOpenSettings, MSGFLT_ALLOW, nullptr);

  if (!overlay_.Create(hinst)) {
    MessageBoxW(nullptr, L"Sightline: failed to create the overlay window.", L"Sightline", MB_ICONERROR);
    return 2;
  }

  taskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");
  // The .ico resource carries 16…256 px images; LoadImage picks the best one for the tray size at this DPI.
  const int traySize = GetSystemMetricsForDpi(SM_CXSMICON, GetDpiForSystem());
  icon_ = static_cast<HICON>(LoadImageW(hinst, MAKEINTRESOURCEW(1), IMAGE_ICON, traySize, traySize, LR_DEFAULTCOLOR));
  TrayAdd();

  // Out-of-context hooks: callbacks run in this thread, nothing is loaded into other processes.
  const DWORD flags = WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS;
  hooks_.push_back(SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, &CoreApp::WinEvent, 0, 0, flags));
  hooks_.push_back(SetWinEventHook(EVENT_SYSTEM_MOVESIZEEND, EVENT_SYSTEM_MOVESIZEEND, nullptr, &CoreApp::WinEvent, 0, 0, flags));
  hooks_.push_back(SetWinEventHook(EVENT_SYSTEM_MINIMIZEEND, EVENT_SYSTEM_MINIMIZEEND, nullptr, &CoreApp::WinEvent, 0, 0, flags));

  pollTimer_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
  if (!pollTimer_) pollTimer_ = CreateWaitableTimerW(nullptr, FALSE, nullptr);

  ApplyConfig();
  OnForeground(GetForegroundWindow());
  Log(L"core", L"started, config %s", loaded ? L"loaded" : L"created");

  bool running = true;
  while (running) {
    const DWORD count = polling_ ? 1 : 0;
    const DWORD r = MsgWaitForMultipleObjectsEx(count, &pollTimer_, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    if (count && r == WAIT_OBJECT_0) PollKeys();
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
        running = false;
        break;
      }
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }

  for (HWINEVENTHOOK h : hooks_) {
    if (h) UnhookWinEvent(h);
  }
  UnregisterHotkeys();
  TrayRemove();
  overlay_.Destroy();
  if (pollTimer_) CloseHandle(pollTimer_);
  if (icon_) DestroyIcon(icon_);
  SaveConfig(cfg_);
  Log(L"core", L"stopped");
  return 0;
}

LRESULT CALLBACK CoreApp::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (self_ && self_->wnd_ == hwnd) return self_->Handle(msg, wp, lp);
  return DefWindowProcW(hwnd, msg, wp, lp);
}

void CALLBACK CoreApp::WinEvent(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG, DWORD, DWORD) {
  if (!self_ || idObject != OBJID_WINDOW) return;
  switch (event) {
    case EVENT_SYSTEM_FOREGROUND: self_->OnForeground(hwnd); break;
    case EVENT_SYSTEM_MOVESIZEEND:
    case EVENT_SYSTEM_MINIMIZEEND:
      if (hwnd == self_->gameHwnd_) self_->UpdateCenter();
      if (hwnd == self_->fgHwnd_) {
        self_->fgFullscreen_ = IsFullscreen(hwnd);
        self_->UpdateVisibility();
      }
      break;
    default: break;
  }
}

LRESULT CoreApp::Handle(UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == taskbarCreated_ && taskbarCreated_) {
    trayAdded_ = false;
    TrayAdd();
    return 0;
  }
  switch (msg) {
    case WM_HOTKEY: {
      const int id = static_cast<int>(wp);
      switch (id) {
        case kHotkeyToggle: ToggleEnabled(); break;
        case kHotkeyNext: Cycle(+1); break;
        case kHotkeyPrev: Cycle(-1); break;
        case kHotkeySettings: OpenSettings(); break;
        case kHotkeyNudgeBase + 0: Nudge(0, -1); break;
        case kHotkeyNudgeBase + 1: Nudge(1, 0); break;
        case kHotkeyNudgeBase + 2: Nudge(0, 1); break;
        case kHotkeyNudgeBase + 3: Nudge(-1, 0); break;
        default:
          if (id >= kHotkeyCrosshairBase && id - kHotkeyCrosshairBase < static_cast<int>(crosshairHotkeys_.size())) {
            SelectCrosshair(crosshairHotkeys_[static_cast<size_t>(id - kHotkeyCrosshairBase)]);
          }
          break;
      }
      return 0;
    }
    case WM_TIMER:
      KillTimer(wnd_, wp);
      if (wp == kTimerTopmost1 || wp == kTimerTopmost2) {
        // Games often switch to borderless fullscreen a moment after activation.
        fgFullscreen_ = IsFullscreen(fgHwnd_);
        UpdateCenter();
        overlay_.BringToTop();
        UpdateVisibility();
      } else if (wp == kTimerSave) {
        SaveConfig(cfg_);
      }
      return 0;
    case WM_COPYDATA: {
      const auto* cds = reinterpret_cast<const COPYDATASTRUCT*>(lp);
      if (!cds) return FALSE;
      if (cds->dwData == ipc::kApplyConfig && cds->lpData) {
        Config next;
        if (ConfigFromString(std::string_view(static_cast<const char*>(cds->lpData), cds->cbData), next)) {
          const bool wasEnabled = cfg_.settings.enabled;
          cfg_ = std::move(next);
          if (cfg_.settings.enabled && !wasEnabled) sleeping_ = false;
          ApplyConfig();
        }
        return TRUE;
      }
      if (cds->dwData == ipc::kSuspendHotkeys) {
        hotkeysSuspended_ = true;
        UnregisterHotkeys();
        return TRUE;
      }
      if (cds->dwData == ipc::kResumeHotkeys) {
        hotkeysSuspended_ = false;
        RegisterHotkeys();
        return TRUE;
      }
      if (cds->dwData == ipc::kPreviewCrosshair) {
        // Unsaved editor draft: shown instead of the active crosshair, never written to cfg_ or disk.
        hasPreview_ = false;
        if (cds->lpData && cds->cbData) {
          json::Value v;
          if (json::Parse(std::string_view(static_cast<const char*>(cds->lpData), cds->cbData), v) && v.IsObject()) {
            FromJson(v, preview_);
            hasPreview_ = !preview_.layers.empty();
          }
        }
        ApplyCrosshair();
        return TRUE;
      }
      return FALSE;
    }
    case ipc::kMsgOpenSettings: OpenSettings(); return 0;
    case kMsgTray:
      switch (LOWORD(lp)) {
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU: TrayMenu(); break;
        case WM_LBUTTONDBLCLK: OpenSettings(); break;
        default: break;
      }
      return 0;
    case WM_DISPLAYCHANGE:
    case WM_SETTINGCHANGE:
      UpdateCenter();
      return 0;
    case WM_CLOSE:
      PostQuitMessage(0);
      return 0;
    default: return DefWindowProcW(wnd_, msg, wp, lp);
  }
}

void CoreApp::ApplyConfig() {
  SetLanguage(cfg_.settings.language);
  // Settings pushes the config on every edit; re-register hotkeys only when a binding changed.
  std::wstring sig = HotkeyToText(cfg_.settings.hkToggle) + L"|" + HotkeyToText(cfg_.settings.hkNext) + L"|" +
                     HotkeyToText(cfg_.settings.hkPrev) + L"|" + HotkeyToText(cfg_.settings.hkSettings) + L"|" +
                     std::to_wstring(cfg_.settings.nudgeMods);
  for (const Crosshair* c : Library(cfg_)) {
    if (c->hotkey.Valid()) sig += L"|" + Widen(c->id) + L"=" + HotkeyToText(c->hotkey);
  }
  if (sig != hotkeySig_) {
    hotkeySig_ = std::move(sig);
    RegisterHotkeys();
  }
  if (cfg_.settings.excludeFromCapture != excludeApplied_) {
    excludeApplied_ = cfg_.settings.excludeFromCapture;
    overlay_.SetExcludeFromCapture(excludeApplied_);
  }
  ApplyCrosshair();
  UpdateCenter();
  UpdateVisibility();
}

void CoreApp::RegisterHotkeys() {
  UnregisterHotkeys();
  if (hotkeysSuspended_) return;
  const Settings& s = cfg_.settings;
  const struct {
    int id;
    Hotkey hk;
  } list[] = {{kHotkeyToggle, s.hkToggle}, {kHotkeyNext, s.hkNext}, {kHotkeyPrev, s.hkPrev}, {kHotkeySettings, s.hkSettings}};
  const auto reg = [&](int id, const Hotkey& hk, bool repeat) {
    if (!hk.Valid()) return;
    if (!RegisterHotKey(wnd_, id, hk.mods | (repeat ? 0 : MOD_NOREPEAT), hk.vk)) {
      Log(L"core", L"hotkey %s is taken by another app (%lu)", HotkeyToText(hk).c_str(), GetLastError());
    }
  };
  for (const auto& e : list) reg(e.id, e.hk, false);
  // Nudge keys auto-repeat while held.
  if (s.nudgeMods) {
    for (int i = 0; i < 4; ++i) reg(kHotkeyNudgeBase + i, Hotkey{s.nudgeMods, kNudgeKeys[i]}, true);
  }
  crosshairHotkeys_.clear();
  for (const Crosshair* c : Library(cfg_)) {
    if (!c->hotkey.Valid()) continue;
    reg(kHotkeyCrosshairBase + static_cast<int>(crosshairHotkeys_.size()), c->hotkey, false);
    crosshairHotkeys_.push_back(c->id);
  }
}

void CoreApp::UnregisterHotkeys() {
  for (int id = kHotkeyToggle; id <= kHotkeySettings; ++id) UnregisterHotKey(wnd_, id);
  for (int i = 0; i < 4; ++i) UnregisterHotKey(wnd_, kHotkeyNudgeBase + i);
  for (size_t i = 0; i < crosshairHotkeys_.size(); ++i) UnregisterHotKey(wnd_, kHotkeyCrosshairBase + static_cast<int>(i));
  crosshairHotkeys_.clear();
}

void CoreApp::OnForeground(HWND fg) {
  if (!fg) return;
  // A draft preview must not outlive the settings window (e.g. if it crashed before ending it).
  if (hasPreview_ && !ipc::SettingsWindow()) {
    hasPreview_ = false;
    ApplyCrosshair();
  }
  wchar_t cls[128] = {};
  GetClassNameW(fg, cls, static_cast<int>(std::size(cls)));
  const std::wstring exe = WindowExeName(fg);
  // Win+D and clicks on empty desktop space often activate the taskbar or another shell
  // surface instead of Progman/WorkerW, so every shell class counts as "desktop".
  fgDesktop_ = IsShellClass(cls);
  fgSettings_ = exe == ipc::kSettingsExe;
  fgHwnd_ = fg;
  fgFullscreen_ = IsFullscreen(fg);
  const bool shell = IsShellClass(cls) || fg == overlay_.Hwnd() || exe == ipc::kCoreExe;

  // Shell surfaces and the settings window keep the previous game context (profile and position).
  if (!shell && !fgSettings_) {
    const bool changed = fg != gameHwnd_;
    gameHwnd_ = fg;
    gameExe_ = exe;
    if (changed) {
      adsActive_ = false;
      adsPrevDown_ = false;
    }
    if (sleeping_ && (ProfileIndex() >= 0 || (cfg_.settings.filterMode == FilterMode::Whitelist && IsMyGame(gameExe_)))) {
      sleeping_ = false;
    }
    ApplyCrosshair();
    UpdateCenter();
    // Some games resize or re-assert z-order right after activation.
    SetTimer(wnd_, kTimerTopmost1, 500, nullptr);
    SetTimer(wnd_, kTimerTopmost2, 2000, nullptr);
  }
  overlay_.BringToTop();
  UpdateVisibility();
}

int CoreApp::ProfileIndex() const {
  const auto& p = cfg_.settings.profiles;
  for (size_t i = 0; i < p.size(); ++i) {
    if (p[i].exe == gameExe_) return static_cast<int>(i);
  }
  return -1;
}

std::string CoreApp::EffectiveId() const {
  const Settings& s = cfg_.settings;
  if (adsActive_ && s.adsKey && s.adsAction == AdsAction::Switch && FindCrosshair(cfg_, s.adsCrosshairId)) {
    return s.adsCrosshairId;
  }
  const int pi = ProfileIndex();
  if (pi >= 0) {
    const std::string& id = cfg_.settings.profiles[static_cast<size_t>(pi)].crosshairId;
    if (FindCrosshair(cfg_, id)) return id;
  }
  return cfg_.activeId;
}

void CoreApp::ApplyCrosshair() {
  overlay_.SetCrosshair(hasPreview_ ? preview_ : ResolveCrosshair(cfg_, EffectiveId()));
}

void CoreApp::UpdateCenter() {
  POINT c{};
  HWND ref = (gameHwnd_ && IsWindow(gameHwnd_)) ? gameHwnd_ : nullptr;
  bool done = false;
  if (cfg_.settings.centerMode == CenterMode::Window && ref && !IsIconic(ref)) {
    RECT rc{};
    POINT tl{0, 0};
    if (GetClientRect(ref, &rc) && ClientToScreen(ref, &tl) && rc.right > 0 && rc.bottom > 0) {
      c = {tl.x + rc.right / 2, tl.y + rc.bottom / 2};
      done = true;
    }
  }
  if (!done) {
    RECT r{};
    const int want = cfg_.settings.monitor;
    const auto monitors = want >= 0 ? ListMonitors() : std::vector<MonitorDesc>{};
    if (want >= 0 && !monitors.empty()) {
      // 0 and 1 both mean the primary monitor (it is always first in the list).
      r = monitors[std::min(static_cast<size_t>(std::max(0, want - 1)), monitors.size() - 1)].rect;
    } else {
      HMONITOR mon = ref ? MonitorFromWindow(ref, MONITOR_DEFAULTTONEAREST) : MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
      MONITORINFO mi{sizeof(mi)};
      GetMonitorInfoW(mon, &mi);
      r = mi.rcMonitor;
    }
    c = {r.left + (r.right - r.left) / 2, r.top + (r.bottom - r.top) / 2};
  }
  c.x += cfg_.settings.offsetX;
  c.y += cfg_.settings.offsetY;
  const int pi = ProfileIndex();
  if (pi >= 0) {
    c.x += cfg_.settings.profiles[static_cast<size_t>(pi)].offsetX;
    c.y += cfg_.settings.profiles[static_cast<size_t>(pi)].offsetY;
  }
  overlay_.SetCenter(c);
}

// "Only my games": the games of the profiles plus the extra filter list.
bool CoreApp::IsMyGame(const std::wstring& exe) const {
  const Settings& s = cfg_.settings;
  if (Contains(s.filterList, exe)) return true;
  for (const GameProfile& p : s.profiles) {
    if (p.exe == exe) return true;
  }
  return false;
}

bool CoreApp::FilterAllows(const std::wstring& exe) const {
  const Settings& s = cfg_.settings;
  if (Contains(s.safeList, exe)) return false;
  switch (s.filterMode) {
    case FilterMode::Whitelist: return IsMyGame(exe);
    case FilterMode::Blacklist: return !Contains(s.filterList, exe);
    default: return true;
  }
}

void CoreApp::UpdateVisibility() {
  const Settings& s = cfg_.settings;
  bool vis = s.enabled && !sleeping_ && FilterAllows(gameExe_);
  if (vis && fgDesktop_ && s.hideOnDesktop) vis = false;
  if (vis && s.onlyFullscreen && !fgFullscreen_ && !fgSettings_) vis = false;
  if (vis && fgSettings_ && s.hideOverSettings) vis = false;
  if (vis && s.adsKey) {
    if (s.adsAction == AdsAction::Hide) vis = !adsActive_;
    else if (s.adsAction == AdsAction::ShowOnly) vis = adsActive_;
  }
  // Menus and inventories show the cursor; the settings window keeps the live preview visible.
  if (vis && s.hideWhenCursor && cursorShown_ && !fgSettings_) vis = false;
  overlay_.SetVisible(vis);
  UpdatePolling();
}

void CoreApp::UpdatePolling() {
  const Settings& s = cfg_.settings;
  const bool active = pollTimer_ && s.enabled && !sleeping_ && FilterAllows(gameExe_);
  const bool ads = active && s.adsKey != 0 && s.adsAction != AdsAction::None;
  const bool cursor = active && s.hideWhenCursor;
  const LONG period = ads ? kAdsPeriodMs : (cursor ? kCursorPeriodMs : 0);
  if (period == pollPeriod_) return;
  pollPeriod_ = period;
  polling_ = period != 0;
  if (polling_) {
    LARGE_INTEGER due;
    due.QuadPart = -static_cast<LONGLONG>(period) * 10000;
    SetWaitableTimer(pollTimer_, &due, period, nullptr, nullptr, FALSE);
  } else {
    CancelWaitableTimer(pollTimer_);
  }
  if (!ads) {
    adsActive_ = false;
    adsPrevDown_ = false;
  }
  if (!cursor) cursorShown_ = false;
}

bool CoreApp::CursorVisible() const {
  CURSORINFO ci{sizeof(ci)};
  return GetCursorInfo(&ci) && (ci.flags & CURSOR_SHOWING) && ci.hCursor != nullptr;
}

void CoreApp::PollKeys() {
  const Settings& s = cfg_.settings;
  bool changed = false;
  bool crosshairChanged = false;
  if (s.adsKey && s.adsAction != AdsAction::None) {
    const bool down = (GetAsyncKeyState(static_cast<int>(s.adsKey)) & 0x8000) != 0;
    const bool before = adsActive_;
    if (s.adsMode == AdsMode::Hold) adsActive_ = down;
    else if (down && !adsPrevDown_) adsActive_ = !adsActive_;
    adsPrevDown_ = down;
    if (before != adsActive_) {
      changed = true;
      crosshairChanged = s.adsAction == AdsAction::Switch;
    }
  }
  if (s.hideWhenCursor) {
    const ULONGLONG now = GetTickCount64();
    if (now - lastCursorCheck_ >= static_cast<ULONGLONG>(kCursorPeriodMs)) {
      lastCursorCheck_ = now;
      const bool shown = CursorVisible();
      if (shown != cursorShown_) {
        cursorShown_ = shown;
        changed = true;
      }
    }
  }
  if (crosshairChanged) ApplyCrosshair();
  if (changed) UpdateVisibility();
}

void CoreApp::SelectCrosshair(const std::string& id) {
  if (!FindCrosshair(cfg_, id)) return;
  hasPreview_ = false;
  const int pi = ProfileIndex();
  if (pi >= 0) cfg_.settings.profiles[static_cast<size_t>(pi)].crosshairId = id;
  else cfg_.activeId = id;
  sleeping_ = false;
  ApplyCrosshair();
  UpdateVisibility();
  ScheduleSave();
  NotifySettings();
}

void CoreApp::Nudge(int dx, int dy) {
  const int pi = ProfileIndex();
  if (pi >= 0) {
    cfg_.settings.profiles[static_cast<size_t>(pi)].offsetX += dx;
    cfg_.settings.profiles[static_cast<size_t>(pi)].offsetY += dy;
  } else {
    cfg_.settings.offsetX += dx;
    cfg_.settings.offsetY += dy;
  }
  UpdateCenter();
  ScheduleSave();
  NotifySettings();
}

void CoreApp::Cycle(int dir) {
  sleeping_ = false;
  hasPreview_ = false;  // an explicit switch ends any editor preview
  const auto lib = Library(cfg_);
  if (lib.empty()) return;
  const std::string cur = EffectiveId();
  int idx = 0;
  for (size_t i = 0; i < lib.size(); ++i) {
    if (lib[i]->id == cur) idx = static_cast<int>(i);
  }
  idx = (idx + dir + static_cast<int>(lib.size())) % static_cast<int>(lib.size());
  const std::string next = lib[static_cast<size_t>(idx)]->id;
  const int pi = ProfileIndex();
  if (pi >= 0) cfg_.settings.profiles[static_cast<size_t>(pi)].crosshairId = next;
  else cfg_.activeId = next;
  ApplyCrosshair();
  UpdateVisibility();
  ScheduleSave();
  NotifySettings();
}

void CoreApp::ToggleEnabled() {
  if (sleeping_) {
    sleeping_ = false;
    cfg_.settings.enabled = true;
  } else {
    cfg_.settings.enabled = !cfg_.settings.enabled;
  }
  UpdateVisibility();
  ScheduleSave();
  NotifySettings();
}

void CoreApp::OpenSettings() {
  if (HWND w = ipc::SettingsWindow()) {
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    AllowSetForegroundWindow(pid);
    PostMessageW(w, ipc::kMsgActivate, 0, 0);
    return;
  }
  const std::wstring path = ExeDir() + L"\\" + ipc::kSettingsExe;
  STARTUPINFOW si{sizeof(si)};
  PROCESS_INFORMATION pi{};
  std::wstring cmd = L"\"" + path + L"\"";
  if (CreateProcessW(path.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
    AllowSetForegroundWindow(pi.dwProcessId);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
  } else {
    Log(L"core", L"cannot start settings (%lu)", GetLastError());
  }
}

void CoreApp::ScheduleSave() { SetTimer(wnd_, kTimerSave, 1500, nullptr); }

void CoreApp::NotifySettings() {
  if (HWND w = ipc::SettingsWindow()) ipc::Send(w, wnd_, ipc::kConfigFromCore, ConfigToString(cfg_, false));
}

void CoreApp::TrayAdd() {
  if (trayAdded_) return;
  NOTIFYICONDATAW nid{sizeof(nid)};
  nid.hWnd = wnd_;
  nid.uID = 1;
  nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  nid.uCallbackMessage = kMsgTray;
  nid.hIcon = icon_ ? icon_ : LoadIconW(nullptr, IDI_APPLICATION);
  wcscpy_s(nid.szTip, L"Sightline");
  trayAdded_ = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
}

void CoreApp::TrayRemove() {
  if (!trayAdded_) return;
  NOTIFYICONDATAW nid{sizeof(nid)};
  nid.hWnd = wnd_;
  nid.uID = 1;
  Shell_NotifyIconW(NIM_DELETE, &nid);
  trayAdded_ = false;
}

void CoreApp::TrayMenu() {
  HMENU menu = CreatePopupMenu();
  HMENU sub = CreatePopupMenu();
  const auto lib = Library(cfg_);
  const std::string cur = EffectiveId();
  for (size_t i = 0; i < lib.size() && i < 60; ++i) {
    const UINT flags = MF_STRING | (lib[i]->id == cur ? MF_CHECKED : 0) | ((i && i % 20 == 0) ? MF_MENUBARBREAK : 0);
    AppendMenuW(sub, flags, kCmdCrosshairBase + i, Widen(lib[i]->name).c_str());
  }
  const bool shown = cfg_.settings.enabled && !sleeping_;
  AppendMenuW(menu, MF_STRING, kCmdToggle, shown ? T("tray.hide") : T("tray.show"));
  AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(sub), T("tray.crosshair"));
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kCmdSettings, T("tray.settings"));
  AppendMenuW(menu, MF_STRING, kCmdQuit, T("tray.quit"));
  SetMenuDefaultItem(menu, kCmdSettings, FALSE);

  POINT pt;
  GetCursorPos(&pt);
  SetForegroundWindow(wnd_);
  const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, wnd_, nullptr);
  PostMessageW(wnd_, WM_NULL, 0, 0);
  DestroyMenu(menu);

  if (cmd == kCmdToggle) {
    ToggleEnabled();
  } else if (cmd == kCmdSettings) {
    OpenSettings();
  } else if (cmd == kCmdQuit) {
    PostMessageW(wnd_, WM_CLOSE, 0, 0);
  } else if (cmd >= kCmdCrosshairBase && cmd < kCmdCrosshairBase + lib.size()) {
    SelectCrosshair(lib[cmd - kCmdCrosshairBase]->id);
  }
}

}  // namespace sl

int WINAPI wWinMain(HINSTANCE hinst, HINSTANCE, PWSTR cmdLine, int) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  const std::wstring args = cmdLine ? cmdLine : L"";
  if (args.find(L"--quit") != std::wstring::npos) {
    if (HWND w = sl::ipc::CoreWindow()) PostMessageW(w, WM_CLOSE, 0, 0);
    return 0;
  }

  HANDLE mutex = CreateMutexW(nullptr, TRUE, sl::ipc::kCoreMutex);
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    // Second launch opens the settings of the running instance.
    if (HWND w = sl::ipc::CoreWindow()) PostMessageW(w, sl::ipc::kMsgOpenSettings, 0, 0);
    if (mutex) CloseHandle(mutex);
    return 0;
  }

  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  sl::CoreApp app;
  const int rc = app.Run(hinst);
  CoUninitialize();
  if (mutex) {
    ReleaseMutex(mutex);
    CloseHandle(mutex);
  }
  return rc;
}
