// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

#include "host/island/island_host.h"
#include <dwmapi.h>
#include <shellscalingapi.h>
#include <math.h>
#include <algorithm>
#include <atomic>
#include <memory>
#include <vector>
#include "host/i18n.h"
#include "host/island/island_audio.h"
#include "host/island/island_draw.h"
#include "host/island/island_sources.h"
#include "host/log.h"
#include "host/widgets/canvas.h"
#include "host/widgets/media.h"

namespace island {

namespace {

const wchar_t kClass[] = L"MacWinIsland_v1";
const UINT WM_APP_CONFIG = WM_APP + 1;   // the settings changed (read them from Shared)
const UINT WM_APP_MEDIA = WM_APP + 2;    // lParam: widgets::MediaInfo*
const UINT WM_APP_CHANGE = WM_APP + 3;   // wParam: Change
const UINT_PTR kFrameTimer = 1, kTickTimer = 2, kHoverTimer = 3, kLeaveTimer = 4;

// The window: room for the largest card and the bubble beside it, the island's top 8 DIPs
// below the top of the work area.
const float kBoxW = 480, kBoxH = 200, kTop = 8, kBubble = 36, kBubbleGap = 8;
// How long the short notes stay (ms); a pointer over the island keeps them.
const ULONGLONG kNoteMs = 2600, kCardMs = 5500;
// Paused music stays this long before the island lets it go.
const ULONGLONG kPausedMs = 30000;

// What the main thread hands over: the settings, the texts, the reading direction.
struct Shared {
    SRWLOCK lock = SRWLOCK_INIT;
    Config config;
    widgets::Texts texts;
    bool rtl = false;
    HANDLE stop = nullptr;
    std::atomic<HWND> window{nullptr};
};

// A spring towards `target`, a little bouncy, like the island on the iPhone. `eps` is how
// close counts as there: a quarter of a DIP for sizes, far less for values from 0 to 1.
struct Spring {
    float x = 0, v = 0, target = 0;
    float stiffness = 260, damping = 0.78f, eps = 0.25f;

    void Step(float dt) {
        const float c = 2 * sqrtf(stiffness) * damping;
        v += (-stiffness * (x - target) - c * v) * dt;
        x += v * dt;
    }
    bool Settled() const { return fabsf(x - target) < eps && fabsf(v) < eps * 8; }
    void Snap() {
        x = target;
        v = 0;
    }
};

INT64 FileTimeNow() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return ((INT64)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

bool Expandable(View v) {
    return v == View::Music || v == View::Call || v == View::Mirroring;
}

// What the island shows: a view, small or grown. A change of face cross-fades.
struct Face {
    View view = View::None;
    bool expanded = false;
    bool operator==(const Face& o) const { return view == o.view && expanded == o.expanded; }
};

// Actions that are buttons: they light up under the pointer and give when pressed.
bool IsButton(Action a) {
    return a != Action::None && a != Action::Toggle && a != Action::ToggleMute;
}

float Smooth(float t) {
    t = std::min(std::max(t, 0.0f), 1.0f);
    return t * t * (3 - 2 * t);
}

// How long a view's symbols take to come in (the longest: the low battery's blinks).
const float kIntro = 1.6f;

// A cover's bytes as a premultiplied square picture `side` px across (the middle of it).
ComPtr<IWICBitmapSource> DecodeCover(IWICImagingFactory* wic, const std::vector<BYTE>& bytes, UINT side) {
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICBitmapClipper> clipper;
    ComPtr<IWICBitmapScaler> scaler;
    ComPtr<IWICFormatConverter> converter;
    ComPtr<IWICBitmap> cached;
    UINT w = 0, h = 0;
    if (FAILED(wic->CreateStream(stream.put())) ||
        FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()), (DWORD)bytes.size())) ||
        FAILED(wic->CreateDecoderFromStream(stream.get(), nullptr, WICDecodeMetadataCacheOnDemand, decoder.put())) ||
        FAILED(decoder->GetFrame(0, frame.put())) || FAILED(frame->GetSize(&w, &h)) || w == 0 || h == 0) {
        return nullptr;
    }
    const UINT square = std::min(w, h);
    const WICRect middle = {(INT)(w - square) / 2, (INT)(h - square) / 2, (INT)square, (INT)square};
    if (FAILED(wic->CreateBitmapClipper(clipper.put())) || FAILED(clipper->Initialize(frame.get(), &middle)) ||
        FAILED(wic->CreateBitmapScaler(scaler.put())) ||
        FAILED(scaler->Initialize(clipper.get(), side, side, WICBitmapInterpolationModeFant)) ||
        FAILED(wic->CreateFormatConverter(converter.put())) ||
        FAILED(converter->Initialize(scaler.get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                     WICBitmapPaletteTypeCustom)) ||
        FAILED(wic->CreateBitmapFromSource(converter.get(), WICBitmapCacheOnLoad, cached.put()))) {
        return nullptr;
    }
    ComPtr<IWICBitmapSource> out;
    cached.As(&out);
    return out;
}

// A program's icon, `side` px, premultiplied; nullptr if it has none.
ComPtr<IWICBitmapSource> ProgramIcon(IWICImagingFactory* wic, const std::wstring& exe, int side) {
    HICON icon = nullptr;
    UINT id = 0;
    if (exe.empty() || PrivateExtractIconsW(exe.c_str(), 0, side, side, &icon, &id, 1, 0) != 1 || !icon) return nullptr;
    ComPtr<IWICBitmap> bitmap;
    ComPtr<IWICFormatConverter> converter;
    ComPtr<IWICBitmap> cached;
    const bool ok = SUCCEEDED(wic->CreateBitmapFromHICON(icon, bitmap.put())) &&
                    SUCCEEDED(wic->CreateFormatConverter(converter.put())) &&
                    SUCCEEDED(converter->Initialize(bitmap.get(), GUID_WICPixelFormat32bppPBGRA,
                                                    WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)) &&
                    SUCCEEDED(wic->CreateBitmapFromSource(converter.get(), WICBitmapCacheOnLoad, cached.put()));
    DestroyIcon(icon);
    if (!ok) return nullptr;
    ComPtr<IWICBitmapSource> out;
    cached.As(&out);
    return out;
}

