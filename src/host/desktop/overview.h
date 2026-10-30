// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

#pragma once
#include <windows.h>
#include <dwmapi.h>
#include <string>
#include <vector>

// The window overview (like Exposé on a Mac): every window of the screen the pointer is on
// glides into a grid over a dimmed, blurred desktop; a click brings one to the front.
// The windows are DWM thumbnails, so they stay live and no program is touched. Runs on
// the main thread; between its show and hide animations it costs nothing.
class Overview {
public:
    void Init(HINSTANCE hInst);
    // Shows the overview on the screen the pointer is on, or hides it when it is open.
    // `animate`: false while Windows' animation effects (or the energy saver) say so.
    void Toggle(bool animate);
    void Close();
    bool IsOpen() const { return hwnd_ != nullptr; }

private:
    struct Item {
        HWND window = nullptr;
        HTHUMBNAIL thumb = nullptr;
        RECT outer = {};   // the window as Windows has it (with its invisible resize border)
        RECT frame = {};   // what can be seen of it, on the overlay
        RECT slot = {};    // where its visible part goes in the grid
        double scale = 1;
        std::wstring title;
    };
    enum class Phase { Opening, Open, Closing };

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);
    void CollectWindows(HMONITOR monitor);
    void Layout();
    void CaptureBackground();
    void StartPhase(Phase phase);
    void Step();
    void Place(double t);
    void Paint(HDC dc);
    int HitTest(POINT pt) const;
    void Choose(int index);
    void Finish();
    void Select(int index);
    void MoveSelection(int dx, int dy);

    HINSTANCE hInst_ = nullptr;
    HWND hwnd_ = nullptr;
    RECT screen_ = {};        // the monitor, in screen coordinates
    RECT area_ = {};          // where the grid goes, on the overlay
    double dpiScale_ = 1;
    std::vector<Item> items_;
    int columns_ = 1;
    int hover_ = -1;          // under the pointer
    int selected_ = -1;       // chosen with the keyboard
    int chosen_ = -1;         // clicked: comes to the front when the overview closes
    Phase phase_ = Phase::Opening;
    LARGE_INTEGER start_ = {}, frequency_ = {};
    double durationMs_ = 0;
    double progress_ = 0;     // 0 = the windows where they are, 1 = in the grid
    double from_ = 0;         // the progress the current phase started at
    HBITMAP sharp_ = nullptr; // the desktop as it was
    HBITMAP soft_ = nullptr;  // the same, blurred and darkened
    HBITMAP frame_ = nullptr; // each frame is put together here, then shown at once
    HFONT font_ = nullptr;
    bool wasActive_ = false;
    bool animate_ = true;
};
