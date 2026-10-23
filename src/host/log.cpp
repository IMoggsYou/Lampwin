// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 KosaWLeb. Created by KosaWLeb: https://github.com/KosaWLeb/MacWin

#include "host/log.h"
#include <windows.h>
#include <shlobj.h>
#include <stdarg.h>
#include <stdio.h>

namespace {

SRWLOCK g_logLock = SRWLOCK_INIT;
HANDLE g_logFile = INVALID_HANDLE_VALUE;
LARGE_INTEGER g_qpcFreq, g_qpcStart;
ULONGLONG g_logBytes = 0;
// A log that grows past this starts over (the full one becomes the .old one).
const ULONGLONG kMaxLogBytes = 4ull << 20;

std::wstring OldLogPath() {
    std::wstring path = LogPath();
    return path.substr(0, path.size() - 4) + L".old.log";
}

// The previous log becomes *.old.log (a crash's log survives the restart after it).
void OpenFresh() {
    MoveFileExW(LogPath().c_str(), OldLogPath().c_str(), MOVEFILE_REPLACE_EXISTING);
    g_logFile = CreateFileW(LogPath().c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    g_logBytes = 0;
}

std::wstring KnownDir(REFKNOWNFOLDERID id) {
    PWSTR path = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &path)) && path) {
        out = std::wstring(path) + L"\\MacWin";
        CreateDirectoryW(out.c_str(), nullptr);
    }
    CoTaskMemFree(path);
    return out;
}

}  // namespace

std::wstring RoamingDataDir() {
    static std::wstring dir = KnownDir(FOLDERID_RoamingAppData);
    return dir;
}

std::wstring LocalDataDir() {
    static std::wstring dir = KnownDir(FOLDERID_LocalAppData);
    return dir;
}

std::wstring LogPath() {
#ifdef MACWIN_DEV_NAMES
    return LocalDataDir() + L"\\host-dev.log";
#else
    return LocalDataDir() + L"\\host.log";
#endif
}

void LogInit() {
    QueryPerformanceFrequency(&g_qpcFreq);
    QueryPerformanceCounter(&g_qpcStart);
    AcquireSRWLockExclusive(&g_logLock);
    OpenFresh();
    ReleaseSRWLockExclusive(&g_logLock);
}

void LogClose() {
    AcquireSRWLockExclusive(&g_logLock);
    if (g_logFile != INVALID_HANDLE_VALUE) CloseHandle(g_logFile);
    g_logFile = INVALID_HANDLE_VALUE;
    ReleaseSRWLockExclusive(&g_logLock);
}

void Log(const wchar_t* fmt, ...) {
    wchar_t msg[1024];
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    double ms = (double)(now.QuadPart - g_qpcStart.QuadPart) * 1000.0 / (double)g_qpcFreq.QuadPart;
    SYSTEMTIME st;
    GetLocalTime(&st);
    int n = swprintf(msg, 1024, L"%02u:%02u:%02u.%03u [%9.1f] ", st.wHour, st.wMinute, st.wSecond,
                     st.wMilliseconds, ms);
    if (n < 0) n = 0;
    va_list ap;
    va_start(ap, fmt);
    vswprintf(msg + n, 1024 - n - 3, fmt, ap);
    va_end(ap);
    wcscat(msg, L"\r\n");

    char utf8[3072];
    int len = WideCharToMultiByte(CP_UTF8, 0, msg, -1, utf8, sizeof(utf8), nullptr, nullptr);
    AcquireSRWLockExclusive(&g_logLock);
    if (g_logFile != INVALID_HANDLE_VALUE && len > 1) {
        if (g_logBytes > kMaxLogBytes) {
            CloseHandle(g_logFile);
            OpenFresh();
        }
        DWORD written = 0;
        if (g_logFile != INVALID_HANDLE_VALUE) WriteFile(g_logFile, utf8, (DWORD)(len - 1), &written, nullptr);
        g_logBytes += written;
    }
    ReleaseSRWLockExclusive(&g_logLock);
    OutputDebugStringW(msg);
}