// ---------------------------------------------------------------------------
// The island's thread
// ---------------------------------------------------------------------------

class Island {
public:
    Island(Shared* shared, HWND notify, UINT msg) : shared_(shared), notify_(notify), msg_(msg) {}
    ~Island();
    bool Create();
    void Run();

private:
    static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM wp, LPARAM lp);
    LRESULT Handle(UINT m, WPARAM wp, LPARAM lp);

    void ReadConfig();
    void ReadEverything();
    void OnMedia(widgets::MediaInfo* info);
    void OnChange(Change change, LPARAM lp);
    void OnPower();
    void Note(View view, ULONGLONG ms);
    void Update();
    void Place();
    void Animate();
    void Wake();
    void Render();
    Scene SceneFor(View view, bool expanded) const;
    Action ButtonAt(POINT px) const;
    INT64 MusicPosition() const;
    bool BarsLive() const;
    void Click(POINT px);
    void Request(const wchar_t* uri);

    Shared* shared_;
    HWND notify_;
    UINT msg_;
    HWND hwnd_ = nullptr;
    widgets::Graphics g_;
    widgets::Surface surface_;
    Config config_;
    widgets::Texts texts_;
    bool rtl_ = false;
    Capabilities caps_;
    Audio audio_;
    Watcher watcher_;
    widgets::MediaWatcher media_;
    // The sound behind the level bars: the music app's own, the microphone's in a call. Only
    // while those bars show; nothing is recorded or kept.
    Spectrum spectrum_;
    MicMeter mic_;
    bool micOn_ = false;
    std::wstring soundSource_;   // the app the spectrum listens to
    DWORD soundPid_ = 0;
    float micLevels_[5] = {};    // the last five readings of the microphone, newest last
    float micStep_ = 0;
    void FollowSound();
    bool Levels(float out[5]) const;

    // What goes on.
    widgets::MediaInfo music_;
    ULONGLONG musicSeen_ = 0;   // when it last played
    ComPtr<IWICBitmapSource> art_;
    D2D1_COLOR_F accent_ = widgets::Rgb(0xFA233B);
    CallInfo call_;
    ComPtr<IWICBitmapSource> callIcon_;
    bool micMuted_ = false;
    MirrorInfo mirror_;
    View note_ = View::None;
    ULONGLONG noteUntil_ = 0;
    int airplane_ = -1, cellular_ = -1, battery_ = 0;
    bool onAc_ = true;
    std::wstring output_, outputId_;
    bool headphones_ = false;

    // What is shown and how.
    View shown_ = View::None, bubble_ = View::None;
    View preferred_ = View::None;   // the activity the bubble was clicked for: it comes first
    bool expanded_ = false, visible_ = false, hovering_ = false, tracking_ = false;
    bool suppressed_ = false;   // something to show, but a full-screen app is in front
    Spring w_, h_, presence_, bubbleIn_;
    float time_ = 0;

    // Motion (seconds of time_). Off with Windows' animation effects.
    bool motion_ = true;
    Face face_, prevFace_;          // the face now and the one fading out
    D2D1_SIZE_F prevSize_ = {};     // the size the old face was laid out for
    float faceStart_ = -10;         // when the face came up
    float playStart_ = -10;         // when play and pause last traded places
    float trackStart_ = -10;        // when the last song gave way
    ComPtr<IWICBitmapSource> prevArt_;
    std::wstring prevTitle_, prevBy_;
    Spring live_, hoverAmount_, pressAmount_;
    Action hover_ = Action::None, lit_ = Action::None, press_ = Action::None;
    LARGE_INTEGER last_ = {};
    UINT frameMs_ = 0;
    float unit_ = 1;
    POINT origin_ = {};   // the window's top left, px
    SIZE size_ = {};
    std::vector<Hit> hits_;
};

Island::~Island() {
    spectrum_.Stop();
    mic_.Stop();
    media_.Stop();
    watcher_.Stop();
    audio_.Stop();
    if (hwnd_) DestroyWindow(hwnd_);
    // Covers posted to a window that is gone: nobody else frees them.
    MSG msg;
    while (PeekMessageW(&msg, nullptr, WM_APP_MEDIA, WM_APP_MEDIA, PM_REMOVE)) delete (widgets::MediaInfo*)msg.lParam;
}

bool Island::Create() {
    if (!g_.Init(L"en-US")) return false;
    // Values from 0 to 1: settled when within a fraction of a percent. Hover lights evenly,
    // a press bounces back.
    for (Spring* s : {&presence_, &bubbleIn_, &live_, &hoverAmount_, &pressAmount_}) s->eps = 0.004f;
    hoverAmount_.stiffness = 320;
    hoverAmount_.damping = 1;
    pressAmount_.stiffness = 520;
    pressAmount_.damping = 0.55f;
    live_.stiffness = 90;
    live_.damping = 1;
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = Proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);
    // Layered (per-pixel alpha: only the island takes clicks), on top, never activated.
    hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kClass, L"", WS_POPUP,
                            0, 0, 1, 1, nullptr, nullptr, wc.hInstance, this);
    if (!hwnd_) {
        Log(L"island: CreateWindowEx failed: %lu", GetLastError());
        return false;
    }
    BOOL on = TRUE;
    DwmSetWindowAttribute(hwnd_, DWMWA_EXCLUDED_FROM_PEEK, &on, sizeof(on));
    shared_->window = hwnd_;
    caps_ = DetectCapabilities();
    ReadConfig();
    audio_.Start(hwnd_, WM_APP_CHANGE);
    watcher_.Start(hwnd_, WM_APP_CHANGE);
    media_.Start(hwnd_, WM_APP_MEDIA, true, config_.youtube);
    ReadEverything();
    Log(L"island: on (battery %d, wireless %d, mobile broadband %d)", caps_.battery, caps_.wireless, caps_.cellular);
    Update();
    return true;
}

