#include "common/crosshair_render.h"
#include "common/i18n.h"
#include "common/ipc.h"
#include "common/logo.h"
#include "settings/app.h"

#include <dwmapi.h>
#include <shobjidl.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>

namespace sl {

namespace {
constexpr UINT_PTR kTimerSave = 1;
constexpr UINT_PTR kTimerCaret = 2;
constexpr UINT_PTR kTimerToast = 3;
constexpr UINT_PTR kTimerStatus = 4;
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"Sightline";
constexpr wchar_t kPersonalizeKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
SettingsApp* g_app = nullptr;

bool SystemPrefersDark() {
  DWORD v = 1, size = sizeof(v);
  if (RegGetValueW(HKEY_CURRENT_USER, kPersonalizeKey, L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &size) ==
      ERROR_SUCCESS) {
    return v == 0;
  }
  return false;
}

// Modal shell file dialog filtered to *.json. Returns an empty path when cancelled.
std::wstring PickJsonFile(HWND owner, bool save) {
  ComPtr<IFileDialog> dlg;
  const HRESULT hr = save ? CoCreateInstance(__uuidof(FileSaveDialog), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg))
                          : CoCreateInstance(__uuidof(FileOpenDialog), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg));
  if (FAILED(hr)) return {};
  const COMDLG_FILTERSPEC spec[] = {{L"Sightline (*.json)", L"*.json"}, {L"*.*", L"*.*"}};
  dlg->SetFileTypes(2, spec);
  dlg->SetDefaultExtension(L"json");
  if (save) dlg->SetFileName(L"sightline-library.json");
  if (FAILED(dlg->Show(owner))) return {};
  ComPtr<IShellItem> item;
  if (FAILED(dlg->GetResult(&item))) return {};
  PWSTR path = nullptr;
  if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) || !path) return {};
  std::wstring out = path;
  CoTaskMemFree(path);
  return out;
}

// Small sample layer per shape for the editor's shape picker icons.
Layer SampleLayer(Shape s) {
  Layer l;
  l.shape = s;
  l.thickness = 2;
  l.gap = 2;
  l.length = 4;
  switch (s) {
    case Shape::Plus: l.length = 5; break;
    case Shape::Dot: l.size = 5; l.round = true; break;
    case Shape::Ring: l.size = 6; l.thickness = 1.5f; break;
    case Shape::Box: l.size = 5; l.thickness = 1; break;
    case Shape::Diamond: l.size = 6; l.thickness = 1; break;
    case Shape::Triangle: l.size = 7; l.thickness = 1; break;
    case Shape::Corners: l.gap = 6; l.length = 3; l.thickness = 1; break;
    case Shape::Chevron: l.gap = 0; l.length = 6; l.thickness = 1.5f; break;
    case Shape::Segments: l.size = 7; l.thickness = 1.5f; l.segments = 4; l.segmentGap = 40; break;
    case Shape::Line: l.length = 6; l.gap = 2; break;
    default: break;
  }
  return l;
}
}  // namespace

