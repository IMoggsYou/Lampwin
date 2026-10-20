// MacWinTestWindow --corners: checks MacWin's window corners (hook/effects/corners.cpp)
// on a window MacWin takes for a Chromium one - Chromium's class name prefix, no title
// bar, the client area is the whole visible window - that also asks DWM for rounded
// corners and Mica, as Electron apps do. Steps: first cut, resize, move, maximize,
// restore, Snap (Win + Left), close. Each step checks the window region, the shade (shadow
// strips) and the grip (resize border) around the window, and the DWM attributes MacWin
// overrides while a window is cut.
//
// Needs MacWin running with "Okna innych programów" on and this program not excluded.
// The Snap step presses Win + Left (and Esc for Snap Assist) - only while this window is
// in the foreground, otherwise it is skipped. Report: %TEMP%\MacWinCornerTest.txt and,
// without --quiet, a message box. Exit code: number of failed checks; 100 = the window
// was never cut.

#include <windows.h>
#include <dwmapi.h>
#include <stdarg.h>
#include <stdio.h>
#include <string>
#include <vector>

int RunCornerTest(HINSTANCE hInst, bool quiet);

namespace {

const wchar_t kClass[] = L"Chrome_WidgetWin_MacWinCornerTest";

struct Report {
    std::wstring text;
    int failed = 0, passed = 0, skipped = 0;

    void Line(const wchar_t* fmt, ...) {
        wchar_t buf[512];
        va_list ap;
        va_start(ap, fmt);
        vswprintf(buf, 512, fmt, ap);
        va_end(ap);
        text += buf;
        text += L"\r\n";
    }
    void Check(bool ok, const wchar_t* what, const wchar_t* detail = L"") {
        (ok ? passed : failed)++;
        Line(L"  %ls %ls%ls%ls", ok ? L"OK  " : L"FAIL", what, *detail ? L" - " : L"", detail);
    }
    void Skip(const wchar_t* what, const wchar_t* why) {
        skipped++;
        Line(L"  SKIP %ls - %ls", what, why);
    }
};

int ResizeBorder(HWND h) {
    UINT dpi = GetDpiForWindow(h);
    return GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
}

LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_NCCALCSIZE:
            // Chromium's custom frame: no title bar; the resize border stays outside the
            // client area at the sides and the bottom.
            if (wp && !IsZoomed(h)) {
                auto* p = (NCCALCSIZE_PARAMS*)lp;
                const int b = ResizeBorder(h);
                p->rgrc[0].left += b;
                p->rgrc[0].right -= b;
                p->rgrc[0].bottom -= b;
                return 0;
            }
            break;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            RECT rc;
            GetClientRect(h, &rc);
            HBRUSH b = CreateSolidBrush(RGB(30, 32, 38));
            FillRect(dc, &rc, b);
            DeleteObject(b);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(235, 235, 240));
            HFONT font = CreateFontW(-28, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
                                     L"Segoe UI");
            HGDIOBJ old = SelectObject(dc, font);
            DrawTextW(dc, L"MacWin – test rogów", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(dc, old);
            DeleteObject(font);
            EndPaint(h, &ps);
            return 0;
        }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// Lets this thread run (MacWin cuts the window in its DefWindowProc) for `ms`.
void Pump(int ms) {
    const ULONGLONG until = GetTickCount64() + (ULONGLONG)ms;
    MSG m;
    while (GetTickCount64() < until) {
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 5, QS_ALLINPUT);
    }
}

RECT ClientScreen(HWND h) {
    RECT rc;
    GetClientRect(h, &rc);
    POINT o = {0, 0};
    ClientToScreen(h, &o);
    OffsetRect(&rc, o.x, o.y);
    return rc;
}

bool RegionScreen(HWND h, RECT* out) {
    RECT box, wr;
    if (GetWindowRgnBox(h, &box) == ERROR || !GetWindowRect(h, &wr)) return false;
    OffsetRect(&box, wr.left, wr.top);
    *out = box;
    return true;
}

bool Near(const RECT& a, const RECT& b, int tol) {
    return abs(a.left - b.left) <= tol && abs(a.top - b.top) <= tol && abs(a.right - b.right) <= tol &&
           abs(a.bottom - b.bottom) <= tol;
}

std::wstring Str(const RECT& r) {
    wchar_t buf[64];
    swprintf(buf, 64, L"(%ld,%ld)-(%ld,%ld)", r.left, r.top, r.right, r.bottom);
    return buf;
}

// MacWin's windows around a cut window: owned by it, by class name.
struct Helpers {
    HWND owner = nullptr;
    std::vector<HWND> all;
    int visibleShades = 0;
    RECT shadeUnion = {};
    HWND grip = nullptr;
};