void Island::Run() {
    for (;;) {
        const DWORD r = MsgWaitForMultipleObjectsEx(1, &shared_->stop, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (r == WAIT_OBJECT_0) return;
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) return;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
}

void Island::ReadConfig() {
    AcquireSRWLockShared(&shared_->lock);
    config_ = shared_->config;
    texts_ = shared_->texts;
    rtl_ = shared_->rtl;
    ReleaseSRWLockShared(&shared_->lock);
    BOOL animations = TRUE;
    motion_ = !SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0) || animations;
}

// The state at start: nothing of it is news, so no note comes up for it.
void Island::ReadEverything() {
    call_ = ReadCall();
    callIcon_ = ProgramIcon(g_.Wic(), call_.path, 96);
    micMuted_ = audio_.MicMuted();
    mirror_ = ReadMirroring();
    airplane_ = AirplaneMode();
    cellular_ = CellularState();
    output_ = audio_.OutputName(&headphones_, &outputId_);
    SYSTEM_POWER_STATUS power;
    if (GetSystemPowerStatus(&power)) {
        onAc_ = power.ACLineStatus == 1;
        battery_ = power.BatteryLifePercent <= 100 ? power.BatteryLifePercent : 0;
    }
}

void Island::OnMedia(widgets::MediaInfo* info) {
    std::unique_ptr<widgets::MediaInfo> owned(info);
    const bool newCover = info->art != music_.art;
    // Another song: the last one's cover and words fade out under the new ones.
    const bool newSong = music_.present && (info->title != music_.title || info->artist != music_.artist);
    if (newSong || (newCover && art_)) {
        prevArt_ = art_;
        prevTitle_ = music_.title;
        prevBy_ = music_.artist.empty() ? music_.app : music_.artist;
        trackStart_ = time_;
    }
    if (music_.present && info->playing != music_.playing) playStart_ = time_;
    music_ = *info;
    if (music_.playing) musicSeen_ = GetTickCount64();
    if (newCover) {
        art_ = music_.art ? DecodeCover(g_.Wic(), *music_.art, 128) : nullptr;
        accent_ = AccentOf(art_.get());
    }
    // Another app plays: find its process again (looked up only now, not every frame).
    if (music_.source != soundSource_) {
        soundSource_ = music_.source;
        soundPid_ = 0;
    }
    Update();
}

// Listens to the music app while its bars show, and to the microphone during a call; both
// stop as soon as the bars are gone.
void Island::FollowSound() {
    const bool music = visible_ && !suppressed_ && shown_ == View::Music && music_.playing;
    if (music) {
        if (!soundPid_ && !soundSource_.empty()) soundPid_ = AudioProcessFor(soundSource_);
        spectrum_.Start(soundPid_);
    } else {
        spectrum_.Stop();
    }
    const bool call = visible_ && !suppressed_ && shown_ == View::Call && !micMuted_;
    if (call && !micOn_) micOn_ = mic_.Start();
    if (!call && micOn_) {
        mic_.Stop();
        micOn_ = false;
        for (float& v : micLevels_) v = 0;
    }
}

// The bars' heights from the sound, if there is any to read.
bool Island::Levels(float out[5]) const {
    if (shown_ == View::Music) return spectrum_.Read(out);
    if (shown_ == View::Call && micOn_) {
        for (int i = 0; i < 5; ++i) out[i] = micLevels_[i];
        return true;
    }
    return false;
}

void Island::Note(View view, ULONGLONG ms) {
    // The same note again (muted twice): its symbol plays once more.
    if (view == note_) faceStart_ = time_;
    note_ = view;
    noteUntil_ = GetTickCount64() + ms;
    Update();
}

void Island::OnChange(Change change, LPARAM lp) {
    switch (change) {
        case kCallChanged: {
            CallInfo now = ReadCall();
            if (now.path != call_.path) callIcon_ = ProgramIcon(g_.Wic(), now.path, 96);
            call_ = now;
            micMuted_ = audio_.MicMuted();
            Update();
            break;
        }
        case kAirplaneChanged: {
            const int now = AirplaneMode();
            if (now != airplane_ && airplane_ != -1 && now != -1 && config_.airplane && caps_.wireless) {
                Note(now == 1 ? View::AirplaneOn : View::AirplaneOff, now == 1 ? kCardMs : kNoteMs);
            }
            airplane_ = now;
            break;
        }
        case kNetworkChanged: {
            const int now = CellularState();
            // Mobile data turned off (not by airplane mode, which has its own note).
            if (cellular_ == 1 && now == 0 && config_.mobileData && caps_.cellular && AirplaneMode() != 1) {
                Note(View::MobileData, kCardMs);
            }
            cellular_ = now;
            break;
        }
        case kOutputChanged: {
            audio_.FollowOutput();
            std::wstring id;
            bool headphones = false;
            const std::wstring name = audio_.OutputName(&headphones, &id);
            if (id != outputId_ && !id.empty() && !outputId_.empty() && config_.output) {
                output_ = name;
                headphones_ = headphones;
                Note(View::Output, kNoteMs + 600);
            }
            outputId_ = id;
            break;
        }
        case kMuteChanged:
            if (config_.silent) Note(lp ? View::Silent : View::Sound, kNoteMs - 600);
            break;
    }
}

