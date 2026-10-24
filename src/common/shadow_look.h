// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/MacWin

#pragma once
#include <math.h>

namespace mw {

// A macOS-like window shadow: a broad soft layer a little below the window and a tight
// contact layer along its edge; plus a dark 1 px edge and a faint light hairline inside
// it (visible on dark windows). Drawn by the shade of a window cut to its corners (the
// hook DLL) and under the genie, which must look the same: the genie hands over to the
// real window, and a shadow that changed there would jump.
struct ShadowLook {
    float sigma1, dy1, op1;   // broad shadow
    float sigma2, dy2, op2;   // contact shadow
    float edge, highlight;
    int margin;               // px around the window the shadow reaches
};

// `scale` = DPI / 96.
inline ShadowLook WindowShadow(float scale) {
    ShadowLook l;
    l.sigma1 = 15.0f * scale;
    l.dy1 = 7.0f * scale;
    l.op1 = 0.32f;
    l.sigma2 = 1.8f * scale;
    l.dy2 = 1.0f * scale;
    l.op2 = 0.22f;
    l.edge = 0.30f;
    l.highlight = 0.10f;
    l.margin = (int)ceilf(3.0f * l.sigma1 + l.dy1) + 2;
    return l;
}

}  // namespace mw
