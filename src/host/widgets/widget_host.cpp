// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

#include "host/widgets/widget_host.h"
#include <dwmapi.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <wtsapi32.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <memory>
#include "host/json.h"
#include "host/log.h"
#include "host/widgets/canvas.h"
#include "host/widgets/location.h"
#include "host/widgets/media.h"
#include "host/widgets/system_stats.h"
#include "host/widgets/weather.h"
#include "host/widgets/widget_layout.h"

namespace widgets {

namespace {

const wchar_t kControlClass[] = L"MacWinWidgets_v1";
const wchar_t kCardClass[] = L"MacWinWidget_v1";
const wchar_t kShadowClass[] = L"MacWinWidgetShadow_v1";

// To the widget thread's control window.
const UINT WM_APP_UPDATE = WM_APP + 1;    // lParam: Update*
const UINT WM_APP_WEATHER = WM_APP + 2;   // lParam: WeatherResult*
const UINT WM_APP_MEDIA = WM_APP + 3;     // lParam: MediaInfo*

const UINT_PTR kTickTimer = 1, kWeatherTimer = 2, kLayoutTimer = 3, kAnimTimer = 4;
const UINT kWeatherEveryMs = 30 * 60 * 1000;
const UINT kWeatherRetryMs = 3 * 60 * 1000;
const ULONGLONG kWeatherCacheMaxAge = 6ULL * 3600;   // seconds: older than this is not shown
const double kAnimMs = 200;
const UINT kSystemEveryMs = 2000;

// GUID_CONSOLE_DISPLAY_STATE (winnt.h), here to need no extra import library.
const GUID kDisplayState = {0x6fe69556, 0x704a, 0x47a0, {0x8f, 0x24, 0xc2, 0x8d, 0x93, 0x6f, 0xda, 0x47}};

struct Update {
    Config config;
    Texts texts;
    std::wstring locale;
    bool rtl = false;
};

struct Monitor {
    std::wstring name;
    RECT work;
    float scale;   // DPI / 96
    bool primary;
};

struct Widget {
    Item item;
    HWND shadow = nullptr, card = nullptr;
    bool destroying = false;   // we destroy the windows, not someone else
    Surface shadowSurface, cardSurface;
    bool shadowDrawn = false;
    // Where it is: its monitor (maybe not the saved one, if that is gone), slot and pixels.
    bool shown = false;
    std::wstring monitor;
    Slot slot = {};
    RECT rect = {};
    float unit = 0;
    // What the last drawing showed, to skip redraws nothing changed for.
    int drawnMinute = -1;
    int drawnDay = -1;
    // A clock's face as drawn last (pixels), and what it was drawn for.
    std::vector<BYTE> face;
    int faceWidth = 0, faceHeight = 0;
    bool faceDark = false, faceRtl = false;
    // The picture of a Photo widget, decoded at the card's size.
    ComPtr<IWICBitmapSource> photo;
    std::wstring photoPath;
    SIZE photoSize = {};
};

bool SameSpan(Span a, Span b) {
    return a.cols == b.cols && a.rows == b.rows;
}

ULONGLONG UnixNow() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER t = {{ft.dwLowDateTime, ft.dwHighDateTime}};
    return t.QuadPart / 10000000ULL - 11644473600ULL;
}

bool AppsUseDarkTheme() {
    DWORD value = 1, size = sizeof(value);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return value == 0;
}

bool ReadUtf8(const std::wstring& path, std::wstring* text) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    std::string bytes;
    LARGE_INTEGER size;
    bool ok = GetFileSizeEx(h, &size) && size.QuadPart > 0 && size.QuadPart < (1 << 20);
    if (ok) {
        bytes.resize((size_t)size.QuadPart);
        DWORD read = 0;
        ok = ReadFile(h, &bytes[0], (DWORD)bytes.size(), &read, nullptr) && read == bytes.size();
    }
    CloseHandle(h);
    if (!ok) return false;
    int n = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), nullptr, 0);
    text->assign((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), &(*text)[0], n);
    return true;
}

// Through a temporary file, so a reader never sees half of it.
void WriteUtf8(const std::wstring& path, const std::wstring& text) {
    int n = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    std::string bytes((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), &bytes[0], n, nullptr, nullptr);
    std::wstring tmp = path + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    bool ok = WriteFile(h, bytes.data(), (DWORD)bytes.size(), &written, nullptr) && written == bytes.size();
    CloseHandle(h);
    if (ok) MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}

double Num(const json::Value& o, const wchar_t* key) {
    const json::Value* v = o.Get(key);
    return v && v->type == json::Value::Number ? v->number : 0;
}

std::wstring Str(const json::Value& o, const wchar_t* key) {
    const json::Value* v = o.Get(key);
    return v && v->type == json::Value::String ? v->str : std::wstring();
}

// Which forecast a widget wants: "my location" or the chosen place, and the unit.
std::wstring WeatherKey(const Config& c) {
    wchar_t key[96];
    swprintf(key, ARRAYSIZE(key), L"%ls%.4f %.4f %ls", c.autoPlace ? L"auto " : L"",
             c.place.valid ? c.place.latitude : 0, c.place.valid ? c.place.longitude : 0, c.fahrenheit ? L"f" : L"c");
    return key;
}

// weather.json: the last forecast (shown at once after a restart) and the place it is for.
struct WeatherCache {
    std::wstring key;
    ULONGLONG fetched = 0;   // unix time
    std::wstring place;
    double latitude = 0, longitude = 0;
    bool located = false;    // Windows' location, not the chosen place
    std::wstring forecast;   // Open-Meteo's answer
};

bool LoadWeatherCache(WeatherCache* c) {
    std::wstring text;
    json::Value root;
    if (!ReadUtf8(LocalDataDir() + L"\\weather.json", &text) || !json::Parse(text, &root) ||
        root.type != json::Value::Object) {
        return false;
    }
    c->key = Str(root, L"key");
    c->fetched = (ULONGLONG)Num(root, L"fetched");
    c->place = Str(root, L"place");
    c->latitude = Num(root, L"latitude");
    c->longitude = Num(root, L"longitude");
    c->located = root.GetBool(L"located", false);
    c->forecast = Str(root, L"forecast");
    return true;
}

void SaveWeatherCache(const WeatherCache& c) {
    json::Value root = json::Object();
    json::Set(&root, L"key", json::String(c.key));
    json::Set(&root, L"fetched", json::Number((double)c.fetched));
    json::Set(&root, L"place", json::String(c.place));
    json::Set(&root, L"latitude", json::Number(c.latitude));
    json::Set(&root, L"longitude", json::Number(c.longitude));
    json::Set(&root, L"located", json::Bool(c.located));
    json::Set(&root, L"forecast", json::String(c.forecast));
    WriteUtf8(LocalDataDir() + L"\\weather.json", json::Serialize(root) + L"\n");
}

// location.json: what "my location" found, for the settings window to show.
void SaveLocationStatus(LocationStatus status, const std::wstring& place) {
    const wchar_t* names[] = {L"unknown", L"found", L"denied", L"unavailable", L"rough"};
    json::Value root = json::Object();
    json::Set(&root, L"status", json::String(names[(int)status]));
    json::Set(&root, L"place", json::String(place));
    WriteUtf8(LocalDataDir() + L"\\location.json", json::Serialize(root) + L"\n");
}

// What the fetch thread needs, copied: it never touches the host.
struct WeatherRequest {
    UINT id = 0;
    bool automatic = false;
    Place place;   // the chosen one
    bool fahrenheit = false;
    std::wstring language;
    // The last place Windows located: its name isn't looked up again while the PC stays there.
    bool known = false;
    double knownLatitude = 0, knownLongitude = 0;
    std::wstring knownName;
};