void Island::OnPower() {
    SYSTEM_POWER_STATUS power;
    if (!GetSystemPowerStatus(&power) || !caps_.battery) return;
    const bool ac = power.ACLineStatus == 1;
    const int level = power.BatteryLifePercent <= 100 ? power.BatteryLifePercent : battery_;
    const int before = battery_;
    battery_ = level;
    if (ac && !onAc_ && config_.charging) {
        Note(View::Charging, kNoteMs);
    } else if (!ac && config_.lowBattery && ((before > 20 && level <= 20) || (before > 10 && level <= 10))) {
        Note(View::LowBattery, kNoteMs + 1400);
    }
    onAc_ = ac;
}

// What to show now: a note, else the most important activity (a call, then mirroring,
// then music); the next one in the bubble.
void Island::Update() {
    const ULONGLONG now = GetTickCount64();
    if (note_ != View::None && now >= noteUntil_ && !hovering_) note_ = View::None;
    std::vector<View> activities;
    if (config_.calls && call_.active) activities.push_back(View::Call);
    if (config_.mirroring && mirror_.active) activities.push_back(View::Mirroring);
    if (config_.music && music_.present && (music_.playing || now - musicSeen_ < kPausedMs)) {
        activities.push_back(View::Music);
    }
    // A click on the bubble put its activity first, for as long as it lasts.
    auto it = std::find(activities.begin(), activities.end(), preferred_);
    if (it == activities.end()) preferred_ = View::None;
    else std::rotate(activities.begin(), it, it + 1);
    View shown = note_ != View::None ? note_ : activities.empty() ? View::None : activities[0];
    View bubble = View::None;
    if (note_ != View::None) {
        if (!activities.empty()) bubble = activities[0];
    } else if (activities.size() > 1) {
        bubble = activities[1];
    }
    // Out of the way of a full-screen game, video or presentation; a slow look tells when it
    // is over.
    suppressed_ = shown != View::None && FullScreenBusy();
    if (suppressed_) {
        shown = bubble = View::None;
        SetTimer(hwnd_, kTickTimer, 2000, nullptr);
    }

    // Nothing to show and nothing to wait for: no clock either.
    if (!suppressed_ && shown == View::None && !visible_) KillTimer(hwnd_, kTickTimer);

    const bool appearing = shown != View::None && !visible_;
    if (shown != shown_) {
        if (!Expandable(shown) || note_ != View::None) expanded_ = false;
        // A change of activity squeezes the island a little, as it does on the iPhone.
        if (shown_ != View::None && shown != View::None) w_.v -= 140;
        shown_ = shown;
    }
    bubble_ = bubble;
    const Face face = {shown_, expanded_ && Expandable(shown_)};
    if (!(face == face_)) {
        prevFace_ = visible_ ? face_ : Face{};
        prevSize_ = {w_.target, h_.target};
        face_ = face;
        faceStart_ = time_;
    }
    if (appearing) {
        Place();
        visible_ = true;
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    FollowSound();
    if (shown_ != View::None) {
        const D2D1_SIZE_F size = Size(g_, SceneFor(shown_, expanded_));
        w_.target = size.width;
        h_.target = size.height;
        if (appearing) {
            // It grows out of a small pill.
            w_.x = std::min(size.width, 110.0f);
            h_.x = 26;
            w_.v = h_.v = 0;
            presence_.x = 0;
        }
    } else {
        w_.target = std::min(w_.x, 110.0f);
        h_.target = 26;
    }
    presence_.target = shown_ != View::None ? 1.0f : 0.0f;
    bubbleIn_.target = bubble_ != View::None ? 1.0f : 0.0f;
    if (visible_) {
        // The clock of a call, the song's position, the notes' end, a game going full screen.
        if (!suppressed_) SetTimer(hwnd_, kTickTimer, 1000, nullptr);
        frameMs_ = 0;   // Animate picks the pace again
        Animate();
    }
}

void Island::Place() {
    HMONITOR mon = config_.followActive ? MonitorFromWindow(GetForegroundWindow(), MONITOR_DEFAULTTOPRIMARY)
                                        : MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = {sizeof(mi)};
    if (!GetMonitorInfoW(mon, &mi)) return;
    UINT dpiX = 96, dpiY = 96;
    GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    unit_ = dpiX / 96.0f;
    size_ = {(LONG)lroundf(kBoxW * unit_), (LONG)lroundf(kBoxH * unit_)};
    origin_ = {(mi.rcWork.left + mi.rcWork.right) / 2 - size_.cx / 2, mi.rcWork.top};
    surface_.Resize(g_, size_.cx, size_.cy);
}

INT64 Island::MusicPosition() const {
    if (music_.duration <= 0) return 0;
    INT64 p = music_.position;
    if (music_.playing && music_.updated > 0) p += std::max<INT64>(0, FileTimeNow() - music_.updated);
    return std::min(p, music_.duration);
}

bool Island::BarsLive() const {
    return (shown_ == View::Music && music_.playing) || (shown_ == View::Call && !micMuted_);
}

Scene Island::SceneFor(View view, bool expanded) const {
    Scene s;
    s.view = view;
    s.expanded = expanded && Expandable(view);
    s.bubble = bubble_;
    s.time = time_;
    s.texts = &texts_;
    s.rtl = rtl_;
    s.media = &music_;
    s.art = art_.get();
    s.accent = accent_;
    s.position = MusicPosition();
    s.call = &call_;
    s.appIcon = callIcon_.get();
    s.micMuted = micMuted_;
    s.callSeconds = call_.active ? std::max<INT64>(0, (FileTimeNow() - call_.start) / 10000000) : 0;
    s.mirror = &mirror_;
    s.battery = battery_;
    s.output = output_;
    s.headphones = headphones_;
    s.live = std::min(std::max(live_.x, 0.0f), 1.0f);
    if (view == shown_) s.hasLevels = Levels(s.levels);
    if (motion_) {
        s.since = time_ - faceStart_;
        s.hover = lit_;
        s.hoverAmount = hoverAmount_.x;
        s.press = press_;
        s.pressAmount = pressAmount_.x;
        s.playSwap = std::min((time_ - playStart_) / 0.42f, 1.0f);
        s.trackSwap = std::min((time_ - trackStart_) / 0.5f, 1.0f);
        if (s.trackSwap < 1) {
            s.prevArt = prevArt_.get();
            s.prevTitle = prevTitle_;
            s.prevBy = prevBy_;
        }
    } else {
        s.live = BarsLive() ? 1.0f : 0.0f;
    }
    return s;
}

// One step of the springs and a frame; the timer runs while something moves.
void Island::Animate() {
    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    float dt = last_.QuadPart ? (float)(now.QuadPart - last_.QuadPart) / (float)freq.QuadPart : 0.016f;
    last_ = now;
    dt = std::min(dt, 1.0f / 30);
    time_ += dt;
    // The microphone's level walks along the bars, a new reading every 70 ms: the call's voice
    // seen as it goes, rather than five bars jumping together. A square root, because a
    // speaking voice peaks far below full scale.
    if (micOn_) {
        micStep_ += dt;
        if (micStep_ >= 0.07f) {
            micStep_ = 0;
            for (int i = 0; i < 4; ++i) micLevels_[i] = micLevels_[i + 1];
            micLevels_[4] = std::min(sqrtf(mic_.Peak()) * 1.3f, 1.0f);
        }
    }
    // A few small steps: the springs stay stable at any frame rate.
    live_.target = BarsLive() ? 1.0f : 0.0f;
    hoverAmount_.target = hover_ != Action::None ? 1.0f : 0.0f;
    for (int i = 0; i < 4; ++i) {
        for (Spring* s : {&w_, &h_, &presence_, &bubbleIn_, &live_, &hoverAmount_, &pressAmount_}) s->Step(dt / 4);
    }
    presence_.x = std::min(std::max(presence_.x, 0.0f), 1.0f);
    hoverAmount_.x = std::min(std::max(hoverAmount_.x, 0.0f), 1.0f);
    bool moving = !w_.Settled() || !h_.Settled() || !presence_.Settled() || !bubbleIn_.Settled();
    // Symbols coming in, a song or play state changing, buttons lighting up or springing back.
    if (motion_) {
        moving = moving || time_ - faceStart_ < kIntro || time_ - playStart_ < 0.45f || time_ - trackStart_ < 0.55f ||
                 !live_.Settled() || !hoverAmount_.Settled() || !pressAmount_.Settled();
    }
    if (!moving && shown_ == View::None && presence_.x < 0.02f) {
        // Gone: nothing to draw, no timers.
        visible_ = false;
        KillTimer(hwnd_, kFrameTimer);
        if (!suppressed_) KillTimer(hwnd_, kTickTimer);
        frameMs_ = 0;
        last_.QuadPart = 0;
        ShowWindow(hwnd_, SW_HIDE);
        FollowSound();
        return;
    }
    Render();
    // Smooth while it moves; the level bars (and the song's progress under them) and a title
    // gliding along need fewer.
    const bool steady = BarsLive() || (shown_ != View::None && Scrolls(g_, SceneFor(shown_, expanded_)));
    const UINT pace = moving ? 15 : steady ? 33 : 0;
    if (pace != frameMs_) {
        frameMs_ = pace;
        if (pace) SetTimer(hwnd_, kFrameTimer, pace, nullptr);
        else KillTimer(hwnd_, kFrameTimer);
        if (!pace) last_.QuadPart = 0;
    }
}

// Something started to move (the pointer, a press): frames until it settles.
void Island::Wake() {
    if (!visible_) return;
    frameMs_ = 0;
    Animate();
}

void Island::Render() {
    if (!visible_ || !size_.cx) return;
    ID2D1RenderTarget* rt = surface_.Begin();
    if (!rt) return;
    rt->SetTransform(D2D1::Matrix3x2F::Scale(unit_, unit_));
    hits_.clear();
    const float p = std::min(std::max(presence_.x, 0.0f), 1.0f);
    if (p > 0.005f) {
        ComPtr<ID2D1Layer> layer;
        const bool faded = p < 0.999f && SUCCEEDED(rt->CreateLayer(nullptr, layer.put()));
        if (faded) {
            rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                                D2D1::IdentityMatrix(), p),
                          layer.get());
        }
        const Scene s = SceneFor(shown_, expanded_);
        // Its edges on whole pixels: sharp, not smeared over two.
        const float left = roundf((kBoxW / 2 - std::max(w_.x, 24.0f) / 2) * unit_) / unit_;
        const float right = roundf((kBoxW / 2 + std::max(w_.x, 24.0f) / 2) * unit_) / unit_;
        const float w = right - left, h = roundf(std::max(h_.x, 20.0f) * unit_) / unit_;
        const float cx = (left + right) / 2;
        // The bubble comes out from under the island's edge.
        if (bubbleIn_.x > 0.01f && bubble_ != View::None) {
            const float in = std::min(std::max(bubbleIn_.x, 0.0f), 1.2f);
            const float size = kBubble * in;
            const float home = cx + w / 2 + kBubbleGap + kBubble / 2, edge = cx + w / 2 - kBubble / 2;
            const float bx = edge + (home - edge) * std::min(in, 1.0f), by = kTop + kBubble / 2;
            DrawBubble(g_, rt, s, bx, by, size, std::min(in, 1.0f));
            hits_.push_back({{bx - kBubble / 2, by - kBubble / 2, bx + kBubble / 2, by + kBubble / 2}, Action::Swap});
        }
        DrawShape(g_, rt, cx, kTop, w, h);
        // The old face leaves quickly, shrinking a little; the new one comes in as the shape
        // gets near where it is going, laid out for that size so it does not wobble with it.
        const float t = motion_ ? time_ - faceStart_ : 10;
        const float out = 1 - Smooth(t / 0.16f);
        if (prevFace_.view != View::None && out > 0.01f) {
            Scene old = SceneFor(prevFace_.view, prevFace_.expanded);
            old.since = 10;
            DrawContent(g_, rt, old, cx, kTop, w, h, prevSize_.width, prevSize_.height, out, 1 - 0.06f * (1 - out),
                        nullptr);
        }
        const float off = fabsf(w - w_.target) + fabsf(h - h_.target);
        const float settle = std::min(std::max(1 - off / 48, 0.0f), 1.0f);
        const float in = Smooth((t - 0.04f) / 0.26f);
        DrawContent(g_, rt, s, cx, kTop, w, h, w_.target, h_.target, settle * in, 0.94f + 0.06f * in, &hits_);
        if (faded) rt->PopLayer();
    }
    surface_.End();
    POINT src = {0, 0};
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(hwnd_, nullptr, &origin_, &size_, surface_.Dc(), &src, 0, &blend, ULW_ALPHA);
}

