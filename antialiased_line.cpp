#include "antialiased_line.hpp"
#include <algorithm> // for std::swap

// Avoid redundant function calls by inlining.
inline float AntialiasedLine::fPartOfNumber(float x)
{
    return x - static_cast<int>(x);
}

// Optimized color brightness function with minimal floating-point operations
uint32_t AntialiasedLine::color_brightness(uint32_t color, float brightness)
{
    uint32_t r = ((color >> 16) & 0xFF) * brightness + 0.5f;
    uint32_t g = ((color >> 8) & 0xFF) * brightness + 0.5f;
    uint32_t b = (color & 0xFF) * brightness + 0.5f;
    return (r << 16) | (g << 8) | b;
}

void AntialiasedLine::drawLine(float x1, float y1, float x2, float y2, uint32_t color)
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

    float gradient = dy / dx;
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
