// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 KosaWLeb. Created by KosaWLeb: https://github.com/KosaWLeb/MacWin

#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "common/settings.h"
#include "host/json.h"

struct GenieSettings {
    int style = 0;   // GenieStyle: 0 modern (Potassiumuncher), 1 classic (original)
    int durationMs = 450;
    bool restore = true;
    // Opening / closing windows: MW_EFFECT_* and their durations.
    int openEffect = MW_EFFECT_GENIE;
    int closeEffect = MW_EFFECT_GENIE;
    int openDurationMs = 450;
    int closeDurationMs = 400;
    bool multiMonitor = false;
    bool unhideTaskbar = true;
    int unhideDurationMs = 450;
    int meshQuality = 48;
    // Window corners: radius at 100 % scaling and Figma-style smoothing in percent
    // (0 = Windows 11's circular corner, 100 = macOS squircle). The genie always uses
    // them; windowCorners also gives every real window that shape.
    int cornerRadius = 15;
    int cornerSmoothing = 100;
    bool windowCorners = true;
    std::vector<std::wstring> excluded;
};

// Programs that must never get the DLL: games whose anti-cheat reacts to foreign
// modules, and the folders games are installed to. The same list is the settings
// app's default (settings-app/Services/SettingsStore.cs).
std::vector<std::wstring> DefaultNoInject();

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
    bool updateAutoInstall = false;
    GenieSettings genie;
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
};
