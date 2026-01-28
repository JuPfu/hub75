
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
    uint l = 1;

    float da = M_PI / 30.0f;

    float centerX = 0.0f;
    float centerY = 0.0f;

    float *s;
    float *c;

    int hour = 0;
    int minute = 0;
    int second = 0;

    int previous_second = second;

    constexpr static uint32_t hour_hand = 0x31A2F2;          // #31A2F2
    constexpr static uint32_t minute_hand = 0x11819d;        // #11819d
    constexpr static uint32_t second_hand = 0xE06F8B;        // #E06F8B
    constexpr static uint32_t second_shadow_hand = 0xBE2633; // #BE2633

    constexpr static uint32_t shadow_hand = 0xffffff;  // #ffffff
    constexpr static uint32_t main_ticks = 0xffffff;   // #ffffff
    constexpr static uint32_t clock_digits = 0xF7E26B; // #F7E26B

public:
    explicit AnalogClock(uint width = 64, uint height = 64) : AntialiasedLine(width, height), w(width), h(height)
    {
        centerX = (float)(w / 2);
        centerY = (float)(h / 2);

        l = std::min(w, h);

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

            set_pen(clock_digits);

            text("12", Point(l / 2 - 7, 1), false, 0.5f, 0.0, false);
            text("3", Point(l - 8, l / 2 - 7), false, 0.5f, 0.0, false);
            text("6", Point(l / 2 - 2, l - 14), false, 0.5f, 0.0, false);
            text("9", Point(1, l / 2 - 7), false, 0.5f, 0.0, false);

            for (auto i = 0; i < 12; i++)
            {
                if (i % 3 != 0)
                {
                    drawLine(c[i * 5] + centerX, s[i * 5] + centerY, c[i * 5] * 0.8f + centerX, s[i * 5] * 0.8f + centerY, main_ticks);
                }
            }

            int corrected_hour = hour * 5 + minute / 12; // additionally move hour hand a little bit every 12 minutes between hour marks
            drawLine(c[corrected_hour] * 0.1f + centerX + 1, s[corrected_hour] * 0.2f + centerY + 1, -c[corrected_hour] * 0.6f + centerX, -s[corrected_hour] * 0.6f + centerY, shadow_hand);
            drawLine(c[corrected_hour] * 0.1f + centerX, s[corrected_hour] * 0.2f + centerY, -c[corrected_hour] * 0.6f + centerX, -s[corrected_hour] * 0.6f + centerY, hour_hand);
            drawLine(c[minute] * 0.15f + centerX + 1, s[minute] * 0.2f + centerY + 1, -c[minute] * 0.85f + centerX, -s[minute] * 0.85f + centerY, shadow_hand);
            drawLine(c[minute] * 0.15f + centerX, s[minute] * 0.2f + centerY, -c[minute] * 0.85f + centerX, -s[minute] * 0.85f + centerY, minute_hand);
            drawLine(c[second] * 0.1f + centerX + 1, s[second] * 0.1f + centerY + 1, -c[second] * 0.85f + centerX, -s[second] * 0.85f + centerY, second_shadow_hand);
            drawLine(c[second] * 0.1f + centerX, s[second] * 0.1f + centerY, -c[second] * 0.85f + centerX, -s[second] * 0.85f + centerY, second_hand);
        }
    }
};
