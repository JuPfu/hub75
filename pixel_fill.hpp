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

    static const int limit = 1;
    int count = 0;
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
        setIntensity(0.2);
    }

    void fill(int start, int end)
    {
        // static const uint32_t col[] = {0xff0000, 0x00ff00, 0x0000ff, 0xffff00, 0x00ffff,  0xff00ff, 0xE88661, 0xffffff};
        // #1b1a18 #E4E5E7
        // #E8B661
        // #E8A161
        // #FA9E91
        // #E8C661
        // #E8CAAF
        // #FF7398
        // #FF9FF2

        static const uint32_t col[] = {0x0000FF, 0xE8B661, 0xE8A161, 0xFA9E91, 0xE8C661, 0xE8CAAF, 0xFF7398, 0xFF9FF2};
        // static const uint32_t col[] = {0x000000, 0x00000F, 0x0000F0, 0x0000FF, 0x000F00, 0x000F0F, 0x000FFF, 0x00F000,
        //                                0x00F00F, 0x00F0FF, 0x00FFFF, 0x0F0000, 0x0F000F, 0x0F00F0, 0x0F00FF, 0x0F0F00};

        if (true || count > limit)
        {
            count = 0;
            if (j >= w)
            {
                j = 0;
                if (l >= h)
                    l = 0;
                l++;
            }

            c = (l * h + j) % 256;
            // c = 255 - l;
            // c = 194u;
        }

        // printf("j= %d   l = %d  color=%d\n", j, l, c); stdio_flush();
        // drawPixel(j, l, (j % 2 == 0) ? i << 16 : 0x0000ff);

        if ((l * h + j) < ((w * h) >> 1))
        {
            drawPixel(j, l, MIN(255, c % 256u)/*col[(j*32+l)%8]*/);

            // if (l * 32 + j > 192 && l * 32 + j < 192 + 16)
            // {
            //     printf("mid  col = %x\n", c << 16); stdio_flush();
            //     drawPixel(j, l, c << 16 /*col[(j*32+l)%8]*/);
            // }
            // else
            // {
            //     printf("low  col = %x\n", c % 256 ); stdio_flush();
            //     drawPixel(j, l, MIN(254,c % 256) /*col[(j*32+l)%8]*/);
            // }
        }
        else
        {
            if (j < 3)
                printf("j= %d   l = %d  color=%d\n", j, l, ((255 - c) % 256u));
            // printf("green col = %x\n", ((255u - c) % 256u) << 8u); stdio_flush();
            drawPixel(j, l, MIN(((255u - c) % 256u), 255u) << 8u /*col[(j*32+l)%8]*/);
        }
        // count++;
        j++;
        /*if ((j % 2) == 0)*/ i++;
        // i = i % 256;
    }
};