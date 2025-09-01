#include "pico/stdlib.h"

#include "hardware/dma.h"
#include "hardware/pio.h"

#include "hub75.hpp"
#include "hub75.pio.h"

// Wiring of the HUB75 matrix
#define DATA_BASE_PIN 0
#define DATA_N_PINS 6
#define ROWSEL_BASE_PIN 6
#define ROWSEL_N_PINS 5
#define CLK_PIN 11
#define STROBE_PIN 12
#define OEN_PIN 13

#define EXIT_FAILURE 1

#define BIT_DEPTH 10 ///< Number of bit planes

// This gamma table is used to correct 8-bit (0-255) colours up to 10-bit, applying gamma correction without losing dynamic range.
// The gamma table is from pimeroni's https://github.com/pimoroni/pimoroni-pico/tree/main/drivers/hub75.

static const uint16_t gamma_lut[256] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
    32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
    48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
    64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79,
    80, 82, 84, 87, 89, 91, 94, 96, 98, 101, 103, 106, 109, 111, 114, 117,
    119, 122, 125, 128, 130, 133, 136, 139, 142, 145, 148, 151, 155, 158, 161, 164,
    167, 171, 174, 177, 181, 184, 188, 191, 195, 198, 202, 206, 209, 213, 217, 221,
    225, 228, 232, 236, 240, 244, 248, 252, 257, 261, 265, 269, 274, 278, 282, 287,
    291, 295, 300, 304, 309, 314, 318, 323, 328, 333, 337, 342, 347, 352, 357, 362,
    367, 372, 377, 382, 387, 393, 398, 403, 408, 414, 419, 425, 430, 436, 441, 447,
    452, 458, 464, 470, 475, 481, 487, 493, 499, 505, 511, 517, 523, 529, 535, 542,
    548, 554, 561, 567, 573, 580, 586, 593, 599, 606, 613, 619, 626, 633, 640, 647,
    653, 660, 667, 674, 681, 689, 696, 703, 710, 717, 725, 732, 739, 747, 754, 762,
    769, 777, 784, 792, 800, 807, 815, 823, 831, 839, 847, 855, 863, 871, 879, 887,
    895, 903, 912, 920, 928, 937, 945, 954, 962, 971, 979, 988, 997, 1005, 1014, 1023};

// Frame buffer for the HUB75 matrix - memory area where pixel data is stored
volatile uint32_t *frame_buffer; ///< Interwoven image data for examples;

// Utility function to claim a DMA channel and panic() if there are none left
static int claim_dma_channel(const char *channel_name);

static void configure_dma_channels();
static void configure_pio(bool);
static void setup_dma_transfers();
static void setup_dma_irq();

// Dummy pixel data emitted at the end of each row to ensure the last genuine pixels of a row are displayed - keep volatile!
static volatile uint32_t dummy_pixel_data[8] = {0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0};
// Control data for the output enable signal - keep volatile!
static volatile uint32_t oen_finished_data = 0;

const bool clk_polarity = 1;
const bool stb_polarity = 1;

// Width and height of the HUB75 LED matrix
static uint width;
static uint height;
static uint offset;

// DMA channel numbers
int pixel_chan;
int dummy_pixel_chan;
int oen_chan;

// DMA channel that becomes active when output enable (OEn) has finished.
// This channel's interrupt handler restarts the pixel data DMA channel.
int oen_finished_chan;

// PIO configuration structure for state machine numbers and corresponding program offsets
typedef struct
{
    uint sm_data;
    PIO data_pio;
    uint data_prog_offs;
    uint sm_row;
    PIO row_pio;
    uint row_prog_offs;
} PioConfig;

static PioConfig pio_config;

// Variables for row addressing and bit plane selection
static volatile uint32_t row_address = 0;
static volatile uint32_t bit_plane = 0;
static volatile uint32_t row_in_bit_plane = 0;

/**
 * @brief Interrupt handler for the Output Enable (OEn) finished event.
 *
 * This interrupt is triggered when the output enable DMA transaction is completed.
 * It updates row addressing and bit-plane selection for the next frame,
 * modifies the PIO state machine instruction, and restarts DMA transfers
 * for pixel data to ensure continuous frame updates.
 */