void Island::Request(const wchar_t* uri) {
    auto* target = new std::wstring(uri);
    if (!PostMessageW(notify_, msg_, kOpen, (LPARAM)target)) delete target;
}

// The button under a point of the window (px), if any.
Action Island::ButtonAt(POINT px) const {
    const D2D1_POINT_2F at = {px.x / unit_, px.y / unit_};
    for (const Hit& h : hits_) {
        if (at.x >= h.rect.left && at.x < h.rect.right && at.y >= h.rect.top && at.y < h.rect.bottom) {
            return IsButton(h.action) ? h.action : Action::None;
        }
    }
    return Action::None;
}

void Island::Click(POINT px) {
    const D2D1_POINT_2F at = {px.x / unit_, px.y / unit_};
    Action action = Action::None;
    for (const Hit& h : hits_) {
        if (at.x >= h.rect.left && at.x < h.rect.right && at.y >= h.rect.top && at.y < h.rect.bottom) {
            action = h.action;
            break;   // buttons come before the area around them
        }
    }
    switch (action) {
        case Action::Toggle:
            // With the pointer growing it, a click only grows it (it shrinks when the pointer
            // leaves); else a click grows and shrinks it.
            if (Expandable(shown_) && (!expanded_ || !config_.hoverExpand)) expanded_ = !expanded_;
            break;
        case Action::PlayPause:
            // To the app shown, not to whatever Windows' media keys would reach (a film, say).
            if (!media_.Send(widgets::MediaCommand::PlayPause)) PressMediaKey(VK_MEDIA_PLAY_PAUSE);
            music_.playing = !music_.playing;   // Windows tells the truth in a moment
            playStart_ = time_;
            if (music_.playing) musicSeen_ = GetTickCount64();
            break;
        case Action::Next:
            if (!media_.Send(widgets::MediaCommand::Next)) PressMediaKey(VK_MEDIA_NEXT_TRACK);
            break;
        case Action::Previous:
            if (!media_.Send(widgets::MediaCommand::Previous)) PressMediaKey(VK_MEDIA_PREV_TRACK);
            break;
        case Action::MicMute:
            audio_.SetMicMuted(!micMuted_);
            micMuted_ = audio_.MicMuted();
            break;
        case Action::ShowApp: ShowCallWindow(call_); break;
        case Action::StopMirroring:
            StopMirroring(mirror_);
            expanded_ = false;
            break;
        case Action::OpenAirplane:
            Request(L"ms-settings:network-airplanemode");
            note_ = View::None;
            break;
        case Action::OpenCellular:
            Request(L"ms-settings:network-cellular");
            note_ = View::None;
            break;
        case Action::Dismiss: note_ = View::None; break;
        case Action::ToggleMute: audio_.SetMuted(!audio_.Muted()); break;
        case Action::Swap:
            // The bubble's activity takes the island (a note there goes).
            if (bubble_ == View::Music) musicSeen_ = GetTickCount64();
            preferred_ = bubble_;
            note_ = View::None;
            expanded_ = false;
            break;
        case Action::None: return;
    }
    Update();
}

