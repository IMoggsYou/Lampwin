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
struct ISessionList : IInspectable {   // IVectorView<GlobalSystemMediaTransportControlsSession>
    virtual HRESULT STDMETHODCALLTYPE GetAt(UINT32 index, ISession** item) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Size(UINT32* size) = 0;
};
struct IManager : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetCurrentSession(ISession** session) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSessions(ISessionList** sessions) = 0;
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
    virtual HRESULT STDMETHODCALLTYPE TryPlayAsync(IAsyncOp** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryPauseAsync(IAsyncOp** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryStopAsync(IAsyncOp** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryRecordAsync(IAsyncOp** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryFastForwardAsync(IAsyncOp** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryRewindAsync(IAsyncOp** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE TrySkipNextAsync(IAsyncOp** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE TrySkipPreviousAsync(IAsyncOp** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryChangeChannelUpAsync(IAsyncOp** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryChangeChannelDownAsync(IAsyncOp** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryTogglePlayPauseAsync(IAsyncOp** operation) = 0;
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

std::wstring Lower(const std::wstring& t) {
    std::wstring out;
    for (wchar_t c : t) out.push_back((wchar_t)towlower(c));
    return out;
}

int Status(ISession* session) {
    ComPtr<IPlaybackInfo> playback;
    int status = 0;
    if (SUCCEEDED(session->GetPlaybackInfo(playback.put())) && playback) playback->get_PlaybackStatus(&status);
    return status;
}

std::wstring SourceOf(ISession* session) {
    HSTRING id = nullptr;
    session->get_SourceAppUserModelId(&id);
    return Take(id);
}

// The session to show: the music app that plays (Windows' own choice first), else one that
// is paused. Films, calls and other sounds are passed over. nullptr: nothing to show.
ComPtr<ISession> PickSession(IManager* manager, HANDLE stop, bool youtube, ComPtr<IMediaProperties>* chosenProps,
                             HRESULT* error) {
    *error = S_OK;
    ComPtr<ISession> current;
    manager->GetCurrentSession(current.put());
    const std::wstring currentId = current ? SourceOf(current.get()) : std::wstring();
    ComPtr<ISessionList> list;
    UINT32 count = 0;
    HRESULT hr = manager->GetSessions(list.put());
    if (FAILED(hr) || !list || FAILED(list->get_Size(&count))) {
        *error = FAILED(hr) ? hr : E_POINTER;
        return nullptr;
    }
    ComPtr<ISession> best;
    ComPtr<IMediaProperties> bestProps;
    int bestRank = 0;
    for (UINT32 i = 0; i < count; ++i) {
        ComPtr<ISession> session;
        if (FAILED(list->GetAt(i, session.put())) || !session) continue;
        const int status = Status(session.get());
        if (status != kPlaying && status != kPaused) continue;
        ComPtr<IAsyncOp> request;
        ComPtr<IMediaProperties> props;
        if (FAILED(session->TryGetMediaPropertiesAsync(request.put())) || !request) continue;
        const HRESULT got = Await(request.get(), stop, kMediaProperties, props.put_void());
        if (got == E_ABORT && WaitForSingleObject(stop, 0) == WAIT_OBJECT_0) {
            *error = E_ABORT;
            return nullptr;
        }
        if (FAILED(got) || !props) continue;   // a session that is just closing
        HSTRING artist = nullptr, album = nullptr;
        props->get_Artist(&artist);
        props->get_AlbumTitle(&album);
        const std::wstring id = SourceOf(session.get());
        const MediaKind kind = ClassifyMedia(id, Take(artist), Take(album));
        if (kind == MediaKind::Other || (kind == MediaKind::Video && !youtube)) continue;
        // Playing beats paused; Windows' own pick beats the others.
        const int rank = (status == kPlaying ? 4 : 1) + (id == currentId ? 2 : 0);
        if (rank > bestRank) {
            bestRank = rank;
            best = session;
            bestProps = props;
        }
    }
    *chosenProps = bestProps;
    return best;
}

// The music to show. Nothing playing is an answer too (present = false); an error means the
// manager should be asked for again. With `details`, the cover (taken over from `last` for
// the same song) and the timeline too.
HRESULT ReadSession(IManager* manager, HANDLE stop, bool details, bool youtube, const MediaInfo& last,
                    MediaInfo* out) {
    *out = MediaInfo();
    ComPtr<IMediaProperties> props;
    HRESULT hr = S_OK;
    ComPtr<ISession> session = PickSession(manager, stop, youtube, &props, &hr);
    if (FAILED(hr)) return hr;
    if (!session || !props) return S_OK;
    HSTRING title = nullptr, artist = nullptr, album = nullptr;
    props->get_Title(&title);
    props->get_Artist(&artist);
    props->get_AlbumTitle(&album);
    out->title = Take(title);
    out->artist = Take(artist);
    out->source = SourceOf(session.get());
    // YouTube in a browser: its name rather than the browser's.
    const MediaKind kind = ClassifyMedia(out->source, out->artist, Take(album));
    out->app = kind == MediaKind::Video ? std::wstring(L"YouTube") : MediaAppName(out->source);
    out->playing = Status(session.get()) == kPlaying;
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
    const bool sameSong = last.present && last.title == out->title && last.artist == out->artist &&
                          last.source == out->source;
    out->art = sameSong ? last.art : ReadCover(props.get(), stop);
    return S_OK;
}

// A button in the island: sent to the session of the app shown. Its answer isn't waited
// for; the next look at the session tells.
void SendCommand(IManager* manager, const std::wstring& source, MediaCommand command) {
    ComPtr<ISessionList> list;
    UINT32 count = 0;
    if (source.empty() || FAILED(manager->GetSessions(list.put())) || !list || FAILED(list->get_Size(&count))) return;
    for (UINT32 i = 0; i < count; ++i) {
        ComPtr<ISession> session;
        if (FAILED(list->GetAt(i, session.put())) || !session || SourceOf(session.get()) != source) continue;
        ComPtr<IAsyncOp> op;
        HRESULT hr = command == MediaCommand::Next       ? session->TrySkipNextAsync(op.put())
                     : command == MediaCommand::Previous ? session->TrySkipPreviousAsync(op.put())
                                                         : session->TryTogglePlayPauseAsync(op.put());
        if (FAILED(hr)) Log(L"media: a button press didn't reach %ls (0x%08lx)", source.c_str(), (unsigned long)hr);
        return;
    }
}

const DWORD kEveryMs = 2000;
const DWORD kRetryMs = 15000;   // Windows' media service isn't answering

}  // namespace

std::wstring MediaAppName(const std::wstring& id) {
    // Known apps by a part of their id: packaged apps have ids like
    // "AppleInc.AppleMusicWin_nzyj5cx40ttyn!App" whose last part says nothing.
    const std::wstring lower = Lower(id);
    static const struct {
        const wchar_t* part;
        const wchar_t* name;
    } kKnown[] = {
        {L"spotify", L"Spotify"},
        {L"applemusic", L"Apple Music"},
        {L"amazon music", L"Amazon Music"},
        {L"amazonmusic", L"Amazon Music"},
        {L"tidal", L"TIDAL"},
        {L"wimpmusic", L"TIDAL"},
        {L"music.youtube", L"YouTube Music"},
        {L"cinhimbnkkaeohfgghhklpknlkffjgod", L"YouTube Music"},   // its Chrome app
        {L"zunemusic", L"Media Player"},
        {L"msedge", L"Microsoft Edge"},
        {L"chrome", L"Google Chrome"},
        {L"firefox", L"Firefox"},
        {L"308046b0af4a39cb", L"Firefox"},   // Firefox's id in the media controls
        {L"brave", L"Brave"},
        {L"opera", L"Opera"},
        {L"vivaldi", L"Vivaldi"},
        {L"vlc", L"VLC"},
        {L"discord", L"Discord"},
    };
    for (const auto& k : kKnown) {
        if (lower.find(k.part) != std::wstring::npos) return k.name;
    }
    std::wstring name = id;
    // A packaged app: its id after the "!".
    size_t bang = name.rfind(L'!');
    if (bang != std::wstring::npos) name = name.substr(bang + 1);
    if (name.size() > 4 && _wcsicmp(name.c_str() + name.size() - 4, L".exe") == 0) name.resize(name.size() - 4);
    // "Microsoft.ZuneMusic" -> "ZuneMusic"
    size_t dot = name.rfind(L'.');
    if (dot != std::wstring::npos && dot + 1 < name.size()) name = name.substr(dot + 1);
    // A hash nobody can read, or a packaged app's bare "App": no name rather than a wrong one.
    bool hex = name.size() >= 12;
    for (wchar_t c : name) hex = hex && iswxdigit(c);
    if (hex || name.empty() || _wcsicmp(name.c_str(), L"App") == 0) return std::wstring();
    name[0] = (wchar_t)towupper(name[0]);
    return name;
}

MediaKind ClassifyMedia(const std::wstring& id, const std::wstring& artist, const std::wstring& album) {
    const std::wstring lower = Lower(id);
    // The music apps, desktop and Store versions alike, and YouTube Music installed as an app.
    static const wchar_t* const kMusic[] = {
        L"spotify",    L"applemusic", L"amazon music", L"amazonmusic", L"tidal",
        L"wimpmusic",  L"music.youtube", L"youtube music", L"youtubemusic",
        L"cinhimbnkkaeohfgghhklpknlkffjgod",   // YouTube Music as a Chrome app
    };
    for (const wchar_t* m : kMusic) {
        if (lower.find(m) != std::wstring::npos) return MediaKind::Music;
    }
    // A browser has one session for whichever tab plays. YouTube Music gives an album; a
    // YouTube video gives its channel as the artist but no album; a film gives neither.
    static const wchar_t* const kBrowsers[] = {L"msedge", L"chrome", L"firefox", L"308046b0af4a39cb",
                                               L"brave",  L"opera",  L"vivaldi", L"thebrowsercompany"};
    for (const wchar_t* b : kBrowsers) {
        if (lower.find(b) == std::wstring::npos) continue;
        if (!album.empty()) return MediaKind::Music;
        return artist.empty() ? MediaKind::Other : MediaKind::Video;
    }
    return MediaKind::Other;
}

MediaWatcher::Shared::~Shared() {
    if (stop) CloseHandle(stop);
    if (wake) CloseHandle(wake);
}

void MediaWatcher::SetYouTube(bool youtube) {
    if (!shared_ || shared_->youtube.exchange(youtube) == youtube) return;
    SetEvent(shared_->wake);   // look again at once with the new choice
}

bool MediaWatcher::Send(MediaCommand command) {
    if (!thread_.joinable() || !shared_) return false;
    shared_->command = (int)command;
    SetEvent(shared_->wake);
    return true;
}

void MediaWatcher::Start(HWND target, UINT message, bool details, bool youtube) {
    if (thread_.joinable()) {
        SetYouTube(youtube);
        return;
    }
    auto shared = std::make_shared<Shared>();
    shared->stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    shared->wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!shared->stop || !shared->wake) return;
    shared->youtube = youtube;
    shared_ = shared;
    thread_ = std::thread([target, message, details, shared] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        {
            ComPtr<IManager> manager;
            MediaInfo last;
            bool first = true, failed = false;
            DWORD wait = 0;
            const HANDLE events[] = {shared->stop, shared->wake};
            for (;;) {
                const DWORD woke = WaitForMultipleObjects(2, events, FALSE, wait);
                if (woke == WAIT_OBJECT_0) break;
                wait = kEveryMs;
                const int command = shared->command.exchange(-1);
                if (command >= 0 && manager) {
                    SendCommand(manager.get(), last.source, (MediaCommand)command);
                    // The app needs a moment before its session tells the new state.
                    if (WaitForSingleObject(shared->stop, 250) == WAIT_OBJECT_0) break;
                }
                MediaInfo now;
                HRESULT hr = manager ? S_OK : OpenManager(shared->stop, &manager);
                if (SUCCEEDED(hr)) hr = ReadSession(manager.get(), shared->stop, details, shared->youtube, last, &now);
                if (hr == E_ABORT && WaitForSingleObject(shared->stop, 0) == WAIT_OBJECT_0) break;
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
        SetEvent(shared_->stop);
        thread_.join();
    }
    shared_.reset();
}

}  // namespace widgets
