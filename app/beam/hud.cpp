#include "beam/hud.h"

#include "beamstatus.h"
#include "streaming/session.h"
#include "streaming/video/decoder.h"

#include <QFile>
#include <QRegularExpression>

#include <cmath>
#include <mutex>
#include <vector>

namespace {

// One filled rectangle of an icon, in grid cells, and its colour.
struct Cell {
    int x, y, w, h;
    Uint8 r, g, b;
};
using Icon = std::vector<Cell>;

enum Slot { SlotNetwork, SlotDecode, SlotEncode, SlotGamepad, SlotGamepadOff, SlotCount };

const char* const kIconFiles[SlotCount] = {
    ":/res/hud/beam-hud-signal-1.svg",
    ":/res/hud/beam-hud-gpu-decode.svg",
    ":/res/hud/beam-hud-gpu-encode.svg",
    ":/res/hud/beam-hud-gamepad.svg",
    ":/res/hud/beam-hud-gamepad.svg",  // drawn red: see redden()
};

constexpr int kGrid = 24;            // the icons' grid
constexpr int kTilePadding = 4;      // around an icon, in grid cells
constexpr int kTileGap = 6;          // between tiles, in grid cells
constexpr int kTileRadius = 6;       // the tile's rounded corners, in grid cells
constexpr int kMargin = 8;           // from the screen's corner, in grid cells
const SDL_Color kChampagne = {241, 221, 191, 128};  // the tile: champagne at half opacity

constexpr Uint32 kRedrawMs = 125;    // eight redraws a second, enough for a slow pulse
constexpr Uint32 kPulseMs = 2000;    // one pulse, dim to bright to dim
constexpr int kSecondsToShow = 3;    // bad seconds before a warning appears
constexpr int kSecondsToClear = 3;   // good seconds before it goes
constexpr Uint32 kGamepadShowMs = 5000;  // the controller icon, after the mode turns on or off

// The thresholds. Starting points, to be tuned against real sessions.
constexpr Uint32 kPingPoorMs = 80;
constexpr double kLossPoor = 0.02;

// On after kSecondsToShow seconds asking for it, off after kSecondsToClear asking not to, so one bad
// second never shows and one good one never hides.
struct Steady {
    int level = 0;
    int upFor = 0;
    int downFor = 0;

    // True when the level changed.
    bool feed(int wanted)
    {
        upFor = wanted > level ? upFor + 1 : 0;
        downFor = wanted < level ? downFor + 1 : 0;
        if (upFor >= kSecondsToShow || downFor >= kSecondsToClear) {
            level = wanted;
            upFor = downFor = 0;
            return true;
        }
        return false;
    }
};

std::mutex s_Lock;
bool s_Active = false;
bool s_Warnings = true;
int s_Scale = 2;
SDL_TimerID s_Timer = 0;
bool s_Shown = false;         // whether a picture is up, so an empty HUD is cleared once
bool s_ConnectionPoor = false;
Steady s_Network, s_Decode, s_Encode;
bool s_Gamepad = false;
Uint32 s_GamepadUntil = 0;  // when the controller icon goes, after the mode turned on or off
bool s_Demo = false;
Icon s_Icons[SlotCount];

Icon loadIcon(const char* path)
{
    Icon icon;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Beam HUD: cannot read %s", path);
        return icon;
    }
    const QString svg = QString::fromUtf8(file.readAll());

    // The icons are drawn as filled paths of rectangles: `<path fill="#rrggbb" d="Mx yhWvHh-Wz…">`.
    static const QRegularExpression pathRe(QStringLiteral("<path fill=\"#([0-9a-fA-F]{6})\" d=\"([^\"]+)\""));
    static const QRegularExpression rectRe(QStringLiteral("M(\\d+) (\\d+)h(\\d+)v(\\d+)h-\\d+z"));
    for (auto paths = pathRe.globalMatch(svg); paths.hasNext();) {
        const auto path = paths.next();
        const uint rgb = path.captured(1).toUInt(nullptr, 16);
        const QString d = path.captured(2);
        for (auto rects = rectRe.globalMatch(d); rects.hasNext();) {
            const auto rect = rects.next();
            icon.push_back({rect.captured(1).toInt(), rect.captured(2).toInt(),
                            rect.captured(3).toInt(), rect.captured(4).toInt(),
                            Uint8(rgb >> 16), Uint8(rgb >> 8), Uint8(rgb)});
        }
    }
    return icon;
}

