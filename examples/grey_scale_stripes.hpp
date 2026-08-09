
#pragma once

#include "pico_graphics.hpp"

using namespace pimoroni;

template <uint32_t W, uint32_t H>
class GreyScaleStripes : public PicoGraphics_PenRGB888
{

private:
    void drawPixel(int x, int y, uint32_t color)
    {
        set_pen(color);
        set_pixel(Point(x, y));
    }

public:
    explicit GreyScaleStripes() : PicoGraphics_PenRGB888(W, H, nullptr)
    {
        set_pen(0);
        clear();
    }

    void drawStripes()
    {
        // grey stripes in different shades all over the panel
        for (uint32_t y = 0; y < H; ++y)
        {
            uint32_t grey = (uint8_t)((y * 255) / (H - 1));
            for (uint32_t x = 0; x < W; ++x)
            {
                drawPixel(x, y, (grey << 16) | (grey << 8) | grey);
            }
        }
    }
};