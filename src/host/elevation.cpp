// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/MacWin

#include "host/elevation.h"
#include <sddl.h>
#include <shlobj.h>
#include <wtsapi32.h>
#include <wctype.h>
#include <vector>
#include "common/names.h"

namespace {

std::wstring Lower(std::wstring s) {
    for (wchar_t& c : s) c = (wchar_t)towlower(c);
    return s;
}

std::wstring ExePath() {
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    return std::wstring(path, n);
}

std::wstring SystemTool(const wchar_t* name) {
    wchar_t dir[MAX_PATH];
    UINT n = GetSystemDirectoryW(dir, MAX_PATH);
    return std::wstring(dir, n) + L"\\" + name;
}

// Runs a system tool without a console window and returns its exit code.
int RunHidden(const std::wstring& exe, const std::wstring& args, DWORD timeoutMs = 20000) {
    std::wstring cmd = L"\"" + exe + L"\" " + args;
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(exe.c_str(), buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si,
                        &pi)) {
        return -1;
    }
    DWORD code = (DWORD)-1;
    if (WaitForSingleObject(pi.hProcess, timeoutMs) == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (int)code;
}

std::wstring XmlEscape(const std::wstring& s) {
    std::wstring out;
    for (wchar_t c : s) {
        switch (c) {
            case L'&': out += L"&amp;"; break;
            case L'<': out += L"&lt;"; break;
            case L'>': out += L"&gt;"; break;
            case L'"': out += L"&quot;"; break;
            default: out.push_back(c);
        }
    }
    return out;
}

}  // namespace

bool RunAsShellUser(const std::wstring& exe, const std::wstring& args) {
    DWORD pid = 0;
    HWND shell = GetShellWindow();
    if (!shell || !GetWindowThreadProcessId(shell, &pid) || !pid) return false;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    HANDLE token = nullptr, primary = nullptr;
    bool ok = process && OpenProcessToken(process, TOKEN_DUPLICATE, &token) &&
              DuplicateTokenEx(token, TOKEN_QUERY | TOKEN_ASSIGN_PRIMARY | TOKEN_DUPLICATE | TOKEN_ADJUST_DEFAULT |
                                          TOKEN_ADJUST_SESSIONID,
                               nullptr, SecurityImpersonation, TokenPrimary, &primary);
    if (ok) {
        std::wstring cmd = L"\"" + exe + L"\" " + args;
        std::vector<wchar_t> buf(cmd.begin(), cmd.end());
        buf.push_back(0);
        STARTUPINFOW si = {sizeof(si)};
        PROCESS_INFORMATION pi = {};
        ok = CreateProcessWithTokenW(primary, 0, exe.c_str(), buf.data(), 0, nullptr, nullptr, &si, &pi) != FALSE;
        if (ok) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
    }
    if (primary) CloseHandle(primary);
    if (token) CloseHandle(token);
    if (process) CloseHandle(process);
    return ok;
}

bool IsElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elevation = {};
    DWORD size = 0;
    bool elevated = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) &&
                    elevation.TokenIsElevated;
    CloseHandle(token);
    return elevated;
}

bool IsInSecureLocation() {
    std::wstring exe = Lower(ExePath());
    const KNOWNFOLDERID* folders[] = {&FOLDERID_ProgramFiles, &FOLDERID_ProgramFilesX86};
    for (const KNOWNFOLDERID* id : folders) {
        PWSTR path = nullptr;
        bool match = false;
        if (SUCCEEDED(SHGetKnownFolderPath(*id, 0, nullptr, &path)) && path) {
            std::wstring prefix = Lower(path) + L"\\";
            match = exe.compare(0, prefix.size(), prefix) == 0;
        }
        CoTaskMemFree(path);
        if (match) return true;
    }
    return false;
}

std::wstring CurrentUserSid() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return L"";
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<BYTE> buf(size ? size : 1);
    std::wstring sid;
    if (size && GetTokenInformation(token, TokenUser, buf.data(), size, &size)) {
        LPWSTR str = nullptr;
        if (ConvertSidToStringSidW(((TOKEN_USER*)buf.data())->User.Sid, &str)) {
            sid = str;
            LocalFree(str);
        }
    }
    CloseHandle(token);
    return sid;
}

// The user signed in to this session. Usually the same as CurrentUserSid(); not when
// an administrator's password elevated the installer for a standard user ("over the
// shoulder"): the task then must be the signed-in user's, not the administrator's.
std::wstring SessionUserSid() {
    std::wstring sid;
    DWORD session = 0;
    LPWSTR user = nullptr, domain = nullptr;
    DWORD bytes = 0;
    if (ProcessIdToSessionId(GetCurrentProcessId(), &session) &&
        WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, session, WTSUserName, &user, &bytes) &&
        WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, session, WTSDomainName, &domain, &bytes) && user &&
        *user) {
        std::wstring account = (domain && *domain) ? std::wstring(domain) + L"\\" + user : std::wstring(user);
        BYTE sidBuf[SECURITY_MAX_SID_SIZE];
        DWORD sidSize = sizeof(sidBuf);
        wchar_t refDomain[256];
        DWORD refSize = ARRAYSIZE(refDomain);
        SID_NAME_USE use;
        LPWSTR str = nullptr;
        if (LookupAccountNameW(nullptr, account.c_str(), sidBuf, &sidSize, refDomain, &refSize, &use) &&
            ConvertSidToStringSidW(sidBuf, &str)) {
            sid = str;
            LocalFree(str);
        }
    }
    if (user) WTSFreeMemory(user);
    if (domain) WTSFreeMemory(domain);
    return sid.empty() ? CurrentUserSid() : sid;
}

