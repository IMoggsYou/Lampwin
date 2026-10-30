// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

#include "host/desktop/hot_corners.h"
#include <shellapi.h>
#include <vector>
#include "host/log.h"

namespace {

// How long the pointer has to stay in a corner. Long enough that a quick click on a
// maximized window's close button (top right) never starts anything.
const UINT kDwellMs = 220;
const UINT_PTR kDwellTimer = 1;
// The corner is this many pixels from the screen's corner pixel, in each direction.
const int kZone = 1;
const wchar_t kWindowClass[] = L"MacWinHotCorners";

HotCorners* g_self = nullptr;
std::vector<RECT> g_screens;   // hook thread only

bool OnAnyScreen(LONG x, LONG y) {
    for (const RECT& r : g_screens) {
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return true;
    }
    return false;
}

BOOL CALLBACK AddScreen(HMONITOR mon, HDC, LPRECT, LPARAM) {
    MONITORINFO mi = {sizeof(mi)};
    if (GetMonitorInfoW(mon, &mi)) g_screens.push_back(mi.rcMonitor);
    return TRUE;
}

// Presses the keys in order and lets them go in reverse (Win+D and the like).
void PressKeys(std::initializer_list<WORD> keys) {
    std::vector<INPUT> inputs;
    for (WORD vk : keys) {
        INPUT in = {INPUT_KEYBOARD};
        in.ki.wVk = vk;
        in.ki.dwFlags = vk == VK_LWIN ? KEYEVENTF_EXTENDEDKEY : 0;
        inputs.push_back(in);
    }
    for (size_t i = keys.size(); i-- > 0;) {
        INPUT in = inputs[i];
        in.ki.dwFlags |= KEYEVENTF_KEYUP;
        inputs.push_back(in);
    }
    if (SendInput((UINT)inputs.size(), inputs.data(), sizeof(INPUT)) != inputs.size()) {
        Log(L"hot corner: SendInput failed: %lu", GetLastError());
    }
}

}  // namespace

void HotCorners::Init(HWND notify, UINT message) {
    notify_ = notify;
    message_ = message;
}

void HotCorners::Apply(const DesktopSettings& settings) {
    bool same = thread_.joinable();
    for (int i = 0; i < kCornerCount; ++i) same = same && actions_[i] == settings.corners[i];
    if (same) return;
    Stop();
    if (!settings.AnyCorner()) return;
    for (int i = 0; i < kCornerCount; ++i) actions_[i] = settings.corners[i];
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    thread_ = std::thread([this, ready] {
        threadId_ = GetCurrentThreadId();
        MSG msg;
        PeekMessageW(&msg, nullptr, 0, 0, PM_NOREMOVE);   // the queue exists before Stop can post to it
        SetEvent(ready);
        Run();
    });
    WaitForSingleObject(ready, 5000);
    CloseHandle(ready);
}

void HotCorners::Stop() {
    if (!thread_.joinable()) return;
    PostThreadMessageW(threadId_, WM_QUIT, 0, 0);
    thread_.join();
    threadId_ = 0;
}

void HotCorners::Run() {
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kWindowClass;
    RegisterClassExW(&wc);
    // A hidden top-level window: it hears about screens being added, moved or removed.
    window_ = CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                              wc.hInstance, nullptr);
    g_self = this;
    corner_ = -1;
    UpdateScreens();
    hook_ = SetWindowsHookExW(WH_MOUSE_LL, MouseProc, GetModuleHandleW(nullptr), 0);
    if (!hook_) Log(L"hot corners: mouse hook failed: %lu", GetLastError());
    else Log(L"hot corners: on");

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) DispatchMessageW(&msg);

    if (hook_) UnhookWindowsHookEx(hook_);
    hook_ = nullptr;
    g_self = nullptr;
    if (window_) DestroyWindow(window_);
    window_ = nullptr;
}

void HotCorners::UpdateScreens() {
    g_screens.clear();
    EnumDisplayMonitors(nullptr, nullptr, AddScreen, 0);
}