static void oen_finished_handler()
{
    // Clear the interrupt request for the finished DMA channel
    dma_hw->ints0 = 1u << oen_finished_chan;

    // Advance row addressing; reset and increment bit-plane if needed
    if (++row_address >= (height >> 1))
    {
        row_address = 0;

        if (++bit_plane >= BIT_DEPTH)
        {
            bit_plane = 0;
        }
        // Patch the PIO program to make it shift to the next bit plane
        hub75_data_rgb888_set_shift(pio_config.data_pio, pio_config.sm_data, pio_config.data_prog_offs, bit_plane);
    }

    // Compute address and length of OEn pulse for next row
    row_in_bit_plane = row_address | ((6u << bit_plane) << 5);
    dma_channel_set_read_addr(oen_chan, &row_in_bit_plane, false);

    // Restart DMA channels for the next row's data transfer
    dma_channel_set_write_addr(oen_finished_chan, &oen_finished_data, true);
    dma_channel_set_read_addr(pixel_chan, &frame_buffer[row_address * (width << 1)], true);
}

/**
 * @brief Starts the DMA transfers for the HUB75 display driver.
 *
 * This function initializes the DMA transfers by setting up the write address
 * for the Output Enable finished DMA channel and the read address for pixel data.
 * It ensures that the display begins processing frames.
 */
void start_hub75_driver()
{
    dma_channel_set_write_addr(oen_finished_chan, &oen_finished_data, true);
    dma_channel_set_read_addr(pixel_chan, &frame_buffer[row_address * (width << 1)], true);
}

void FM6126A_write_register(uint16_t value, uint8_t position)
{
    gpio_put(CLK_PIN, !clk_polarity);
    gpio_put(STROBE_PIN, !stb_polarity);

    uint8_t threshold = width - position;
    for (auto i = 0u; i < width; i++)
    {
        auto j = i % 16;
        bool b = value & (1 << j);

        gpio_put(DATA_BASE_PIN, b);
        gpio_put((DATA_BASE_PIN + 1), b);
        gpio_put((DATA_BASE_PIN + 2), b);
        gpio_put((DATA_BASE_PIN + 3), b);
        gpio_put((DATA_BASE_PIN + 4), b);
        gpio_put((DATA_BASE_PIN + 5), b);

        // Assert strobe/latch if i > threshold
        // This somehow indicates to the FM6126A which register we want to write :|
        gpio_put(STROBE_PIN, i > threshold);
        gpio_put(CLK_PIN, clk_polarity);
        sleep_us(10);
        gpio_put(CLK_PIN, !clk_polarity);
    }
}

void FM6126A_setup()
{
    // Ridiculous register write nonsense for the FM6126A-based 64x64 matrix
    FM6126A_write_register(0b1111111111111110, 12);
    FM6126A_write_register(0b0000001000000000, 13);
}

/**
 * @brief Initializes the HUB75 display by setting up DMA and PIO subsystems.
 *
 * This function configures the necessary hardware components to drive a HUB75
 * LED matrix display. It initializes DMA channels, PIO state machines, and
 * interrupt handlers.
 *
 * @param w Width of the HUB75 display in pixels.
 * @param h Height of the HUB75 display in pixels.
 */
void create_hub75_driver(uint w, uint h, PanelType pt, bool inverted_stb)
{
    width = w;
    height = h;
    offset = width * (height >> 1);

    frame_buffer = new uint32_t[width * height](); // Allocate memory for frame buffer and zero-initialize

    if (pt == PANEL_FM6126A)
    {
        FM6126A_setup();
    }

    configure_pio(inverted_stb);
    configure_dma_channels();
    setup_dma_transfers();
    setup_dma_irq();
}

/**
 * @brief Configures the PIO state machines for HUB75 matrix control.
 *
 * This function sets up the PIO state machines responsible for shifting
 * pixel data and controlling row addressing. If a PIO state machine cannot
 * be claimed, it prints an error message.
 */
