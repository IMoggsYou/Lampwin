// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

#include "host/island/island_sources.h"
// winsock2 before windows.h's own winsock: iphlpapi needs the newer one.
#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <wctype.h>
#include <atomic>
#include <vector>
#include "host/log.h"

namespace island {

namespace {

const wchar_t kConsentStore[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore";
const wchar_t kRadioState[] = L"SYSTEM\\CurrentControlSet\\Control\\RadioManagement\\SystemRadioState";
const ULONG kWifi = 71;                     // IF_TYPE_IEEE80211
const ULONG kWwan = 243, kWwan2 = 244;      // IF_TYPE_WWANPP, IF_TYPE_WWANPP2

// Core Audio's ids, spelled out: MinGW doesn't attach them to all of its declarations.
const CLSID kDeviceEnumeratorClass = {0xbcde0395, 0xe52f, 0x467c, {0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e}};
const IID kDeviceEnumerator = {0xa95664d2, 0x9614, 0x4f35, {0xa7, 0x46, 0xde, 0x8d, 0xb6, 0x36, 0x17, 0xe6}};
const IID kEndpointVolume = {0x5cdf2c82, 0x841e, 0x4546, {0x97, 0x22, 0x0c, 0xf7, 0x40, 0x78, 0x22, 0x9a}};
const IID kNotificationClient = {0x7991eec9, 0x7e89, 0x4d85, {0x83, 0x90, 0x6c, 0x70, 0x3c, 0xec, 0x60, 0xc0}};
const IID kVolumeCallback = {0x657804fa, 0xd6ad, 0x4496, {0x8a, 0x60, 0x35, 0x27, 0x52, 0xaf, 0x4f, 0x89}};
const IID kUnknown = {0x00000000, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

// A program using a capability now (Windows sets LastUsedTimeStop when it lets go).
struct Use {
    std::wstring key;   // "C:#Program Files#...#Discord.exe" or a package family name
    bool packaged = false;
    INT64 start = 0;
};

INT64 Qword(HKEY key, const wchar_t* sub, const wchar_t* name) {
    ULONGLONG v = 0;
    DWORD size = sizeof(v);
    return RegGetValueW(key, sub, name, RRF_RT_REG_QWORD, nullptr, &v, &size) == ERROR_SUCCESS ? (INT64)v : 0;
}

void ReadUses(HKEY key, bool packaged, std::vector<Use>* out) {
    wchar_t name[512];
    for (DWORD i = 0;; ++i) {
        DWORD len = ARRAYSIZE(name);
        if (RegEnumKeyExW(key, i, name, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
        if (packaged && _wcsicmp(name, L"NonPackaged") == 0) continue;
        const INT64 start = Qword(key, name, L"LastUsedTimeStart");
        if (start != 0 && Qword(key, name, L"LastUsedTimeStop") == 0) out->push_back({name, packaged, start});
    }
}

std::vector<Use> InUse(const wchar_t* capability) {
    std::vector<Use> uses;
    const std::wstring path = std::wstring(kConsentStore) + L"\\" + capability;
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, KEY_READ, &key) == ERROR_SUCCESS) {
        ReadUses(key, true, &uses);
        RegCloseKey(key);
    }
    if (RegOpenKeyExW(HKEY_CURRENT_USER, (path + L"\\NonPackaged").c_str(), 0, KEY_READ, &key) == ERROR_SUCCESS) {
        ReadUses(key, false, &uses);
        RegCloseKey(key);
    }
    return uses;
}

std::wstring Lower(std::wstring s) {
    for (wchar_t& c : s) c = (wchar_t)towlower(c);
    return s;
}

// The exe of a desktop program from its consent key ("C:#Program Files#x.exe").
std::wstring PathOf(const std::wstring& key) {
    std::wstring p = key;
    for (wchar_t& c : p) {
        if (c == L'#') c = L'\\';
    }
    return p;
}

// A crashed program never writes LastUsedTimeStop: only programs that run count.
bool Running(const std::wstring& exe) {
    const std::wstring want = Lower(exe);
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return true;
    const wchar_t* base = wcsrchr(want.c_str(), L'\\');
    base = base ? base + 1 : want.c_str();
    PROCESSENTRY32W pe = {sizeof(pe)};
    bool found = false;
    for (BOOL ok = Process32FirstW(snap, &pe); ok && !found; ok = Process32NextW(snap, &pe)) {
        if (_wcsicmp(pe.szExeFile, base) != 0) continue;
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
        if (!h) {
            found = true;   // can't look at it (elevated): the name will do
            continue;
        }
        wchar_t image[MAX_PATH];
        DWORD n = MAX_PATH;
        if (QueryFullProcessImageNameW(h, 0, image, &n)) found = Lower(image) == want;
        CloseHandle(h);
    }
    CloseHandle(snap);
    return found;
}

// What people call the program: its description ("Discord"), else its file name.
std::wstring DesktopName(const std::wstring& exe) {
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(exe.c_str(), &ignored);
    if (size) {
        std::vector<BYTE> info(size);
        struct Translation {
            WORD language, codepage;
        }* tr = nullptr;
        UINT len = 0;
        if (GetFileVersionInfoW(exe.c_str(), 0, size, info.data()) &&
            VerQueryValueW(info.data(), L"\\VarFileInfo\\Translation", (void**)&tr, &len) && len >= sizeof(*tr)) {
            wchar_t query[64];
            swprintf(query, ARRAYSIZE(query), L"\\StringFileInfo\\%04x%04x\\FileDescription", tr->language,
                     tr->codepage);
            wchar_t* text = nullptr;
            if (VerQueryValueW(info.data(), query, (void**)&text, &len) && text && len > 1) {
                std::wstring d(text);
                while (!d.empty() && d.back() == L' ') d.pop_back();
                if (!d.empty() && d.size() <= 40) return d;
            }
        }
    }
    const wchar_t* base = wcsrchr(exe.c_str(), L'\\');
    std::wstring name = base ? base + 1 : exe;
    if (name.size() > 4 && _wcsicmp(name.c_str() + name.size() - 4, L".exe") == 0) name.resize(name.size() - 4);
    if (!name.empty()) name[0] = (wchar_t)towupper(name[0]);
    return name;
}

// A Store app's name from its package family ("5319275A.WhatsAppDesktop_cv1g1gvanyjgm").
std::wstring PackagedName(const std::wstring& family) {
    std::wstring name = family.substr(0, family.find(L'_'));
    const size_t dot = name.rfind(L'.');
    if (dot != std::wstring::npos) name = name.substr(dot + 1);
    static const struct {
        const wchar_t* id;
        const wchar_t* name;
    } kKnown[] = {{L"whatsappdesktop", L"WhatsApp"}, {L"microsoftteams", L"Microsoft Teams"},
                  {L"msteams", L"Microsoft Teams"},   {L"skypeapp", L"Skype"},
                  {L"telegramdesktop", L"Telegram"},  {L"zoom", L"Zoom"}};
    const std::wstring lower = Lower(name);
    for (const auto& k : kKnown) {
        if (lower == k.id) return k.name;
    }
    return name;
}

struct FindWindowOf {
    DWORD pid;
    HWND found;
};

BOOL CALLBACK FindMainWindow(HWND h, LPARAM lp) {
    auto* f = (FindWindowOf*)lp;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid != f->pid || !IsWindowVisible(h) || GetWindow(h, GW_OWNER) ||
        (GetWindowLongW(h, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) || GetWindowTextLengthW(h) == 0) {
        return TRUE;
    }
    f->found = h;
    return FALSE;
}

bool Headphones(IMMDevice* device, std::wstring* name) {
    ComPtr<IPropertyStore> store;
    if (FAILED(device->OpenPropertyStore(STGM_READ, store.put()))) return false;
    // PKEY_Device_FriendlyName and PKEY_AudioEndpoint_FormFactor.
    const PROPERTYKEY kName = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};
    const PROPERTYKEY kForm = {{0x1da5d803, 0xd492, 0x4edd, {0x8c, 0x23, 0xe0, 0xc0, 0xff, 0xee, 0x7f, 0x0e}}, 0};
    PROPVARIANT v;
    PropVariantInit(&v);
    if (SUCCEEDED(store->GetValue(kName, &v)) && v.vt == VT_LPWSTR && v.pwszVal) *name = v.pwszVal;
    PropVariantClear(&v);
    bool headphones = false;
    if (SUCCEEDED(store->GetValue(kForm, &v)) && v.vt == VT_UI4) headphones = v.ulVal == 3 || v.ulVal == 5;
    PropVariantClear(&v);
    return headphones;
}

ComPtr<IAudioEndpointVolume> Volume(IMMDeviceEnumerator* devices, EDataFlow flow, ERole role) {
    ComPtr<IMMDevice> device;
    ComPtr<IAudioEndpointVolume> volume;
    if (devices && SUCCEEDED(devices->GetDefaultAudioEndpoint(flow, role, device.put())) && device) {
        device->Activate(kEndpointVolume, CLSCTX_ALL, nullptr, volume.put_void());
    }
    return volume;
}

// Core Audio calls these on its own threads: they only post.
class OutputClient final : public IMMNotificationClient {
public:
    OutputClient(HWND target, UINT message) : target_(target), message_(message) {}
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG n = --refs_;
        if (n == 0) delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (iid == kUnknown || iid == kNotificationClient) {
            *out = static_cast<IMMNotificationClient*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override {
        if (flow == eRender && role == eConsole) PostMessageW(target_, message_, kOutputChanged, 0);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }

private:
    std::atomic<ULONG> refs_{1};
    HWND target_;
    UINT message_;
};

class MuteCallback final : public IAudioEndpointVolumeCallback {
public:
    MuteCallback(HWND target, UINT message) : target_(target), message_(message) {}
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG n = --refs_;
        if (n == 0) delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (iid == kUnknown || iid == kVolumeCallback) {
            *out = static_cast<IAudioEndpointVolumeCallback*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    // Every volume step comes here too: only a change of the mute is passed on.
    HRESULT STDMETHODCALLTYPE OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA data) override {
        if (!data) return S_OK;
        const int muted = data->bMuted ? 1 : 0;
        if (muted != last_.exchange(muted)) PostMessageW(target_, message_, kMuteChanged, muted);
        return S_OK;
    }
    void Seed(bool muted) { last_ = muted ? 1 : 0; }

private:
    std::atomic<ULONG> refs_{1};
    std::atomic<int> last_{-1};
    HWND target_;
    UINT message_;
};

}  // namespace

Capabilities DetectCapabilities() {
    Capabilities c;
    SYSTEM_POWER_STATUS power;
    // 128: no system battery; 255: unknown (a desktop as a rule).
    c.battery = GetSystemPowerStatus(&power) && power.BatteryFlag != 128 && power.BatteryFlag != 255;
    ULONG size = 16 * 1024;
    std::vector<BYTE> buffer(size);
    const ULONG flags =
        GAA_FLAG_SKIP_UNICAST | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG r = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, (IP_ADAPTER_ADDRESSES*)buffer.data(), &size);
    if (r == ERROR_BUFFER_OVERFLOW) {
        buffer.resize(size);
        r = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, (IP_ADAPTER_ADDRESSES*)buffer.data(), &size);
    }
    if (r == NO_ERROR) {
        for (auto* a = (IP_ADAPTER_ADDRESSES*)buffer.data(); a; a = a->Next) {
            if (a->IfType == kWifi) c.wireless = true;
            if (a->IfType == kWwan || a->IfType == kWwan2) c.wireless = c.cellular = true;
        }
    }
    return c;
}

CallInfo ReadCall() {
    CallInfo call;
    wchar_t self[MAX_PATH] = L"", windows[MAX_PATH] = L"";
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    GetWindowsDirectoryW(windows, MAX_PATH);
    const std::wstring windowsDir = Lower(windows) + L"\\";
    // The program that took the microphone last is the call.
    for (const Use& u : InUse(L"microphone")) {
        if (u.start <= call.start) continue;
        if (u.packaged) {
            call = {true, PackagedName(u.key), L"", u.start, false};
            continue;
        }
        const std::wstring exe = PathOf(u.key);
        const std::wstring lower = Lower(exe);
        // Windows' own parts (voice typing, the sound settings' test) and Lampwin aren't calls.
        if (lower.compare(0, windowsDir.size(), windowsDir) == 0 || _wcsicmp(exe.c_str(), self) == 0) continue;
        if (!Running(exe)) continue;
        call = {true, DesktopName(exe), exe, u.start, false};
    }
    if (call.active) {
        for (const Use& u : InUse(L"webcam")) {
            const bool same = u.packaged ? PackagedName(u.key) == call.app
                                         : _wcsicmp(PathOf(u.key).c_str(), call.path.c_str()) == 0;
            call.camera = call.camera || same;
        }
    }
    return call;
}

bool ShowCallWindow(const CallInfo& call) {
    if (call.path.empty()) return false;
    const std::wstring want = Lower(call.path);
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    const wchar_t* base = wcsrchr(want.c_str(), L'\\');
    base = base ? base + 1 : want.c_str();
    HWND found = nullptr;
    PROCESSENTRY32W pe = {sizeof(pe)};
    for (BOOL ok = Process32FirstW(snap, &pe); ok && !found; ok = Process32NextW(snap, &pe)) {
        if (_wcsicmp(pe.szExeFile, base) != 0) continue;
        FindWindowOf f = {pe.th32ProcessID, nullptr};
        EnumWindows(FindMainWindow, (LPARAM)&f);
        found = f.found;
    }
    CloseHandle(snap);
    if (!found) return false;
    if (IsIconic(found)) ShowWindow(found, SW_RESTORE);
    // A click in the island is the user's input: Windows lets the window come forward.
    SetForegroundWindow(found);
    return true;
}

MirrorInfo ReadMirroring() {
    MirrorInfo m;
    UINT32 pathCount = 0, modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS) return m;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) !=
        ERROR_SUCCESS) {
        return m;
    }
    paths.resize(pathCount);
    auto nameOf = [](const DISPLAYCONFIG_PATH_INFO& p) {
        DISPLAYCONFIG_TARGET_DEVICE_NAME name = {};
        name.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        name.header.size = sizeof(name);
        name.header.adapterId = p.targetInfo.adapterId;
        name.header.id = p.targetInfo.id;
        return DisplayConfigGetDeviceInfo(&name.header) == ERROR_SUCCESS ? std::wstring(name.monitorFriendlyDeviceName)
                                                                         : std::wstring();
    };
    for (size_t i = 0; i < paths.size(); ++i) {
        const DISPLAYCONFIG_PATH_INFO& p = paths[i];
        if (p.targetInfo.outputTechnology == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_MIRACAST) {
            return {true, true, nameOf(p)};
        }
        // Two displays showing the same picture: the same source.
        for (size_t j = i + 1; j < paths.size(); ++j) {
            const DISPLAYCONFIG_PATH_INFO& q = paths[j];
            const bool sameAdapter = p.sourceInfo.adapterId.LowPart == q.sourceInfo.adapterId.LowPart &&
                                     p.sourceInfo.adapterId.HighPart == q.sourceInfo.adapterId.HighPart;
            if (sameAdapter && p.sourceInfo.id == q.sourceInfo.id) {
                // The other screen is the one it is mirrored to, not the laptop's own.
                const bool pInternal = p.targetInfo.outputTechnology == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL;
                m = {true, false, nameOf(pInternal ? q : p)};
            }
        }
    }
    return m;
}