LRESULT CALLBACK Island::Proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (m == WM_NCCREATE) {
        auto* cs = (CREATESTRUCTW*)lp;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
    }
    auto* self = (Island*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (self && self->hwnd_ == h) return self->Handle(m, wp, lp);
    return DefWindowProcW(h, m, wp, lp);
}

LRESULT Island::Handle(UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
        case WM_APP_CONFIG:
            ReadConfig();
            media_.SetYouTube(config_.youtube);
            caps_ = DetectCapabilities();
            if (visible_) Place();
            Update();
            return 0;
        case WM_APP_MEDIA:
            OnMedia((widgets::MediaInfo*)lp);
            return 0;
        case WM_APP_CHANGE:
            OnChange((Change)wp, lp);
            return 0;
        case WM_POWERBROADCAST:
            if (wp == PBT_APMPOWERSTATUSCHANGE) OnPower();
            return TRUE;
        case WM_DISPLAYCHANGE: {
            const MirrorInfo now = ReadMirroring();
            if (!(now == mirror_)) mirror_ = now;
            if (visible_) Place();
            Update();
            return 0;
        }
        case WM_SETTINGCHANGE:
            if (wp == SPI_SETWORKAREA && visible_) {
                Place();
                Render();
            }
            return 0;
        case WM_TIMER:
            if (wp == kFrameTimer) {
                Animate();
            } else if (wp == kTickTimer) {
                Update();
            } else if (wp == kHoverTimer) {
                KillTimer(hwnd_, kHoverTimer);
                if (hovering_ && Expandable(shown_) && note_ == View::None && !expanded_) {
                    expanded_ = true;
                    Update();
                }
            } else if (wp == kLeaveTimer) {
                KillTimer(hwnd_, kLeaveTimer);
                if (!hovering_ && expanded_) {
                    expanded_ = false;
                    Update();
                }
            }
            return 0;
        case WM_MOUSEMOVE: {
            if (!tracking_) {
                TRACKMOUSEEVENT t = {sizeof(t), TME_LEAVE, hwnd_, 0};
                tracking_ = TrackMouseEvent(&t) != FALSE;
                hovering_ = true;
                KillTimer(hwnd_, kLeaveTimer);
                if (config_.hoverExpand && Expandable(shown_) && !expanded_) SetTimer(hwnd_, kHoverTimer, 280, nullptr);
            }
            // The button under the pointer lights up; the one it left fades.
            const Action a = ButtonAt({(short)LOWORD(lp), (short)HIWORD(lp)});
            if (a != hover_) {
                hover_ = a;
                if (a != Action::None) {
                    lit_ = a;
                    hoverAmount_.x = hoverAmount_.v = 0;
                }
                Wake();
            }
            return 0;
        }
        case WM_LBUTTONDOWN:
            press_ = ButtonAt({(short)LOWORD(lp), (short)HIWORD(lp)});
            if (press_ != Action::None) {
                pressAmount_.target = 1;
                Wake();
            }
            return 0;
        case WM_MOUSELEAVE:
            tracking_ = false;
            hovering_ = false;
            hover_ = Action::None;
            pressAmount_.target = 0;
            Wake();
            KillTimer(hwnd_, kHoverTimer);
            if (expanded_ && config_.hoverExpand) SetTimer(hwnd_, kLeaveTimer, 450, nullptr);
            // A note the pointer kept up ends now (if its time is over).
            if (note_ != View::None) Update();
            return 0;
        case WM_LBUTTONUP:
            // The pressed button springs back as its action happens.
            pressAmount_.target = 0;
            Click({(short)LOWORD(lp), (short)HIWORD(lp)});
            Wake();
            return 0;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_CLOSE:
            return 0;   // only its thread ends it
    }
    return DefWindowProcW(hwnd_, m, wp, lp);
}

}  // namespace

