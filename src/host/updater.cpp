// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 KosaWLeb. Created by KosaWLeb: https://github.com/IMoggsYou/MacWin

#include "host/updater.h"
#include <bcrypt.h>
#include <stdio.h>
#include <wctype.h>
#include <winhttp.h>
#include <vector>
#include "host/json.h"
#include "host/log.h"
#include "version.h"

namespace {

const wchar_t kUserAgent[] = L"MacWin/" MACWIN_VERSION_W L" (+https://github.com/IMoggsYou/MacWin)";
const wchar_t kLatestReleaseUrl[] = L"https://api.github.com/repos/IMoggsYou/MacWin/releases/latest";
const wchar_t kDownloadPrefix[] = L"https://github.com/IMoggsYou/MacWin/releases/download/";
const size_t kMaxApiBytes = 1u << 20;
const size_t kMaxInstallerBytes = 256u << 20;
// Scheduled checks: once a day (a little less, so the timer never just misses it).
const ULONGLONG kCheckIntervalMs = 20ull * 60 * 60 * 1000;

std::wstring Lower(std::wstring s) {
    for (wchar_t& c : s) c = (wchar_t)towlower(c);
    return s;
}

std::wstring Widen(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring out((size_t)n, L'\0');
    if (n) MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], n);
    return out;
}

std::string Narrow(const std::wstring& s) {
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out((size_t)n, '\0');
    if (n) WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], n, nullptr, nullptr);
    return out;
}

// Only plain release file names ever become paths on disk.
bool IsSafeFileName(const std::wstring& name) {
    if (name.empty() || name.size() > 100 || name[0] == L'.') return false;
    for (wchar_t c : name) {
        if (!iswalnum(c) && c != L'.' && c != L'-' && c != L'_') return false;
    }
    return true;
}

bool IsVersion(const std::wstring& v) {
    if (v.empty() || !iswdigit(v[0]) || v.size() > 32) return false;
    for (wchar_t c : v) {
        if (!iswalnum(c) && c != L'.' && c != L'-') return false;
    }
    return true;
}

std::wstring NowIso(ULONGLONG* filetime) {
    SYSTEMTIME st;
    GetSystemTime(&st);
    FILETIME ft;
    SystemTimeToFileTime(&st, &ft);
    if (filetime) *filetime = ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    wchar_t buf[32];
    swprintf(buf, 32, L"%04u-%02u-%02uT%02u:%02u:%02uZ", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
             st.wSecond);
    return buf;
}

