#include "host/taskbar_locator.h"
#include <uiautomation.h>
#include <wctype.h>
#include <algorithm>
#include "common/strutil.h"
#include "host/log.h"
#include "host/render/genie_effect.h"

namespace {

const ULONGLONG kRefreshDebounceMs = 400;
const int kStrongMatch = 500;

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

    // Fetch name + rect for all matches in one cross-process call instead of two
    // round trips per element.
    IUIAutomationCacheRequest* cache = nullptr;
    uia->CreateCacheRequest(&cache);
    if (cache) {
        cache->AddProperty(UIA_NamePropertyId);
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
    std::vector<Button> list;
    std::vector<HWND> trays;
    AddTrays(&trays);
    LARGE_INTEGER f, t0, t1;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
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
                BSTR name = nullptr;
                RECT rc = {};
                if (SUCCEEDED(item->get_CachedName(&name)) && name && SUCCEEDED(item->get_CachedBoundingRectangle(&rc))) {
                    std::wstring n = Lower(name);
                    if (!n.empty()) list.push_back({std::move(n), rc, tray});
                }
                if (name) SysFreeString(name);
                item->Release();
            }
            arr->Release();
        }
        root->Release();
    }
    QueryPerformanceCounter(&t1);

    AcquireSRWLockExclusive(&lock_);
    buttons_.swap(list);
    valid_ = true;
    dirty_ = false;
    refreshDue_ = 0;
    size_t count = buttons_.size();
    ReleaseSRWLockExclusive(&lock_);
    Log(L"locator: %zu taskbar items in %.1f ms", count, (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / f.QuadPart);
}

TaskbarLocator::WinInfo TaskbarLocator::Describe(HWND hwnd, HMONITOR hMon) const {
    WinInfo w;
    // Cross-process GetWindowText reads the stored caption; it never sends WM_GETTEXT.
    wchar_t title[512] = {0};
    GetWindowTextW(hwnd, title, 512);
    w.title = Lower(title);

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    mw::ImageNames names;
    if (mw::ProcessImageNames(pid, &names)) {
        w.proc = names.base;
        size_t dot = w.proc.find(L'.');
        if (dot != std::wstring::npos) w.proc = w.proc.substr(0, dot);
    }
    w.hint = w.proc;
    if (w.proc == L"chrome") w.hint = L"google chrome";
    else if (w.proc == L"msedge") w.hint = L"microsoft edge";
    if (!w.proc.empty() && hMon) w.processKey = w.proc + L"_" + std::to_wstring(reinterpret_cast<size_t>(hMon));
    return w;
}

// Potassiumuncher's scoring, against the cached button list of the monitor's taskbar.
bool TaskbarLocator::MatchLocked(const WinInfo& w, HMONITOR hMon, int* outX, int* outScore) const {
    HWND tray = FindTaskbarForMonitor(hMon);
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfoW(hMon, &mi);
    const int monRight = mi.rcMonitor.right;
    int best = 0;
    bool found = false;
    for (const Button& b : buttons_) {
        if (b.tray != tray) continue;
        const std::wstring& n = b.name;
        int score = 0;
        if (!w.title.empty()) {
            if (w.title == n) score += 1000;
            if (w.title.find(n) != std::wstring::npos) score += 500;
            if (n.find(w.title) != std::wstring::npos) score += 500;
        }
        if (!w.proc.empty() && n.find(w.proc) != std::wstring::npos) score += 400;
        if (!w.hint.empty() && w.hint != w.proc && n.find(w.hint) != std::wstring::npos) score += 900;

        std::wstring word;
        for (size_t i = 0; i <= w.title.size(); ++i) {
            wchar_t c = i < w.title.size() ? w.title[i] : L' ';
            if (iswalnum(c)) {
                word += c;
            } else {
                if (word.length() >= 4 && n.find(word) != std::wstring::npos) score += 50;
                word.clear();
            }
        }
        if (n.find(L"start") != std::wstring::npos) score -= 500;
        if (n.find(L"search") != std::wstring::npos) score -= 500;
        if (n.find(L"task view") != std::wstring::npos) score -= 500;
        if (n.find(L"widgets") != std::wstring::npos) score -= 500;

        if (score > best && b.rc.right > b.rc.left && b.rc.left < monRight - 50) {
            best = score;
            *outX = b.rc.left + (b.rc.right - b.rc.left) / 2;
            found = true;
        }
    }
    *outScore = best;
    return found;
}