struct WeatherResult {
    UINT id = 0;
    LocationStatus location = LocationStatus::Unknown;
    bool hasPlace = false;
    bool located = false;
    double latitude = 0, longitude = 0;
    std::wstring place;
    bool ok = false;
    std::wstring forecast;
};

// On the fetch thread: where (Windows' location, else the chosen place), then the forecast.
WeatherResult* FetchWeatherFor(const WeatherRequest& r, HttpCancel* cancel) {
    auto* out = new WeatherResult();
    out->id = r.id;
    if (r.automatic) {
        double lat = 0, lon = 0, accuracy = 0;
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        out->location = WindowsLocation(&lat, &lon, &accuracy, cancel);
        CoUninitialize();
        // Only a guess from the internet address: a place the user chose knows better.
        if (out->location == LocationStatus::Found && accuracy > kRoughLocationMeters) {
            out->location = LocationStatus::Rough;
        }
        if (out->location == LocationStatus::Found || (out->location == LocationStatus::Rough && !r.place.valid)) {
            out->hasPlace = out->located = true;
            out->latitude = RoundCoordinate(lat);
            out->longitude = RoundCoordinate(lon);
            if (r.known && !r.knownName.empty() &&
                DistanceKm(out->latitude, out->longitude, r.knownLatitude, r.knownLongitude) < 3) {
                out->place = r.knownName;
            } else {
                std::wstring body;
                if (FetchJson(PlaceNameUrl(out->latitude, out->longitude, r.language), &body, cancel)) {
                    ParsePlaceName(body, &out->place);
                }
            }
        }
    }
    if (!out->hasPlace && r.place.valid) {
        out->hasPlace = true;
        out->latitude = r.place.latitude;
        out->longitude = r.place.longitude;
        out->place = r.place.name;
    }
    if (out->hasPlace) out->ok = FetchJson(ForecastUrl(out->latitude, out->longitude, r.fahrenheit), &out->forecast, cancel);
    return out;
}

// ---------------------------------------------------------------------------
// The widget thread
// ---------------------------------------------------------------------------

class Host;
thread_local Host* t_host = nullptr;   // this thread's host, for the widgets' windows

class Host {
public:
    Host(HWND notify, UINT msg) : notify_(notify), msg_(msg) {}
    ~Host();
    bool Create();
    void Run();
    HWND Control() const { return control_; }

private:
    static LRESULT CALLBACK ControlProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK CardProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT OnControl(UINT msg, WPARAM wp, LPARAM lp);
    LRESULT OnCard(HWND hwnd, Widget* w, UINT msg, WPARAM wp, LPARAM lp);

    void ApplyUpdate(Update* u);
    std::vector<Monitor> Monitors() const;
    const Monitor* Find(const std::vector<Monitor>& monitors, const std::wstring& name) const;
    float UnitFor(const Monitor& m) const { return m.scale * config_.scale / 100.0f; }
    std::vector<Slot> Taken(const std::wstring& monitor, const Widget* except) const;
    void Layout();
    void Show(Widget* w, const Monitor& m, const Grid& grid, const Slot& slot);
    void Hide(Widget* w);
    bool CreateWindows(Widget* w);
    void DestroyWindows(Widget* w);
    void RecreateAll();
    void Render(Widget* w);
    void RenderAll(bool onlyAuto = false);
    void Move(Widget* w, int x, int y);
    Scene SceneFor(Widget* w, const SYSTEMTIME& now);
    bool IsDark(const Widget* w) const;
    void Schedule();
    void Tick();
    bool Busy() const;
    void Suspend(bool locked, bool displayOff);

    void WeatherChanged();
    void FetchWeather();
    void OnWeather(WeatherResult* result);
    bool HasKind(Kind k) const;
    void MediaChanged();
    void OnMedia(MediaInfo* info);

    void StartDrag(Widget* w);
    void Drag(Widget* w, POINT cursor);
    void Drop(Widget* w, bool cancel);
    void UpdateDropTarget(const Monitor& m, const Grid& grid, const Slot& slot, float unit);
    void HideDropTarget();
    void Animate(Widget* w, POINT to);
    void AnimationStep();
    void FinishAnimation();
    void Click(Widget* w);
    void ShowMenu(Widget* w, POINT at);
    void SetSize(Widget* w, Size size);
    void Remove(Widget* w);
    void Persist();
    void Notify(Request r, LPARAM lp = 0);
    const std::wstring& T(const wchar_t* key) const;
    Widget* ById(const std::wstring& id) const;

    HWND notify_;
    UINT msg_;
    HWND control_ = nullptr;
    bool recreate_ = false;   // a widget's window was destroyed behind our back
    UINT taskbarCreated_ = 0;
    HPOWERNOTIFY displayNotify_ = nullptr;
    HKEY themeKey_ = nullptr;
    HANDLE themeEvent_ = nullptr;
    bool dark_ = false;

    Config config_;
    Texts texts_;
    std::wstring locale_;
    bool rtl_ = false;
    bool started_ = false;
    std::vector<std::unique_ptr<Widget>> widgets_;
    Graphics g_;

    bool locked_ = false, displayOff_ = false;
    SystemSampler sampler_;
    SystemStats stats_;
    ULONGLONG statsAt_ = 0;

    // Weather: the forecast shown, the place it is for, and the request in flight.
    Forecast forecast_;
    WeatherState weatherState_ = WeatherState::NoPlace;
    std::wstring weatherPlace_;
    std::wstring weatherKey_;
    UINT weatherRequest_ = 0;
    std::wstring fetchKey_;
    std::thread fetch_;
    HttpCancel cancel_;
    // The last place Windows located, and what the settings window was told about it.
    bool known_ = false;
    double knownLatitude_ = 0, knownLongitude_ = 0;
    std::wstring knownName_;
    LocationStatus reported_ = LocationStatus::Unknown;
    std::wstring reportedPlace_;

    // Now playing: watched only while such a widget is on screen.
    MediaWatcher media_;
    MediaInfo nowPlaying_;

    // Mouse: the widget pressed, and whether it is being dragged.
    Widget* pressed_ = nullptr;
    bool dragging_ = false;
    POINT pressAt_ = {}, pressOrigin_ = {};
    // Where a dragged widget would land.
    HWND dropTarget_ = nullptr;
    Surface dropSurface_;
    bool dropValid_ = false;
    std::wstring dropMonitor_;
    Slot dropSlot_ = {};
    float dropUnit_ = 0;
    // The glide into a slot after a drop.
    Widget* animated_ = nullptr;
    POINT animFrom_ = {}, animTo_ = {};
    LARGE_INTEGER animStart_ = {}, qpf_ = {};
};

Host::~Host() {
    t_host = nullptr;
    cancel_.Cancel();
    if (fetch_.joinable()) fetch_.join();
    media_.Stop();
    for (auto& w : widgets_) DestroyWindows(w.get());
    if (dropTarget_) DestroyWindow(dropTarget_);
    if (displayNotify_) UnregisterPowerSettingNotification(displayNotify_);
    if (control_) {
        WTSUnRegisterSessionNotification(control_);
        DestroyWindow(control_);
    }
    if (themeKey_) RegCloseKey(themeKey_);
    if (themeEvent_) CloseHandle(themeEvent_);
}

