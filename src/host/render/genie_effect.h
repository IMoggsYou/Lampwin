// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin
// Portions ported from the Windhawk mod "MacOS Minimize Animation" (MIT, Abdullah Masood
// and Potassiumuncher); see THIRD_PARTY_NOTICES.md.

#pragma once
#include <windows.h>

// Geometry of one genie animation, all in physical screen pixels (the host is
// per-monitor DPI aware). Ported from the mod's two engines: the modern lamp curve
// (Potassiumuncher) and the original strip-based "classic" warp; plus MacWin's own
// scale effect.

enum GenieStyle : int {
    kGenieModern = 0,    // Potassiumuncher's lamp curve
    kGenieClassic = 1,   // the mod's original (v2.2.0) funnel
    kGenieScale = 2,     // macOS's other effect: shrinks into the icon and fades
    kGenieShatter = 3,   // opening / closing: the window breaks into shards that fly apart
    kGenieFragments = 4, // the same with a few big pieces, each flying its own way
    kGenieZoom = 5,      // opening / closing: grows out of / shrinks into a point (the click)
};

inline bool IsShatterStyle(int style) { return style == kGenieShatter || style == kGenieFragments; }
// Effects about a point of the window's own (impactU/V) rather than its taskbar button:
// no button lookup, and never turned around.
inline bool IsPointStyle(int style) { return IsShatterStyle(style) || style == kGenieZoom; }

// The crack grid for a w x h window: `cells` cells along its longer side, never fewer
// than 3 along the other.
void ShatterGrid(int w, int h, int cells, int* cols, int* rows);
const int kShatterCells = 9;     // ~100 shards
const int kFragmentCells = 5;    // ~30 big pieces
// Pieces travel much farther than a genie: they take this much longer than the open or
// close duration set, so one setting feels right for every effect.
const float kPiecesPace = 1.35f;
// Zoom: how small the window starts / ends (of its size), and how far an opening window
// grows past its size before it settles (a gentle spring).
const float kZoomEndScale = 0.08f;
const float kZoomOvershoot = 0.04f;

struct Geometry {
    float x, y, width, height;
};

// Where the genie goes: the center of the window's taskbar button (and the size of the
// icon in it) when the button is known, else just an x on the taskbar.
struct DockTarget {
    int x = 0;
    int y = 0;      // valid with hasRect
    int size = 0;   // icon size, valid with hasRect
    bool hasRect = false;
};

struct GenieParams {
    bool closing;    // MW_ANIM_CLOSE: the window is gone afterwards (nothing to cache)
    // DPI / 96 of the window's shadow (mw::WindowShadow), 0 for a window without one
    // (maximized). The canvas leaves room for it.
    float shadowScale;
    int style;
    Geometry win;    // window (DWM extended frame) rect
    // Modern: the taskbar icon rect the genie funnels into.
    // Classic: x = dock x, y = dock y, width = neck width, height = 1 if the dock is
    // above the window (taskbar at the top), else 0.
    // Scale: the rect the window ends in, its own shape fitted into the icon.
    // Shatter / fragments: x = columns, y = rows of the crack grid, width = seed,
    // height = 1 for fragments.
    Geometry icon;
    int canvasLeft, canvasTop, canvasWidth, canvasHeight;   // ghost window rect
    // Shatter / fragments: where the glass breaks first, in window uv (0..1) - the pointer
    // when it is on the window (the close button you clicked), else the middle.
    // Zoom: the point it grows out of / shrinks into (may lie outside the window).
    float impactU = 0.5f, impactV = 0.5f;
};

HWND FindTaskbarForMonitor(HMONITOR hMon);

// Where the dock target sits before (or without) a taskbar-button match: left-aligned
// taskbars start near the monitor's left, centered ones at its middle.
int DefaultDockX(HMONITOR hMon);

bool ComputeGenieParams(const RECT& winRect, HMONITOR hDockMon, const DockTarget& dock, int style, GenieParams* out);

// Time -> progress. Modern and scale ease in and out (smootherstep: no jolt where the
// motion starts or stops), classic is linear (its funnel eases per row). These three are
// symmetric, f(1 - x) = 1 - f(x), which is what lets a running genie be turned around at
// any point without a jump: see Renderer::Retarget. Shatter and fragments are never
// turned around.
float GenieEase(const GenieParams& g, float raw);

// Global opacity at progress t: every style melts into the icon instead of vanishing
// at once - modern over its last 8 %, classic over its last 20 %, scale over its last
// 40 %.
float GenieFade(const GenieParams& g, float t);

// CPU copy of the shader's vertex function (tx, ty in 0..1, progress t).
void GenieVertex(const GenieParams& g, float tx, float ty, float t, float* outX, float* outY);

// True when the warped mesh has collapsed to (almost) a line: such frames are not
// presented, otherwise they flash as a single horizontal line.
bool GenieFrameDegenerate(const GenieParams& g, float t);