ULONGLONG ParseIso(const std::wstring& iso) {
    SYSTEMTIME st = {};
    unsigned y, mo, d, h, mi, s;
    if (swscanf(iso.c_str(), L"%u-%u-%uT%u:%u:%uZ", &y, &mo, &d, &h, &mi, &s) != 6) return 0;
    st.wYear = (WORD)y;
    st.wMonth = (WORD)mo;
    st.wDay = (WORD)d;
    st.wHour = (WORD)h;
    st.wMinute = (WORD)mi;
    st.wSecond = (WORD)s;
    FILETIME ft;
    if (!SystemTimeToFileTime(&st, &ft)) return 0;
    return ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

ULONGLONG NowFiletime() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

// One HTTPS GET. The body goes to `body` (up to `cap` bytes) or, with `file`, to that
// file. Redirects are followed (GitHub sends downloads to its CDN); the host the data
// finally came from must be trusted.
bool HttpGet(const std::wstring& url, size_t cap, std::string* body, HANDLE file, volatile bool* stop,
             DWORD* statusOut) {
    URL_COMPONENTS uc = {sizeof(uc)};
    wchar_t host[256] = L"", path[2048] = L"";
    uc.lpszHostName = host;
    uc.dwHostNameLength = ARRAYSIZE(host);
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = ARRAYSIZE(path);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS ||
        !IsTrustedDownloadHost(host)) {
        return false;
    }

    HINTERNET session = WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        session = WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
    }
    if (!session) return false;
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    if (!WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols))) {
        protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
    }
    WinHttpSetTimeouts(session, 10000, 15000, 30000, 30000);

    bool ok = false;
    HINTERNET connect = WinHttpConnect(session, host, uc.nPort, 0);
    HINTERNET request = connect ? WinHttpOpenRequest(connect, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                                     WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
                                : nullptr;
    if (request) {
        DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
        WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects));
        WinHttpAddRequestHeaders(request, L"Accept: application/vnd.github+json, application/octet-stream",
                                 (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
        DWORD status = 0, size = sizeof(status);
        if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(request, nullptr) &&
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX)) {
            if (statusOut) *statusOut = status;
            // Where the data really comes from after the redirects.
            wchar_t finalUrl[4096] = L"";
            DWORD urlBytes = sizeof(finalUrl);
            URL_COMPONENTS fc = {sizeof(fc)};
            wchar_t finalHost[256] = L"";
            fc.lpszHostName = finalHost;
            fc.dwHostNameLength = ARRAYSIZE(finalHost);
            bool trusted = WinHttpQueryOption(request, WINHTTP_OPTION_URL, finalUrl, &urlBytes) &&
                           WinHttpCrackUrl(finalUrl, 0, 0, &fc) && fc.nScheme == INTERNET_SCHEME_HTTPS &&
                           IsTrustedDownloadHost(finalHost);
            if (status == 200 && trusted) {
                ok = true;
                size_t total = 0;
                std::vector<char> chunk(64 * 1024);
                for (;;) {
                    if (stop && *stop) {
                        ok = false;
                        break;
                    }
                    DWORD read = 0;
                    if (!WinHttpReadData(request, chunk.data(), (DWORD)chunk.size(), &read)) {
                        ok = false;
                        break;
                    }
                    if (read == 0) break;
                    total += read;
                    if (total > cap) {
                        ok = false;
                        break;
                    }
                    if (file) {
                        DWORD written = 0;
                        if (!WriteFile(file, chunk.data(), read, &written, nullptr) || written != read) {
                            ok = false;
                            break;
                        }
                    } else {
                        body->append(chunk.data(), read);
                    }
                }
            } else if (!trusted) {
                Log(L"update: refused data from %ls", finalUrl);
            }
        }
    }
    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return ok;
}

bool Sha256File(const std::wstring& path, std::wstring* hex) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    bool ok = BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) &&
              BCRYPT_SUCCESS(BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0));
    std::vector<unsigned char> buf(256 * 1024);
    while (ok) {
        DWORD read = 0;
        if (!ReadFile(f, buf.data(), (DWORD)buf.size(), &read, nullptr)) ok = false;
        if (!ok || read == 0) break;
        ok = BCRYPT_SUCCESS(BCryptHashData(hash, buf.data(), read, 0));
    }
    unsigned char digest[32];
    ok = ok && BCRYPT_SUCCESS(BCryptFinishHash(hash, digest, sizeof(digest), 0));
    if (hash) BCryptDestroyHash(hash);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    CloseHandle(f);
    if (!ok) return false;
    hex->clear();
    for (unsigned char b : digest) {
        wchar_t two[3];
        swprintf(two, 3, L"%02x", b);
        *hex += two;
    }
    return true;
}

std::wstring UpdatesDir() {
    std::wstring dir = LocalDataDir() + L"\\updates";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

std::wstring StatePath() {
    return LocalDataDir() + L"\\update.json";
}

}  // namespace

// ---------------------------------------------------------------------------
// Pure helpers
// ---------------------------------------------------------------------------

int CompareVersions(const std::wstring& a, const std::wstring& b) {
    auto parts = [](const std::wstring& v, unsigned out[4], bool* pre) {
        size_t i = (!v.empty() && (v[0] == L'v' || v[0] == L'V')) ? 1 : 0;
        *pre = false;
        for (int p = 0; p < 4; ++p) {
            out[p] = 0;
            while (i < v.size() && iswdigit(v[i])) out[p] = out[p] * 10 + (unsigned)(v[i++] - L'0');
            if (i < v.size() && v[i] == L'.') {
                ++i;
                continue;
            }
            for (int q = p + 1; q < 4; ++q) out[q] = 0;
            *pre = i < v.size() && v[i] == L'-';   // 1.2.0-beta comes before 1.2.0
            break;
        }
    };
    unsigned pa[4], pb[4];
    bool prea, preb;
    parts(a, pa, &prea);
    parts(b, pb, &preb);
    for (int i = 0; i < 4; ++i) {
        if (pa[i] != pb[i]) return pa[i] < pb[i] ? -1 : 1;
    }
    if (prea != preb) return prea ? -1 : 1;
    return 0;
}

