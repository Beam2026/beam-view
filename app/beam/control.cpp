#include "beam/control.h"

#include "beamstatus.h"
#include "beam/hud.h"
#include "streaming/session.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>

#include <QString>

namespace {

enum Call : intptr_t {
    CallStats,
    CallWindow,
    CallSound,
    CallMouse,
    CallKeyboard,
    CallDisconnect,
    CallHudDemo,
};

std::atomic<bool> s_SoundOff{false};

// The capture last said, so setCaptureActive repeating itself says nothing. Starts unknown (-1).
std::atomic<int> s_ReportedCapture{-1};

// A call is one short line; anything longer is not one, and is read past rather than split.
constexpr int kMaxLine = 128;

// Words for the parts of a call, so the reader and the reports say them the same way.
const char* onOff(bool on) { return on ? "on" : "off"; }
const char* lockFree(bool locked) { return locked ? "lock" : "free"; }

void post(Call call, bool value)
{
    SDL_Event event = {};
    event.type = SDL_USEREVENT;
    event.user.code = SDL_CODE_BEAM_CALL;
    event.user.data1 = reinterpret_cast<void*>(call);
    event.user.data2 = reinterpret_cast<void*>(static_cast<intptr_t>(value));
    if (SDL_PushEvent(&event) < 0) {
        BeamStatus::warning(QString("call not applied: %1").arg(SDL_GetError()));
    }
}

// `<what> <value>` with value one of two words; true when it parsed.
bool choice(const QString& value, const char* yes, const char* no, bool& out)
{
    if (value == yes) { out = true; return true; }
    if (value == no) { out = false; return true; }
    return false;
}

void parse(const QString& line)
{
    const QStringList words = line.split(' ', Qt::SkipEmptyParts);
    if (words.isEmpty()) {
        return;
    }

    const QString& what = words[0];
    const QString value = words.size() == 2 ? words[1] : QString();
    bool on = false;

    if (what == "stats" && choice(value, "on", "off", on)) {
        post(CallStats, on);
    }
    else if (what == "window" && choice(value, "full", "windowed", on)) {
        post(CallWindow, on);
    }
    else if (what == "sound" && choice(value, "on", "off", on)) {
        post(CallSound, on);
    }
    else if (what == "mouse" && choice(value, "lock", "free", on)) {
        post(CallMouse, on);
    }
    else if (what == "keyboard" && choice(value, "lock", "free", on)) {
        post(CallKeyboard, on);
    }
    else if (what == "hud" && words.size() == 3 && words[1] == "demo" && choice(words[2], "on", "off", on)) {
        post(CallHudDemo, on);
    }
    else if (what == "disconnect" && words.size() == 1) {
        post(CallDisconnect, true);
    }
    else {
        BeamStatus::warning(QString("unknown call: %1").arg(line));
    }
}

void readCalls()
{
    char buffer[kMaxLine];
    bool skipping = false;
    while (fgets(buffer, sizeof(buffer), stdin) != nullptr) {
        const size_t length = strlen(buffer);
        const bool endsLine = length > 0 && buffer[length - 1] == '\n';
        // A chunk that filled the buffer without ending its line belongs to a line too long to be a
        // call: skip to that line's end, then carry on. (A last line with no newline is short.)
        const bool filled = !endsLine && length == sizeof(buffer) - 1;
        if (skipping) {
            if (endsLine) {
                skipping = false;
                BeamStatus::warning("a line longer than any call was ignored");
            }
            continue;
        }
        if (filled) {
            skipping = true;
            continue;
        }
        parse(QString::fromUtf8(buffer, (int)length).trimmed());
    }
}

} // namespace

bool BeamSessionControl::isFullScreen(Session* session)
{
    return (SDL_GetWindowFlags(session->m_Window) & session->m_FullScreenFlag) != 0;
}

void BeamSessionControl::listen()
{
    static std::atomic<bool> listening{false};
    if (listening.exchange(true)) {
        return;
    }
    // Detached: it spends its life blocked in a read, and the process ending is what ends it.
    std::thread(readCalls).detach();
}

