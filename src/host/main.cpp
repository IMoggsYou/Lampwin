// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/MacWin

// MacWin host: tray icon, settings, hook installation, IPC and the GPU renderer.

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include "common/names.h"
#include "host/app_icon.h"
#include "host/crash.h"
#include "host/elevation.h"
#include "host/helper32.h"
#include "host/i18n.h"
#include "host/injector.h"
#include "host/ipc_server.h"
#include "host/log.h"
#include "host/render/device.h"
#include "host/settings_store.h"
#include "host/taskbar_locator.h"
#include "host/updater.h"
#include "host/win_events.h"
#include "version.h"

namespace {

const UINT WM_APP_TRAY = WM_APP + 1;
const UINT WM_APP_HELPER_EXITED = WM_APP + 3;
const UINT WM_APP_UPDATE = WM_APP + 4;
const UINT_PTR kPreloadTimer = 1;
const UINT_PTR kUpdateTimer = 2;
const UINT kFirstUpdateCheckMs = 60 * 1000;           // after start: let the session settle
const UINT kUpdateCheckEveryMs = 3 * 60 * 60 * 1000;  // the updater checks at most once a day
const int kHotkeyQuit = 1;
// Settings app -> host (src/common/names.h has the rest): wParam 1 = check now, 2 = install.
const wchar_t kUpdateMessage[] = L"MacWin_" MACWIN_NS L"Update_v1";
const wchar_t kReleasesUrl[] = L"https://github.com/IMoggsYou/MacWin/releases";

enum MenuId : UINT {
    ID_SETTINGS = 100,
    ID_ENABLED,
    ID_DEVMODE,
    ID_OPEN_LOG,
    ID_UPDATE,
    ID_QUIT,
};

// What a click on the current notification balloon does.
enum class BalloonAction { None, UpdateDialog, ReleaseNotes };

class App {
public:
    bool Init(HINSTANCE hInst);
    void Shutdown();

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);
    void AddTrayIcon();
    void UpdateTrayIcon();
    void ShowMenu();
    void ShowBalloon(const std::wstring& title, const std::wstring& text, BalloonAction action);
    void OnUpdateEvent(WPARAM event);
    void ShowUpdateDialog();
    void StartUpdate();
    void RunInstaller();
    void ApplySettings(bool save);
    void RefreshWindowCorners();
    void ReloadSettings();
    void OpenSettingsApp();
    void OpenAsUser(const std::wstring& path);
    std::wstring ExeDir() const;

    HINSTANCE hInst_ = nullptr;
    HWND hwnd_ = nullptr;
    UINT taskbarCreatedMsg_ = 0;
    UINT reloadMsg_ = 0;
    UINT quitMsg_ = 0;
    UINT cornersMsg_ = 0;
    UINT updateMsg_ = 0;
    bool elevated_ = false;
    BalloonAction balloon_ = BalloonAction::None;
    bool installAfterDownload_ = false;
    HICON iconOn_ = nullptr, iconOff_ = nullptr;
    bool trayAdded_ = false;

    HANDLE shutdownEvent_ = nullptr;
    HANDLE aliveMutex_ = nullptr;

    SettingsStore settings_;
    Renderer renderer_;
    TaskbarLocator locator_;
    Injector injector_;
    Helper32 helper32_;
    WinEvents winEvents_;
    Updater updater_;
    IpcServer ipc_{renderer_, settings_, locator_};
};


std::wstring App::ExeDir() const {
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring dir(path, n);
    size_t slash = dir.find_last_of(L"\\/");
    return slash == std::wstring::npos ? dir : dir.substr(0, slash);
}

