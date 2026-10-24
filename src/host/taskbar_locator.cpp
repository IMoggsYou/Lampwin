// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin
// Portions ported from the Windhawk mod "MacOS Minimize Animation" (MIT, Abdullah Masood
// and Potassiumuncher); see THIRD_PARTY_NOTICES.md.

#include "host/taskbar_locator.h"
#include <uiautomation.h>
#include <propsys.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wctype.h>
#include <algorithm>
#include "common/strutil.h"
#include "host/log.h"

namespace {

const ULONGLONG kRefreshDebounceMs = 400;
const int kStrongMatch = 500;
const int kExactMatch = 10000;   // same AppUserModelID

// PKEY_AppUserModel_ID {9F4C2855-9F79-4B39-A8D0-E1D42DE1D5F3}, 5
const PROPERTYKEY kAppUserModelId = {{0x9F4C2855, 0x9F79, 0x4B39, {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}}, 5};
const wchar_t kAppIdPrefix[] = L"Appid: ";

std::wstring Lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });
    return s;
}

IUIAutomation* Uia(void* p) { return (IUIAutomation*)p; }

void AddTrays(std::vector<HWND>* out) {
    if (HWND main = FindWindowW(L"Shell_TrayWnd", nullptr)) out->push_back(main);
    HWND sec = nullptr;
    while ((sec = FindWindowExW(nullptr, sec, L"Shell_SecondaryTrayWnd", nullptr)) != nullptr) out->push_back(sec);
}

// Folders the shell writes as "{KNOWNFOLDERID}\rest" in implicit AppUserModelIDs.
const std::vector<std::pair<std::wstring, std::wstring>>& KnownFolderPrefixes() {
    static const std::vector<std::pair<std::wstring, std::wstring>> list = [] {
        std::vector<std::pair<std::wstring, std::wstring>> out;
        const KNOWNFOLDERID* ids[] = {&FOLDERID_ProgramFilesX64, &FOLDERID_ProgramFilesX86, &FOLDERID_System,
                                      &FOLDERID_SystemX86, &FOLDERID_Windows, &FOLDERID_ProgramFilesCommonX64,
                                      &FOLDERID_ProgramFilesCommonX86};
        for (const KNOWNFOLDERID* id : ids) {
            PWSTR path = nullptr;
            if (SUCCEEDED(SHGetKnownFolderPath(*id, KF_FLAG_DONT_VERIFY, nullptr, &path)) && path) {
                wchar_t guid[64];
                StringFromGUID2(*id, guid, 64);
                out.emplace_back(Lower(path) + L"\\", Lower(guid) + L"\\");
            }
            CoTaskMemFree(path);
        }
        return out;
    }();
    return list;
}

DockTarget TargetOf(const RECT& rc) {
    DockTarget t;
    t.x = rc.left + (rc.right - rc.left) / 2;
    t.y = rc.top + (rc.bottom - rc.top) / 2;
    int side = std::min(rc.right - rc.left, rc.bottom - rc.top);
    t.size = std::clamp((int)(side * 0.55f), 16, 64);   // the icon inside the button
    t.hasRect = true;
    return t;
}

// Potassiumuncher's title / process scoring (the mod's GetTaskbarButtonX).
int NameScore(const std::wstring& title, const std::wstring& proc, const std::wstring& hint, const std::wstring& n) {
    int score = 0;
    if (!title.empty()) {
        if (title == n) score += 1000;
        if (title.find(n) != std::wstring::npos) score += 500;
        if (n.find(title) != std::wstring::npos) score += 500;
    }
    if (!proc.empty() && n.find(proc) != std::wstring::npos) score += 400;
    if (!hint.empty() && hint != proc && n.find(hint) != std::wstring::npos) score += 900;
    std::wstring word;
    for (size_t i = 0; i <= title.size(); ++i) {
        wchar_t c = i < title.size() ? title[i] : L' ';
        if (iswalnum(c)) {
            word += c;
        } else {
            if (word.length() >= 4 && n.find(word) != std::wstring::npos) score += 50;
            word.clear();
        }
    }
    return score;
}

}  // namespace