void StopMirroring(const MirrorInfo& m) {
    if (!m.active) return;
    if (!m.wireless) {
        SetDisplayConfig(0, nullptr, 0, nullptr, SDC_APPLY | SDC_TOPOLOGY_EXTEND);
        return;
    }
    // Win + K: Windows' Cast panel, which has the Disconnect button.
    INPUT in[4] = {};
    for (INPUT& i : in) i.type = INPUT_KEYBOARD;
    in[0].ki.wVk = VK_LWIN;
    in[1].ki.wVk = 'K';
    in[2].ki.wVk = 'K';
    in[2].ki.dwFlags = KEYEVENTF_KEYUP;
    in[3].ki.wVk = VK_LWIN;
    in[3].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(4, in, sizeof(INPUT));
}

int AirplaneMode() {
    DWORD v = 0, size = sizeof(v);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, kRadioState, nullptr, RRF_RT_REG_DWORD, nullptr, &v, &size) != ERROR_SUCCESS) {
        return -1;
    }
    return v == 1 ? 1 : 0;
}

int CellularState() {
    ULONG size = 16 * 1024;
    std::vector<BYTE> buffer(size);
    const ULONG flags =
        GAA_FLAG_SKIP_UNICAST | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG r = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, (IP_ADAPTER_ADDRESSES*)buffer.data(), &size);
    if (r == ERROR_BUFFER_OVERFLOW) {
        buffer.resize(size);
        r = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, (IP_ADAPTER_ADDRESSES*)buffer.data(), &size);
    }
    if (r != NO_ERROR) return -1;
    int state = -1;
    for (auto* a = (IP_ADAPTER_ADDRESSES*)buffer.data(); a; a = a->Next) {
        if (a->IfType != kWwan && a->IfType != kWwan2) continue;
        if (a->OperStatus == IfOperStatusUp) return 1;
        state = 0;
    }
    return state;
}

