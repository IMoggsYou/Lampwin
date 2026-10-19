#pragma once
#include <windows.h>
#include <deque>
#include <functional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// Finds the taskbar button of a window (Potassiumuncher's UI Automation matching from
// the mod), without running UI Automation on the app's UI thread at minimize time:
//  - the taskbar's buttons (name + rect) are cached and refreshed on a background MTA
//    thread whenever taskbar-relevant windows appear / disappear (debounced),
//  - a minimize scores the window against the cached list (microseconds); only a
//    cold or weak match waits for a fresh UIA scan.
class TaskbarLocator {
public:
    bool Start();
    void Stop();

    // Instant answer from the cached list; false if it is stale or the match is weak.
    bool TryResolve(HWND hwnd, HMONITOR hMon, int* x);
    // Refreshes the list if needed, then calls done(x) on the locator thread.
    void ResolveAsync(HWND hwnd, HMONITOR hMon, int fallbackX, std::function<void(int)> done);
    // The user clicked this window's own taskbar button (cursor over the taskbar).
    void Learn(HWND hwnd, int x);
    // Per-window, then per-process learned position, else defaultX.
    int Fallback(HWND hwnd, HMONITOR hMon, int defaultX);
    void Invalidate();
    void Forget(HWND hwnd);

private:
    struct Button {
        std::wstring name;   // lowercase
        RECT rc;
        HWND tray;
    };
    struct WinInfo {
        std::wstring title, proc, hint, processKey;
    };
    struct Request {
        HWND hwnd;
        HMONITOR hMon;
        int fallbackX;
        std::function<void(int)> done;
    };

    void ThreadMain();
    bool InitUia();
    void ReleaseUia();
    void Refresh();
    WinInfo Describe(HWND hwnd, HMONITOR hMon) const;
    bool MatchLocked(const WinInfo& w, HMONITOR hMon, int* x, int* score) const;
    void LearnMatchLocked(HWND hwnd, const WinInfo& w, int x);
    int FallbackLocked(HWND hwnd, const WinInfo& w, int defaultX) const;
    void Resolve(Request& r, bool* refreshed);

    std::thread thread_;
    HANDLE wake_ = nullptr;
    volatile bool stop_ = false;

    mutable SRWLOCK lock_ = SRWLOCK_INIT;
    std::vector<Button> buttons_;
    bool valid_ = false;
    volatile bool dirty_ = true;
    ULONGLONG refreshDue_ = 0;   // GetTickCount64 of the debounced refresh, 0 = none
    std::unordered_map<HWND, int> perWindow_;
    std::unordered_map<std::wstring, int> perProcess_;
    std::deque<Request> queue_;

    // UIA objects (locator thread only).
    void* uia_ = nullptr;
    void* condition_ = nullptr;
    void* cacheRequest_ = nullptr;
};
