#pragma once

#include "antialiased_line.hpp"

using namespace pimoroni;

#ifndef M_PI
#define M_PI (3.14159265358979323846)
#endif

template <uint32_t W, uint32_t H>
class Rotator : public AntialiasedLine<W, H>
{
private:
    float rx = 1.0f;
    float ry = 1.0f;
    float r = 1.0f;
    float da = M_PI / 180.0f;

    float s[360];
    float c[360];

    float centerX = 0.0f;
    float centerY = 0.0f;

    int j = 0;

    uint32_t col = 0xffffff;

public:
    explicit Rotator()
    {
        centerX = W / 2.0f;
        centerY = H / 2.0f;

        uint l = MIN(W, H);

        r = l / 2.0f - 1.01f;

        for (auto i = 180; i < 540; i++)
        {
            s[i - 180] = std::sin(i * da) * r;
            c[i - 180] = std::cos(i * da) * r;
        }

        j = 0;
    }

    void draw()
    {
        this->set_pen(0);
        this->clear();
        this->set_pen(col);

        rx = c[j];
        ry = s[j];

        if (j > 270)
        {
            this->drawLine(rx + centerX, ry + centerY, -rx + centerX, -ry + centerY, 0xffff00);
        }
        else if (j > 180)
        {
            this->drawLine(rx + centerX, ry + centerY, -rx + centerX, -ry + centerY, 0x00ff00);
        }
        else if (j > 90)
        {
            this->drawLine(rx + centerX, ry + centerY, -rx + centerX, -ry + centerY, 0xff0000);
        }
        else
        {
            this->drawLine(rx + centerX, ry + centerY, -rx + centerX, -ry + centerY, col);
        }
        if (++j >= 360)
            j = 0;
    }
};