BOOL CALLBACK CollectHelper(HWND w, LPARAM lp) {
    Helpers* hs = (Helpers*)lp;
    if (GetWindow(w, GW_OWNER) != hs->owner) return TRUE;
    wchar_t cls[64];
    GetClassNameW(w, cls, 64);
    const bool shade = wcscmp(cls, L"MacWinCornerShade") == 0, grip = wcscmp(cls, L"MacWinCornerGrip") == 0;
    if (!shade && !grip) return TRUE;
    hs->all.push_back(w);
    if (!IsWindowVisible(w)) return TRUE;
    RECT r;
    GetWindowRect(w, &r);
    if (grip) {
        hs->grip = w;
    } else {
        if (hs->visibleShades++ == 0) {
            hs->shadeUnion = r;
        } else {
            UnionRect(&hs->shadeUnion, &hs->shadeUnion, &r);
        }
    }
    return TRUE;
}

Helpers FindHelpers(HWND owner) {
    Helpers hs;
    hs.owner = owner;
    EnumWindows(CollectHelper, (LPARAM)&hs);
    return hs;
}

int DwmInt(HWND h, DWORD attr) {
    int v = -1;
    DwmGetWindowAttribute(h, attr, &v, sizeof(v));
    return v;
}

// The window is cut and everything around it follows its current place and size.
void CheckCut(Report& r, HWND h) {
    const RECT client = ClientScreen(h);
    RECT region;
    const bool hasRegion = RegionScreen(h, &region);
    r.Check(hasRegion && Near(region, client, 1), L"region = the visible window",
            hasRegion ? (Str(region) + L" vs " + Str(client)).c_str() : L"no region");

    const Helpers hs = FindHelpers(h);
    RECT want = client;
    InflateRect(&want, ResizeBorder(h), ResizeBorder(h));
    RECT grip = {};
    if (hs.grip) GetWindowRect(hs.grip, &grip);
    r.Check(hs.grip && Near(grip, want, 1), L"grip = resize border around the window",
            hs.grip ? (Str(grip) + L" vs " + Str(want)).c_str() : L"no visible grip");

    RECT around = client;
    InflateRect(&around, 2, 2);
    RECT inter;
    const bool covers = hs.visibleShades > 0 && IntersectRect(&inter, &hs.shadeUnion, &around) &&
                        EqualRect(&inter, &around);
    wchar_t detail[160];
    swprintf(detail, 160, L"%d strip(s), %ls", hs.visibleShades, Str(hs.shadeUnion).c_str());
    r.Check(covers, L"shadow strips surround the window", detail);
}

// DWM's border color cannot be read back (DwmGetWindowAttribute refuses
// DWMWA_BORDER_COLOR), so only the rounding and the backdrop are checked.
void CheckOverrides(Report& r, HWND h, bool cut) {
    const int corner = DwmInt(h, DWMWA_WINDOW_CORNER_PREFERENCE);
    const int backdrop = DwmInt(h, DWMWA_SYSTEMBACKDROP_TYPE);
    wchar_t detail[96];
    swprintf(detail, 96, L"corner %d, backdrop %d", corner, backdrop);
    if (cut) {
        r.Check(corner == DWMWCP_DONOTROUND && backdrop == DWMSBT_NONE, L"DWM rounding and Mica off", detail);
    } else {
        r.Check(corner == DWMWCP_ROUND && backdrop == DWMSBT_MAINWINDOW, L"the program's DWM rounding and Mica back",
                detail);
    }
}

void PressKeys(std::initializer_list<WORD> down) {
    std::vector<INPUT> in;
    for (WORD vk : down) {
        INPUT i = {};
        i.type = INPUT_KEYBOARD;
        i.ki.wVk = vk;
        in.push_back(i);
    }
    for (auto it = down.end(); it != down.begin();) {
        INPUT i = {};
        i.type = INPUT_KEYBOARD;
        i.ki.wVk = *--it;
        i.ki.dwFlags = KEYEVENTF_KEYUP;
        in.push_back(i);
    }
    SendInput((UINT)in.size(), in.data(), sizeof(INPUT));
}

void SaveReport(const Report& r, const std::wstring& path) {
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return;
    std::string utf8(WideCharToMultiByte(CP_UTF8, 0, r.text.c_str(), (int)r.text.size(), nullptr, 0, nullptr, nullptr),
                     '\0');
    WideCharToMultiByte(CP_UTF8, 0, r.text.c_str(), (int)r.text.size(), &utf8[0], (int)utf8.size(), nullptr, nullptr);
    fwrite("\xEF\xBB\xBF", 1, 3, f);
    fwrite(utf8.data(), 1, utf8.size(), f);
    fclose(f);
}

}  // namespace

