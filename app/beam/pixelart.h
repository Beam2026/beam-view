#pragma once

#include "SDL_compat.h"

#include <vector>

// Beam's pixel art -- the status icons and Beam's mark -- is SVG made only of filled rectangles on a
// grid: `<path fill="#rrggbb" d="Mx yhWvHh-Wz…">`. Read as those rectangles, it can be drawn at any
// whole multiple of its grid, so no pixel is ever blurred.
namespace PixelArt
{

// One filled rectangle, in grid cells, and its colour.
struct Cell {
    int x, y, w, h;
    Uint8 r, g, b;
};

// The rectangles in a resource such as ":/res/hud/beam-hud-gamepad.svg"; empty if it cannot be read.
std::vector<Cell> load(const char* path);

}
