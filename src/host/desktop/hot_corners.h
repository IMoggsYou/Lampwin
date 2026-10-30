// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

#pragma once
#include <windows.h>
#include <thread>
#include "host/settings_store.h"

// Hot corners: the pointer pushed into a corner of a screen, and kept there for a moment,
// does what the settings say. A low-level mouse hook on a thread of its own watches the
// pointer (only while a corner has an action); the action itself is posted to the main
// window and done there (wParam: the CornerAction, lParam: the point, packed).
class HotCorners {
public:
    // `notify` gets `message` for every corner the pointer reaches.
    void Init(HWND notify, UINT message);
    // Starts or stops the thread to match the settings.
    void Apply(const DesktopSettings& settings);
    void Stop();

    // True when something runs full screen (a game, a video, a presentation): corners
    // then do nothing, so a game's pointer in a corner doesn't open the overview.
    static bool FullScreenAppRunning(POINT pt);
    // Performs `action` (main thread). `hwnd`: a window of ours for the system commands.
    static void Perform(CornerAction action, HWND hwnd);

private:
    void Run();
    static LRESULT CALLBACK MouseProc(int code, WPARAM wp, LPARAM lp);
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    void OnMove(POINT pt);
    void OnTimer();
    void UpdateScreens();

    HWND notify_ = nullptr;
    UINT message_ = 0;
    std::thread thread_;
    DWORD threadId_ = 0;
    // Read by the hook thread, written by the main thread before it starts.
    CornerAction actions_[kCornerCount] = {};

    // Hook thread only.
    HWND window_ = nullptr;
    HHOOK hook_ = nullptr;
    int corner_ = -1;        // the corner the pointer is in, -1: none
    bool fired_ = false;     // done for this visit; the pointer has to leave first
    POINT where_ = {};
};
