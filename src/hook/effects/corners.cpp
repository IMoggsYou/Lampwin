// Window corners: cuts the windows of this process to the corner MacWin draws in the
// genie (macOS squircle or a plain round corner, radius from the settings) with a
// window region. Windows 11 itself can only round by 8 px; anything else needs a
// region, and a region switches DWM's frame off for that window: no shadow, no 1 px
// border, no DWM title bar or caption buttons (Windows falls back to the old "basic"
// frame), and no anti-aliasing. So only windows that draw their whole frame themselves
// are cut: Chromium / Electron windows (Chrome, Brave, Edge, Discord, VS Code, ...)
// whose client area is the entire visible window. Windows with a system title bar keep
// Windows 11's corners. Maximized and full-screen windows stay square, and Windows 11's
// own 8 px circular corner is left to DWM.
//
// The region is the client area, which a region does not change (DWM's extended frame
// bounds collapse to the window rect once a region is set). It follows the window from
// DefWindowProc's WM_WINDOWPOSCHANGED, in the window's own thread, so it is in place
// before the resized window is composed.
//
// What DWM no longer draws, the shade does: a soft two-layer shadow and an anti-aliased
// edge that hides the region's pixel steps (see PlaceShade). And what the region cut
// off - the invisible resize border around the window - the grip gives back.

#include <windows.h>
#include <dwmapi.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <algorithm>
#include <unordered_map>
#include <vector>
#include <MinHook.h>
#include "common/corner_shape.h"
#include "common/names.h"
#include "common/strutil.h"
#include "hook/runtime.h"

UINT g_cornersMsg = 0xFFFFFFFFu;   // no message has this number until CornersOnActivate

namespace {

enum : WPARAM { kRefresh = 0, kRemove = 1, kFit = 2 };   // kFit: see FitArranged

// What MacWin did to a window, so it is only redone on a change and undone exactly.
struct Cut {
    RECT frame;      // client area, window coordinates
    int radius;      // px, window coordinates
    int smoothing;   // percent
    bool region;     // our window region is set
    bool square;     // we set DWMWCP_DONOTROUND (radius 0)
    HWND shade[4] = {};            // shadow + edge in strips: top, bottom, left, right
    int shadeW = 0, shadeH = 0;    // whole shade size the strips were drawn for
    bool shadeSingle = false;      // drawn as one piece (a window too small for strips)
    HWND grip = nullptr;           // invisible resize border around the window
    int gripW = 0, gripH = 0;      // frame size its region was made for
    RECT arranged = {};            // window rect FitArranged gave a snapped window
    bool arrangedSet = false;
    // The program's own values of the DWM attributes overridden while cut (kOverrides).
    bool overridden = false;
    int saved[3] = {};
};

SRWLOCK g_lock = SRWLOCK_INIT;
std::unordered_map<HWND, Cut> g_cuts;
thread_local int t_cutting = 0;   // our own SetWindowRgn re-enters through WM_WINDOWPOSCHANGED

bool FindCut(HWND h, Cut* out) {
    AcquireSRWLockShared(&g_lock);
    auto it = g_cuts.find(h);
    bool found = it != g_cuts.end();
    if (found && out) *out = it->second;
    ReleaseSRWLockShared(&g_lock);
    return found;
}

// The "Excluded programs" list, matched against this process once per publish.
bool ExcludedHere(const SharedSettings* s) {
    static SRWLOCK lock = SRWLOCK_INIT;
    static uint32_t gen = 0xFFFFFFFFu;
    static bool excluded = false;
    static wchar_t list[MACWIN_LIST_CHARS];
    AcquireSRWLockExclusive(&lock);
    if (gen != s->generation) {
        gen = s->generation;
        excluded = false;
        wchar_t path[MAX_PATH];
        DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
        mw::ImageNames self;
        if (len && len < MAX_PATH && mw::MakeImageNames(path, &self)) {
            SettingsCopyList(s, s->genieExcluded, list, MACWIN_LIST_CHARS);
            excluded = mw::ListMatches(list, self);
        }
    }
    bool r = excluded;
    ReleaseSRWLockExclusive(&lock);
    return r;
}

void SetCornerPreference(HWND h, DWM_WINDOW_CORNER_PREFERENCE pref) {
    DwmSetWindowAttribute(h, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
}

typedef HRESULT(WINAPI* DwmSetWindowAttribute_t)(HWND, DWORD, LPCVOID, DWORD);
DwmSetWindowAttribute_t DwmSetWindowAttribute_Original;

// DWM attributes a cut window must not have. Once a window has a region, DWM's frame
// bounds collapse to the whole window rect, invisible resize border included, and DWM
// applies these to that rect instead of the cut shape: Mica / acrylic shows as a gray
// square around the window, Windows 11's rounded corner and border (which Electron asks
// for) as an outline a few px outside it. While cut they are off; what the program set,
// before or meanwhile (DwmSetWindowAttribute_Hook), is given back afterwards.
struct Override {
    DWORD attr;
    int value;      // ours
    int fallback;   // the program's, if it cannot be read
};
const Override kOverrides[3] = {
    {DWMWA_SYSTEMBACKDROP_TYPE, DWMSBT_NONE, DWMSBT_AUTO},
    {DWMWA_BORDER_COLOR, (int)DWMWA_COLOR_NONE, (int)DWMWA_COLOR_DEFAULT},
    {DWMWA_WINDOW_CORNER_PREFERENCE, DWMWCP_DONOTROUND, DWMWCP_DEFAULT},
};

int OverrideIndex(DWORD attr) {
    for (int i = 0; i < 3; ++i) {
        if (kOverrides[i].attr == attr) return i;
    }
    return -1;
}

// Bypasses our own hook (see DwmSetWindowAttribute_Hook).
void SetDwmInt(HWND h, DWORD attr, int value) {
    auto set = DwmSetWindowAttribute_Original ? DwmSetWindowAttribute_Original : DwmSetWindowAttribute;
    set(h, attr, &value, sizeof(value));
}

void TakeOverrides(HWND h, int* saved) {
    for (int i = 0; i < 3; ++i) {
        int v = 0;
        saved[i] = SUCCEEDED(DwmGetWindowAttribute(h, kOverrides[i].attr, &v, sizeof(v))) ? v : kOverrides[i].fallback;
        SetDwmInt(h, kOverrides[i].attr, kOverrides[i].value);
    }
}

void GiveBackOverrides(HWND h, const int* saved) {
    for (int i = 0; i < 3; ++i) SetDwmInt(h, kOverrides[i].attr, saved[i]);
}

// Chromium / Electron (Chrome, Brave, Edge, Discord, VS Code, Spotify, ...) draws its
// title bar and caption buttons itself. With the system frame (Electron's default) the
// client area starts under a system title bar - that one is not ours to cut. Read while
// the window has no region, so DWM's extended frame bounds still mean the visible frame.
bool DrawsOwnFrame(HWND h) {
    wchar_t cls[32];
    if (!GetClassNameW(h, cls, 32) || wcsncmp(cls, L"Chrome_WidgetWin_", 17) != 0) return false;
    DPI_AWARENESS_CONTEXT old = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    RECT frame, client;
    POINT origin = {0, 0};
    bool ok = SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame))) &&
              GetClientRect(h, &client) && ClientToScreen(h, &origin);
    if (old) SetThreadDpiAwarenessContext(old);
    if (!ok) return false;
    // The client area may leave out DWM's 1 px border.
    return labs(origin.y - frame.top) <= 1 && labs(origin.x - frame.left) <= 2 &&
           labs(origin.x + client.right - frame.right) <= 2 && labs(origin.y + client.bottom - frame.bottom) <= 2;
}

