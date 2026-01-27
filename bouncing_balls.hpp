// Example derived from https://github.com/pimoroni/pimoroni-pico/blob/main/examples/interstate75/interstate75_balls_demo.cpp

#include "../libraries/pico_graphics/pico_graphics.hpp"
#include "../libraries/bitmap_fonts/font14_outline_data.hpp"

using namespace pimoroni;

class BouncingBalls : public PicoGraphics_PenRGB888
{
private:
    struct mPoint
    {
        float x;
        float y;
        uint8_t r;
        float dx;
        float dy;
        Pen pen;
    };

    std::vector<mPoint> mShapes;

    Point mTextLocation;

    Pen mBG;
    Pen mWHITE;

    struct Rect rect;

    void mCreateShapes(int quantityOfBalls);

public:
    explicit BouncingBalls(uint quantityOfBalls = 10, uint width = 64, uint height = 64) : PicoGraphics_PenRGB888(width, height, nullptr)
    {
        mCreateShapes(quantityOfBalls);

        if (height < 64)
        {
            mTextLocation = Point(1, 1);
        }
        else
        {
            mTextLocation = Point(10, 10);
        }

        rect = {0, 0, (int32_t)width, (int32_t)height};

        mBG = create_pen(0xEB, 0x89, 0x31);
        mWHITE = create_pen(250, 250, 250);
        set_font(&font14_outline);
    }

    void bounce();
};