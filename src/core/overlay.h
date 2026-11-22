#pragma once

#include "common/crosshair_render.h"
#include "common/model.h"

#include <string>

namespace sl {

// Click-through, topmost, non-activating layered window that shows the crosshair.
// Pixels come from the shared CPU rasteriser and go into a tiny DIB shown with
// UpdateLayeredWindow, so no graphics stack is loaded in this process.
// The window is redrawn only when the crosshair changes.
class Overlay {
 public:
  bool Create(HINSTANCE hinst);
  void Destroy();

  void SetCrosshair(const Crosshair& c);
  void SetCenter(POINT screenPixel);
  void SetVisible(bool visible);
  void SetExcludeFromCapture(bool exclude);
  void BringToTop();

  HWND Hwnd() const { return hwnd_; }
  bool Visible() const { return visible_; }

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
  void Render();
  void Place();
  void ReleaseDib();

  HWND hwnd_ = nullptr;
  HDC memDc_ = nullptr;
  HBITMAP dib_ = nullptr;
  HGDIOBJ oldBmp_ = nullptr;
  void* dibBits_ = nullptr;
  int dibSize_ = 0;

  Image image_;
  std::string fingerprint_;
  int size_ = 0;
  POINT center_{0, 0};
  bool visible_ = false;
  bool hasCrosshair_ = false;
  bool exclude_ = false;
};

}  // namespace sl