// A window MacWin has not cut yet: top-level with a title bar or sizing frame (no
// menus, tooltips, splash screens), not shaped by the program itself - no per-pixel
// alpha, no region of its own, no square corners asked of DWM - and drawing its whole
// frame itself.
bool Eligible(HWND h, LONG_PTR style) {
    if ((style & WS_CAPTION) != WS_CAPTION && !(style & WS_THICKFRAME)) return false;
    // Layered windows usually paint their own shape with per-pixel alpha - not ours to
    // cut. Chromium's transparent windows (Claude) are layered too but drawn through
    // DirectComposition (no redirection bitmap); their shape is the client area.
    const LONG_PTR ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
    if ((ex & WS_EX_LAYERED) && !(ex & WS_EX_NOREDIRECTIONBITMAP)) return false;
    RECT box;
    if (GetWindowRgnBox(h, &box) != ERROR) return false;
    DWORD pref = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref))) &&
        pref == DWMWCP_DONOTROUND) {
        return false;
    }
    return DrawsOwnFrame(h);
}

// The client area in window coordinates (the region's coordinates), read in the
// window's own DPI mode; full screen is judged in physical pixels.
bool Measure(HWND h, RECT* frame, bool* fullScreen) {
    DPI_AWARENESS_CONTEXT old = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    RECT pr, wr, client;
    POINT origin = {0, 0};
    bool ok = GetWindowRect(h, &pr) != FALSE;
    MONITORINFO mi = {sizeof(mi)};
    *fullScreen = ok && GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi) &&
                  pr.left <= mi.rcMonitor.left && pr.top <= mi.rcMonitor.top &&
                  pr.right >= mi.rcMonitor.right && pr.bottom >= mi.rcMonitor.bottom;
    SetThreadDpiAwarenessContext(GetWindowDpiAwarenessContext(h));
    ok = ok && GetWindowRect(h, &wr) && GetClientRect(h, &client) && ClientToScreen(h, &origin);
    if (old) SetThreadDpiAwarenessContext(old);
    if (!ok) return false;
    frame->left = origin.x - wr.left;
    frame->top = origin.y - wr.top;
    frame->right = frame->left + client.right;
    frame->bottom = frame->top + client.bottom;
    return client.right >= 40 && client.bottom >= 40;
}

// The corner as whole pixels: a pixel belongs to the window when its centre is inside
// the curve (mw::CornerShape). Rows with the same inset are merged into one rectangle.
HRGN MakeRegion(const RECT& frame, int radius, int smoothing) {
    int w = frame.right - frame.left, h = frame.bottom - frame.top;
    float extent, n;
    mw::CornerShape((float)radius, (float)smoothing / 100.0f, (float)w, (float)h, &extent, &n);
    int rows = (int)ceilf(extent);
    auto inset = [&](int y) {
        int i = y < h - 1 - y ? y : h - 1 - y;   // row index from the nearer edge
        if (i >= rows) return 0;
        float v = (extent - ((float)i + 0.5f)) / extent;
        if (v <= 0.0f) return 0;
        float u = powf(1.0f - powf(v, n), 1.0f / n);
        int d = (int)ceilf(extent * (1.0f - u) - 0.5f);
        return d < 0 ? 0 : d;
    };

    std::vector<RECT> rects;
    rects.reserve(2 * (size_t)rows + 1);
    for (int y = 0; y < h;) {
        int d = inset(y), next = y + 1;
        while (next < h && inset(next) == d) ++next;
        if (2 * d < w) rects.push_back({frame.left + d, frame.top + y, frame.right - d, frame.top + next});
        y = next;
    }
    std::vector<char> buf(sizeof(RGNDATAHEADER) + rects.size() * sizeof(RECT));
    auto* data = (RGNDATA*)buf.data();
    data->rdh.dwSize = sizeof(RGNDATAHEADER);
    data->rdh.iType = RDH_RECTANGLES;
    data->rdh.nCount = (DWORD)rects.size();
    data->rdh.nRgnSize = 0;
    data->rdh.rcBound = frame;
    memcpy(data->Buffer, rects.data(), rects.size() * sizeof(RECT));
    return ExtCreateRegion(nullptr, (DWORD)buf.size(), data);
}

