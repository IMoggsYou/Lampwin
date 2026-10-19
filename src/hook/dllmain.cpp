#include <windows.h>
#include "common/names.h"
#include "common/strutil.h"
#include "hook/runtime.h"

HMODULE g_hModule = nullptr;

// Runs under the loader lock: kernel32/ntdll only. Decides whether this process may
// keep the DLL at all - refusing here unmaps it again, so programs on the "do not
// inject" list (and, in test mode, everything outside the allowlist) never have
// MacWin code in them.
static bool ShouldLoadIntoThisProcess(bool* isHost, bool* alwaysAllowed) {
    wchar_t path[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (!len || len >= MAX_PATH) return false;
    mw::ImageNames self;
    if (!mw::MakeImageNames(path, &self)) return false;

    // The host loads the DLL itself (to hand its hook procedures to Windows); it
    // keeps it, but never activates anything in it.
    if (wcscmp(self.base, MACWIN_HOST_EXE) == 0 || wcscmp(self.base, MACWIN_HELPER32_EXE) == 0) {
        *isHost = true;
        return true;
    }
    // Our own settings app: always animated (even in test mode), never excluded.
    if (wcscmp(self.base, MACWIN_SETTINGS_EXE) == 0) {
        *alwaysAllowed = true;
        return true;
    }

    HANDLE hMap = OpenFileMappingW(FILE_MAP_READ, FALSE, MACWIN_SETTINGS_SECTION);
    if (!hMap) return false;
    auto* s = (const SharedSettings*)MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, sizeof(SharedSettings));
    bool allow = false;
    if (s && s->magic == MACWIN_SETTINGS_MAGIC && s->size == sizeof(SharedSettings)) {
        static wchar_t list[MACWIN_LIST_CHARS];   // DllMain calls are serialized
        SettingsCopyList(s, s->noInject, list, MACWIN_LIST_CHARS);
        if (!mw::ListMatches(list, self)) {
            if (s->devMode) {
                SettingsCopyList(s, s->devAllowlist, list, MACWIN_LIST_CHARS);
                allow = mw::ListMatches(list, self);
            } else {
                allow = true;
            }
        }
    }
    if (s) UnmapViewOfFile(s);
    CloseHandle(hMap);
    return allow;
}

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hModule = hInst;
        DisableThreadLibraryCalls(hInst);
        bool isHost = false, alwaysAllowed = false;
        if (!ShouldLoadIntoThisProcess(&isHost, &alwaysAllowed)) return FALSE;
        LoaderSetHostProcess(isHost);
        LoaderSetAlwaysAllowed(alwaysAllowed);
    } else if (reason == DLL_PROCESS_DETACH && !reserved) {
        LoaderOnProcessDetach();
    }
    return TRUE;
}

// Windows calls these in every GUI thread of the session. They are only the vehicle
// that gets the DLL loaded; the real work is done by the inline hooks EnsureInit
// installs. WH_CBT stays installed (it fires rarely: window create / activate /
// min-max); WH_GETMESSAGE is installed for a moment at host start-up so programs
// that were already running pick the DLL up right away.
extern "C" __declspec(dllexport) LRESULT CALLBACK MacWinCbtProc(int code, WPARAM wParam, LPARAM lParam) {
    EnsureInit();
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

extern "C" __declspec(dllexport) LRESULT CALLBACK MacWinGetMsgProc(int code, WPARAM wParam, LPARAM lParam) {
    EnsureInit();
    return CallNextHookEx(nullptr, code, wParam, lParam);
}