bool ParseLatestRelease(const std::wstring& text, Release* out) {
    json::Value root;
    if (!json::Parse(text, &root) || root.type != json::Value::Object) return false;
    if (root.GetBool(L"draft", false) || root.GetBool(L"prerelease", false)) return false;
    const json::Value* tag = root.Get(L"tag_name");
    const json::Value* page = root.Get(L"html_url");
    const json::Value* assets = root.Get(L"assets");
    if (!tag || tag->type != json::Value::String || !assets || assets->type != json::Value::Array) return false;
    Release r;
    r.version = tag->str;
    if (!r.version.empty() && (r.version[0] == L'v' || r.version[0] == L'V')) r.version.erase(0, 1);
    if (!IsVersion(r.version)) return false;
    if (page && page->type == json::Value::String && page->str.rfind(L"https://github.com/", 0) == 0) {
        r.notesUrl = page->str;
    }
    for (const json::Value& asset : assets->items) {
        const json::Value* name = asset.Get(L"name");
        const json::Value* url = asset.Get(L"browser_download_url");
        if (!name || name->type != json::Value::String || !url || url->type != json::Value::String) continue;
        if (url->str.rfind(kDownloadPrefix, 0) != 0 || !IsSafeFileName(name->str)) continue;
        std::wstring lower = Lower(name->str);
        if (lower == L"sha256sums.txt") {
            r.sumsUrl = url->str;
        } else if (lower.rfind(L"macwinsetup-", 0) == 0 && lower.size() > 4 &&
                   lower.compare(lower.size() - 4, 4, L".exe") == 0) {
            r.installerName = name->str;
            r.installerUrl = url->str;
        }
    }
    if (r.installerUrl.empty() || r.sumsUrl.empty()) return false;
    *out = r;
    return true;
}

std::wstring FindSha256(const std::string& sums, const std::wstring& fileName) {
    std::wstring text = Widen(sums), want = Lower(fileName);
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find(L'\n', pos);
        if (end == std::wstring::npos) end = text.size();
        std::wstring line = text.substr(pos, end - pos);
        pos = end + 1;
        while (!line.empty() && (line.back() == L'\r' || line.back() == L' ')) line.pop_back();
        if (line.size() < 66) continue;
        std::wstring hash = Lower(line.substr(0, 64));
        size_t name = line.find_first_not_of(L" *", 64);
        if (name == std::wstring::npos || Lower(line.substr(name)) != want) continue;
        bool hex = true;
        for (wchar_t c : hash) hex = hex && iswxdigit(c);
        if (hex) return hash;
    }
    return L"";
}

bool IsTrustedDownloadHost(const std::wstring& rawHost) {
    std::wstring host = Lower(rawHost);
    const std::wstring cdn = L".githubusercontent.com";
    return host == L"github.com" || host == L"api.github.com" ||
           (host.size() > cdn.size() && host.compare(host.size() - cdn.size(), cdn.size(), cdn) == 0);
}

// ---------------------------------------------------------------------------
// Updater
// ---------------------------------------------------------------------------

void Updater::Start(HWND notify, UINT msg) {
    notify_ = notify;
    msg_ = msg;
    LoadState();
    wake_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    thread_ = std::thread([this] { ThreadMain(); });
}

void Updater::Stop() {
    if (!thread_.joinable()) return;
    stop_ = true;
    SetEvent(wake_);
    thread_.join();   // a download notices stop_ between 64 KB chunks
    CloseHandle(wake_);
    wake_ = nullptr;
}

void Updater::Check(bool manual) {
    if (!manual) {
        AcquireSRWLockShared(&lock_);
        ULONGLONG last = lastCheckFiletime_;
        ReleaseSRWLockShared(&lock_);
        ULONGLONG now = NowFiletime();
        if (last && now > last && (now - last) / 10000 < kCheckIntervalMs) return;
    }
    InterlockedExchange(&wantCheck_, manual ? 2 : 1);
    SetEvent(wake_);
}

