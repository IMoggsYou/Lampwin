// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/MacWin

#pragma once
#include <windows.h>
#include <string>

// Running elevated, so the genie also reaches programs started as administrator.
//
// An elevated MacWin installs its hook into elevated processes too, which makes the
// location of MacWinHook64.dll security-relevant: it must sit where only administrators
// can write (Program Files), otherwise any program could swap it and get its code run
// inside elevated processes. MacWin therefore refuses to run elevated from anywhere else.

bool IsElevated();
bool IsInSecureLocation();   // this exe lives under Program Files
std::wstring CurrentUserSid();
// The user signed in to this session (differs from CurrentUserSid() when an
// administrator's credentials elevated this process for a standard user).
std::wstring SessionUserSid();

// SDDL -> SECURITY_ATTRIBUTES for the named objects the DLLs open.
class SecurityAttributes {
public:
    explicit SecurityAttributes(const std::wstring& sddl);
    ~SecurityAttributes();
    SecurityAttributes(const SecurityAttributes&) = delete;
    SecurityAttributes& operator=(const SecurityAttributes&) = delete;
    SECURITY_ATTRIBUTES* get() { return sd_ ? &sa_ : nullptr; }

private:
    PSECURITY_DESCRIPTOR sd_ = nullptr;
    SECURITY_ATTRIBUTES sa_ = {};
};

enum class SharedObject { Settings, ShutdownEvent, AliveMutex };
// Read (or wait) access for every program of the session; full access for SYSTEM,
// administrators and this user. When elevated, a High label keeps lower-integrity
// programs from writing - except the alive mutex, which they must be able to release.
std::wstring SharedObjectSddl(SharedObject kind, bool elevated);

// Starts a program as the signed-in user, with arguments, from an elevated MacWin: with
// the token of the shell (Explorer). False if that is not possible.
bool RunAsShellUser(const std::wstring& exe, const std::wstring& args);

// The "MacWin" scheduled task: starts MacWin.exe --task with the highest privileges,
// without a UAC prompt, optionally at every logon.
bool TaskExists();
bool RunTask();
int InstallTask(bool atLogon);   // needs elevation; returns a process exit code
int RemoveTask();
