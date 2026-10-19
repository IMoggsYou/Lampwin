#include "host/log.h"
#include <windows.h>
#include <shlobj.h>
#include <stdarg.h>
#include <stdio.h>

namespace {

SRWLOCK g_logLock = SRWLOCK_INIT;
HANDLE g_logFile = INVALID_HANDLE_VALUE;
LARGE_INTEGER g_qpcFreq, g_qpcStart;

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
    return LocalDataDir() + L"\\host.log";
}

void LogInit() {
    QueryPerformanceFrequency(&g_qpcFreq);
    QueryPerformanceCounter(&g_qpcStart);
    g_logFile = CreateFileW(LogPath().c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
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
        DWORD written;
        WriteFile(g_logFile, utf8, (DWORD)(len - 1), &written, nullptr);
    }
    ReleaseSRWLockExclusive(&g_logLock);
    OutputDebugStringW(msg);
}
