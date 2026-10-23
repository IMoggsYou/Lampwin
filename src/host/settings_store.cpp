// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 KosaWLeb. Created by KosaWLeb: https://github.com/KosaWLeb/MacWin

#include "host/settings_store.h"
#include <wctype.h>
#include "common/names.h"
#include "host/json.h"
#include "host/log.h"

namespace {

int Clamp(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

std::wstring Lower(std::wstring s) {
    for (wchar_t& c : s) c = (wchar_t)towlower(c);
    return s;
}

// Entries separated by NUL, ended by an empty entry; never overruns the field.
void WriteList(wchar_t* dst, const std::vector<std::wstring>& items) {
    size_t pos = 0;
    for (const std::wstring& raw : items) {
        std::wstring item = Lower(raw);
        if (item.empty()) continue;
        if (pos + item.size() + 1 > MACWIN_LIST_CHARS - 2) break;
        memcpy(dst + pos, item.c_str(), item.size() * sizeof(wchar_t));
        pos += item.size();
        dst[pos++] = 0;
    }
    while (pos < MACWIN_LIST_CHARS) dst[pos++] = 0;
}

bool ReadUtf8File(const std::wstring& path, std::wstring* out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size;
    bool ok = GetFileSizeEx(h, &size) && size.QuadPart < (1 << 20);
    std::string bytes;
    if (ok) {
        bytes.resize((size_t)size.QuadPart);
        DWORD read = 0;
        ok = bytes.empty() || (ReadFile(h, &bytes[0], (DWORD)bytes.size(), &read, nullptr) && read == bytes.size());
    }
    CloseHandle(h);
    if (!ok) return false;
    size_t skip = (bytes.size() >= 3 && (unsigned char)bytes[0] == 0xEF && (unsigned char)bytes[1] == 0xBB &&
                   (unsigned char)bytes[2] == 0xBF) ? 3 : 0;
    int n = MultiByteToWideChar(CP_UTF8, 0, bytes.data() + skip, (int)(bytes.size() - skip), nullptr, 0);
    out->assign((size_t)n, L'\0');
    if (n) MultiByteToWideChar(CP_UTF8, 0, bytes.data() + skip, (int)(bytes.size() - skip), &(*out)[0], n);
    return true;
}

// settings.json names of MW_EFFECT_*.
const wchar_t* const kEffectNames[MW_EFFECT_COUNT] = {L"system", L"genie"};

int EffectFromJson(const json::Value* v, int fallback) {
    if (!v || v->type != json::Value::String) return fallback;
    for (int i = 0; i < MW_EFFECT_COUNT; ++i) {
        if (v->str == kEffectNames[i]) return i;
    }
    return fallback;
}

// settings.json layout version: 2 added the game folders to noInject.
const int kSettingsVersion = 2;

}  // namespace

std::vector<std::wstring> DefaultNoInject() {
    return {L"robloxplayerbeta.exe", L"valorant-win64-shipping.exe", L"fortniteclient-win64-shipping.exe",
            L"r5apex.exe", L"r5apex_dx12.exe", L"cs2.exe", L"overwatch.exe", L"cod.exe", L"wow.exe",
            L"\\steamapps\\common\\", L"\\epic games\\", L"\\riot games\\", L"\\xboxgames\\",
            L"\\ea games\\", L"\\ubisoft game launcher\\games\\", L"\\gog galaxy\\games\\"};
}

void SettingsStore::InitPath() {
    path_ = RoamingDataDir() + L"\\settings.json";
}

bool SettingsStore::Create(uint32_t hostInstance, SECURITY_ATTRIBUTES* sa) {
    instance_ = hostInstance;
    InitPath();
    // Read-only for the DLLs; may already exist if a previous host died while
    // programs still had it mapped - then it is simply reused.
    // Access denied = the section of a previous (elevated) host is still held by DLLs
    // that are shutting down; they let go of it within moments.
    for (int attempt = 0; attempt < 30; ++attempt) {
        map_ = CreateFileMappingW(INVALID_HANDLE_VALUE, sa, PAGE_READWRITE, 0, sizeof(SharedSettings),
                                  MACWIN_SETTINGS_SECTION);
        if (map_ || GetLastError() != ERROR_ACCESS_DENIED) break;
        Sleep(100);
    }
    if (!map_) {
        Log(L"CreateFileMapping(settings) failed: %lu", GetLastError());
        return false;
    }
    view_ = (SharedSettings*)MapViewOfFile(map_, FILE_MAP_WRITE, 0, 0, sizeof(SharedSettings));
    if (!view_) {
        Log(L"MapViewOfFile(settings) failed: %lu", GetLastError());
        return false;
    }
    return true;
}

void SettingsStore::SetIpcWindow(HWND hwnd) {
    ipcHwnd_ = (uint64_t)(uintptr_t)hwnd;
}

void SettingsStore::Load() {
    HostSettings d;   // defaults
    std::wstring text;
    if (!ReadUtf8File(path_, &text)) {
        s_ = d;
        Save();
        Log(L"settings: no file, defaults written to %ls", path_.c_str());
        return;
    }
    json::Value root;
    if (!json::Parse(text, &root) || root.type != json::Value::Object) {
        s_ = d;
        root_ = json::Object();
        Log(L"settings: %ls is not valid JSON, using defaults (the file is left as it is)", path_.c_str());
        return;
    }
    root_ = root;
    s_.enabled = root.GetBool(L"enabled", d.enabled);
    s_.devMode = root.GetBool(L"devMode", d.devMode);
    s_.devAllowlist = root.GetStrings(L"devAllowlist", d.devAllowlist);
    s_.noInject = root.GetStrings(L"noInject", d.noInject);
    const json::Value* language = root.Get(L"language");
    s_.language = language && language->type == json::Value::String ? language->str : d.language;
    if (const json::Value* u = root.Get(L"updates"); u && u->type == json::Value::Object) {
        s_.updateCheck = u->GetBool(L"check", d.updateCheck);
        s_.updateAutoInstall = u->GetBool(L"autoInstall", d.updateAutoInstall);
    }
    const json::Value* g = root.Get(L"genie");
    json::Value empty;
    empty.type = json::Value::Object;
    if (!g || g->type != json::Value::Object) g = &empty;
    const json::Value* style = g->Get(L"style");
    s_.genie.style = (style && style->type == json::Value::String && style->str == L"classic") ? 1 : 0;
    s_.genie.durationMs = Clamp(g->GetInt(L"durationMs", d.genie.durationMs), 50, 2000);
    s_.genie.restore = g->GetBool(L"restoreAnimation", d.genie.restore);
    s_.genie.openEffect = EffectFromJson(g->Get(L"openAnimation"), d.genie.openEffect);
    s_.genie.closeEffect = EffectFromJson(g->Get(L"closeAnimation"), d.genie.closeEffect);
    s_.genie.openDurationMs = Clamp(g->GetInt(L"openDurationMs", d.genie.openDurationMs), 100, 1500);
    s_.genie.closeDurationMs = Clamp(g->GetInt(L"closeDurationMs", d.genie.closeDurationMs), 100, 1500);
    s_.genie.multiMonitor = g->GetBool(L"multiMonitor", d.genie.multiMonitor);
    s_.genie.unhideTaskbar = g->GetBool(L"unhideTaskbar", d.genie.unhideTaskbar);
    s_.genie.unhideDurationMs = Clamp(g->GetInt(L"unhideDurationMs", d.genie.unhideDurationMs), 0, 5000);
    s_.genie.meshQuality = Clamp(g->GetInt(L"meshQuality", d.genie.meshQuality), 8, 96);
    s_.genie.cornerRadius = Clamp(g->GetInt(L"cornerRadius", d.genie.cornerRadius), 0, 40);
    s_.genie.cornerSmoothing = Clamp(g->GetInt(L"cornerSmoothing", d.genie.cornerSmoothing), 0, 100);
    s_.genie.windowCorners = g->GetBool(L"windowCorners", d.genie.windowCorners);
    s_.genie.excluded = g->GetStrings(L"excludedPrograms", d.genie.excluded);
    Log(L"settings: loaded %ls", path_.c_str());

    // An older file: new default protections are added to what the user has.
    if (root.GetInt(L"settingsVersion", 1) < kSettingsVersion) {
        for (const std::wstring& entry : DefaultNoInject()) {
            bool present = false;
            for (const std::wstring& have : s_.noInject) present = present || _wcsicmp(have.c_str(), entry.c_str()) == 0;
            if (!present) s_.noInject.push_back(entry);
        }
        Log(L"settings: updated to version %d", kSettingsVersion);
        Save();
    }
}

bool SettingsStore::Save() const {
    const HostSettings& s = s_;
    json::Value root = root_.type == json::Value::Object ? root_ : json::Object();
    json::Set(&root, L"settingsVersion", json::Number(kSettingsVersion));
    json::Set(&root, L"enabled", json::Bool(s.enabled));
    json::Set(&root, L"devMode", json::Bool(s.devMode));
    json::Set(&root, L"devAllowlist", json::Strings(s.devAllowlist));
    json::Set(&root, L"noInject", json::Strings(s.noInject));
    json::Set(&root, L"language", json::String(s.language));
    const json::Value* oldUpdates = root.Get(L"updates");
    json::Value updates = oldUpdates && oldUpdates->type == json::Value::Object ? *oldUpdates : json::Object();
    json::Set(&updates, L"check", json::Bool(s.updateCheck));
    json::Set(&updates, L"autoInstall", json::Bool(s.updateAutoInstall));
    json::Set(&root, L"updates", updates);
    const json::Value* oldGenie = root.Get(L"genie");
    json::Value g = oldGenie && oldGenie->type == json::Value::Object ? *oldGenie : json::Object();
    json::Set(&g, L"style", json::String(s.genie.style == 1 ? L"classic" : L"modern"));
    json::Set(&g, L"durationMs", json::Number(s.genie.durationMs));
    json::Set(&g, L"restoreAnimation", json::Bool(s.genie.restore));
    json::Set(&g, L"openAnimation", json::String(kEffectNames[s.genie.openEffect]));
    json::Set(&g, L"closeAnimation", json::String(kEffectNames[s.genie.closeEffect]));
    json::Set(&g, L"openDurationMs", json::Number(s.genie.openDurationMs));
    json::Set(&g, L"closeDurationMs", json::Number(s.genie.closeDurationMs));
    json::Set(&g, L"multiMonitor", json::Bool(s.genie.multiMonitor));
    json::Set(&g, L"unhideTaskbar", json::Bool(s.genie.unhideTaskbar));
    json::Set(&g, L"unhideDurationMs", json::Number(s.genie.unhideDurationMs));
    json::Set(&g, L"meshQuality", json::Number(s.genie.meshQuality));
    json::Set(&g, L"cornerRadius", json::Number(s.genie.cornerRadius));
    json::Set(&g, L"cornerSmoothing", json::Number(s.genie.cornerSmoothing));
    json::Set(&g, L"windowCorners", json::Bool(s.genie.windowCorners));
    json::Set(&g, L"excludedPrograms", json::Strings(s.genie.excluded));
    json::Set(&root, L"genie", g);
    std::wstring t = json::Serialize(root) + L"\n";

    int n = WideCharToMultiByte(CP_UTF8, 0, t.c_str(), (int)t.size(), nullptr, 0, nullptr, nullptr);
    std::string bytes((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, t.c_str(), (int)t.size(), &bytes[0], n, nullptr, nullptr);

    // Write to a temp file and swap it in, so a crash never leaves half a file.
    std::wstring tmp = path_ + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(h, bytes.data(), (DWORD)bytes.size(), &written, nullptr) && written == bytes.size();
    CloseHandle(h);
    ok = ok && MoveFileExW(tmp.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!ok) Log(L"settings: saving %ls failed: %lu", path_.c_str(), GetLastError());
    return ok;
}

void SettingsStore::Write(bool enabled) {
    if (!view_) return;
    SharedSettings* v = view_;
    InterlockedIncrement(&v->seq);   // odd: readers retry
    MemoryBarrier();
    v->magic = MACWIN_SETTINGS_MAGIC;
    v->size = sizeof(SharedSettings);
    v->hostPid = GetCurrentProcessId();
    v->hostInstance = instance_;
    v->hostIpcHwnd = ipcHwnd_;
    v->enabled = enabled ? 1 : 0;
    v->devMode = s_.devMode ? 1 : 0;
    v->genieDurationMs = s_.genie.durationMs;
    v->genieRestore = s_.genie.restore ? 1 : 0;
    v->genieLaunch = s_.genie.openEffect != MW_EFFECT_SYSTEM ? 1 : 0;
    v->openEffect = s_.genie.openEffect;
    v->closeEffect = s_.genie.closeEffect;
    v->openDurationMs = s_.genie.openDurationMs;
    v->closeDurationMs = s_.genie.closeDurationMs;
    v->genieMultiMonitor = s_.genie.multiMonitor ? 1 : 0;
    v->genieUnhideTaskbar = s_.genie.unhideTaskbar ? 1 : 0;
    v->genieUnhideDurationMs = s_.genie.unhideDurationMs;
    v->genieMeshQuality = s_.genie.meshQuality;
    v->cornerRadius = s_.genie.cornerRadius;
    v->cornerSmoothing = s_.genie.cornerSmoothing;
    v->windowCorners = s_.genie.windowCorners ? 1 : 0;
    WriteList(v->genieExcluded, s_.genie.excluded);
    WriteList(v->noInject, s_.noInject);
    WriteList(v->devAllowlist, s_.devAllowlist);
    v->generation = v->generation + 1;
    MemoryBarrier();
    InterlockedIncrement(&v->seq);   // even again
}

void SettingsStore::Publish() {
    Write(s_.enabled);
}

void SettingsStore::PublishDisabled() {
    Write(false);
}
