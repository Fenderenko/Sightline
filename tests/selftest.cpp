// Self-test: renders every preset into a contact sheet (BMP) and checks crosshair code importers.
// Usage: selftest.exe <output.bmp> [code ...]

#include "common/crosshair_render.h"
#include "common/i18n.h"
#include "common/model.h"

#include <cstdio>
#include <vector>

using namespace sl;

namespace {

bool WriteBmp(const wchar_t* path, int w, int h, const std::vector<uint32_t>& px) {
  BITMAPFILEHEADER fh{};
  BITMAPINFOHEADER ih{};
  ih.biSize = sizeof(ih);
  ih.biWidth = w;
  ih.biHeight = -h;
  ih.biPlanes = 1;
  ih.biBitCount = 32;
  fh.bfType = 0x4D42;
  fh.bfOffBits = sizeof(fh) + sizeof(ih);
  fh.bfSize = fh.bfOffBits + static_cast<DWORD>(px.size() * 4);
  FILE* f = nullptr;
  if (_wfopen_s(&f, path, L"wb") || !f) return false;
  fwrite(&fh, sizeof(fh), 1, f);
  fwrite(&ih, sizeof(ih), 1, f);
  fwrite(px.data(), 4, px.size(), f);
  fclose(f);
  return true;
}

const char* SourceName(CodeSource s) {
  switch (s) {
    case CodeSource::Sightline: return "Sightline";
    case CodeSource::Valorant: return "Valorant";
    case CodeSource::Cs2: return "CS2";
    default: return "none";
  }
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  const auto& presets = BuiltinPresets();
  constexpr int kCell = 72, kZoom = 3, kCols = 8;
  const int rows = (static_cast<int>(presets.size()) + kCols - 1) / kCols;
  const int w = kCols * kCell * kZoom, h = rows * kCell * kZoom;
  std::vector<uint32_t> sheet(static_cast<size_t>(w) * h, 0xFF3A3D40);  // mid grey backdrop
  for (size_t i = 0; i < presets.size(); ++i) {
    const Image img = RasterizeCrosshair(presets[i]);
    const int ox = static_cast<int>(i % kCols) * kCell + (kCell - img.width) / 2;
    const int oy = static_cast<int>(i / kCols) * kCell + (kCell - img.height) / 2;
    for (int y = 0; y < img.height; ++y) {
      for (int x = 0; x < img.width; ++x) {
        const uint32_t s = img.pixels[static_cast<size_t>(y) * img.width + x];
        const uint32_t a = s >> 24;
        if (!a) continue;
        for (int zy = 0; zy < kZoom; ++zy) {
          for (int zx = 0; zx < kZoom; ++zx) {
            const int px = (ox + x) * kZoom + zx, py = (oy + y) * kZoom + zy;
            if (px < 0 || py < 0 || px >= w || py >= h) continue;
            uint32_t& d = sheet[static_cast<size_t>(py) * w + px];
            uint32_t out = 0xFF000000;
            for (int c = 0; c < 24; c += 8) {
              const uint32_t sc = (s >> c) & 0xFF, dc = (d >> c) & 0xFF;
              out |= ((sc + dc * (255 - a) / 255) & 0xFF) << c;
            }
            d = out;
          }
        }
      }
    }
  }
  const wchar_t* outPath = argc > 1 ? argv[1] : L"presets.bmp";
  printf("presets: %zu, sheet %dx%d -> %s\n", presets.size(), w, h, WriteBmp(outPath, w, h, sheet) ? "ok" : "FAILED");

  // Symmetry: the alpha mask must mirror onto itself around the centre of its bounding box,
  // horizontally for every preset and vertically for all but the T-shape.
  int crooked = 0;
  // Mixed-parity user cases on top of the presets: 1 px lines + 2 px dot, 2 px lines + 3 px dot,
  // 1 px lines + 2 px plus, ring + 2 px dot.
  std::vector<Crosshair> cases(presets.begin(), presets.end());
  const auto mixed = [&](const char* name, std::initializer_list<Layer> layers) {
    Crosshair c;
    c.name = name;
    c.layers = layers;
    cases.push_back(c);
  };
  Layer thin;
  thin.length = 20;
  thin.thickness = 1;
  thin.gap = 1;
  Layer dot2;
  dot2.shape = Shape::Dot;
  dot2.size = 2;
  Layer dot3 = dot2;
  dot3.size = 3;
  Layer cross2;
  cross2.thickness = 2;
  Layer plus2;
  plus2.shape = Shape::Plus;
  plus2.thickness = 2;
  Layer ring;
  ring.shape = Shape::Ring;
  ring.size = 9;
  ring.thickness = 1.5f;
  mixed("mix t1 + dot2", {thin, dot2});
  mixed("mix t2 + dot3", {cross2, dot3});
  mixed("mix t1 + plus2", {thin, plus2});
  mixed("mix ring + dot2", {ring, dot2});
  // Blur: soft outline (glow), soft fill, both at once on a mixed-parity pair.
  Layer glow = thin;
  glow.outlineBlur = 3;
  Layer softDot = dot2;
  softDot.blur = 1.5f;
  Layer softAll = cross2;
  softAll.blur = 8;
  softAll.outlineBlur = 8;
  mixed("blur outline", {glow});
  mixed("blur t1 + dot2", {glow, softDot});
  mixed("blur max", {softAll});
  // Zero-length rays (old configs, imports) must still draw at least 1 px.
  Layer zero = cross2;
  zero.length = 0;
  mixed("zero length", {zero});
  int clipped = 0, empty = 0;
  for (const auto& p : cases) {
    const Image img = RasterizeCrosshair(p);
    const auto alpha = [&](int x, int y) { return static_cast<int>(img.pixels[static_cast<size_t>(y) * img.width + x] >> 24); };
    // The surface must leave a transparent border, otherwise the extent estimate cut something off.
    for (int i = 0; i < img.width; ++i) {
      if (alpha(i, 0) || alpha(i, img.height - 1) || alpha(0, i) || alpha(img.width - 1, i)) {
        ++clipped;
        printf("  %-18s CLIPPED\n", p.name.c_str());
        break;
      }
    }
    int x0 = img.width, y0 = img.height, x1 = -1, y1 = -1;
    for (int y = 0; y < img.height; ++y) {
      for (int x = 0; x < img.width; ++x) {
        if (alpha(x, y)) {
          x0 = std::min(x0, x);
          x1 = std::max(x1, x);
          y0 = std::min(y0, y);
          y1 = std::max(y1, y);
        }
      }
    }
    if (x1 < 0) {
      ++empty;
      printf("  %-18s EMPTY\n", p.name.c_str());
      continue;
    }
    int dh = 0, dv = 0;
    for (int y = y0; y <= y1; ++y) {
      for (int x = x0; x <= x1; ++x) {
        dh = std::max(dh, std::abs(alpha(x, y) - alpha(x0 + x1 - x, y)));
        dv = std::max(dv, std::abs(alpha(x, y) - alpha(x, y0 + y1 - y)));
      }
    }
    // T-shapes, chevrons and triangles are vertically asymmetric by design.
    bool vertical = true;
    for (const auto& L : p.layers) {
      if (L.shape == Shape::TShape || L.shape == Shape::Chevron || L.shape == Shape::Triangle) vertical = false;
    }
    const bool ok = dh <= 8 && (!vertical || dv <= 8);
    if (!ok) ++crooked;
    printf("  %-18s %s  h=%3d v=%3d\n", p.name.c_str(), ok ? "ok     " : "CROOKED", dh, dv);
  }
  printf("crooked presets: %d\n", crooked + clipped + empty);

  // Config round trip: a favourited preset is stored as metadata, an edited one as a full copy,
  // and share codes never carry the sender's hotkey.
  {
    Config cfg;
    Crosshair& fav = EditableCrosshair(cfg, "classic.cross");
    fav.favorite = true;
    fav.hotkey = {MOD_CONTROL | MOD_ALT, '1'};
    EditableCrosshair(cfg, "dot.round").layers[0].size = 7;
    const std::string text = ConfigToString(cfg, false);
    Config back;
    const bool parsed = ConfigFromString(text, back);
    const Crosshair* f = FindCrosshair(back, "classic.cross");
    const bool metaOk = text.find("\"meta\":true") != std::string::npos && f && f->favorite && f->hotkey.Valid() &&
                        !IsModifiedPreset(back, "classic.cross");
    const bool editOk = IsModifiedPreset(back, "dot.round") && FindCrosshair(back, "dot.round")->layers[0].size == 7;
    Crosshair decoded;
    const bool codeOk = DecodeShareCode(EncodeShareCode(*f), decoded) && !decoded.hotkey.Valid() && !decoded.favorite;
    printf("config round trip: parsed=%d meta=%d edited=%d sharecode=%d\n", parsed, metaOk, editOk, codeOk);
  }

  // Every language must translate every key and keep the printf specifiers of the English text.
  {
    int gaps = 0;
    for (const LanguageInfo& l : Languages()) {
      int badFormat = 0;
      const int missing = MissingTranslations(l.code, &badFormat);
      if (missing || badFormat) printf("  i18n %-2s missing=%d badFormat=%d\n", l.code, missing, badFormat);
      gaps += missing + badFormat;
    }
    printf("i18n gaps: %d\n", gaps);
  }

  // Redraw cost: one rasterisation happens per crosshair change (never per frame).
  {
    LARGE_INTEGER f, t0, t1;
    QueryPerformanceFrequency(&f);
    double worst1 = 0, worst4 = 0, total1 = 0;
    for (const auto& p : presets) {
      for (const float scale : {1.f, 4.f}) {
        Crosshair c = p;
        c.scale = scale;
        QueryPerformanceCounter(&t0);
        for (int i = 0; i < 20; ++i) (void)RasterizeCrosshair(c);
        QueryPerformanceCounter(&t1);
        const double ms = (t1.QuadPart - t0.QuadPart) * 1000.0 / f.QuadPart / 20;
        if (scale == 1.f) {
          worst1 = std::max(worst1, ms);
          total1 += ms;
        } else {
          worst4 = std::max(worst4, ms);
        }
      }
    }
    printf("rasterise: avg %.3f ms, worst %.3f ms at scale 1; worst %.2f ms at scale 4\n", total1 / presets.size(), worst1,
           worst4);
  }

  // Logo icon at tray / taskbar sizes, side by side, 6x zoom.
  {
    const int sizes[] = {16, 32, 64};
    const int z = 6, lw = (16 + 32 + 64 + 40) * z, lh = 64 * z;
    std::vector<uint32_t> logo(static_cast<size_t>(lw) * lh, 0xFFECE8E0);
    int ox = 0;
    for (const int s : sizes) {
      const Image img = RasterizeLogo(s);
      for (int y = 0; y < s; ++y) {
        for (int x = 0; x < s; ++x) {
          const uint32_t px = img.pixels[static_cast<size_t>(y) * s + x];
          const uint32_t a = px >> 24;
          for (int zy = 0; zy < z; ++zy) {
            for (int zx = 0; zx < z; ++zx) {
              uint32_t& d = logo[static_cast<size_t>(y * z + zy) * lw + (ox + x) * z + zx];
              uint32_t out = 0xFF000000;
              for (int c = 0; c < 24; c += 8) out |= (((px >> c) & 0xFF) + ((d >> c) & 0xFF) * (255 - a) / 255) << c;
              d = out;
            }
          }
        }
      }
      ox += s + 20;
    }
    std::wstring logoPath(outPath);
    logoPath.insert(logoPath.size() - 4, L"_logo");
    printf("logo sheet -> %s\n", WriteBmp(logoPath.c_str(), lw, lh, logo) ? "ok" : "FAILED");
  }

  for (int i = 2; i < argc; ++i) {
    Crosshair c;
    const std::string code = Narrow(argv[i]);
    const CodeSource src = ImportCrosshairCode(code, c);
    printf("\n[%s] %s\n", SourceName(src), code.c_str());
    for (const auto& L : c.layers) {
      printf("  %-8s len=%.1f thk=%.1f gap=%.1f size=%.1f rays=%d fill=%s op=%.2f outline=%d(%.0fpx, %.2f)\n", ShapeKey(L.shape),
             L.length, L.thickness, L.gap, L.size, L.rays, ColorToHex(L.fill).c_str(), L.opacity, L.outline ? 1 : 0,
             L.outlineThickness, L.outlineOpacity);
    }
  }
  return 0;
}
