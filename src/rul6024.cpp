#include <cstdlib>

#include "pico/stdlib.h"
#include "hardware/pio.h"

#include "hub75.hpp"
#include "hub75.pio.h"

#include "rul6024.h"

static uint32_t register_dma_buffer[256];

// -----------------------------------------------------------------------------
// Expand one 16-bit register value into sixteen 6-bit DMA values.
//
// Bit15 is transmitted first.
// Each output byte contains either
//
//      0x3f  -> all six data outputs HIGH
//      0x00  -> all six data outputs LOW
// -----------------------------------------------------------------------------

static constexpr uint8_t RUL6024_DATA_HIGH = 0x3f;
static constexpr uint8_t RUL6024_DATA_LOW = 0x00;
static uint16_t init_count = 0;
static void prepare_register_dma(uint16_t value, uint32_t *dst)
{
    int r = HUB75::DISPLAY_WIDTH / 16;
    for (int j = 0; j < r; ++j)
    {
        // for (int bit = 0; bit < 16; ++bit)
        for (int bit = 15; bit >= 0; --bit)
        {
            uint32_t word = (value & (1u << bit)) ? RUL6024_DATA_HIGH : RUL6024_DATA_LOW;
            *dst++ = word;
            init_count++;
        }
    }
    printf("prepare_register_dma init_count=%d\n", init_count);
}

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
void rul6024_setup(PIO pio, uint sm, uint offset)
{
    prepare_register_dma(WREG1, &register_dma_buffer[0]);
    prepare_register_dma(WREG2, &register_dma_buffer[64]);

    prepare_register_dma(0xFD00, &register_dma_buffer[128]);

    rul6024_write_register_program_init(pio, sm, offset, DATA_BASE_PIN, CLK_PIN);

    rul6024_write_register(pio, sm, HUB75::DISPLAY_WIDTH, 4, &register_dma_buffer[128]);  // I do not know why this should work ?
    rul6024_write_register(pio, sm, HUB75::DISPLAY_WIDTH, 5, &register_dma_buffer[128]); // I do not know why this should work ?
    rul6024_write_register(pio, sm, HUB75::DISPLAY_WIDTH, 6, &register_dma_buffer[128]);  // I do not know why this should work ?
    rul6024_write_register(pio, sm, HUB75::DISPLAY_WIDTH, 7, &register_dma_buffer[128]); // I do not know why this should work ?
    rul6024_write_register(pio, sm, HUB75::DISPLAY_WIDTH, 8, &register_dma_buffer[128]);  // I do not know why this should work ?
    rul6024_write_register(pio, sm, HUB75::DISPLAY_WIDTH, 9, &register_dma_buffer[128]); // I do not know why this should work ?
    rul6024_write_register(pio, sm, HUB75::DISPLAY_WIDTH, 10, &register_dma_buffer[128]);  // I do not know why this should work ?

    rul6024_write_register(pio, sm, HUB75::DISPLAY_WIDTH, CMD_WREG1, &register_dma_buffer[0]);

    rul6024_write_register(pio, sm, HUB75::DISPLAY_WIDTH, CMD_WREG2, &register_dma_buffer[64]);
}

void rul6024_initialize()
{
    uint sm;
    PIO pio;
    uint offset;

    int gpio_pins[] = {
        DATA_BASE_PIN,
        DATA_BASE_PIN + 5, // 6 RGB pins
        CLK_PIN,
        STROBE_PIN,
        OEN_PIN};
    int n = sizeof(gpio_pins) / sizeof(gpio_pins[0]);

    // Find the smallest element in the array
    int min_gpio = *std::min_element(gpio_pins, gpio_pins + n);
    // Find the largest element in the array
    int max_gpio = *std::max_element(gpio_pins, gpio_pins + n);

    // On RP2350B, GPIO 30-47 are only accessible via PIO2
    // Force both state machines onto PIO2
    if (!pio_claim_free_sm_and_add_program_for_gpio_range(
            &rul6024_write_register_program,
            &pio,
            &sm,
            &offset,
            min_gpio,
            // This parameter needs to know the lowest and highest GPIO number actually used by the state machine
            // across all its pin groups: out, set, in, and side-set, so it can pick/configure a PIO instance whose window covers both ends.
            max_gpio - min_gpio + 1,
            true))
    {
        panic("Failed to claim PIO SM for hub75_bitplane_stream_program\n");
    }

    if (sm < 0)
    {
        printf("rul6024_initialize: No free SM on this PIO instance!\n");
        return;
    };

    rul6024_setup(pio, sm, offset);
    pio_sm_set_enabled(pio, sm, false);

    pio_remove_program(pio, &rul6024_write_register_program, offset);
    pio_sm_unclaim(pio, sm);
}