// ---------------------------------------------------------------------------
// The shade: a layered, click-through, disabled popup OWNED by the cut window, so
// Windows keeps it right above that window in the z-order and hides it along with it
// when the window is minimized. It paints only outside the window's shape plus the
// shape's outermost pixels, so lying above the window it looks exactly like a shadow
// below it. Its window procedure is DefWindowProcW and its class belongs to the
// program, not to this DLL: a shade left behind after an unload holds no code of ours,
// and WM_CLOSE (posted from any thread) destroys it in its own thread.
// ---------------------------------------------------------------------------

const wchar_t kShadeClass[] = L"MacWinCornerShade";

// A macOS-like window shadow: a broad soft layer a little below the window and a tight
// contact layer along its edge; plus a dark 1 px edge and a faint light hairline inside
// it (visible on dark windows), both anti-aliased.
struct ShadeLook {
    float sigma1, dy1, op1;   // broad shadow
    float sigma2, dy2, op2;   // contact shadow
    float edge, highlight;
    int margin;               // px around the window the shade reaches
};

ShadeLook Look(float scale) {
    ShadeLook l;
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

float Clamp01(float v) {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

// Signed distance (px, < 0 inside) to the window shape centred at 0 with half size
// (hw, hh): the superellipse corner of mw::CornerShape, to first order like GeniePS.
float ShapeDistance(float px, float py, float hw, float hh, float extent, float n) {
    float qx = fabsf(px) - hw, qy = fabsf(py) - hh;
    if (extent > 0.0f) {
        float kx = qx + extent, ky = qy + extent;
        if (kx > 0.0f || ky > 0.0f) {
            float ax = kx > 0.0f ? kx / extent : 0.0f, ay = ky > 0.0f ? ky / extent : 0.0f;
            float tx = powf(ax, n), ty = powf(ay, n);
            float m = powf(tx + ty, 1.0f / n);
            float mn = powf(m, n - 1.0f);
            float gx = ax > 0.0f ? tx / ax / fmaxf(mn, 1e-6f) : 0.0f;
            float gy = ay > 0.0f ? ty / ay / fmaxf(mn, 1e-6f) : 0.0f;
            return (m - 1.0f) * extent / fmaxf(sqrtf(gx * gx + gy * gy), 1e-3f);
        }
    }
    return fmaxf(qx, qy);
}

// Exact distance to a plain rounded rectangle: the shadow's shape (blurred, the
// squircle's difference does not show) - also correct far away from the window.
float RoundRectDistance(float px, float py, float hw, float hh, float r) {
    float qx = fabsf(px) - hw + r, qy = fabsf(py) - hh + r;
    float ox = fmaxf(qx, 0.0f), oy = fmaxf(qy, 0.0f);
    return sqrtf(ox * ox + oy * oy) + fminf(fmaxf(qx, qy), 0.0f) - r;
}

// The window shape a shade pixel belongs to: half sizes, corner (see mw::CornerShape).
struct ShadeShape {
    float hw, hh, extent, n, r;
};

ShadeShape MakeShadeShape(float hw, float hh, int radius, int smoothing) {
    ShadeShape sh;
    sh.hw = hw;
    sh.hh = hh;
    mw::CornerShape((float)radius, (float)smoothing / 100.0f, 2.0f * hw, 2.0f * hh, &sh.extent, &sh.n);
    sh.r = fminf((float)radius, fminf(hw, hh));
    return sh;
}

// One premultiplied BGRA shade pixel at (cx, cy) from the window's centre.
uint32_t ShadePixel(float cx, float cy, const ShadeShape& sh, const ShadeLook& l) {
    const float d = ShapeDistance(cx, cy, sh.hw, sh.hh, sh.extent, sh.n);
    if (d < -2.5f) return 0;   // inside the window: nothing
    const float cover = Clamp01(0.5f - d);
    const float edge = (cover - Clamp01(-0.5f - d)) * l.edge;                      // outermost px
    const float light = (Clamp01(-0.5f - d) - Clamp01(-1.5f - d)) * l.highlight;   // the next
    const float k1 = 1.0f / (l.sigma1 * 1.41421356f), k2 = 1.0f / (l.sigma2 * 1.41421356f);
    const float s1 = l.op1 * 0.5f * erfcf(RoundRectDistance(cx, cy - l.dy1, sh.hw, sh.hh, sh.r) * k1);
    const float s2 = l.op2 * 0.5f * erfcf(RoundRectDistance(cx, cy - l.dy2, sh.hw, sh.hh, sh.r) * k2);
    const float shadow = (1.0f - (1.0f - s1) * (1.0f - s2)) * (1.0f - cover);
    // Black shadow and edge, the white hairline over them.
    const float a = light + (edge + shadow * (1.0f - edge)) * (1.0f - light);
    const uint32_t A = (uint32_t)(Clamp01(a) * 255.0f + 0.5f);
    const uint32_t C = (uint32_t)(light * 255.0f + 0.5f);
    return (A << 24) | (C << 16) | (C << 8) | C;
}

// The parts of a shade that do not depend on the window's size, made once for a window
// far larger than the shade's reach: the top-left and bottom-left corner blocks (the
// shadow sits a little low, so top and bottom differ; the right side is the left one
// mirrored) and the profiles across the top, bottom and left edges. A resize then only
// copies them into place instead of evaluating the shadow per pixel.
struct ShadeTiles {
    int radius = -1, smoothing = -1;
    UINT dpi = 0;
    int c = 0;                                     // corner block size (px)
    std::vector<uint32_t> topLeft, bottomLeft;     // c x c
    std::vector<uint32_t> top, bottom, left;       // c each
};

SRWLOCK g_tilesLock = SRWLOCK_INIT;
ShadeTiles g_tiles;   // the last look used (one radius and scale at a time, as a rule)

void MakeTiles(int radius, int smoothing, UINT dpi) {
    ShadeTiles& t = g_tiles;
    if (t.radius == radius && t.smoothing == smoothing && t.dpi == dpi) return;
    const ShadeLook l = Look((float)dpi / 96.0f);
    const float big = 4096.0f;   // half size of the stand-in window
    const ShadeShape sh = MakeShadeShape(big, big, radius, smoothing);
    const int m = l.margin;
    // Wide enough that the corner's influence on the blurred shadow has died out.
    const int c = m + (int)ceilf(sh.extent + 3.0f * l.sigma1) + 3;
    t.c = c;
    t.topLeft.assign((size_t)c * c, 0);
    t.bottomLeft.assign((size_t)c * c, 0);
    t.top.assign(c, 0);
    t.bottom.assign(c, 0);
    t.left.assign(c, 0);
    for (int y = 0; y < c; ++y) {
        const float ty = (float)y + 0.5f - (float)m - big;        // shade rows from the top
        const float by = big + (float)m - (float)c + (float)y + 0.5f;   // the last c rows
        for (int x = 0; x < c; ++x) {
            const float lx = (float)x + 0.5f - (float)m - big;
            t.topLeft[(size_t)y * c + x] = ShadePixel(lx, ty, sh, l);
            t.bottomLeft[(size_t)y * c + x] = ShadePixel(lx, by, sh, l);
        }
        t.top[y] = ShadePixel(0.0f, ty, sh, l);
        t.bottom[y] = ShadePixel(0.0f, by, sh, l);
        t.left[y] = ShadePixel((float)y + 0.5f - (float)m - big, 0.0f, sh, l);
    }
    t.radius = radius;
    t.smoothing = smoothing;
    t.dpi = dpi;
}

// One pixel of the whole W x H shade: from the tiles when the window is big enough for
// them (`tiled`, g_tilesLock held), else computed.
uint32_t ShadeAt(int x, int y, int W, int H, bool tiled, const ShadeShape& sh, const ShadeLook& l) {
    if (!tiled) {
        return ShadePixel((float)x + 0.5f - (float)l.margin - sh.hw, (float)y + 0.5f - (float)l.margin - sh.hh, sh, l);
    }
    const ShadeTiles& t = g_tiles;
    const int c = t.c;
    const int lx = x < W - 1 - x ? x : W - 1 - x;   // the right half is the left one mirrored
    if (y < c) return lx < c ? t.topLeft[(size_t)y * c + lx] : t.top[y];
    if (y >= H - c) {
        const int ry = y - (H - c);
        return lx < c ? t.bottomLeft[(size_t)ry * c + lx] : t.bottom[ry];
    }
    return lx < c ? t.left[lx] : 0;
}

// The shade in four strips around the window: only where something is drawn - the
// margin and, at top and bottom, the corners. A resize then sends Windows a few hundred
// KB instead of a window-sized bitmap, and a strip whose length stayed the same is only
// moved (top and bottom keep their pixels in a vertical resize, left and right in a
// horizontal one).
struct ShadeLayout {
    int W, H;        // the whole shade: window + margin on each side
    bool single;     // too small to split: the top strip is the whole shade
    RECT strip[4];   // shade coordinates: top, bottom, left, right
};

ShadeLayout LayoutShade(int fw, int fh, const ShadeLook& l, float extent) {
    ShadeLayout s;
    s.W = fw + 2 * l.margin;
    s.H = fh + 2 * l.margin;
    const int T = l.margin + (int)ceilf(extent) + 3;   // margin + the corner's curve
    const int S = l.margin + 3;                        // margin + edge and hairline
    s.single = s.H < 2 * T + 1 || s.W < 2 * S + 1;
    for (RECT& r : s.strip) r = {0, 0, 0, 0};
    if (s.single) {
        s.strip[0] = {0, 0, s.W, s.H};
    } else {
        s.strip[0] = {0, 0, s.W, T};
        s.strip[1] = {0, s.H - T, s.W, s.H};
        s.strip[2] = {0, T, S, s.H - T};
        s.strip[3] = {s.W - S, T, s.W, s.H - T};
    }
    return s;
}

// Draws strip `r` of the whole W x H shade into its window at `pos` (screen px).
void RenderStrip(HWND strip, const RECT& r, POINT pos, int W, int H, bool tiled, const ShadeShape& sh,
                 const ShadeLook& l) {
    const int w = r.right - r.left, h = r.bottom - r.top;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;   // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bmp && bits) {
        uint32_t* px = (uint32_t*)bits;
        for (int y = 0; y < h; ++y) {
            uint32_t* row = px + (size_t)y * w;
            for (int x = 0; x < w; ++x) row[x] = ShadeAt(r.left + x, r.top + y, W, H, tiled, sh, l);
        }
        HGDIOBJ old = SelectObject(mem, bmp);
        POINT src = {0, 0};
        SIZE size = {w, h};
        BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        UpdateLayeredWindow(strip, screen, &pos, &size, mem, &src, 0, &blend, ULW_ALPHA);
        SelectObject(mem, old);
    }
    if (bmp) DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
}

void CloakLike(HWND helper, HWND owner) {
    UINT cloaked = 0;
    BOOL cloak = SUCCEEDED(DwmGetWindowAttribute(owner, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked;
    DwmSetWindowAttribute(helper, DWMWA_CLOAK, &cloak, sizeof(cloak));
}

HWND CreateShade(HWND owner) {
    HINSTANCE exe = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = exe;
    wc.lpszClassName = kShadeClass;
    RegisterClassExW(&wc);   // fails harmlessly once it exists
    HWND shade = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, kShadeClass,
                                 L"", WS_POPUP | WS_DISABLED, 0, 0, 1, 1, owner, nullptr, exe, nullptr);
    if (!shade) return nullptr;
    BOOL on = TRUE;
    DwmSetWindowAttribute(shade, DWMWA_TRANSITIONS_FORCEDISABLED, &on, sizeof(on));
    DwmSetWindowAttribute(shade, DWMWA_EXCLUDED_FROM_PEEK, &on, sizeof(on));
    CloakLike(shade, owner);   // hidden with its window while that one is cloaked
    return shade;
}

void DestroyShade(HWND shade) {
    if (!shade) return;
    if (GetWindowThreadProcessId(shade, nullptr) == GetCurrentThreadId()) {
        DestroyWindow(shade);
    } else {
        PostMessageW(shade, WM_CLOSE, 0, 0);   // DefWindowProc destroys it in its thread
    }
}

// ---------------------------------------------------------------------------
// The grip: the resize border the region took away (Windows' is outside the visible
// window, in the invisible frame). A practically invisible (alpha 1) popup owned by the
// window, shaped to a ring from the window's edge out to the border width, that tells
// Windows "left / right / corner ..." and hands a press on to the window as a resize.
// Unlike the shade it runs code of this DLL, so every grip is destroyed, in its own
// thread, before the DLL may go (CornersOnShutdown).
// ---------------------------------------------------------------------------

const wchar_t kGripClass[] = L"MacWinCornerGrip";
bool g_gripLeft = false;   // a grip could not be destroyed: the DLL must stay

LRESULT GripHitTest(HWND g, int sx, int sy) {
    RECT gr;
    if (!GetWindowRect(g, &gr)) return HTNOWHERE;
    const int x = sx - gr.left, y = sy - gr.top, W = gr.right - gr.left, H = gr.bottom - gr.top;
    const int band = (int)GetWindowLongPtrW(g, GWLP_USERDATA);
    const int corner = band * 3;   // how far along an edge a press still sizes diagonally
    const bool left = x < corner, right = x >= W - corner, top = y < corner, bottom = y >= H - corner;
    if (top && left) return HTTOPLEFT;
    if (top && right) return HTTOPRIGHT;
    if (bottom && left) return HTBOTTOMLEFT;
    if (bottom && right) return HTBOTTOMRIGHT;
    if (x < band) return HTLEFT;
    if (x >= W - band) return HTRIGHT;
    if (y < band) return HTTOP;
    if (y >= H - band) return HTBOTTOM;
    return HTNOWHERE;
}

LRESULT CALLBACK GripProc(HWND g, UINT msg, WPARAM wp, LPARAM lp) {
    HookScope scope;
    switch (msg) {
        case WM_NCHITTEST:
            return GripHitTest(g, (short)LOWORD(lp), (short)HIWORD(lp));
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_NCLBUTTONDOWN:
        case WM_NCLBUTTONDBLCLK:
            if (wp >= HTLEFT && wp <= HTBOTTOMRIGHT) {
                // The window's own sizing loop, as if its border had been pressed:
                // SC_SIZE + WMSZ_LEFT .. WMSZ_BOTTOMRIGHT (= HTLEFT .. HTBOTTOMRIGHT - 9).
                if (HWND owner = GetWindow(g, GW_OWNER)) {
                    SetForegroundWindow(owner);
                    SendMessageW(owner, WM_SYSCOMMAND, SC_SIZE | (WPARAM)(wp - HTLEFT + 1), lp);
                }
                return 0;
            }
            break;
    }
    return DefWindowProcW(g, msg, wp, lp);
}

HWND CreateGrip(HWND owner) {
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = GripProc;
    wc.hInstance = g_hModule;
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = kGripClass;
    RegisterClassExW(&wc);   // unregistered again in CornersOnShutdown
    HWND grip = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, kGripClass, L"", WS_POPUP, 0, 0,
                                1, 1, owner, nullptr, g_hModule, nullptr);
    if (!grip) return nullptr;
    SetLayeredWindowAttributes(grip, 0, 1, LWA_ALPHA);   // invisible, still takes the mouse
    BOOL on = TRUE;
    DwmSetWindowAttribute(grip, DWMWA_TRANSITIONS_FORCEDISABLED, &on, sizeof(on));
    DwmSetWindowAttribute(grip, DWMWA_EXCLUDED_FROM_PEEK, &on, sizeof(on));
    CloakLike(grip, owner);
    return grip;
}

void DestroyGrip(HWND grip) {
    if (!grip) return;
    if (GetWindowThreadProcessId(grip, nullptr) == GetCurrentThreadId()) {
        DestroyWindow(grip);
        return;
    }
    DWORD_PTR result = 0;
    SendMessageTimeoutW(grip, WM_CLOSE, 0, 0, SMTO_ABORTIFHUNG, 500, &result);
    if (IsWindow(grip)) g_gripLeft = true;
}

// The ring the grip covers: band px around the window, minus the window's shape (so the
// spots the rounded corners leave free size the window too).
void ShapeGrip(HWND grip, int fw, int fh, int band, int radius, int smoothing) {
    HRGN ring = CreateRectRgn(0, 0, fw + 2 * band, fh + 2 * band);
    RECT inner = {band, band, band + fw, band + fh};
    HRGN hole = MakeRegion(inner, radius, smoothing);
    if (ring && hole) CombineRgn(ring, ring, hole, RGN_DIFF);
    if (hole) DeleteObject(hole);
    if (ring && !SetWindowRgn(grip, ring, FALSE)) DeleteObject(ring);
}

// Puts the shade and the grip around the window and shows them: strips whose length
// changed are drawn anew (all of them with `render`: a new radius or scale), the rest
// only move - every move in one DeferWindowPos, so they travel together. Coordinates in
// the window's own DPI mode.
void PlaceShade(HWND h, bool render) {
    Cut c;
    if (!FindCut(h, &c) || !c.region) return;
    bool created = false;
    for (HWND& strip : c.shade) {
        if (!strip) {
            strip = CreateShade(h);
            created = true;
        }
    }
    if (!c.grip) {
        c.grip = CreateGrip(h);
        created = true;
    }
    if (created) {
        AcquireSRWLockExclusive(&g_lock);
        auto it = g_cuts.find(h);
        if (it != g_cuts.end()) {
            memcpy(it->second.shade, c.shade, sizeof(c.shade));
            it->second.grip = c.grip;
        }
        ReleaseSRWLockExclusive(&g_lock);
        render = true;
    }
    DPI_AWARENESS_CONTEXT old = SetThreadDpiAwarenessContext(GetWindowDpiAwarenessContext(h));
    RECT wr;
    if (GetWindowRect(h, &wr)) {
        const UINT dpi = GetDpiForWindow(h);
        const int fw = c.frame.right - c.frame.left, fh = c.frame.bottom - c.frame.top;
        const int left = wr.left + c.frame.left, top = wr.top + c.frame.top;
        const ShadeLook l = Look((float)dpi / 96.0f);
        const ShadeShape sh = MakeShadeShape(fw * 0.5f, fh * 0.5f, c.radius, c.smoothing);
        const ShadeLayout s = LayoutShade(fw, fh, l, sh.extent);
        const POINT origin = {left - l.margin, top - l.margin};
        const bool all = render || s.single || s.single != c.shadeSingle;
        HDWP defer = BeginDeferWindowPos(5);
        auto move = [&](HWND w, int x, int y, int cx, int cy, UINT flags) {
            flags |= SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER;
            if (defer) defer = DeferWindowPos(defer, w, nullptr, x, y, cx, cy, flags);
            if (!defer) SetWindowPos(w, nullptr, x, y, cx, cy, flags);
        };

        AcquireSRWLockExclusive(&g_tilesLock);
        MakeTiles(c.radius, c.smoothing, dpi);
        const bool tiled = g_tiles.c > 0 && s.W >= 2 * g_tiles.c && s.H >= 2 * g_tiles.c;
        for (int i = 0; i < 4; ++i) {
            HWND strip = c.shade[i];
            if (!strip) continue;
            const RECT& r = s.strip[i];
            if (r.right <= r.left || r.bottom <= r.top) {
                if (IsWindowVisible(strip)) ShowWindow(strip, SW_HIDE);
                continue;
            }
            const POINT pos = {origin.x + r.left, origin.y + r.top};
            const bool redraw = all || (i < 2 ? s.W != c.shadeW : s.H != c.shadeH);
            if (redraw) {
                RenderStrip(strip, r, pos, s.W, s.H, tiled, sh, l);
            } else {
                move(strip, pos.x, pos.y, 0, 0, SWP_NOSIZE);
            }
            if (!IsWindowVisible(strip)) ShowWindow(strip, SW_SHOWNOACTIVATE);
        }
        ReleaseSRWLockExclusive(&g_tilesLock);
        c.shadeW = s.W;
        c.shadeH = s.H;
        c.shadeSingle = s.single;

        if (c.grip) {
            const int band = GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
            SetWindowLongPtrW(c.grip, GWLP_USERDATA, band);
            if (render || fw != c.gripW || fh != c.gripH) {
                ShapeGrip(c.grip, fw, fh, band, c.radius, c.smoothing);
                c.gripW = fw;
                c.gripH = fh;
            }
            move(c.grip, left - band, top - band, fw + 2 * band, fh + 2 * band, SWP_SHOWWINDOW);
        }
        if (defer) EndDeferWindowPos(defer);

        AcquireSRWLockExclusive(&g_lock);
        auto it = g_cuts.find(h);
        if (it != g_cuts.end()) {
            it->second.shadeW = c.shadeW;
            it->second.shadeH = c.shadeH;
            it->second.shadeSingle = c.shadeSingle;
            it->second.gripW = c.gripW;
            it->second.gripH = c.gripH;
        }
        ReleaseSRWLockExclusive(&g_lock);
    }
    if (old) SetThreadDpiAwarenessContext(old);
}

void HideShade(HWND h) {
    Cut c;
    if (!FindCut(h, &c)) return;
    for (HWND strip : c.shade) {
        if (strip && IsWindowVisible(strip)) ShowWindow(strip, SW_HIDE);
    }
    if (c.grip && IsWindowVisible(c.grip)) ShowWindow(c.grip, SW_HIDE);
}

void Uncut(HWND h) {
    Cut c;
    AcquireSRWLockExclusive(&g_lock);
    auto it = g_cuts.find(h);
    bool found = it != g_cuts.end();
    if (found) {
        c = it->second;
        g_cuts.erase(it);
    }
    ReleaseSRWLockExclusive(&g_lock);
    if (!found) return;
    ++t_cutting;
    for (HWND strip : c.shade) DestroyShade(strip);
    DestroyGrip(c.grip);
    if (c.overridden) GiveBackOverrides(h, c.saved);
    if (c.region) SetWindowRgn(h, nullptr, TRUE);
    if (c.square) SetCornerPreference(h, DWMWCP_DEFAULT);
    --t_cutting;
}

void Update(HWND h) {
    if (t_cutting) return;
    LONG_PTR style = GetWindowLongPtrW(h, GWL_STYLE);
    // Minimized: the cut stays for the restore (Windows hides the shade with the window).
    // Hidden: the cut stays too, the shade goes.
    if (style & (WS_CHILD | WS_MINIMIZE)) return;
    if (!(style & WS_VISIBLE)) {
        HideShade(h);
        return;
    }
    Cut prev;
    bool had = FindCut(h, &prev);

    const SharedSettings* s = Settings();
    bool want = s && s->windowCorners && EffectsActiveHere() && !ExcludedHere(s) && !(style & WS_MAXIMIZE);
    int base = s ? s->cornerRadius : 0, smoothing = s ? s->cornerSmoothing : 0;
    // Windows 11's own corner (circular, 8 px) is DWM's: it keeps the shadow and border.
    if (want && smoothing == 0 && base == 8) want = false;
    if (!want || (!had && !Eligible(h, style))) {
        if (had && !want) Uncut(h);
        return;
    }
    RECT frame;
    bool fullScreen = false;
    if (!Measure(h, &frame, &fullScreen) || fullScreen) {
        if (had) Uncut(h);
        return;
    }

    Cut next;
    if (had) {
        memcpy(next.shade, prev.shade, sizeof(next.shade));
        next.grip = prev.grip;
        next.arranged = prev.arranged;
        next.arrangedSet = prev.arrangedSet;
    }
    next.frame = frame;
    next.radius = MulDiv(base, (int)GetDpiForWindow(h), 96);
    next.smoothing = smoothing;
    next.region = next.radius > 0;
    next.square = next.radius == 0;
    RECT box;
    if (had && EqualRect(&prev.frame, &next.frame) && prev.radius == next.radius &&
        prev.smoothing == next.smoothing &&
        (!next.region || GetWindowRgnBox(h, &box) != ERROR)) {   // Chromium may reset the region
        PlaceShade(h, false);
        return;
    }
    if (had && prev.overridden) {
        next.overridden = true;
        memcpy(next.saved, prev.saved, sizeof(next.saved));
    }
    if (next.region && !next.overridden) {
        TakeOverrides(h, next.saved);
        next.overridden = true;
    } else if (!next.region && next.overridden) {   // square: no region, DWM's frame is right
        GiveBackOverrides(h, next.saved);
        next.overridden = false;
    }
    AcquireSRWLockExclusive(&g_lock);
    g_cuts[h] = next;
    ReleaseSRWLockExclusive(&g_lock);

    ++t_cutting;
    if (next.square != (had && prev.square)) SetCornerPreference(h, next.square ? DWMWCP_DONOTROUND : DWMWCP_DEFAULT);
    const bool newShape = !had || !prev.region || prev.radius != next.radius || prev.smoothing != next.smoothing;
    if (next.region) {
        // No forced repaint for a resize: the program repaints for its new size anyway,
        // and a second full repaint per step is what made resizing stutter.
        HRGN rgn = MakeRegion(frame, next.radius, next.smoothing);
        if (rgn && !SetWindowRgn(h, rgn, newShape)) DeleteObject(rgn);   // on success the system owns it
    } else if (had && prev.region) {
        SetWindowRgn(h, nullptr, TRUE);
    }
    if (!next.region && had && prev.region) {   // square now: DWM's frame is back
        for (HWND strip : next.shade) DestroyShade(strip);
        DestroyGrip(next.grip);
        AcquireSRWLockExclusive(&g_lock);
        auto it = g_cuts.find(h);
        if (it != g_cuts.end()) {
            for (HWND& strip : it->second.shade) strip = nullptr;
            it->second.grip = nullptr;
        }
        ReleaseSRWLockExclusive(&g_lock);
    }
    --t_cutting;
    PlaceShade(h, newShape);
}

// Snap (snap layouts, Win + arrows, dragging to an edge) puts a window so that its
// visible frame - DWM's extended frame bounds - fills the zone. A cut window has no
// such bounds any more (they collapse to the window rect), so the invisible resize
// border would end up in the zone and leave a gap at screen edges and between windows.
// A window Windows reports as arranged gets that border back outside the zone: its
// visible part then fills the zone exactly, as an uncut window's does.
thread_local int t_fitting = 0;

void FitArranged(HWND h) {
    typedef BOOL(WINAPI * IsWindowArranged_t)(HWND);
    static const auto isArranged =
        (IsWindowArranged_t)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "IsWindowArranged");
    if (!isArranged || t_cutting || t_fitting) return;
    Cut c;
    if (!FindCut(h, &c) || !c.region) return;
    const bool arranged = isArranged(h) != FALSE;
    if (!arranged) {
        if (c.arrangedSet) {
            AcquireSRWLockExclusive(&g_lock);
            auto it = g_cuts.find(h);
            if (it != g_cuts.end()) it->second.arrangedSet = false;
            ReleaseSRWLockExclusive(&g_lock);
        }
        return;
    }
    DPI_AWARENESS_CONTEXT old = SetThreadDpiAwarenessContext(GetWindowDpiAwarenessContext(h));
    RECT wr;
    if (GetWindowRect(h, &wr) && !(c.arrangedSet && EqualRect(&wr, &c.arranged))) {
        // The border: where the client area (the visible window) sits in the window rect.
        const int l = c.frame.left, t = c.frame.top;
        const int r = (wr.right - wr.left) - c.frame.right, b = (wr.bottom - wr.top) - c.frame.bottom;
        if (l > 0 || t > 0 || r > 0 || b > 0) {
            RECT want = {wr.left - l, wr.top - t, wr.right + r, wr.bottom + b};
            AcquireSRWLockExclusive(&g_lock);
            auto it = g_cuts.find(h);
            if (it != g_cuts.end()) {
                it->second.arranged = want;
                it->second.arrangedSet = true;
            }
            ReleaseSRWLockExclusive(&g_lock);
            ++t_fitting;
            SetWindowPos(h, nullptr, want.left, want.top, want.right - want.left, want.bottom - want.top,
                         SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
            --t_fitting;
        }
    }
    if (old) SetThreadDpiAwarenessContext(old);
}

BOOL CALLBACK PostRefresh(HWND h, LPARAM) {
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(h)) PostMessageW(h, g_cornersMsg, kRefresh, 0);
    return TRUE;
}

}  // namespace

