#include <cstdlib>

#include "pico/stdlib.h"

#include "hub75.hpp"

#include "rul6024.h"

void RUL6024_init_register(const Hub75PinConfig &pins)
{
    // Set up GPIO
    for (auto i = 0u; i < pins.data_n_pins; i++)
    {
        gpio_init(pins.data_base_pin + i);
        gpio_set_function(pins.data_base_pin + i, GPIO_FUNC_SIO);
        gpio_set_dir(pins.data_base_pin + i, true);
        gpio_put(pins.data_base_pin + i, 0);
    }

    for (auto i = 0u; i < pins.rowsel_n_pins; i++)
    {
        gpio_init(pins.rowsel_base_pin + i);
        gpio_set_function(pins.rowsel_base_pin + i, GPIO_FUNC_SIO);
        gpio_set_dir(pins.rowsel_base_pin + i, true);
        gpio_put(pins.rowsel_base_pin + i, 0);
    }

    gpio_init(pins.clk_pin);
    gpio_set_function(pins.clk_pin, GPIO_FUNC_SIO);
    gpio_set_dir(pins.clk_pin, true);
    gpio_put(pins.clk_pin, LOW);

    gpio_init(pins.strobe_pin);
    gpio_set_function(pins.strobe_pin, GPIO_FUNC_SIO);
    gpio_set_dir(pins.strobe_pin, true);
    gpio_put(pins.clk_pin, LOW);

    gpio_init(pins.oen_pin);
    gpio_set_function(pins.oen_pin, GPIO_FUNC_SIO);
    gpio_set_dir(pins.oen_pin, true);
    gpio_put(pins.oen_pin, LOW);
}

void RUL6024_write_register(const Hub75PinConfig &pins, uint32_t matrix_panel_width, uint16_t value, uint8_t position)
{
    gpio_put(pins.strobe_pin, LOW);
    sleep_us(10);

    uint8_t threshold = matrix_panel_width - position;
    for (auto i = 0u; i < matrix_panel_width; i++)
    {
        auto j = i % 16;
        bool b = value & (1 << j);

        gpio_put(pins.clk_pin, LOW);
        sleep_us(10);
        gpio_put(pins.data_base_pin, b);
        gpio_put((pins.data_base_pin + 1), b);
        gpio_put((pins.data_base_pin + 2), b);
        gpio_put((pins.data_base_pin + 3), b);
        gpio_put((pins.data_base_pin + 4), b);
        gpio_put((pins.data_base_pin + 5), b);

        // Assert strobe/latch if i > threshold
        // This somehow indicates to the FM6126A which register we want to write :|
        gpio_put(pins.strobe_pin, i > threshold);
        sleep_us(10);
        gpio_put(pins.clk_pin, HIGH);
        sleep_us(10);
    }
}