bool Host::Create() {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpfnWndProc = ControlProc;
    wc.lpszClassName = kControlClass;
    RegisterClassExW(&wc);
    wc.lpfnWndProc = CardProc;
    wc.lpszClassName = kCardClass;
    RegisterClassExW(&wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.lpszClassName = kShadowClass;
    RegisterClassExW(&wc);

    if (!g_.Init(L"en-US")) {
        Log(L"widgets: Direct2D / DirectWrite / WIC unavailable");
        return false;
    }
    QueryPerformanceFrequency(&qpf_);
    t_host = this;
    // A hidden top-level window (not message-only): it gets the broadcasts, and it is the
    // foreground window while a widget's menu is open.
    control_ = CreateWindowExW(WS_EX_TOOLWINDOW, kControlClass, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, inst, this);
    if (!control_) return false;
    taskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");
    // An elevated MacWin gets these from Explorer only when it lets them through.
    ChangeWindowMessageFilterEx(control_, taskbarCreated_, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(control_, WM_SETTINGCHANGE, MSGFLT_ALLOW, nullptr);
    WTSRegisterSessionNotification(control_, NOTIFY_FOR_THIS_SESSION);
    displayNotify_ = RegisterPowerSettingNotification(control_, &kDisplayState, DEVICE_NOTIFY_WINDOW_HANDLE);
    // Light / dark mode: the registry value itself is watched, broadcasts don't always come.
    themeEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0,
                      KEY_NOTIFY | KEY_QUERY_VALUE, &themeKey_) == ERROR_SUCCESS) {
        RegNotifyChangeKeyValue(themeKey_, FALSE, REG_NOTIFY_CHANGE_LAST_SET, themeEvent_, TRUE);
    }
    dark_ = AppsUseDarkTheme();
    return true;
}

