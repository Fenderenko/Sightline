// Build tool: writes the multi-size application icon from the shared logo rasteriser.
// Usage: mkicon.exe <output.ico>

#include "common/crosshair_render.h"

#include <cstdio>
#include <vector>

using namespace sl;

namespace {

#pragma pack(push, 1)
struct IconDir {
  uint16_t reserved = 0, type = 1, count = 0;
};
struct IconEntry {
  uint8_t width, height, colors = 0, reserved = 0;
  uint16_t planes = 1, bitCount = 32;
  uint32_t bytes, offset;
};
#pragma pack(pop)

// One 32-bit DIB icon image: header (double height), bottom-up straight-alpha BGRA, empty AND mask.
std::vector<uint8_t> DibImage(int size) {
  const Image img = RasterizeLogo(size);
  BITMAPINFOHEADER bi{};
  bi.biSize = sizeof(bi);
  bi.biWidth = size;
  bi.biHeight = size * 2;
  bi.biPlanes = 1;
  bi.biBitCount = 32;
  bi.biCompression = BI_RGB;
  const size_t maskStride = ((size + 31) / 32) * 4;
  std::vector<uint8_t> out(sizeof(bi) + static_cast<size_t>(size) * size * 4 + maskStride * size, 0);
  memcpy(out.data(), &bi, sizeof(bi));
  uint8_t* px = out.data() + sizeof(bi);
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      const uint32_t p = img.pixels[static_cast<size_t>(size - 1 - y) * size + x];  // bottom-up rows
      const uint32_t a = p >> 24;
      uint8_t* d = px + (static_cast<size_t>(y) * size + x) * 4;
      for (int c = 0; c < 3; ++c) {
        const uint32_t v = (p >> (8 * c)) & 0xFF;
        d[c] = static_cast<uint8_t>(a ? std::min<uint32_t>(255, v * 255 / a) : 0);  // un-premultiply
      }
      d[3] = static_cast<uint8_t>(a);
    }
  }
  return out;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: mkicon <output.ico>\n");
    return 1;
  }
  const int sizes[] = {16, 20, 24, 32, 40, 48, 64, 96, 128, 256};
  std::vector<std::vector<uint8_t>> images;
  for (const int s : sizes) images.push_back(DibImage(s));

  IconDir dir;
  dir.count = static_cast<uint16_t>(std::size(sizes));
  std::vector<IconEntry> entries;
  uint32_t offset = static_cast<uint32_t>(sizeof(IconDir) + sizeof(IconEntry) * std::size(sizes));
  for (size_t i = 0; i < std::size(sizes); ++i) {
    IconEntry e{};
    e.width = static_cast<uint8_t>(sizes[i] >= 256 ? 0 : sizes[i]);  // 0 means 256
    e.height = e.width;
    e.planes = 1;
    e.bitCount = 32;
    e.bytes = static_cast<uint32_t>(images[i].size());
    e.offset = offset;
    offset += e.bytes;
    entries.push_back(e);
  }
  FILE* f = nullptr;
  if (_wfopen_s(&f, argv[1], L"wb") || !f) {
    fwprintf(stderr, L"cannot write %s\n", argv[1]);
    return 1;
  }
  fwrite(&dir, sizeof(dir), 1, f);
  fwrite(entries.data(), sizeof(IconEntry), entries.size(), f);
  for (const auto& img : images) fwrite(img.data(), 1, img.size(), f);
  fclose(f);
  return 0;
}