int RunCornerTest(HINSTANCE hInst, bool quiet) {
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = Proc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);

    Report r;
    r.Line(L"MacWin – test rogów okien");
    HWND h = CreateWindowExW(0, kClass, L"MacWin – test rogów", WS_OVERLAPPEDWINDOW, 200, 150, 900, 620, nullptr,
                             nullptr, hInst, nullptr);
    if (!h) return 100;
    // What Electron asks of DWM; MacWin turns it off while the window is cut.
    int round = DWMWCP_ROUND, mica = DWMSBT_MAINWINDOW;
    COLORREF border = RGB(255, 140, 0);
    BOOL dark = TRUE;
    DwmSetWindowAttribute(h, DWMWA_WINDOW_CORNER_PREFERENCE, &round, sizeof(round));
    DwmSetWindowAttribute(h, DWMWA_BORDER_COLOR, &border, sizeof(border));
    DwmSetWindowAttribute(h, DWMWA_SYSTEMBACKDROP_TYPE, &mica, sizeof(mica));
    DwmSetWindowAttribute(h, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    SetWindowPos(h, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    ShowWindow(h, SW_SHOWNORMAL);
    SetForegroundWindow(h);

    // The first cut (after the open animation, if there is one).
    RECT region;
    for (int waited = 0; waited < 4000 && !RegionScreen(h, &region); waited += 250) Pump(250);
    Pump(400);
    int code = 0;
    if (!RegionScreen(h, &region)) {
        r.Line(L"");
        r.Line(L"Okno nie zostało przycięte: MacWin nie działa, przełącznik „Okna innych programów” jest");
        r.Line(L"wyłączony albo ten program jest w wykluczeniach.");
        code = 100;
    } else {
        r.Line(L"");
        r.Line(L"1. Pierwsze przycięcie");
        CheckCut(r, h);
        CheckOverrides(r, h, true);

        r.Line(L"2. Zmiana rozmiaru");
        SetWindowPos(h, nullptr, 0, 0, 700, 480, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        Pump(500);
        CheckCut(r, h);

        r.Line(L"3. Przesunięcie");
        RECT wr;
        GetWindowRect(h, &wr);
        SetWindowPos(h, nullptr, wr.left + 90, wr.top + 70, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        Pump(400);
        CheckCut(r, h);

        r.Line(L"4. Maksymalizacja");
        ShowWindow(h, SW_MAXIMIZE);
        Pump(1200);
        RECT dummy;
        r.Check(!RegionScreen(h, &dummy), L"no region (square corners)");
        const Helpers hs = FindHelpers(h);
        r.Check(hs.visibleShades == 0 && !hs.grip, L"no shadow strips or grip");
        CheckOverrides(r, h, false);

        r.Line(L"5. Przywrócenie");
        ShowWindow(h, SW_RESTORE);
        Pump(1200);
        CheckCut(r, h);
        CheckOverrides(r, h, true);

        r.Line(L"6. Snap (Win + strzałka w lewo)");
        typedef BOOL(WINAPI * IsWindowArranged_t)(HWND);
        auto isArranged = (IsWindowArranged_t)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "IsWindowArranged");
        SetForegroundWindow(h);
        Pump(200);
        if (!isArranged) {
            r.Skip(L"Snap", L"no IsWindowArranged in this Windows");
        } else if (GetForegroundWindow() != h) {
            r.Skip(L"Snap", L"the test window is not in the foreground, keys would go elsewhere");
        } else {
            GetWindowRect(h, &wr);
            PressKeys({VK_LWIN, VK_LEFT});
            Pump(900);
            if (GetForegroundWindow() != h) PressKeys({VK_ESCAPE});   // Snap Assist took the focus
            Pump(700);
            MONITORINFO mi = {sizeof(mi)};
            GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi);
            const RECT client = ClientScreen(h);
            r.Check(isArranged(h) != FALSE, L"Windows reports the window as snapped");
            const bool fills = abs(client.left - mi.rcWork.left) <= 1 && abs(client.top - mi.rcWork.top) <= 1 &&
                               abs(client.bottom - mi.rcWork.bottom) <= 1;
            r.Check(fills, L"the visible window reaches the screen edges",
                    (Str(client) + L" in " + Str(mi.rcWork)).c_str());
            CheckCut(r, h);
            SetWindowPos(h, nullptr, wr.left, wr.top, wr.right - wr.left, wr.bottom - wr.top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            Pump(500);
        }

        r.Line(L"7. Zamknięcie");
        const std::vector<HWND> helpers = FindHelpers(h).all;
        PostMessageW(h, WM_CLOSE, 0, 0);
        Pump(1500);
        r.Check(!IsWindow(h), L"window closed");
        int left = 0;
        for (HWND w : helpers) left += IsWindow(w) ? 1 : 0;
        wchar_t detail[64];
        swprintf(detail, 64, L"%d of %d left", left, (int)helpers.size());
        r.Check(left == 0, L"shadow strips and grip gone", detail);
        code = r.failed;
    }
    if (IsWindow(h)) DestroyWindow(h);

    r.Line(L"");
    r.Line(L"Wynik: %d OK, %d błędów, %d pominiętych.", r.passed, r.failed, r.skipped);
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    const std::wstring path = std::wstring(temp) + L"MacWinCornerTest.txt";
    SaveReport(r, path);
    if (!quiet) {
        std::wstring msg = r.text + L"\r\nRaport: " + path;
        MessageBoxW(nullptr, msg.c_str(), L"MacWin – test rogów",
                    MB_OK | (code ? MB_ICONWARNING : MB_ICONINFORMATION));
    }
    return code;
}
