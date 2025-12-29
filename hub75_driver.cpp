#include "pico/stdlib.h"
#include "pico/multicore.h"

// Pico W devices use a GPIO on the WIFI chip for the LED,
// so when building for Pico W, CYW43_WL_GPIO_LED_PIN will be defined
#ifdef CYW43_WL_GPIO_LED_PIN
#include "pico/cyw43_arch.h"
#endif

#include "hardware/clocks.h"
#include "hardware/gpio.h"

#include "hub75.hpp"

// Example images
#include "vanessa_mai_64x64.h"
#include "taylor_swift_64x64.h"

// Example effects
#include "antialiased_line.hpp"
#include "bouncing_balls.hpp"
#include "rotator.cpp"
#include "fire_effect.hpp"
#include "hue_value_spectrum.hpp"
#include "pixel_fill.hpp"

// Set RGB_MATRIX_WIDTH and RGB_MATRIX_HEIGHT to the width and height of your matrix panel!
#define RGB_MATRIX_WIDTH 64
#define RGB_MATRIX_HEIGHT 64

// Panel type FM6126A receives some initial incantation sequence.
// This should usually have no effect on generic matrix panels.
// You might see a short lighting of some leds for generic panels.
// To suppress this effect set PANEL_TYPE to PANEL_GENERIC.

// PanelType - either PANEL_GENERIC or PANEL_FM6126A
#define PANEL_TYPE PANEL_GENERIC

// Some matrix panels have inverted STROBE signals.
// If your data-sheet says so set STB_INVERTED to true.

// stb_inverted - either true (inverted) or false (default)
#define STB_INVERTED false

static int demo_index = 0; ///< Example selector

// Perform initialisation
int pico_led_init(void)
{
#if defined(PICO_DEFAULT_LED_PIN)
    // A device like Pico that uses a GPIO for the LED will define PICO_DEFAULT_LED_PIN
    // so we can use normal GPIO functionality to turn the led on and off
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    return PICO_OK;
#elif defined(CYW43_WL_GPIO_LED_PIN)
    // For Pico W devices we need to initialise the driver etc
    return cyw43_arch_init();
#endif
}

// Turn the led on or off
void pico_set_led(bool led_on)
{
#if defined(PICO_DEFAULT_LED_PIN)
    // Just set the GPIO on or off
    gpio_put(PICO_DEFAULT_LED_PIN, led_on);
#elif defined(CYW43_WL_GPIO_LED_PIN)
    // Ask the wifi "driver" to set the GPIO on or off
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_on);
#endif
}

// Pico - please, blink LED when program starts
int led_init(void)
{
    int rc = pico_led_init(); // Initialize the LED
    hard_assert(rc == PICO_OK);

    for (int i = 0; i < 8; i++)
    {
        pico_set_led(true);
        sleep_ms(250); // Wait 250ms
        pico_set_led(false);
        sleep_ms(250); // Wait 250ms
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
    if (++demo_index > 6)
    {
        demo_index = 0; // Cycle through all examples
    }
    return true;
}

/**
 * @brief Secondary core entry point - creates and starts driver for HUB75 rgb matrix.
 */
void core1_entry()
{
    create_hub75_driver(RGB_MATRIX_WIDTH, RGB_MATRIX_HEIGHT, PANEL_GENERIC, STB_INVERTED);
    start_hub75_driver();
}

void initialize()
{
    // Set system clock to 250MHz - just to show that it is possible to drive the HUB75 panel with a high clock speed
    set_sys_clock_khz(250000, true);

    stdio_init_all(); // Initialize Pico SDK

    led_init(); // Initialize LED - blinking at program start

    multicore_reset_core1(); // Reset core 1

    multicore_launch_core1(core1_entry); // Launch core 1 entry function - the Hub75 driver is doing its job there
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

    HueValueSpectrum hueValueSpectrum = HueValueSpectrum(RGB_MATRIX_WIDTH, RGB_MATRIX_HEIGHT);

    PixelFill pixelFill = PixelFill(RGB_MATRIX_WIDTH, RGB_MATRIX_HEIGHT);

    // Cycle through the examples - move to next example every 15 seconds
    struct repeating_timer timer;
    add_repeating_timer_ms(-15.0 / 1.0 * 1000.0, skip_to_next_demo, NULL, &timer);

    // The Hub75 driver is constantly running on core 1 with a frequency much higher than 200Hz. CPU load on core 1 is low due to DMA and PIO usage.
    // The animated examples are updated at 60Hz.
    float hz = 60.0f;
    float ms = 1000.0f / hz;

    // set brightness of panel
    float intensity = 1.0f;
    setIntensity(intensity);
    float step = 0.01f;

    while (true)
    {
        if (demo_index == 0)
        {
            // Image data is in r8, g8, b8 format
            bouncingBalls.bounce();
            update(&bouncingBalls);
        }
        else if (demo_index == 1)
        {
            // Image data is in r8, g8, b8 format
            fireEffect.burn();
            update(&fireEffect);
        }
        else if (demo_index == 2)
        {
            // Taylor Swift - image data is in b8, g8, r8 format
            // By iHeartRadioCA, CC BY 3.0, https://commons.wikimedia.org/w/index.php?curid=137551448
            update_bgr(taylor_swift_64x64);
        }
        else if (demo_index == 3)
        {
            // Image data is in r8, g8, b8 format
            rotator.draw_line();
            update(&rotator);
        }
        else if (demo_index == 4)
        {
            // Image data is in r8, g8, b8 format
            hueValueSpectrum.drawShades();
            update(&hueValueSpectrum);
        }

        else if (demo_index == 5)
        {
            // Vanessa Mai - image data is in b8, g8, r8 format
            // By Lanzunlimited, CC BY-SA 4.0, https://commons.wikimedia.org/w/index.php?curid=87037267
            update_bgr(vanessa_mai_64x64);
        }
        else if (demo_index == 6)
        {
            // Image data is in r8, g8, b8 format
            pixelFill.fill(0, (RGB_MATRIX_WIDTH * RGB_MATRIX_HEIGHT) -1);
            update(&pixelFill);
        }

        // matrix panel brightness will vary
        float value = sin(intensity);
        setIntensity(MAX(0.15, (value * value * value * value)));

        // Update intensity for next loop
        intensity += step;
        if (intensity >= M_PI)
        {
            intensity = 0.0f;
        }

        sleep_ms(ms); // 60 updates per second - the HUB75 driver is running independently with far more than 200Hz (see README.md)
    }
}