int SettingsApp::Run(HINSTANCE hinst, int show) {
  g_app = this;
  hinst_ = hinst;
  LoadConfig(cfg_);
  SetLanguage(cfg_.settings.language);
  if (!CoreOnline()) LaunchCore();

  WNDCLASSEXW wc{sizeof(wc)};
  wc.style = CS_DBLCLKS;
  wc.lpfnWndProc = &SettingsApp::WndProc;
  wc.hInstance = hinst;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.lpszClassName = ipc::kSettingsClass;
  RegisterClassExW(&wc);

  // Default client area 1280x820 DIP, clamped to the work area.
  const UINT sysDpi = GetDpiForSystem();
  const float s = sysDpi / 96.f;
  RECT frame{0, 0, static_cast<LONG>(1280 * s), static_cast<LONG>(820 * s)};
  AdjustWindowRectExForDpi(&frame, WS_OVERLAPPEDWINDOW, FALSE, 0, sysDpi);
  RECT wa{};
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
  const int w = std::min<int>(frame.right - frame.left, wa.right - wa.left);
  const int h = std::min<int>(frame.bottom - frame.top, wa.bottom - wa.top);
  const int x = wa.left + std::max(0, static_cast<int>((wa.right - wa.left - w) / 2));
  const int y = wa.top + std::max(0, static_cast<int>((wa.bottom - wa.top - h) / 2));
  hwnd_ = CreateWindowExW(0, ipc::kSettingsClass, L"Sightline", WS_OVERLAPPEDWINDOW, x, y, w, h, nullptr, nullptr, hinst,
                          nullptr);
  if (!hwnd_) return 1;
  ChangeWindowMessageFilterEx(hwnd_, WM_COPYDATA, MSGFLT_ALLOW, nullptr);
  ChangeWindowMessageFilterEx(hwnd_, ipc::kMsgActivate, MSGFLT_ALLOW, nullptr);

  ApplyTheme();

  // Multi-size .ico resource (16…256 px): load the exact sizes for this DPI so neither the title bar
  // nor the taskbar has to scale a bitmap.
  {
    const UINT dpi = GetDpiForWindow(hwnd_);
    const int bigPx = GetSystemMetricsForDpi(SM_CXICON, dpi), smallPx = GetSystemMetricsForDpi(SM_CXSMICON, dpi);
    if (auto* icon = static_cast<HICON>(LoadImageW(hinst, MAKEINTRESOURCEW(1), IMAGE_ICON, bigPx, bigPx, LR_DEFAULTCOLOR))) {
      SendMessageW(hwnd_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
    }
    if (auto* icon = static_cast<HICON>(LoadImageW(hinst, MAKEINTRESOURCEW(1), IMAGE_ICON, smallPx, smallPx, LR_DEFAULTCOLOR))) {
      SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
    }
  }

  dpi_ = static_cast<float>(GetDpiForWindow(hwnd_));
  if (!CreateDevice()) {
    MessageBoxW(hwnd_, L"Sightline: Direct2D initialisation failed.", L"Sightline", MB_ICONERROR);
    return 2;
  }
  SetTimer(hwnd_, kTimerStatus, 2000, nullptr);
  ShowWindow(hwnd_, show);
  UpdateWindow(hwnd_);

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  if (capture_ != Capture::None) ipc::Send(ipc::CoreWindow(), hwnd_, ipc::kResumeHotkeys, {});
  SaveConfig(cfg_);
  return 0;
}

bool SettingsApp::CreateDevice() {
  const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                      D3D_FEATURE_LEVEL_10_0};
  HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
                                 static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, &d3d_, nullptr, nullptr);
  if (FAILED(hr)) {
    hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
                           static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, &d3d_, nullptr, nullptr);
  }
  if (FAILED(hr)) return false;
  ComPtr<IDXGIDevice> dxgi;
  d3d_.As(&dxgi);
  ComPtr<IDXGIAdapter> adapter;
  dxgi->GetAdapter(&adapter);
  ComPtr<IDXGIFactory2> factory;
  adapter->GetParent(IID_PPV_ARGS(&factory));

  RECT rc;
  GetClientRect(hwnd_, &rc);
  DXGI_SWAP_CHAIN_DESC1 d{};
  d.Width = static_cast<UINT>(std::max<LONG>(1, rc.right));
  d.Height = static_cast<UINT>(std::max<LONG>(1, rc.bottom));
  d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  d.SampleDesc.Count = 1;
  d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  d.BufferCount = 2;
  d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  d.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
  hr = factory->CreateSwapChainForHwnd(d3d_.Get(), hwnd_, &d, nullptr, nullptr, &swap_);
  if (FAILED(hr)) return false;

  D2D1_FACTORY_OPTIONS opts{};
  hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &opts,
                         reinterpret_cast<void**>(d2dFactory_.GetAddressOf()));
  if (SUCCEEDED(hr)) hr = d2dFactory_->CreateDevice(dxgi.Get(), &d2dDevice_);
  if (SUCCEEDED(hr)) hr = d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc_);
  if (SUCCEEDED(hr)) {
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory1),
                             reinterpret_cast<IUnknown**>(dw_.GetAddressOf()));
  }
  if (FAILED(hr)) return false;
  dc_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
  ResizeTarget();
  dc_->SetUnitMode(D2D1_UNIT_MODE_DIPS);
  previews_.clear();
  return ui_.Init(dc_.Get(), dw_.Get(), hwnd_);
}

