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

std::wstring ListJson(const std::vector<std::wstring>& items, const wchar_t* indent) {
    if (items.empty()) return L"[]";
    std::wstring out = L"[\n";
    for (size_t i = 0; i < items.size(); ++i) {
        out += indent;
        out += L"  " + json::Quote(items[i]);
        out += (i + 1 < items.size()) ? L",\n" : L"\n";
    }
    out += indent;
    out += L"]";
    return out;
}

}  // namespace

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
        Log(L"settings: %ls is not valid JSON, using defaults", path_.c_str());
        return;
    }
    s_.enabled = root.GetBool(L"enabled", d.enabled);
    s_.devMode = root.GetBool(L"devMode", d.devMode);
    s_.devAllowlist = root.GetStrings(L"devAllowlist", d.devAllowlist);
    s_.noInject = root.GetStrings(L"noInject", d.noInject);
    const json::Value* g = root.Get(L"genie");
    json::Value empty;
    empty.type = json::Value::Object;
    if (!g || g->type != json::Value::Object) g = &empty;
    const json::Value* style = g->Get(L"style");
    s_.genie.style = (style && style->type == json::Value::String && style->str == L"classic") ? 1 : 0;
    s_.genie.durationMs = Clamp(g->GetInt(L"durationMs", d.genie.durationMs), 50, 2000);
    s_.genie.restore = g->GetBool(L"restoreAnimation", d.genie.restore);
    s_.genie.launch = g->GetBool(L"launchAnimation", d.genie.launch);
    s_.genie.multiMonitor = g->GetBool(L"multiMonitor", d.genie.multiMonitor);
    s_.genie.unhideTaskbar = g->GetBool(L"unhideTaskbar", d.genie.unhideTaskbar);
    s_.genie.unhideDurationMs = Clamp(g->GetInt(L"unhideDurationMs", d.genie.unhideDurationMs), 0, 5000);
    s_.genie.meshQuality = Clamp(g->GetInt(L"meshQuality", d.genie.meshQuality), 8, 96);
    s_.genie.excluded = g->GetStrings(L"excludedPrograms", d.genie.excluded);
    Log(L"settings: loaded %ls", path_.c_str());
}

bool SettingsStore::Save() const {
    const HostSettings& s = s_;
    std::wstring t = L"{\n";
    t += L"  \"enabled\": " + std::wstring(s.enabled ? L"true" : L"false") + L",\n";
    t += L"  \"devMode\": " + std::wstring(s.devMode ? L"true" : L"false") + L",\n";
    t += L"  \"devAllowlist\": " + ListJson(s.devAllowlist, L"  ") + L",\n";
    t += L"  \"noInject\": " + ListJson(s.noInject, L"  ") + L",\n";
    t += L"  \"genie\": {\n";
    t += L"    \"style\": " + std::wstring(s.genie.style == 1 ? L"\"classic\"" : L"\"modern\"") + L",\n";
    t += L"    \"durationMs\": " + std::to_wstring(s.genie.durationMs) + L",\n";
    t += L"    \"restoreAnimation\": " + std::wstring(s.genie.restore ? L"true" : L"false") + L",\n";
    t += L"    \"launchAnimation\": " + std::wstring(s.genie.launch ? L"true" : L"false") + L",\n";
    t += L"    \"multiMonitor\": " + std::wstring(s.genie.multiMonitor ? L"true" : L"false") + L",\n";
    t += L"    \"unhideTaskbar\": " + std::wstring(s.genie.unhideTaskbar ? L"true" : L"false") + L",\n";
    t += L"    \"unhideDurationMs\": " + std::to_wstring(s.genie.unhideDurationMs) + L",\n";
    t += L"    \"meshQuality\": " + std::to_wstring(s.genie.meshQuality) + L",\n";
    t += L"    \"excludedPrograms\": " + ListJson(s.genie.excluded, L"    ") + L"\n";
    t += L"  }\n}\n";

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
    v->genieLaunch = s_.genie.launch ? 1 : 0;
    v->genieMultiMonitor = s_.genie.multiMonitor ? 1 : 0;
    v->genieUnhideTaskbar = s_.genie.unhideTaskbar ? 1 : 0;
    v->genieUnhideDurationMs = s_.genie.unhideDurationMs;
    v->genieMeshQuality = s_.genie.meshQuality;
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