static void configure_pio(bool inverted_stb)
{
    if (!pio_claim_free_sm_and_add_program(&hub75_data_rgb888_program, &pio_config.data_pio, &pio_config.sm_data, &pio_config.data_prog_offs))
    {
        fprintf(stderr, "Failed to claim PIO state machine for hub75_data_rgb888_program\n");
    }

    if (inverted_stb)
    {
        if (!pio_claim_free_sm_and_add_program(&hub75_row_inverted_program, &pio_config.row_pio, &pio_config.sm_row, &pio_config.row_prog_offs))
        {
            fprintf(stderr, "Failed to claim PIO state machine for hub75_row_inverted_program\n");
        }
    }
    else
    {
        if (!pio_claim_free_sm_and_add_program(&hub75_row_program, &pio_config.row_pio, &pio_config.sm_row, &pio_config.row_prog_offs))
        {
            fprintf(stderr, "Failed to claim PIO state machine for hub75_row_program\n");
        }
    }

    hub75_data_rgb888_program_init(pio_config.data_pio, pio_config.sm_data, pio_config.data_prog_offs, DATA_BASE_PIN, CLK_PIN);
    hub75_row_program_init(pio_config.row_pio, pio_config.sm_row, pio_config.row_prog_offs, ROWSEL_BASE_PIN, ROWSEL_N_PINS, STROBE_PIN);
}

/**
 * @brief Configures and claims DMA channels for HUB75 control.
 *
 * This function assigns DMA channels to handle pixel data transfer,
 * dummy pixel data, output enable signal, and output enable completion.
 * If a DMA channel cannot be claimed, the function prints an error message and exits.
 */
static void configure_dma_channels()
{
    pixel_chan = claim_dma_channel("pixel channel");
    dummy_pixel_chan = claim_dma_channel("dummy pixel channel");
    oen_chan = claim_dma_channel("output enable channel");
    oen_finished_chan = claim_dma_channel("output enable has finished channel");
}

/**
 * @brief Configures a DMA input channel for transferring data to a PIO state machine.
 *
 * This function sets up a DMA channel to transfer data from memory to a PIO
 * state machine. It configures transfer size, address incrementing, and DMA
 * chaining to ensure seamless operation.
 *
 * @param channel DMA channel number to configure.
 * @param transfer_count Number of data transfers per DMA transaction.
 * @param dma_size Data transfer size (8, 16, or 32-bit).
 * @param read_incr Whether the read address should increment after each transfer.
 * @param chain_to DMA channel to chain the transfer to, enabling automatic triggering.
 * @param pio PIO instance that will receive the transferred data.
 * @param sm State machine within the PIO instance that will process the data.
 */
static void dma_input_channel_setup(uint channel,
                                    uint transfer_count,
                                    enum dma_channel_transfer_size dma_size,
                                    bool read_incr,
                                    uint chain_to,
                                    PIO pio,
                                    uint sm)
{
    dma_channel_config conf = dma_channel_get_default_config(channel);
    channel_config_set_transfer_data_size(&conf, dma_size);
    channel_config_set_read_increment(&conf, read_incr);
    channel_config_set_write_increment(&conf, false);
    channel_config_set_dreq(&conf, pio_get_dreq(pio, sm, true));

    channel_config_set_chain_to(&conf, chain_to);

    dma_channel_configure(
        channel,        // Channel to be configured
        &conf,          // DMA configuration
        &pio->txf[sm],  // Write address: PIO TX FIFO
        NULL,           // Read address: set later
        transfer_count, // Number of transfers per transaction
        false           // Do not start transfer immediately
    );
}

/**
 * @brief Sets up DMA transfers for the HUB75 matrix.
 *
 * Configures multiple DMA channels to transfer pixel data, dummy pixel data,
 * and output enable signal, to the PIO state machines controlling the HUB75 matrix.
 * Also configures the DMA channel which gets active when an output enable signal has finished
 */
