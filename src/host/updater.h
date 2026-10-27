// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

#pragma once
#include <windows.h>
#include <string>
#include <thread>

// Updates from GitHub Releases (IMoggsYou/Lampwin), in MacWin.exe so they also work
// without the settings app. A background thread asks the GitHub API for the latest
// release (at most once a day, or when asked), downloads MacWinSetup-X.Y.Z.exe and
// SHA256SUMS.txt from that release only (github.com / *.githubusercontent.com over
// HTTPS) and checks the installer's SHA-256 before anything runs it. The state goes to
// %LOCALAPPDATA%\MacWin\update.json for the settings app; the host window gets
// WM_APP_UPDATE when something changed.

struct Release {
    std::wstring version;       // "1.2.0" (tag without the "v")
    std::wstring notesUrl;      // the release page
    std::wstring notes;         // that version's CHANGELOG.md section (Markdown)
    std::wstring installerName; // MacWinSetup-1.2.0.exe
    std::wstring installerUrl;
    std::wstring sumsUrl;       // SHA256SUMS.txt
};

// Pure helpers (unit-tested): versions like "1.2.3" or "v1.2.3"; <0, 0, >0.
int CompareVersions(const std::wstring& a, const std::wstring& b);
bool ParseLatestRelease(const std::wstring& json, Release* out);
// The hash for `fileName` in a SHA256SUMS.txt ("<64 hex>  <name>" lines), lowercase.
std::wstring FindSha256(const std::string& sums, const std::wstring& fileName);
bool IsTrustedDownloadHost(const std::wstring& host);
// How a check ends: available, upToDate, unusable (a release without a usable installer),
// failed (manual check, no answer), or - for a scheduled one that failed - the status it
// started from (idle instead of checking).
std::wstring CheckOutcome(bool fetched, unsigned long http, bool parsed, bool newer, bool manual,
                          const std::wstring& before);

class Updater {
public:
    enum Event : WPARAM { kStateChanged = 1, kInstallerReady = 2 };

    // Posts `msg` (wParam: Event) to `notify` when the state changes.
    void Start(HWND notify, UINT msg);
    void Stop();

    // Ask GitHub now (manual) or if the last check is a day old (scheduled).
    void Check(bool manual);
    // Download and verify the available release; kInstallerReady when done.
    void Download();

    // Snapshot of the state, for the tray and the dialogs (main thread).
    // idle, checking, upToDate, available, unusable, downloading, installing, failed, failedInstall
    std::wstring Status();
    Release Available();             // version empty if none
    std::wstring InstallerPath();    // after kInstallerReady
    bool ShouldNotify();             // a newly found version nobody was told about yet (true once)
    std::wstring UpdatedFrom();      // set once after an update: the version before it
    void SetInstalling();            // the installer was started
    void SetFailed();

private:
    void ThreadMain();
    void DoCheck(bool manual);
    void DoDownload();
    void SetStatus(const std::wstring& status, bool notify = true);
    void LoadState();
    void SaveState();

    HWND notify_ = nullptr;
    UINT msg_ = 0;
    std::thread thread_;
    HANDLE wake_ = nullptr;
    volatile bool stop_ = false;
    volatile LONG wantCheck_ = 0;    // 1 scheduled, 2 manual
    volatile LONG wantDownload_ = 0;

    SRWLOCK lock_ = SRWLOCK_INIT;
    std::wstring status_ = L"idle";
    Release available_;
    std::wstring installerPath_;
    std::wstring lastCheck_;         // ISO 8601 UTC
    ULONGLONG lastCheckFiletime_ = 0;
    std::wstring notifiedVersion_;
    std::wstring updatedFrom_;
};
