// Anti-aliased Line | Xiaolin Wu’s algorithm
// Implementation derived from https://github.com/tobsa/Xiaolin-Wus-Line-Algorithm/blob/master/Xiaolin%20Wu's%20Line%20Algorithm/Source/XiaolinWusLineAlgorithm.cpp
// Integration of the Xiaolin Wu's line algorithm into the Pimoroni Pico Graphics library.

#pragma once

#include <algorithm> // for std::swap

#include "pico_graphics.hpp"
#include "font14_outline_data.hpp"

using namespace pimoroni;

template <uint32_t W, uint32_t H>
class AntialiasedLine : public PicoGraphics_PenRGB888
{
private:
    alignas(4) uint8_t pixel_buf_[W * H * sizeof(uint32_t)];

    uint32_t color_brightness(uint32_t color, float brightness);
    float fPartOfNumber(float x);

public:
    explicit AntialiasedLine() : PicoGraphics_PenRGB888(W, H, pixel_buf_) {}

    void drawLine(float x1, float y1, float x2, float y2, uint32_t color);
};

// Avoid redundant function calls by inlining.
template <uint32_t W, uint32_t H>
inline float AntialiasedLine<W, H>::fPartOfNumber(float x)
{
    return x - std::floor(x);
}

// Optimized color brightness function with minimal floating-point operations
template <uint32_t W, uint32_t H>
uint32_t AntialiasedLine<W, H>::color_brightness(uint32_t color, float brightness)
{
    brightness = std::max(0.0f, std::min(1.0f, brightness));
    uint32_t r = ((color >> 16) & 0xFF) * brightness + 0.5f;
    uint32_t g = ((color >> 8) & 0xFF) * brightness + 0.5f;
    uint32_t b = (color & 0xFF) * brightness + 0.5f;
    return (r << 16) | (g << 8) | b;
}

template <uint32_t W, uint32_t H>
void AntialiasedLine<W, H>::drawLine(float x1, float y1, float x2, float y2, uint32_t color)
{
    float dx = x2 - x1, dy = y2 - y1;

    bool steep = std::abs(dy) > std::abs(dx);
    if (steep)
    {
        std::swap(x1, y1);
        std::swap(x2, y2);
        std::swap(dx, dy);
    }

    if (x2 < x1)
    {
        std::swap(x1, x2);
        std::swap(y1, y2);
    }

    float gradient = (dx != 0.0f) ? dy / dx : 1.0f;
    float xend = std::round(x1);
    float yend = y1 + gradient * (xend - x1);
    float xgap = 1.0f - fPartOfNumber(x1 + 0.5f);
    int xpxl1 = static_cast<int>(xend);
    int ypxl1 = static_cast<int>(yend);

    set_pen(color_brightness(color, (1.0f - fPartOfNumber(yend)) * xgap));
    set_pixel(steep ? Point(ypxl1, xpxl1) : Point(xpxl1, ypxl1));

    set_pen(color_brightness(color, fPartOfNumber(yend) * xgap));
    set_pixel(steep ? Point(ypxl1 + 1, xpxl1) : Point(xpxl1, ypxl1 + 1));

    float intery = yend + gradient;

    xend = std::round(x2);
    yend = y2 + gradient * (xend - x2);
    xgap = fPartOfNumber(x2 + 0.5f);
    int xpxl2 = static_cast<int>(xend);
    int ypxl2 = static_cast<int>(yend);

    set_pen(color_brightness(color, (1.0f - fPartOfNumber(yend)) * xgap));
    set_pixel(steep ? Point(ypxl2, xpxl2) : Point(xpxl2, ypxl2));

    set_pen(color_brightness(color, fPartOfNumber(yend) * xgap));
    set_pixel(steep ? Point(ypxl2 + 1, xpxl2) : Point(xpxl2, ypxl2 + 1));

    for (int x = xpxl1 + 1; x <= xpxl2 - 1; ++x)
    {
        int y = static_cast<int>(intery);
        set_pen(color_brightness(color, 1.0f - fPartOfNumber(intery)));
        set_pixel(steep ? Point(y, x) : Point(x, y));

        set_pen(color_brightness(color, fPartOfNumber(intery)));
        set_pixel(steep ? Point(y + 1, x) : Point(x, y + 1));

        intery += gradient;
    }
}
