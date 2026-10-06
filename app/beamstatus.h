#pragma once

#include <QString>

// Line-oriented status output that Beam parses from this process's stdout.
// The format is a contract shared with Beam's engines.rs — keep it in sync
// with docs/beam-integration.md. Lines look like:
//
//   beam: connecting
//   beam: first-frame
//   beam: warning <text>
//   beam: error <code> <text>
//   beam: ended <reason>
//
namespace BeamStatus
{

// Stable error codes for the "beam: error" line. The text after the code is
// human-readable and free to change; the codes are not.
enum ErrorCode {
    ErrorLaunchFailed = 1,   // host not found, app not found, launch rejected
    ErrorStageFailed = 2,    // a connection stage failed before streaming began
    ErrorSession = 3,        // the connection failed or terminated abnormally
    ErrorPairingFailed = 4,  // the pair command failed
    ErrorQuitFailed = 5,     // the quit command failed
};

void connecting();

// "first-frame" is emitted at most once per process, when the first video
// frame is *on screen* -- Beam's trigger to hide its own window, so it must
// not fire while this program's window is still invisible.
//
// The pacer calls frameRendered() for every rendered frame. Normally the first
// one emits the line. If the session has cloaked its window
// (holdFirstFrameUntilRevealed), the reveal takes two steps:
//
//   Reveal  first frame: the session uncloaks the window at the *bottom* of the
//           z-order, where DWM composes it out of sight, and calls revealed()
//   Raise   after kComposeTime and kComposeFrames: the session raises it, with
//           real frames already in it, and calls raised() -- which emits the line
enum class FrameAction { None, Reveal, Raise };

void holdFirstFrameUntilRevealed();
FrameAction frameRendered();
void revealed();
void raised();

// Emitted at most once, the moment the user asks to end the stream, before the
// seconds of polite shutdown that follow. Beam ends the session on it rather
// than waiting for this process to exit.
void ending();

// A setting this session could not honour, and what it does instead -- a codec the host cannot
// encode, surround the audio device cannot play, HDR the GPU cannot decode. Headless, these were
// queued for a dialog that never shows, so the fallback happened with no word to anyone (P11).
void warning(const QString& text);

void error(ErrorCode code, const QString& text);

// reason is "clean" or "error"
void ended(const QString& reason);

}
