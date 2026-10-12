#pragma once

#include "SDL_compat.h"

class Session;

// Beam (P16): calls on stdin while a stream runs, one per line, and the state they leave behind.
// The contract is docs/beam-integration.md; Beam's side is viewer.rs.
//
//   stats on|off            the performance overlay
//   window full|windowed    full screen (Beam's borderless) or a window
//   sound on|off            this PC playing the stream's audio
//   mouse lock|free         the cursor kept inside the stream window, or free to leave it
//   keyboard lock|free      Windows, Alt+Tab and Alt+F4 sent to the host, or kept on this PC
//   disconnect              end the stream, exactly as Ctrl+Alt+Shift+Q does
//
// Every change, whoever made it -- a call, or a key combination -- is said as `beam: state <what>
// <value>`, which is how Beam learns what the person changed. An unknown call is a `beam: warning`
// and changes nothing.
//
// Calls are read on a thread of their own and applied on the session's SDL thread: nothing here
// touches SDL's window, the renderer or the session from the reader.

// Above the user-event codes in decoder.h (0, and Beam's 200 and 201) and session.cpp (100-105).
#define SDL_CODE_BEAM_CALL 202

class BeamSessionControl
{
public:
    // Starts reading calls, once per process. A closed or empty stdin -- a run by hand -- ends the
    // reader at once and changes nothing.
    static void listen();

    // A call the reader posted, applied on the session's thread.
    static void apply(Session* session, const SDL_UserEvent& event);

    // Whether the person turned the stream's sound off. Separate from the session's own mute, which
    // follows focus when "mute when not in front" is on and would otherwise undo this.
    static bool soundOff();

    // Said after a key combination changed one of these, so Beam hears it as it would a call.
    static void reportStats(Session* session);
    static void reportWindow(Session* session);

    static void reportMouse(Session* session);

private:
    static bool isFullScreen(Session* session);
};
