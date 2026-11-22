#pragma once

#include "common/model.h"

#include <cstdint>
#include <vector>

namespace sl {

// Premultiplied BGRA pixels, top-down rows. One uint32 = 0xAARRGGBB (little-endian bytes B, G, R, A),
// which matches both 32-bit DIB sections and DXGI_FORMAT_B8G8R8A8_UNORM.
struct Image {
  int width = 0;
  int height = 0;
  std::vector<uint32_t> pixels;
};

// Half-extent in pixels from the center pixel to the farthest drawn pixel, including outline and margin.
int CrosshairExtent(const Crosshair& c);

// Odd surface size that fits the crosshair with its center on the middle pixel.
inline int CrosshairSurfaceSize(const Crosshair& c) { return CrosshairExtent(c) * 2 + 1; }

// The whole crosshair shares one centre: a pixel corner when the first pixel-snapped element
// (line thickness or square dot) is even, a pixel centre when it is odd.
bool CrosshairEvenCenter(const Crosshair& c);
// Pixel size actually drawn for a line / square dot: mismatched parity is grown by 1 px so the
// element stays symmetric. The editor uses this to show the effective size.
inline int SymmetricPixels(int px, bool evenCenter) { return ((px % 2 == 0) == evenCenter) ? px : px + 1; }

// CPU rasteriser shared by the overlay and the settings previews, so both show identical pixels.
// Axis-aligned shapes land on whole pixels (sharp 1 px lines); everything else uses 4×4 supersampling.
Image RasterizeCrosshair(const Crosshair& c);

// The Sightline mark rasterised for window and tray icons.
Image RasterizeLogo(int size);

}  // namespace sl
