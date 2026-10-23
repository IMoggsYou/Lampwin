// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/MacWin

#include "host/widgets/widget_layout.h"
#include <math.h>
#include <limits.h>

namespace widgets {

Span SpanOf(Size s) {
    switch (s) {
        case Size::Medium: return {2, 1};
        case Size::Large: return {2, 2};
        default: return {1, 1};
    }
}

bool Overlaps(const Slot& a, const Slot& b) {
    return a.col < b.col + b.span.cols && b.col < a.col + a.span.cols && a.row < b.row + b.span.rows &&
           b.row < a.row + a.span.rows;
}

// Whole pixels, so every card edge is crisp and every gap the same.
Grid::Grid(const RECT& work, double unit)
    : work_(work),
      cell_((int)lround(kCellDip * unit)),
      gap_((int)lround(kGapDip * unit)),
      margin_((int)lround(kMarginDip * unit)) {
    if (cell_ < 1) cell_ = 1;
    int pitch = cell_ + gap_;
    int w = work.right - work.left - 2 * margin_ + gap_;
    int h = work.bottom - work.top - 2 * margin_ + gap_;
    cols_ = w > 0 ? w / pitch : 0;
    rows_ = h > 0 ? h / pitch : 0;
}

SIZE Grid::CardSize(Span span) const {
    return {span.cols * cell_ + (span.cols - 1) * gap_, span.rows * cell_ + (span.rows - 1) * gap_};
}

RECT Grid::Rect(const Slot& s) const {
    SIZE size = CardSize(s.span);
    int pitch = cell_ + gap_;
    // A wider widget grows to the left of its column, which keeps its right edge on the grid.
    int right = work_.right - margin_ - s.col * pitch;
    int top = work_.top + margin_ + s.row * pitch;
    return {right - size.cx, top, right, top + size.cy};
}

bool Grid::Inside(const Slot& s) const {
    return s.col >= 0 && s.row >= 0 && s.col + s.span.cols <= cols_ && s.row + s.span.rows <= rows_;
}

bool Grid::Free(const Slot& s, const std::vector<Slot>& taken) const {
    if (!Inside(s)) return false;
    for (const Slot& t : taken) {
        if (Overlaps(s, t)) return false;
    }
    return true;
}

bool Grid::Nearest(Span span, POINT at, const std::vector<Slot>& taken, Slot* out) const {
    long long best = LLONG_MAX;
    for (int row = 0; row + span.rows <= rows_; ++row) {
        for (int col = 0; col + span.cols <= cols_; ++col) {
            Slot s{col, row, span};
            if (!Free(s, taken)) continue;
            RECT r = Rect(s);
            long long dx = r.left - at.x, dy = r.top - at.y;
            long long d = dx * dx + dy * dy;
            if (d < best) {
                best = d;
                *out = s;
            }
        }
    }
    return best != LLONG_MAX;
}

bool Grid::First(Span span, const std::vector<Slot>& taken, Slot* out) const {
    for (int row = 0; row + span.rows <= rows_; ++row) {
        for (int col = 0; col + span.cols <= cols_; ++col) {
            Slot s{col, row, span};
            if (Free(s, taken)) {
                *out = s;
                return true;
            }
        }
    }
    return false;
}

}  // namespace widgets
