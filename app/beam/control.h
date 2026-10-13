#pragma once

#include "SDL_compat.h"
#include "settings/streamingpreferences.h"

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
//   hud demo on|off         every status icon at once, to see them (P19)
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

    // The actions, on the session's thread. A call and the list over the stream (P20) both come
    // here, and each says the state it leaves. All of them last for this session only: nothing is
    // saved, so the next stream starts from Beam's settings again.
    static void setStats(Session* session, bool on);
    static void setFullScreen(Session* session, bool on);
    static void setSound(bool on);
    static void setMouseLock(Session* session, bool locked);
    static void setKeyboardLock(Session* session, bool locked);
    static void setMuteInBackground(Session* session, bool on);
    static void setMouseMode(Session* session, bool desktop);
    static void setDecoder(Session* session, StreamingPreferences::VideoDecoderSelection decoder);
    static void setVsync(Session* session, bool vsync, bool pacing);
    static void disconnect();

    // For the list: the session's input, its window, and what each setting is now.
    static void setCapture(Session* session, bool active);
    static bool captureActive(Session* session);
    static void raiseKeys(Session* session);
    static SDL_Window* window(Session* session);
    static bool isFullScreen(Session* session);
    static bool mouseLocked(Session* session);
    static bool keyboardLocked(Session* session);
    static bool muteInBackground(Session* session);
    static bool mouseModeDesktop(Session* session);
    static StreamingPreferences::VideoDecoderSelection decoder(Session* session);
    static bool vsync(Session* session);
    static bool framePacing(Session* session);
    static int videoFormat(Session* session);

    // Said after a call or a key combination changed one of these, so Beam hears both alike.
    static void reportStats(Session* session);
    static void reportWindow(Session* session);
    static void reportMouse(Session* session);

    // Upstream's own toggles, which no call makes, said so the log has every change:
    //   capture on|off         input sent to the host, or not (Ctrl+Alt+Shift+Z, a click back into
    //                          the stream, focus going elsewhere) -- once per actual change
    //   mouse-mode game|desktop  relative or absolute mouse (Ctrl+Alt+Shift+M)
    //   cursor show|hide       the local cursor in desktop mouse mode (Ctrl+Alt+Shift+C)
    //   window minimized       the stream window minimised (Ctrl+Alt+Shift+D)
    //   gamepad-mouse on|off   a controller driving the mouse, toggled by a long press of Start (P17)
    static void reportCapture(bool active);
    static void reportMouseMode(bool absolute);
    static void reportCursor(bool shown);
    static void reportMinimized();
    static void reportGamepadMouse(bool active);

};
