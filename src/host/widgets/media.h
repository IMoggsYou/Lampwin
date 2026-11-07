// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

#pragma once
#include <windows.h>
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// "Now playing" for its widget and the island: the music that Windows' own media controls
// know about (the volume flyout, the lock screen) - the song, its artist and the app playing
// it. Only music apps count (Spotify, Apple Music, Amazon Music, TIDAL, YouTube Music, and
// YouTube when asked for): a film or a video call never shows up. Read on this PC only;
// nothing is sent anywhere.
namespace widgets {

struct MediaInfo {
    bool present = false;   // something is playing or paused
    bool playing = false;
    std::wstring title, artist, app;
    std::wstring source;    // the app's id in the media controls (its AppUserModelId)
    // Only with details (the island): the cover as the app gives it (an image file's bytes;
    // none when it has no cover) - the same pointer as long as the song is the same - and
    // where the song is: `position` at `updated` (a FILETIME, UTC), both in 100 ns units;
    // `duration` 0 when the app doesn't say.
    std::shared_ptr<const std::vector<BYTE>> art;
    INT64 position = 0, duration = 0, updated = 0;

    bool operator==(const MediaInfo& o) const = default;
};

enum class MediaCommand { PlayPause, Next, Previous };

// Asks Windows every 2 seconds on a thread of its own, only while it runs (a Now playing
// widget is on screen, the island is on).
class MediaWatcher {
public:
    ~MediaWatcher() { Stop(); }
    // `target` gets `message` with a new MediaInfo* in lParam (the receiver deletes it): at
    // once, then whenever it changes. `details`: the cover and the timeline too. `youtube`:
    // videos on YouTube count as music too.
    void Start(HWND target, UINT message, bool details = false, bool youtube = false);
    void Stop();
    bool Running() const { return thread_.joinable(); }
    void SetYouTube(bool youtube);
    // Play, pause or skip in the app shown (not in whatever Windows' media keys would reach,
    // which may be a film); false when the watcher isn't running.
    bool Send(MediaCommand command);

private:
    struct Shared {
        std::atomic<bool> youtube{false};
        std::atomic<int> command{-1};
        HANDLE stop = nullptr, wake = nullptr;
        ~Shared();
    };
    std::thread thread_;
    std::shared_ptr<Shared> shared_;
};

// The app's name from its media session id ("Spotify.exe", "MSEdge",
// "SpotifyAB.SpotifyMusic_zpdnekdrzrea0!Spotify"); empty when it can't be told.
std::wstring MediaAppName(const std::wstring& appUserModelId);

// What a media session is: music from a music app (or YouTube Music in a browser, which
// gives an album), a YouTube video (a browser session with a channel but no album), or
// something else (a film, a call, a game).
enum class MediaKind { Other, Music, Video };
MediaKind ClassifyMedia(const std::wstring& appUserModelId, const std::wstring& artist, const std::wstring& album);

}  // namespace widgets
