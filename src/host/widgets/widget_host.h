// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

#pragma once
#include <windows.h>
#include <string>
#include <thread>
#include <vector>
#include "host/widgets/widget_config.h"
#include "host/widgets/widget_draw.h"

// The desktop widgets. They live on a thread of their own (the main thread answers the
// hook DLLs within milliseconds and must never wait for a redraw or the network), which
// only runs while at least one widget is shown.
namespace widgets {

// What the host asks of the main thread (wParam of the notify message).
enum Request : WPARAM {
    kEdited = 1,     // lParam: std::vector<Item>* to save (moved, resized, removed)
    kOpen,           // lParam: std::wstring* to open as the user (a file, an app, a link)
    kOpenSettings,   // the settings window, on the Widgets page
    kHideAll,        // turn "Show widgets" off
};

class WidgetHost {
public:
    ~WidgetHost() { Stop(); }
    // The main thread's window gets `msg` with a Request; it owns the lParam objects.
    void Init(HWND notify, UINT msg);
    // New settings, texts or language (main thread). Starts the thread when there is a
    // widget to show and stops it when there is none.
    void Apply(const Config& config, const Texts& texts, const std::wstring& locale, bool rtl);
    void Stop();

private:
    bool Start();

    HWND notify_ = nullptr;
    UINT msg_ = 0;
    std::thread thread_;
    HWND control_ = nullptr;   // the widget thread's own window
};

}  // namespace widgets
