#pragma once

#include "libraries/pico_graphics/pico_graphics.hpp"
#include <cstdint>

using namespace pimoroni;

class Rectangle : public PicoGraphics_PenRGB888
{
public:
    explicit Rectangle(uint width = 64, uint height = 64) : PicoGraphics_PenRGB888(width, height, nullptr)
    {
        set_pen(0);
        clear();
        setIntensity(1.0);
    }

    void draw()
    {
        constexpr int max_width = DISPLAY_WIDTH - 1;
        constexpr int max_height = DISPLAY_HEIGHT - 1;

        // Draw four lines to form a rectangle around the screen. Each line has a unique color and ends one pixel short
        // of the edge of the panel so it doesn't 'interfere' with the next line.
        // (the pixel would be overwritten, but why draw it twice?)

        // Top edge, horizontal: red
        set_pen(0xFF0000);
        line(Point(0, 0), Point(max_width, 0));
        // Right edge, vertical: green
        set_pen(0x00FF00);
        line(Point(max_width, 0), Point(max_width, max_height)); // Why not max_height-1?  Maybe an issue with the graphics libraries line() function?
        // Bottom edge, horizontal: blue
        set_pen(0x0000FF);
        line(Point(max_width + 1, max_height), Point(1, max_height)); // max_width+1 is needed to get the corner pixel lit up?
        // Left edge, vertical: cyan
        set_pen(0x00FFFF);
        line(Point(0, max_height + 1), Point(0, 1)); // max_height+1 is needed to get the corner pixel lit up?

        // Draw a cross inside the rectangle, reaching from corner to corner
        // Top left to bottom right: yellow
        set_pen(0xFFFF00);
        line(Point(1, 1), Point(max_width, max_height));
        // Bottom left to top right: white
        set_pen(0xFFFFFF);
        line(Point(1, max_height - 1), Point(max_width, 1)); // Why not max_width-1?
    }
};
