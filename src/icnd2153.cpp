#include <cstdlib>

#include "pico/stdlib.h"
#include "hardware/clocks.h"

#include "hub75.hpp"
#include "icnd2153.hpp"


#ifdef ABC
// =============================================================================
// ICND2153 one-time startup command sequence
// =============================================================================
// Per the ICND2153 datasheet's "Control Command" table: a command is
// selected by the NUMBER of DCLK rising edges that occur while LE is held
// high. This must run BEFORE configure_pio() reassigns CLK_PIN/LE_PIN to
// PIO control, since it needs plain GPIO ownership of those two pins.

// static void icnd2153_send_command(uint dclk_pin, uint le_pin, uint n_edges)
// {
//     gpio_put(le_pin, 1);
//     sleep_us(1);                        // LE setup time before first DCLK edge
//     for (uint i = 0; i < n_edges; i++)
//     {
//         gpio_put(dclk_pin, 1);
//         sleep_us(1);
//         gpio_put(dclk_pin, 0);
//         sleep_us(1);
//     }
//     gpio_put(le_pin, 0);
//     sleep_us(1);                        // LE hold time after last DCLK edge
// }

// static void icnd2153_startup_sequence(uint dclk_pin, uint le_pin)
// {
//     gpio_init(dclk_pin);
//     gpio_init(le_pin);
//     gpio_set_dir(dclk_pin, GPIO_OUT);
//     gpio_set_dir(le_pin, GPIO_OUT);
//     gpio_put(dclk_pin, 0);
//     gpio_put(le_pin, 0);
//     sleep_us(10);

//     icnd2153_send_command(dclk_pin, le_pin, 14);  // PRE_ACT
//     sleep_us(10);
//     icnd2153_send_command(dclk_pin, le_pin, 12);  // EN_OP
//     sleep_us(10);

//     // Leave pins as plain GPIO outputs here — configure_pio() will call
//     // pio_gpio_init() on them afterward to hand control to the PIO block
//     // for normal (DATA_LATCH-per-row) operation.
// }

// // =============================================================================
// // Free-running GCLK
// // =============================================================================
// // Datasheet: "the reference clock input pin for PWM gray scale control" —
// // feeds the chip's internal comparator, independent of DCLK/LE/CLK. A
// // hardware PWM slice is the simplest way to generate this: it runs forever
// // in hardware, with zero PIO/CPU involvement once started.

// static void icnd2153_start_gclk(uint gclk_pin, float freq_hz)
// {
//     gpio_set_function(gclk_pin, GPIO_FUNC_PWM);
//     uint slice = pwm_gpio_to_slice_num(gclk_pin);
//     uint chan  = pwm_gpio_to_channel(gclk_pin);

//     uint32_t wrap = 100;   // small wrap -> clean, jitter-free 50% duty square wave
//     float sys_clk_hz = (float)clock_get_hz(clk_sys);
//     float div = sys_clk_hz / (freq_hz * (wrap + 1));

//     pwm_config cfg = pwm_get_default_config();
//     pwm_config_set_clkdiv(&cfg, div);
//     pwm_config_set_wrap(&cfg, wrap);
//     pwm_init(slice, &cfg, true);
//     pwm_set_chan_level(slice, chan, (wrap + 1) / 2);   // 50% duty
// }

//===========

void icnd2153_init_register()
{
    // Set up GPIO
    for (auto i = 0; i < DATA_N_PINS; i++)
    {
        gpio_init(DATA_BASE_PIN + i);
        gpio_set_function(DATA_BASE_PIN + i, GPIO_FUNC_SIO);
        gpio_set_dir(DATA_BASE_PIN + i, true);
        gpio_put(DATA_BASE_PIN + i, 0);
    }

    for (auto i = 0; i < ROWSEL_N_PINS; i++)
    {
        gpio_init(ROWSEL_BASE_PIN + i);
        gpio_set_function(ROWSEL_BASE_PIN + i, GPIO_FUNC_SIO);
        gpio_set_dir(ROWSEL_BASE_PIN + i, true);
        gpio_put(ROWSEL_BASE_PIN + i, 0);
    }

    gpio_init(CLK_PIN);
    gpio_set_function(CLK_PIN, GPIO_FUNC_SIO);
    gpio_set_dir(CLK_PIN, true);
    gpio_put(CLK_PIN, LOW);

    gpio_init(STROBE_PIN);
    gpio_set_function(STROBE_PIN, GPIO_FUNC_SIO);
    gpio_set_dir(STROBE_PIN, true);
    gpio_put(CLK_PIN, LOW);

    gpio_init(OEN_PIN);
    gpio_set_function(OEN_PIN, GPIO_FUNC_SIO);
    gpio_set_dir(OEN_PIN, true);
    gpio_put(OEN_PIN, LOW);
}

