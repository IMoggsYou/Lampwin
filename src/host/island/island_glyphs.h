// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

#pragma once
#include <d2d1.h>

// The island's symbols: Lampwin's own, drawn on a 24 x 24 grid and filled even-odd (holes
// are cut out). Kept as path data so they stay sharp at any size and scale.
namespace island {

enum class Glyph {
    Play,
    Pause,
    Next,
    Previous,
    Phone,
    Mic,
    Slash,
    SlashGap,
    Camera,
    Open,
    Bell,
    Airplane,
    Antenna,
    Mirroring,
    Display,
    Headphones,
    Speaker,
    Bolt,
    Count
};

// `glyph` filling the square (x, y, size), in `color`, in the target's current transform.
void DrawGlyph(ID2D1RenderTarget* rt, Glyph glyph, float x, float y, float size, const D2D1_COLOR_F& color);
// The same with a slash across it (microphone off, silent): the slash is cut out of the
// glyph by a wider one in `back`, the colour behind it.
void DrawSlashed(ID2D1RenderTarget* rt, Glyph glyph, float x, float y, float size, const D2D1_COLOR_F& color,
                 const D2D1_COLOR_F& back);

}  // namespace island
