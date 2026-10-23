// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 KosaWLeb. Created by KosaWLeb: https://github.com/KosaWLeb/MacWin

#pragma once
#include <math.h>

namespace mw {

// The window corner MacWin draws: a superellipse |x|^n + |y|^n = 1 over `extent` px from
// the corner, fitted to Figma's corner smoothing - extent (1 + smoothing) * radius,
// exponent that puts its 45 degree point on the plain arc. Smoothing 0 gives n = 2,
// Windows 11's circular corner; at 100 % and 15 px it stays within ~0.05 px of Figma's
// curve. Used by the genie (GeniePS) and by the window regions of the hook DLL.
inline void CornerShape(float radius, float smoothing, float w, float h, float* extent, float* exponent) {
    float half = (w < h ? w : h) * 0.5f;
    float r = radius < half ? radius : half;
    float p = (1.0f + smoothing) * r;
    if (p > half) p = half;
    if (r <= 0.0f || p <= 0.0f) {
        *extent = 0.0f;
        *exponent = 2.0f;
        return;
    }
    float diag = r * (1.0f - 0.70710678f) / p;   // 45 degree point, fraction of the extent
    *extent = p;
    *exponent = 0.69314718f / -logf(1.0f - diag);
}

}  // namespace mw