static inline void pulse_clk()
{
    gpio_put(CLK_PIN, 1);
    asm volatile("nop \n nop \n nop");
    gpio_put(CLK_PIN, 0);
}

static inline void pulse_lat()
{
    gpio_put(STROBE_PIN, 1);
    asm volatile("nop \n nop \n nop");
    gpio_put(STROBE_PIN, 0);
}

static inline void shift_rgb6(uint8_t value)
{
    gpio_put(DATA_BASE_PIN, (value >> 0) & 1);
    gpio_put(DATA_BASE_PIN + 1, (value >> 1) & 1);
    gpio_put(DATA_BASE_PIN + 2, (value >> 2) & 1);

    gpio_put(DATA_BASE_PIN + 3, (value >> 3) & 1);
    gpio_put(DATA_BASE_PIN + 4, (value >> 4) & 1);
    gpio_put(DATA_BASE_PIN + 5, (value >> 5) & 1);

    pulse_clk();
}

static void icnd2153_write_register(uint16_t reg)
{
    gpio_put(OEN_PIN, 1);

    // Shift 16 bits MSB first
    for (int i = 15; i >= 0; --i)
    {
        const uint8_t bit = (reg >> i) & 1;

        // replicate bit to all RGB channels
        uint8_t rgb =
            (bit << 0) |
            (bit << 1) |
            (bit << 2) |
            (bit << 3) |
            (bit << 4) |
            (bit << 5);

        shift_rgb6(rgb);
    }

    // ICND2153 register latch
    // Many panels expect LAT high during several CLK cycles.

    gpio_put(STROBE_PIN, 1);

    for (int i = 0; i < 4; ++i)
    {
        pulse_clk();
    }

    gpio_put(STROBE_PIN, 0);
}

void icnd2153_init()
{
    icnd2153_init_register();

    // Disable output during init
    gpio_put(OEN_PIN, 1);

    gpio_put(CLK_PIN, 0);
    gpio_put(STROBE_PIN, 0);

    // Clear RGB outputs
    gpio_put(DATA_BASE_PIN, 0);
    gpio_put(DATA_BASE_PIN + 1, 0);
    gpio_put(DATA_BASE_PIN + 2, 0);

    gpio_put(DATA_BASE_PIN + 3, 0);
    gpio_put(DATA_BASE_PIN + 4, 0);
    gpio_put(DATA_BASE_PIN + 5, 0);

    // Reset row address lines
    gpio_put(ROWSEL_BASE_PIN, 0);
    gpio_put(ROWSEL_BASE_PIN + 1, 0);
    gpio_put(ROWSEL_BASE_PIN + 2, 0);
    gpio_put(ROWSEL_BASE_PIN + 3, 0);
    gpio_put(ROWSEL_BASE_PIN + 4, 0);

    sleep_ms(10);

    // Flush shift registers
    // This removes random startup garbage.

    for (int i = 0; i < 512; ++i)
    {
        pulse_clk();
    }

    pulse_lat();

    sleep_ms(1);

    // Optional conservative configuration writes.
    // These values are intentionally minimal and safe.
    // Many panels will ignore them harmlessly.

    icnd2153_write_register(0xffff);
    icnd2153_write_register(0xffff);

    sleep_ms(1);

    // Clear panel contents completely.
    // Important for some 4L panels.

    for (int row = 0; row < 16; ++row)
    {
        gpio_put(ROWSEL_BASE_PIN, (row >> 0) & 1);
        gpio_put(ROWSEL_BASE_PIN + 1, (row >> 1) & 1);
        gpio_put(ROWSEL_BASE_PIN + 2, (row >> 2) & 1);
        gpio_put(ROWSEL_BASE_PIN + 3, (row >> 3) & 1);
        gpio_put(ROWSEL_BASE_PIN + 4, (row >> 4) & 1);

        for (int i = 0; i < 256; ++i)
        {
            shift_rgb6(0x00);
        }

        pulse_lat();
    }

    gpio_put(OEN_PIN, 0);
}
#endif