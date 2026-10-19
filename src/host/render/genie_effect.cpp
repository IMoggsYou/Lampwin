#include "host/render/genie_effect.h"
#include <shellscalingapi.h>
#include <math.h>

#define COMPAT_TASKBAR_HEIGHT 70
#define COMPAT_ICON_SIZE 33

namespace {

const float PI = 3.14159265f;
const float kClassicSpread = 0.65f;   // rows nearest the dock lead the morph

// Union of the monitors involved (the window's own and the one the genie funnels
// to), so a window on another monitor than the dock target is never clipped.
RECT GetGenieMonitorUnion(const RECT& winRect, HMONITOR hDockMon) {
    RECT winMon;
    HMONITOR hWinMon = MonitorFromRect(&winRect, MONITOR_DEFAULTTONEAREST);
    MONITORINFO wmi = {sizeof(wmi)};
    winMon = (hWinMon && GetMonitorInfoW(hWinMon, &wmi)) ? wmi.rcMonitor : winRect;

    RECT dockMon;
    MONITORINFO dmi = {sizeof(dmi)};
    dockMon = (hDockMon && GetMonitorInfoW(hDockMon, &dmi)) ? dmi.rcMonitor : winMon;

    RECT u;
    u.left = winMon.left < dockMon.left ? winMon.left : dockMon.left;
    u.top = winMon.top < dockMon.top ? winMon.top : dockMon.top;
    u.right = winMon.right > dockMon.right ? winMon.right : dockMon.right;
    u.bottom = winMon.bottom > dockMon.bottom ? winMon.bottom : dockMon.bottom;
    return u;
}

// Trim the canvas to the monitors in use, but never cut the window rect or the dock
// target column away.
void ClampGenieCanvas(int& left, int& top, int& right, int& bottom, const RECT& winRect, int dockX,
                      const RECT& monUnion) {
    if (left < monUnion.left) left = monUnion.left;
    if (right > monUnion.right) right = monUnion.right;
    if (top < monUnion.top) top = monUnion.top;
    if (bottom > monUnion.bottom) bottom = monUnion.bottom;

    if (left > winRect.left) left = winRect.left;
    if (right < winRect.right) right = winRect.right;
    if (top > winRect.top) top = winRect.top;
    if (bottom < winRect.bottom) bottom = winRect.bottom;
    if (left > dockX - 16) left = dockX - 16;   // icon rect is dockX +- 11
    if (right < dockX + 16) right = dockX + 16;
}

void SetCanvas(GenieParams* out, int left, int top, int right, int bottom) {
    out->canvasLeft = left;
    out->canvasTop = top;
    out->canvasWidth = right - left;
    // One px short of the full span, as in the mod (a ghost exactly covering a monitor
    // can be treated as a fullscreen window by the shell).
    out->canvasHeight = (bottom - top) - 1;
    if (out->canvasWidth < 1) out->canvasWidth = 1;
    if (out->canvasHeight < 1) out->canvasHeight = 1;
}

void LampVertexMacOS(float tx, float ty, float p, const Geometry& w, const Geometry& i, float* outX, float* outY) {
    float split = 0.3f;
    float k = (p <= split) ? (p / split) : 1.0f;
    float j = (p > split) ? ((p - split) / (1.0f - split)) : 0.0f;

    float expandHeight = (i.y - w.y - w.height);
    float fullHeight = (i.y - w.y) - (expandHeight * (1.0f - k));
    float height = fullHeight - (j * fullHeight);
    float safeFull = fabsf(fullHeight) < 0.001f ? 0.001f : fullHeight;

    float y = ty * height;
    float x = tx * (i.width) + tx * (w.width - i.width) * (1.0f - j) * (1.0f - ty) +
              tx * (w.width - i.width) * (1.0f - k) * ty;

    float offsetX = (i.x - w.x) * (y / (fullHeight + 0.1f)) * k + (i.x - w.x) * j;
    float offsetY = i.y - w.y - height - (expandHeight * (1.0f - k));

    float effectX =
        sinf(((height - y) / safeFull) * 2.0f * PI + PI) * (w.x + w.width * tx - (i.x + i.width * tx)) / 7.0f * k;

    *outX = w.x + x + offsetX + effectX;
    *outY = w.y + y + offsetY;
}

float ClassicMorph(float v, float tt) {
    float m = tt * (1.0f + kClassicSpread) - (1.0f - v) * kClassicSpread;
    if (m < 0.0f) m = 0.0f;
    if (m > 1.0f) m = 1.0f;
    return m * m * (3.0f - 2.0f * m);
}

// The classic warp: every source row slides towards the dock edge and narrows towards
// the neck, the rows nearest the dock first.
void ClassicVertex(float tx, float ty, float tt, const Geometry& w, const Geometry& d, float* outX, float* outY) {
    bool above = d.height > 0.5f;
    float e = ClassicMorph(above ? 1.0f - ty : ty, tt);
    float idY = w.y + w.height * ty;
    float y = idY + (d.y - idY) * e;
    float width = w.width + (d.width - w.width) * e;
    float cx0 = w.x + w.width * 0.5f;
    float cx = cx0 + (d.x - cx0) * e;
    *outX = cx + (tx - 0.5f) * width;
    *outY = y;
}

}  // namespace