void TaskbarLocator::LearnMatchLocked(HWND hwnd, const WinInfo& w, int x) {
    perWindow_[hwnd] = x;
    if (!w.processKey.empty()) perProcess_[w.processKey] = x;
}

int TaskbarLocator::FallbackLocked(HWND hwnd, const WinInfo& w, int defaultX) const {
    auto it = perWindow_.find(hwnd);
    if (it != perWindow_.end()) return it->second;
    if (!w.processKey.empty()) {
        auto p = perProcess_.find(w.processKey);
        if (p != perProcess_.end()) return p->second;
    }
    return defaultX;
}

bool TaskbarLocator::TryResolve(HWND hwnd, HMONITOR hMon, int* x) {
    WinInfo w = Describe(hwnd, hMon);
    AcquireSRWLockExclusive(&lock_);
    bool ok = false;
    if (valid_ && !dirty_) {
        int score = 0, mx = 0;
        if (MatchLocked(w, hMon, &mx, &score) && score >= kStrongMatch) {
            LearnMatchLocked(hwnd, w, mx);
            *x = mx;
            ok = true;
        }
    }
    ReleaseSRWLockExclusive(&lock_);
    return ok;
}

void TaskbarLocator::ResolveAsync(HWND hwnd, HMONITOR hMon, int fallbackX, std::function<void(int)> done) {
    AcquireSRWLockExclusive(&lock_);
    queue_.push_back({hwnd, hMon, fallbackX, std::move(done)});
    ReleaseSRWLockExclusive(&lock_);
    SetEvent(wake_);
}

void TaskbarLocator::Learn(HWND hwnd, int x) {
    AcquireSRWLockExclusive(&lock_);
    perWindow_[hwnd] = x;
    ReleaseSRWLockExclusive(&lock_);
}

int TaskbarLocator::Fallback(HWND hwnd, HMONITOR hMon, int defaultX) {
    WinInfo w = Describe(hwnd, hMon);
    AcquireSRWLockShared(&lock_);
    int x = FallbackLocked(hwnd, w, defaultX);
    ReleaseSRWLockShared(&lock_);
    return x;
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
    int x = 0, score = 0;
    AcquireSRWLockExclusive(&lock_);
    bool found = MatchLocked(w, r.hMon, &x, &score);
    ReleaseSRWLockExclusive(&lock_);
    // A weak match against an older list may mean the window's title changed since:
    // rescan once before settling.
    if ((!found || score < kStrongMatch) && !*refreshed) {
        Refresh();
        *refreshed = true;
        AcquireSRWLockExclusive(&lock_);
        found = MatchLocked(w, r.hMon, &x, &score);
        ReleaseSRWLockExclusive(&lock_);
    }
    AcquireSRWLockExclusive(&lock_);
    if (found) LearnMatchLocked(r.hwnd, w, x);
    else x = FallbackLocked(r.hwnd, w, r.fallbackX);
    ReleaseSRWLockExclusive(&lock_);
    r.done(x);
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
                r.done(Fallback(r.hwnd, r.hMon, r.fallbackX));
            }
        }
    }
    // Pending requests still get an answer, so their windows are revealed.
    std::deque<Request> rest;
    AcquireSRWLockExclusive(&lock_);
    rest.swap(queue_);
    ReleaseSRWLockExclusive(&lock_);
    for (Request& r : rest) r.done(r.fallbackX);
    ReleaseUia();
    if (SUCCEEDED(hrCo)) CoUninitialize();
}