// ---------------------------------------------------------------------------
// IslandHost (main thread)
// ---------------------------------------------------------------------------

namespace {
Shared* g_shared = nullptr;   // owned by IslandHost; one island per process
}  // namespace

void IslandHost::Init(HWND notify, UINT msg) {
    notify_ = notify;
    msg_ = msg;
}

void IslandHost::Apply(const Config& config, const widgets::Texts& texts, bool rtl) {
    if (!config.enabled) {
        Stop();
        return;
    }
    if (!g_shared) {
        g_shared = new Shared();
        g_shared->stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    }
    AcquireSRWLockExclusive(&g_shared->lock);
    g_shared->config = config;
    g_shared->texts = texts;
    g_shared->rtl = rtl;
    ReleaseSRWLockExclusive(&g_shared->lock);
    if (thread_.joinable()) {
        // The thread reads them itself; it takes them when it starts anyway.
        if (HWND w = g_shared->window.load()) PostMessageW(w, WM_APP_CONFIG, 0, 0);
        return;
    }
    ResetEvent(g_shared->stop);
    Shared* shared = g_shared;
    HWND notify = notify_;
    UINT msg = msg_;
    thread_ = std::thread([shared, notify, msg] {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        {
            Island island(shared, notify, msg);
            if (island.Create()) island.Run();
            shared->window = nullptr;
        }
        CoUninitialize();
    });
}

void IslandHost::Stop() {
    if (!thread_.joinable()) return;
    SetEvent(g_shared->stop);
    thread_.join();
    Log(L"island: off");
}

// ---------------------------------------------------------------------------
// Snapshot
// ---------------------------------------------------------------------------

