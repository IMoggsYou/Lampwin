#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "common/settings.h"

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

struct HostSettings {
    bool enabled = true;
    // Test mode: the DLL only loads into the programs on devAllowlist.
    bool devMode = true;
    std::vector<std::wstring> devAllowlist{L"notepad.exe", L"macwintestwindow.exe"};
    // Programs that must never get the DLL, e.g. games whose anti-cheat reacts to
    // foreign modules.
    std::vector<std::wstring> noInject{
        L"robloxplayerbeta.exe", L"valorant-win64-shipping.exe", L"fortniteclient-win64-shipping.exe",
        L"r5apex.exe", L"r5apex_dx12.exe", L"cs2.exe"};
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
    void Load();   // missing or broken file: defaults (a missing file is written out)
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
    std::wstring path_;
    uint32_t instance_ = 0;
    uint64_t ipcHwnd_ = 0;
};