void BeamSessionControl::apply(Session* session, const SDL_UserEvent& event)
{
    const bool on = reinterpret_cast<intptr_t>(event.data2) != 0;

    switch (static_cast<Call>(reinterpret_cast<intptr_t>(event.data1))) {
    case CallStats:      setStats(session, on); break;
    case CallWindow:     setFullScreen(session, on); break;
    case CallSound:      setSound(on); break;
    case CallMouse:      setMouseLock(session, on); break;
    case CallKeyboard:   setKeyboardLock(session, on); break;
    case CallDisconnect: disconnect(); break;
    case CallHudDemo:
        BeamHud::demo(on);
        BeamStatus::state("hud-demo", onOff(on));
        break;
    }
}

void BeamSessionControl::setStats(Session* session, bool on)
{
    session->getOverlayManager().setOverlayState(Overlay::OverlayDebug, on);
    reportStats(session);
}

void BeamSessionControl::setFullScreen(Session* session, bool on)
{
    // Upstream's own toggle, as Ctrl+Alt+Shift+X does it: on Windows it rebuilds the decoder, so the
    // picture blinks once.
    if (isFullScreen(session) != on) {
        session->toggleFullscreen();
        session->m_InputHandler->raiseAllKeys();
    }
    reportWindow(session);
}

void BeamSessionControl::setSound(bool on)
{
    s_SoundOff = !on;
    BeamStatus::state("sound", onOff(on));
}

void BeamSessionControl::setMouseLock(Session* session, bool locked)
{
    // Upstream's pointer region lock, as its key combination sets it: the cursor cannot leave the
    // stream window. Marked as the person's choice, so going full screen or windowed no longer
    // changes it by itself.
    SdlInputHandler* input = session->m_InputHandler;
    input->m_PointerRegionLockActive = locked;
    input->m_PointerRegionLockToggledByUser = true;
    input->updatePointerRegionLock();
    reportMouse(session);
}

void BeamSessionControl::setKeyboardLock(Session* session, bool locked)
{
    // The keyboard grab: locked, Windows, Alt+Tab and Alt+F4 go to the host; free, they stay on this
    // PC while every other key still reaches the stream. Beam starts every stream locked
    // (`--capture-system-keys always`). Keys held now are raised, so none sticks on either side.
    SdlInputHandler* input = session->m_InputHandler;
    input->m_CaptureSystemKeysMode = locked ? StreamingPreferences::CSK_ALWAYS : StreamingPreferences::CSK_OFF;
    input->raiseAllKeys();
    input->updateKeyboardGrabState();
    BeamStatus::state("keyboard", lockFree(locked));
}

void BeamSessionControl::setMuteInBackground(Session* session, bool on)
{
    // "Mute when the stream isn't in front", for this session. Turned off while muted by it, the
    // sound comes back now rather than at the next focus change.
    session->m_Preferences->muteOnFocusLoss = on;
    if (!on) {
        session->m_AudioMuted = false;
    }
    BeamStatus::state("mute-in-background", onOff(on));
}

void BeamSessionControl::setMouseMode(Session* session, bool desktop)
{
    // Ctrl+Alt+Shift+M's steps, but only recapturing input that was captured: with the list over
    // the stream open, it stays released.
    SdlInputHandler* input = session->m_InputHandler;
    const bool captured = input->isCaptureActive();
    if (captured) {
        input->setCaptureActive(false);
    }
    input->m_AbsoluteMouseMode = desktop;
    if (captured) {
        input->setCaptureActive(true);
    }
    reportMouseMode(desktop);
}

// The decoder is rebuilt from the session's settings whenever the render device resets -- upstream's
// own path for a lost GPU -- so changing the setting and asking for that applies it, with one blink.
static void rebuildDecoder()
{
    SDL_Event reset = {};
    reset.type = SDL_RENDER_DEVICE_RESET;
    SDL_PushEvent(&reset);
}

void BeamSessionControl::setDecoder(Session* session, StreamingPreferences::VideoDecoderSelection decoder)
{
    session->m_Preferences->videoDecoderSelection = decoder;
    rebuildDecoder();
    BeamStatus::state("decoder", decoder == StreamingPreferences::VDS_FORCE_HARDWARE ? "hardware"
                               : decoder == StreamingPreferences::VDS_FORCE_SOFTWARE ? "software" : "auto");
}

