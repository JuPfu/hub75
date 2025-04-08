#include "pico.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"

#include "libraries/pico_graphics/pico_graphics.hpp"

#include "hub75.hpp"

// Example effects
#include "antialiased_line.hpp"
#include "bouncing_balls.hpp"
#include "rotator.cpp"
#include "fire_effect.hpp"

// Example images
#include "vanessa_mai_64x64.h"
#include "taylor_swift_64x64.h"

#define RGB_MATRIX_WIDTH 64
#define RGB_MATRIX_HEIGHT 64
#define OFFSET RGB_MATRIX_WIDTH *(RGB_MATRIX_HEIGHT >> 1)

extern volatile uint32_t *frame_buffer; ///< Frame buffer addrss of the Hub75 driver buffer containing interwoven pixel data for display.

static int frame_index = 0; ///< Example selector

using namespace pimoroni;

// This gamma table is used to correct 8-bit (0-255) colours up to 10-bit, applying gamma correction without losing dynamic range.
// The gamma table is from pimeroni's https://github.com/pimoroni/pimoroni-pico/tree/main/drivers/hub75.

static const uint16_t gamma_lut[256] = {
    0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8,
    8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15, 16,
    16, 17, 17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22, 23, 24, 25,
    26, 27, 29, 30, 31, 33, 34, 35, 37, 38, 40, 41, 43, 44, 46, 47,
    49, 51, 53, 54, 56, 58, 60, 62, 64, 66, 68, 70, 72, 74, 76, 78,
    80, 82, 85, 87, 89, 92, 94, 96, 99, 101, 104, 106, 109, 112, 114, 117,
    120, 122, 125, 128, 131, 134, 137, 140, 143, 146, 149, 152, 155, 158, 161, 164,
    168, 171, 174, 178, 181, 185, 188, 192, 195, 199, 202, 206, 210, 214, 217, 221,
    225, 229, 233, 237, 241, 245, 249, 253, 257, 261, 265, 270, 274, 278, 283, 287,
    291, 296, 300, 305, 309, 314, 319, 323, 328, 333, 338, 343, 347, 352, 357, 362,
    367, 372, 378, 383, 388, 393, 398, 404, 409, 414, 420, 425, 431, 436, 442, 447,
    453, 459, 464, 470, 476, 482, 488, 494, 499, 505, 511, 518, 524, 530, 536, 542,
    548, 555, 561, 568, 574, 580, 587, 593, 600, 607, 613, 620, 627, 633, 640, 647,
    654, 661, 668, 675, 682, 689, 696, 703, 711, 718, 725, 733, 740, 747, 755, 762,
    770, 777, 785, 793, 800, 808, 816, 824, 832, 839, 847, 855, 863, 872, 880, 888,
    896, 904, 912, 921, 929, 938, 946, 954, 963, 972, 980, 989, 997, 1006, 1015, 1023};

// Pico 1 - please, blink LED when program starts
int led_init(void)
{
    gpio_init(25);              // Initialize default LED pin
    gpio_set_dir(25, GPIO_OUT); // Set as output

    for (int i = 0; i < 8; i++)
    {
        gpio_put(25, true);  // Turn LED on
        sleep_ms(500);       // Wait 500ms
        gpio_put(25, false); // Turn LED off
        sleep_ms(750);       // Wait 500ms
    }
    return PICO_OK;
}

/**
 * @brief Cycle through all examples
 *
 * @param t pointer to repeating timer
 * @return true
 */
bool skip_to_next_demo(__unused struct repeating_timer *t)
{
    if (++frame_index > 4)
        frame_index = 0; // Cycle through all examples

    return true;
}

/**
 * @brief Secondary core entry point - creates and starts driver for HUB75 rgb matrix.
 */
void core1_entry()
{
    create_hub75_driver(RGB_MATRIX_WIDTH, RGB_MATRIX_HEIGHT);
    start_hub75_driver();
}

