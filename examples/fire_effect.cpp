// Example derived from https://github.com/pimoroni/pimoroni-pico/blob/main/examples/interstate75/interstate75_fire_effect.cpp
#include "pico_graphics.hpp"

using namespace pimoroni;

template <uint32_t W, uint32_t H>
class FireEffect : public PicoGraphics_PenRGB888
{
private:
    bool landscape = true;

    alignas(4) uint8_t pixel_buf_[W * H * sizeof(uint32_t)];
    float heat_[W * H]{};

public:
    FireEffect() : PicoGraphics_PenRGB888(W, H, pixel_buf_) {}

    void set(int x, int y, float v)
    {
        heat_[x + y * W] = v;
    }

    float get(int x, int y)
    {
        if (y >= (int)H)
            y = H - 1;
        else if (y < 0)
            y = 0;
        if (x >= (int)W)
            x = W - 1;
        else if (x < 0)
            x = 0;

        return heat_[x + y * W];
    }

    void burn();
};

template <uint32_t W, uint32_t H>
void FireEffect<W, H>::burn()
{
    for (uint32_t y = 0; y < H; y++)
    {
        for (uint32_t x = 0; x < W; x++)
        {
            float value = get(x, y);

            if (value > 0.5f)
            {
                int r = 25 - (int)((255 * value) * 0.1f);
                set_pen(255 - r, 255 - r, (int)(150 * value) + 105);
            }
            else if (value > 0.4f)
            {
                int b = (int)(350 * value) - 140;
                set_pen(220 + (b >> 1), 160, b);
            }
            else if (value > 0.3f)
            {
                int b = (int)(500 * value) - 150;
                set_pen(180 + (b >> 1), 30, b);
            }
            else
            {
                int c = (int)(150 * value);
                set_pen(c, c, c);
            }

            pixel(Point(x, y));

            // update this pixel by averaging the below pixels
            float average = (get(x, y) + get(x, y + 2) + get(x, y + 1) + get(x - 1, y + 1) + get(x + 1, y + 1)) / 5.0f;

            // damping factor to ensure flame tapers out towards the top of the displays
            average *= landscape ? 0.985f : 0.99f;

            // update the heat map with our newly averaged value
            set(x, y, average);
        }
    }

    // clear the bottom row and then add a new fire seed to it
    for (uint32_t x = 0; x < W; x++)
    {
        set(x, H - 1, 0.0f);
    }

    // add a new random heat source
    int source_count = landscape ? 7 : 1;
    for (int c = 0; c < source_count; c++)
    {
        int px = (rand() % (W - 4)) + 2;
        set(px, H - 2, 1.0f);
        set(px + 1, H - 2, 1.0f);
        set(px - 1, H - 2, 1.0f);
        set(px, H - 1, 1.0f);
        set(px + 1, H - 1, 1.0f);
        set(px - 1, H - 1, 1.0f);
    }
}