void BeamSessionControl::setVsync(Session* session, bool vsync, bool pacing)
{
    session->m_Preferences->enableVsync = vsync;
    session->m_Preferences->framePacing = vsync && pacing;
    rebuildDecoder();
    BeamStatus::state("vsync", !vsync ? "off" : pacing ? "pacing" : "on");
}

void BeamSessionControl::showCursor(Session* session)
{
    // This PC's cursor, visible in desktop mouse mode -- as Ctrl+Alt+Shift+C shows it, and that still
    // hides it. Upstream hides it and leaves the host's cursor in the picture to point with, but a
    // Beam session starts view-only: the host's cursor does not follow until the host gives the mouse,
    // and with neither shown the guest had nothing to reach the Beam button with. Applied as input is
    // next captured; game mouse mode hides the cursor whatever this says.
    session->m_InputHandler->m_MouseCursorCapturedVisibilityState = true;
    if (!SDL_GetRelativeMouseMode()) {
        SDL_ShowCursor(SDL_ENABLE);
    }
    reportCursor(true);
}

void BeamSessionControl::disconnect()
{
    // The quit key combination's own path, so Beam hears `beam: ending` as it does for that.
    SDL_Event quit = {};
    quit.type = SDL_QUIT;
    quit.quit.timestamp = SDL_GetTicks();
    SDL_PushEvent(&quit);
}

void BeamSessionControl::setCapture(Session* session, bool active) { session->m_InputHandler->setCaptureActive(active); }
bool BeamSessionControl::captureActive(Session* session) { return session->m_InputHandler->isCaptureActive(); }
void BeamSessionControl::raiseKeys(Session* session) { session->m_InputHandler->raiseAllKeys(); }
SDL_Window* BeamSessionControl::window(Session* session) { return session->m_Window; }
bool BeamSessionControl::mouseLocked(Session* session) { return session->m_InputHandler->m_PointerRegionLockActive; }
bool BeamSessionControl::keyboardLocked(Session* session) { return session->m_InputHandler->m_CaptureSystemKeysMode != StreamingPreferences::CSK_OFF; }
bool BeamSessionControl::muteInBackground(Session* session) { return session->m_Preferences->muteOnFocusLoss; }
bool BeamSessionControl::mouseModeDesktop(Session* session) { return session->m_InputHandler->m_AbsoluteMouseMode; }
StreamingPreferences::VideoDecoderSelection BeamSessionControl::decoder(Session* session) { return session->m_Preferences->videoDecoderSelection; }
bool BeamSessionControl::vsync(Session* session) { return session->m_Preferences->enableVsync; }
bool BeamSessionControl::framePacing(Session* session) { return session->m_Preferences->framePacing; }
int BeamSessionControl::videoFormat(Session* session) { return session->m_ActiveVideoFormat; }

bool BeamSessionControl::soundOff()
{
    return s_SoundOff;
}

void BeamSessionControl::reportStats(Session* session)
{
    BeamStatus::state("stats", onOff(session->getOverlayManager().isOverlayEnabled(Overlay::OverlayDebug)));
}

void BeamSessionControl::reportMouse(Session* session)
{
    BeamStatus::state("mouse", lockFree(session->m_InputHandler->m_PointerRegionLockActive));
}

void BeamSessionControl::reportCapture(bool active)
{
    const int now = active ? 1 : 0;
    if (s_ReportedCapture.exchange(now) != now) {
        BeamStatus::state("capture", onOff(active));
    }
}

void BeamSessionControl::reportMouseMode(bool absolute)
{
    BeamStatus::state("mouse-mode", absolute ? "desktop" : "game");
}

void BeamSessionControl::reportCursor(bool shown)
{
    BeamStatus::state("cursor", shown ? "show" : "hide");
}

void BeamSessionControl::reportMinimized()
{
    BeamStatus::state("window", "minimized");
}

void BeamSessionControl::reportGamepadMouse(bool active)
{
    BeamStatus::state("gamepad-mouse", onOff(active));
}

void BeamSessionControl::reportWindow(Session* session)
{
    BeamStatus::state("window", isFullScreen(session) ? "full" : "windowed");
}
