#include "beam/control.h"

#include "beamstatus.h"
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
    CallInput,
    CallDisconnect,
};

std::atomic<bool> s_SoundOff{false};

// The capture last reported, so a repeated setCaptureActive with no change says nothing. Starts
// unknown (-1): the first report always goes out.
std::atomic<int> s_ReportedCapture{-1};

// A call is one short line; anything longer is not one, and is read past rather than split.
constexpr int kMaxLine = 128;

// Words for the parts of a call, so the reader and the reports say them the same way.
const char* onOff(bool on) { return on ? "on" : "off"; }

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
    else if (what == "input" && choice(value, "capture", "release", on)) {
        post(CallInput, on);
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
    case CallStats:
        session->getOverlayManager().setOverlayState(Overlay::OverlayDebug, on);
        reportStats(session);
        break;

    case CallWindow:
        // Upstream's own toggle, as Ctrl+Alt+Shift+X does it: on Windows it rebuilds the decoder,
        // so the picture blinks once.
        if (isFullScreen(session) != on) {
            session->toggleFullscreen();
            session->m_InputHandler->raiseAllKeys();
        }
        reportWindow(session);
        break;

    case CallSound:
        s_SoundOff = !on;
        BeamStatus::state("sound", onOff(on));
        break;

    case CallInput:
        // Ctrl+Alt+Shift+Z's own steps: the handler grabs or lets go of both mouse and keyboard, and
        // on letting go raises every key so none stays down on the host. It reports the change.
        if (session->m_InputHandler->isCaptureActive() != on) {
            session->m_InputHandler->setCaptureActive(on);
            if (!on) {
                session->m_InputHandler->raiseAllKeys();
            }
        }
        else {
            // Already so: nothing changed to report, but every call is answered.
            BeamStatus::state("input", on ? "capture" : "release");
        }
        break;

    case CallDisconnect: {
        // The quit key combination's own path, so Beam hears `beam: ending` as it does for that.
        SDL_Event quit = {};
        quit.type = SDL_QUIT;
        quit.quit.timestamp = SDL_GetTicks();
        SDL_PushEvent(&quit);
        break;
    }
    }
}

bool BeamSessionControl::soundOff()
{
    return s_SoundOff;
}

void BeamSessionControl::reportStats(Session* session)
{
    BeamStatus::state("stats", onOff(session->getOverlayManager().isOverlayEnabled(Overlay::OverlayDebug)));
}

void BeamSessionControl::reportInput(bool captured)
{
    if (s_ReportedCapture.exchange(captured ? 1 : 0) != (captured ? 1 : 0)) {
        BeamStatus::state("input", captured ? "capture" : "release");
    }
}

void BeamSessionControl::reportWindow(Session* session)
{
    BeamStatus::state("window", isFullScreen(session) ? "full" : "windowed");
}
