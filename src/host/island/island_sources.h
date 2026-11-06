// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

#pragma once
#include <windows.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <string>
#include <thread>
#include "host/com.h"

// What the island shows besides music: everything is read on this PC, from what Windows
// itself shows (the microphone icon in the taskbar, Quick Settings, the volume flyout);
// nothing is sent anywhere and nothing is changed except on a click in the island.
namespace island {

// What this PC has: an option for hardware it lacks is not offered (the settings app
// checks the same way, settings-app/Services/IslandCapabilities.cs).
struct Capabilities {
    bool battery = false;    // a laptop or tablet battery
    bool wireless = false;   // Wi-Fi (or mobile broadband): airplane mode means something
    bool cellular = false;   // a mobile broadband modem
};
Capabilities DetectCapabilities();

// A call: a program using the microphone (Discord, Signal, Teams, Zoom, WhatsApp, a browser
// in a meeting), as Windows records it for its own microphone icon.
struct CallInfo {
    bool active = false;
    std::wstring app;    // the program's name ("Discord")
    std::wstring path;   // its exe, empty for a Store app
    INT64 start = 0;     // FILETIME (UTC) the microphone went on
    bool camera = false; // its camera is on too

    bool operator==(const CallInfo& o) const = default;
};
CallInfo ReadCall();
// The call's program to the front; false if it has no window.
bool ShowCallWindow(const CallInfo& call);

// The screen shown on another display too: a wireless display (Miracast) or a duplicated
// one (Win + P, "Duplicate").
struct MirrorInfo {
    bool active = false;
    bool wireless = false;
    std::wstring target;   // that display's name, empty if Windows doesn't know it

    bool operator==(const MirrorInfo& o) const = default;
};
MirrorInfo ReadMirroring();
// Stops it: a duplicated display goes back to extended; for a wireless display Windows'
// Cast panel opens (it disconnects there).
void StopMirroring(const MirrorInfo& m);

// Airplane mode: -1 when this PC doesn't have it, else 0 / 1.
int AirplaneMode();
// The mobile broadband connection: -1 without a modem, 0 down, 1 up.
int CellularState();
// A full-screen game, video or presentation is in front: the island stays out of the way.
bool FullScreenBusy();
// Presses a media key (play / pause, next, previous): Windows hands it to the app whose
// music its media controls show.
void PressMediaKey(WORD vk);

// The default sound output and microphone (Windows' Core Audio). Created on the island's
// thread (COM apartment); notifications arrive from Windows' threads as `message` posted
// to `target` with a Change in wParam.
enum Change : WPARAM {
    kCallChanged = 1,     // the microphone or camera went on or off for a program
    kAirplaneChanged,
    kNetworkChanged,
    kOutputChanged,       // another default sound output
    kMuteChanged,         // lParam: 1 muted, 0 not
};

class Audio {
public:
    ~Audio() { Stop(); }
    bool Start(HWND target, UINT message);
    void Stop();
    // The default output: its name and whether it is a headset or headphones. Empty
    // when there is none.
    std::wstring OutputName(bool* headphones, std::wstring* id);
    bool Muted();
    void SetMuted(bool muted);
    bool MicMuted();
    void SetMicMuted(bool muted);
    // After kOutputChanged: watch the new output's mute.
    void FollowOutput();

private:
    ComPtr<IMMDeviceEnumerator> devices_;
    ComPtr<IMMNotificationClient> client_;
    ComPtr<IAudioEndpointVolume> output_;
    ComPtr<IAudioEndpointVolumeCallback> volumeCallback_;
    HWND target_ = nullptr;
    UINT message_ = 0;
};

// Watches the registry (microphone and camera use, airplane mode) and the network on a
// thread of its own and posts a Change when something moved.
class Watcher {
public:
    ~Watcher() { Stop(); }
    void Start(HWND target, UINT message);
    void Stop();

private:
    std::thread thread_;
    HANDLE stop_ = nullptr;
    HANDLE network_ = nullptr;   // NotifyIpInterfaceChange's handle
};

}  // namespace island