bool TaskbarLocator::Start() {
    wake_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    refreshDue_ = GetTickCount64();   // initial scan right away
    thread_ = std::thread([this] { ThreadMain(); });
    return true;
}

void TaskbarLocator::Stop() {
    if (!thread_.joinable()) return;
    stop_ = true;
    SetEvent(wake_);
    thread_.join();
    CloseHandle(wake_);
    wake_ = nullptr;
}

bool TaskbarLocator::InitUia() {
    IUIAutomation* uia = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(CUIAutomation8), nullptr, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation),
                                  (void**)&uia);
    if (FAILED(hr)) {
        hr = CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation),
                              (void**)&uia);
    }
    if (FAILED(hr) || !uia) {
        Log(L"locator: UI Automation unavailable (0x%08lX)", (unsigned long)hr);
        return false;
    }
    // Never let a hung Explorer stall the queue for long.
    IUIAutomation2* uia2 = nullptr;
    if (SUCCEEDED(uia->QueryInterface(__uuidof(IUIAutomation2), (void**)&uia2)) && uia2) {
        uia2->put_TransactionTimeout(400);
        uia2->put_ConnectionTimeout(1500);
        uia2->Release();
    }

    IUIAutomationCondition* buttonCond = nullptr;
    IUIAutomationCondition* listItemCond = nullptr;
    IUIAutomationCondition* orCond = nullptr;
    VARIANT v;
    v.vt = VT_I4;
    v.lVal = UIA_ButtonControlTypeId;
    uia->CreatePropertyCondition(UIA_ControlTypePropertyId, v, &buttonCond);
    v.lVal = UIA_ListItemControlTypeId;
    uia->CreatePropertyCondition(UIA_ControlTypePropertyId, v, &listItemCond);
    if (buttonCond && listItemCond) uia->CreateOrCondition(buttonCond, listItemCond, &orCond);
    if (buttonCond) buttonCond->Release();
    if (listItemCond) listItemCond->Release();

    // Fetch id + name + rect for all matches in one cross-process call instead of
    // several round trips per element.
    IUIAutomationCacheRequest* cache = nullptr;
    uia->CreateCacheRequest(&cache);
    if (cache) {
        cache->AddProperty(UIA_NamePropertyId);
        cache->AddProperty(UIA_AutomationIdPropertyId);
        cache->AddProperty(UIA_BoundingRectanglePropertyId);
    }
    if (!orCond || !cache) {
        if (orCond) orCond->Release();
        if (cache) cache->Release();
        uia->Release();
        return false;
    }
    uia_ = uia;
    condition_ = orCond;
    cacheRequest_ = cache;
    return true;
}

void TaskbarLocator::ReleaseUia() {
    if (cacheRequest_) ((IUIAutomationCacheRequest*)cacheRequest_)->Release();
    if (condition_) ((IUIAutomationCondition*)condition_)->Release();
    if (uia_) Uia(uia_)->Release();
    cacheRequest_ = condition_ = uia_ = nullptr;
}