void RUL6024_write_command(const Hub75PinConfig &pins, uint32_t matrix_panel_width, uint8_t command)
{
    // The chip contains a simple 16-bit shift register. The grayscale value and configuration
    // value are latched into the shift register (the data transmitted to the chip first is the high bit
    // of the register). The control command is parsed by counting the length of the LE signal.
    // Different LE lengths represent different commands. For example, a LE signal with a
    // length of 3 represents the "Data_Latch" command, which is used to control the shift
    // register to latch the value and send the 16-bit data in the shift register to the
    // output channel. The following table lists all the commands and their meanings.
    //
    // Command Name    LE length     Command Description
    //
    // RESET_OEN       1 & 2         The reset signal of the time-sharing display function is 1 LE width first, followed by 2 LE widths.
    // DATA_LATCH      3             Latch 16 bit data and send it to output channel
    // Reserved        4 to 10       Reserved
    // WR_REG1         11            Write configuration register 1
    // WR_REG2         12            Write configuration register 2

    switch (command)
    {
    case CMD_RESET_OEN:
        // The reset signal of the time-sharing display function is 1 LE width first, followed by 2 LE widths.

        gpio_put(pins.oen_pin, HIGH);
        sleep_us(10);
        gpio_put(pins.clk_pin, LOW);
        gpio_put(pins.strobe_pin, LOW); // clk    --_--
        sleep_us(10);                  // LE     _____
                                        // OE     ---__
        gpio_put(pins.clk_pin, HIGH);
        sleep_us(10);

        gpio_put(pins.clk_pin, LOW);
        sleep_us(10);
        gpio_put(pins.strobe_pin, HIGH);
        sleep_us(10);
        // gpio_put(pins.oen_pin, LOW);
        // sleep_us(10);

        gpio_put(pins.clk_pin, HIGH);
        sleep_us(10);

        gpio_put(pins.strobe_pin, LOW);
        gpio_put(pins.clk_pin, LOW);
        sleep_us(10);

        // gpio_put(pins.oen_pin, HIGH);
        // sleep_us(10);

        gpio_put(pins.clk_pin, HIGH);
        sleep_us(10);
        gpio_put(pins.clk_pin, LOW);
        sleep_us(10);
        gpio_put(pins.oen_pin, LOW);
        sleep_us(10);

        gpio_put(pins.clk_pin, HIGH);
        sleep_us(10);

        gpio_put(pins.clk_pin, LOW);
        gpio_put(pins.strobe_pin, HIGH);
        sleep_us(10);
        gpio_put(pins.oen_pin, HIGH);

        // LE set to high for 2 clock cycle
        gpio_put(pins.clk_pin, HIGH);
        sleep_us(10);
        gpio_put(pins.clk_pin, LOW);
        sleep_us(10);
        gpio_put(pins.clk_pin, HIGH);
        sleep_us(10);
        gpio_put(pins.strobe_pin, LOW);
        gpio_put(pins.clk_pin, LOW);
        sleep_us(10);
        break;
    case CMD_DATA_LATCH:
        gpio_put(pins.clk_pin, LOW);
        sleep_us(10);
        gpio_put(pins.clk_pin, HIGH);
        sleep_us(10);
        gpio_put(pins.strobe_pin, HIGH);
        sleep_us(10);
        gpio_put(pins.clk_pin, LOW);
        sleep_us(10);
        gpio_put(pins.clk_pin, HIGH);
        sleep_us(10);
        gpio_put(pins.clk_pin, LOW);
        sleep_us(10);
        gpio_put(pins.clk_pin, HIGH);
        sleep_us(10);
        gpio_put(pins.clk_pin, LOW);
        sleep_us(10);
        gpio_put(pins.clk_pin, HIGH);
        sleep_us(10);
        gpio_put(pins.clk_pin, LOW);
        sleep_us(10);
        gpio_put(pins.strobe_pin, LOW);
        sleep_us(10);
        gpio_put(pins.oen_pin, LOW);
        break;
    case CMD_WREG1:
        gpio_put(pins.clk_pin, LOW);
        gpio_put(pins.strobe_pin, LOW);
        gpio_put(pins.oen_pin, HIGH);
        sleep_us(10);

        for (auto i = 0; i <= CMD_WREG1; i++)
        {
            gpio_put(pins.clk_pin, HIGH);
            sleep_us(10);
            if (i == 0)
            {
                gpio_put(pins.strobe_pin, HIGH);
                sleep_us(10);
            }
            gpio_put(pins.clk_pin, LOW);
            sleep_us(10);
        }

        RUL6024_write_register(pins, matrix_panel_width, WREG1, 12);

        gpio_put(pins.oen_pin, LOW);
        sleep_us(10);

        break;
    case CMD_WREG2:
        gpio_put(pins.oen_pin, HIGH);
        gpio_put(pins.clk_pin, LOW);
        gpio_put(pins.strobe_pin, LOW);
        sleep_us(10);

        for (auto i = 0; i <= CMD_WREG2; i++)
        {
            gpio_put(pins.clk_pin, HIGH);
            sleep_us(10);
            if (i == 0)
            {
                gpio_put(pins.strobe_pin, HIGH);
                sleep_us(10);
            }
            gpio_put(pins.clk_pin, LOW);
            sleep_us(10);
        }

        RUL6024_write_register(pins, matrix_panel_width, WREG2, 12);

        gpio_put(pins.oen_pin, LOW);
        sleep_us(10);
        break;
    }
}

void RUL6024_setup(const Hub75PinConfig &pins, uint32_t matrix_panel_width)
{
    RUL6024_init_register(pins);

    RUL6024_write_command(pins, matrix_panel_width, CMD_WREG1);
    RUL6024_write_command(pins, matrix_panel_width, CMD_WREG2);

    RUL6024_write_command(pins, matrix_panel_width, CMD_DATA_LATCH);
    // RESET_OEN is required after writing WREG2
    RUL6024_write_command(pins, matrix_panel_width, CMD_RESET_OEN);
}
