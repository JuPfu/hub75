// Anti-aliased Line | Xiaolin Wu’s algorithm
// Implementation derived from https://github.com/tobsa/Xiaolin-Wus-Line-Algorithm/blob/master/Xiaolin%20Wu's%20Line%20Algorithm/Source/XiaolinWusLineAlgorithm.cpp
// Integration of the Xiaolin Wu's line algorithm into the Pimoroni Pico Graphics library.

#pragma once

#include "libraries/pico_graphics/pico_graphics.hpp"
#include "libraries/bitmap_fonts/font14_outline_data.hpp"

using namespace pimoroni;

class AntialiasedLine : public PicoGraphics_PenRGB888
{
private:
    uint32_t color_brightness(uint32_t color, float brightness);
    float fPartOfNumber(float x);

public:
    explicit AntialiasedLine() : AntialiasedLine(64, 64) {}

    explicit AntialiasedLine(uint width, uint height) : PicoGraphics_PenRGB888(width, height, nullptr) {}

    void drawLine(float x1, float y1, float x2, float y2, uint32_t color);
};