void Updater::Download() {
    InterlockedExchange(&wantDownload_, 1);
    SetEvent(wake_);
}

std::wstring Updater::Status() {
    AcquireSRWLockShared(&lock_);
    std::wstring s = status_;
    ReleaseSRWLockShared(&lock_);
    return s;
}

Release Updater::Available() {
    AcquireSRWLockShared(&lock_);
    Release r = available_;
    ReleaseSRWLockShared(&lock_);
    return r;
}

std::wstring Updater::InstallerPath() {
    AcquireSRWLockShared(&lock_);
    std::wstring p = installerPath_;
    ReleaseSRWLockShared(&lock_);
    return p;
}

bool Updater::ShouldNotify() {
    AcquireSRWLockExclusive(&lock_);
    bool notify = status_ == L"available" && !available_.version.empty() && notifiedVersion_ != available_.version;
    if (notify) notifiedVersion_ = available_.version;
    ReleaseSRWLockExclusive(&lock_);
    if (notify) SaveState();
    return notify;
}

std::wstring Updater::UpdatedFrom() {
    AcquireSRWLockExclusive(&lock_);
    std::wstring from;
    from.swap(updatedFrom_);
    ReleaseSRWLockExclusive(&lock_);
    return from;
}

void Updater::SetInstalling() {
    SetStatus(L"installing");
}

void Updater::SetFailed() {
    SetStatus(L"failedInstall");
}

void Updater::ThreadMain() {
    while (!stop_) {
        WaitForSingleObject(wake_, INFINITE);
        if (stop_) break;
        if (LONG check = InterlockedExchange(&wantCheck_, 0)) DoCheck(check == 2);
        if (InterlockedExchange(&wantDownload_, 0)) DoDownload();
    }
}

void Updater::DoCheck(bool manual) {
    std::wstring before = Status();
    if (before == L"downloading" || before == L"installing") return;
    SetStatus(L"checking", manual);
    std::string body;
    DWORD http = 0;
    Release rel;
    bool ok = HttpGet(kLatestReleaseUrl, kMaxApiBytes, &body, nullptr, &stop_, &http);
    bool parsed = ok && ParseLatestRelease(Widen(body), &rel);
    if (!parsed) {
        // 404: nothing released yet (or no installer in it) - that is "up to date".
        if (ok || http == 404) {
            Log(L"update: no usable release (HTTP %lu)", http);
        } else {
            Log(L"update: check failed (HTTP %lu, error %lu)", http, GetLastError());
            SetStatus(manual ? L"failed" : (before == L"checking" ? L"idle" : before));
            return;
        }
    }
    AcquireSRWLockExclusive(&lock_);
    lastCheck_ = NowIso(&lastCheckFiletime_);
    bool newer = parsed && CompareVersions(rel.version, MACWIN_VERSION_W) > 0;
    if (newer) available_ = rel;
    else available_ = Release();
    ReleaseSRWLockExclusive(&lock_);
    Log(L"update: latest %ls, this is %ls", parsed ? rel.version.c_str() : L"-", MACWIN_VERSION_W);
    SetStatus(newer ? L"available" : L"upToDate");
}