// The controller as the mode turns off: its body in the two reds of the network icon's bar.
Icon redden(Icon icon)
{
    for (Cell& c : icon) {
        if (c.r == 0xec && c.g == 0xe5 && c.b == 0xd6) { c.r = 0xff; c.g = 0x5f; c.b = 0x6d; }
        else if (c.r == 0xbf && c.g == 0xb7 && c.b == 0xa8) { c.r = 0xc2; c.g = 0x41; c.b = 0x4d; }
    }
    return icon;
}

Uint32 argb(Uint8 r, Uint8 g, Uint8 b, int a)
{
    return (Uint32(a) << 24) | (Uint32(r) << 16) | (Uint32(g) << 8) | b;
}

// The icons on screen now, in their fixed order.
std::vector<Slot> visibleSlots(Uint32 now)
{
    std::vector<Slot> onScreen;
    if (s_Demo) {
        for (int i = 0; i < SlotCount; i++) onScreen.push_back(Slot(i));
        return onScreen;
    }
    if (s_Warnings) {
        if (s_Network.level) onScreen.push_back(SlotNetwork);
        if (s_Decode.level) onScreen.push_back(SlotDecode);
        if (s_Encode.level) onScreen.push_back(SlotEncode);
    }
    if (SDL_TICKS_PASSED(now, s_GamepadUntil) == SDL_FALSE) onScreen.push_back(s_Gamepad ? SlotGamepad : SlotGamepadOff);
    return onScreen;
}

// The HUD as one surface, the screen's top-left corner at its origin. Called with s_Lock held.
SDL_Surface* compose(const std::vector<Slot>& onScreen, Uint32 now)
{
    const int s = s_Scale;
    const int tile = (kGrid + 2 * kTilePadding) * s;
    const int count = (int)onScreen.size();
    const int width = kMargin * s + count * tile + (count - 1) * kTileGap * s;
    const int height = kMargin * s + tile;

    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
    if (surface == nullptr) {
        return nullptr;
    }
    SDL_FillRect(surface, nullptr, 0);

    // The pulse: brightness from 40% to 100% and back, smoothly, once every kPulseMs.
    const double phase = (now % kPulseMs) / double(kPulseMs);
    const double pulse = 0.4 + 0.6 * (0.5 + 0.5 * std::cos(phase * 2 * 3.14159265358979));

    Uint32* pixels = (Uint32*)surface->pixels;
    const int stride = surface->pitch / 4;
    auto fill = [&](int x, int y, int w, int h, Uint32 colour) {
        for (int yy = y; yy < y + h; yy++) {
            for (int xx = x; xx < x + w; xx++) {
                pixels[yy * stride + xx] = colour;
            }
        }
    };

    // A rounded square, its corners smoothed: each pixel's share of the shape sets its opacity.
    auto roundedTile = [&](int x0, int y0, int size, double radius, SDL_Color c, double alpha) {
        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                // Distance outside the corner's circle, for a pixel in a corner; inside elsewhere.
                const double cx = x + 0.5 < radius ? radius : (x + 0.5 > size - radius ? size - radius : x + 0.5);
                const double cy = y + 0.5 < radius ? radius : (y + 0.5 > size - radius ? size - radius : y + 0.5);
                const double d = std::hypot(x + 0.5 - cx, y + 0.5 - cy);
                const double cover = SDL_clamp(radius - d + 0.5, 0.0, 1.0);
                if (cover > 0) {
                    pixels[(y0 + y) * stride + x0 + x] = argb(c.r, c.g, c.b, int(c.a * alpha * cover));
                }
            }
        }
    };

    for (int i = 0; i < count; i++) {
        const double alpha = onScreen[i] == SlotGamepad || onScreen[i] == SlotGamepadOff ? 1.0 : pulse;
        const int x0 = kMargin * s + i * (tile + kTileGap * s);
        const int y0 = kMargin * s;

        roundedTile(x0, y0, tile, kTileRadius * s, kChampagne, alpha);

        // The icon, each cell a whole number of pixels.
        const int ix = x0 + kTilePadding * s;
        const int iy = y0 + kTilePadding * s;
        for (const Cell& c : s_Icons[onScreen[i]]) {
            fill(ix + c.x * s, iy + c.y * s, c.w * s, c.h * s, argb(c.r, c.g, c.b, int(255 * alpha)));
        }
    }
    return surface;
}

