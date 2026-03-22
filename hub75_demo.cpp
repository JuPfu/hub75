#include "pico/stdlib.h"

// Pico W devices use a GPIO on the WIFI chip for the LED,
// so when building for Pico W, CYW43_WL_GPIO_LED_PIN will be defined
#ifdef CYW43_WL_GPIO_LED_PIN
#include "pico/cyw43_arch.h"
#endif

#include "hardware/clocks.h"

#include "hub75.hpp"

#if HUB75_MULTICORE == true
#include "pico/multicore.h"
#endif

// Example images
#if MATRIX_PANEL_HEIGHT == 64
#include "taylor_swift_64x64.h"
#else
#include "matreshka_32x16.h"
#endif

// Example effects
#include "antialiased_line.hpp"
#include "bouncing_balls.hpp"
#include "rotator.cpp"
#include "analog_clock.cpp"
#include "fire_effect.hpp"
#include "hue_value_spectrum.hpp"
#include "pixel_fill.hpp"

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
#else
    return PICO_OK;
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
 * @brief Secondary core entry point.
 *
 * Initializes and starts the HUB75 driver on core 1.
 */
void core1_entry()
{
    create_hub75_driver(MATRIX_PANEL_WIDTH, MATRIX_PANEL_HEIGHT, PANEL_TYPE, INVERTED_STB);
    start_hub75_driver();

    // KEEP CORE 1 ALIVE — without this, Core 1's NVIC is torn down and DMA_IRQ_1 stops firing
    //
    // Add your additional tasks for core1 here
    while (true)
    {
        tight_loop_contents();
    }
}

void initialize()
{
    // Set system clock to 250MHz - just to show that it is possible to drive the HUB75 panel with a high clock speed
    set_sys_clock_khz(266000, true);

    stdio_init_all(); // Initialize Pico SDK

    led_init(); // Initialize LED - blinking at program start

#if HUB75_MULTICORE == true
    // Run hub75 driver on core1
    multicore_reset_core1();             // Reset core 1
    multicore_launch_core1(core1_entry); // Launch core 1 entry function - the Hub75 driver is doing its job there
#else
    // Run hub75 on core0 - the Hub75 driver is doing its job here
    create_hub75_driver(MATRIX_PANEL_WIDTH, MATRIX_PANEL_HEIGHT, PANEL_TYPE, INVERTED_STB);
    start_hub75_driver();
#endif
}

int main()
{
    initialize();

    // The following examples are animated. In the update function the color of the modified image data is ramped up to 10 bits and the image data is interwoven.

    // Create bouncing balls using pico_graphics functionality
    BouncingBalls bouncingBalls(10, MATRIX_PANEL_WIDTH, MATRIX_PANEL_HEIGHT);

    // Create rotating antialiased line using pico_graphics functionality
    Rotator rotator(MATRIX_PANEL_WIDTH, MATRIX_PANEL_HEIGHT);

    // Create analog clock using pico_graphics functionality
    AnalogClock analogClock(MATRIX_PANEL_WIDTH, MATRIX_PANEL_HEIGHT);

    // Create fire effect using pico_graphics functionality
    FireEffect fireEffect = FireEffect(MATRIX_PANEL_WIDTH, MATRIX_PANEL_HEIGHT);

    HueValueSpectrum hueValueSpectrum = HueValueSpectrum(MATRIX_PANEL_WIDTH, MATRIX_PANEL_HEIGHT);

    PixelFill pixelFill = PixelFill(MATRIX_PANEL_WIDTH, MATRIX_PANEL_HEIGHT);

    // Cycle through the examples - move to next example every 15 seconds
    struct repeating_timer timer;
    add_repeating_timer_ms(-15.0 / 1.0 * 1000.0, skip_to_next_demo, NULL, &timer);

    // The Hub75 driver is constantly running on core 1 with a frequency usually much higher than 200Hz.
    // CPU load (on core 1) is low due to DMA and PIO usage.
    // The animated examples are updated at 100Hz.
    float hz = 100.0f;
    float ms = 1000.0f / hz;

    // set basis brightness of matrix panel
    setBasisBrightness(255);

    // set full brightness of panel
    float intensity = 1.0f;
    setIntensity(intensity);

    float step = -0.005f;

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
#if MATRIX_PANEL_HEIGHT == 64
            update_bgr(taylor_swift_64x64);
#else
            update_bgr(matreshka_32x16);
#endif
        }
        else if (demo_index == 3)
        {
            rotator.draw();
            update(&rotator);
        }
        else if (demo_index == 4)
        {
            analogClock.draw();
            update(&analogClock);
        }
        else if (demo_index == 5)
        {
            // Image data is in r8, g8, b8 format
            hueValueSpectrum.drawShades();
            update(&hueValueSpectrum);
        }
        else if (demo_index == 6)
        {
            // Image data is in r8, g8, b8 format
            pixelFill.fill(0, (MATRIX_PANEL_WIDTH * MATRIX_PANEL_HEIGHT) - 1);
            update(&pixelFill);
        }

        printf("intensity = %f\n", intensity);
        // matrix panel brightness will vary
        setIntensity(intensity);

        // Update intensity for next loop
        intensity += step;
        if (intensity >= 1.0f)
        {
            step = -step;
        }
        else if (intensity <= 0.0f)
        {
            step = -step;
        }

        sleep_ms(ms); // hz updates per second - the HUB75 driver is running independently usually with far more than 200Hz (see README.md)
    }
}
