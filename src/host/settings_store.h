// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "common/settings.h"
#include "host/json.h"
#include "host/widgets/widget_config.h"

struct GenieSettings {
    // GenieStyle: 0 modern (Potassiumuncher), 1 classic (original, the default), 2 scale
    int style = 1;
    int durationMs = 450;
    bool restore = true;
    // Opening / closing windows: MW_EFFECT_* and their durations.
    int openEffect = MW_EFFECT_GENIE;
    int closeEffect = MW_EFFECT_GENIE;
    int openDurationMs = 450;
    int closeDurationMs = 400;
    bool multiMonitor = false;
    // Windows' "Animation effects" off = no window animations from MacWin either.
    bool followSystemMotion = true;
    // Windows' energy saver (or battery saver) on = no window animations either.
    bool followEnergySaver = true;
    bool unhideTaskbar = true;
    int unhideDurationMs = 450;
    int meshQuality = 48;
    // Window corners: radius at 100 % scaling and Figma-style smoothing in percent
    // (0 = Windows 11's circular corner, 100 = macOS squircle). windowCorners gives real
    // windows that shape; the genie keeps each window's own corner (see IpcServer).
    int cornerRadius = 15;
    int cornerSmoothing = 100;
    bool windowCorners = true;
    std::vector<std::wstring> excluded;
};

// Programs that must never get the DLL: games whose anti-cheat reacts to foreign
// modules, and the folders games are installed to. The same list is the settings
// app's default (settings-app/Services/SettingsStore.cs).
std::vector<std::wstring> DefaultNoInject();

// What a hot corner does when the pointer stays in it (settings.json names in
// settings_store.cpp, the same as the settings app's).
enum class CornerAction { None, Overview, Desktop, TaskView, Start, Notifications, Lock, DisplayOff, ScreenSaver, Count };
enum Corner { kTopLeft, kTopRight, kBottomLeft, kBottomRight, kCornerCount };

struct DesktopSettings {
    CornerAction corners[kCornerCount] = {CornerAction::None, CornerAction::None, CornerAction::None,
                                          CornerAction::None};
    // Ctrl+Alt+Up shows the window overview (like Ctrl+Up on a Mac).
    bool overviewHotkey = false;

    bool AnyCorner() const {
        for (CornerAction a : corners) {
            if (a != CornerAction::None) return true;
        }
        return false;
    }
};

// Light by day, dark by night: Windows' colour mode and the wallpaper follow the sun (or
// two times of day).
struct AppearanceSettings {
    bool darkMode = false;
    bool wallpaper = false;
    std::wstring dayWallpaper, nightWallpaper;
    // Sunrise and sunset where the PC is; else (or when that isn't known) these two times.
    bool followSun = true;
    int dayStartMinutes = 7 * 60;
    int nightStartMinutes = 19 * 60;

    bool Any() const { return darkMode || wallpaper; }
};

struct HostSettings {
    bool enabled = true;
    // Test mode: the DLL only loads into the programs on devAllowlist.
    bool devMode = false;
    std::vector<std::wstring> devAllowlist{L"notepad.exe", L"macwintestwindow.exe"};
    std::vector<std::wstring> noInject = DefaultNoInject();
    // "auto" (the Windows display language) or a language code (see i18n.h).
    std::wstring language = L"auto";
    // GitHub Releases: check once a day; install without asking.
    bool updateCheck = true;
    bool updateAutoInstall = true;
    // Own icons for programs (the settings app's App icons page manages the files).
    bool appIcons = true;
    GenieSettings genie;
    widgets::Config widgets;
    DesktopSettings desktop;
    AppearanceSettings appearance;
};

// settings.json (%APPDATA%\MacWin) <-> HostSettings <-> shared section read by the DLLs.
class SettingsStore {
public:
    // Creates the shared section; `sa` decides who may open it (see elevation.h).
    bool Create(uint32_t hostInstance, SECURITY_ATTRIBUTES* sa);
    // File only (no shared section): for one-off command-line edits.
    void InitPath();
    void SetIpcWindow(HWND hwnd);
    // Missing or broken file: defaults (a missing file is written out). Settings from
    // an older version are brought up to date (and saved).
    void Load();
    // Keys this version doesn't know (written by a newer one) are kept.
    bool Save() const;
    void Publish();
    // Publishes enabled = false without touching the file (used while shutting down).
    void PublishDisabled();
    HostSettings& Get() { return s_; }
    // Windows' animation effects (SPI_GETCLIENTAREAANIMATION), read at start and when they
    // change; publish afterwards.
    void SetSystemAnimations(bool on) { systemAnimations_ = on; }
    // Windows' energy saver, read at start and when it changes; publish afterwards.
    void SetEnergySaver(bool on) { energySaver_ = on; }
    // No window animations now: Windows' are off (or its energy saver is on) and the
    // settings say to follow that.
    bool ReduceMotion() const {
        return (s_.genie.followSystemMotion && !systemAnimations_) || (s_.genie.followEnergySaver && energySaver_);
    }
    const std::wstring& Path() const { return path_; }
    uint32_t Instance() const { return instance_; }

private:
    void Write(bool enabled);

    HANDLE map_ = nullptr;
    SharedSettings* view_ = nullptr;
    HostSettings s_;
    json::Value root_;   // the file as read: unknown keys survive a Save
    std::wstring path_;
    uint32_t instance_ = 0;
    uint64_t ipcHwnd_ = 0;
    bool systemAnimations_ = true;
    bool energySaver_ = false;
};
