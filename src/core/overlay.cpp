#include "core/overlay.h"

#include "common/ipc.h"

namespace sl {

namespace {
constexpr DWORD kExStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_LAYERED;
}

LRESULT CALLBACK Overlay::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_NCHITTEST: return HTTRANSPARENT;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
      // Content comes from UpdateLayeredWindow; BeginPaint only clears the update region so
      // WM_PAINT is not regenerated in a loop while the window is hidden.
      PAINTSTRUCT ps;
      BeginPaint(hwnd, &ps);
      EndPaint(hwnd, &ps);
      return 0;
    }
    default: return DefWindowProcW(hwnd, msg, wp, lp);
  }
}

bool Overlay::Create(HINSTANCE hinst) {
  WNDCLASSEXW wc{sizeof(wc)};
  wc.lpfnWndProc = &Overlay::WndProc;
  wc.hInstance = hinst;
  wc.lpszClassName = ipc::kOverlayClass;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  RegisterClassExW(&wc);

  // Layered + transparent: invisible to hit testing across processes (click-through).
  hwnd_ = CreateWindowExW(kExStyle, ipc::kOverlayClass, L"Sightline Overlay", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, hinst,
                          nullptr);
  if (!hwnd_) {
    Log(L"core", L"overlay: CreateWindowEx failed (%lu)", GetLastError());
    return false;
  }
  if (exclude_) SetExcludeFromCapture(true);
  return true;
}

void Overlay::Destroy() {
  ReleaseDib();
  if (hwnd_) DestroyWindow(hwnd_);
  hwnd_ = nullptr;
  size_ = 0;
  visible_ = false;
  fingerprint_.clear();
}

void Overlay::ReleaseDib() {
  if (memDc_) {
    if (oldBmp_) SelectObject(memDc_, oldBmp_);
    DeleteDC(memDc_);
  }
  if (dib_) DeleteObject(dib_);
  memDc_ = nullptr;
  dib_ = nullptr;
  oldBmp_ = nullptr;
  dibBits_ = nullptr;
  dibSize_ = 0;
}

void Overlay::Render() {
  if (size_ <= 0 || !hwnd_) return;
  if (dibSize_ != size_) {
    ReleaseDib();
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = size_;
    bi.bmiHeader.biHeight = -size_;  // top-down, matches the rasteriser's row order
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    memDc_ = CreateCompatibleDC(nullptr);
    dib_ = CreateDIBSection(memDc_, &bi, DIB_RGB_COLORS, &dibBits_, nullptr, 0);
    if (!memDc_ || !dib_) {
      ReleaseDib();
      return;
    }
    oldBmp_ = SelectObject(memDc_, dib_);
    dibSize_ = size_;
  }
  if (static_cast<int>(image_.pixels.size()) == size_ * size_) {
    memcpy(dibBits_, image_.pixels.data(), image_.pixels.size() * sizeof(uint32_t));
  } else {
    memset(dibBits_, 0, static_cast<size_t>(size_) * size_ * 4);
  }
  GdiFlush();

  POINT dst{center_.x - size_ / 2, center_.y - size_ / 2};
  SIZE sz{size_, size_};
  POINT src{0, 0};
  BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};  // premultiplied BGRA
  if (!UpdateLayeredWindow(hwnd_, nullptr, &dst, &sz, memDc_, &src, 0, &blend, ULW_ALPHA)) {
    Log(L"core", L"overlay: UpdateLayeredWindow failed (%lu)", GetLastError());
  }
}

void Overlay::Place() {
  if (!hwnd_ || size_ <= 0) return;
  SetWindowPos(hwnd_, HWND_TOPMOST, center_.x - size_ / 2, center_.y - size_ / 2, size_, size_,
               SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

void Overlay::SetCrosshair(const Crosshair& c) {
  std::string fp = CrosshairFingerprint(c);
  if (hasCrosshair_ && fp == fingerprint_) return;
  fingerprint_ = std::move(fp);
  hasCrosshair_ = true;
  image_ = RasterizeCrosshair(c);
  size_ = image_.width;
  Render();
}

void Overlay::SetCenter(POINT p) {
  if (p.x == center_.x && p.y == center_.y) return;
  center_ = p;
  Place();
}

void Overlay::SetVisible(bool visible) {
  if (visible == visible_ || !hwnd_) return;
  visible_ = visible;
  if (visible) {
    Place();
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
  } else {
    ShowWindow(hwnd_, SW_HIDE);
  }
}

void Overlay::SetExcludeFromCapture(bool exclude) {
  exclude_ = exclude;
  if (!hwnd_) return;
  if (!SetWindowDisplayAffinity(hwnd_, exclude ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE)) {
    Log(L"core", L"overlay: SetWindowDisplayAffinity failed (%lu)", GetLastError());
  }
}

void Overlay::BringToTop() {
  if (visible_) Place();
}

}  // namespace sl
