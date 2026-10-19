// MacWinTestWindow.exe - development helper. Shows a plain window and, on request,
// minimizes / restores itself through both paths the hooks cover (the title bar's
// SC_MINIMIZE / SC_RESTORE and ShowWindow):
//   --cycle N   N slow rounds (1.5 s apart)
//   --rapid N   N rounds of clicks faster than the animation itself, so every genie
//               is turned around mid-flight
// The exit code reports the final state: 0 = restored and visible, 10 = still
// minimized, 11 = still cloaked (invisible), 12 = both.

#include <windows.h>
#include <dwmapi.h>
#include <stdlib.h>
#include <vector>

namespace {

enum Action { kSysMin, kSysRestore, kShowMin, kShowRestore, kCheck };
struct Step {
    Action action;
    UINT waitMs;   // until the next step
};

std::vector<Step> g_steps;
size_t g_next = 0;
int g_exitCode = 0;
const UINT_PTR kTimer = 1;

void Paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    for (int y = 0; y < rc.bottom; y += 4) {
        RECT band = {0, y, rc.right, y + 4};
        int k = rc.bottom ? y * 255 / rc.bottom : 0;
        HBRUSH b = CreateSolidBrush(RGB(40 + k / 3, 90 + k / 4, 200 - k / 3));
        FillRect(dc, &band, b);
        DeleteObject(b);
    }
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    HFONT font = CreateFontW(-36, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
                             L"Segoe UI");
    HGDIOBJ old = SelectObject(dc, font);
    DrawTextW(dc, L"MacWin – okno testowe", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, old);
    DeleteObject(font);
    EndPaint(hwnd, &ps);
}

void Run(HWND hwnd) {
    KillTimer(hwnd, kTimer);
    if (g_next >= g_steps.size()) {
        DestroyWindow(hwnd);
        return;
    }
    const Step& s = g_steps[g_next++];
    switch (s.action) {
        case kSysMin: PostMessageW(hwnd, WM_SYSCOMMAND, SC_MINIMIZE, 0); break;
        case kSysRestore: PostMessageW(hwnd, WM_SYSCOMMAND, SC_RESTORE, 0); break;
        case kShowMin: ShowWindow(hwnd, SW_MINIMIZE); break;
        case kShowRestore: ShowWindow(hwnd, SW_RESTORE); break;
        case kCheck: {
            BOOL iconic = IsIconic(hwnd);
            UINT cloaked = 0;
            DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
            g_exitCode = (iconic && cloaked) ? 12 : iconic ? 10 : cloaked ? 11 : 0;
            break;
        }
    }
    SetTimer(hwnd, kTimer, s.waitMs, nullptr);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: Paint(hwnd); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_TIMER:
            if (wp == kTimer) Run(hwnd);
            return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int Count(const wchar_t* cmdLine, const wchar_t* flag) {
    const wchar_t* c = wcsstr(cmdLine, flag);
    if (!c) return 0;
    int n = _wtoi(c + wcslen(flag));
    return n > 0 ? n : 1;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR cmdLine, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    for (int i = Count(cmdLine, L"--cycle"); i > 0; --i) {
        g_steps.insert(g_steps.end(),
                       {{kSysMin, 1500}, {kSysRestore, 1500}, {kShowMin, 1500}, {kShowRestore, 1500}});
    }
    for (int i = Count(cmdLine, L"--rapid"); i > 0; --i) {
        // Every wait is shorter than the 450 ms genie: each click lands mid-flight.
        g_steps.insert(g_steps.end(), {{kSysMin, 200},
                                       {kSysRestore, 150},
                                       {kSysMin, 100},
                                       {kSysRestore, 250},
                                       {kShowMin, 120},
                                       {kShowRestore, 90},
                                       {kShowMin, 60},
                                       {kShowRestore, 1200}});
    }
    if (!g_steps.empty()) g_steps.push_back({kCheck, 300});

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.lpszClassName = L"MacWinTestWindow";
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"MacWin – okno testowe", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                                CW_USEDEFAULT, 900, 600, nullptr, nullptr, hInst, nullptr);
    ShowWindow(hwnd, SW_SHOWNORMAL);
    UpdateWindow(hwnd);
    if (!g_steps.empty()) SetTimer(hwnd, kTimer, 1500, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return g_exitCode;
}