void SettingsApp::ResizeTarget() {
  if (!swap_) return;
  dc_->SetTarget(nullptr);
  target_.Reset();
  RECT rc;
  GetClientRect(hwnd_, &rc);
  swap_->ResizeBuffers(0, static_cast<UINT>(std::max<LONG>(1, rc.right)), static_cast<UINT>(std::max<LONG>(1, rc.bottom)),
                       DXGI_FORMAT_UNKNOWN, 0);
  ComPtr<IDXGISurface> surface;
  swap_->GetBuffer(0, IID_PPV_ARGS(&surface));
  const auto props = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                                             D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), dpi_,
                                             dpi_);
  dc_->CreateBitmapFromDxgiSurface(surface.Get(), &props, &target_);
  dc_->SetTarget(target_.Get());
  dc_->SetDpi(dpi_, dpi_);
}

const SettingsApp::Preview* SettingsApp::PreviewFor(const Crosshair& c) {
  auto it = previews_.find(c.id);
  if (it == previews_.end()) return nullptr;
  return it->second.bitmap ? &it->second : nullptr;
}

// Re-rasterises `p` when `c` changed since the last call. Same rasteriser as the overlay, so the
// preview matches the in-game pixels exactly.
bool SettingsApp::RasterizeInto(Preview& p, const Crosshair& c) {
  std::string fp = CrosshairFingerprint(c);
  if (p.bitmap && p.fingerprint == fp) return true;
  const Image img = RasterizeCrosshair(c);
  ComPtr<ID2D1Bitmap1> bmp;
  const auto props = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,
                                             D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                                             96.f, 96.f);
  if (FAILED(dc_->CreateBitmap(D2D1::SizeU(static_cast<UINT>(img.width), static_cast<UINT>(img.height)), img.pixels.data(),
                               static_cast<UINT32>(img.width * 4), &props, &bmp))) {
    return false;
  }
  p.bitmap = bmp;
  p.size = img.width;
  p.fingerprint = std::move(fp);
  return true;
}

void SettingsApp::UpdatePreviews() {
  for (const Crosshair* c : Library(cfg_)) RasterizeInto(previews_[c->id], *c);
}

const SettingsApp::Preview* SettingsApp::DraftPreview() {
  Preview& p = previews_["~draft"];
  return RasterizeInto(p, draft_) ? &p : nullptr;
}

const SettingsApp::Preview* SettingsApp::ShapeIcon(Shape s) {
  const std::string key = "~shape." + std::to_string(static_cast<int>(s));
  Preview& p = previews_[key];
  if (p.bitmap) return &p;
  Crosshair c;
  c.layers.push_back(SampleLayer(s));
  const Image img = RasterizeCrosshair(c);
  const auto props = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,
                                             D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                                             96.f, 96.f);
  if (FAILED(dc_->CreateBitmap(D2D1::SizeU(static_cast<UINT>(img.width), static_cast<UINT>(img.height)), img.pixels.data(),
                               static_cast<UINT32>(img.width * 4), &props, &p.bitmap))) {
    return nullptr;
  }
  p.size = img.width;
  return &p;
}

int SettingsApp::DrawPreview(const Crosshair& c, D2D1_RECT_F box, float maxZoom, int backdrop, bool grid, float radius) {
  return DrawBitmap(PreviewFor(c), box, maxZoom, backdrop, grid, 0.86f, radius);
}

