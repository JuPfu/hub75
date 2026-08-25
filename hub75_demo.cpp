#include "pico/stdlib.h"
#include "pico/multicore.h"

// Pico W devices use a GPIO on the WIFI chip for the LED,
// so when building for Pico W, CYW43_WL_GPIO_LED_PIN will be defined
#ifdef CYW43_WL_GPIO_LED_PIN
#include "pico/cyw43_arch.h"
#endif

#include "hardware/clocks.h"

#include "hub75.hpp"

// Panel/pin/color/rotation configuration - see include/hub75.hpp for field docs.
// Matches a single generic 64x64 panel wired to GPIO 0-13.
constexpr Hub75Config panel_cfg{
    .panel = {
        .matrix_panel_width = 64,
        .matrix_panel_height = 64,
        .chain_rows = 1,
        .chain_cols = 1,
        .chain_mode = Hub75ChainMode::SERPENTINE,
        .panel_kind = RowMapping::Standard,
        .panel_chip = Hub75PanelChip::GENERIC,
        .inverted_stb = false,
        .sm_clockdiv_factor = 1.0f,
        .base_latch_ns = 80,
        .base_addr_ns = 160,
    },
    .screen = {
        .rotation = Hub75Rotation::DEG_0,
    },
    .pins = {
        .data_base_pin = 0,
        .data_n_pins = 6,
        .rowsel_base_pin = 6,
        .rowsel_n_pins = 5,
        .clk_pin = 11,
        .strobe_pin = 12,
        .oen_pin = 13,
    },
    .color = {
        .bitplanes = 10,
        .separate_cie_channels = true,
        .balanced_light_output = true,
        .ccm_rg_shift = 6,
        .ccm_gb_shift = 7,
    },
    .frame_rate_debug = false,
};

using Panel = Hub75Driver<panel_cfg>;

// Large fixed-size buffers - must have static storage duration, not live on the stack.
static Panel driver;

// Example images. All four are included unconditionally; demo_image() below picks the
// one matching Panel's actual size via if constexpr, so the other three are never referenced
// and the compiler discards them (each is `static`, internal linkage, unused).
#include "taylor_swift_128x64.h"
#include "taylor_swift_64x128.h"
#include "taylor_swift_64x64.h"
#include "matreshka_32x16.h"

const uint8_t *demo_image()
{
    if constexpr (Panel::SCREEN_WIDTH == 128 && Panel::SCREEN_HEIGHT == 64)
        return taylor_swift_128x64;
    else if constexpr (Panel::SCREEN_WIDTH == 64 && Panel::SCREEN_HEIGHT == 128)
        return taylor_swift_64x128;
    else if constexpr (Panel::SCREEN_WIDTH == 64 && Panel::SCREEN_HEIGHT == 64)
        return taylor_swift_64x64;
    else
        return matreshka_32x16;
}

// Example effects
#include "antialiased_line.hpp"
#include "bouncing_balls.hpp"
#include "rotator.hpp"
#include "analog_clock.hpp"
#include "fire_effect.hpp"
#include "hue_value_spectrum.hpp"
#include "pixel_fill.hpp"
#include "grey_scale_stripes.hpp"
#include "rectangle.hpp"

static int demo_index = -1; ///< Example selector (-1 for auto-cycle)

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
    if (++demo_index > 8)
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
    driver.create();
    driver.start();

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
    // Set system clock to 266MHz - just to show that it is possible to drive the HUB75 panel with a high clock speed
    set_sys_clock_khz(266000, true);

    stdio_init_all(); // Initialize Pico SDK

    led_init(); // Initialize LED - blinking at program start

    if constexpr (HUB75_MULTICORE)
    {
        // Run hub75 driver on core1
        multicore_reset_core1();             // Reset core 1
        multicore_launch_core1(core1_entry); // Launch core 1 entry function - the Hub75 driver is doing its job there
    }
    else
    {
        // Run hub75 on core0 - the Hub75 driver is doing its job here
        driver.create();
        driver.start();
    }
}