void TaskbarLocator::Refresh() {
    std::vector<Button> apps, all;
    std::vector<HWND> trays;
    AddTrays(&trays);
    LARGE_INTEGER f, t0, t1;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    const size_t prefixLen = wcslen(kAppIdPrefix);
    for (HWND tray : trays) {
        if (!uia_) break;
        IUIAutomationElement* root = nullptr;
        if (FAILED(Uia(uia_)->ElementFromHandle(tray, &root)) || !root) continue;
        IUIAutomationElementArray* arr = nullptr;
        if (SUCCEEDED(root->FindAllBuildCache(TreeScope_Descendants, (IUIAutomationCondition*)condition_,
                                              (IUIAutomationCacheRequest*)cacheRequest_, &arr)) &&
            arr) {
            int len = 0;
            arr->get_Length(&len);
            for (int i = 0; i < len; ++i) {
                IUIAutomationElement* item = nullptr;
                if (FAILED(arr->GetElement(i, &item)) || !item) continue;
                BSTR name = nullptr, id = nullptr;
                RECT rc = {};
                item->get_CachedName(&name);
                item->get_CachedAutomationId(&id);
                if (SUCCEEDED(item->get_CachedBoundingRectangle(&rc)) && rc.right > rc.left) {
                    Button b{Lower(name ? name : L""), L"", rc, tray};
                    if (id && wcsncmp(id, kAppIdPrefix, prefixLen) == 0) {
                        b.aumid = Lower(id + prefixLen);
                        apps.push_back(b);
                    }
                    if (!b.name.empty()) all.push_back(std::move(b));
                }
                if (name) SysFreeString(name);
                if (id) SysFreeString(id);
                item->Release();
            }
            arr->Release();
        }
        root->Release();
    }
    QueryPerformanceCounter(&t1);

    // App buttons only (never Start, Search, Widgets or tray icons). A taskbar without
    // "Appid:" ids (older Windows) falls back to every button, as in the mod.
    bool haveAppIds = !apps.empty();
    AcquireSRWLockExclusive(&lock_);
    buttons_.swap(haveAppIds ? apps : all);
    valid_ = true;
    dirty_ = false;
    refreshDue_ = 0;
    size_t count = buttons_.size();
    ReleaseSRWLockExclusive(&lock_);
    Log(L"locator: %zu taskbar %ls in %.1f ms", count, haveAppIds ? L"app buttons" : L"items (no app ids)",
        (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / f.QuadPart);
}

// The AppUserModelIDs this window's taskbar button may carry, lowercase:
//  1. the window's explicit one (SetCurrentProcessExplicitAppUserModelID is not readable
//     from outside; per-window ids, as Chromium sets them, are),
//  2. its package's (Store / MSIX apps: "Family!App"),
//  3. the implicit one: the exe path, as is or as "{KNOWNFOLDERID}\rest".
TaskbarLocator::WinInfo TaskbarLocator::Describe(HWND hwnd, HMONITOR hMon) const {
    WinInfo w;
    // Cross-process GetWindowText reads the stored caption; it never sends WM_GETTEXT.
    wchar_t title[512] = {0};
    GetWindowTextW(hwnd, title, 512);
    w.title = Lower(title);

    IPropertyStore* store = nullptr;
    if (SUCCEEDED(SHGetPropertyStoreForWindow(hwnd, __uuidof(IPropertyStore), (void**)&store)) && store) {
        PROPVARIANT v;
        PropVariantInit(&v);
        if (SUCCEEDED(store->GetValue(kAppUserModelId, &v)) && v.vt == VT_LPWSTR && v.pwszVal && *v.pwszVal) {
            w.aumids.push_back(Lower(v.pwszVal));
        }
        PropVariantClear(&v);
        store->Release();
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        typedef LONG(WINAPI * GetAumid_t)(HANDLE, UINT32*, PWSTR);
        static auto getAumid =
            (GetAumid_t)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetApplicationUserModelId");
        wchar_t aumid[256];
        UINT32 len = 256;
        if (getAumid && getAumid(hProc, &len, aumid) == ERROR_SUCCESS) w.aumids.push_back(Lower(aumid));

        wchar_t path[MAX_PATH];
        DWORD plen = MAX_PATH;
        mw::ImageNames names;
        if (QueryFullProcessImageNameW(hProc, 0, path, &plen) && mw::MakeImageNames(path, &names)) {
            w.aumids.push_back(names.full);
            for (const auto& [prefix, guid] : KnownFolderPrefixes()) {
                if (wcsncmp(names.full, prefix.c_str(), prefix.size()) == 0) {
                    w.aumids.push_back(guid + (names.full + prefix.size()));
                }
            }
            w.proc = names.base;
            size_t dot = w.proc.find(L'.');
            if (dot != std::wstring::npos) w.proc = w.proc.substr(0, dot);
        }
        CloseHandle(hProc);
    }
    if (w.proc == L"explorer") w.aumids.push_back(L"microsoft.windows.explorer");
    w.hint = w.proc;
    if (w.proc == L"chrome") w.hint = L"google chrome";
    else if (w.proc == L"msedge") w.hint = L"microsoft edge";
    if (!w.proc.empty() && hMon) w.processKey = w.proc + L"_" + std::to_wstring(reinterpret_cast<size_t>(hMon));
    return w;
}

bool TaskbarLocator::MatchLocked(const WinInfo& w, HMONITOR hMon, DockTarget* out, int* outScore) const {
    HWND tray = FindTaskbarForMonitor(hMon);
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfoW(hMon, &mi);
    const Button* best = nullptr;
    int bestScore = 0;

    // 1. Same AppUserModelID: exactly the button the shell made for this window's app.
    //    Several such buttons = taskbar set to never combine (one button per window):
    //    the title picks the right one.
    for (const Button& b : buttons_) {
        if (b.tray != tray || b.aumid.empty()) continue;
        if (std::find(w.aumids.begin(), w.aumids.end(), b.aumid) == w.aumids.end()) continue;
        int score = kExactMatch + NameScore(w.title, L"", L"", b.name);
        if (score > bestScore) {
            best = &b;
            bestScore = score;
        }
    }
    // 2. Fallback: title / process name scoring.
    if (!best) {
        for (const Button& b : buttons_) {
            if (b.tray != tray) continue;
            int score = NameScore(w.title, w.proc, w.hint, b.name);
            if (b.aumid.empty()) {
                // Only on a taskbar without app ids: keep the mod's guards against the
                // system buttons and the tray area.
                if (b.name.find(L"start") != std::wstring::npos || b.name.find(L"search") != std::wstring::npos ||
                    b.name.find(L"task view") != std::wstring::npos || b.name.find(L"widgets") != std::wstring::npos ||
                    b.rc.left >= mi.rcMonitor.right - 50) {
                    continue;
                }
            }
            if (score > bestScore) {
                best = &b;
                bestScore = score;
            }
        }
    }
    if (!best) return false;
    *out = TargetOf(best->rc);
    *outScore = bestScore;
    return true;
}

const TaskbarLocator::Button* TaskbarLocator::ButtonAtLocked(POINT pt) const {
    for (const Button& b : buttons_) {
        if (PtInRect(&b.rc, pt)) return &b;
    }
    return nullptr;
}

void TaskbarLocator::LearnLocked(HWND hwnd, const WinInfo& w, const DockTarget& t, bool perProcess) {
    perWindow_[hwnd] = t;
    if (perProcess && !w.processKey.empty()) perProcess_[w.processKey] = t;
}

DockTarget TaskbarLocator::FallbackLocked(HWND hwnd, const WinInfo& w, const DockTarget& def) const {
    auto it = perWindow_.find(hwnd);
    if (it != perWindow_.end()) return it->second;
    if (!w.processKey.empty()) {
        auto p = perProcess_.find(w.processKey);
        if (p != perProcess_.end()) return p->second;
    }
    return def;
}

bool TaskbarLocator::TryResolve(HWND hwnd, HMONITOR hMon, const POINT* click, DockTarget* out) {
    WinInfo w = Describe(hwnd, hMon);
    AcquireSRWLockExclusive(&lock_);
    bool ok = false;
    const Button* under = click ? ButtonAtLocked(*click) : nullptr;
    int score = 0;
    if (under && !under->aumid.empty() &&
        std::find(w.aumids.begin(), w.aumids.end(), under->aumid) != w.aumids.end()) {
        // Pressed this app's own button: exact, even with several windows of one app.
        *out = TargetOf(under->rc);
        ok = true;
    } else if (valid_ && !dirty_ && MatchLocked(w, hMon, out, &score) && score >= kStrongMatch) {
        ok = true;
    } else if (under) {
        *out = TargetOf(under->rc);   // its id is unknown to us: trust the press
        ok = true;
    } else if (click) {
        // On the taskbar, but over no button we know (yet): the press point itself.
        *out = DockTarget{click->x, 0, 0, false};
        ok = true;
    }
    if (ok) LearnLocked(hwnd, w, *out, !click);
    ReleaseSRWLockExclusive(&lock_);
    return ok;
}

void TaskbarLocator::ResolveAsync(HWND hwnd, HMONITOR hMon, DockTarget fallback, std::function<void(DockTarget)> done) {
    AcquireSRWLockExclusive(&lock_);
    queue_.push_back({hwnd, hMon, fallback, std::move(done)});
    ReleaseSRWLockExclusive(&lock_);
    SetEvent(wake_);
}

DockTarget TaskbarLocator::Fallback(HWND hwnd, HMONITOR hMon, int defaultX) {
    WinInfo w = Describe(hwnd, hMon);
    AcquireSRWLockShared(&lock_);
    DockTarget t = FallbackLocked(hwnd, w, DockTarget{defaultX, 0, 0, false});
    ReleaseSRWLockShared(&lock_);
    return t;
}

void TaskbarLocator::Invalidate() {
    AcquireSRWLockExclusive(&lock_);
    dirty_ = true;
    if (!refreshDue_) refreshDue_ = GetTickCount64() + kRefreshDebounceMs;
    ReleaseSRWLockExclusive(&lock_);
    if (wake_) SetEvent(wake_);
}

void TaskbarLocator::Forget(HWND hwnd) {
    AcquireSRWLockExclusive(&lock_);
    perWindow_.erase(hwnd);
    ReleaseSRWLockExclusive(&lock_);
}

void TaskbarLocator::Resolve(Request& r, bool* refreshed) {
    if (!*refreshed && (dirty_ || !valid_)) {
        Refresh();
        *refreshed = true;
    }
    WinInfo w = Describe(r.hwnd, r.hMon);
    DockTarget t;
    int score = 0;
    AcquireSRWLockExclusive(&lock_);
    bool found = MatchLocked(w, r.hMon, &t, &score);
    ReleaseSRWLockExclusive(&lock_);
    // A weak match against an older list may mean the window's title changed since:
    // rescan once before settling.
    if ((!found || score < kStrongMatch) && !*refreshed) {
        Refresh();
        *refreshed = true;
        AcquireSRWLockExclusive(&lock_);
        found = MatchLocked(w, r.hMon, &t, &score);
        ReleaseSRWLockExclusive(&lock_);
    }
    AcquireSRWLockExclusive(&lock_);
    if (found) LearnLocked(r.hwnd, w, t, true);
    else t = FallbackLocked(r.hwnd, w, r.fallback);
    ReleaseSRWLockExclusive(&lock_);
    r.done(t);
}

void TaskbarLocator::ThreadMain() {
    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool uiaOk = InitUia();
    while (!stop_) {
        DWORD timeout = INFINITE;
        AcquireSRWLockShared(&lock_);
        bool haveWork = !queue_.empty();
        ULONGLONG due = refreshDue_;
        ReleaseSRWLockShared(&lock_);
        if (!haveWork && due) {
            ULONGLONG now = GetTickCount64();
            timeout = due > now ? (DWORD)(due - now) : 0;
        }
        if (!haveWork) WaitForSingleObject(wake_, timeout);
        if (stop_) break;

        std::deque<Request> work;
        AcquireSRWLockExclusive(&lock_);
        work.swap(queue_);
        due = refreshDue_;
        ReleaseSRWLockExclusive(&lock_);

        bool refreshed = false;
        if (uiaOk && due && GetTickCount64() >= due) {
            Refresh();
            refreshed = true;
        }
        for (Request& r : work) {
            if (uiaOk) {
                Resolve(r, &refreshed);
            } else {
                r.done(Fallback(r.hwnd, r.hMon, r.fallback.x));
            }
        }
    }
    // Pending requests still get an answer, so their windows are revealed.
    std::deque<Request> rest;
    AcquireSRWLockExclusive(&lock_);
    rest.swap(queue_);
    ReleaseSRWLockExclusive(&lock_);
    for (Request& r : rest) r.done(r.fallback);
    ReleaseUia();
    if (SUCCEEDED(hrCo)) CoUninitialize();
}