Uint32 redraw(Uint32 interval, void*)
{
    std::lock_guard<std::mutex> guard(s_Lock);
    Session* session = Session::get();
    if (!s_Active || session == nullptr) {
        return interval;
    }

    const Uint32 now = SDL_GetTicks();
    const std::vector<Slot> onScreen = visibleSlots(now);
    if (onScreen.empty()) {
        if (s_Shown) {
            session->getOverlayManager().setOverlaySurface(Overlay::OverlayBeamHud, nullptr);
            s_Shown = false;
        }
        return interval;
    }

    // Redrawn every tick, steady icons too: a decoder rebuilt mid-stream -- full screen to a window,
    // say -- starts with no overlay, and this puts the HUD back within a tick.
    session->getOverlayManager().setOverlaySurface(Overlay::OverlayBeamHud, compose(onScreen, now));
    s_Shown = true;
    return interval;
}

} // namespace

namespace BeamHud
{

void begin(SDL_Window* window, bool warnings)
{
    SDL_DisplayMode mode = {};
    const int screenHeight =
        SDL_GetCurrentDisplayMode(SDL_GetWindowDisplayIndex(window), &mode) == 0 ? mode.h : 1080;

    std::lock_guard<std::mutex> guard(s_Lock);
    if (s_Icons[0].empty()) {
        for (int i = 0; i < SlotCount; i++) {
            s_Icons[i] = loadIcon(kIconFiles[i]);
        }
        s_Icons[SlotGamepadOff] = redden(s_Icons[SlotGamepadOff]);
    }
    // 48 pixels on a 1080-line screen, 72 on 1440, 96 on 4K: whole multiples of the 24-cell grid.
    s_Scale = SDL_max(1, (int)std::lround(screenHeight / 540.0));
    s_Warnings = warnings;
    s_Active = true;
    if (s_Timer == 0) {
        s_Timer = SDL_AddTimer(kRedrawMs, redraw, nullptr);
    }
}

void end()
{
    std::lock_guard<std::mutex> guard(s_Lock);
    s_Active = false;
    if (s_Timer != 0) {
        SDL_RemoveTimer(s_Timer);
        s_Timer = 0;
    }
}

void connectionStatus(bool poor)
{
    std::lock_guard<std::mutex> guard(s_Lock);
    s_ConnectionPoor = poor;
}

void videoWindow(const _VIDEO_STATS& window, int streamFps)
{
    std::lock_guard<std::mutex> guard(s_Lock);
    if (!s_Active) {
        return;
    }

    const double frameUs = 1000000.0 / (streamFps > 0 ? streamFps : 60);
    const double loss = window.totalFrames ? double(window.networkDroppedFrames) / window.totalFrames : 0;
    const Uint32 ping = window.lastRtt;  // 0 when not known yet
    const double decodeUs = window.decodedFrames ? double(window.totalDecodeTimeUs) / window.decodedFrames : 0;
    // The host's processing time comes in tenths of a millisecond.
    const double encodeUs = window.framesWithHostProcessingLatency
        ? window.totalHostProcessingLatency * 100.0 / window.framesWithHostProcessingLatency : 0;

    const bool networkPoor = s_ConnectionPoor || ping > kPingPoorMs || loss > kLossPoor;
    if (s_Network.feed(networkPoor ? 1 : 0)) {
        BeamStatus::notice("network", QString("%1 (ping %2 ms, %3% lost)").arg(s_Network.level ? "poor" : "off")
                           .arg(ping).arg(loss * 100, 0, 'f', 1));
    }
    if (s_Decode.feed(decodeUs > frameUs ? 1 : 0)) {
        BeamStatus::notice("decode", QString("%1 (%2 ms a frame, %3 ms allowed)").arg(s_Decode.level ? "on" : "off")
                           .arg(decodeUs / 1000, 0, 'f', 1).arg(frameUs / 1000, 0, 'f', 1));
    }
    if (s_Encode.feed(encodeUs > frameUs ? 1 : 0)) {
        BeamStatus::notice("encode", QString("%1 (%2 ms a frame, %3 ms allowed)").arg(s_Encode.level ? "on" : "off")
                           .arg(encodeUs / 1000, 0, 'f', 1).arg(frameUs / 1000, 0, 'f', 1));
    }
}

void demo(bool on)
{
    std::lock_guard<std::mutex> guard(s_Lock);
    s_Demo = on;
}

void gamepadMouse(bool active)
{
    std::lock_guard<std::mutex> guard(s_Lock);
    if (s_Gamepad != active) {
        s_Gamepad = active;
        // Shown as the mode turns on (as drawn) or off (red), and gone after kGamepadShowMs.
        s_GamepadUntil = SDL_GetTicks() + kGamepadShowMs;
        BeamStatus::notice("gamepad-mouse", active ? "on" : "off");
    }
}

}