bool FullScreenBusy() {
    QUERY_USER_NOTIFICATION_STATE state = QUNS_ACCEPTS_NOTIFICATIONS;
    if (FAILED(SHQueryUserNotificationState(&state))) return false;
    return state == QUNS_BUSY || state == QUNS_RUNNING_D3D_FULL_SCREEN || state == QUNS_PRESENTATION_MODE;
}

void PressMediaKey(WORD vk) {
    INPUT in[2] = {};
    in[0].type = in[1].type = INPUT_KEYBOARD;
    in[0].ki.wVk = in[1].ki.wVk = vk;
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
}

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------

bool Audio::Start(HWND target, UINT message) {
    target_ = target;
    message_ = message;
    if (FAILED(CoCreateInstance(kDeviceEnumeratorClass, nullptr, CLSCTX_ALL, kDeviceEnumerator, devices_.put_void()))) {
        Log(L"island: no Core Audio");
        return false;
    }
    client_ = ComPtr<IMMNotificationClient>::Attach(new OutputClient(target, message));
    devices_->RegisterEndpointNotificationCallback(client_.get());
    FollowOutput();
    return true;
}

void Audio::Stop() {
    if (output_ && volumeCallback_) output_->UnregisterControlChangeNotify(volumeCallback_.get());
    if (devices_ && client_) devices_->UnregisterEndpointNotificationCallback(client_.get());
    output_.reset();
    volumeCallback_.reset();
    client_.reset();
    devices_.reset();
}

