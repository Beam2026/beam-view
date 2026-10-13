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

enum Slot { SlotNetworkPoor, SlotNetworkBad, SlotDecode, SlotEncode, SlotGamepad, SlotCount };

const char* const kIconFiles[SlotCount] = {
    ":/res/hud/beam-hud-signal-2.svg",
    ":/res/hud/beam-hud-signal-1.svg",
    ":/res/hud/beam-hud-gpu-down.svg",
    ":/res/hud/beam-hud-gpu-up.svg",
    ":/res/hud/beam-hud-gamepad.svg",
};

constexpr int kGrid = 24;            // the icons' grid
constexpr int kTilePadding = 4;      // around an icon, in grid cells
constexpr int kTileGap = 4;          // between tiles, in grid cells
constexpr int kMargin = 8;           // from the screen's corner, in grid cells
const SDL_Color kChampagne = {241, 221, 191, 128};  // the tile: champagne at half opacity

constexpr Uint32 kRedrawMs = 125;    // eight redraws a second, enough for a slow pulse
constexpr Uint32 kPulseMs = 2000;    // one pulse, dim to bright to dim
constexpr int kSecondsToShow = 3;    // bad seconds before a warning appears
constexpr int kSecondsToClear = 3;   // good seconds before it goes

// The thresholds. Starting points, to be tuned against real sessions.
constexpr Uint32 kPingPoorMs = 80;
constexpr Uint32 kPingBadMs = 150;
constexpr double kLossPoor = 0.02;
constexpr double kLossBad = 0.10;

// A level that moves up after kSecondsToShow seconds asking for more, and down after
// kSecondsToClear asking for less, so one bad second never shows and one good one never hides.
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

Uint32 argb(Uint8 r, Uint8 g, Uint8 b, int a)
{
    return (Uint32(a) << 24) | (Uint32(r) << 16) | (Uint32(g) << 8) | b;
}

// The onScreen on screen now, in their fixed order.
std::vector<Slot> visibleSlots()
{
    std::vector<Slot> onScreen;
    if (s_Demo) {
        for (int i = 0; i < SlotCount; i++) onScreen.push_back(Slot(i));
        return onScreen;
    }
    if (s_Warnings) {
        if (s_Network.level == 2) onScreen.push_back(SlotNetworkBad);
        else if (s_Network.level == 1) onScreen.push_back(SlotNetworkPoor);
        if (s_Decode.level) onScreen.push_back(SlotDecode);
        if (s_Encode.level) onScreen.push_back(SlotEncode);
    }
    if (s_Gamepad) onScreen.push_back(SlotGamepad);
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

    for (int i = 0; i < count; i++) {
        const double alpha = onScreen[i] == SlotGamepad ? 1.0 : pulse;
        const int x0 = kMargin * s + i * (tile + kTileGap * s);
        const int y0 = kMargin * s;

        // The tile, with its corners cut one cell in, the pixel-art way of rounding.
        const Uint32 tint = argb(kChampagne.r, kChampagne.g, kChampagne.b, int(kChampagne.a * alpha));
        fill(x0 + s, y0, tile - 2 * s, tile, tint);
        fill(x0, y0 + s, s, tile - 2 * s, tint);
        fill(x0 + tile - s, y0 + s, s, tile - 2 * s, tint);

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

    const std::vector<Slot> onScreen = visibleSlots();
    if (onScreen.empty()) {
        if (s_Shown) {
            session->getOverlayManager().setOverlaySurface(Overlay::OverlayBeamHud, nullptr);
            s_Shown = false;
        }
        return interval;
    }

    // Redrawn every tick, steady icons too: a decoder rebuilt mid-stream -- full screen to a window,
    // say -- starts with no overlay, and this puts the HUD back within a tick.
    session->getOverlayManager().setOverlaySurface(Overlay::OverlayBeamHud, compose(onScreen, SDL_GetTicks()));
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

    const int network = (ping > kPingBadMs || loss > kLossBad) ? 2
                      : (s_ConnectionPoor || ping > kPingPoorMs || loss > kLossPoor) ? 1 : 0;

    if (s_Network.feed(network)) {
        const char* level = s_Network.level == 2 ? "bad" : s_Network.level == 1 ? "poor" : "off";
        BeamStatus::notice("network", QString("%1 (ping %2 ms, %3% lost)").arg(level).arg(ping).arg(loss * 100, 0, 'f', 1));
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
        BeamStatus::notice("gamepad-mouse", active ? "on" : "off");
    }
}

}
