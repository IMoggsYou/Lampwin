// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

#pragma once
#include <windows.h>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// "Now playing" for its widget: what Windows' own media controls show (the volume flyout,
// the lock screen) - the song or video, its artist and the app playing it. Read on this PC
// only; nothing is sent anywhere.
namespace widgets {

struct MediaInfo {
    bool present = false;   // something is playing or paused
    bool playing = false;
    std::wstring title, artist, app;
    // Only with details (the island): the cover as the app gives it (an image file's bytes;
    // none when it has no cover) - the same pointer as long as the song is the same - and
    // where the song is: `position` at `updated` (a FILETIME, UTC), both in 100 ns units;
    // `duration` 0 when the app doesn't say.
    std::shared_ptr<const std::vector<BYTE>> art;
    INT64 position = 0, duration = 0, updated = 0;

    bool operator==(const MediaInfo& o) const = default;
};

// Asks Windows every 2 seconds on a thread of its own, only while it runs (a Now playing
// widget is on screen).
class MediaWatcher {
public:
    ~MediaWatcher() { Stop(); }
    // `target` gets `message` with a new MediaInfo* in lParam (the receiver deletes it): at
    // once, then whenever it changes. `details`: the cover and the timeline too.
    void Start(HWND target, UINT message, bool details = false);
    void Stop();
    bool Running() const { return thread_.joinable(); }

private:
    std::thread thread_;
    HANDLE stop_ = nullptr;
};

// The app's name from its media session id ("Spotify.exe", "MSEdge",
// "SpotifyAB.SpotifyMusic_zpdnekdrzrea0!Spotify"); empty when it can't be told.
std::wstring MediaAppName(const std::wstring& appUserModelId);

}  // namespace widgets