// The crosshair is magnified by an integer factor of device pixels with nearest-neighbour sampling,
// so every in-game pixel stays visible and sharp.
int SettingsApp::DrawBitmap(const Preview* p, D2D1_RECT_F box, float maxZoom, int backdrop, bool grid, float fit,
                            float radius) {
  const auto& t = ui_.th();
  const float w = box.right - box.left, h = box.bottom - box.top;
  if (backdrop == 2) ui_.FillRound(box, radius, D2D1::ColorF(0.80f, 0.80f, 0.79f));
  else if (backdrop == 1) ui_.FillRound(box, radius, D2D1::ColorF(0.075f, 0.08f, 0.085f));
  else if (backdrop == 0) ui_.FillRound(box, radius, t.tile);
  if (!p) return 0;
  const float pxPerDip = Scale();
  float zoom = std::floor(std::min(w, h) * fit * pxPerDip / p->size);
  zoom = std::clamp(zoom, 1.f, std::max(1.f, maxZoom));
  const float sizeDip = p->size * zoom / pxPerDip;
  const float cx = std::round(((box.left + box.right) * 0.5f) * pxPerDip) / pxPerDip;
  const float cy = std::round(((box.top + box.bottom) * 0.5f) * pxPerDip) / pxPerDip;
  const float half = std::round(p->size / 2.f * zoom) / pxPerDip;
  const D2D1_RECT_F dst = ui::Rc(cx - half, cy - half, cx - half + sizeDip, cy - half + sizeDip);
  ui_.PushClip(box);
  if (grid && zoom >= 5 && backdrop != 2) {
    // Pixel grid stays inside the rounded corners.
    const float cell = zoom / pxPerDip;
    const D2D1_COLOR_F hair = D2D1::ColorF(1, 1, 1, 0.045f);
    const D2D1_RECT_F g = ui::Rc(box.left + radius, box.top + radius, box.right - radius, box.bottom - radius);
    const float x0 = dst.left - std::floor((dst.left - g.left) / cell) * cell;
    const float y0 = dst.top - std::floor((dst.top - g.top) / cell) * cell;
    for (float x = x0; x < g.right; x += cell) ui_.VLine(x, g.top, g.bottom, hair);
    for (float y = y0; y < g.bottom; y += cell) ui_.HLine(g.left, g.right, y, hair);
  }
  dc_->DrawBitmap(p->bitmap.Get(), &dst, 1.f, D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR, nullptr, nullptr);
  ui_.PopClip();
  return static_cast<int>(zoom);
}

void SettingsApp::ApplyTheme() {
  const std::string& t = cfg_.settings.theme;
  const bool dark = t == "dark" || (t != "light" && SystemPrefersDark());
  ui_.SetTheme(dark ? ui::DarkTheme() : ui::LightTheme());
  if (!hwnd_) return;
  const BOOL useDark = dark ? TRUE : FALSE;
  DwmSetWindowAttribute(hwnd_, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &useDark, sizeof(useDark));
  const COLORREF caption = dark ? RGB(0x2B, 0x2E, 0x31) : RGB(0xEC, 0xE8, 0xE0);
  const COLORREF border = dark ? RGB(0x3A, 0x3E, 0x41) : RGB(0xD6, 0xD1, 0xC6);
  const COLORREF text = dark ? RGB(0xE9, 0xE7, 0xE1) : RGB(0x17, 0x18, 0x1A);
  DwmSetWindowAttribute(hwnd_, 35 /*DWMWA_CAPTION_COLOR*/, &caption, sizeof(caption));
  DwmSetWindowAttribute(hwnd_, 34 /*DWMWA_BORDER_COLOR*/, &border, sizeof(border));
  DwmSetWindowAttribute(hwnd_, 36 /*DWMWA_TEXT_COLOR*/, &text, sizeof(text));
  Invalidate();
}

void SettingsApp::RunFileAction(FileAction a) {
  if (a == FileAction::Export) {
    const std::wstring path = PickJsonFile(hwnd_, true);
    if (path.empty()) return;
    if (WriteFileAtomic(path, ExportLibrary(cfg_))) Toast(T("toast.exported"));
    else Toast(T("toast.saveFailed"));
  } else if (a == FileAction::Import) {
    const std::wstring path = PickJsonFile(hwnd_, false);
    if (path.empty()) return;
    std::string text;
    const bool wasDirty = DraftDirty();
    const int n = ReadFileUtf8(path, text) ? ImportLibrary(text, cfg_) : -1;
    if (n < 0) {
      Toast(T("toast.importFailed"));
      return;
    }
    // A clean draft follows the imported state (an imported preset override may replace the active one).
    if (!wasDirty) draftId_.clear();
    Toast(std::wstring(T("toast.importedFile")) + L" " + std::to_wstring(n));
    PushConfig();
  }
}

void SettingsApp::ImportCode() {
  Crosshair imported;
  const CodeSource src = ImportCrosshairCode(Narrow(importCode_), imported);
  if (src == CodeSource::None) {
    Toast(T("toast.badCode"));
    return;
  }
  importCode_.clear();
  // SL1 codes serialise the sender's hotkey; a shared crosshair must not steal a binding here.
  imported.hotkey = {};
  imported.favorite = false;
  // Added to the library right away; only the switch to it waits for the unsaved-changes answer.
  cfg_.crosshairs.push_back(imported);
  Toast(src == CodeSource::Valorant ? T("toast.importedValorant")
        : src == CodeSource::Cs2    ? T("toast.importedCs2")
                                    : T("toast.imported"));
  PushConfig();
  Activate(imported.id);
}