bool App::Init(HINSTANCE hInst) {
    hInst_ = hInst;
    LARGE_INTEGER q;
    QueryPerformanceCounter(&q);
    const uint32_t instance = GetCurrentProcessId() * 2654435761u ^ (uint32_t)q.QuadPart;
    elevated_ = IsElevated();
    Log(L"MacWin %ls starting (pid %lu, instance %08X, %ls)", MACWIN_VERSION_W, GetCurrentProcessId(), instance,
        elevated_ ? L"administrator: elevated programs are animated too" : L"standard user");

    // Per-instance objects the DLLs watch: the shutdown event, and a mutex we hold for
    // our whole life - if we crash, it is abandoned and the DLLs shut down too. When we
    // are elevated, these (and the settings section) must still be readable from
    // ordinary programs, and must not be writable by them.
    SecurityAttributes eventSa(SharedObjectSddl(SharedObject::ShutdownEvent, elevated_));
    SecurityAttributes mutexSa(SharedObjectSddl(SharedObject::AliveMutex, elevated_));
    SecurityAttributes sectionSa(SharedObjectSddl(SharedObject::Settings, elevated_));
    wchar_t name[96];
    swprintf(name, 96, MACWIN_SHUTDOWN_EVENT_FMT, instance);
    shutdownEvent_ = CreateEventW(eventSa.get(), TRUE, FALSE, name);
    swprintf(name, 96, MACWIN_ALIVE_MUTEX_FMT, instance);
    aliveMutex_ = CreateMutexW(mutexSa.get(), TRUE, name);
    if (!shutdownEvent_ || !aliveMutex_) {
        Log(L"cannot create instance objects: %lu", GetLastError());
        return false;
    }

    if (!settings_.Create(instance, sectionSa.get())) {
        settings_.Load();
        i18n::Init(settings_.Get().language);
        i18n::Message(nullptr, i18n::T(L"host.error.sharedSettings"), MB_ICONERROR);
        return false;
    }
    settings_.Load();
    i18n::Init(settings_.Get().language);

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = MACWIN_HOST_WNDCLASS;
    RegisterClassExW(&wc);
    // A real (hidden) top-level window: message-only windows miss TaskbarCreated.
    // The title tells the settings app whether we run elevated.
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, MACWIN_HOST_WNDCLASS, elevated_ ? L"MacWin (administrator)" : L"MacWin",
                            WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, hInst, this);
    if (!hwnd_) return false;
    taskbarCreatedMsg_ = RegisterWindowMessageW(L"TaskbarCreated");
    reloadMsg_ = RegisterWindowMessageW(MACWIN_RELOAD_MESSAGE);
    quitMsg_ = RegisterWindowMessageW(MACWIN_QUIT_MESSAGE);
    cornersMsg_ = RegisterWindowMessageW(MACWIN_CORNERS_MESSAGE);
    updateMsg_ = RegisterWindowMessageW(kUpdateMessage);
    // Lower-integrity senders (every DLL, the settings app, Explorer's tray callbacks)
    // are blocked by UIPI while we are elevated, unless let through explicitly.
    ChangeWindowMessageFilterEx(hwnd_, WM_COPYDATA, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(hwnd_, WM_APP_TRAY, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(hwnd_, reloadMsg_, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(hwnd_, quitMsg_, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(hwnd_, taskbarCreatedMsg_, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(hwnd_, updateMsg_, MSGFLT_ALLOW, nullptr);

    settings_.SetIpcWindow(hwnd_);
    settings_.Publish();

    if (!renderer_.Start()) {
        i18n::Message(nullptr, i18n::T(L"host.error.renderer"), MB_ICONERROR);
        return false;
    }
    locator_.Start();
    winEvents_.Start({[this](HWND h) {
                          renderer_.Evict(h);
                          locator_.Forget(h);
                      },
                      [this] { locator_.Invalidate(); }});

    if (!injector_.Start(ExeDir() + L"\\MacWinHook64.dll")) {
        i18n::Message(nullptr, i18n::T(L"host.error.hookDll"), MB_ICONERROR);
        return false;
    }
    injector_.BeginPreload();
    SetTimer(hwnd_, kPreloadTimer, 2000, nullptr);
    // 32-bit programs: MacWinHelper32.exe gets MacWinHook32.dll into them.
    helper32_.Start(ExeDir() + L"\\MacWinHelper32.exe", instance, hwnd_, WM_APP_HELPER_EXITED);

    if (!RegisterHotKey(hwnd_, kHotkeyQuit, MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, VK_F12)) {
        Log(L"hotkey Ctrl+Alt+Shift+F12 unavailable: %lu", GetLastError());
    }

    iconOn_ = CreateAppIcon(GetSystemMetrics(SM_CXSMICON), true);
    iconOff_ = CreateAppIcon(GetSystemMetrics(SM_CXSMICON), false);
    AddTrayIcon();

    updater_.Start(hwnd_, WM_APP_UPDATE);
    SetTimer(hwnd_, kUpdateTimer, kFirstUpdateCheckMs, nullptr);
    std::wstring updatedFrom = updater_.UpdatedFrom();
    if (!updatedFrom.empty()) {
        Log(L"updated from %ls", updatedFrom.c_str());
        ShowBalloon(i18n::F(L"host.update.done.title", MACWIN_VERSION_W), i18n::T(L"host.update.done.text"),
                    BalloonAction::ReleaseNotes);
    }
    Log(L"ready");
    return true;
}

void App::Shutdown() {
    updater_.Stop();
    Log(L"shutting down");
    // 1. No new animations from here on.
    settings_.PublishDisabled();
    winEvents_.Stop();
    // 2. Finish in-flight work: pending UIA lookups still dispatch, then every running
    //    genie tells its DLL to reveal / finish.
    locator_.Stop();
    renderer_.Stop();
    // 3. Tell every DLL to remove its hooks and unload itself, then drop the hook.
    if (shutdownEvent_) SetEvent(shutdownEvent_);
    helper32_.Stop();
    injector_.Stop();
    if (trayAdded_) {
        NOTIFYICONDATAW nid = {sizeof(nid)};
        nid.hWnd = hwnd_;
        nid.uID = 1;
        Shell_NotifyIconW(NIM_DELETE, &nid);
        trayAdded_ = false;
    }
    if (aliveMutex_) {
        ReleaseMutex(aliveMutex_);
        CloseHandle(aliveMutex_);
    }
    if (shutdownEvent_) CloseHandle(shutdownEvent_);
    if (iconOn_) DestroyIcon(iconOn_);
    if (iconOff_) DestroyIcon(iconOff_);
    Log(L"bye");
}

void App::AddTrayIcon() {
    NOTIFYICONDATAW nid = {sizeof(nid)};
    nid.hWnd = hwnd_;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_APP_TRAY;
    nid.hIcon = settings_.Get().enabled ? iconOn_ : iconOff_;
    wcsncpy(nid.szTip, i18n::T(settings_.Get().enabled ? L"host.tray.tooltip.on" : L"host.tray.tooltip.off").c_str(),
            ARRAYSIZE(nid.szTip) - 1);
    trayAdded_ = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);
}

void App::UpdateTrayIcon() {
    NOTIFYICONDATAW nid = {sizeof(nid)};
    nid.hWnd = hwnd_;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_TIP;
    nid.hIcon = settings_.Get().enabled ? iconOn_ : iconOff_;
    wcsncpy(nid.szTip, i18n::T(settings_.Get().enabled ? L"host.tray.tooltip.on" : L"host.tray.tooltip.off").c_str(),
            ARRAYSIZE(nid.szTip) - 1);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

// A Windows notification from the tray icon; a click on it does `action`.
void App::ShowBalloon(const std::wstring& title, const std::wstring& text, BalloonAction action) {
    NOTIFYICONDATAW nid = {sizeof(nid)};
    nid.hWnd = hwnd_;
    nid.uID = 1;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = NIIF_INFO | NIIF_RESPECT_QUIET_TIME;
    wcsncpy(nid.szInfoTitle, title.c_str(), ARRAYSIZE(nid.szInfoTitle) - 1);
    wcsncpy(nid.szInfo, text.c_str(), ARRAYSIZE(nid.szInfo) - 1);
    if (Shell_NotifyIconW(NIM_MODIFY, &nid)) balloon_ = action;
}

// ---------------------------------------------------------------------------
// Updates (the logic is in updater.cpp; this is the user's side of it)
// ---------------------------------------------------------------------------

void App::OnUpdateEvent(WPARAM event) {
    if (event == Updater::kInstallerReady) {
        if (installAfterDownload_ || settings_.Get().updateAutoInstall) RunInstaller();
        return;
    }
    if (updater_.Status() != L"available") return;
    if (settings_.Get().updateAutoInstall) {
        installAfterDownload_ = true;
        updater_.Download();
    } else if (updater_.ShouldNotify()) {
        Release r = updater_.Available();
        ShowBalloon(i18n::F(L"host.update.available.title", r.version), i18n::T(L"host.update.available.text"),
                    BalloonAction::UpdateDialog);
    }
}

// "MacWin X is available": [Install now] [What's new] [Later].
void App::ShowUpdateDialog() {
    Release r = updater_.Available();
    if (r.version.empty()) return;
    std::wstring heading = i18n::F(L"host.update.dialog.heading", r.version);
    std::wstring text = i18n::F(L"host.update.dialog.text", MACWIN_VERSION_W);
    const int kInstall = 100, kNotes = 101, kLater = 102;
    TASKDIALOG_BUTTON buttons[] = {
        {kInstall, i18n::T(L"host.update.dialog.install").c_str()},
        {kNotes, i18n::T(L"host.update.dialog.whatsNew").c_str()},
        {kLater, i18n::T(L"host.update.dialog.later").c_str()},
    };
    TASKDIALOGCONFIG tdc = {sizeof(tdc)};
    tdc.hwndParent = nullptr;
    tdc.hInstance = hInst_;
    tdc.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION |
                  (i18n::IsRightToLeft() ? TDF_RTL_LAYOUT : 0);
    tdc.pszWindowTitle = i18n::T(L"host.update.dialog.title").c_str();
    tdc.hMainIcon = iconOn_;
    tdc.dwFlags |= TDF_USE_HICON_MAIN;
    tdc.pszMainInstruction = heading.c_str();
    tdc.pszContent = text.c_str();
    tdc.cButtons = ARRAYSIZE(buttons);
    tdc.pButtons = buttons;
    tdc.nDefaultButton = kInstall;
    int pressed = 0;
    SetForegroundWindow(hwnd_);
    if (FAILED(TaskDialogIndirect(&tdc, &pressed, nullptr, nullptr))) return;
    if (pressed == kInstall) StartUpdate();
    else if (pressed == kNotes) OpenAsUser(r.notesUrl.empty() ? kReleasesUrl : r.notesUrl);
}

void App::StartUpdate() {
    std::wstring status = updater_.Status();
    if (status != L"available" && status != L"failedInstall") return;
    installAfterDownload_ = true;
    updater_.Download();
}

// Runs the verified installer. It stops this MacWin, replaces the files and starts the
// new version itself (silent: a progress window only; fully automatic: nothing at all).
void App::RunInstaller() {
    installAfterDownload_ = false;
    std::wstring path = updater_.InstallerPath();
    if (path.empty()) return;
    std::wstring args = settings_.Get().updateAutoInstall ? L"/VERYSILENT /SUPPRESSMSGBOXES /NORESTART"
                                                          : L"/SILENT /SUPPRESSMSGBOXES /NORESTART";
    SHELLEXECUTEINFOW sei = {sizeof(sei)};
    sei.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = elevated_ ? L"open" : L"runas";   // the installer needs administrator rights
    sei.lpFile = path.c_str();
    sei.lpParameters = args.c_str();
    sei.nShow = SW_SHOWNORMAL;
    updater_.SetInstalling();
    if (ShellExecuteExW(&sei)) {
        Log(L"update: installer started");
        return;
    }
    DWORD err = GetLastError();
    Log(L"update: installer did not start: %lu", err);
    updater_.SetFailed();
    if (err != ERROR_CANCELLED) i18n::Message(nullptr, i18n::T(L"host.update.failed"), MB_ICONWARNING);
}

void App::ApplySettings(bool save) {
    if (save) settings_.Save();
    settings_.Publish();
    UpdateTrayIcon();
    RefreshWindowCorners();
}

// The DLLs cut a window's corners when it moves or resizes; after a settings change every
// window is asked to do it now (or to drop the corners). Windows without the DLL ignore it.
void App::RefreshWindowCorners() {
    if (!cornersMsg_) return;
    EnumWindows(
        [](HWND h, LPARAM lp) -> BOOL {
            if (IsWindowVisible(h)) PostMessageW(h, (UINT)lp, 0, 0);
            return TRUE;
        },
        (LPARAM)cornersMsg_);
}

// Opens a program or file as the signed-in user. While we are elevated, a plain
// ShellExecute would start it elevated too; Explorer starts it with its own token.
void App::OpenAsUser(const std::wstring& path) {
    if (elevated_) {
        wchar_t windir[MAX_PATH];
        UINT n = GetWindowsDirectoryW(windir, MAX_PATH);
        std::wstring explorer = std::wstring(windir, n) + L"\\explorer.exe";
        std::wstring arg = L"\"" + path + L"\"";
        ShellExecuteW(nullptr, L"open", explorer.c_str(), arg.c_str(), nullptr, SW_SHOWNORMAL);
    } else {
        ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}

void App::OpenSettingsApp() {
    std::wstring exe = ExeDir() + L"\\MacWin.Settings.exe";
    // No settings app next to us (e.g. a bare build): fall back to the file itself.
    OpenAsUser(GetFileAttributesW(exe.c_str()) != INVALID_FILE_ATTRIBUTES ? exe : settings_.Path());
}

// The settings app saved settings.json. A change to where the DLL may load also
// re-runs the preload, so programs that are already running get it right away.
void App::ReloadSettings() {
    HostSettings before = settings_.Get();
    settings_.Load();
    const HostSettings& after = settings_.Get();
    if (after.language != before.language) i18n::Init(after.language);
    ApplySettings(false);
    if (after.updateCheck && !before.updateCheck) updater_.Check(false);
    if (before.devMode != after.devMode || before.devAllowlist != after.devAllowlist ||
        before.noInject != after.noInject) {
        injector_.BeginPreload();
        SetTimer(hwnd_, kPreloadTimer, 2000, nullptr);
        helper32_.Rescan();
    }
}

void App::ShowMenu() {
    const HostSettings& s = settings_.Get();
    HMENU menu = CreatePopupMenu();
    // Right-to-left languages: items aligned and read from the right.
    const UINT rtl = i18n::IsRightToLeft() ? MFT_RIGHTORDER | MFT_RIGHTJUSTIFY : 0;
    AppendMenuW(menu, MF_STRING | rtl, ID_SETTINGS, i18n::T(L"host.menu.settings").c_str());
    SetMenuDefaultItem(menu, ID_SETTINGS, FALSE);
    std::wstring status = updater_.Status();
    if (status == L"available" || status == L"failedInstall") {
        std::wstring update = i18n::F(L"host.menu.update", updater_.Available().version);
        AppendMenuW(menu, MF_STRING | rtl, ID_UPDATE, update.c_str());
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | rtl | (s.enabled ? MF_CHECKED : 0), ID_ENABLED, i18n::T(L"host.menu.enabled").c_str());
    AppendMenuW(menu, MF_STRING | rtl | (s.devMode ? MF_CHECKED : 0), ID_DEVMODE, i18n::T(L"host.menu.testMode").c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | rtl, ID_OPEN_LOG, i18n::T(L"host.menu.openLog").c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    std::wstring quit = i18n::T(L"host.menu.quit") + L"\tCtrl+Alt+Shift+F12";
    AppendMenuW(menu, MF_STRING | rtl, ID_QUIT, quit.c_str());

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd_);   // required so the menu closes when clicking elsewhere
    UINT flags = TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY | (i18n::IsRightToLeft() ? TPM_LAYOUTRTL : 0);
    UINT cmd = TrackPopupMenu(menu, flags, pt.x, pt.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    PostMessageW(hwnd_, WM_NULL, 0, 0);

    HostSettings& m = settings_.Get();
    switch (cmd) {
        case ID_SETTINGS:
            OpenSettingsApp();
            break;
        case ID_ENABLED:
            m.enabled = !m.enabled;
            ApplySettings(true);
            break;
        case ID_DEVMODE:
            m.devMode = !m.devMode;
            ApplySettings(true);
            // The DLL decides at load time where it may stay; programs that were
            // already running (and refused it) pick it up once they are restarted.
            injector_.BeginPreload();
            SetTimer(hwnd_, kPreloadTimer, 2000, nullptr);
            helper32_.Rescan();
            break;
        case ID_OPEN_LOG:
            OpenAsUser(LogPath());
            break;
        case ID_UPDATE:
            ShowUpdateDialog();
            break;
        case ID_QUIT:
            DestroyWindow(hwnd_);
            break;
    }
}

LRESULT CALLBACK App::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = (CREATESTRUCTW*)lp;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        ((App*)cs->lpCreateParams)->hwnd_ = hwnd;
    }
    App* app = (App*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    return app ? app->Handle(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT App::Handle(UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == taskbarCreatedMsg_ && msg) {
        // Explorer restarted: new tray, new taskbar buttons.
        AddTrayIcon();
        locator_.Invalidate();
        return 0;
    }
    if (msg == reloadMsg_ && msg) {
        ReloadSettings();
        return 0;
    }
    if (msg == quitMsg_ && msg) {
        Log(L"quit requested");
        DestroyWindow(hwnd_);
        return 0;
    }
    if (msg == updateMsg_ && msg) {
        if (wp == 1) updater_.Check(true);
        else if (wp == 2) StartUpdate();
        return 0;
    }
    switch (msg) {
        case WM_COPYDATA:
            return ipc_.Handle((const COPYDATASTRUCT*)lp);
        case WM_APP_HELPER_EXITED:
            helper32_.OnExited();
            return 0;
        case WM_APP_UPDATE:
            OnUpdateEvent(wp);
            return 0;
        case WM_APP_TRAY:
            switch (LOWORD(lp)) {
                case NIN_SELECT:
                case NIN_KEYSELECT:
                    OpenSettingsApp();   // left click: the settings window
                    break;
                case WM_CONTEXTMENU:
                    ShowMenu();          // right click: the quick menu
                    break;
                case NIN_BALLOONUSERCLICK: {
                    BalloonAction action = balloon_;
                    balloon_ = BalloonAction::None;
                    if (action == BalloonAction::UpdateDialog) ShowUpdateDialog();
                    else if (action == BalloonAction::ReleaseNotes) OpenAsUser(kReleasesUrl);
                    break;
                }
                case NIN_BALLOONTIMEOUT:
                    balloon_ = BalloonAction::None;
                    break;
            }
            return 0;
        case WM_TIMER:
            if (wp == kPreloadTimer) {
                KillTimer(hwnd_, kPreloadTimer);
                injector_.EndPreload();
            } else if (wp == kUpdateTimer) {
                SetTimer(hwnd_, kUpdateTimer, kUpdateCheckEveryMs, nullptr);
                if (settings_.Get().updateCheck) updater_.Check(false);
            }
            return 0;
        case WM_HOTKEY:
            if (wp == kHotkeyQuit) {
                Log(L"emergency hotkey: quitting");
                DestroyWindow(hwnd_);
            }
            return 0;
        case WM_SETTINGCHANGE:
        case WM_DISPLAYCHANGE:
        case WM_DPICHANGED:
            locator_.Invalidate();
            break;
        case WM_QUERYENDSESSION:
            return TRUE;
        case WM_ENDSESSION:
            if (wp) DestroyWindow(hwnd_);
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd_);
            return 0;
        case WM_DESTROY:
            UnregisterHotKey(hwnd_, kHotkeyQuit);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

// --quit: asks a running instance (possibly elevated) to shut down and waits for it.
int QuitRunning() {
    HWND running = FindWindowW(MACWIN_HOST_WNDCLASS, nullptr);
    if (!running) return 0;
    DWORD pid = 0;
    GetWindowThreadProcessId(running, &pid);
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid);   // may be refused for an elevated host
    PostMessageW(running, RegisterWindowMessageW(MACWIN_QUIT_MESSAGE), 0, 0);
    if (h) {
        WaitForSingleObject(h, 15000);
        CloseHandle(h);
    } else {
        for (int i = 0; i < 150 && IsWindow(running); ++i) Sleep(100);
        Sleep(500);   // the process finishes its shutdown right after the window goes
    }
    return 0;
}

// --configure key=value ...: edits settings.json (used by the installer), and tells a
// running instance to reload it.
int Configure(const std::vector<std::wstring>& args) {
    SettingsStore store;
    store.InitPath();
    store.Load();
    HostSettings& s = store.Get();
    for (const std::wstring& a : args) {
        size_t eq = a.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring key = a.substr(0, eq);
        bool on = a.substr(eq + 1) == L"1" || a.substr(eq + 1) == L"true";
        if (key == L"devMode") s.devMode = on;
        else if (key == L"enabled") s.enabled = on;
    }
    if (!store.Save()) return 1;
    if (HWND running = FindWindowW(MACWIN_HOST_WNDCLASS, nullptr)) {
        PostMessageW(running, RegisterWindowMessageW(MACWIN_RELOAD_MESSAGE), 0, 0);
    }
    return 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    // DLLs only from System32 and next to MacWin.exe: nothing planted in the current
    // directory or on PATH is ever loaded.
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_APPLICATION_DIR);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    // The main thread reads windows' AppUserModelIDs (shell property stores): COM.
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    std::vector<std::wstring> args;
    int argc = 0;
    if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        for (int i = 1; i < argc; ++i) args.push_back(argv[i]);
        LocalFree(argv);
    }
    auto has = [&](const wchar_t* flag) {
        for (const std::wstring& a : args) {
            if (a == flag) return true;
        }
        return false;
    };

    if (has(L"--quit")) return QuitRunning();
    if (has(L"--install-task")) return InstallTask(!has(L"--no-logon"));
    if (has(L"--remove-task")) return RemoveTask();
    if (has(L"--configure")) return Configure(args);

    const bool fromTask = has(L"--task");
    const bool elevated = IsElevated();
    // The installed copy, started normally (Start menu, settings app): hand over to the
    // scheduled task, which runs us elevated without a UAC prompt.
    if (!elevated && !fromTask && IsInSecureLocation() && TaskExists() && RunTask()) return 0;
    if (elevated && !IsInSecureLocation()) {
        SettingsStore store;
        store.InitPath();
        store.Load();
        i18n::Init(store.Get().language);
        i18n::Message(nullptr, i18n::T(L"host.error.elevatedOutside"), MB_ICONWARNING);
        return 1;
    }

    // One instance per session. An elevated instance's mutex can't even be opened from
    // a standard one (access denied), which means the same.
    HANDLE single = CreateMutexW(nullptr, TRUE, MACWIN_SINGLE_INSTANCE);
    DWORD singleErr = GetLastError();
    if (!single || singleErr == ERROR_ALREADY_EXISTS || FindWindowW(MACWIN_HOST_WNDCLASS, nullptr)) {
        if (single) CloseHandle(single);
        if (fromTask) return 0;
        // Started again (Start menu, a desktop shortcut): show the settings instead.
        wchar_t exe[MAX_PATH];
        DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring dir(exe, n);
        dir = dir.substr(0, dir.find_last_of(L'\\'));
        std::wstring settingsApp = dir + L"\\MacWin.Settings.exe";
        if (GetFileAttributesW(settingsApp.c_str()) == INVALID_FILE_ATTRIBUTES ||
            (INT_PTR)ShellExecuteW(nullptr, L"open", settingsApp.c_str(), nullptr, nullptr, SW_SHOWNORMAL) <= 32) {
            SettingsStore store;
            store.InitPath();
            store.Load();
            i18n::Init(store.Get().language);
            i18n::Message(nullptr, i18n::T(L"host.alreadyRunning"), MB_ICONINFORMATION);
        }
        return 0;
    }

    LogInit();
    InstallCrashHandler();
    App app;
    int code = 0;
    if (app.Init(hInst)) {
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    } else {
        code = 1;
    }
    app.Shutdown();
    LogClose();
    if (single) CloseHandle(single);
    return code;
}
