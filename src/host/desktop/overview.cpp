// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

#include "host/desktop/overview.h"
#include <shellscalingapi.h>
#include <math.h>
#include <stdint.h>
#include <algorithm>
#include "host/log.h"

namespace {

const wchar_t kWindowClass[] = L"MacWinOverview";
const UINT_PTR kFrameTimer = 1;
const double kOpenMs = 320, kCloseMs = 260;

double Lerp(double a, double b, double t) {
    return a + (b - a) * t;
}

// Fast at first, gently into place: the same feel as the settings window's own motion.
double EaseOut(double t) {
    const double u = 1 - t;
    return 1 - u * u * u;
}

int Width(const RECT& r) {
    return r.right - r.left;
}

int Height(const RECT& r) {
    return r.bottom - r.top;
}

bool IsCloaked(HWND h) {
    DWORD cloaked = 0;
    return SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked;
}

// The windows Alt+Tab would show: visible, not minimized, not a tool window, not owned.
bool IsOverviewWindow(HWND h) {
    if (!IsWindowVisible(h) || IsIconic(h) || IsCloaked(h) || GetWindowTextLengthW(h) == 0) return false;
    const LONG_PTR ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
    const bool appWindow = (ex & WS_EX_APPWINDOW) != 0;
    if (!appWindow && ((ex & WS_EX_TOOLWINDOW) || GetWindow(h, GW_OWNER))) return false;
    if (ex & WS_EX_NOACTIVATE) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId()) return false;   // the widgets, the genie's own windows
    wchar_t cls[64] = {};
    GetClassNameW(h, cls, ARRAYSIZE(cls));
    for (const wchar_t* shell : {L"Progman", L"WorkerW", L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd"}) {
        if (wcscmp(cls, shell) == 0) return false;
    }
    return true;
}

struct Collect {
    HMONITOR monitor;
    std::vector<HWND> windows;
};

BOOL CALLBACK CollectWindow(HWND h, LPARAM lp) {
    auto* c = (Collect*)lp;
    if (IsOverviewWindow(h) && MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST) == c->monitor) c->windows.push_back(h);
    return TRUE;
}

// A top-down 32-bit DIB section; `bits` points at its pixels.
HBITMAP CreateDib(HDC dc, int w, int h, uint32_t** bits) {
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* p = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &p, nullptr, 0);
    *bits = (uint32_t*)p;
    return bmp;
}

// Three box blurs in a row look like a Gaussian; on a small picture this is quick.
void BoxBlur(uint32_t* px, int w, int h, int radius) {
    std::vector<uint32_t> tmp((size_t)w * h);
    for (int pass = 0; pass < 3; ++pass) {
        for (int dir = 0; dir < 2; ++dir) {
            const uint32_t* src = dir == 0 ? px : tmp.data();
            uint32_t* dst = dir == 0 ? tmp.data() : px;
            const int lines = dir == 0 ? h : w, len = dir == 0 ? w : h;
            for (int line = 0; line < lines; ++line) {
                for (int i = 0; i < len; ++i) {
                    int sum[3] = {}, n = 0;
                    for (int k = std::max(0, i - radius); k <= std::min(len - 1, i + radius); ++k, ++n) {
                        const uint32_t c = dir == 0 ? src[(size_t)line * w + k] : src[(size_t)k * w + line];
                        sum[0] += c & 0xFF;
                        sum[1] += (c >> 8) & 0xFF;
                        sum[2] += (c >> 16) & 0xFF;
                    }
                    const uint32_t c = (uint32_t)(sum[0] / n) | ((uint32_t)(sum[1] / n) << 8) | ((uint32_t)(sum[2] / n) << 16);
                    if (dir == 0) dst[(size_t)line * w + i] = c;
                    else dst[(size_t)i * w + line] = c;
                }
            }
        }
    }
}

}  // namespace

void Overview::Init(HINSTANCE hInst) {
    hInst_ = hInst;
    QueryPerformanceFrequency(&frequency_);
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kWindowClass;
    RegisterClassExW(&wc);
}

