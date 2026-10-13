#pragma once

struct _VIDEO_STATS;
struct SDL_Window;

// Beam (P19): the status icons over the stream -- top left, icons only, each on an ivory tile at 80%
// opacity, each shown only while its problem lasts:
//
//   network        the connection is poor: frames lost, or a ping over 80 ms     (signal-1)
//   decode         this PC cannot decode a frame in a frame's time   (gpu-decode)
//   encode         their PC cannot encode a frame in a frame's time  (gpu-encode)
//   gamepad-mouse  a controller has just started driving the mouse   (gamepad, for 5 s)
//                  or just stopped                                   (the same, on a red tile, 5 s)
//
// The three warnings pulse slowly; the controller, a mode rather than a problem, stays steady. A
// warning appears after three bad seconds and goes after three good ones, so one bad moment does
// not make it flicker. Every change is said on stdout as `beam: notice <kind> <value>`.
//
// The pictures are app/res/hud/*.svg, pixel art on a 24-pixel grid, drawn 1.5 times their size on a
// 1080-line screen with every edge on a whole pixel, so nothing blurs; the tiles' corners are smooth. Moonlight's own status text no longer appears.
//
// Thread-safe: the stats arrive on the decoder's thread, the connection status on the streaming
// library's, the controller on the session's, and the picture is redrawn on a timer.
namespace BeamHud
{

// As the stream's event loop starts: the height of the screen the window is on sets the scale. With
// connection warnings off, only the controller icon is shown.
void begin(SDL_Window* window, bool warnings);

// As the stream's event loop ends, before the session goes: no redraw can reach it afterwards.
void end();

// The streaming library's verdict on the connection.
void connectionStatus(bool poor);

// One second of the decoder's numbers, with the ping filled in.
void videoWindow(const _VIDEO_STATS& window, int streamFps);

// Gamepad mouse mode, on or off.
void gamepadMouse(bool active);

// Every icon at once, whatever the stream is doing -- to see them (the `hud demo` call).
void demo(bool on);

// The last second's ping, and a frame's encode and decode time in milliseconds; negative while not
// known yet. For the list over the stream (P20).
void latest(int& pingMs, double& encodeMs, double& decodeMs);

}
