// Example derived from https://github.com/pimoroni/pimoroni-pico/blob/main/examples/interstate75/interstate75_balls_demo.cpp

#include "bouncing_balls.hpp"
#include <random>

void BouncingBalls::bounce() {
    set_pen(mBG);
    clear(); 
    
    int bound_w = bounds.w;
    int bound_h = bounds.h;

    for (auto &shape : mShapes)
    {
        shape.x += shape.dx;
        shape.y += shape.dy;

        if (shape.x - shape.r < 0) {
            shape.dx = -shape.dx;
            shape.x = shape.r;
        }
        else if (shape.x + shape.r >= bound_w) {
            shape.dx = -shape.dx;
            shape.x = bound_w - shape.r;
        }

        if (shape.y - shape.r < 0) {
            shape.dy = -shape.dy;
            shape.y = shape.r;
        }
        else if (shape.y + shape.r >= bound_h) {
            shape.dy = -shape.dy;
            shape.y = bound_h - shape.r;
        }

        set_pen(shape.pen);
        circle(Point(shape.x, shape.y), shape.r);
    }

    set_pen(mWHITE);
    text("Hello World", mTextLocation, false, 0.5f, 0.0f, false);
}

void BouncingBalls::mCreateShapes(int quantityOfBalls)
{
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<int> rand_x(0, bounds.w - 1);
    static std::uniform_int_distribution<int> rand_y(0, bounds.h - 1);
    static std::uniform_int_distribution<int> rand_r(2, 6);
    static std::uniform_real_distribution<float> rand_speed(-2.0f, 2.0f);
    static std::uniform_int_distribution<int> rand_color(0, 255);

    for (uint8_t i = 0; i < quantityOfBalls; i++)
    {
        mShapes.emplace_back(mPoint{
            static_cast<float>(rand_x(gen)),
            static_cast<float>(rand_y(gen)),
            static_cast<uint8_t>(rand_r(gen)),
            rand_speed(gen),
            rand_speed(gen),
            create_pen(rand_color(gen), rand_color(gen), rand_color(gen))
        });
    }
}
