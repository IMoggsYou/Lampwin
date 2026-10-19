#include "host/win_events.h"
#include <unordered_set>

namespace {

WinEvents::Callbacks g_cb;
std::unordered_set<HWND> g_taskbarWindows;   // windows that (probably) have a button

// Roughly the shell's own rule for getting a taskbar button.
bool HasTaskbarButton(HWND hwnd) {
    if (GetAncestor(hwnd, GA_ROOT) != hwnd) return false;
    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (ex & WS_EX_TOOLWINDOW) return false;
    if (ex & WS_EX_APPWINDOW) return true;
    return GetWindow(hwnd, GW_OWNER) == nullptr;
}

}  // namespace

bool WinEvents::Start(Callbacks cb) {
    g_cb = std::move(cb);
    // EVENT_OBJECT_DESTROY (0x8001) .. EVENT_OBJECT_HIDE (0x8003) covers show as well.
    hook_ = SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_HIDE, nullptr, Proc, 0, 0,
                            WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    return hook_ != nullptr;
}

void WinEvents::Stop() {
    if (hook_) UnhookWinEvent(hook_);
    hook_ = nullptr;
    g_taskbarWindows.clear();
}

void CALLBACK WinEvents::Proc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD) {
    if (!hwnd || idObject != OBJID_WINDOW || idChild != CHILDID_SELF) return;
    if (event == EVENT_OBJECT_DESTROY) {
        if (g_cb.destroyed) g_cb.destroyed(hwnd);
        if (g_taskbarWindows.erase(hwnd) && g_cb.taskbarChanged) g_cb.taskbarChanged();
        return;
    }
    if (!HasTaskbarButton(hwnd)) return;
    if (event == EVENT_OBJECT_SHOW) g_taskbarWindows.insert(hwnd);
    if (g_cb.taskbarChanged) g_cb.taskbarChanged();
}
