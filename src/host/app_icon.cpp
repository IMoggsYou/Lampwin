// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

#include "host/app_icon.h"
#include <math.h>

namespace {

float Clamp01(float v) {
    return v < 0 ? 0 : (v > 1 ? 1 : v);
}

// Signed distance to a rounded rectangle centered at (cx, cy).
float RoundRectDist(float x, float y, float cx, float cy, float hw, float hh, float r) {
    float qx = fabsf(x - cx) - (hw - r);
    float qy = fabsf(y - cy) - (hh - r);
    float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
    float inside = qx > qy ? qx : qy;
    return sqrtf(ox * ox + oy * oy) + (inside < 0 ? inside : 0) - r;
}

}  // namespace

HICON CreateAppIcon(int size, bool enabled) {
    BITMAPV5HEADER bi = {};
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
    HDC dc = GetDC(nullptr);
    void* bits = nullptr;
    HBITMAP color = CreateDIBSection(dc, (BITMAPINFO*)&bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, dc);
    if (!color) return nullptr;

    // Tile: blue -> violet gradient (grey when disabled). Glyph: a window on top that
    // narrows into a funnel towards the bottom right, like the genie effect.
    DWORD* px = (DWORD*)bits;
    const float s = (float)size;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float fx = (x + 0.5f) / s, fy = (y + 0.5f) / s;
            float tile = Clamp01(0.5f - RoundRectDist(fx, fy, 0.5f, 0.5f, 0.47f, 0.47f, 0.22f) * s);

            float r = enabled ? 0.20f + 0.45f * fy : 0.45f;
            float g = enabled ? 0.45f - 0.20f * fy : 0.47f;
            float b = enabled ? 0.98f - 0.10f * fy : 0.50f;

            // Window part.
            float win = Clamp01(0.5f - RoundRectDist(fx, fy, 0.42f, 0.33f, 0.24f, 0.12f, 0.04f) * s);
            // Funnel: centre drifts right and width shrinks as it goes down.
            float glyph = win;
            if (fy > 0.40f && fy < 0.84f) {
                float k = (fy - 0.40f) / 0.44f;
                float cx = 0.42f + 0.22f * k * k;
                float hw = 0.24f * (1.0f - k) + 0.035f * k;
                float d = (fabsf(fx - cx) - hw) * s;
                float f = Clamp01(0.5f - d);
                if (f > glyph) glyph = f;
            }

            float a = tile;
            float cr = r + (1.0f - r) * glyph;
            float cg = g + (1.0f - g) * glyph;
            float cb = b + (1.0f - b) * glyph;
            BYTE A = (BYTE)(a * 255.0f + 0.5f);
            BYTE R = (BYTE)(cr * a * 255.0f + 0.5f);
            BYTE G = (BYTE)(cg * a * 255.0f + 0.5f);
            BYTE B = (BYTE)(cb * a * 255.0f + 0.5f);
            px[y * size + x] = ((DWORD)A << 24) | ((DWORD)R << 16) | ((DWORD)G << 8) | B;
        }
    }

    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO ii = {};
    ii.fIcon = TRUE;
    ii.hbmColor = color;
    ii.hbmMask = mask;
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}