void Audio::FollowOutput() {
    if (output_ && volumeCallback_) output_->UnregisterControlChangeNotify(volumeCallback_.get());
    output_ = Volume(devices_.get(), eRender, eConsole);
    if (!output_) return;
    auto* callback = new MuteCallback(target_, message_);
    BOOL muted = FALSE;
    output_->GetMute(&muted);
    callback->Seed(muted != FALSE);
    volumeCallback_ = ComPtr<IAudioEndpointVolumeCallback>::Attach(callback);
    output_->RegisterControlChangeNotify(volumeCallback_.get());
}

std::wstring Audio::OutputName(bool* headphones, std::wstring* id) {
    *headphones = false;
    ComPtr<IMMDevice> device;
    if (!devices_ || FAILED(devices_->GetDefaultAudioEndpoint(eRender, eConsole, device.put())) || !device) {
        return std::wstring();
    }
    LPWSTR raw = nullptr;
    if (SUCCEEDED(device->GetId(&raw)) && raw) {
        *id = raw;
        CoTaskMemFree(raw);
    }
    std::wstring name;
    *headphones = Headphones(device.get(), &name);
    return name;
}

bool Audio::Muted() {
    BOOL muted = FALSE;
    return output_ && SUCCEEDED(output_->GetMute(&muted)) && muted;
}