LRESULT CALLBACK HotCorners::MouseProc(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && g_self) {
        const auto* info = (const MSLLHOOKSTRUCT*)lp;
        if (wp == WM_MOUSEMOVE) {
            g_self->OnMove(info->pt);
        } else if (g_self->corner_ >= 0) {
            // A click in the corner (a maximized window's close button) is what was meant.
            g_self->fired_ = true;
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

LRESULT CALLBACK HotCorners::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (g_self) {
        if (msg == WM_TIMER && wp == kDwellTimer) {
            g_self->OnTimer();
            return 0;
        }
        if (msg == WM_DISPLAYCHANGE || (msg == WM_SETTINGCHANGE && wp == SPI_SETWORKAREA)) {
            g_self->UpdateScreens();
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// Called for every move of the pointer: no more than a few comparisons.
void HotCorners::OnMove(POINT pt) {
    int corner = -1;
    for (const RECT& r : g_screens) {
        if (pt.x < r.left || pt.x >= r.right || pt.y < r.top || pt.y >= r.bottom) continue;
        const bool left = pt.x <= r.left + kZone, right = pt.x >= r.right - 1 - kZone;
        const bool top = pt.y <= r.top + kZone, bottom = pt.y >= r.bottom - 1 - kZone;
        if (!(left || right) || !(top || bottom)) break;
        // Only a corner the pointer can't go past: not one where two screens meet.
        const LONG x = left ? r.left : r.right - 1, y = top ? r.top : r.bottom - 1;
        const LONG outX = left ? x - 1 : x + 1, outY = top ? y - 1 : y + 1;
        if (OnAnyScreen(outX, y) || OnAnyScreen(x, outY) || OnAnyScreen(outX, outY)) break;
        corner = top ? (left ? kTopLeft : kTopRight) : (left ? kBottomLeft : kBottomRight);
        break;
    }
    if (corner >= 0 && actions_[corner] == CornerAction::None) corner = -1;
    if (corner == corner_) return;
    corner_ = corner;
    fired_ = false;
    where_ = pt;
    if (corner >= 0) SetTimer(window_, kDwellTimer, kDwellMs, nullptr);
    else KillTimer(window_, kDwellTimer);
}

void HotCorners::OnTimer() {
    KillTimer(window_, kDwellTimer);
    if (corner_ < 0 || fired_) return;
    // Dragging something into the corner isn't asking for it.
    if (GetAsyncKeyState(VK_LBUTTON) < 0 || GetAsyncKeyState(VK_RBUTTON) < 0 || GetAsyncKeyState(VK_MBUTTON) < 0) return;
    fired_ = true;
    PostMessageW(notify_, message_, (WPARAM)actions_[corner_], MAKELPARAM((WORD)(short)where_.x, (WORD)(short)where_.y));
}

bool HotCorners::FullScreenAppRunning(POINT pt) {
    QUERY_USER_NOTIFICATION_STATE state;
    if (SUCCEEDED(SHQueryUserNotificationState(&state)) &&
        (state == QUNS_BUSY || state == QUNS_RUNNING_D3D_FULL_SCREEN || state == QUNS_PRESENTATION_MODE)) {
        return true;
    }
    // A borderless window covering the whole screen the pointer is on (games, videos).
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    wchar_t cls[64] = {};
    GetClassNameW(fg, cls, ARRAYSIZE(cls));
    if (wcscmp(cls, L"Progman") == 0 || wcscmp(cls, L"WorkerW") == 0) return false;
    MONITORINFO mi = {sizeof(mi)};
    RECT wr;
    if (!GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi) || !GetWindowRect(fg, &wr)) return false;
    const LONG style = GetWindowLongW(fg, GWL_STYLE);
    return !(style & WS_CAPTION) && wr.left <= mi.rcMonitor.left && wr.top <= mi.rcMonitor.top &&
           wr.right >= mi.rcMonitor.right && wr.bottom >= mi.rcMonitor.bottom;
}

void HotCorners::Perform(CornerAction action, HWND hwnd) {
    switch (action) {
        case CornerAction::Desktop:
            PressKeys({VK_LWIN, 'D'});
            break;
        case CornerAction::TaskView:
            PressKeys({VK_LWIN, VK_TAB});
            break;
        case CornerAction::Start:
            PressKeys({VK_LWIN});
            break;
        case CornerAction::Notifications:
            PressKeys({VK_LWIN, 'N'});
            break;
        case CornerAction::Lock:
            LockWorkStation();
            break;
        case CornerAction::DisplayOff:
            DefWindowProcW(hwnd, WM_SYSCOMMAND, SC_MONITORPOWER, 2);
            break;
        case CornerAction::ScreenSaver:
            DefWindowProcW(hwnd, WM_SYSCOMMAND, SC_SCREENSAVE, 0);
            break;
        case CornerAction::None:
        case CornerAction::Overview:   // the App shows its own overview
        case CornerAction::Count:
            break;
    }
}