SecurityAttributes::SecurityAttributes(const std::wstring& sddl) {
    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd_, nullptr)) {
        sa_.nLength = sizeof(sa_);
        sa_.lpSecurityDescriptor = sd_;
        sa_.bInheritHandle = FALSE;
    } else {
        sd_ = nullptr;
    }
}

SecurityAttributes::~SecurityAttributes() {
    if (sd_) LocalFree(sd_);
}

std::wstring SharedObjectSddl(SharedObject kind, bool elevated) {
    std::wstring sddl = L"D:(A;;GA;;;SY)(A;;GA;;;BA)";
    std::wstring sid = CurrentUserSid();
    if (!sid.empty()) sddl += L"(A;;GA;;;" + sid + L")";
    switch (kind) {
        case SharedObject::Settings:
            sddl += L"(A;;GR;;;AU)";
            if (elevated) sddl += L"S:(ML;;NW;;;HI)";
            break;
        case SharedObject::ShutdownEvent:
            sddl += L"(A;;0x00100000;;;AU)";   // SYNCHRONIZE
            if (elevated) sddl += L"S:(ML;;NW;;;HI)";
            break;
        case SharedObject::AliveMutex:
            sddl += L"(A;;0x00100001;;;AU)";   // SYNCHRONIZE | MUTEX_MODIFY_STATE
            if (elevated) sddl += L"S:(ML;;NW;;;ME)";
            break;
    }
    return sddl;
}

bool TaskExists() {
    return RunHidden(SystemTool(L"schtasks.exe"), L"/Query /TN \"" MACWIN_TASK_NAME L"\"") == 0;
}

bool RunTask() {
    return RunHidden(SystemTool(L"schtasks.exe"), L"/Run /TN \"" MACWIN_TASK_NAME L"\"") == 0;
}

int InstallTask(bool atLogon) {
    if (!IsElevated()) return ERROR_ELEVATION_REQUIRED;
    if (!IsInSecureLocation()) return ERROR_ACCESS_DENIED;
    std::wstring sid = SessionUserSid();
    if (sid.empty()) return ERROR_INVALID_SID;

    // Interactive, highest privileges, no battery / idle / time limits, normal priority
    // (a task's default priority is "below normal", which would hurt the animation).
    // A crash (non-zero exit) is restarted after a minute, three times at most.
    std::wstring xml =
        L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>\r\n"
        L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\r\n"
        L"  <RegistrationInfo><Author>IMoggsYou</Author><Description>MacWin: macOS-style window animations "
        L"(https://github.com/IMoggsYou/MacWin)</Description></RegistrationInfo>\r\n"
        L"  <Triggers>";
    if (atLogon) xml += L"<LogonTrigger><Enabled>true</Enabled><UserId>" + sid + L"</UserId></LogonTrigger>";
    xml += L"</Triggers>\r\n"
           L"  <Principals><Principal id=\"Author\"><UserId>" + sid +
           L"</UserId><LogonType>InteractiveToken</LogonType><RunLevel>HighestAvailable</RunLevel></Principal></Principals>\r\n"
           L"  <Settings>\r\n"
           L"    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>\r\n"
           L"    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>\r\n"
           L"    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>\r\n"
           L"    <AllowHardTerminate>true</AllowHardTerminate>\r\n"
           L"    <StartWhenAvailable>false</StartWhenAvailable>\r\n"
           L"    <RunOnlyIfNetworkAvailable>false</RunOnlyIfNetworkAvailable>\r\n"
           L"    <IdleSettings><StopOnIdleEnd>false</StopOnIdleEnd><RestartOnIdle>false</RestartOnIdle></IdleSettings>\r\n"
           L"    <AllowStartOnDemand>true</AllowStartOnDemand>\r\n"
           L"    <Enabled>true</Enabled>\r\n"
           L"    <Hidden>false</Hidden>\r\n"
           L"    <RunOnlyIfIdle>false</RunOnlyIfIdle>\r\n"
           L"    <WakeToRun>false</WakeToRun>\r\n"
           L"    <ExecutionTimeLimit>PT0S</ExecutionTimeLimit>\r\n"
           L"    <Priority>4</Priority>\r\n"
           L"    <RestartOnFailure><Interval>PT1M</Interval><Count>3</Count></RestartOnFailure>\r\n"
           L"  </Settings>\r\n"
           L"  <Actions Context=\"Author\"><Exec><Command>" + XmlEscape(ExePath()) +
           L"</Command><Arguments>--task</Arguments></Exec></Actions>\r\n"
           L"</Task>\r\n";

    wchar_t tempDir[MAX_PATH], tempFile[MAX_PATH];
    GetTempPathW(MAX_PATH, tempDir);
    if (!GetTempFileNameW(tempDir, L"mwt", 0, tempFile)) return (int)GetLastError();
    HANDLE f = CreateFileW(tempFile, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (f == INVALID_HANDLE_VALUE) return (int)GetLastError();
    const wchar_t bom = 0xFEFF;
    DWORD written = 0;
    WriteFile(f, &bom, sizeof(bom), &written, nullptr);
    WriteFile(f, xml.data(), (DWORD)(xml.size() * sizeof(wchar_t)), &written, nullptr);
    CloseHandle(f);
    int code = RunHidden(SystemTool(L"schtasks.exe"),
                         L"/Create /TN \"" MACWIN_TASK_NAME L"\" /XML \"" + std::wstring(tempFile) + L"\" /F");
    DeleteFileW(tempFile);
    return code;
}

int RemoveTask() {
    if (!TaskExists()) return 0;
    return RunHidden(SystemTool(L"schtasks.exe"), L"/Delete /TN \"" MACWIN_TASK_NAME L"\" /F");
}