void SettingsApp::Frame() {
  if (inFrame_) {
    // Re-entered from a synchronous message sent during drawing; repaint once the outer frame ends.
    ui_.wantRedraw = true;
    return;
  }
  if (!dc_ || !target_) return;
  inFrame_ = true;
  cfg_.crosshairs.reserve(cfg_.crosshairs.size() + 8);  // keeps Library() pointers stable for this frame
  UpdatePreviews();
  RECT rc;
  GetClientRect(hwnd_, &rc);
  const D2D1_SIZE_F size = D2D1::SizeF(rc.right / Scale(), rc.bottom / Scale());

  dc_->BeginDraw();
  dc_->SetTransform(D2D1::Matrix3x2F::Identity());
  ui_.Begin(in_, size, Scale());
  pickerSeen_ = false;
  pickerTarget_ = nullptr;
  ui_.SetModal(popup_ != Popup::None);
  Chrome();
  ui_.End();
  const HRESULT hr = dc_->EndDraw();
  if (SUCCEEDED(hr)) swap_->Present(1, 0);
  inFrame_ = false;
  if (hr == D2DERR_RECREATE_TARGET) {
    dc_.Reset();
    target_.Reset();
    swap_.Reset();
    CreateDevice();
  }

  if (pendingTheme_) {
    pendingTheme_ = false;
    ApplyTheme();
  }
  if (!pendingDelete_.empty()) {
    RemoveUserCrosshair(cfg_, pendingDelete_);
    previews_.erase(pendingDelete_);
    pendingDelete_.clear();
    PushConfig();
    ui_.wantRedraw = true;
  }
  SyncPreview();
  in_.EndFrame();
  if (pendingClose_) {
    pendingClose_ = false;
    DestroyWindow(hwnd_);
    return;
  }
  if (pendingFile_ != FileAction::None) {
    // Modal dialogs run outside BeginDraw/EndDraw; the nested message loop may repaint.
    const FileAction a = pendingFile_;
    pendingFile_ = FileAction::None;
    RunFileAction(a);
    ui_.wantRedraw = true;
  }
  if (ui_.wantRedraw) Invalidate();
  SetCursor(LoadCursorW(nullptr, ui_.cursor == ui::Cursor::Hand    ? IDC_HAND
                                 : ui_.cursor == ui::Cursor::IBeam ? IDC_IBEAM
                                                                   : IDC_ARROW));
  if (ui_.AnyFocus()) SetTimer(hwnd_, kTimerCaret, 530, nullptr);
  else KillTimer(hwnd_, kTimerCaret);
}

void SettingsApp::PushConfig() {
  ipc::Send(ipc::CoreWindow(), hwnd_, ipc::kApplyConfig, ConfigToString(cfg_, false));
  ScheduleSave();
  Invalidate();
}

void SettingsApp::ScheduleSave() { SetTimer(hwnd_, kTimerSave, 400, nullptr); }

void SettingsApp::Toast(std::wstring text) {
  toast_ = std::move(text);
  toastUntil_ = GetTickCount64() + 2600;
  SetTimer(hwnd_, kTimerToast, 2700, nullptr);
  Invalidate();
}

void SettingsApp::StartCapture(Capture c, std::string crosshairId) {
  capture_ = c;
  captureCrosshair_ = std::move(crosshairId);
  ipc::Send(ipc::CoreWindow(), hwnd_, ipc::kSuspendHotkeys, {});
  Invalidate();
}