void Audio::SetMuted(bool muted) {
    if (output_) output_->SetMute(muted, nullptr);
}

bool Audio::MicMuted() {
    ComPtr<IAudioEndpointVolume> mic = Volume(devices_.get(), eCapture, eCommunications);
    BOOL muted = FALSE;
    return mic && SUCCEEDED(mic->GetMute(&muted)) && muted;
}

void Audio::SetMicMuted(bool muted) {
    ComPtr<IAudioEndpointVolume> mic = Volume(devices_.get(), eCapture, eCommunications);
    if (mic) mic->SetMute(muted, nullptr);
}

// ---------------------------------------------------------------------------
// Watcher
// ---------------------------------------------------------------------------

namespace {

struct NetworkTarget {
    HWND target;
    UINT message;
};
NetworkTarget g_network = {};

void CALLBACK NetworkChanged(void*, MIB_IPINTERFACE_ROW*, MIB_NOTIFICATION_TYPE) {
    PostMessageW(g_network.target, g_network.message, kNetworkChanged, 0);
}

// A registry key that signals `event` when something under it changes (armed again after
// every signal).
struct Watched {
    HKEY root;
    std::wstring path;
    Change change;
    HKEY key = nullptr;
    HANDLE event = nullptr;
};

bool Arm(Watched& w) {
    if (!w.key && RegOpenKeyExW(w.root, w.path.c_str(), 0, KEY_NOTIFY | KEY_READ, &w.key) != ERROR_SUCCESS) {
        return false;
    }
    return RegNotifyChangeKeyValue(w.key, TRUE, REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_LAST_SET, w.event, TRUE) ==
           ERROR_SUCCESS;
}

}  // namespace

