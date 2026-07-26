#include <cstdlib>

#include "pico/stdlib.h"

#include "hub75.hpp"
#include "fm6126a.h"

const bool clk_polarity = 1;
const bool stb_polarity = 1;
const bool oe_polarity = 0;

void FM6126A_init_register(const Hub75PinConfig &pins)
{
    // Set up GPIO
    gpio_init(pins.data_base_pin);
    gpio_set_function(pins.data_base_pin, GPIO_FUNC_SIO);
    gpio_set_dir(pins.data_base_pin, true);
    gpio_put(pins.data_base_pin, 0);
    gpio_init((pins.data_base_pin + 1));
    gpio_set_function((pins.data_base_pin + 1), GPIO_FUNC_SIO);
    gpio_set_dir((pins.data_base_pin + 1), true);
    gpio_put((pins.data_base_pin + 1), 0);
    gpio_init((pins.data_base_pin + 2));
    gpio_set_function((pins.data_base_pin + 2), GPIO_FUNC_SIO);
    gpio_set_dir((pins.data_base_pin + 2), true);
    gpio_put((pins.data_base_pin + 2), 0);

    gpio_init((pins.data_base_pin + 3));
    gpio_set_function((pins.data_base_pin + 3), GPIO_FUNC_SIO);
    gpio_set_dir((pins.data_base_pin + 3), true);
    gpio_put((pins.data_base_pin + 3), 0);
    gpio_init((pins.data_base_pin + 4));
    gpio_set_function((pins.data_base_pin + 4), GPIO_FUNC_SIO);
    gpio_set_dir((pins.data_base_pin + 4), true);
    gpio_put((pins.data_base_pin + 4), 0);
    gpio_init((pins.data_base_pin + 5));
    gpio_set_function((pins.data_base_pin + 5), GPIO_FUNC_SIO);
    gpio_set_dir((pins.data_base_pin + 5), true);
    gpio_put((pins.data_base_pin + 5), 0);

    gpio_init(pins.rowsel_base_pin);
    gpio_set_function(pins.rowsel_base_pin, GPIO_FUNC_SIO);
    gpio_set_dir(pins.rowsel_base_pin, true);
    gpio_put(pins.rowsel_base_pin, 0);
    gpio_init((pins.rowsel_base_pin + 1));
    gpio_set_function((pins.rowsel_base_pin + 1), GPIO_FUNC_SIO);
    gpio_set_dir((pins.rowsel_base_pin + 1), true);
    gpio_put((pins.rowsel_base_pin + 1), 0);
    gpio_init((pins.rowsel_base_pin + 2));
    gpio_set_function((pins.rowsel_base_pin + 2), GPIO_FUNC_SIO);
    gpio_set_dir((pins.rowsel_base_pin + 2), true);
    gpio_put((pins.rowsel_base_pin + 2), 0);
    gpio_init((pins.rowsel_base_pin + 3));
    gpio_set_function((pins.rowsel_base_pin + 3), GPIO_FUNC_SIO);
    gpio_set_dir((pins.rowsel_base_pin + 3), true);
    gpio_put((pins.rowsel_base_pin + 3), 0);
    gpio_init((pins.rowsel_base_pin + 4));
    gpio_set_function((pins.rowsel_base_pin + 4), GPIO_FUNC_SIO);
    gpio_set_dir((pins.rowsel_base_pin + 4), true);
    gpio_put((pins.rowsel_base_pin + 4), 0);

    gpio_init(pins.clk_pin);
    gpio_set_function(pins.clk_pin, GPIO_FUNC_SIO);
    gpio_set_dir(pins.clk_pin, true);
    gpio_put(pins.clk_pin, !clk_polarity);
    gpio_init(pins.strobe_pin);
    gpio_set_function(pins.strobe_pin, GPIO_FUNC_SIO);
    gpio_set_dir(pins.strobe_pin, true);
    gpio_put(pins.clk_pin, !stb_polarity);
    gpio_init(pins.oen_pin);
    gpio_set_function(pins.oen_pin, GPIO_FUNC_SIO);
    gpio_set_dir(pins.oen_pin, true);
    gpio_put(pins.clk_pin, !oe_polarity);
}

void FM6126A_write_register(const Hub75PinConfig &pins, uint32_t matrix_panel_width, uint16_t value, uint8_t position)
{
    gpio_put(pins.oen_pin, HIGH);
    gpio_put(pins.clk_pin, LOW);
    gpio_put(pins.strobe_pin, LOW);

    sleep_ms(10);

    uint8_t threshold = matrix_panel_width - position;
    for (auto i = 0u; i < matrix_panel_width; i++)
    {
        auto j = i % 16;
        bool b = value & (1 << j);

        gpio_put(pins.data_base_pin, b);
        gpio_put((pins.data_base_pin + 1), b);
        gpio_put((pins.data_base_pin + 2), b);
        gpio_put((pins.data_base_pin + 3), b);
        gpio_put((pins.data_base_pin + 4), b);
        gpio_put((pins.data_base_pin + 5), b);

        // Assert strobe/latch if i > threshold
        // This somehow indicates to the FM6126A which register we want to write :|
        gpio_put(pins.strobe_pin, i > threshold);
        gpio_put(pins.clk_pin, HIGH);
        sleep_ms(10);
        gpio_put(pins.clk_pin, LOW);
    }
    gpio_put(pins.oen_pin, LOW);
}

/**
 * @brief Generate initialisation sequence for FM6126A based led matrix panels.
 *
 * First initialise all GPIOs connected to the led matrix panel.
 * Second send the initialisation sequence to the FM6126A based led matrix panel.
 * The source code is based on Pimoronis Hub75 driver, see https://github.com/pimoroni/pimoroni-pico/blob/main/drivers/hub75/hub75.cpp
 *
 */
void FM6126A_setup(const Hub75PinConfig &pins, uint32_t matrix_panel_width)
{
    FM6126A_init_register(pins);

    // Ridiculous register write nonsense for the FM6126A-based 64x64 matrix
    FM6126A_write_register(pins, matrix_panel_width, 0b1111111111111110, 12);
    FM6126A_write_register(pins, matrix_panel_width, 0b0000010000000000, 13);
}
