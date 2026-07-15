
#pragma once

#include "pico_graphics.hpp"

using namespace pimoroni;

template <uint32_t W, uint32_t H>
class GreyScaleStripes : public PicoGraphics_PenRGB888
{

private:
    alignas(4) uint8_t pixel_buf_[W * H * sizeof(uint32_t)];

    void drawPixel(int x, int y, uint32_t color)
    {
        set_pen(color);
        set_pixel(Point(x, y));
    }

public:
    explicit GreyScaleStripes() : PicoGraphics_PenRGB888(W, H, pixel_buf_)
    {
        set_pen(0);
        clear();
    }

    void drawStripes()
    {
        // grey stripes in different shades all over the panel
        for (int y = 0; y < (int)H; ++y)
        {
            uint32_t grey = (uint8_t)((y * 255) / (H - 1));
            for (int x = 0; x < (int)W; ++x)
            {
                drawPixel(x, y, (grey << 16) | (grey << 8) | grey);
            }
        }
    }
};