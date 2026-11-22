#pragma once

#include "common/winutil.h"

#include <d2d1.h>

namespace sl {

// Sightline mark: monogram S built from two half-turns, with an accent dot at its centre.
// Uses only ID2D1RenderTarget API so it works on any target.
void DrawLogo(ID2D1RenderTarget* rt, const D2D1_RECT_F& box, D2D1_COLOR_F ink, D2D1_COLOR_F accent);

// Renders the icon variant (light S on an accent tile) into a 32-bit icon. Caller owns the HICON.
HICON CreateLogoIcon(int sizePx);

}  // namespace sl
