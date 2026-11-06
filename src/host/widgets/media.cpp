// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

#include "host/widgets/media.h"
#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include <asyncinfo.h>
#include <wctype.h>
#include "host/com.h"
#include "host/log.h"

namespace widgets {

namespace {

// Windows.Media.Control as the Windows SDK declares it (winrt/windows.media.control.h,
// which MinGW doesn't have): each interface up to the last method used here, in the SDK's
// order, with its IID.
struct IAsyncOp : IInspectable {   // IAsyncOperation<T>
    virtual HRESULT STDMETHODCALLTYPE put_Completed(IUnknown* handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Completed(IUnknown** handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetResults(IInspectable** results) = 0;
};

const IID kManagerStatics = {0x2050c4ee, 0x11a0, 0x57de, {0xae, 0xd7, 0xc9, 0x7c, 0x70, 0x33, 0x82, 0x45}};
struct IManagerStatics : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE RequestAsync(IAsyncOp** operation) = 0;
};

const IID kManager = {0xcace8eac, 0xe86e, 0x504a, {0xab, 0x31, 0x5f, 0xf8, 0xff, 0x1b, 0xce, 0x49}};
struct ISession;
struct IManager : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetCurrentSession(ISession** session) = 0;
};

struct ITimeline : IInspectable {   // TimeSpan and DateTime are one INT64 each (100 ns units)
    virtual HRESULT STDMETHODCALLTYPE get_StartTime(INT64* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_EndTime(INT64* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_MinSeekTime(INT64* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_MaxSeekTime(INT64* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Position(INT64* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_LastUpdatedTime(INT64* value) = 0;
};

struct IPlaybackInfo : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Controls(IInspectable** controls) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_PlaybackStatus(int* status) = 0;
};
const int kPlaying = 4;   // GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing
const int kPaused = 5;

struct ISession : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_SourceAppUserModelId(HSTRING* id) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryGetMediaPropertiesAsync(IAsyncOp** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetTimelineProperties(ITimeline** timeline) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPlaybackInfo(IPlaybackInfo** info) = 0;
};

const IID kMediaProperties = {0x68856cf6, 0xadb4, 0x54b2, {0xac, 0x16, 0x05, 0x83, 0x79, 0x07, 0xac, 0xb6}};
struct IStreamReference : IInspectable {   // Windows.Storage.Streams.IRandomAccessStreamReference
    virtual HRESULT STDMETHODCALLTYPE OpenReadAsync(IAsyncOp** operation) = 0;
};

struct IMediaProperties : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Title(HSTRING* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Subtitle(HSTRING* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AlbumArtist(HSTRING* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Artist(HSTRING* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AlbumTitle(HSTRING* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_TrackNumber(INT32* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Genres(IInspectable** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AlbumTrackCount(INT32* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_PlaybackType(IInspectable** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Thumbnail(IStreamReference** value) = 0;
};

const IID kAsyncInfo = {0x00000036, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
const IID kUnknown = {0x00000000, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

const wchar_t kManagerClass[] = L"Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager";

std::wstring Take(HSTRING s) {
    UINT32 n = 0;
    const wchar_t* raw = s ? WindowsGetStringRawBuffer(s, &n) : nullptr;
    std::wstring out = raw ? std::wstring(raw, n) : std::wstring();
    if (s) WindowsDeleteString(s);
    return out;
}

// Waits for an asynchronous call (up to 3 s, or until `stop` is set) and returns its result
// as the interface `iid`.
HRESULT Await(IAsyncOp* op, HANDLE stop, const IID& iid, void** out) {
    ComPtr<IAsyncInfo> info;
    if (FAILED(op->QueryInterface(kAsyncInfo, info.put_void()))) return E_NOINTERFACE;
    AsyncStatus status = AsyncStatus::Started;
    for (int i = 0; i < 60 && SUCCEEDED(info->get_Status(&status)) && status == AsyncStatus::Started; ++i) {
        if (WaitForSingleObject(stop, 50) == WAIT_OBJECT_0) break;
    }
    if (status != AsyncStatus::Completed) {
        if (status == AsyncStatus::Started) info->Cancel();
        return E_ABORT;
    }
    ComPtr<IInspectable> result;
    HRESULT hr = op->GetResults(result.put());
    if (FAILED(hr) || !result) return FAILED(hr) ? hr : E_POINTER;
    return result->QueryInterface(iid, out);
}

HRESULT OpenManager(HANDLE stop, ComPtr<IManager>* manager) {
    HSTRING_HEADER header;
    HSTRING cls = nullptr;
    HRESULT hr = WindowsCreateStringReference(kManagerClass, (UINT32)wcslen(kManagerClass), &header, &cls);
    ComPtr<IManagerStatics> statics;
    ComPtr<IAsyncOp> request;
    if (SUCCEEDED(hr)) hr = RoGetActivationFactory(cls, kManagerStatics, statics.put_void());
    if (SUCCEEDED(hr)) hr = statics->RequestAsync(request.put());
    if (SUCCEEDED(hr) && !request) hr = E_POINTER;
    if (SUCCEEDED(hr)) hr = Await(request.get(), stop, kManager, manager->put_void());
    return hr;
}

// The cover's bytes, read whole (a few hundred KB at most; anything bigger is skipped).
std::shared_ptr<const std::vector<BYTE>> ReadCover(IMediaProperties* props, HANDLE stop) {
    typedef HRESULT(WINAPI * CreateStream_t)(IUnknown*, REFIID, void**);
    static const auto createStream = []() -> CreateStream_t {
        HMODULE shcore = LoadLibraryExW(L"shcore.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        return shcore ? (CreateStream_t)(void*)GetProcAddress(shcore, "CreateStreamOverRandomAccessStream") : nullptr;
    }();
    ComPtr<IStreamReference> ref;
    ComPtr<IAsyncOp> open;
    ComPtr<IUnknown> randomAccess;
    ComPtr<IStream> stream;
    if (!createStream || FAILED(props->get_Thumbnail(ref.put())) || !ref || FAILED(ref->OpenReadAsync(open.put())) ||
        !open || FAILED(Await(open.get(), stop, kUnknown, randomAccess.put_void())) ||
        FAILED(createStream(randomAccess.get(), __uuidof(IStream), stream.put_void()))) {
        return nullptr;
    }
    auto bytes = std::make_shared<std::vector<BYTE>>();
    BYTE chunk[16384];
    ULONG read = 0;
    while (SUCCEEDED(stream->Read(chunk, sizeof(chunk), &read)) && read > 0) {
        bytes->insert(bytes->end(), chunk, chunk + read);
        if (bytes->size() > (8u << 20)) return nullptr;
    }
    if (bytes->empty()) return nullptr;
    return bytes;
}

// The session Windows shows in its media controls. Nothing playing is an answer too
// (present = false); an error means the manager should be asked for again. With
// `details`, the cover (taken over from `last` for the same song) and the timeline too.
HRESULT ReadSession(IManager* manager, HANDLE stop, bool details, const MediaInfo& last, MediaInfo* out) {
    *out = MediaInfo();
    ComPtr<ISession> session;
    HRESULT hr = manager->GetCurrentSession(session.put());
    if (FAILED(hr) || !session) return hr;
    ComPtr<IPlaybackInfo> playback;
    int status = 0;
    if (SUCCEEDED(session->GetPlaybackInfo(playback.put())) && playback) playback->get_PlaybackStatus(&status);
    if (status != kPlaying && status != kPaused) return S_OK;   // stopped or closed: nothing to show

    ComPtr<IAsyncOp> request;
    ComPtr<IMediaProperties> props;
    hr = session->TryGetMediaPropertiesAsync(request.put());
    if (SUCCEEDED(hr) && !request) hr = E_POINTER;
    if (SUCCEEDED(hr)) hr = Await(request.get(), stop, kMediaProperties, props.put_void());
    // A session that is just closing: nothing to show this time.
    if (FAILED(hr)) return hr == E_ABORT ? hr : S_OK;
    HSTRING title = nullptr, artist = nullptr, app = nullptr;
    props->get_Title(&title);
    props->get_Artist(&artist);
    session->get_SourceAppUserModelId(&app);
    out->title = Take(title);
    out->artist = Take(artist);
    out->app = MediaAppName(Take(app));
    out->playing = status == kPlaying;
    out->present = !out->title.empty();
    if (!details || !out->present) return S_OK;
    ComPtr<ITimeline> timeline;
    INT64 start = 0, end = 0;
    if (SUCCEEDED(session->GetTimelineProperties(timeline.put())) && timeline &&
        SUCCEEDED(timeline->get_StartTime(&start)) && SUCCEEDED(timeline->get_EndTime(&end)) && end > start &&
        SUCCEEDED(timeline->get_Position(&out->position)) && SUCCEEDED(timeline->get_LastUpdatedTime(&out->updated))) {
        out->duration = end - start;
        out->position -= start;
    } else {
        out->position = out->duration = out->updated = 0;
    }
    const bool sameSong =
        last.present && last.title == out->title && last.artist == out->artist && last.app == out->app;
    out->art = sameSong ? last.art : ReadCover(props.get(), stop);
    return S_OK;
}

const DWORD kEveryMs = 2000;
const DWORD kRetryMs = 15000;   // Windows' media service isn't answering

}  // namespace

std::wstring MediaAppName(const std::wstring& id) {
    std::wstring name = id;
    // A packaged app: its id after the "!".
    size_t bang = name.rfind(L'!');
    if (bang != std::wstring::npos) name = name.substr(bang + 1);
    if (name.size() > 4 && _wcsicmp(name.c_str() + name.size() - 4, L".exe") == 0) name.resize(name.size() - 4);
    // "Microsoft.ZuneMusic" -> "ZuneMusic"
    size_t dot = name.rfind(L'.');
    if (dot != std::wstring::npos && dot + 1 < name.size()) name = name.substr(dot + 1);
    std::wstring lower;
    for (wchar_t c : name) lower.push_back((wchar_t)towlower(c));
    static const struct {
        const wchar_t* id;
        const wchar_t* name;
    } kKnown[] = {
        {L"msedge", L"Microsoft Edge"}, {L"chrome", L"Google Chrome"}, {L"firefox", L"Firefox"},
        {L"308046b0af4a39cb", L"Firefox"},   // Firefox's id in the media controls
        {L"brave", L"Brave"}, {L"opera", L"Opera"}, {L"vivaldi", L"Vivaldi"}, {L"spotify", L"Spotify"},
        {L"zunemusic", L"Media Player"}, {L"vlc", L"VLC"}, {L"discord", L"Discord"},
    };
    for (const auto& k : kKnown) {
        if (lower == k.id) return k.name;
    }
    // A hash nobody can read: no name rather than a wrong one.
    bool hex = name.size() >= 12;
    for (wchar_t c : name) hex = hex && iswxdigit(c);
    if (hex || name.empty()) return std::wstring();
    name[0] = (wchar_t)towupper(name[0]);
    return name;
}

void MediaWatcher::Start(HWND target, UINT message, bool details) {
    if (thread_.joinable()) return;
    if (!stop_) stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stop_) return;
    ResetEvent(stop_);
    HANDLE stop = stop_;
    thread_ = std::thread([target, message, stop, details] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        {
            ComPtr<IManager> manager;
            MediaInfo last;
            bool first = true, failed = false;
            DWORD wait = 0;
            while (WaitForSingleObject(stop, wait) == WAIT_TIMEOUT) {
                wait = kEveryMs;
                MediaInfo now;
                HRESULT hr = manager ? S_OK : OpenManager(stop, &manager);
                if (SUCCEEDED(hr)) hr = ReadSession(manager.get(), stop, details, last, &now);
                if (hr == E_ABORT && WaitForSingleObject(stop, 0) == WAIT_OBJECT_0) break;
                if (FAILED(hr)) {
                    if (!failed) Log(L"widgets: Windows' media controls don't answer (0x%08lx)", (unsigned long)hr);
                    failed = true;
                    manager.reset();
                    wait = kRetryMs;
                    now = MediaInfo();
                } else {
                    failed = false;
                }
                if (first || !(now == last)) {
                    first = false;
                    last = now;
                    auto* posted = new MediaInfo(now);
                    if (!PostMessageW(target, message, 0, (LPARAM)posted)) delete posted;
                }
            }
        }
        CoUninitialize();
    });
}

void MediaWatcher::Stop() {
    if (thread_.joinable()) {
        SetEvent(stop_);
        thread_.join();
    }
    if (stop_) {
        CloseHandle(stop_);
        stop_ = nullptr;
    }
}

}  // namespace widgets