void Overview::Toggle(bool animate) {
    animate_ = animate;
    if (hwnd_) {
        if (phase_ != Phase::Closing) StartPhase(Phase::Closing);
        return;
    }
    POINT pt;
    GetCursorPos(&pt);
    HMONITOR monitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {sizeof(mi)};
    if (!GetMonitorInfoW(monitor, &mi)) return;
    screen_ = mi.rcMonitor;
    UINT dpiX = 96, dpiY = 96;
    GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    dpiScale_ = dpiX / 96.0;
    const int margin = (int)(56 * dpiScale_);
    area_ = {mi.rcWork.left - screen_.left + margin, mi.rcWork.top - screen_.top + margin,
             mi.rcWork.right - screen_.left - margin, mi.rcWork.bottom - screen_.top - margin};

    CollectWindows(monitor);
    CaptureBackground();
    hover_ = selected_ = chosen_ = -1;
    progress_ = 0;
    wasActive_ = false;
    font_ = CreateFontW(-(int)(13 * dpiScale_), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kWindowClass, L"", WS_POPUP, screen_.left, screen_.top,
                            Width(screen_), Height(screen_), nullptr, nullptr, hInst_, this);
    if (!hwnd_) {
        Log(L"overview: no window: %lu", GetLastError());
        Finish();
        return;
    }
    // It appears at once: the windows' own movement is the animation.
    BOOL noTransitions = TRUE;
    DwmSetWindowAttribute(hwnd_, DWMWA_TRANSITIONS_FORCEDISABLED, &noTransitions, sizeof(noTransitions));
    // Back to front, so the window in front stays in front while they move.
    for (auto it = items_.begin(); it != items_.end();) {
        if (FAILED(DwmRegisterThumbnail(hwnd_, it->window, &it->thumb))) {
            it = items_.erase(it);
        } else {
            ++it;
        }
    }
    Layout();
    Place(0);
    ShowWindow(hwnd_, SW_SHOW);
    // A hot corner or a hotkey isn't a click on us: Windows only lets a program that is
    // being used take the focus, so borrow the foreground window's input for a moment.
    HWND fg = GetForegroundWindow();
    DWORD fgThread = fg ? GetWindowThreadProcessId(fg, nullptr) : 0, me = GetCurrentThreadId();
    const bool attached = fgThread && fgThread != me && AttachThreadInput(me, fgThread, TRUE);
    SetForegroundWindow(hwnd_);
    SetFocus(hwnd_);
    if (attached) AttachThreadInput(me, fgThread, FALSE);
    Log(L"overview: %zu windows", items_.size());
    StartPhase(Phase::Opening);
}

void Overview::Close() {
    if (hwnd_) Finish();
}

void Overview::CollectWindows(HMONITOR monitor) {
    items_.clear();
    Collect c{monitor, {}};
    EnumWindows(CollectWindow, (LPARAM)&c);
    // EnumWindows goes front to back; the thumbnails are added back to front.
    for (auto it = c.windows.rbegin(); it != c.windows.rend(); ++it) {
        Item item;
        item.window = *it;
        RECT outer, frame;
        if (!GetWindowRect(item.window, &outer)) continue;
        if (FAILED(DwmGetWindowAttribute(item.window, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame)))) frame = outer;
        if (Width(frame) < 40 || Height(frame) < 40) continue;
        OffsetRect(&outer, -screen_.left, -screen_.top);
        OffsetRect(&frame, -screen_.left, -screen_.top);
        item.outer = outer;
        item.frame = frame;
        wchar_t title[256] = {};
        GetWindowTextW(item.window, title, ARRAYSIZE(title));
        item.title = title;
        items_.push_back(item);
    }
}

