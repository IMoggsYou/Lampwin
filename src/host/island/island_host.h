// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

#pragma once
#include <windows.h>
#include <string>
#include <thread>
#include "host/widgets/widget_draw.h"

// The island: a black pill at the top of the screen showing what goes on - music, a call,
// screen mirroring - and short notes (charging, silent, airplane mode, ...), after the
// Dynamic Island of the iPhone. It lives on a thread of its own, only while it is on.
namespace island {

struct Config {
    bool enabled = true;
    // What it shows.
    bool music = true, calls = true, mirroring = true, charging = true, lowBattery = true, airplane = true,
         mobileData = true, silent = true, output = true;
    bool youtube = false;        // YouTube videos count as music (the widget's Now playing too)
    bool hoverExpand = true;     // the pointer over it shows more; else a click does
    bool followActive = false;   // on the monitor of the window in front, else the main one

    bool operator==(const Config& o) const = default;
};

// What the island asks of the main thread (wParam of the notify message).
enum Request : WPARAM {
    kOpen = 1,   // lParam: std::wstring* to open as the user (Windows' settings pages)
};

class IslandHost {
public:
    ~IslandHost() { Stop(); }
    // The main thread's window gets `msg` with a Request; it owns the lParam objects.
    void Init(HWND notify, UINT msg);
    // New settings, texts or language (main thread). Starts the thread when the island is
    // on and stops it when it is off.
    void Apply(const Config& config, const widgets::Texts& texts, bool rtl);
    void Stop();

private:
    HWND notify_ = nullptr;
    UINT msg_ = 0;
    std::thread thread_;
};

// `MacWin.exe --island-snapshot <dir> [--lang <code>]`: every view of the island with
// sample data into <dir>\island.png, and how each comes in, frame by frame, into
// <dir>\island-motion.png.
int IslandSnapshot(const std::wstring& dir, const std::wstring& language);

}  // namespace island
