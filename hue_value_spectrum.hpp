// Example derviced from https://github.com/mrcodetastic/ESP32-HUB75-MatrixPanel-DMA/blob/master/examples/HueValueSpectrum/HueValueSpectrum.ino
#include <algorithm>

#include "libraries/pico_graphics/pico_graphics.hpp"
#include "libraries/bitmap_fonts/font14_outline_data.hpp"

using namespace pimoroni;

#ifndef M_PI
#define M_PI (3.14159265358979323846)
#endif

class HueValueSpectrum : public PicoGraphics_PenRGB888
{
private:
    uint width, height;

public:
    explicit HueValueSpectrum(uint width = 64, uint height = 64) : PicoGraphics_PenRGB888(width, height, nullptr), width(width), height(height) {}

    void drawShades()
    {
        // Canvas loop
        float t = (float)((millis() % 8000u) / 8000.f);
        float tt = (float)((millis() % 32000u) / 32000.f);

        for (int x = 0; x < width; x++)
        {
            // calculate the overal shade
            float f = (((sin(tt - (float)x / height / 32.) * 2.f * M_PI) + 1.0f) / 2.0f) * 255.0f;
            // calculate hue spectrum into rgb
            float r = std::max(std::min(cosf(2.f * M_PI * (t + ((float)x / height + 0.f) / 3.f)) + 0.5f, 1.f), 0.f);
            float g = std::max(std::min(cosf(2.f * M_PI * (t + ((float)x / height + 1.f) / 3.f)) + 0.5f, 1.f), 0.f);
            float b = std::max(std::min(cosf(2.f * M_PI * (t + ((float)x / height + 2.f) / 3.f)) + 0.5f, 1.f), 0.f);

            // iterate pixels for every row
            for (int y = 0; y < height; y++)
            {
                if (y * 2 < height)
                {
                    // top-middle part of screen, transition of value
                    t = (2.f * y + 1.0f) / height;
                    set_pen((uint8_t)((r * t) * f), (uint8_t)((g * t) * f), (uint8_t)((b * t) * f));
                }
                else
                {
                    // middle to bottom of screen, transition of saturation
                    t = (2.f * (height - y) - 1.0f) / height;
                    set_pen((uint8_t)((r * t + 1.0f - t) * f), (uint8_t)((g * t + 1.0f - t) * f), (uint8_t)((b * t + 1.0f - t) * f));
                }
                set_pixel(Point(x, y));
            }
        }
    }
};