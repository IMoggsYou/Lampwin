// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

#pragma once
#include <string>
#include <vector>
#include "host/island/island_sources.h"
#include "host/widgets/canvas.h"
#include "host/widgets/media.h"
#include "host/widgets/widget_draw.h"

// What the island looks like: a black pill at the top of the screen that grows into a card,
// after Apple's Dynamic Island (compact, expanded and minimal views). All in DIPs.
namespace island {

using widgets::Texts;

enum class View {
    None,
    Music,
    Call,
    Mirroring,
    Charging,
    LowBattery,
    Silent,
    Sound,
    AirplaneOn,
    AirplaneOff,
    MobileData,
    Output,   // another sound output (headphones connected)
};

// What a click on a part of the island does.
enum class Action {
    None,
    Toggle,          // the compact view: grow or shrink
    PlayPause,
    Next,
    Previous,
    MicMute,
    ShowApp,
    StopMirroring,
    OpenAirplane,    // Windows' airplane mode settings
    OpenCellular,    // Windows' mobile network settings
    Dismiss,
    ToggleMute,
    Swap,            // the bubble: its activity takes the island
};

struct Hit {
    D2D1_RECT_F rect;
    Action action;
};

struct Scene {
    View view = View::None;
    bool expanded = false;
    View bubble = View::None;   // a second activity, as a small circle beside the island
    float time = 0;             // seconds, for the level bars
    const Texts* texts = nullptr;
    bool rtl = false;
    // Music
    const widgets::MediaInfo* media = nullptr;
    bool hasLevels = false;            // the sound as it plays, five bands (bass to treble), 0 to 1
    float levels[5] = {};
    IWICBitmapSource* art = nullptr;   // the cover, premultiplied; nullptr: the music tile
    D2D1_COLOR_F accent = {0.98f, 0.14f, 0.23f, 1};   // the bars: the cover's colour
    INT64 position = 0;                // where the song is now (100 ns)
    // Call
    const CallInfo* call = nullptr;
    IWICBitmapSource* appIcon = nullptr;
    bool micMuted = false;
    INT64 callSeconds = 0;
    // Mirroring
    const MirrorInfo* mirror = nullptr;
    // Battery
    int battery = 0;
    // Sound output
    std::wstring output;
    bool headphones = false;
    // Motion. Each value rests at its default; with Windows' animation effects off the host
    // leaves them there.
    float since = 10;                // seconds this view has been up: its symbols come in
    float live = 1;                  // how far the level bars move (0: still)
    Action hover = Action::None;     // the button under the pointer, and how lit it is
    float hoverAmount = 0;
    Action press = Action::None;     // the button held down, and how far it is pressed in
    float pressAmount = 0;
    float playSwap = 1;              // play and pause trading places, 0 to 1
    float trackSwap = 1;             // the previous song giving way to this one, 0 to 1
    IWICBitmapSource* prevArt = nullptr;
    std::wstring prevTitle, prevBy;
};

// The size of the island for this scene (without the bubble).
D2D1_SIZE_F Size(widgets::Graphics& g, const Scene& s);
// The black shape of size (w, h) with its top centre at (cx, top).
void DrawShape(widgets::Graphics& g, ID2D1RenderTarget* rt, float cx, float top, float w, float h);
// What is on the island: `s` laid out for (cw, ch) from the shape's top centre, clipped to the
// shape (w, h), at `opacity` and `scale` (about its middle). Clickable parts go to `hits`.
void DrawContent(widgets::Graphics& g, ID2D1RenderTarget* rt, const Scene& s, float cx, float top, float w, float h,
                 float cw, float ch, float opacity, float scale, std::vector<Hit>* hits);
// The shape and, with `content` > 0, what is on it at that opacity.
void Draw(widgets::Graphics& g, ID2D1RenderTarget* rt, const Scene& s, float cx, float top, float w, float h,
          float content, std::vector<Hit>* hits);
// The bubble (a second activity), `size` across, centred on (cx, cy).
void DrawBubble(widgets::Graphics& g, ID2D1RenderTarget* rt, const Scene& s, float cx, float cy, float size,
                float content);

// Whether something on the island moves by itself (a title too long for its place glides
// along): frames are needed even when nothing else moves.
bool Scrolls(widgets::Graphics& g, const Scene& s);

// The colour that stands for a cover in the level bars (its most colourful average).
D2D1_COLOR_F AccentOf(IWICBitmapSource* art);

// "3:07", "1:02:45".
std::wstring Clock(INT64 seconds);

}  // namespace island
