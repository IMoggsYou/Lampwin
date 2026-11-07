// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

#pragma once
#include <windows.h>
#include <atomic>
#include <string>
#include <thread>
#include "host/com.h"

// The sound behind the island's level bars. Everything stays in memory on this PC: the sound
// is read a few milliseconds at a time, measured and thrown away; nothing is recorded or sent.
namespace island {

// The sound of the music, in five bands from bass to treble, for the island's level bars.
class Spectrum {
public:
    static constexpr int kBands = 5;

    Spectrum() = default;
    Spectrum(const Spectrum&) = delete;
    Spectrum& operator=(const Spectrum&) = delete;
    ~Spectrum();

    // Listens to what process `pid` (and its children) plays; pid 0, or when that can't be done,
    // to the default output as a whole. Starting again with another pid switches. Cheap to call
    // again with the same pid (no restart).
    void Start(DWORD pid);
    void Stop();   // idempotent; joins the thread
    // The five bands now, each 0..1, smoothed (fast attack, slower release); false while there is
    // nothing to show (not started, starting, failed, or the device went away).
    bool Read(float out[kBands]) const;

private:
    void Run();

    std::thread thread_;
    HANDLE wake_ = nullptr;   // auto-reset: Stop() or another pid
    std::atomic<bool> stop_{false};
    std::atomic<DWORD> pid_{0};
    std::atomic<bool> valid_{false};
    std::atomic<float> bands_[kBands] = {};
};

// The process that plays the sound of the app with this AppUserModelId: an audio session on the
// default output whose process image name (e.g. "Spotify.exe") or package family name matches the
// id ("SpotifyAB.SpotifyMusic_zpdnekdrzrea0!Spotify"). Prefers a session that is playing now.
// 0 if none. Opens every process with a session, so call it when the music changes, not per frame.
DWORD AudioProcessFor(const std::wstring& aumid);

struct PeakMeter;   // IAudioMeterInformation, declared in the .cpp

// The microphone's level (default capture device, communications role) for the call's bars.
// Start, Peak and Stop belong on one thread that has COM initialized (the island's).

class MicMeter {
public:
    MicMeter();
    MicMeter(const MicMeter&) = delete;
    MicMeter& operator=(const MicMeter&) = delete;
    ~MicMeter();

    bool Start();   // false without a microphone
    void Stop();
    float Peak() const;   // 0..1, 0 when stopped or when the device went away

private:
    ComPtr<PeakMeter> meter_;
};

}  // namespace island
