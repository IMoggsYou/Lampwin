// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

#include "host/widgets/widget_layout.h"
#include <math.h>
#include <limits.h>

namespace widgets {

Span SpanOf(Size s) {
    switch (s) {
        case Size::Medium: return {2 * kColSteps, kRowSteps};
        case Size::Large: return {2 * kColSteps, 2 * kRowSteps};
        default: return {kColSteps, kRowSteps};
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
      margin_((int)lround(kMarginDip * unit)),
      bottomMargin_((int)lround(kBottomMarginDip * unit)) {
    if (cell_ < 1) cell_ = 1;
    int pitch = cell_ + gap_;
    int w = work.right - work.left - 2 * margin_ + gap_;
    int h = work.bottom - work.top - margin_ - bottomMargin_ + gap_;
    cols_ = w > 0 ? w * kColSteps / pitch : 0;
    rows_ = h > 0 ? h * kRowSteps / pitch : 0;
}

SIZE Grid::CardSize(Span span) const {
    int cols = span.cols / kColSteps, rows = span.rows / kRowSteps;
    return {cols * cell_ + (cols - 1) * gap_, rows * cell_ + (rows - 1) * gap_};
}

RECT Grid::Rect(const Slot& s) const {
    SIZE size = CardSize(s.span);
    int pitch = cell_ + gap_;
    // A wider widget grows to the left of its column, which keeps its right edge on the grid.
    // Rounded per step, so two steps are always one cell and gap apart.
    int right = work_.right - margin_ - s.col * pitch / kColSteps;
    int top = work_.top + margin_ + s.row * pitch / kRowSteps;
    // The grid rarely ends exactly on the far margins: the last row moves down onto the
    // bottom one and the last column left onto the left one (nothing lies beyond them, and
    // moving that way keeps them clear of the others).
    if (s.row + s.span.rows >= rows_) top = work_.bottom - bottomMargin_ - size.cy;
    int left = right - size.cx;
    if (s.col + s.span.cols >= cols_) left = work_.left + margin_;
    return {left, top, left + size.cx, top + size.cy};
}

double FitUnit(const RECT& work, double unit) {
    const double large = 2 * kCellDip + kGapDip;
    // A few pixels to spare: the grid rounds every size to whole pixels.
    const double byWidth = (work.right - work.left - 4) / (2.0 * kMarginDip + large);
    const double byHeight = (work.bottom - work.top - 4) / ((double)kMarginDip + kBottomMarginDip + large);
    double fit = byWidth < byHeight ? byWidth : byHeight;
    // A sliver of a monitor gets no widgets rather than unreadable ones.
    if (fit < 0.3) fit = 0.3;
    return unit < fit ? unit : fit;
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