void Updater::DoDownload() {
    Release rel = Available();
    if (rel.version.empty()) return;
    SetStatus(L"downloading");

    std::string sums;
    std::wstring want;
    if (HttpGet(rel.sumsUrl, 64 * 1024, &sums, nullptr, &stop_, nullptr)) want = FindSha256(sums, rel.installerName);
    if (want.empty()) {
        Log(L"update: no SHA-256 for %ls in SHA256SUMS.txt", rel.installerName.c_str());
        SetStatus(L"failedInstall");
        return;
    }

    std::wstring dir = UpdatesDir();
    // Old downloads go first.
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) DeleteFileW((dir + L"\\" + fd.cFileName).c_str());
        } while (FindNextFileW(find, &fd));
        FindClose(find);
    }
    std::wstring path = dir + L"\\" + rel.installerName;
    std::wstring partial = path + L".part";
    HANDLE file = CreateFileW(partial.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    bool ok = file != INVALID_HANDLE_VALUE &&
              HttpGet(rel.installerUrl, kMaxInstallerBytes, nullptr, file, &stop_, nullptr);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);

    std::wstring got;
    ok = ok && Sha256File(partial, &got);
    if (ok && got != want) {
        Log(L"update: SHA-256 mismatch for %ls (%ls, expected %ls)", rel.installerName.c_str(), got.c_str(),
            want.c_str());
        ok = false;
    }
    ok = ok && MoveFileExW(partial.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
    if (!ok) {
        DeleteFileW(partial.c_str());
        if (!stop_) Log(L"update: download of %ls failed", rel.installerName.c_str());
        SetStatus(L"failedInstall");
        return;
    }
    Log(L"update: %ls downloaded and verified", rel.installerName.c_str());
    AcquireSRWLockExclusive(&lock_);
    installerPath_ = path;
    ReleaseSRWLockExclusive(&lock_);
    if (notify_) PostMessageW(notify_, msg_, kInstallerReady, 0);
}

void Updater::SetStatus(const std::wstring& status, bool notify) {
    AcquireSRWLockExclusive(&lock_);
    status_ = status;
    ReleaseSRWLockExclusive(&lock_);
    SaveState();
    if (notify && notify_) PostMessageW(notify_, msg_, kStateChanged, 0);
}

void Updater::LoadState() {
    std::wstring text;
    HANDLE f = CreateFileW(StatePath().c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        char buf[16384];
        DWORD read = 0;
        if (ReadFile(f, buf, sizeof(buf), &read, nullptr)) text = Widen(std::string(buf, read));
        CloseHandle(f);
    }
    json::Value root;
    if (text.empty() || !json::Parse(text, &root) || root.type != json::Value::Object) return;
    auto str = [&](const wchar_t* key) {
        const json::Value* v = root.Get(key);
        return v && v->type == json::Value::String ? v->str : std::wstring();
    };
    lastCheck_ = str(L"lastCheck");
    lastCheckFiletime_ = ParseIso(lastCheck_);
    notifiedVersion_ = str(L"notifiedVersion");
    // Just updated: the installer ran and this is the version it installed.
    std::wstring from = str(L"installingFrom"), to = str(L"installingTo");
    if (str(L"status") == L"installing" && !to.empty() && CompareVersions(to, MACWIN_VERSION_W) == 0 &&
        CompareVersions(from, MACWIN_VERSION_W) < 0) {
        updatedFrom_ = from;
    }
    status_ = lastCheck_.empty() ? L"idle" : L"upToDate";   // the next check says more
}

void Updater::SaveState() {
    AcquireSRWLockShared(&lock_);
    std::wstring t = L"{\n";
    t += L"  \"status\": " + json::Quote(status_) + L",\n";
    t += L"  \"current\": " + json::Quote(MACWIN_VERSION_W) + L",\n";
    t += L"  \"latest\": " + json::Quote(available_.version) + L",\n";
    t += L"  \"notesUrl\": " + json::Quote(available_.notesUrl) + L",\n";
    t += L"  \"lastCheck\": " + json::Quote(lastCheck_) + L",\n";
    t += L"  \"notifiedVersion\": " + json::Quote(notifiedVersion_);
    if (status_ == L"installing") {
        t += L",\n  \"installingFrom\": " + json::Quote(MACWIN_VERSION_W);
        t += L",\n  \"installingTo\": " + json::Quote(available_.version);
    }
    t += L"\n}\n";
    ReleaseSRWLockShared(&lock_);

    std::string bytes = Narrow(t);
    std::wstring path = StatePath(), tmp = path + L".tmp";
    static SRWLOCK fileLock = SRWLOCK_INIT;   // both threads save
    AcquireSRWLockExclusive(&fileLock);
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        bool ok = WriteFile(f, bytes.data(), (DWORD)bytes.size(), &written, nullptr) && written == bytes.size();
        CloseHandle(f);
        if (ok) MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
    }
    ReleaseSRWLockExclusive(&fileLock);
}
