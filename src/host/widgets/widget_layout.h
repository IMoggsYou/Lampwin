// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/MacWin

#pragma once
#include <windows.h>
#include <vector>
#include "host/widgets/widget_config.h"

// The grid widgets snap to, like the macOS desktop: square cells along the top-right of
// a monitor's work area (Windows keeps its icons on the left). Pure math, no windows.
namespace widgets {

// DIPs at 100 %: a small widget is one cell, a medium one two cells wide, a large one
// two by two.
const int kCellDip = 164;
const int kGapDip = 16;
const int kMarginDip = 24;

struct Span {
    int cols;
    int rows;
};
Span SpanOf(Size s);

struct Slot {
    int col;
    int row;
    Span span;
};

class Grid {
public:
    // `unit`: physical px per DIP (the monitor's DPI scale times the widgets' scale).
    Grid(const RECT& work, double unit);

    int Cols() const { return cols_; }
    int Rows() const { return rows_; }
    int CellPx() const { return cell_; }
    // Size of a widget's card in px.
    SIZE CardSize(Span span) const;
    // Card rectangle of a slot (col 0 is the rightmost column).
    RECT Rect(const Slot& s) const;
    bool Inside(const Slot& s) const;
    // The free slot whose card is closest to a card whose top-left corner is at `at` (px);
    // false if none is free.
    bool Nearest(Span span, POINT at, const std::vector<Slot>& taken, Slot* out) const;
    // The first free slot, filling rows from the top, right to left.
    bool First(Span span, const std::vector<Slot>& taken, Slot* out) const;

private:
    bool Free(const Slot& s, const std::vector<Slot>& taken) const;

    RECT work_;
    int cell_, gap_, margin_;
    int cols_, rows_;
};

bool Overlaps(const Slot& a, const Slot& b);

}  // namespace widgets