void Host::Run() {
    for (;;) {
        DWORD r = MsgWaitForMultipleObjectsEx(1, &themeEvent_, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (r == WAIT_OBJECT_0) {
            if (themeKey_) RegNotifyChangeKeyValue(themeKey_, FALSE, REG_NOTIFY_CHANGE_LAST_SET, themeEvent_, TRUE);
            bool dark = AppsUseDarkTheme();
            if (dark != dark_) {
                dark_ = dark;
                RenderAll(true);
            }
        }
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                // Updates that came too late.
                while (PeekMessageW(&msg, control_, WM_APP_UPDATE, WM_APP_MEDIA, PM_REMOVE)) {
                    if (msg.message == WM_APP_UPDATE) delete (Update*)msg.lParam;
                    else if (msg.message == WM_APP_WEATHER) delete (WeatherResult*)msg.lParam;
                    else delete (MediaInfo*)msg.lParam;
                }
                return;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
}

const std::wstring& Host::T(const wchar_t* key) const {
    static const std::wstring empty;
    auto it = texts_.find(key);
    return it != texts_.end() ? it->second : empty;
}

Widget* Host::ById(const std::wstring& id) const {
    for (const auto& w : widgets_) {
        if (w->item.id == id) return w.get();
    }
    return nullptr;
}

bool Host::HasKind(Kind k) const {
    for (const auto& w : widgets_) {
        if (w->item.kind == k) return true;
    }
    return false;
}

void Host::Notify(Request r, LPARAM lp) {
    if (PostMessageW(notify_, msg_, r, lp)) return;
    if (r == kEdited) delete (std::vector<Item>*)lp;
    if (r == kOpen) delete (std::wstring*)lp;
}

// The main thread saves the widgets as they are now (it doesn't send them back: they
// are like that here already).
void Host::Persist() {
    auto* items = new std::vector<Item>();
    for (const auto& w : widgets_) items->push_back(w->item);
    config_.items = *items;
    Notify(kEdited, (LPARAM)items);
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

void Host::ApplyUpdate(Update* u) {
    std::unique_ptr<Update> update(u);
    const Config before = config_;
    const bool first = !started_;
    started_ = true;
    bool redrawAll = first || u->locale != locale_ || u->rtl != rtl_ || u->texts != texts_ ||
                     u->config.clockSeconds != before.clockSeconds || u->config.fahrenheit != before.fahrenheit;
    bool relayout = first || u->config.scale != before.scale;
    if (u->locale != locale_) g_.SetLocale(u->locale);
    locale_ = u->locale;
    rtl_ = u->rtl;
    texts_ = std::move(u->texts);
    config_ = u->config;

    std::vector<std::unique_ptr<Widget>> next;
    std::vector<Widget*> changed;   // a new look, note or picture: drawn again
    for (const Item& item : config_.items) {
        std::unique_ptr<Widget> w;
        for (auto& old : widgets_) {
            if (old && old->item.id == item.id) w = std::move(old);
        }
        if (!w) {
            w = std::make_unique<Widget>();
            w->item = item;
            relayout = true;
        } else if (!(w->item == item)) {
            const Item& was = w->item;
            if (was.size != item.size || was.placed != item.placed || was.monitor != item.monitor ||
                was.col != item.col || was.row != item.row) {
                relayout = true;
            }
            if (was.look != item.look || was.text != item.text || was.path != item.path || was.date != item.date) {
                changed.push_back(w.get());
            }
            w->item = item;
        }
        next.push_back(std::move(w));
    }
    // Removed in the settings: away with the windows (and any drag of them).
    for (auto& old : widgets_) {
        if (!old) continue;
        if (pressed_ == old.get()) {
            pressed_ = nullptr;
            dragging_ = false;
            HideDropTarget();
            ReleaseCapture();
        }
        if (animated_ == old.get()) FinishAnimation();
        DestroyWindows(old.get());
        relayout = true;
    }
    widgets_ = std::move(next);

    WeatherChanged();
    // Layout draws the widgets it creates or resizes; the rest only if something they show
    // changed.
    if (relayout) Layout();
    if ((redrawAll && !first) || !(before.place == config_.place) || before.autoPlace != config_.autoPlace) {
        RenderAll();
    } else {
        for (Widget* w : changed) Render(w);
    }
    Schedule();
}

std::vector<Monitor> Host::Monitors() const {
    std::vector<Monitor> out;
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR h, HDC, LPRECT, LPARAM lp) -> BOOL {
            MONITORINFOEXW mi = {};
            mi.cbSize = sizeof(mi);
            if (!GetMonitorInfoW(h, &mi)) return TRUE;
            UINT dx = 96, dy = 96;
            if (FAILED(GetDpiForMonitor(h, MDT_EFFECTIVE_DPI, &dx, &dy))) dx = 96;
            ((std::vector<Monitor>*)lp)
                ->push_back({mi.szDevice, mi.rcWork, dx / 96.0f, (mi.dwFlags & MONITORINFOF_PRIMARY) != 0});
            return TRUE;
        },
        (LPARAM)&out);
    return out;
}

const Monitor* Host::Find(const std::vector<Monitor>& monitors, const std::wstring& name) const {
    const Monitor* primary = nullptr;
    for (const Monitor& m : monitors) {
        if (m.name == name) return &m;
        if (m.primary) primary = &m;
    }
    return primary ? primary : monitors.empty() ? nullptr : &monitors[0];
}

std::vector<Slot> Host::Taken(const std::wstring& monitor, const Widget* except) const {
    std::vector<Slot> out;
    for (const auto& w : widgets_) {
        if (w.get() != except && w->shown && w->monitor == monitor) out.push_back(w->slot);
    }
    return out;
}

// Puts every widget on its slot. Widgets that already have one go first (in their
// order), new ones then take the first free slots; a widget whose slot is gone or taken
// moves to the nearest free one without forgetting the saved one (the monitor may only be
// unplugged for a while).
void Host::Layout() {
    std::vector<Monitor> monitors = Monitors();
    for (auto& w : widgets_) w->shown = false;
    bool placedNew = false;
    for (int pass = 0; pass < 2; ++pass) {
        for (auto& wp : widgets_) {
            Widget* w = wp.get();
            if (w->item.placed != (pass == 0)) continue;
            if (w == pressed_ && dragging_) {
                w->shown = true;   // where the drag leaves it
                continue;
            }
            const Monitor* m = Find(monitors, w->item.monitor);
            if (!m) {
                Hide(w);
                continue;
            }
            Grid grid(m->work, UnitFor(*m));
            Span span = SpanOf(w->item.size);
            std::vector<Slot> taken = Taken(m->name, w);
            Slot slot = {w->item.col, w->item.row, span};
            bool ok;
            if (!w->item.placed) {
                ok = grid.First(span, taken, &slot);
            } else {
                bool fits = grid.Inside(slot);
                for (const Slot& t : taken) fits = fits && !Overlaps(slot, t);
                RECT want = grid.Rect(slot);
                ok = fits || grid.Nearest(span, {want.left, want.top}, taken, &slot);
            }
            if (!ok) {
                Log(L"widgets: no room for %ls on %ls", KindName(w->item.kind), m->name.c_str());
                Hide(w);
                continue;
            }
            if (!w->item.placed) {
                w->item.placed = true;
                w->item.monitor = m->name;
                w->item.col = slot.col;
                w->item.row = slot.row;
                placedNew = true;
            }
            Show(w, *m, grid, slot);
        }
    }
    if (placedNew) Persist();
}

void Host::Show(Widget* w, const Monitor& m, const Grid& grid, const Slot& slot) {
    RECT r = grid.Rect(slot);
    float unit = UnitFor(m);
    bool resized = unit != w->unit || r.right - r.left != w->rect.right - w->rect.left ||
                   r.bottom - r.top != w->rect.bottom - w->rect.top;
    w->monitor = m.name;
    w->slot = slot;
    w->shown = true;
    if (!w->card && !CreateWindows(w)) return;
    const bool hidden = !IsWindowVisible(w->shadow);
    if (resized) {
        w->unit = unit;
        w->rect = r;
        w->shadowDrawn = false;
        Render(w);
    } else {
        if (r.left != w->rect.left || r.top != w->rect.top) {
            if (animated_ == w) FinishAnimation();
            Move(w, r.left, r.top);
        }
        if (hidden) Render(w);   // not drawn while it was hidden
    }
    if (hidden) {
        ShowWindow(w->shadow, SW_SHOWNOACTIVATE);
        ShowWindow(w->card, SW_SHOWNOACTIVATE);
        // Down to the desktop: an owned window can't go below its owner, so this puts it
        // right above the desktop and under every program.
        SetWindowPos(w->shadow, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

void Host::Hide(Widget* w) {
    w->shown = false;
    if (w->shadow) ShowWindow(w->shadow, SW_HIDE);
    if (w->card) ShowWindow(w->card, SW_HIDE);
}

bool Host::CreateWindows(Widget* w) {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    // Owned by the desktop: always right above it, under every program, and left alone by
    // "Show desktop".
    HWND desktop = FindWindowW(L"Progman", nullptr);
    const DWORD ex = WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    w->shadow = CreateWindowExW(ex | WS_EX_TRANSPARENT, kShadowClass, L"", WS_POPUP, 0, 0, 1, 1, desktop, nullptr,
                                inst, nullptr);
    w->card = w->shadow ? CreateWindowExW(ex, kCardClass, L"MacWin widget", WS_POPUP, 0, 0, 1, 1, w->shadow, nullptr,
                                          inst, w)
                        : nullptr;
    if (!w->card) {
        Log(L"widgets: CreateWindowEx failed: %lu", GetLastError());
        DestroyWindows(w);
        return false;
    }
    DWORD round = DWMWCP_DONOTROUND;
    DwmSetWindowAttribute(w->card, DWMWA_WINDOW_CORNER_PREFERENCE, &round, sizeof(round));
    DwmSetWindowAttribute(w->shadow, DWMWA_WINDOW_CORNER_PREFERENCE, &round, sizeof(round));
    w->shadowDrawn = false;
    return true;
}

void Host::DestroyWindows(Widget* w) {
    w->destroying = true;
    // One of them may be gone already (destroyed with the desktop window it belonged to).
    if (w->card && IsWindow(w->card)) DestroyWindow(w->card);
    if (w->shadow && IsWindow(w->shadow)) DestroyWindow(w->shadow);
    w->card = w->shadow = nullptr;
    w->destroying = false;
    w->shown = false;
    w->unit = 0;
}

// Explorer restarted (a new desktop window to belong to), or a window went away.
void Host::RecreateAll() {
    for (auto& w : widgets_) DestroyWindows(w.get());
    if (dropTarget_) DestroyWindow(dropTarget_);
    dropTarget_ = nullptr;
    Layout();
}

bool Host::IsDark(const Widget* w) const {
    return w->item.look == Look::Dark || (w->item.look == Look::Auto && dark_);
}

Scene Host::SceneFor(Widget* w, const SYSTEMTIME& now) {
    Scene s;
    s.kind = w->item.kind;
    s.size = w->item.size;
    s.dark = IsDark(w);
    s.rtl = rtl_;
    s.texts = &texts_;
    s.locale = locale_;
    s.now = now;
    wchar_t format[80] = L"";
    GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_STIMEFORMAT, format, ARRAYSIZE(format));
    s.clock24 = wcschr(format, L'H') != nullptr;
    s.seconds = config_.clockSeconds;
    wchar_t first[4] = L"0";
    GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_IFIRSTDAYOFWEEK, first, ARRAYSIZE(first));
    s.firstDay = _wtoi(first) % 7;
    s.weather = config_.autoPlace || config_.place.valid ? weatherState_ : WeatherState::NoPlace;
    s.fahrenheit = config_.fahrenheit;
    s.forecast = &forecast_;
    s.place = weatherPlace_;
    s.stats = stats_;
    s.text = w->item.text;
    s.date = w->item.date;
    s.media = &nowPlaying_;
    if (w->item.kind == Kind::Photo) {
        SIZE size = {w->rect.right - w->rect.left, w->rect.bottom - w->rect.top};
        if (w->photoPath != w->item.path || w->photoSize.cx != size.cx || w->photoSize.cy != size.cy) {
            w->photoPath = w->item.path;
            w->photoSize = size;
            w->photo = w->item.path.empty() ? nullptr : LoadPicture(g_, w->item.path, size.cx, size.cy);
            if (!w->item.path.empty() && !w->photo) Log(L"widgets: can't read the picture %ls", w->item.path.c_str());
        }
        s.photo = w->photo.get();
    }
    return s;
}

void Host::Render(Widget* w) {
    if (!w->card || !w->shown || w->unit <= 0) return;
    const int cw = w->rect.right - w->rect.left, ch = w->rect.bottom - w->rect.top;
    const int reach = (int)lroundf(kShadowReach * w->unit);
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    POINT origin = {0, 0};
    if (!w->shadowDrawn && w->shadowSurface.Resize(g_, cw + 2 * reach, ch + 2 * reach)) {
        DrawShadow(g_, w->shadowSurface,
                   {(float)reach, (float)reach, (float)(reach + cw), (float)(reach + ch)},
                   kCardRadius * w->unit, kCardSmoothing, w->unit);
        POINT at = {w->rect.left - reach, w->rect.top - reach};
        SIZE size = {w->shadowSurface.Width(), w->shadowSurface.Height()};
        UpdateLayeredWindow(w->shadow, nullptr, &at, &size, w->shadowSurface.Dc(), &origin, 0, &blend, ULW_ALPHA);
        w->shadowDrawn = true;
    }
    if (!w->cardSurface.Resize(g_, cw, ch)) return;
    SYSTEMTIME now;
    GetLocalTime(&now);
    Scene scene = SceneFor(w, now);
    if (w->item.kind == Kind::Clock) {
        // The face once, then every tick a copy of it with the hands on top.
        const size_t bytes = (size_t)cw * ch * 4;
        if (w->face.size() != bytes || w->faceWidth != cw || w->faceHeight != ch || w->faceDark != scene.dark ||
            w->faceRtl != scene.rtl) {
            scene.pass = Pass::Face;
            if (ID2D1RenderTarget* rt = w->cardSurface.Begin()) {
                DrawCard(g_, rt, scene, w->unit, cw, ch);
                w->cardSurface.End();
            }
            w->face.assign(w->cardSurface.Bits(), w->cardSurface.Bits() + bytes);
            w->faceWidth = cw;
            w->faceHeight = ch;
            w->faceDark = scene.dark;
            w->faceRtl = scene.rtl;
        } else {
            memcpy(w->cardSurface.Bits(), w->face.data(), bytes);
        }
        scene.pass = Pass::Hands;
    }
    if (ID2D1RenderTarget* rt = w->cardSurface.Begin(scene.pass == Pass::All)) {
        DrawCard(g_, rt, scene, w->unit, cw, ch);
        w->cardSurface.End();
    }
    POINT at = {w->rect.left, w->rect.top};
    SIZE size = {cw, ch};
    UpdateLayeredWindow(w->card, nullptr, &at, &size, w->cardSurface.Dc(), &origin, 0, &blend, ULW_ALPHA);
    w->drawnMinute = now.wHour * 60 + now.wMinute;
    w->drawnDay = now.wDay;
}

void Host::RenderAll(bool onlyAuto) {
    if (locked_ || displayOff_) return;
    for (auto& w : widgets_) {
        if (!onlyAuto || w->item.look == Look::Auto) Render(w.get());
    }
}

void Host::Move(Widget* w, int x, int y) {
    const int reach = (int)lroundf(kShadowReach * w->unit);
    HDWP dwp = BeginDeferWindowPos(2);
    const UINT flags = SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE;
    if (dwp) dwp = DeferWindowPos(dwp, w->shadow, nullptr, x - reach, y - reach, 0, 0, flags);
    if (dwp) dwp = DeferWindowPos(dwp, w->card, nullptr, x, y, 0, 0, flags);
    if (dwp) EndDeferWindowPos(dwp);
    OffsetRect(&w->rect, x - w->rect.left, y - w->rect.top);
}

// ---------------------------------------------------------------------------
// Time: redraws only when something on a card changes
// ---------------------------------------------------------------------------

// A full-screen game or presentation hides the desktop: no redraws until it is gone.
bool Host::Busy() const {
    QUERY_USER_NOTIFICATION_STATE state;
    return SUCCEEDED(SHQueryUserNotificationState(&state)) &&
           (state == QUNS_BUSY || state == QUNS_RUNNING_D3D_FULL_SCREEN || state == QUNS_PRESENTATION_MODE);
}

void Host::Schedule() {
    KillTimer(control_, kTickTimer);
    if (locked_ || displayOff_) {
        MediaChanged();
        return;
    }
    SYSTEMTIME now;
    GetLocalTime(&now);
    UINT delay = UINT_MAX, tolerance = 50;
    for (const auto& w : widgets_) {
        if (!w->shown) continue;
        switch (w->item.kind) {
            case Kind::Clock:
                if (config_.clockSeconds) {
                    delay = std::min<UINT>(delay, 1000 - now.wMilliseconds + 2);
                    tolerance = 0;
                    break;
                }
                [[fallthrough]];
            case Kind::Calendar:
            case Kind::Countdown:
                delay = std::min<UINT>(delay, (60 - now.wSecond) * 1000 - now.wMilliseconds + 2);
                break;
            case Kind::System:
                delay = std::min<UINT>(delay, kSystemEveryMs);
                break;
            default:
                break;
        }
    }
    MediaChanged();
    if (delay == UINT_MAX) return;
    // Windows may fire timers of all programs together within the tolerance (saves power).
    SetCoalescableTimer(control_, kTickTimer, delay, nullptr, tolerance ? tolerance : TIMERV_NO_COALESCING);
}

void Host::Tick() {
    KillTimer(control_, kTickTimer);
    if (Busy()) {
        SetTimer(control_, kTickTimer, 5000, nullptr);
        return;
    }
    SYSTEMTIME now;
    GetLocalTime(&now);
    const int minute = now.wHour * 60 + now.wMinute;
    ULONGLONG t = GetTickCount64();
    bool sampled = false;
    for (auto& wp : widgets_) {
        Widget* w = wp.get();
        if (!w->shown || w == animated_) continue;
        bool redraw = false;
        switch (w->item.kind) {
            case Kind::Clock: redraw = config_.clockSeconds || w->drawnMinute != minute; break;
            case Kind::Calendar:
            case Kind::Countdown: redraw = w->drawnDay != now.wDay || w->drawnMinute > minute; break;
            case Kind::System:
                if (!sampled && t - statsAt_ >= kSystemEveryMs - 100) {
                    stats_ = sampler_.Sample();
                    statsAt_ = t;
                    sampled = true;
                }
                redraw = sampled;
                break;
            default: break;
        }
        if (redraw) Render(w);
    }
    Schedule();
}

void Host::Suspend(bool locked, bool displayOff) {
    bool was = locked_ || displayOff_;
    locked_ = locked;
    displayOff_ = displayOff;
    bool now = locked_ || displayOff_;
    if (was && !now) RenderAll();   // the time moved on while nobody could see it
    Schedule();
}

// ---------------------------------------------------------------------------
// Weather
// ---------------------------------------------------------------------------

// Fetched only while a Weather widget is on the desktop, for "my location" or a chosen place.
void Host::WeatherChanged() {
    if (!HasKind(Kind::Weather) || (!config_.autoPlace && !config_.place.valid)) {
        KillTimer(control_, kWeatherTimer);
        weatherKey_.clear();
        weatherState_ = WeatherState::NoPlace;
        return;
    }
    std::wstring key = WeatherKey(config_);
    if (key == weatherKey_) return;
    weatherKey_ = key;
    forecast_ = Forecast();
    weatherState_ = WeatherState::Loading;
    weatherPlace_ = config_.autoPlace ? T(L"host.widgets.weather.here") : config_.place.name;
    // The last forecast for this place shows at once; a new one follows when it is due.
    WeatherCache cache;
    ULONGLONG now = UnixNow();
    if (LoadWeatherCache(&cache)) {
        if (cache.located && !known_) {
            known_ = true;
            knownLatitude_ = cache.latitude;
            knownLongitude_ = cache.longitude;
            knownName_ = cache.place;
        }
        if (cache.key == key && now >= cache.fetched && now - cache.fetched < kWeatherCacheMaxAge &&
            ParseForecast(cache.forecast, &forecast_)) {
            weatherState_ = WeatherState::Ready;
            if (!cache.place.empty()) weatherPlace_ = cache.place;
            ULONGLONG age = (now - cache.fetched) * 1000;
            if (age < kWeatherEveryMs) {
                SetTimer(control_, kWeatherTimer, (UINT)(kWeatherEveryMs - age), nullptr);
                return;
            }
        }
    }
    FetchWeather();
}

void Host::FetchWeather() {
    KillTimer(control_, kWeatherTimer);
    if (weatherKey_.empty() || fetch_.joinable()) return;   // nothing to fetch, or already on the way
    WeatherRequest r;
    r.id = ++weatherRequest_;
    r.automatic = config_.autoPlace;
    r.place = config_.place;
    r.fahrenheit = config_.fahrenheit;
    r.language = locale_;
    r.known = known_;
    r.knownLatitude = knownLatitude_;
    r.knownLongitude = knownLongitude_;
    r.knownName = knownName_;
    fetchKey_ = weatherKey_;
    HWND target = control_;
    HttpCancel* cancel = &cancel_;
    fetch_ = std::thread([r, target, cancel] {
        WeatherResult* result = FetchWeatherFor(r, cancel);
        if (!PostMessageW(target, WM_APP_WEATHER, 0, (LPARAM)result)) delete result;
    });
}

void Host::OnWeather(WeatherResult* result) {
    std::unique_ptr<WeatherResult> owned(result);
    if (fetch_.joinable()) fetch_.join();
    if (result->id != weatherRequest_) return;
    if (fetchKey_ != weatherKey_) {
        FetchWeather();   // the place changed while this one was on the way
        return;
    }
    if (result->located) {
        known_ = true;
        knownLatitude_ = result->latitude;
        knownLongitude_ = result->longitude;
        if (!result->place.empty()) knownName_ = result->place;
    }
    // The settings window shows what "my location" found (only rewritten when it changes).
    if (config_.autoPlace && (result->location != reported_ || result->place != reportedPlace_)) {
        reported_ = result->location;
        reportedPlace_ = result->place;
        SaveLocationStatus(reported_, reportedPlace_);
    }
    Forecast f;
    if (!result->hasPlace) {
        // Location off and no place chosen: the widget says how to fix that.
        Log(L"widgets: no location for the weather");
        weatherState_ = WeatherState::LocationOff;
        SetTimer(control_, kWeatherTimer, kWeatherRetryMs, nullptr);
    } else if (result->ok && ParseForecast(result->forecast, &f)) {
        forecast_ = f;
        weatherState_ = WeatherState::Ready;
        weatherPlace_ = result->place.empty() ? T(L"host.widgets.weather.here") : result->place;
        WeatherCache cache;
        cache.key = weatherKey_;
        cache.fetched = UnixNow();
        cache.place = result->place;
        cache.latitude = result->latitude;
        cache.longitude = result->longitude;
        cache.located = result->located;
        cache.forecast = result->forecast;
        SaveWeatherCache(cache);
        SetTimer(control_, kWeatherTimer, kWeatherEveryMs, nullptr);
    } else {
        Log(L"widgets: the forecast didn't come (%ls)", result->ok ? L"unreadable" : L"no connection");
        // An older forecast stays up; without one, the widget says it is offline.
        if (weatherState_ != WeatherState::Ready) weatherState_ = WeatherState::Offline;
        SetTimer(control_, kWeatherTimer, kWeatherRetryMs, nullptr);
    }
    for (auto& w : widgets_) {
        if (w->item.kind == Kind::Weather && !locked_ && !displayOff_) Render(w.get());
    }
}

// ---------------------------------------------------------------------------
// Now playing
// ---------------------------------------------------------------------------

void Host::MediaChanged() {
    bool shown = false;
    for (const auto& w : widgets_) shown = shown || (w->shown && w->item.kind == Kind::NowPlaying);
    if (shown && !locked_ && !displayOff_) {
        media_.Start(control_, WM_APP_MEDIA);
    } else {
        media_.Stop();
    }
}

// What is playing changed (the watcher says only then).
void Host::OnMedia(MediaInfo* info) {
    std::unique_ptr<MediaInfo> owned(info);
    nowPlaying_ = *info;
    if (locked_ || displayOff_) return;
    for (auto& w : widgets_) {
        if (w->item.kind == Kind::NowPlaying) Render(w.get());
    }
}

// ---------------------------------------------------------------------------
// Mouse: click, drag to another slot, right-click menu
// ---------------------------------------------------------------------------

void Host::StartDrag(Widget* w) {
    dragging_ = true;
    // While it moves it floats above everything; dropped, it goes back down.
    SetWindowPos(w->shadow, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetWindowPos(w->card, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void Host::Drag(Widget* w, POINT cursor) {
    int x = pressOrigin_.x + cursor.x - pressAt_.x, y = pressOrigin_.y + cursor.y - pressAt_.y;
    Move(w, x, y);
    // The slot it would land in: on the monitor under the cursor, nearest to the card.
    std::vector<Monitor> monitors = Monitors();
    HMONITOR hm = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW mi = {};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(hm, &mi);
    const Monitor* m = Find(monitors, mi.szDevice);
    if (!m) return;
    float unit = UnitFor(*m);
    Grid grid(m->work, unit);
    Span span = SpanOf(w->item.size);
    Slot slot;
    // Measured from the card's middle: the target follows the cursor across monitors of
    // different scale.
    SIZE card = grid.CardSize(span);
    POINT topLeft = {(w->rect.left + w->rect.right) / 2 - card.cx / 2, (w->rect.top + w->rect.bottom) / 2 - card.cy / 2};
    if (grid.Nearest(span, topLeft, Taken(m->name, w), &slot)) {
        UpdateDropTarget(*m, grid, slot, unit);
    } else {
        dropValid_ = false;
        HideDropTarget();
    }
}

// A frosted outline where the widget will land.
void Host::UpdateDropTarget(const Monitor& m, const Grid& grid, const Slot& slot, float unit) {
    bool same = dropValid_ && dropMonitor_ == m.name && dropSlot_.col == slot.col && dropSlot_.row == slot.row &&
                SameSpan(dropSlot_.span, slot.span) && dropUnit_ == unit && dropTarget_ && IsWindowVisible(dropTarget_);
    dropValid_ = true;
    dropMonitor_ = m.name;
    dropSlot_ = slot;
    if (same) return;
    RECT r = grid.Rect(slot);
    if (!dropTarget_) {
        dropTarget_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                      kShadowClass, L"", WS_POPUP, 0, 0, 1, 1, FindWindowW(L"Progman", nullptr),
                                      nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!dropTarget_) return;
        DWORD round = DWMWCP_DONOTROUND;
        DwmSetWindowAttribute(dropTarget_, DWMWA_WINDOW_CORNER_PREFERENCE, &round, sizeof(round));
    }
    const int w = r.right - r.left, h = r.bottom - r.top;
    if (unit != dropUnit_ || w != dropSurface_.Width() || h != dropSurface_.Height()) {
        dropUnit_ = unit;
        if (!dropSurface_.Resize(g_, w, h)) return;
        if (ID2D1RenderTarget* rt = dropSurface_.Begin()) {
            float px = 1.0f;
            ComPtr<ID2D1PathGeometry> shape =
                Squircle(g_.D2D(), {px, px, w - px, h - px}, kCardRadius * unit - px, kCardSmoothing);
            ComPtr<ID2D1SolidColorBrush> fill, edge;
            rt->CreateSolidColorBrush(Rgb(0xFFFFFF, 0.16f), fill.put());
            rt->CreateSolidColorBrush(Rgb(0xFFFFFF, 0.55f), edge.put());
            if (shape && fill && edge) {
                rt->FillGeometry(shape.get(), fill.get());
                rt->DrawGeometry(shape.get(), edge.get(), 1.5f * unit);
            }
            dropSurface_.End();
        }
    }
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    POINT origin = {0, 0}, at = {r.left, r.top};
    SIZE size = {dropSurface_.Width(), dropSurface_.Height()};
    UpdateLayeredWindow(dropTarget_, nullptr, &at, &size, dropSurface_.Dc(), &origin, 0, &blend, ULW_ALPHA);
    if (!IsWindowVisible(dropTarget_)) {
        ShowWindow(dropTarget_, SW_SHOWNOACTIVATE);
        SetWindowPos(dropTarget_, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

void Host::HideDropTarget() {
    if (dropTarget_) ShowWindow(dropTarget_, SW_HIDE);
}

void Host::Drop(Widget* w, bool cancel) {
    dragging_ = false;
    HideDropTarget();
    SetWindowPos(w->shadow, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (cancel || !dropValid_) {
        Animate(w, pressOrigin_);
        return;
    }
    dropValid_ = false;
    std::vector<Monitor> monitors = Monitors();
    const Monitor* m = Find(monitors, dropMonitor_);
    if (!m) {
        Animate(w, pressOrigin_);
        return;
    }
    w->item.placed = true;
    w->item.monitor = m->name;
    w->item.col = dropSlot_.col;
    w->item.row = dropSlot_.row;
    w->monitor = m->name;
    w->slot = dropSlot_;
    Grid grid(m->work, UnitFor(*m));
    RECT target = grid.Rect(dropSlot_);
    float unit = UnitFor(*m);
    if (unit != w->unit) {
        // Another monitor scale: drawn anew for it, centered where it was let go.
        int cx = (w->rect.left + w->rect.right) / 2, cy = (w->rect.top + w->rect.bottom) / 2;
        int tw = target.right - target.left, th = target.bottom - target.top;
        w->unit = unit;
        w->rect = {cx - tw / 2, cy - th / 2, cx - tw / 2 + tw, cy - th / 2 + th};
        w->shadowDrawn = false;
        Render(w);
    }
    Animate(w, {target.left, target.top});
    Persist();
}

void Host::Animate(Widget* w, POINT to) {
    animated_ = w;
    animFrom_ = {w->rect.left, w->rect.top};
    animTo_ = to;
    // Windows' animation effects turned off: the widget goes straight to its place.
    BOOL animations = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0) && !animations) {
        FinishAnimation();
        return;
    }
    QueryPerformanceCounter(&animStart_);
    SetTimer(control_, kAnimTimer, 8, nullptr);
}

void Host::FinishAnimation() {
    Widget* w = animated_;
    animated_ = nullptr;
    KillTimer(control_, kAnimTimer);
    if (w) Move(w, animTo_.x, animTo_.y);
}

void Host::AnimationStep() {
    Widget* w = animated_;
    if (!w) {
        KillTimer(control_, kAnimTimer);
        return;
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    double t = (now.QuadPart - animStart_.QuadPart) * 1000.0 / qpf_.QuadPart / kAnimMs;
    if (t >= 1) {
        FinishAnimation();
        return;
    }
    double e = 1 - pow(1 - t, 3);   // ease out
    Move(w, (int)lround(animFrom_.x + (animTo_.x - animFrom_.x) * e),
         (int)lround(animFrom_.y + (animTo_.y - animFrom_.y) * e));
}

// A click opens what the widget is about.
void Host::Click(Widget* w) {
    std::wstring target;
    switch (w->item.kind) {
        case Kind::Clock: target = L"ms-clock:"; break;
        case Kind::System: {
            wchar_t dir[MAX_PATH];
            UINT n = GetSystemDirectoryW(dir, MAX_PATH);
            target = std::wstring(dir, n) + L"\\Taskmgr.exe";
            break;
        }
        case Kind::Photo:
            if (!w->item.path.empty() && GetFileAttributesW(w->item.path.c_str()) != INVALID_FILE_ATTRIBUTES) {
                target = w->item.path;
            }
            break;
        default: break;
    }
    bool settings = target.empty() && (w->item.kind == Kind::Notes || w->item.kind == Kind::Photo ||
                                       w->item.kind == Kind::Countdown ||
                                       (w->item.kind == Kind::Weather && (weatherState_ == WeatherState::LocationOff ||
                                                                          (!config_.autoPlace && !config_.place.valid))));
    if (!target.empty()) Notify(kOpen, (LPARAM)new std::wstring(target));
    else if (settings) Notify(kOpenSettings);
}

void Host::ShowMenu(Widget* w, POINT at) {
    enum : UINT { kSize = 100, kLook = 200, kEdit = 300, kHide, kRemove };
    const UINT rtl = rtl_ ? MFT_RIGHTORDER | MFT_RIGHTJUSTIFY : 0;
    HMENU menu = CreatePopupMenu(), sizes = CreatePopupMenu(), looks = CreatePopupMenu();
    const Size kSizes[] = {Size::Small, Size::Medium, Size::Large};
    const wchar_t* const kSizeKeys[] = {L"host.widgets.menu.small", L"host.widgets.menu.medium",
                                        L"host.widgets.menu.large"};
    for (int i = 0; i < 3; ++i) {
        if (!Supports(w->item.kind, kSizes[i])) continue;
        AppendMenuW(sizes, MF_STRING | rtl, kSize + i, T(kSizeKeys[i]).c_str());
    }
    CheckMenuRadioItem(sizes, kSize, kSize + 2, kSize + (UINT)w->item.size, MF_BYCOMMAND);
    AppendMenuW(menu, MF_POPUP | rtl, (UINT_PTR)sizes, T(L"host.widgets.menu.size").c_str());
    // The weather has its sky and a photo its picture: no light or dark for them.
    if (w->item.kind != Kind::Weather && w->item.kind != Kind::Photo) {
        const wchar_t* const kLookKeys[] = {L"host.widgets.menu.auto", L"host.widgets.menu.light",
                                            L"host.widgets.menu.dark"};
        for (int i = 0; i < 3; ++i) AppendMenuW(looks, MF_STRING | rtl, kLook + i, T(kLookKeys[i]).c_str());
        CheckMenuRadioItem(looks, kLook, kLook + 2, kLook + (UINT)w->item.look, MF_BYCOMMAND);
        AppendMenuW(menu, MF_POPUP | rtl, (UINT_PTR)looks, T(L"host.widgets.menu.appearance").c_str());
    } else {
        DestroyMenu(looks);
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | rtl, kEdit, T(L"host.widgets.menu.edit").c_str());
    AppendMenuW(menu, MF_STRING | rtl, kHide, T(L"host.widgets.menu.hide").c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | rtl, kRemove, T(L"host.widgets.menu.remove").c_str());

    const std::wstring id = w->item.id;
    SetForegroundWindow(control_);   // so the menu closes on a click elsewhere
    UINT flags = TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY | (rtl_ ? TPM_LAYOUTRTL : 0);
    UINT cmd = TrackPopupMenu(menu, flags, at.x, at.y, 0, control_, nullptr);
    DestroyMenu(menu);
    PostMessageW(control_, WM_NULL, 0, 0);
    // The settings may have changed while the menu was open.
    w = ById(id);
    if (!w || !cmd) return;
    if (cmd >= kSize && cmd < kSize + 3) {
        SetSize(w, kSizes[cmd - kSize]);
    } else if (cmd >= kLook && cmd < kLook + 3) {
        w->item.look = (Look)(cmd - kLook);
        Render(w);
        Persist();
    } else if (cmd == kEdit) {
        Notify(kOpenSettings);
    } else if (cmd == kHide) {
        Notify(kHideAll);
    } else if (cmd == kRemove) {
        Remove(w);
    }
}

// Same corner where possible; otherwise the nearest slot with room; no room, no change.
void Host::SetSize(Widget* w, Size size) {
    if (w->item.size == size || !Supports(w->item.kind, size)) return;
    std::vector<Monitor> monitors = Monitors();
    const Monitor* m = Find(monitors, w->monitor);
    if (!m) return;
    Grid grid(m->work, UnitFor(*m));
    Slot slot = {w->slot.col, w->slot.row, SpanOf(size)};
    std::vector<Slot> taken = Taken(m->name, w);
    bool fits = grid.Inside(slot);
    for (const Slot& t : taken) fits = fits && !Overlaps(slot, t);
    if (!fits && !grid.Nearest(slot.span, {w->rect.left, w->rect.top}, taken, &slot)) {
        MessageBeep(MB_ICONWARNING);
        return;
    }
    w->item.size = size;
    w->item.placed = true;
    w->item.monitor = m->name;
    w->item.col = slot.col;
    w->item.row = slot.row;
    Show(w, *m, grid, slot);
    Persist();
    Schedule();
}

void Host::Remove(Widget* w) {
    if (animated_ == w) FinishAnimation();
    DestroyWindows(w);
    for (auto it = widgets_.begin(); it != widgets_.end(); ++it) {
        if (it->get() == w) {
            widgets_.erase(it);
            break;
        }
    }
    Persist();
    WeatherChanged();
    Schedule();
}

// ---------------------------------------------------------------------------
// Window procedures
// ---------------------------------------------------------------------------

LRESULT CALLBACK Host::ControlProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = (CREATESTRUCTW*)lp;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        ((Host*)cs->lpCreateParams)->control_ = hwnd;
    }
    Host* host = (Host*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    return host ? host->OnControl(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT Host::OnControl(UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == taskbarCreated_ && msg) {
        RecreateAll();
        return 0;
    }
    switch (msg) {
        case WM_APP_UPDATE:
            ApplyUpdate((Update*)lp);
            return 0;
        case WM_APP_WEATHER:
            OnWeather((WeatherResult*)lp);
            return 0;
        case WM_APP_MEDIA:
            OnMedia((MediaInfo*)lp);
            return 0;
        case WM_TIMER:
            if (wp == kTickTimer) {
                Tick();
            } else if (wp == kWeatherTimer) {
                FetchWeather();
            } else if (wp == kAnimTimer) {
                AnimationStep();
            } else if (wp == kLayoutTimer) {
                KillTimer(control_, kLayoutTimer);
                if (recreate_) {
                    recreate_ = false;
                    RecreateAll();
                } else {
                    Layout();
                }
                RenderAll();
            }
            return 0;
        case WM_DISPLAYCHANGE:
        case WM_DPICHANGED:
            SetTimer(control_, kLayoutTimer, 300, nullptr);
            return 0;
        case WM_SETTINGCHANGE:
            // The taskbar moved or changed size; the time or date format changed.
            if (wp == SPI_SETWORKAREA) SetTimer(control_, kLayoutTimer, 300, nullptr);
            else if (lp && wcscmp((const wchar_t*)lp, L"intl") == 0) RenderAll();
            return 0;
        case WM_TIMECHANGE:
            RenderAll();
            Schedule();
            return 0;
        case WM_WTSSESSION_CHANGE:
            if (wp == WTS_SESSION_LOCK) Suspend(true, displayOff_);
            else if (wp == WTS_SESSION_UNLOCK) Suspend(false, displayOff_);
            return 0;
        case WM_POWERBROADCAST:
            if (wp == PBT_POWERSETTINGCHANGE) {
                auto* setting = (const POWERBROADCAST_SETTING*)lp;
                if (setting && IsEqualGUID(setting->PowerSetting, kDisplayState) && setting->DataLength >= sizeof(DWORD)) {
                    Suspend(locked_, *(const DWORD*)setting->Data == 0);
                }
            } else if (wp == PBT_APMRESUMEAUTOMATIC) {
                RenderAll();
                Schedule();
                // The network needs a moment after sleep.
                if (!weatherKey_.empty()) SetTimer(control_, kWeatherTimer, 15000, nullptr);
            }
            return TRUE;
        case WM_CLOSE:
            DestroyWindow(control_);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            control_ = nullptr;
            return 0;
    }
    return DefWindowProcW(control_, msg, wp, lp);
}

LRESULT CALLBACK Host::CardProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = (CREATESTRUCTW*)lp;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
    }
    Widget* w = (Widget*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    return w && t_host ? t_host->OnCard(hwnd, w, msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

// `hwnd`, not w->card: while the window is being created, w->card isn't set yet.
LRESULT Host::OnCard(HWND hwnd, Widget* w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_LBUTTONDOWN:
            if (animated_ == w) FinishAnimation();
            pressed_ = w;
            dragging_ = false;
            GetCursorPos(&pressAt_);
            pressOrigin_ = {w->rect.left, w->rect.top};
            SetCapture(hwnd);
            return 0;
        case WM_MOUSEMOVE:
            if (pressed_ == w && GetCapture() == hwnd) {
                POINT pt;
                GetCursorPos(&pt);
                if (!dragging_ && (abs(pt.x - pressAt_.x) >= GetSystemMetrics(SM_CXDRAG) ||
                                   abs(pt.y - pressAt_.y) >= GetSystemMetrics(SM_CYDRAG))) {
                    StartDrag(w);
                }
                if (dragging_) Drag(w, pt);
            }
            return 0;
        case WM_LBUTTONUP:
            if (pressed_ == w) {
                bool dragged = dragging_;
                pressed_ = nullptr;
                ReleaseCapture();
                if (dragged) Drop(w, false);
                else Click(w);
            }
            return 0;
        case WM_CAPTURECHANGED:
            // Taken away mid-drag (a window popped up): back to where it was.
            if (pressed_ == w && (HWND)lp != hwnd) {
                pressed_ = nullptr;
                if (dragging_) Drop(w, true);
            }
            return 0;
        case WM_RBUTTONUP: {
            POINT pt;
            GetCursorPos(&pt);
            ShowMenu(w, pt);
            return 0;
        }
        case WM_DPICHANGED:
            return 0;   // the host sizes widgets itself, for the monitor they land on
        case WM_DESTROY:
            if (!w->destroying) {
                // Not ours to destroy (the desktop window went away): rebuilt shortly.
                w->card = nullptr;
                w->shown = false;
                if (pressed_ == w) pressed_ = nullptr;
                if (animated_ == w) animated_ = nullptr;
                recreate_ = true;
                SetTimer(control_, kLayoutTimer, 1000, nullptr);
            }
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

// ---------------------------------------------------------------------------
// WidgetHost (main thread)
// ---------------------------------------------------------------------------

void WidgetHost::Init(HWND notify, UINT msg) {
    notify_ = notify;
    msg_ = msg;
}

void WidgetHost::Apply(const Config& config, const Texts& texts, const std::wstring& locale, bool rtl) {
    bool wanted = config.show && !config.items.empty();
    if (!wanted) {
        Stop();
        return;
    }
    if (!control_ && !Start()) return;
    auto* u = new Update{config, texts, locale, rtl};
    if (!PostMessageW(control_, WM_APP_UPDATE, 0, (LPARAM)u)) delete u;
}

bool WidgetHost::Start() {
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ready) return false;
    HWND control = nullptr;
    HWND notify = notify_;
    UINT msg = msg_;
    thread_ = std::thread([notify, msg, ready, &control] {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        {
            Host host(notify, msg);
            bool ok = host.Create();
            control = ok ? host.Control() : nullptr;
            SetEvent(ready);
            if (ok) host.Run();
        }
        CoUninitialize();
    });
    WaitForSingleObject(ready, INFINITE);
    CloseHandle(ready);
    control_ = control;
    if (!control_) {
        thread_.join();
        Log(L"widgets: could not start");
        return false;
    }
    Log(L"widgets: started");
    return true;
}

void WidgetHost::Stop() {
    if (!thread_.joinable()) return;
    if (control_) PostMessageW(control_, WM_CLOSE, 0, 0);
    thread_.join();
    control_ = nullptr;
    Log(L"widgets: stopped");
}

}  // namespace widgets