void SettingsApp::FinishCapture(Hotkey hk, bool clear) {
  Settings& s = cfg_.settings;
  if (capture_ == Capture::Ads) {
    s.adsKey = clear ? 0 : hk.vk;
  } else {
    Hotkey* slots[] = {&s.hkToggle, &s.hkNext, &s.hkPrev, &s.hkSettings};
    Hotkey* target = capture_ == Capture::Crosshair
                         ? &EditableCrosshair(cfg_, captureCrosshair_).hotkey
                         : slots[static_cast<int>(capture_) - static_cast<int>(Capture::Toggle)];
    if (clear) {
      *target = {};
    } else {
      // One shortcut, one action: take it away from global slots and other crosshairs.
      bool moved = false;
      for (Hotkey* other : slots) {
        if (other != target && *other == hk) {
          *other = {};
          moved = true;
        }
      }
      // Nudge owns modifiers + arrow keys while it is on.
      const bool arrow = hk.vk == VK_UP || hk.vk == VK_DOWN || hk.vk == VK_LEFT || hk.vk == VK_RIGHT;
      if (arrow && s.nudgeMods != 0 && hk.mods == s.nudgeMods) {
        s.nudgeMods = 0;
        moved = true;
      }
      std::vector<std::string> owners;
      for (const Crosshair* c : Library(cfg_)) {
        if (c->hotkey == hk && !(capture_ == Capture::Crosshair && c->id == captureCrosshair_)) owners.push_back(c->id);
      }
      for (const std::string& id : owners) {
        EditableCrosshair(cfg_, id).hotkey = {};
        moved = true;
      }
      // EditableCrosshair may reallocate the crosshair list; resolve the target again.
      if (capture_ == Capture::Crosshair) target = &EditableCrosshair(cfg_, captureCrosshair_).hotkey;
      *target = hk;
      if (moved) Toast(std::wstring(T("toast.hotkeyMoved")) + L" " + HotkeyToText(hk));
    }
  }
  capture_ = Capture::None;
  captureCrosshair_.clear();
  ipc::Send(ipc::CoreWindow(), hwnd_, ipc::kResumeHotkeys, {});
  PushConfig();
}

bool SettingsApp::HandleCaptureKey(UINT msg, WPARAM wp) {
  if (capture_ == Capture::None) return false;
  UINT vk = 0;
  switch (msg) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
      vk = static_cast<UINT>(wp);
      break;
    case WM_RBUTTONDOWN: vk = VK_RBUTTON; break;
    case WM_MBUTTONDOWN: vk = VK_MBUTTON; break;
    case WM_XBUTTONDOWN: vk = GET_XBUTTON_WPARAM(wp) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2; break;
    case WM_LBUTTONDOWN:
      capture_ = Capture::None;
      ipc::Send(ipc::CoreWindow(), hwnd_, ipc::kResumeHotkeys, {});
      Invalidate();
      return false;  // let the click reach the UI
    default: return false;
  }
  switch (vk) {
    case VK_SHIFT: case VK_CONTROL: case VK_MENU: case VK_LWIN: case VK_RWIN:
    case VK_LSHIFT: case VK_RSHIFT: case VK_LCONTROL: case VK_RCONTROL: case VK_LMENU: case VK_RMENU:
      return true;  // wait for the main key
    default: break;
  }
  if (vk == VK_ESCAPE) {
    FinishCapture({}, true);
    return true;
  }
  const bool mouse = vk == VK_RBUTTON || vk == VK_MBUTTON || vk == VK_XBUTTON1 || vk == VK_XBUTTON2;
  if (capture_ != Capture::Ads && mouse) {
    Toast(T("toast.mouseNotAllowed"));
    return true;
  }
  Hotkey hk;
  hk.vk = vk;
  if (capture_ != Capture::Ads) {
    if (GetKeyState(VK_CONTROL) < 0) hk.mods |= MOD_CONTROL;
    if (GetKeyState(VK_SHIFT) < 0) hk.mods |= MOD_SHIFT;
    if (GetKeyState(VK_MENU) < 0) hk.mods |= MOD_ALT;
    if (GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0) hk.mods |= MOD_WIN;
    // A global hotkey swallows its key system-wide, so bare typing keys need a modifier.
    // F-keys, numpad keys, Pause and Scroll Lock are allowed on their own.
    const bool standalone = (vk >= VK_F1 && vk <= VK_F24) || (vk >= VK_NUMPAD0 && vk <= VK_DIVIDE) || vk == VK_PAUSE ||
                            vk == VK_SCROLL;
    if (hk.mods == 0 && !standalone) {
      Toast(T("toast.needModifier"));
      return true;  // keep capturing
    }
  }
  FinishCapture(hk, false);
  return true;
}

