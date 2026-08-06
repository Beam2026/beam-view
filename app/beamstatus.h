#pragma once

#include <QString>

// Line-oriented status output that Beam parses from this process's stdout.
// The format is a contract shared with Beam's engines.rs — keep it in sync
// with docs/beam-integration.md. Lines look like:
//
//   beam: connecting
//   beam: first-frame
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

// Emitted at most once per process, from the first successfully rendered
// video frame. This is Beam's trigger to reveal the embedded window.
void firstFrame();

void error(ErrorCode code, const QString& text);

// reason is "clean" or "error"
void ended(const QString& reason);

}