void Watcher::Start(HWND target, UINT message) {
    if (thread_.joinable()) return;
    stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stop_) return;
    g_network = {target, message};
    if (NotifyIpInterfaceChange(AF_UNSPEC, NetworkChanged, nullptr, FALSE, &network_) != NO_ERROR) network_ = nullptr;
    HANDLE stop = stop_;
    thread_ = std::thread([target, message, stop] {
        // The airplane mode key is the parent's (RadioManagement): its value is the key's own.
        std::vector<Watched> keys = {
            {HKEY_CURRENT_USER, std::wstring(kConsentStore) + L"\\microphone", kCallChanged},
            {HKEY_CURRENT_USER, std::wstring(kConsentStore) + L"\\webcam", kCallChanged},
            {HKEY_LOCAL_MACHINE, kRadioState, kAirplaneChanged},
        };
        std::vector<HANDLE> waits = {stop};
        for (Watched& w : keys) {
            w.event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (w.event && Arm(w)) waits.push_back(w.event);
        }
        for (;;) {
            DWORD r = WaitForMultipleObjects((DWORD)waits.size(), waits.data(), FALSE, INFINITE);
            if (r == WAIT_OBJECT_0 || r >= WAIT_OBJECT_0 + waits.size()) break;
            for (Watched& w : keys) {
                if (w.event != waits[r - WAIT_OBJECT_0]) continue;
                // Windows writes several values in a row: one post for the burst.
                Sleep(150);
                Arm(w);
                PostMessageW(target, message, w.change, 0);
            }
        }
        for (Watched& w : keys) {
            if (w.key) RegCloseKey(w.key);
            if (w.event) CloseHandle(w.event);
        }
    });
}

void Watcher::Stop() {
    if (network_) {
        CancelMibChangeNotify2(network_);
        network_ = nullptr;
    }
    if (thread_.joinable()) {
        SetEvent(stop_);
        thread_.join();
    }
    if (stop_) {
        CloseHandle(stop_);
        stop_ = nullptr;
    }
}

}  // namespace island
