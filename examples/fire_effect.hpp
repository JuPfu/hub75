// Example derived from https://github.com/pimoroni/pimoroni-pico/blob/main/examples/interstate75/interstate75_fire_effect.cpp
#include "libraries/pico_graphics/pico_graphics.hpp"

using namespace pimoroni;

class FireEffect : public PicoGraphics_PenRGB888
{
private:
    uint width, height;
    bool landscape = true;
    float *heat; // Pointer to dynamically allocated memory

public:
    explicit FireEffect(uint width = 64, uint height = 64)
        : PicoGraphics_PenRGB888(width, height, nullptr), width(width), height(height)
    {
        heat = new float[width * height](); // Allocate memory and zero-initialize
        if (width > height)
        {
            landscape = false;
        }
    }

    ~FireEffect()
    {
        delete[] heat; // Properly deallocate memory
    }

    void set(int x, int y, float v)
    {
        heat[x + y * width] = v;
    }

    float get(int x, int y)
    {
        if (y >= height)
            y = height - 1;
        else if (y < 0)
            y = 0;
        if (x >= width)
            x = width - 1;
        else if (x < 0)
            x = 0;

        return heat[x + y * width];
    }

    void burn();
};
