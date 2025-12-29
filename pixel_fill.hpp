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
        if (j >= w)
        {
            j = 0;
            if (l >= h)
                l = 0;
            l++;
        }

        c = (l * h + j) % 256;

        if ((l * h + j) < ((w * h) >> 1))
        {
            drawPixel(j, l, MIN(255, c % 256u) /*col[(j*32+l)%8]*/);
        }
        else
        {
            drawPixel(j, l, MIN(((255u - c) % 256u), 255u) << 8u /*col[(j*32+l)%8]*/);
        }
        j++;
    }
};