#pragma once
#include <windows.h>

// Geometry of one genie animation, all in physical screen pixels (the host is
// per-monitor DPI aware). Ported from the mod's two engines: the modern lamp curve
// (Potassiumuncher) and the original strip-based "classic" warp.

enum GenieStyle : int {
    kGenieModern = 0,    // Potassiumuncher's lamp curve
    kGenieClassic = 1,   // the mod's original (v2.2.0) funnel
};

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
    int style;
    Geometry win;    // window (DWM extended frame) rect
    // Modern: the taskbar icon rect the genie funnels into.
    // Classic: x = dock x, y = dock y, width = neck width, height = 1 if the dock is
    // above the window (taskbar at the top), else 0.
    Geometry icon;
    int canvasLeft, canvasTop, canvasWidth, canvasHeight;   // ghost window rect
};

HWND FindTaskbarForMonitor(HMONITOR hMon);

// Where the dock target sits before (or without) a taskbar-button match: left-aligned
// taskbars start near the monitor's left, centered ones at its middle.
int DefaultDockX(HMONITOR hMon);

bool ComputeGenieParams(const RECT& winRect, HMONITOR hDockMon, const DockTarget& dock, int style, GenieParams* out);

// Time -> progress. Modern eases (smoothstep), classic is linear (its funnel eases
// per row). Both are symmetric, f(1 - x) = 1 - f(x), which is what lets a running
// genie be turned around at any point without a jump: see Renderer::Retarget.
float GenieEase(const GenieParams& g, float raw);

// Global opacity: the classic style fades out over the last 20%.
float GenieFade(const GenieParams& g, float t);

// CPU copy of the shader's vertex function (tx, ty in 0..1, progress t).
void GenieVertex(const GenieParams& g, float tx, float ty, float t, float* outX, float* outY);

// True when the warped mesh has collapsed to (almost) a line: such frames are not
// presented, otherwise they flash as a single horizontal line.
bool GenieFrameDegenerate(const GenieParams& g, float t);