HWND FindTaskbarForMonitor(HMONITOR hMon) {
    HWND hMainTray = FindWindowW(L"Shell_TrayWnd", nullptr);
    HMONITOR mainMon = MonitorFromWindow(hMainTray, MONITOR_DEFAULTTOPRIMARY);
    if (hMon == mainMon || !hMon) return hMainTray;
    HWND hSecTray = nullptr;
    while ((hSecTray = FindWindowExW(nullptr, hSecTray, L"Shell_SecondaryTrayWnd", nullptr)) != nullptr) {
        if (MonitorFromWindow(hSecTray, MONITOR_DEFAULTTONULL) == hMon) return hSecTray;
    }
    return hMainTray;
}

int DefaultDockX(HMONITOR hMon) {
    MONITORINFO mi = {sizeof(mi)};
    if (!GetMonitorInfoW(hMon, &mi)) {
        mi.rcMonitor = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    }
    DWORD alignVal = 1, dataSize = sizeof(alignVal);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
                 L"TaskbarAl", RRF_RT_REG_DWORD, nullptr, &alignVal, &dataSize);
    int monWidth = mi.rcMonitor.right - mi.rcMonitor.left;
    return alignVal == 0 ? mi.rcMonitor.left + 160 : mi.rcMonitor.left + monWidth / 2;
}