static void setup_dma_transfers()
{
    dma_input_channel_setup(pixel_chan, width << 1, DMA_SIZE_32, true, dummy_pixel_chan, pio_config.data_pio, pio_config.sm_data);
    dma_input_channel_setup(dummy_pixel_chan, 8, DMA_SIZE_32, false, oen_chan, pio_config.data_pio, pio_config.sm_data);
    dma_input_channel_setup(oen_chan, 1, DMA_SIZE_32, true, oen_chan, pio_config.row_pio, pio_config.sm_row);

    dma_channel_set_read_addr(dummy_pixel_chan, dummy_pixel_data, false);

    row_in_bit_plane = row_address | ((6u << bit_plane) << 5);
    dma_channel_set_read_addr(oen_chan, &row_in_bit_plane, false);

    dma_channel_config oen_finished_config = dma_channel_get_default_config(oen_finished_chan);
    channel_config_set_transfer_data_size(&oen_finished_config, DMA_SIZE_32);
    channel_config_set_read_increment(&oen_finished_config, false);
    channel_config_set_write_increment(&oen_finished_config, false);
    channel_config_set_dreq(&oen_finished_config, pio_get_dreq(pio_config.row_pio, pio_config.sm_row, false));
    dma_channel_configure(oen_finished_chan, &oen_finished_config, &oen_finished_data, &pio_config.row_pio->rxf[pio_config.sm_row], 1, false);
}

/**
 * @brief Sets up and enables the DMA interrupt handler.
 *
 * Registers the interrupt service routine (ISR) for the output enable finished DMA channel.
 * This is the channel that triggers the end of the output enable signal, which in turn
 * triggers the start of the next row's pixel data transfer.
 */
static void setup_dma_irq()
{
    irq_set_exclusive_handler(DMA_IRQ_0, oen_finished_handler);
    dma_channel_set_irq0_enabled(oen_finished_chan, true);
    irq_set_enabled(DMA_IRQ_0, true);
}

/**
 * @brief Claims an available DMA channel.
 *
 * Attempts to claim an unused DMA channel. If no channels are available,
 * prints an error message and exits the program.
 *
 * @param channel_name A descriptive name for the channel, used in error messages.
 * @return The claimed DMA channel number.
 */
static inline int claim_dma_channel(const char *channel_name)
{
    int dma_channel = dma_claim_unused_channel(true);
    if (dma_channel < 0)
    {
        fprintf(stderr, "Failed to claim DMA channel for %s\n", channel_name);
        exit(EXIT_FAILURE); // Stop execution
    }
    return dma_channel;
}

/**
 * @brief Updates the frame buffer with pixel data from the source array.
 *
 * This function takes a source array of pixel data and updates the frame buffer
 * with interleaved pixel values. The pixel values are gamma-corrected to 10 bits using a lookup table.
 *
 * @param src Pointer to the source pixel data array (RGB888 format).
 */
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
        for (int i = 0; i < width * height; i += 2)
        {
            frame_buffer[i] = gamma_lut[(src[j] & 0x0000ff) >> 0] << 20 | gamma_lut[(src[j] & 0x00ff00) >> 8] << 10 | gamma_lut[(src[j] & 0xff0000) >> 16];
            frame_buffer[i + 1] = gamma_lut[(src[j + offset] & 0x0000ff) >> 0] << 20 | gamma_lut[(src[j + offset] & 0x00ff00) >> 8] << 10 | gamma_lut[(src[j + offset] & 0xff0000) >> 16];
            j++;
        }
    }
}

/**
 * @brief Updates the frame buffer with pixel data from the source array.
 *
 * This function takes a source array of pixel data and updates the frame buffer
 * with interleaved pixel values. The pixel values are gamma-corrected to 10 bits using a lookup table.
 *
 * @param src Pointer to the source pixel data array (BGR888 format).
 */
void update_bgr(uint8_t *src)
{
    uint rgb_offset = offset * 3;
    uint k = 0;
    // Ramp up color resolution from 8 to 10 bits via gamma table look-up
    // Interweave pixels as required by Hub75 LED panel matrix
    for (int j = 0; j < width * height; j += 2)
    {
        frame_buffer[j] = gamma_lut[src[k]] << 20 | gamma_lut[src[k + 1]] << 10 | gamma_lut[src[k + 2]];
        frame_buffer[j + 1] = gamma_lut[src[rgb_offset + k]] << 20 | gamma_lut[src[rgb_offset + k + 1]] << 10 | gamma_lut[src[rgb_offset + k + 2]];
        k += 3;
    }
}