#include <cstdlib>

#include "pico/stdlib.h"

#include "hub75.hpp"
#include "fm6124dj.h"

const bool clk_polarity = 1;
const bool stb_polarity = 1;
const bool oe_polarity = 0;

void FM6124DJ_init_register()
{
    // Set up GPIO
    gpio_init(DATA_BASE_PIN);
    gpio_set_function(DATA_BASE_PIN, GPIO_FUNC_SIO);
    gpio_set_dir(DATA_BASE_PIN, true);
    gpio_put(DATA_BASE_PIN, 0);
    gpio_init((DATA_BASE_PIN + 1));
    gpio_set_function((DATA_BASE_PIN + 1), GPIO_FUNC_SIO);
    gpio_set_dir((DATA_BASE_PIN + 1), true);
    gpio_put((DATA_BASE_PIN + 1), 0);
    gpio_init((DATA_BASE_PIN + 2));
    gpio_set_function((DATA_BASE_PIN + 2), GPIO_FUNC_SIO);
    gpio_set_dir((DATA_BASE_PIN + 2), true);
    gpio_put((DATA_BASE_PIN + 2), 0);

    gpio_init((DATA_BASE_PIN + 3));
    gpio_set_function((DATA_BASE_PIN + 3), GPIO_FUNC_SIO);
    gpio_set_dir((DATA_BASE_PIN + 3), true);
    gpio_put((DATA_BASE_PIN + 3), 0);
    gpio_init((DATA_BASE_PIN + 4));
    gpio_set_function((DATA_BASE_PIN + 4), GPIO_FUNC_SIO);
    gpio_set_dir((DATA_BASE_PIN + 4), true);
    gpio_put((DATA_BASE_PIN + 4), 0);
    gpio_init((DATA_BASE_PIN + 5));
    gpio_set_function((DATA_BASE_PIN + 5), GPIO_FUNC_SIO);
    gpio_set_dir((DATA_BASE_PIN + 5), true);
    gpio_put((DATA_BASE_PIN + 5), 0);

    gpio_init(ROWSEL_BASE_PIN);
    gpio_set_function(ROWSEL_BASE_PIN, GPIO_FUNC_SIO);
    gpio_set_dir(ROWSEL_BASE_PIN, true);
    gpio_put(ROWSEL_BASE_PIN, 0);
    gpio_init((ROWSEL_BASE_PIN + 1));
    gpio_set_function((ROWSEL_BASE_PIN + 1), GPIO_FUNC_SIO);
    gpio_set_dir((ROWSEL_BASE_PIN + 1), true);
    gpio_put((ROWSEL_BASE_PIN + 1), 0);
    gpio_init((ROWSEL_BASE_PIN + 2));
    gpio_set_function((ROWSEL_BASE_PIN + 2), GPIO_FUNC_SIO);
    gpio_set_dir((ROWSEL_BASE_PIN + 2), true);
    gpio_put((ROWSEL_BASE_PIN + 2), 0);
    gpio_init((ROWSEL_BASE_PIN + 3));
    gpio_set_function((ROWSEL_BASE_PIN + 3), GPIO_FUNC_SIO);
    gpio_set_dir((ROWSEL_BASE_PIN + 3), true);
    gpio_put((ROWSEL_BASE_PIN + 3), 0);
    gpio_init((ROWSEL_BASE_PIN + 4));
    gpio_set_function((ROWSEL_BASE_PIN + 4), GPIO_FUNC_SIO);
    gpio_set_dir((ROWSEL_BASE_PIN + 4), true);
    gpio_put((ROWSEL_BASE_PIN + 4), 0);

    gpio_init(CLK_PIN);
    gpio_set_function(CLK_PIN, GPIO_FUNC_SIO);
    gpio_set_dir(CLK_PIN, true);
    gpio_put(CLK_PIN, !clk_polarity);
    gpio_init(STROBE_PIN);
    gpio_set_function(STROBE_PIN, GPIO_FUNC_SIO);
    gpio_set_dir(STROBE_PIN, true);
    gpio_put(CLK_PIN, !stb_polarity);
    gpio_init(OEN_PIN);
    gpio_set_function(OEN_PIN, GPIO_FUNC_SIO);
    gpio_set_dir(OEN_PIN, true);
    gpio_put(CLK_PIN, !oe_polarity);
}

void FM6124DJ_setup()
{
    FM6124DJ_init_register();
    
    // Step 1: assert all data pins low, clock out a full row to clear
    // shift registers
    gpio_put(STROBE_PIN, 0);
    gpio_put(OEN_PIN, 1);    // OEn deasserted (active low = panel off)

    for (int i = 0; i < MATRIX_PANEL_WIDTH; i++)
    {
        gpio_put(CLK_PIN, 0);
        // all data pins already low
        gpio_put(CLK_PIN, 1);
    }

    // Step 2: assert LATCH (STROBE) high
    gpio_put(STROBE_PIN, 1);

    // Step 3: clock out exactly the number of pulses required to address
    // the dual-latch configuration register — check FM6124DJ datasheet
    // for the exact count (FM6126A uses 11 pulses for reg1, 4 for reg2)
    constexpr int DUAL_LATCH_REG_CLOCKS = 11;
    for (int i = 0; i < DUAL_LATCH_REG_CLOCKS; i++)
    {
        gpio_put(CLK_PIN, 0);
        gpio_put(CLK_PIN, 1);
    }

    // Step 4: release LATCH
    gpio_put(STROBE_PIN, 0);

    // Step 5: clock out the configuration word that enables dual-latch mode
    // The bit pattern is chip-specific — VERIFY against FM6124DJ datasheet
    // FM6126A reg1 value that enables double-latch: 0b0111111111111110
    constexpr uint16_t DUAL_LATCH_ENABLE = 0b0111111111111110;
    for (int bit = 15; bit >= 0; bit--)
    {
        gpio_put(CLK_PIN, 0);
        // set all R/G/B data pins to the current config bit
        uint8_t val = (DUAL_LATCH_ENABLE >> bit) & 1;
        gpio_put(DATA_BASE_PIN + 0, val); // R0
        gpio_put(DATA_BASE_PIN + 1, val); // G0
        gpio_put(DATA_BASE_PIN + 2, val); // B0
        gpio_put(DATA_BASE_PIN + 3, val); // R1
        gpio_put(DATA_BASE_PIN + 4, val); // G1
        gpio_put(DATA_BASE_PIN + 5, val); // B1
        gpio_put(CLK_PIN, 1);
    }

    // Step 6: pulse LATCH to commit configuration
    gpio_put(STROBE_PIN, 1);
    gpio_put(STROBE_PIN, 0);

    // Step 7: clear shift registers again with zeros
    for (int i = 0; i < MATRIX_PANEL_WIDTH; i++)
    {
        gpio_put(CLK_PIN, 0);
        gpio_put(DATA_BASE_PIN + 0, 0);
        gpio_put(DATA_BASE_PIN + 1, 0);
        gpio_put(DATA_BASE_PIN + 2, 0);
        gpio_put(DATA_BASE_PIN + 3, 0);
        gpio_put(DATA_BASE_PIN + 4, 0);
        gpio_put(DATA_BASE_PIN + 5, 0);
        gpio_put(CLK_PIN, 1);
    }

    gpio_put(STROBE_PIN, 0);
    gpio_put(OEN_PIN, 1); // keep panel off until driver starts
}