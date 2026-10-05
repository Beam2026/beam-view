#include "beamstatus.h"

#include <atomic>
#include <chrono>
#include <cstdio>

static void emitLine(const QString& line)
{
    // Beam's reader is line-oriented, so a status line must stay one line
    QString sanitized = line;
    sanitized.replace('\r', ' ');
    sanitized.replace('\n', ' ');

    QByteArray utf8 = sanitized.toUtf8();
    fprintf(stdout, "beam: %s\n", utf8.constData());

    // stdout is a fully buffered pipe when Beam is the parent; a status line
    // sitting in the buffer is as good as never sent
    fflush(stdout);
}

namespace BeamStatus {

// Where the first frame is on its way to the screen. Only ever moves forward.
enum FirstFrameState {
    Waiting,     // no frame rendered yet
    Revealing,   // a frame rendered while cloaked; the session is uncloaking
    Composing,   // uncloaked at the bottom of the z-order, composing out of sight
    Raising,     // composed long enough; the session is raising the window
    Announced,   // "first-frame" has been sent
};

// Measured on 2026-10-05: 20 ms between uncloaking and Beam dropping its
// loading screen still showed a black flash, because a newly uncloaked window
// presents black until DWM has composed real frames into it. Before the cloak
// existed the window had ~260 ms behind Beam's cover and never flashed.
static constexpr auto kComposeTime = std::chrono::milliseconds(150);
static constexpr int kComposeFrames = 2;

static std::atomic_int s_State(Waiting);
static std::atomic_bool s_HoldUntilRevealed(false);
static std::atomic_int s_FramesSinceReveal(0);
static std::atomic<std::chrono::steady_clock::rep> s_RevealedAt(0);

static std::chrono::steady_clock::rep now()
{
    return std::chrono::steady_clock::now().time_since_epoch().count();
}

void connecting()
{
    emitLine("connecting");
}

void holdFirstFrameUntilRevealed()
{
    s_HoldUntilRevealed = true;
}

FrameAction frameRendered()
{
    // Called for every rendered frame, from the render thread
    int state = s_State.load();
    switch (state) {
    case Waiting:
        if (s_HoldUntilRevealed) {
            // The window is cloaked: this frame is in it, but not on screen.
            return s_State.compare_exchange_strong(state, Revealing) ? FrameAction::Reveal
                                                                     : FrameAction::None;
        }
        if (s_State.compare_exchange_strong(state, Announced)) {
            emitLine("first-frame");
        }
        return FrameAction::None;

    case Composing: {
        int frames = ++s_FramesSinceReveal;
        auto elapsed = std::chrono::steady_clock::duration(now() - s_RevealedAt.load());
        if (frames >= kComposeFrames && elapsed >= kComposeTime &&
                s_State.compare_exchange_strong(state, Raising)) {
            return FrameAction::Raise;
        }
        return FrameAction::None;
    }

    default:
        return FrameAction::None;
    }
}

void revealed()
{
    s_FramesSinceReveal = 0;
    s_RevealedAt = now();
    int state = Revealing;
    s_State.compare_exchange_strong(state, Composing);
}

void raised()
{
    // From Raising normally, or from Composing when the session's fallback
    // timer raised the window because frames stopped arriving.
    int state = s_State.load();
    while (state == Raising || state == Composing) {
        if (s_State.compare_exchange_weak(state, Announced)) {
            emitLine("first-frame");
            return;
        }
    }
}

void error(ErrorCode code, const QString& text)
{
    emitLine(QString("error %1 %2").arg(code).arg(text));
}

void ended(const QString& reason)
{
    emitLine(QString("ended %1").arg(reason));
}

}