void update(
    PicoGraphics const *graphics // Graphics object to be updated - RGB888 format, this is 24-bits (8 bits per color channel) in a uint32_t array
)
{
    if (graphics->pen_type == PicoGraphics::PEN_RGB888)
    {
        uint32_t const *src = static_cast<uint32_t const *>(graphics->frame_buffer);

        // Ramp up color resolution from 8 to 10 bits via gamma table look-up
        // Interweave pixels from intermediate buffer into target image to fit the format expected by Hub75 LED panel.
        uint j = 0;
        for (int i = 0; i < RGB_MATRIX_HEIGHT * RGB_MATRIX_WIDTH; i += 2)
        {
            frame_buffer[i] = gamma_lut[(src[j] & 0x0000ff) >> 0] << 20 | gamma_lut[(src[j] & 0x00ff00) >> 8] << 10 | gamma_lut[(src[j] & 0xff0000) >> 16];
            frame_buffer[i + 1] = gamma_lut[(src[j + OFFSET] & 0x0000ff) >> 0] << 20 | gamma_lut[(src[j + OFFSET] & 0x00ff00) >> 8] << 10 | gamma_lut[(src[j + OFFSET] & 0xff0000) >> 16];
            j++;
        }
    }
}

void update_bgr(uint8_t *src)
{
    uint offset = OFFSET * 3;
    uint k = 0;
    // Ramp up color resolution from 8 to 10 bits via gamma table look-up
    // Interweave pixels as requiered by Hub75 LED panel matrix
    for (int j = 0; j < RGB_MATRIX_WIDTH * RGB_MATRIX_HEIGHT; j += 2)
    {
        frame_buffer[j] = gamma_lut[src[k]] << 20 | gamma_lut[src[k + 1]] << 10 | gamma_lut[src[k + 2]];
        frame_buffer[j + 1] = gamma_lut[src[offset + k]] << 20 | gamma_lut[src[offset + k + 1]] << 10 | gamma_lut[src[offset + k + 2]];
        k += 3;
    }
}

void initialize()
{
    // Set system clock to 250MHz - just to show that it is possible to drive the HUB75 panel with a high clock speed
    set_sys_clock_khz(250000, true);

    // Initialize Pico SDK
    stdio_init_all();

    // Initialize LED - blinking at program start
    led_init();

    // Initialize Hub75 driver on core 1
    multicore_reset_core1();
}

int main()
{
    initialize();

    // The following examples are animated. In the update function the color of the modified image data is ramped up to 10 bits and the image data is interwoven.

    // Create bouncing balls using pico_graphics functionality - image data is delivered in uint32_t array with 24-bit (rgb888) color data format
    BouncingBalls bouncingBalls(25, RGB_MATRIX_WIDTH, RGB_MATRIX_HEIGHT);

    // Create rotating antialiased line using pico_graphics functionality - image data is delivered in uint32_t array with 24-bit (rgb888) color data format
    Rotator rotator(RGB_MATRIX_WIDTH, RGB_MATRIX_HEIGHT);

    // Create fire effect using pico_graphics functionality - image data is delivered in uint32_t array with 24-bit (rgb888) color data format
    FireEffect fireEffect = FireEffect(RGB_MATRIX_WIDTH, RGB_MATRIX_HEIGHT);

    multicore_launch_core1(core1_entry); // Launch core 1 entry function - the Hub75 driver is doing its job there

    // Cycle through the examples - move to next example every 10 seconds
    struct repeating_timer timer;
    add_repeating_timer_ms(-10.0 / 1.0 * 1000.0, skip_to_next_demo, NULL, &timer);

    // The Hub75 driver is constantly running on core 1 with a frequency much higher than 200Hz. CPU load on core 1 is low due to DMA and PIO usage.
    // The animated examples are updated at 60Hz.
    float hz = 60.0f;
    float ms = 1000.0f / hz;

    while (true)
    {
        if (frame_index == 0)
        {
            // Vanessa Mai - image data is in b8, g8, r8 format
            // By Lanzunlimited, CC BY-SA 4.0, https://commons.wikimedia.org/w/index.php?curid=87037267
            update_bgr(vanessa_mai_64x64);
        }
        else if (frame_index == 1)
        {
            // Taylor Swift - image data is in b8, g8, r8 format
            // By iHeartRadioCA, CC BY 3.0, https://commons.wikimedia.org/w/index.php?curid=137551448
            update_bgr(taylor_swift_64x64);
        }
        else if (frame_index == 2)
        {
            // Image data is in r8, g8, b8 format
            fireEffect.burn();
            update(&fireEffect);
        }
        else if (frame_index == 3)
        {
            // Image data is in r8, g8, b8 format
            rotator.draw_line();
            update(&rotator);
        }
        else if (frame_index == 4)
        {
            // Image data is in r8, g8, b8 format
            bouncingBalls.bounce();
            update(&bouncingBalls);
        }
        sleep_ms(ms); // 60 updates per second - the HUB75 driver is running independently with far more than 200Hz
    }
}
