#include "host/crash.h"
#include <windows.h>
#include <dbghelp.h>
#include <stdio.h>
#include <algorithm>
#include <exception>
#include <string>
#include <vector>
#include "host/log.h"
#include "version.h"

namespace {

wchar_t g_dumpDir[MAX_PATH];
volatile LONG g_crashing = 0;

typedef BOOL(WINAPI* MiniDumpWriteDump_t)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
                                          PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);

// Runs on the crashing thread with the process in an unknown state: no heap, no locks
// it does not need, nothing that can wait on another thread.
LONG WINAPI OnCrash(EXCEPTION_POINTERS* info) {
    if (InterlockedExchange(&g_crashing, 1)) return EXCEPTION_EXECUTE_HANDLER;
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t path[MAX_PATH + 64];
    swprintf(path, ARRAYSIZE(path), L"%ls\\MacWin-%ls-%04u%02u%02u-%02u%02u%02u.dmp", g_dumpDir, MACWIN_VERSION_W,
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    const DWORD code = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0;
    const void* at = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionAddress : nullptr;

    bool written = false;
    if (HMODULE dbghelp = LoadLibraryExW(L"dbghelp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)) {
        auto write = (MiniDumpWriteDump_t)(void*)GetProcAddress(dbghelp, "MiniDumpWriteDump");
        HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (write && file != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION mei = {GetCurrentThreadId(), info, FALSE};
            written = write(GetCurrentProcess(), GetCurrentProcessId(), file,
                            (MINIDUMP_TYPE)(MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules),
                            info ? &mei : nullptr, nullptr, nullptr) != FALSE;
        }
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    }
    // No Log() here: the crash may have happened inside it, holding its lock. The dump
    // has the details; the next start mentions it in the log.
    wchar_t line[128];
    swprintf(line, ARRAYSIZE(line), L"MacWin crashed: exception 0x%08lX at %p (dump %ls)\n", code, at,
             written ? L"written" : L"failed");
    OutputDebugStringW(line);
    return EXCEPTION_EXECUTE_HANDLER;   // ends the process
}

// The newest five dumps stay; older ones go.
void PruneDumps() {
    std::vector<std::pair<FILETIME, std::wstring>> dumps;
    WIN32_FIND_DATAW fd;
    std::wstring pattern = std::wstring(g_dumpDir) + L"\\MacWin-*.dmp";
    HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        dumps.emplace_back(fd.ftLastWriteTime, fd.cFileName);
    } while (FindNextFileW(find, &fd));
    FindClose(find);
    std::sort(dumps.begin(), dumps.end(), [](const auto& a, const auto& b) {
        return CompareFileTime(&a.first, &b.first) > 0;
    });
    // A crash in the last hour (this start is probably the task restarting MacWin).
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER n{{now.dwLowDateTime, now.dwHighDateTime}};
    ULARGE_INTEGER d{{dumps[0].first.dwLowDateTime, dumps[0].first.dwHighDateTime}};
    if (n.QuadPart - d.QuadPart < 3600ull * 10000000ull) {
        Log(L"the previous run crashed; dump: %ls\\%ls", g_dumpDir, dumps[0].second.c_str());
    }
    for (size_t i = 5; i < dumps.size(); ++i) {
        DeleteFileW((std::wstring(g_dumpDir) + L"\\" + dumps[i].second).c_str());
    }
}

}  // namespace

void InstallCrashHandler() {
    std::wstring dir = LocalDataDir() + L"\\crashes";
    CreateDirectoryW(dir.c_str(), nullptr);
    wcsncpy(g_dumpDir, dir.c_str(), MAX_PATH - 1);
    PruneDumps();
    SetUnhandledExceptionFilter(OnCrash);
    // std::terminate (an uncaught C++ exception, e.g. out of memory) gets a dump too.
    std::set_terminate([] {
        RaiseException(0xE0000001, EXCEPTION_NONCONTINUABLE, 0, nullptr);
        TerminateProcess(GetCurrentProcess(), 3);
    });
}