void SettingsApp::ApplyAutostart(bool on) {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return;
  if (on) {
    const std::wstring cmd = L"\"" + ExeDir() + L"\\" + ipc::kCoreExe + L"\"";
    RegSetValueExW(key, kRunValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()),
                   static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
  } else {
    RegDeleteValueW(key, kRunValue);
  }
  RegCloseKey(key);
}

bool SettingsApp::CoreOnline() const { return ipc::CoreWindow() != nullptr; }

void SettingsApp::LaunchCore() {
  const std::wstring path = ExeDir() + L"\\" + ipc::kCoreExe;
  STARTUPINFOW si{sizeof(si)};
  PROCESS_INFORMATION pi{};
  std::wstring cmd = L"\"" + path + L"\"";
  if (CreateProcessW(path.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
    WaitForInputIdle(pi.hProcess, 3000);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
  }
}

std::vector<std::wstring> SettingsApp::RunningApps() const {
  struct Ctx {
    std::vector<std::wstring> out;
  } ctx;
  EnumWindows(
      [](HWND w, LPARAM lp) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(lp);
        if (!IsWindowVisible(w) || GetWindow(w, GW_OWNER) || (GetWindowLongW(w, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)) return TRUE;
        BOOL cloaked = FALSE;
        DwmGetWindowAttribute(w, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
        if (cloaked || GetWindowTextLengthW(w) == 0) return TRUE;
        std::wstring exe = WindowExeName(w);
        if (exe.empty() || exe == L"explorer.exe" || exe == ipc::kSettingsExe || exe == ipc::kCoreExe ||
            exe == L"textinputhost.exe" || exe == L"applicationframehost.exe") {
          return TRUE;
        }
        if (std::find(c->out.begin(), c->out.end(), exe) == c->out.end()) c->out.push_back(exe);
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&ctx));
  std::sort(ctx.out.begin(), ctx.out.end());
  return ctx.out;
}

LRESULT CALLBACK SettingsApp::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (g_app && g_app->hwnd_ == hwnd) return g_app->Handle(msg, wp, lp);
  if (g_app && !g_app->hwnd_ && msg == WM_NCCREATE) g_app->hwnd_ = hwnd;
  return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT SettingsApp::Handle(UINT msg, WPARAM wp, LPARAM lp) {
  const auto mousePos = [&]() {
    in_.mouse = D2D1::Point2F(GET_X_LPARAM(lp) / Scale(), GET_Y_LPARAM(lp) / Scale());
  };
  switch (msg) {
    case WM_PAINT: {
      ValidateRect(hwnd_, nullptr);
      Frame();
      return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_SIZE:
      if (wp != SIZE_MINIMIZED && swap_) {
        ResizeTarget();
        Frame();
      }
      return 0;
    case WM_DPICHANGED: {
      dpi_ = static_cast<float>(HIWORD(wp));
      const RECT* r = reinterpret_cast<const RECT*>(lp);
      SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
      ResizeTarget();
      Invalidate();
      return 0;
    }
    case WM_GETMINMAXINFO: {
      auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
      // Minimum client area 1080x700 DIP.
      RECT r{0, 0, static_cast<LONG>(1080 * Scale()), static_cast<LONG>(700 * Scale())};
      AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, 0, static_cast<UINT>(dpi_));
      mmi->ptMinTrackSize = {r.right - r.left, r.bottom - r.top};
      return 0;
    }
    case WM_SETCURSOR:
      if (LOWORD(lp) == HTCLIENT) {
        SetCursor(LoadCursorW(nullptr, ui_.cursor == ui::Cursor::Hand    ? IDC_HAND
                                       : ui_.cursor == ui::Cursor::IBeam ? IDC_IBEAM
                                                                         : IDC_ARROW));
        return TRUE;
      }
      break;
    case WM_MOUSEMOVE: {
      mousePos();
      TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
      TrackMouseEvent(&tme);
      Invalidate();
      return 0;
    }
    case WM_MOUSELEAVE:
      in_.mouse = D2D1::Point2F(-1000, -1000);
      Invalidate();
      return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
      HandleCaptureKey(WM_LBUTTONDOWN, wp);
      mousePos();
      in_.down = true;
      in_.pressed = true;
      in_.doubleClick = msg == WM_LBUTTONDBLCLK;
      SetCapture(hwnd_);
      Invalidate();
      return 0;
    case WM_LBUTTONUP:
      mousePos();
      in_.down = false;
      in_.released = true;
      ReleaseCapture();
      Invalidate();
      return 0;
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_XBUTTONDOWN:
      if (HandleCaptureKey(msg, wp)) return msg == WM_XBUTTONDOWN ? TRUE : 0;
      break;
    case WM_RBUTTONUP:
      if (capture_ != Capture::None) return 0;  // the press was consumed by key capture
      mousePos();
      in_.rightClick = true;
      Invalidate();
      return 0;
    case WM_MOUSEWHEEL:
      in_.wheel += GET_WHEEL_DELTA_WPARAM(wp) / static_cast<float>(WHEEL_DELTA);
      lastWheel_ = GetTickCount64();
      Invalidate();
      return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
      if (HandleCaptureKey(msg, wp)) return 0;
      in_.keys.push_back(static_cast<UINT>(wp));
      in_.ctrl = GetKeyState(VK_CONTROL) < 0;
      in_.shift = GetKeyState(VK_SHIFT) < 0;
      Invalidate();
      if (msg == WM_SYSKEYDOWN) break;
      return 0;
    case WM_CHAR:
      in_.chars.push_back(static_cast<wchar_t>(wp));
      Invalidate();
      return 0;
    case WM_TIMER:
      if (wp == kTimerSave) {
        KillTimer(hwnd_, kTimerSave);
        SaveConfig(cfg_);
      } else if (wp == kTimerCaret) {
        ui_.caretOn = !ui_.caretOn;
        Invalidate();
      } else if (wp == kTimerToast) {
        KillTimer(hwnd_, kTimerToast);
        toast_.clear();
        Invalidate();
      } else if (wp == kTimerStatus) {
        Invalidate();  // refresh core status indicator
      }
      return 0;
    case WM_COPYDATA: {
      const auto* cds = reinterpret_cast<const COPYDATASTRUCT*>(lp);
      if (cds && cds->dwData == ipc::kConfigFromCore && cds->lpData) {
        Config next;
        if (ConfigFromString(std::string_view(static_cast<const char*>(cds->lpData), cds->cbData), next)) {
          // The core ends the draft preview when it switches the active crosshair itself; the next
          // frame either resends it or asks about the unsaved changes (SyncDraft).
          if (ResolveCrosshair(next, next.activeId).id != ActiveId()) sentPreview_.clear();
          cfg_ = std::move(next);
          SetLanguage(cfg_.settings.language);
          if (inFrame_) pendingTheme_ = true;
          else ApplyTheme();
          Invalidate();
        }
        return TRUE;
      }
      return FALSE;
    }
    case WM_SETTINGCHANGE:
      if (lp && lstrcmpiW(reinterpret_cast<LPCWSTR>(lp), L"ImmersiveColorSet") == 0) {
        if (inFrame_) pendingTheme_ = true;
        else ApplyTheme();
      }
      break;
    case ipc::kMsgActivate:
      if (IsIconic(hwnd_)) ShowWindow(hwnd_, SW_RESTORE);
      SetForegroundWindow(hwnd_);
      return 0;
    case WM_CLOSE:
      if (RequestClose()) DestroyWindow(hwnd_);
      return 0;
    case WM_DESTROY:
      // The unsaved draft must not stay on screen once the editor is gone.
      if (!sentPreview_.empty()) {
        ipc::Send(ipc::CoreWindow(), hwnd_, ipc::kPreviewCrosshair, {});
        sentPreview_.clear();
      }
      PostQuitMessage(0);
      return 0;
    default: break;
  }
  return DefWindowProcW(hwnd_, msg, wp, lp);
}

}  // namespace sl

int WINAPI wWinMain(HINSTANCE hinst, HINSTANCE, PWSTR, int show) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  HANDLE mutex = CreateMutexW(nullptr, TRUE, sl::ipc::kSettingsMutex);
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    if (HWND w = sl::ipc::SettingsWindow()) {
      if (IsIconic(w)) ShowWindow(w, SW_RESTORE);
      SetForegroundWindow(w);
    }
    if (mutex) CloseHandle(mutex);
    return 0;
  }
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  sl::SettingsApp app;
  const int rc = app.Run(hinst, show);
  CoUninitialize();
  if (mutex) {
    ReleaseMutex(mutex);
    CloseHandle(mutex);
  }
  return rc;
}
