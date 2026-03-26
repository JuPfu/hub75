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
    float da = M_PI / 180.0f;

    float *s;
    float *c;

    float centerX = 0.0f;
    float centerY = 0.0f;

    int j = 0;

    uint32_t col = 0xffffff;

public:
    explicit Rotator(uint width = 64, uint height = 64) : AntialiasedLine(width, height), w(width), h(height)
    {
        centerX = w / 2.0f;
        centerY = h / 2.0f;

        uint l = MIN(w, h);

        r = l / 2.0f - 1.01f;

        s = new float[360]();
        c = new float[360]();
        for (auto i = 180; i < 540; i++)
        {
            s[i - 180] = std::sin(i * da) * r;
            c[i - 180] = std::cos(i * da) * r;
        }

        j = 0;
    }

    void draw()
    {
        set_pen(0);
        clear();
        set_pen(col);

        rx = c[j];
        ry = s[j];

        if (j > 270)
        {
            drawLine(rx + centerX, ry + centerY, -rx + centerX, -ry + centerY, 0xffff00);
        }
        else if (j > 180)
        {
            drawLine(rx + centerX, ry + centerY, -rx + centerX, -ry + centerY, 0x00ff00);
        }
        else if (j > 90)
        {
            drawLine(rx + centerX, ry + centerY, -rx + centerX, -ry + centerY, 0xff0000);
        }
        else
        {
            drawLine(rx + centerX, ry + centerY, -rx + centerX, -ry + centerY, col);
        }
        if (++j >= 360)
            j = 0;
    }
};
