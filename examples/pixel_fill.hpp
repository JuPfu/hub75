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
    volatile int l = 0;

    volatile int index = 0;

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
        static const uint32_t col[] = {0xFF0000, 0x00FF00, 0x0000FF, 0xBE2633, 0xE06F8B, 0x493C2B, 0xA46422, 0xEB8931,
                                       0xF7E26B, 0x2F484E, 0x44891A, 0xA3CE27, 0x1B2632, 0x005784, 0x31A2F2, 0xB2DCEF};

        drawPixel(j++, l, col[index]);

        if (j >= w)
        {
            j = 0;
            l++;
            if (l >= h)
                l = 0;
            if ((l % 2) == 0)
                index++;
            if (index >= 16)
                index = 0;
        }
    }
};