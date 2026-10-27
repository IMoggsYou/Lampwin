// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

#pragma once
#include <windows.h>
#include <string>
#include <thread>

// "Now playing" for its widget: what Windows' own media controls show (the volume flyout,
// the lock screen) - the song or video, its artist and the app playing it. Read on this PC
// only; nothing is sent anywhere.
namespace widgets {

struct MediaInfo {
    bool present = false;   // something is playing or paused
    bool playing = false;
    std::wstring title, artist, app;

    bool operator==(const MediaInfo& o) const = default;
};

// Asks Windows every 2 seconds on a thread of its own, only while it runs (a Now playing
// widget is on screen).
class MediaWatcher {
public:
    ~MediaWatcher() { Stop(); }
    // `target` gets `message` with a new MediaInfo* in lParam (the receiver deletes it): at
    // once, then whenever it changes.
    void Start(HWND target, UINT message);
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
