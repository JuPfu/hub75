
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
        // Graustufen-Streifen über das gesamte Panel
        for (int y = 0; y < MATRIX_PANEL_HEIGHT; ++y)
        {
            uint32_t grey = (uint8_t)((y * 255) / (MATRIX_PANEL_HEIGHT - 1));
            for (int x = 0; x < MATRIX_PANEL_WIDTH; ++x)
            {
                drawPixel(x, y, (grey << 16) | (grey << 8) | grey);
            }
        }
    }
};