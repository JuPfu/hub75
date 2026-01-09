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

        printf("PixelFill w=%d  h=%d\n", w, h);
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

        if ((i) < 32)
        {
            drawPixel(j, l, col[0]); // red
        }
        else if (i < 64)
        {
            drawPixel(j, l, col[1]); // green
        }
        else if (i < 96)
        {
            drawPixel(j, l, col[3]); // brown
        }
        else if (i < 128)
        {
            drawPixel(j, l, col[4]); // yellow
        }
        else if (i < 5 * w)
        {
            drawPixel(j, l, col[6]); // grey
        }
        else if (i < 6 * w)
        {
            drawPixel(j, l, col[2]); // blue
        }
        else if (i < 7 * w)
        {
            drawPixel(j, l, col[0]); // red
        }
        else if (i < 8 * w)
        {
            drawPixel(j, l, col[1]); // green
        }
        else if (i < 9 * w)
        {
            drawPixel(j, l, col[2]); // blue
        }
        else if (i < 10 * w)
        {
            drawPixel(j, l, col[3]); // brown
        }
        else if (i < 11 * w)
        {
            drawPixel(j, l, col[4]); // brown
        }
        else if (i < 12 * w)
        {
            drawPixel(j, l, col[5]); // brown
        }
        else if (i < 13 * w)
        {
            drawPixel(j, l, col[6]); // brown
        }
        else if (i < 14 * w)
        {
            drawPixel(j, l, col[7]); // brown
        }
        else if (i < 15 * w)
        {
            drawPixel(j, l, col[8]); // brown
        }
        else
        {
            drawPixel(j, l, col[9]); // brown
        }
        j++;
        i++;
        if ( i >= 512 ) i = 0;
    }
};