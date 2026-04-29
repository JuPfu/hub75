
#pragma once

#include "libraries/pico_graphics/pico_graphics.hpp"

using namespace pimoroni;

class GreyScaleStripes : public PicoGraphics_PenRGB888
{

private:
    int w;
    int h;

    void drawPixel(int x, int y, uint32_t color)
    {
        set_pen(color);
        set_pixel(Point(x, y));
    }

public:
    explicit GreyScaleStripes(uint width = 64, uint height = 64) : PicoGraphics_PenRGB888(width, height, nullptr), w(width), h(height)
    {
        set_pen(0);
        clear();
        setIntensity(1.0);
    }

    void drawStripes()
    {
        // grey stripes in different shades all over the panel
        for (int y = 0; y < h; ++y)
        {
            uint32_t grey = (uint8_t)((y * 255) / (h - 1));
            for (int x = 0; x < w; ++x)
            {
                drawPixel(x, y, (grey << 16) | (grey << 8) | grey);
            }
        }
    }
};