// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

#pragma once
#include <windows.h>
#include <atomic>
#include <thread>
#include "host/settings_store.h"
#include "host/widgets/weather.h"

// Light by day, dark by night: at sunrise and sunset (or at two times of day) Windows
// switches between light and dark mode and between two wallpapers. Main thread, one timer
// set to the next change; the location and the changes themselves run on worker threads.
class Appearance {
public:
    // `timer`: an id for a timer on `hwnd`; `located`: a message `hwnd` passes to OnLocated.
    void Init(HWND hwnd, UINT_PTR timer, UINT located);
    void Stop();
    // New settings (or the first ones): Windows is set to what fits the time now.
    void Apply(const AppearanceSettings& settings, const widgets::Place& place);
    // The timer went off, the PC woke up or the clock changed: switch if it is time.
    void Check();
    // The location worker's answer (lParam: a LocatedPlace*, freed here).
    void OnLocated(LPARAM lp);

private:
    struct LocatedPlace {
        bool found = false;
        double latitude = 0, longitude = 0;
    };

    void Evaluate(bool force);
    void Locate();
    bool KnownPlace(double* latitude, double* longitude) const;

    HWND hwnd_ = nullptr;
    UINT_PTR timer_ = 0;
    UINT located_ = 0;
    AppearanceSettings settings_;
    widgets::Place place_;     // the Weather widget's chosen place
    bool haveLocation_ = false;
    double latitude_ = 0, longitude_ = 0;
    ULONGLONG lastLookup_ = 0; // GetTickCount64 of the last location lookup
    std::thread locator_;
    std::atomic<bool> locating_{false};
    widgets::HttpCancel cancel_;
    int applied_ = -1;         // what was last set: 1 day, 0 night, -1 nothing yet
    bool started_ = false;
};