// A grid that makes the windows as big as possible: every column count is tried, each
// window keeps its proportions, and none grows past its real size. The windows keep
// their order on the screen (top to bottom, then left to right), so each moves little.
void Overview::Layout() {
    const int n = (int)items_.size();
    if (n == 0) return;
    const double gap = 28 * dpiScale_, label = 34 * dpiScale_;
    const double w = Width(area_), h = Height(area_);
    double best = -1;
    for (int cols = 1; cols <= n; ++cols) {
        const int rows = (n + cols - 1) / cols;
        const double cellW = (w - gap * (cols - 1)) / cols, cellH = (h - gap * (rows - 1)) / rows - label;
        if (cellW <= 0 || cellH <= 0) continue;
        double total = 0;
        for (const Item& it : items_) {
            const double s = std::min({cellW / Width(it.frame), cellH / Height(it.frame), 1.0});
            total += Width(it.frame) * Height(it.frame) * s * s;
        }
        if (total > best) {
            best = total;
            columns_ = cols;
        }
    }
    const int cols = columns_, rows = (n + cols - 1) / cols;
    const double cellW = (w - gap * (cols - 1)) / cols, cellH = (h - gap * (rows - 1)) / rows;

    std::vector<int> order(n);
    for (int i = 0; i < n; ++i) order[i] = i;
    auto centerY = [&](int i) { return items_[i].frame.top + Height(items_[i].frame) / 2; };
    auto centerX = [&](int i) { return items_[i].frame.left + Width(items_[i].frame) / 2; };
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return centerY(a) < centerY(b); });
    const double gridH = rows * cellH + (rows - 1) * gap;
    for (int row = 0; row < rows; ++row) {
        const int first = row * cols, count = std::min(cols, n - first);
        std::stable_sort(order.begin() + first, order.begin() + first + count,
                         [&](int a, int b) { return centerX(a) < centerX(b); });
        const double rowW = count * cellW + (count - 1) * gap;
        const double y = area_.top + (h - gridH) / 2 + row * (cellH + gap);
        for (int k = 0; k < count; ++k) {
            Item& it = items_[order[first + k]];
            const double x = area_.left + (w - rowW) / 2 + k * (cellW + gap);
            it.scale = std::min({cellW / Width(it.frame), (cellH - label) / Height(it.frame), 1.0});
            const double sw = Width(it.frame) * it.scale, sh = Height(it.frame) * it.scale;
            const LONG left = (LONG)lround(x + (cellW - sw) / 2), top = (LONG)lround(y + (cellH - label - sh) / 2);
            it.slot = {left, top, left + (LONG)lround(sw), top + (LONG)lround(sh)};
        }
    }
}

// The desktop as it is now, sharp, and a blurred, darkened copy that the overview fades to.
void Overview::CaptureBackground() {
    const int w = Width(screen_), h = Height(screen_);
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    sharp_ = CreateCompatibleBitmap(screen, w, h);
    frame_ = CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ old = SelectObject(dc, sharp_);
    BitBlt(dc, 0, 0, w, h, screen, screen_.left, screen_.top, SRCCOPY | CAPTUREBLT);

    // Shrink, blur and darken a small copy, then stretch it back up smoothly.
    const int sw = std::max(1, w / 12), sh = std::max(1, h / 12);
    uint32_t* small = nullptr;
    HBITMAP smallBmp = CreateDib(screen, sw, sh, &small);
    HDC sdc = CreateCompatibleDC(screen);
    HGDIOBJ oldSmall = SelectObject(sdc, smallBmp);
    SetStretchBltMode(sdc, HALFTONE);
    SetBrushOrgEx(sdc, 0, 0, nullptr);
    StretchBlt(sdc, 0, 0, sw, sh, dc, 0, 0, w, h, SRCCOPY);
    GdiFlush();
    if (small) {
        BoxBlur(small, sw, sh, 2);
        for (int i = 0; i < sw * sh; ++i) {
            const uint32_t c = small[i];
            small[i] = (((c & 0xFF) * 45 / 100) | ((((c >> 8) & 0xFF) * 45 / 100) << 8) |
                        ((((c >> 16) & 0xFF) * 45 / 100) << 16));
        }
    }
    uint32_t* big = nullptr;
    soft_ = CreateDib(screen, w, h, &big);
    if (big && small) {
        // Bilinear: GDI's own stretching would show the small copy's pixels as squares.
        for (int y = 0; y < h; ++y) {
            const double fy = std::max(0.0, (y + 0.5) * sh / h - 0.5);
            const int y0 = std::min((int)fy, sh - 1), y1 = std::min(y0 + 1, sh - 1);
            const double ty = fy - y0;
            for (int x = 0; x < w; ++x) {
                const double fx = std::max(0.0, (x + 0.5) * sw / w - 0.5);
                const int x0 = std::min((int)fx, sw - 1), x1 = std::min(x0 + 1, sw - 1);
                const double tx = fx - x0;
                const uint32_t a = small[y0 * sw + x0], b = small[y0 * sw + x1];
                const uint32_t c = small[y1 * sw + x0], d = small[y1 * sw + x1];
                uint32_t out = 0;
                for (int shift = 0; shift < 24; shift += 8) {
                    const double top = Lerp((a >> shift) & 0xFF, (b >> shift) & 0xFF, tx);
                    const double bottom = Lerp((c >> shift) & 0xFF, (d >> shift) & 0xFF, tx);
                    out |= (uint32_t)Lerp(top, bottom, ty) << shift;
                }
                big[(size_t)y * w + x] = out;
            }
        }
    }
    SelectObject(sdc, oldSmall);
    DeleteDC(sdc);
    DeleteObject(smallBmp);
    SelectObject(dc, old);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
}