bool ComputeGenieParams(const RECT& r, HMONITOR hMon, const DockTarget& dock, int style, GenieParams* out) {
    int dockX = dock.x;
    const int w = r.right - r.left;
    const int h = r.bottom - r.top;
    if (w <= 0 || h <= 0) return false;
    Geometry wGeom = {(float)r.left, (float)r.top, (float)w, (float)h};

    MONITORINFO mi = {sizeof(mi)};
    if (!GetMonitorInfoW(hMon, &mi)) {
        mi.rcMonitor = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    }
    const RECT& mon = mi.rcMonitor;
    HWND hTray = FindTaskbarForMonitor(hMon);
    RECT tr = {};
    bool haveTray = hTray && GetWindowRect(hTray, &tr) && tr.bottom - tr.top > 0;
    RECT monUnion = GetGenieMonitorUnion(r, hMon);
    out->style = style;
    out->win = wGeom;

    if (style == kGenieClassic) {
        // Funnel to the taskbar edge of the monitor (top if the taskbar is in the upper
        // half), kept outside the window's own span so the row map stays monotonic.
        if (dockX < mon.left) dockX = mon.left;
        if (dockX > mon.right) dockX = mon.right;
        // Into the middle of the icon when its button is known; else the screen edge
        // on the taskbar's side, as in the mod.
        float dockY = (float)mon.bottom;
        if (haveTray && (tr.top + tr.bottom) < (mon.top + mon.bottom)) dockY = (float)mon.top;
        if (dock.hasRect) dockY = (float)dock.y;
        const float bottomF = (float)(r.top + h);
        const bool above = dockY < ((float)r.top + bottomF) * 0.5f;
        if (above) {
            if (dockY > (float)r.top) dockY = (float)r.top;
        } else if (dockY < bottomF) {
            dockY = bottomF;
        }
        float neckW = w * 0.03f;
        if (neckW < 12.0f) neckW = 12.0f;
        if (neckW > 60.0f) neckW = 60.0f;
        out->icon = {(float)dockX, dockY, neckW, above ? 1.0f : 0.0f};

        int left = (r.left < dockX ? r.left : dockX) - w / 2;
        int right = ((r.left + w) > dockX ? (r.left + w) : dockX) + w / 2;
        int top = r.top < (int)dockY ? r.top : (int)dockY;
        int bottom = (r.top + h) > (int)dockY ? (r.top + h) : (int)dockY;
        ClampGenieCanvas(left, top, right, bottom, r, dockX, monUnion);
        SetCanvas(out, left, top, right, bottom);
        return true;
    }

    UINT dpiX = 96, dpiY = 96;
    if (FAILED(GetDpiForMonitor(hMon, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) dpiY = 96;
    float dpiScale = dpiY / 96.0f;
    float taskbarTop = haveTray ? (float)tr.top : (float)mon.bottom - COMPAT_TASKBAR_HEIGHT * dpiScale;
    // The lamp curve pours the window into the line y = icon.y, icon.width wide: with the
    // button known, that is the middle of its icon (the genie visibly goes into the icon,
    // over the taskbar); otherwise the taskbar's top edge, as in the mod.
    Geometry iGeom = dock.hasRect
        ? Geometry{(float)dock.x - dock.size * 0.5f, (float)dock.y, (float)dock.size, dock.size * 0.5f}
        : Geometry{(float)dockX - 11.0f, taskbarTop, 22.0f, COMPAT_ICON_SIZE * dpiScale};
    out->icon = iGeom;

    // Tight canvas between the window rect and the dock target. A ~W/2 pad alone can't
    // bound the curve's sway (its amplitude scales with the window-to-dock distance),
    // so the horizontal pad is the exact sway bound max(|w.x-i.x|, |w.right-i.right|)/7.
    const float sway = fmaxf(fabsf((float)r.left - iGeom.x), fabsf((float)r.left + w - iGeom.x - iGeom.width)) / 7.0f;
    int padX = (int)(((float)(w / 2) > sway ? (float)(w / 2) : sway) + 1);
    int left = (r.left < dockX ? r.left : dockX) - padX;
    int right = ((r.left + w) > dockX ? (r.left + w) : dockX) + padX;
    int top = (int)fminf(wGeom.y, iGeom.y);
    int bottom = (int)fmaxf(wGeom.y + wGeom.height, iGeom.y + iGeom.height);
    ClampGenieCanvas(left, top, right, bottom, r, dockX, monUnion);
    SetCanvas(out, left, top, right, bottom);
    return true;
}

float GenieEase(const GenieParams& g, float raw) {
    if (raw < 0.0f) raw = 0.0f;
    if (raw > 1.0f) raw = 1.0f;
    if (g.style == kGenieClassic) return raw;
    return raw * raw * (3.0f - 2.0f * raw);
}

float GenieFade(const GenieParams& g, float t) {
    if (g.style != kGenieClassic || t <= 0.8f) return 1.0f;
    float f = (1.0f - t) / 0.2f;
    return f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
}

void GenieVertex(const GenieParams& g, float tx, float ty, float t, float* outX, float* outY) {
    if (g.style == kGenieClassic) {
        ClassicVertex(tx, ty, t, g.win, g.icon, outX, outY);
    } else {
        LampVertexMacOS(tx, ty, t, g.win, g.icon, outX, outY);
    }
}

bool GenieFrameDegenerate(const GenieParams& g, float t) {
    if (GenieFade(g, t) <= 0.0f) return true;
    // The mesh outline is its border, so sampling the four edges bounds it.
    const int n = 8;
    float left = 1e9f, right = -1e9f, top = 1e9f, bottom = -1e9f;
    for (int e = 0; e < 4; ++e) {
        for (int s = 0; s <= n; ++s) {
            float u = (float)s / n;
            float tx = (e == 0 || e == 1) ? u : (e == 2) ? 0.0f : 1.0f;
            float ty = (e == 0) ? 0.0f : (e == 1) ? 1.0f : u;
            float px, py;
            GenieVertex(g, tx, ty, t, &px, &py);
            left = fminf(left, px);
            right = fmaxf(right, px);
            top = fminf(top, py);
            bottom = fmaxf(bottom, py);
        }
    }
    return (right - left) < 2.0f || (bottom - top) < 2.0f;
}
