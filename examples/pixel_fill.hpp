#pragma once

#include "pico_graphics.hpp"

using namespace pimoroni;

template <uint32_t W, uint32_t H>
class PixelFill : public PicoGraphics_PenRGB888
{
private:

    int i = 0;
    int j = 0;
    int l = 0;

    int index = 0;

    void drawPixel(int x, int y, uint32_t color)
    {
        set_pen(color);
        set_pixel(Point(x, y));
    }

public:
    explicit PixelFill() : PicoGraphics_PenRGB888(W, H, nullptr)
    {
        set_pen(0);
        clear();
    }

    void fill()
    {
        static const uint32_t col[] = {0xFF0000, 0x00FF00, 0x0000FF, 0xBE2633, 0xE06F8B, 0x493C2B, 0xA46422, 0xEB8931,
                                       0xF7E26B, 0x2F484E, 0x44891A, 0xA3CE27, 0x1B2632, 0x005784, 0x31A2F2, 0xB2DCEF};

        drawPixel(j++, l, col[index]);

        if (j >= (int)W)
        {
            j = 0;
            l++;
            if (l >= (int)H)
                l = 0;
            if ((l % 2) == 0)
                index++;
            if (index >= 16)
                index = 0;
        }
    }
};