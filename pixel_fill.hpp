#pragma once

#include "libraries/pico_graphics/pico_graphics.hpp"

using namespace pimoroni;

class PixelFill : public PicoGraphics_PenRGB888
{
private:
    int w;
    int h;

    volatile int i = 0;
    volatile int j = 0;

    volatile int index = 0;
    volatile int l = 0;
    volatile uint32_t c = 0;

    void drawPixel(int x, int y, uint32_t color)
    {
        set_pen(color);
        set_pixel(Point(x, y));
    }

public:
    explicit PixelFill(uint width = 32, uint height = 16) : PicoGraphics_PenRGB888(width, height, nullptr), w(width), h(height)
    {
        set_pen(0);
        clear();
        setIntensity(1.0);
    }

    void fill(int start, int end)
    {
        static const uint32_t col[] = {0xFF0000, 0x00FF00, 0x0000FF, 0xa06090, 0xFFFF00, 0xFF00FF, 0xF0F0F0, 0xFFAA55,
                                        0x00F00F, 0x00F0FF, 0x00FFFF, 0x0F0000, 0x0F000F, 0x0F00F0, 0x0F00FF, 0x0F0F00};

        if (j >= w)
        {
            j = 0;
            if (l >= h)
                l = 0;
            l++;
        }

        c = (l * h + j) % 256;

        if ( (l * h + j) < 2 * w ) {
            drawPixel(j, l, col[1]);  // green
        }
        else if ((l * h + j) < 4 * w) {
            drawPixel(j, l, col[3]);   // brown
        }
        else if ((l * h + j) < 6 * w) {
            drawPixel(j, l, col[4]);   // yellow
        }
        else if ((l * h + j) < ((w * h) >> 2))
        {
            drawPixel(j, l, col[6]);  // grey
        }
        else if ((l * h + j) < ((w * h) >> 1))
        {
            drawPixel(j, l, col[2]); // blue
        }
        else if ((l * h + j) < (48 * w)) {
            drawPixel(j, l, col[4]); // yellow
        }
        else {
            drawPixel(j, l, col[0]); // red
        }
        j++;
    }
};