int IslandSnapshot(const std::wstring& dir, const std::wstring& language) {
    i18n::Init(language.empty() ? L"auto" : language);
    CreateDirectoryW(dir.c_str(), nullptr);
    widgets::Graphics g;
    if (!g.Init(i18n::LocaleName())) return 1;
    const widgets::Texts texts = i18n::All(L"host.island.");
    const float unit = 1.5f, W = 820, H = 760;

    widgets::MediaInfo media;
    media.present = media.playing = true;
    media.title = L"Golden Hour on the Coast Road";
    media.artist = L"The Lanterns";
    media.app = L"Spotify";
    media.duration = 211 * 10000000LL;
    // A short title (the pill narrows to it) and one too long for it (it glides).
    widgets::MediaInfo shortSong = media, longSong = media;
    shortSong.title = L"Halo";
    longSong.title = L"The Long Way Home Through the Northern Lights (Extended Night Drive Mix)";
    CallInfo call = {true, L"Discord", L"", 0, true};
    MirrorInfo mirror = {true, true, L"Living Room TV"};

    struct Sample {
        View view;
        bool expanded;
        View bubble = View::None;
        bool micMuted = false;
        const widgets::MediaInfo* song = nullptr;
    } samples[] = {
        {View::Music, false},           {View::Music, false, View::None, false, &shortSong},
        {View::Music, false, View::None, false, &longSong}, {View::Music, false, View::Call}, {View::Call, false},
        {View::Mirroring, false},       {View::Charging, false},          {View::LowBattery, false},
        {View::Silent, false},          {View::Sound, false},             {View::AirplaneOff, false},
        {View::Output, false},          {View::Music, true},              {View::Call, true},
        {View::Call, true, View::None, true}, {View::Mirroring, true},    {View::AirplaneOn, false},
        {View::MobileData, false},
    };

    auto make = [&](View view, bool expanded, View bubble, bool micMuted, const widgets::MediaInfo* song) {
        Scene s;
        s.view = view;
        s.expanded = expanded;
        s.bubble = bubble;
        s.time = 0.7f;
        s.texts = &texts;
        s.rtl = i18n::IsRightToLeft();
        s.media = song ? song : &media;
        s.position = 50 * 10000000LL;
        s.call = &call;
        s.micMuted = micMuted;
        s.live = micMuted ? 0.0f : 1.0f;
        s.callSeconds = 724;
        s.mirror = &mirror;
        s.battery = view == View::LowBattery ? 10 : 81;
        s.output = L"WH-1000XM5";
        s.headphones = true;
        return s;
    };
    // A wallpaper-like backdrop, so the island's edge shows as it would.
    auto backdrop = [](ID2D1RenderTarget* rt, float w, float h) {
        ComPtr<ID2D1GradientStopCollection> stops;
        ComPtr<ID2D1LinearGradientBrush> wallpaper;
        const D2D1_GRADIENT_STOP gs[] = {{0, widgets::Rgb(0xFF6A3D)}, {0.45f, widgets::Rgb(0xC2185B)},
                                         {0.75f, widgets::Rgb(0x4A148C)}, {1, widgets::Rgb(0x0D47A1)}};
        if (SUCCEEDED(rt->CreateGradientStopCollection(gs, 4, stops.put())) &&
            SUCCEEDED(rt->CreateLinearGradientBrush({{0, 0}, {w, h}}, stops.get(), wallpaper.put()))) {
            rt->FillRectangle({0, 0, w, h}, wallpaper.get());
        }
    };

    widgets::Surface surface;
    if (!surface.Resize(g, (int)(W * unit), (int)(H * unit))) return 1;
    ID2D1RenderTarget* rt = surface.Begin();
    if (!rt) return 1;
    rt->SetTransform(D2D1::Matrix3x2F::Scale(unit, unit));
    backdrop(rt, W, H);
    float x = 20, y = 20, rowH = 0;
    for (const Sample& sample : samples) {
        const Scene s = make(sample.view, sample.expanded, sample.bubble, sample.micMuted, sample.song);
        const D2D1_SIZE_F size = Size(g, s);
        const float total = size.width + (s.bubble != View::None ? kBubbleGap + kBubble : 0);
        if (x + total > W - 20) {
            x = 20;
            y += rowH + 18;
            rowH = 0;
        }
        Draw(g, rt, s, x + size.width / 2, y, size.width, size.height, 1, nullptr);
        if (s.bubble != View::None) {
            DrawBubble(g, rt, s, x + size.width + kBubbleGap + kBubble / 2, y + kBubble / 2, kBubble, 1);
        }
        x += total + 18;
        rowH = std::max(rowH, size.height);
    }
    surface.End();
    if (!widgets::SavePng(g, surface.Bits(), surface.Width(), surface.Height(), dir + L"\\island.png")) return 1;

    // island-motion.png: how each view comes in (and play and pause trade places), frame by
    // frame from 0 to 1.6 s, to check the motion without a desktop.
    struct Motion {
        View view;
        bool expanded;
        bool swap;    // play and pause instead of the view coming in
        bool glide;   // a long title gliding along (seconds 0 to 7 instead)
    } motions[] = {{View::Music, false, false, true}, {View::Music, true, false, true},
                   {View::Music, false, false}, {View::Music, true, false}, {View::Music, true, true},
                   {View::Call, false, false},  {View::Call, true, false},  {View::Charging, false, false},
                   {View::Silent, false, false}, {View::AirplaneOff, false, false}, {View::Output, false, false},
                   {View::MobileData, false, false}};
    const float times[] = {0, 0.06f, 0.12f, 0.2f, 0.3f, 0.45f, 0.7f, 1.6f};
    const float MW = 20 + ARRAYSIZE(times) * 380.0f, MH = 20 + ARRAYSIZE(motions) * 196.0f;
    widgets::Surface strip;
    if (!strip.Resize(g, (int)MW, (int)MH) || !(rt = strip.Begin())) return 1;
    backdrop(rt, MW, MH);
    y = 20;
    for (const Motion& m : motions) {
        Scene s = make(m.view, m.expanded, View::None, false, m.glide ? &longSong : nullptr);
        const D2D1_SIZE_F size = Size(g, s);
        for (size_t i = 0; i < ARRAYSIZE(times); ++i) {
            if (m.swap) s.playSwap = std::min(times[i] / 0.42f, 1.0f);
            else if (!m.glide) s.since = times[i];
            s.time = m.glide ? i * 1.0f : 0.7f + times[i];
            Draw(g, rt, s, 20 + 380.0f * i + 180, y, size.width, size.height, 1, nullptr);
        }
        y += 196;
    }
    strip.End();
    return widgets::SavePng(g, strip.Bits(), strip.Width(), strip.Height(), dir + L"\\island-motion.png") ? 0 : 1;
}

}  // namespace island
