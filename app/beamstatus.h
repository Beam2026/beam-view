#pragma once

#include <QString>

// Line-oriented status output that Beam parses from this process's stdout.
// The format is a contract shared with Beam's engines.rs — keep it in sync
// with docs/beam-integration.md. Lines look like:
//
//   beam: connecting
//   beam: first-frame
//   beam: warning <text>
//   beam: state <what> <value>
//   beam: notice <kind> <value>
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

// What the stream is doing now, after a call or a key combination changed it (P16):
// `beam: state <what> <value>`, such as `state sound off`.
void state(const QString& what, const QString& value);

// A status icon over the stream appearing or going, and why (P19): `beam: notice <kind> <value>`,
// such as `notice network poor (ping 112 ms, 0.4% lost)`.
void notice(const QString& kind, const QString& value);

// This install's client certificate, base64 of its PEM: what a host trusts for a session instead of
// pairing (P12). Public -- the private key never leaves this machine.
void identity(const QByteArray& pem);

void error(ErrorCode code, const QString& text);

// Started without a command (P15): there is no front end to fall back to, so this is a launch
// failure. The caller exits 1 straight after.
void noCommand();

// reason is "clean" or "error"
void ended(const QString& reason);

}
