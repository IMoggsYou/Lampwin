// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

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
// Above the taskbar: the lowest widgets sit right next to it.
const int kBottomMarginDip = 12;
// Rows are half a cell high, so a widget also fits in the space a whole row would leave
// over the taskbar; the lowest row lies on the bottom margin (see Grid::Rect).
const int kRowSteps = 2;

struct Span {
    int cols;
    int rows;   // in rows of the grid: kRowSteps per cell
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
    // Card rectangle of a slot (col 0 is the rightmost column). A slot that reaches the
    // last row sits on the bottom margin.
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
    int cell_, gap_, margin_, bottomMargin_;
    int cols_, rows_;
};

bool Overlaps(const Slot& a, const Slot& b);

}  // namespace widgets
