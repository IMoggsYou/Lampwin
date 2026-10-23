// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/MacWin

#pragma once
#include <windows.h>
#include <wchar.h>
#include <wctype.h>

namespace mw {

inline void LowerInPlace(wchar_t* s) {
    for (; *s; ++s) *s = (wchar_t)towlower(*s);
}

// Lowercase forms of an image path used for list matching (same rules as the mod's
// "Excluded programs": full path, file name, or file name without ".exe").
struct ImageNames {
    wchar_t full[MAX_PATH];
    wchar_t base[MAX_PATH];
    wchar_t baseNoExt[MAX_PATH];
};

inline bool MakeImageNames(const wchar_t* path, ImageNames* out) {
    if (!path || !*path) return false;
    wcsncpy(out->full, path, MAX_PATH - 1);
    out->full[MAX_PATH - 1] = 0;
    LowerInPlace(out->full);

    const wchar_t* base = out->full;
    for (const wchar_t* p = out->full; *p; ++p) {
        if (*p == L'\\' || *p == L'/') base = p + 1;
    }
    wcscpy(out->base, base);
    wcscpy(out->baseNoExt, base);
    size_t n = wcslen(out->baseNoExt);
    if (n > 4 && wcscmp(out->baseNoExt + n - 4, L".exe") == 0) out->baseNoExt[n - 4] = 0;
    return true;
}

inline bool ProcessImageNames(DWORD pid, ImageNames* out) {
    if (!pid) return false;
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return false;
    wchar_t path[MAX_PATH];
    DWORD len = MAX_PATH;
    bool ok = QueryFullProcessImageNameW(hProc, 0, path, &len) != 0;
    CloseHandle(hProc);
    return ok && MakeImageNames(path, out);
}

// `list`: entries separated by L'\0', ended by an empty entry; at least the last two
// characters of the buffer are NUL (SettingsCopyList guarantees it). An entry matches the
// full path, the file name or the file name without ".exe"; an entry with a backslash
// that does not end in ".exe" is a folder pattern, found anywhere in the full path
// (e.g. "\steamapps\common\" matches every program installed there).
inline bool ListMatches(const wchar_t* list, const ImageNames& n) {
    for (const wchar_t* p = list; *p;) {
        const size_t len = wcslen(p);
        if (wcscmp(p, n.full) == 0 || wcscmp(p, n.base) == 0 || wcscmp(p, n.baseNoExt) == 0) {
            return true;
        }
        const bool exe = len >= 4 && wcscmp(p + len - 4, L".exe") == 0;
        if (!exe && wcschr(p, L'\\') && wcsstr(n.full, p)) return true;
        p += len + 1;
    }
    return false;
}

}  // namespace mw
