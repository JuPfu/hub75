#include "antialiased_line.hpp"

using namespace pimoroni;

#ifndef M_PI
#define M_PI (3.14159265358979323846)
#endif

class Rotator : public AntialiasedLine
{
private:
    uint w = 64;
    uint h = 64;

    float rx = 1.0f;
    float ry = 1.0f;
    float r = 1.0f;
    float a = 0.0f;
    float da = M_PI / 180.0f;
    float incr = 0.0f;

    float centerX = 0.0f;
    float centerY = 0.0f;

    uint32_t col = 0xffff00;

public:
    explicit Rotator(uint width = 64, uint height = 64) : AntialiasedLine(width, height), w(width), h(height)
    {
        centerX = w / 2.0f;
        centerY = h / 2.0f;

        uint l = MIN(w, h);

        r = l / 2.0f;
    }

    void draw()
    {
        set_pen(0);
        clear();
        set_pen(col);

        float c = cos(a);
        float s = sin(a);

        r = std::clamp(r + incr, 0.0f, 30.0f);

        rx = r * c;
        ry = r * s;

        a += da;
        if (a >= 2.0f * M_PI)
            a -= 2.0f * M_PI;

        drawLine(rx + centerX, ry + centerY, -rx + centerX, -ry + centerY, col);
    }
};
