// MacWin host: tray icon, settings, hook installation, IPC and the GPU renderer.

#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include "common/names.h"
#include "host/app_icon.h"
#include "host/elevation.h"
#include "host/helper32.h"
#include "host/injector.h"
#include "host/ipc_server.h"
#include "host/log.h"
#include "host/render/device.h"
#include "host/settings_store.h"
#include "host/taskbar_locator.h"
#include "host/win_events.h"

namespace {

const UINT WM_APP_TRAY = WM_APP + 1;
const UINT WM_APP_HELPER_EXITED = WM_APP + 3;
const UINT_PTR kPreloadTimer = 1;
const int kHotkeyQuit = 1;

enum MenuId : UINT {
    ID_SETTINGS = 100,
    ID_ENABLED,
    ID_DEVMODE,
    ID_OPEN_LOG,
    ID_QUIT,
};

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
    void ApplySettings(bool save);
    void ReloadSettings();
    void OpenSettingsApp();
    void OpenAsUser(const std::wstring& path);
    std::wstring ExeDir() const;

    HINSTANCE hInst_ = nullptr;
    HWND hwnd_ = nullptr;
    UINT taskbarCreatedMsg_ = 0;
    UINT reloadMsg_ = 0;
    UINT quitMsg_ = 0;
    bool elevated_ = false;
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
    Log(L"MacWin starting (pid %lu, instance %08X, %ls)", GetCurrentProcessId(), instance,
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
        MessageBoxW(nullptr, L"MacWin nie może utworzyć ustawień współdzielonych. Jeśli przed chwilą działał "
                             L"z uprawnieniami administratora, wyloguj się i zaloguj ponownie.",
                    L"MacWin", MB_ICONERROR);
        return false;
    }
    settings_.Load();

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
    // Lower-integrity senders (every DLL, the settings app, Explorer's tray callbacks)
    // are blocked by UIPI while we are elevated, unless let through explicitly.
    ChangeWindowMessageFilterEx(hwnd_, WM_COPYDATA, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(hwnd_, WM_APP_TRAY, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(hwnd_, reloadMsg_, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(hwnd_, quitMsg_, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(hwnd_, taskbarCreatedMsg_, MSGFLT_ALLOW, nullptr);

    settings_.SetIpcWindow(hwnd_);
    settings_.Publish();

    if (!renderer_.Start()) {
        MessageBoxW(nullptr, L"MacWin nie może uruchomić renderera (Direct3D 11 / DirectComposition).\n"
                             L"Szczegóły są w logu.", L"MacWin", MB_ICONERROR);
        return false;
    }
    locator_.Start();
    winEvents_.Start({[this](HWND h) {
                          renderer_.Evict(h);
                          locator_.Forget(h);
                      },
                      [this] { locator_.Invalidate(); }});

    if (!injector_.Start(ExeDir() + L"\\MacWinHook64.dll")) {
        MessageBoxW(nullptr, L"MacWin nie może załadować MacWinHook64.dll. Szczegóły są w logu.", L"MacWin",
                    MB_ICONERROR);
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
    Log(L"ready");
    return true;
}

void App::Shutdown() {
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
    wcscpy(nid.szTip, L"MacWin – animacje okien");
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
    wcscpy(nid.szTip, settings_.Get().enabled ? L"MacWin – animacje okien" : L"MacWin – wyłączony");
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void App::ApplySettings(bool save) {
    if (save) settings_.Save();
    settings_.Publish();
    UpdateTrayIcon();
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
    ApplySettings(false);
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
    AppendMenuW(menu, MF_STRING, ID_SETTINGS, L"Ustawienia…");
    SetMenuDefaultItem(menu, ID_SETTINGS, FALSE);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (s.enabled ? MF_CHECKED : 0), ID_ENABLED, L"Animacje włączone");
    AppendMenuW(menu, MF_STRING | (s.devMode ? MF_CHECKED : 0), ID_DEVMODE,
                L"Tryb testowy (tylko programy z listy)");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_OPEN_LOG, L"Otwórz dziennik");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_QUIT, L"Zakończ\tCtrl+Alt+Shift+F12");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd_);   // required so the menu closes when clicking elsewhere
    UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd_, nullptr);
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
    switch (msg) {
        case WM_COPYDATA:
            return ipc_.Handle((const COPYDATASTRUCT*)lp);
        case WM_APP_HELPER_EXITED:
            helper32_.OnExited();
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
            }
            return 0;
        case WM_TIMER:
            if (wp == kPreloadTimer) {
                KillTimer(hwnd_, kPreloadTimer);
                injector_.EndPreload();
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
        MessageBoxW(nullptr,
                    L"MacWin z uprawnieniami administratora może działać tylko z folderu Program Files "
                    L"(po instalacji). Z tego folderu każdy program mógłby podmienić MacWinHook64.dll "
                    L"i dostać się do programów uruchomionych jako administrator.\n\n"
                    L"Uruchom MacWin bez uprawnień administratora albo zainstaluj go instalatorem.",
                    L"MacWin", MB_ICONWARNING);
        return 1;
    }

    // One instance per session. An elevated instance's mutex can't even be opened from
    // a standard one (access denied), which means the same.
    HANDLE single = CreateMutexW(nullptr, TRUE, MACWIN_SINGLE_INSTANCE);
    DWORD singleErr = GetLastError();
    if (!single || singleErr == ERROR_ALREADY_EXISTS || FindWindowW(MACWIN_HOST_WNDCLASS, nullptr)) {
        if (!fromTask) MessageBoxW(nullptr, L"MacWin już działa (ikona w zasobniku).", L"MacWin", MB_ICONINFORMATION);
        if (single) CloseHandle(single);
        return 0;
    }

    LogInit();
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