LRESULT CornersDefWindowProc(DefWindowProcFn original, HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    HookScope scope;
    if (msg == WM_NCDESTROY) {
        AcquireSRWLockExclusive(&g_lock);
        g_cuts.erase(h);
        ReleaseSRWLockExclusive(&g_lock);
        return original(h, msg, wp, lp);
    }
    if (msg == WM_EXITSIZEMOVE) {
        // The end of a move / resize: the shade is drawn once more for the final size.
        LRESULT r = original(h, msg, wp, lp);
        PlaceShade(h, true);
        return r;
    }
    if (msg == g_cornersMsg) {
        if (wp == kRemove) {
            Uncut(h);
        } else if (wp == kFit) {
            FitArranged(h);
        } else {
            Update(h);
        }
        return 0;
    }
    // WM_WINDOWPOSCHANGED: only a new size (or a window being shown, hidden or restyled)
    // can change the cut; a plain move only takes the shade along.
    LRESULT r = original(h, msg, wp, lp);
    const WINDOWPOS* pos = (const WINDOWPOS*)lp;
    if (pos && !(GetWindowLongPtrW(h, GWL_STYLE) & WS_CHILD)) {
        if (!(pos->flags & SWP_NOSIZE) || (pos->flags & (SWP_SHOWWINDOW | SWP_HIDEWINDOW | SWP_FRAMECHANGED))) {
            Update(h);
        } else if (!(pos->flags & SWP_NOMOVE) && !t_cutting) {
            PlaceShade(h, false);
        }
        // Snapped? Asked once the move is through (Windows may mark the window arranged
        // only after placing it).
        Cut c;
        if ((!(pos->flags & SWP_NOSIZE) || !(pos->flags & SWP_NOMOVE)) && !t_fitting && FindCut(h, &c) && c.region) {
            PostMessageW(h, g_cornersMsg, kFit, 0);
        }
    }
    return r;
}

