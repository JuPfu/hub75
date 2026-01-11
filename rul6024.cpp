#include <cstdlib>

#include "pico/stdlib.h"

#include "hub75.hpp"

#include "rul6024.h"

void RUL6024_init_register()
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

void RUL6024_write_register(uint16_t value, uint8_t position)
{
    gpio_put(STROBE_PIN, LOW);
    sleep_us(10);

    uint8_t threshold = RGB_MATRIX_WIDTH - position;
    for (auto i = 0u; i < RGB_MATRIX_WIDTH; i++)
    {
        auto j = i % 16;
        bool b = value & (1 << j);

        gpio_put(CLK_PIN, LOW);
        sleep_us(10);
        gpio_put(DATA_BASE_PIN, b);
        gpio_put((DATA_BASE_PIN + 1), b);
        gpio_put((DATA_BASE_PIN + 2), b);
        gpio_put((DATA_BASE_PIN + 3), b);
        gpio_put((DATA_BASE_PIN + 4), b);
        gpio_put((DATA_BASE_PIN + 5), b);

        // Assert strobe/latch if i > threshold
        // This somehow indicates to the FM6126A which register we want to write :|
        gpio_put(STROBE_PIN, i > threshold);
        sleep_us(10);
        gpio_put(CLK_PIN, HIGH);
        sleep_us(10);
    }
}

void RUL6024_write_command(uint8_t command)
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

        gpio_put(OEN_PIN, HIGH);
        sleep_us(10);
        gpio_put(CLK_PIN, LOW);
        gpio_put(STROBE_PIN, LOW); // clk    --_--
        sleep_us(10);              // LE     _____
                                   // OE     ---__
        gpio_put(CLK_PIN, HIGH);
        sleep_us(10);

        gpio_put(CLK_PIN, LOW);
        sleep_us(10);
        gpio_put(STROBE_PIN, HIGH);
        sleep_us(10);
        // gpio_put(OEN_PIN, LOW);
        // sleep_us(10);

        gpio_put(CLK_PIN, HIGH);
        sleep_us(10);

        gpio_put(STROBE_PIN, LOW);
        gpio_put(CLK_PIN, LOW);
        sleep_us(10);

        // gpio_put(OEN_PIN, HIGH);
        // sleep_us(10);

        gpio_put(CLK_PIN, HIGH);
        sleep_us(10);
        gpio_put(CLK_PIN, LOW);
        sleep_us(10);
        gpio_put(OEN_PIN, LOW);
        sleep_us(10);

        gpio_put(CLK_PIN, HIGH);
        sleep_us(10);

        gpio_put(CLK_PIN, LOW);
        gpio_put(STROBE_PIN, HIGH);
        sleep_us(10);
        gpio_put(OEN_PIN, HIGH);

        // LE set to high for 2 clock cycle
        gpio_put(CLK_PIN, HIGH);
        sleep_us(10);
        gpio_put(CLK_PIN, LOW);
        sleep_us(10);
        gpio_put(CLK_PIN, HIGH);
        sleep_us(10);
        gpio_put(STROBE_PIN, LOW);
        gpio_put(CLK_PIN, LOW);
        sleep_us(10);
        break;
    case CMD_DATA_LATCH:
        gpio_put(CLK_PIN, LOW);
        sleep_us(10);
        gpio_put(CLK_PIN, HIGH);
        sleep_us(10);
        gpio_put(STROBE_PIN, HIGH);
        sleep_us(10);
        gpio_put(CLK_PIN, LOW);
        sleep_us(10);
        gpio_put(CLK_PIN, HIGH);
        sleep_us(10);
        gpio_put(CLK_PIN, LOW);
        sleep_us(10);
        gpio_put(CLK_PIN, HIGH);
        sleep_us(10);
        gpio_put(CLK_PIN, LOW);
        sleep_us(10);
        gpio_put(CLK_PIN, HIGH);
        sleep_us(10);
        gpio_put(CLK_PIN, LOW);
        sleep_us(10);
        gpio_put(STROBE_PIN, LOW);
        sleep_us(10);
        gpio_put(OEN_PIN, LOW);
        break;
    case CMD_WREG1:
        gpio_put(CLK_PIN, LOW);
        gpio_put(STROBE_PIN, LOW);
        gpio_put(OEN_PIN, HIGH);
        sleep_us(10);

        for (auto i = 0; i <= CMD_WREG1; i++)
        {
            gpio_put(CLK_PIN, HIGH);
            sleep_us(10);
            if (i == 0)
            {
                gpio_put(STROBE_PIN, HIGH);
                sleep_us(10);
            }
            gpio_put(CLK_PIN, LOW);
            sleep_us(10);
        }

        RUL6024_write_register(WREG1, 12);

        gpio_put(OEN_PIN, LOW);
        sleep_us(10);

        break;
    case CMD_WREG2:
        gpio_put(OEN_PIN, HIGH);
        gpio_put(CLK_PIN, LOW);
        gpio_put(STROBE_PIN, LOW);
        sleep_us(10);

        for (auto i = 0; i <= CMD_WREG2; i++)
        {
            gpio_put(CLK_PIN, HIGH);
            sleep_us(10);
            if (i == 0)
            {
                gpio_put(STROBE_PIN, HIGH);
                sleep_us(10);
            }
            gpio_put(CLK_PIN, LOW);
            sleep_us(10);
        }

        RUL6024_write_register(WREG2, 12);

        gpio_put(OEN_PIN, LOW);
        sleep_us(10);
        break;
    }
}

void RUL6024_setup()
{
    RUL6024_init_register();

    RUL6024_write_command(CMD_WREG1);
    RUL6024_write_command(CMD_WREG2);

    RUL6024_write_command(CMD_DATA_LATCH);
    // RESET_OEN is required after writing WREG2
    RUL6024_write_command(CMD_RESET_OEN);
}