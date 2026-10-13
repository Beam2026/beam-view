#pragma once

#include "SDL_compat.h"

class Session;

// Beam (P20): the Beam button over the stream, and the list it opens.
//
// The button is Beam's mark on a solid ivory tile, like the status icons'. It sits on the top, left or
// right edge of the stream -- top centre at first -- and is dragged along them, snapping to the
// nearest when let go. It shows whenever the cursor does (desktop mouse mode, or input released);
// in game mouse mode, and after "Hide button", Ctrl+Alt+Shift+B opens the list and brings it back.
//
// The list is drawn like Beam's Help list: the last second's ping, encode and decode times, then
// Hide button, Window, Sound, Codec, Decoder, V-Sync, Mute when not in front, Mouse mode, Input
// locks and Disconnect. Codec, Decoder, V-Sync and Input locks open a smaller list beside it.
// Every change lasts for this session only, and is said as a `beam: state` line, as calls are.
//
// While the list is open, the mouse and keyboard are this PC's: input to the host is released, and
// given back as it was when the list closes (a click outside, Escape, or the hotkey again).
//
// Everything here runs on the session's thread, inside its event loop.
namespace BeamMenu
{

// Above the user-event codes in decoder.h, session.cpp and beam/control.h.
constexpr int kRedrawCode = 203;

// As the stream's event loop starts and ends.
void begin(Session* session);
void end();

// Every event the loop takes, before anything else sees it: true when it was the button's or the
// list's, and must go no further -- not to the host, not to upstream's handlers.
bool handleEvent(Session* session, const SDL_Event& event);

}