void CornersOnActivate() {
    UINT msg = RegisterWindowMessageW(MACWIN_CORNERS_MESSAGE);
    if (!msg) return;
    g_cornersMsg = msg;
    // Windows that are already open get their corners right away.
    EnumWindows(PostRefresh, 0);
}

// Before the hooks go: every window gets its own shape back. The window's thread does
// it (in DefWindowProc); if the program never passes the message on, it is done from
// here - only for a thread that answered, never blocking on a hung one.
bool CornersOnShutdown() {
    std::vector<HWND> windows;
    AcquireSRWLockShared(&g_lock);
    for (const auto& kv : g_cuts) windows.push_back(kv.first);
    ReleaseSRWLockShared(&g_lock);
    for (HWND h : windows) {
        DWORD_PTR result = 0;
        if (SendMessageTimeoutW(h, g_cornersMsg, kRemove, 0, SMTO_ABORTIFHUNG, 500, &result)) {
            Uncut(h);
        } else {
            // A hung window keeps its region, but its shade goes once the thread wakes up.
            Cut c;
            if (FindCut(h, &c)) {
                for (HWND strip : c.shade) DestroyShade(strip);
                DestroyGrip(c.grip);
            }
        }
    }
    AcquireSRWLockExclusive(&g_lock);
    g_cuts.clear();
    ReleaseSRWLockExclusive(&g_lock);
    // The grip class points at this DLL's code: it must go (and can only go once no grip
    // is left), or a later load at another address would reuse a stale window procedure.
    if (!UnregisterClassW(kGripClass, g_hModule) && GetLastError() != ERROR_CLASS_DOES_NOT_EXIST) g_gripLeft = true;
    return !g_gripLeft;
}

