#include "common/logo.h"

#include "common/crosshair_render.h"

#include <algorithm>
#include <vector>

namespace sl {

void DrawLogo(ID2D1RenderTarget* rt, const D2D1_RECT_F& box, D2D1_COLOR_F ink, D2D1_COLOR_F accent) {
  // Monogram S on a 48-unit grid; the drawn glyph spans units 10..38, which is mapped onto `box`.
  const float side = std::min(box.right - box.left, box.bottom - box.top);
  const float s = side / 28.f;
  const float ox = box.left + ((box.right - box.left) - side) * 0.5f - 10.f * s;
  const float oy = box.top + ((box.bottom - box.top) - side) * 0.5f - 10.f * s;
  const auto P = [&](float x, float y) { return D2D1::Point2F(ox + x * s, oy + y * s); };

  ComPtr<ID2D1Factory> factory;
  rt->GetFactory(&factory);
  ComPtr<ID2D1PathGeometry> geo;
  ComPtr<ID2D1StrokeStyle> round;
  if (!factory || FAILED(factory->CreatePathGeometry(&geo))) return;
  factory->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                         D2D1_LINE_JOIN_ROUND),
                             nullptr, 0, &round);
  ComPtr<ID2D1GeometrySink> sink;
  if (FAILED(geo->Open(&sink))) return;
  // Upper stroke: bar to the left, then a half-turn down the left side.
  sink->BeginFigure(P(33, 13), D2D1_FIGURE_BEGIN_HOLLOW);
  sink->AddLine(P(22, 13));
  sink->AddArc(D2D1::ArcSegment(P(22, 27), D2D1::SizeF(7 * s, 7 * s), 0, D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,
                                D2D1_ARC_SIZE_SMALL));
  sink->EndFigure(D2D1_FIGURE_END_OPEN);
  // Lower stroke: bar to the right, then a half-turn up the right side.
  sink->BeginFigure(P(15, 35), D2D1_FIGURE_BEGIN_HOLLOW);
  sink->AddLine(P(26, 35));
  sink->AddArc(D2D1::ArcSegment(P(26, 21), D2D1::SizeF(7 * s, 7 * s), 0, D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,
                                D2D1_ARC_SIZE_SMALL));
  sink->EndFigure(D2D1_FIGURE_END_OPEN);
  sink->Close();

  ComPtr<ID2D1SolidColorBrush> brush;
  rt->CreateSolidColorBrush(ink, &brush);
  if (!brush) return;
  rt->DrawGeometry(geo.Get(), brush.Get(), 3.2f * s, round.Get());
  brush->SetColor(accent);
  rt->FillEllipse(D2D1::Ellipse(P(24, 24), 3.f * s, 3.f * s), brush.Get());
}

HICON CreateLogoIcon(int size) {
  // CPU rasteriser: no Direct2D/WIC needed, so the core never loads a graphics stack for its icon.
  const Image img = RasterizeLogo(size);
  std::vector<BYTE> px(img.pixels.size() * 4);
  memcpy(px.data(), img.pixels.data(), px.size());
  // Icons expect straight alpha.
  for (size_t i = 0; i < px.size(); i += 4) {
    const BYTE a = px[i + 3];
    if (a && a < 255) {
      for (int c = 0; c < 3; ++c) px[i + c] = static_cast<BYTE>(std::min(255, px[i + c] * 255 / a));
    }
  }

  BITMAPV5HEADER bi{};
  bi.bV5Size = sizeof(bi);
  bi.bV5Width = size;
  bi.bV5Height = -size;
  bi.bV5Planes = 1;
  bi.bV5BitCount = 32;
  bi.bV5Compression = BI_BITFIELDS;
  bi.bV5RedMask = 0x00FF0000;
  bi.bV5GreenMask = 0x0000FF00;
  bi.bV5BlueMask = 0x000000FF;
  bi.bV5AlphaMask = 0xFF000000;
  void* bits = nullptr;
  HDC dc = GetDC(nullptr);
  HBITMAP color = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS, &bits, nullptr, 0);
  ReleaseDC(nullptr, dc);
  if (!color) return nullptr;
  memcpy(bits, px.data(), px.size());
  HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
  ICONINFO ii{};
  ii.fIcon = TRUE;
  ii.hbmColor = color;
  ii.hbmMask = mask;
  HICON icon = CreateIconIndirect(&ii);
  DeleteObject(color);
  DeleteObject(mask);
  return icon;
}

}  // namespace sl