void Overview::StartPhase(Phase phase) {
    phase_ = phase;
    from_ = progress_;
    QueryPerformanceCounter(&start_);
    const double target = phase == Phase::Closing ? 0 : 1;
    durationMs_ = animate_ ? (phase == Phase::Closing ? kCloseMs : kOpenMs) * fabs(target - from_) : 0;
    if (durationMs_ <= 0) {
        progress_ = target;
        Place(progress_);
        if (phase == Phase::Closing) {
            Finish();
            return;
        }
        phase_ = Phase::Open;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    SetTimer(hwnd_, kFrameTimer, 8, nullptr);
}

void Overview::Step() {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    const double ms = (now.QuadPart - start_.QuadPart) * 1000.0 / frequency_.QuadPart;
    const double t = std::min(1.0, ms / durationMs_);
    const double target = phase_ == Phase::Closing ? 0 : 1;
    progress_ = Lerp(from_, target, EaseOut(t));
    Place(progress_);
    if (t < 1) return;
    KillTimer(hwnd_, kFrameTimer);
    if (phase_ == Phase::Closing) {
        Finish();
        return;
    }
    phase_ = Phase::Open;
    // The pointer may already be on a window: show its title.
    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(hwnd_, &pt);
    hover_ = HitTest(pt);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// Every window between where it is (t = 0) and its place in the grid (t = 1).
void Overview::Place(double t) {
    for (const Item& it : items_) {
        const double left = Lerp(it.frame.left, it.slot.left, t), top = Lerp(it.frame.top, it.slot.top, t);
        const double scale = Lerp(1, it.scale, t);
        DWM_THUMBNAIL_PROPERTIES p = {};
        p.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_VISIBLE | DWM_TNP_OPACITY;
        p.fVisible = TRUE;
        p.opacity = 255;
        // The thumbnail is the whole window, invisible resize border included.
        p.rcDestination = {(LONG)lround(left - (it.frame.left - it.outer.left) * scale),
                           (LONG)lround(top - (it.frame.top - it.outer.top) * scale),
                           (LONG)lround(left + (it.outer.right - it.frame.left) * scale),
                           (LONG)lround(top + (it.outer.bottom - it.frame.top) * scale)};
        DwmUpdateThumbnailProperties(it.thumb, &p);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Overview::Paint(HDC target) {
    const int w = Width(screen_), h = Height(screen_);
    HDC dc = CreateCompatibleDC(target);
    HGDIOBJ oldFrame = SelectObject(dc, frame_);
    HDC src = CreateCompatibleDC(target);
    HGDIOBJ oldSrc = SelectObject(src, sharp_);
    BitBlt(dc, 0, 0, w, h, src, 0, 0, SRCCOPY);
    SelectObject(src, soft_);
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, (BYTE)lround(255 * std::clamp(progress_, 0.0, 1.0)), 0};
    GdiAlphaBlend(dc, 0, 0, w, h, src, 0, 0, w, h, blend);
    SelectObject(src, oldSrc);
    DeleteDC(src);

    if (phase_ == Phase::Open) {
        DWORD colorization = 0;
        BOOL opaque = FALSE;
        COLORREF accent = SUCCEEDED(DwmGetColorizationColor(&colorization, &opaque))
                              ? RGB((colorization >> 16) & 0xFF, (colorization >> 8) & 0xFF, colorization & 0xFF)
                              : RGB(0, 120, 215);
        const int ring = std::max(2, (int)(3 * dpiScale_)), round = (int)(14 * dpiScale_);
        HGDIOBJ oldFont = SelectObject(dc, font_);
        SetBkMode(dc, TRANSPARENT);
        for (int i = 0; i < (int)items_.size(); ++i) {
            const Item& it = items_[i];
            const bool lit = i == hover_ || i == selected_;
            if (lit) {
                // A ring around the window (the thumbnail covers the inside).
                HPEN pen = CreatePen(PS_SOLID, ring, accent);
                HGDIOBJ oldPen = SelectObject(dc, pen), oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
                const int grow = ring + (int)(3 * dpiScale_);
                RoundRect(dc, it.slot.left - grow, it.slot.top - grow, it.slot.right + grow, it.slot.bottom + grow, round,
                          round);
                SelectObject(dc, oldBrush);
                SelectObject(dc, oldPen);
                DeleteObject(pen);
            }
            const int labelW = std::max(Width(it.slot), (int)(180 * dpiScale_));
            const int cx = (it.slot.left + it.slot.right) / 2;
            RECT text = {cx - labelW / 2, it.slot.bottom + (int)(10 * dpiScale_), cx + labelW / 2,
                         it.slot.bottom + (int)(32 * dpiScale_)};
            RECT measured = text;
            DrawTextW(dc, it.title.c_str(), (int)it.title.size(), &measured,
                      DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            const int textW = std::min(Width(measured), labelW);
            RECT pill = {cx - textW / 2 - (int)(10 * dpiScale_), text.top - (int)(3 * dpiScale_),
                         cx + textW / 2 + (int)(10 * dpiScale_), text.bottom + (int)(3 * dpiScale_)};
            if (lit) {
                HBRUSH brush = CreateSolidBrush(accent);
                HGDIOBJ oldBrush = SelectObject(dc, brush), oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
                RoundRect(dc, pill.left, pill.top, pill.right, pill.bottom, Height(pill), Height(pill));
                SelectObject(dc, oldPen);
                SelectObject(dc, oldBrush);
                DeleteObject(brush);
            }
            SetTextColor(dc, lit ? RGB(255, 255, 255) : RGB(225, 225, 230));
            DrawTextW(dc, it.title.c_str(), (int)it.title.size(), &text,
                      DT_CENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS | DT_VCENTER);
        }
        SelectObject(dc, oldFont);
    }

    BitBlt(target, 0, 0, w, h, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldFrame);
    DeleteDC(dc);
}

int Overview::HitTest(POINT pt) const {
    if (phase_ != Phase::Open) return -1;
    for (int i = (int)items_.size() - 1; i >= 0; --i) {
        if (PtInRect(&items_[i].slot, pt)) return i;
    }
    return -1;
}

void Overview::Choose(int index) {
    if (index < 0 || index >= (int)items_.size()) return;
    chosen_ = index;
    // It comes to the front behind the overview, which then flies everything back.
    SetForegroundWindow(items_[index].window);
    StartPhase(Phase::Closing);
}

void Overview::Finish() {
    if (hwnd_) KillTimer(hwnd_, kFrameTimer);
    for (Item& it : items_) {
        if (it.thumb) DwmUnregisterThumbnail(it.thumb);
    }
    HWND chosen = chosen_ >= 0 && chosen_ < (int)items_.size() ? items_[chosen_].window : nullptr;
    items_.clear();
    HWND hwnd = hwnd_;
    hwnd_ = nullptr;
    if (hwnd) DestroyWindow(hwnd);
    for (HBITMAP bmp : {sharp_, soft_, frame_}) {
        if (bmp) DeleteObject(bmp);
    }
    if (font_) DeleteObject(font_);
    sharp_ = soft_ = frame_ = nullptr;
    font_ = nullptr;
    if (chosen && IsWindow(chosen)) SetForegroundWindow(chosen);
}

void Overview::Select(int index) {
    if (index == selected_) return;
    selected_ = index;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// The nearest window in the arrow's direction, counting a sideways step double.
void Overview::MoveSelection(int dx, int dy) {
    if (items_.empty()) return;
    int from = selected_ >= 0 ? selected_ : hover_;
    if (from < 0) {
        Select(0);
        return;
    }
    const RECT& r = items_[from].slot;
    const double cx = (r.left + r.right) / 2.0, cy = (r.top + r.bottom) / 2.0;
    int best = -1;
    double bestScore = 1e18;
    for (int i = 0; i < (int)items_.size(); ++i) {
        const RECT& o = items_[i].slot;
        const double ox = (o.left + o.right) / 2.0 - cx, oy = (o.top + o.bottom) / 2.0 - cy;
        const double along = ox * dx + oy * dy, across = fabs(ox * dy) + fabs(oy * dx);
        if (i == from || along <= 1) continue;
        const double score = along + 2 * across;
        if (score < bestScore) {
            bestScore = score;
            best = i;
        }
    }
    if (best >= 0) Select(best);
}

LRESULT CALLBACK Overview::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = (CREATESTRUCTW*)lp;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
    }
    auto* self = (Overview*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (self && self->hwnd_ == hwnd) return self->Handle(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT Overview::Handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_TIMER:
            if (wp == kFrameTimer) Step();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd_, &ps);
            Paint(dc);
            EndPaint(hwnd_, &ps);
            return 0;
        }
        case WM_MOUSEMOVE: {
            POINT pt = {(short)LOWORD(lp), (short)HIWORD(lp)};
            const int hit = HitTest(pt);
            if (hit != hover_) {
                hover_ = hit;
                if (hit >= 0) selected_ = -1;
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            POINT pt = {(short)LOWORD(lp), (short)HIWORD(lp)};
            const int hit = HitTest(pt);
            if (hit >= 0) Choose(hit);
            else if (phase_ != Phase::Closing) StartPhase(Phase::Closing);
            return 0;
        }
        case WM_RBUTTONUP:
            if (phase_ != Phase::Closing) StartPhase(Phase::Closing);
            return 0;
        case WM_KEYDOWN:
            switch (wp) {
                case VK_ESCAPE:
                    if (phase_ != Phase::Closing) StartPhase(Phase::Closing);
                    break;
                case VK_RETURN:
                case VK_SPACE:
                    Choose(selected_ >= 0 ? selected_ : hover_);
                    break;
                case VK_LEFT:
                    MoveSelection(-1, 0);
                    break;
                case VK_RIGHT:
                    MoveSelection(1, 0);
                    break;
                case VK_UP:
                    MoveSelection(0, -1);
                    break;
                case VK_DOWN:
                    MoveSelection(0, 1);
                    break;
                case VK_TAB:
                    if (!items_.empty()) {
                        const int n = (int)items_.size(), step = GetKeyState(VK_SHIFT) < 0 ? n - 1 : 1;
                        Select(((selected_ >= 0 ? selected_ : hover_ >= 0 ? hover_ : n - 1) + step) % n);
                    }
                    break;
            }
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(wp) != WA_INACTIVE) {
                wasActive_ = true;
            } else if (wasActive_ && phase_ != Phase::Closing) {
                // Another window took the focus (Alt+Tab, a notification): step aside.
                StartPhase(Phase::Closing);
            }
            return 0;
        case WM_DISPLAYCHANGE:
            Finish();
            return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}