// A program setting one of the overridden attributes on a cut window (Chromium sets its
// backdrop on theme changes): remembered for the uncut, ours stays meanwhile.
namespace {

__attribute__((noinline)) HRESULT WINAPI OverrideSlow(HWND h, DWORD attr, LPCVOID data, DWORD size) {
    HookScope scope;
    const int i = OverrideIndex(attr);
    if (i >= 0 && data && size == sizeof(int)) {
        AcquireSRWLockExclusive(&g_lock);
        auto it = g_cuts.find(h);
        bool cut = it != g_cuts.end() && it->second.overridden;
        if (cut) it->second.saved[i] = *(const int*)data;
        ReleaseSRWLockExclusive(&g_lock);
        if (cut) {
            int ours = kOverrides[i].value;
            return DwmSetWindowAttribute_Original(h, attr, &ours, sizeof(ours));
        }
    }
    return DwmSetWindowAttribute_Original(h, attr, data, size);
}

// The program cloaking its own window (Chromium does, e.g. while it is hidden or being
// set up): the shade and grip go with it.
__attribute__((noinline)) HRESULT WINAPI CloakSlow(HWND h, DWORD attr, LPCVOID data, DWORD size) {
    HookScope scope;
    HRESULT hr = DwmSetWindowAttribute_Original(h, attr, data, size);
    if (SUCCEEDED(hr) && data && size == sizeof(BOOL)) CornersOnCloak(h, *(const BOOL*)data != FALSE);
    return hr;
}

HRESULT WINAPI DwmSetWindowAttribute_Hook(HWND h, DWORD attr, LPCVOID data, DWORD size) {
    if (attr == DWMWA_SYSTEMBACKDROP_TYPE || attr == DWMWA_BORDER_COLOR || attr == DWMWA_WINDOW_CORNER_PREFERENCE) {
        MW_MUSTTAIL return OverrideSlow(h, attr, data, size);
    }
    if (attr == DWMWA_CLOAK) MW_MUSTTAIL return CloakSlow(h, attr, data, size);
    MW_MUSTTAIL return DwmSetWindowAttribute_Original(h, attr, data, size);
}

}  // namespace