int main()
{
    initialize();

    // The following examples are animated. In the update function the color of the modified image data is ramped up to 10 bits and the image data is interwoven.

    // Create bouncing balls using pico_graphics functionality
    static BouncingBalls<Panel::SCREEN_WIDTH, Panel::SCREEN_HEIGHT> bouncingBalls(10);

    // Create rotating antialiased line using pico_graphics functionality
    static Rotator<Panel::SCREEN_WIDTH, Panel::SCREEN_HEIGHT> rotator;

    // Create analog clock using pico_graphics functionality
    static AnalogClock<Panel::SCREEN_WIDTH, Panel::SCREEN_HEIGHT> analogClock;

    // Create fire effect using pico_graphics functionality
    static FireEffect<Panel::SCREEN_WIDTH, Panel::SCREEN_HEIGHT> fireEffect;

    static HueValueSpectrum<Panel::SCREEN_WIDTH, Panel::SCREEN_HEIGHT> hueValueSpectrum;

    static PixelFill<Panel::SCREEN_WIDTH, Panel::SCREEN_HEIGHT> pixelFill;

    // Pico RAM is finite - due to your configuration of panel_cfg (dimensions, bitplanes,
    // balanced_light_output and separate_cie_channels) you have to select just a selection of demos!

    // static GreyScaleStripes<Panel::SCREEN_WIDTH, Panel::SCREEN_HEIGHT> greyScaleStripes;

    // static Rectangle<Panel::SCREEN_WIDTH, Panel::SCREEN_HEIGHT> rectangle;

    // Cycle through the examples - move to next example every 15 seconds
    struct repeating_timer timer;
    if (demo_index < 0)
    {
        demo_index = 0;
        add_repeating_timer_ms(-15.0 / 1.0 * 1000.0, skip_to_next_demo, NULL, &timer);
    }

    // The Hub75 driver is constantly running on core 1 with a frequency usually much higher than 200Hz.
    // CPU load (on core 1) is low due to DMA and PIO usage.
    // The animated examples are updated at 100Hz.
    const float fps = 100.0f;
    const float frame_delay_ms = 1000.0f / fps;

    // set basis brightness of matrix panel
    driver.setBasisBrightness(8);

    // set full brightness of panel
    float intensity = 1.0f;
    driver.setIntensity(intensity);

    float step = -0.005f;

    absolute_time_t next_frame = make_timeout_time_ms((uint32_t)frame_delay_ms);

    while (true)
    {
        if (demo_index == 0)
        {
            // Image data is in r8, g8, b8 format
            bouncingBalls.bounce();
            driver.update(&bouncingBalls);
        }
        else if (demo_index == 1)
        {
            // Image data is in r8, g8, b8 format
            fireEffect.burn();
            driver.update(&fireEffect);
        }
        else if (demo_index == 2)
        {
            // Taylor Swift - image data is in b8, g8, r8 format
            // By iHeartRadioCA, CC BY 3.0, https://commons.wikimedia.org/w/index.php?curid=137551448
            driver.update_bgr(demo_image());
        }
        else if (demo_index == 3)
        {
            rotator.draw();
            driver.update(&rotator);
        }
        else if (demo_index == 4)
        {
            analogClock.draw();
            driver.update(&analogClock);
        }
        else if (demo_index == 5)
        {
            // Image data is in r8, g8, b8 format
            hueValueSpectrum.drawShades();
            driver.update(&hueValueSpectrum);
        }
        else if (demo_index == 6)
        {
            // Image data is in r8, g8, b8 format
            pixelFill.fill();
            driver.update(&pixelFill);
        }
        // else if (demo_index == 7)
        // {
        //     greyScaleStripes.drawStripes();
        //     driver.update(&greyScaleStripes);
        // }
        // else if (demo_index == 8)
        // {
        //     rectangle.draw();
        //     driver.update(&rectangle);
        // }

        // matrix panel brightness will vary when you uncomment the following api call
        // driver.setIntensity(intensity);

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
        sleep_until(next_frame);
        next_frame = delayed_by_ms(next_frame, (uint32_t)frame_delay_ms);
    }
}
