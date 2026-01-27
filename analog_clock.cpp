
#include "pico/aon_timer.h"

#include "antialiased_line.hpp"

using namespace pimoroni;

#ifndef M_PI
#define M_PI (3.14159265358979323846)
#endif

class AnalogClock : public AntialiasedLine
{
private:
    uint w = 64;
    uint h = 64;

    float r = 1.0f;
    float da = M_PI / 30.0f;

    float centerX = 0.0f;
    float centerY = 0.0f;

    float *s;
    float *c;

    int hour = 0;
    int minute = 0;
    int second = 0;

    int previous_second = second;

    uint32_t col1 = 0x31A2F2;
    uint32_t col2 = 0x11819d;
    uint32_t col3 = 0xE06F8B;

public:
    explicit AnalogClock(uint width = 64, uint height = 64) : AntialiasedLine(width, height), w(width), h(height)
    {
        centerX = (float)(w / 2);
        centerY = (float)(h / 2);

        uint l = std::min(w, h);

        r = l * (0.95f / 2.0f);

        s = new float[60]();
        c = new float[60]();
        for (auto i = 15; i < 75; i++)
        {
            s[i - 15] = std::sin(i * da) * r;
            c[i - 15] = std::cos(i * da) * r;
        }

        struct timespec ts = {0LL, 0L};
        ts.tv_sec = 60 * 60 * 3 + 25 * 60 + 45;

        if (!aon_timer_start(&ts))
        {
            printf("COULD NOT SET AON TIMER");
        }

        set_font(&font14_outline);
    }

    void draw()
    {
        struct timespec ct;
        if (aon_timer_get_time(&ct))
        {
            int period = (ct.tv_sec % (3600 * 12));
            hour = (ct.tv_sec % (3600 * 12)) / 3600;
            minute = (period - hour * 3600) / 60;
            second = (period - hour * 3600 - minute * 60);
        }

        // only update frame buffer when second has changed
        if (second != previous_second)
        {
            previous_second = second;

            set_pen(0);
            clear();

            set_pen(0x1B2632);
            set_pen(0xF7E26B);
            
            text("12", Point(w / 2 - 7, 1), false, 0.5f, 0.0, false);
            text("3", Point(w - 8, h / 2 - 7), false, 0.5f, 0.0, false);
            text("6", Point(w / 2 - 2, h - 14), false, 0.5f, 0.0, false);
            text("9", Point(1, h / 2 - 7), false, 0.5f, 0.0, false);

            for (auto i = 0; i < 12; i++)
            {
                if (i % 3 != 0)
                {
                    drawLine(c[i * 5] + centerX, s[i * 5] + centerY, c[i * 5] * 0.8f + centerX, s[i * 5] * 0.8f + centerY, 0xffffff);
                }
            }

            int corrected_hour = hour * 5 + minute / 12;
            drawLine(c[corrected_hour] * 0.1f + centerX + 1, s[corrected_hour] * 0.2f + centerY + 1, -c[corrected_hour] * 0.6f + centerX, -s[corrected_hour] * 0.6f + centerY, 0xffffff);
            drawLine(c[corrected_hour] * 0.1f + centerX, s[corrected_hour] * 0.2f + centerY, -c[corrected_hour] * 0.6f + centerX, -s[corrected_hour] * 0.6f + centerY, col1);
            drawLine(c[minute] * 0.15f + centerX + 1, s[minute] * 0.2f + centerY + 1, -c[minute] * 0.85f + centerX, -s[minute] * 0.85f + centerY, 0xffffff);
            drawLine(c[minute] * 0.15f + centerX, s[minute] * 0.2f + centerY, -c[minute] * 0.85f + centerX, -s[minute] * 0.85f + centerY, col1);
            drawLine(c[second] * 0.1f + centerX + 1, s[second] * 0.1f + centerY + 1, -c[second] * 0.85f + centerX, -s[second] * 0.85f + centerY, 0xBE2633);
            drawLine(c[second] * 0.1f + centerX, s[second] * 0.1f + centerY, -c[second] * 0.85f + centerX, -s[second] * 0.85f + centerY, col3);
        }
    }
};