// Optional: without it a program that sets one of the overridden attributes again just
// gets it back until the next cut, so a failure never costs the genie.
bool CornersInstallHooks() {
    HMODULE dwm = GetModuleHandleW(L"dwmapi.dll");
    void* target = dwm ? (void*)GetProcAddress(dwm, "DwmSetWindowAttribute") : nullptr;
    MH_STATUS st = target ? MH_CreateHook(target, (void*)DwmSetWindowAttribute_Hook,
                                          (void**)&DwmSetWindowAttribute_Original)
                          : MH_ERROR_FUNCTION_NOT_FOUND;
    if (st != MH_OK) {
        DwmSetWindowAttribute_Original = nullptr;
        DbgLog(L"MH_CreateHook(DwmSetWindowAttribute) failed: %d", (int)st);
    }
    return true;
}

void CornersOnCloak(HWND h, bool cloaked) {
    Cut c;
    if (!FindCut(h, &c)) return;
    BOOL cloak = cloaked;
    auto set = DwmSetWindowAttribute_Original ? DwmSetWindowAttribute_Original : DwmSetWindowAttribute;
    for (HWND strip : c.shade) {
        if (strip) set(strip, DWMWA_CLOAK, &cloak, sizeof(cloak));
    }
    if (c.grip) set(c.grip, DWMWA_CLOAK, &cloak, sizeof(cloak));
}
