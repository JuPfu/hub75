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

#define TEMPORAL_DITHERING // use temporal dithering - remove define to use no dithering

// Scan rate 1 : 32 for a 64x64 matrix panel means 64 pixel height divided by 32 pixel results in 2 rows lit simultaneously.
// Scan rate 1 : 16 for a 64x64 matrix panel means 64 pixel height divided by 16 pixel results in 4 rows lit simultaneously.
// Scan rate 1 : 16 for a 64x32 matrix panel means 32 pixel height divided by 16 pixel results in 2 rows lit simultaneously.
// Scan rate 1 : 8 for a 64x32 matrix panel means 32 pixel height divided by 8 pixel results in 4 rows lit simultaneously.
// ...
// Define either HUB75_MULTIPLEX_2_ROWS or HUB75_MULTIPLEX_2_ROWS to fit your matrix panel.

#define HUB75_MULTIPLEX_2_ROWS // two rows lit simultaneously
// #define HUB75_MULTIPLEX_4_ROWS   // four rows lit simultaneously

#if !defined(HUB75_MULTIPLEX_2_ROWS) && !defined(HUB75_MULTIPLEX_4_ROWS)
#error "You must define either HUB75_MULTIPLEX_2_ROWS or HUB75_MULTIPLEX_4_ROWS to match your panel's scan rate"
#endif

// Deduced from https://jared.geek.nz/2013/02/linear-led-pwm/
// The CIE 1931 lightness formula is what actually describes how we perceive light.

#ifdef TEMPORAL_DITHERING
static const uint16_t lut[256] = {
    0, 7, 14, 21, 28, 36, 43, 50, 57, 64, 71, 78, 85, 92, 100, 107,
    114, 121, 128, 135, 142, 149, 157, 164, 172, 180, 189, 197, 206, 215, 225, 234,
    244, 254, 265, 276, 287, 298, 310, 322, 334, 346, 359, 373, 386, 400, 414, 428,
    443, 458, 474, 490, 506, 522, 539, 557, 574, 592, 610, 629, 648, 668, 688, 708,
    729, 750, 771, 793, 815, 838, 861, 885, 909, 933, 958, 983, 1009, 1035, 1061, 1088,
    1116, 1144, 1172, 1201, 1230, 1260, 1290, 1321, 1353, 1384, 1417, 1449, 1482, 1516, 1550, 1585,
    1621, 1656, 1693, 1729, 1767, 1805, 1843, 1882, 1922, 1962, 2003, 2044, 2085, 2128, 2171, 2214,
    2258, 2303, 2348, 2394, 2440, 2487, 2535, 2583, 2632, 2681, 2731, 2782, 2833, 2885, 2938, 2991,
    3045, 3099, 3154, 3210, 3266, 3323, 3381, 3439, 3498, 3558, 3618, 3679, 3741, 3803, 3867, 3930,
    3995, 4060, 4126, 4193, 4260, 4328, 4397, 4466, 4536, 4607, 4679, 4752, 4825, 4899, 4973, 5049,
    5125, 5202, 5280, 5358, 5437, 5517, 5598, 5680, 5762, 5845, 5929, 6014, 6100, 6186, 6273, 6361,
    6450, 6540, 6630, 6722, 6814, 6907, 7001, 7095, 7191, 7287, 7385, 7483, 7582, 7682, 7782, 7884,
    7986, 8090, 8194, 8299, 8405, 8512, 8620, 8729, 8838, 8949, 9060, 9173, 9286, 9400, 9516, 9632,
    9749, 9867, 9986, 10106, 10227, 10348, 10471, 10595, 10720, 10845, 10972, 11100, 11228, 11358, 11489, 11620,
    11753, 11887, 12021, 12157, 12294, 12432, 12570, 12710, 12851, 12993, 13136, 13279, 13424, 13570, 13718, 13866,
    14015, 14165, 14317, 14469, 14622, 14777, 14933, 15089, 15247, 15406, 15566, 15727, 15890, 16053, 16217, 16383};
#else
static const uint16_t lut[256] = {
    0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7,
    7, 8, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 15,
    15, 16, 17, 17, 18, 19, 19, 20, 21, 22, 22, 23, 24, 25, 26, 27,
    28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 42, 43, 44,
    45, 47, 48, 50, 51, 52, 54, 55, 57, 58, 60, 61, 63, 65, 66, 68,
    70, 71, 73, 75, 77, 79, 81, 83, 84, 86, 88, 90, 93, 95, 97, 99,
    101, 103, 106, 108, 110, 113, 115, 118, 120, 123, 125, 128, 130, 133, 136, 138,
    141, 144, 147, 149, 152, 155, 158, 161, 164, 167, 171, 174, 177, 180, 183, 187,
    190, 194, 197, 200, 204, 208, 211, 215, 218, 222, 226, 230, 234, 237, 241, 245,
    249, 254, 258, 262, 266, 270, 275, 279, 283, 288, 292, 297, 301, 306, 311, 315,
    320, 325, 330, 335, 340, 345, 350, 355, 360, 365, 370, 376, 381, 386, 392, 397,
    403, 408, 414, 420, 425, 431, 437, 443, 449, 455, 461, 467, 473, 480, 486, 492,
    499, 505, 512, 518, 525, 532, 538, 545, 552, 559, 566, 573, 580, 587, 594, 601,
    609, 616, 624, 631, 639, 646, 654, 662, 669, 677, 685, 693, 701, 709, 717, 726,
    734, 742, 751, 759, 768, 776, 785, 794, 802, 811, 820, 829, 838, 847, 857, 866,
    875, 885, 894, 903, 913, 923, 932, 942, 952, 962, 972, 982, 992, 1002, 1013, 1023};
#endif

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
const bool oe_polarity = 0;

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

// Accumulator precision has to fit the lut precision.
#ifndef ACC_BITS
#define ACC_BITS 14
#endif

// Derived constants
static const int ACC_SHIFT = (ACC_BITS - 10); // number of low bits preserved in accumulator

// Per-channel accumulators (allocated at runtime)
static uint32_t *acc_r = nullptr;
static uint32_t *acc_g = nullptr;
static uint32_t *acc_b = nullptr;

// Variables for brightness control
volatile float brightness = 1.0f;    // fine control [0.0–1.0]
volatile uint32_t basis_factor = 6u; // baseline scaling

inline uint32_t set_row_in_bit_plane(uint32_t row_address, uint32_t bit_plane)
{
    return row_address | ((uint32_t)((basis_factor << bit_plane) * brightness) << ROWSEL_N_PINS);
}

/**
 * @brief Set the baseline brightness scaling factor for the panel.
 *
 * This acts as the coarse brightness control (default = 6u).
 *
 * @param factor Brightness factor (must be > 0, range 1–255).
 */
void setBasisBrightness(uint8_t factor)
{
    basis_factor = (factor > 0) ? factor : 1u;
}

/**
 * @brief Set the runtime brightness level of the panel.
 *
 * This acts as the fine brightness/intensity control, scaling within the
 * current basis brightness range.
 *
 * @param intensity Intensity value in range [0.0f, 1.0f].
 *        Values outside are clamped to the valid range.
 */
void setIntensity(float intensity)
{
    if (intensity < 0.0f)
        brightness = 0.0f;
    else if (intensity > 1.0f)
        brightness = 1.0f;
    else
        brightness = intensity;
}

/**
 * @brief Initialize per-pixel accumulators used for temporal dithering.
 *
 * This must be called after width and height are set and after the frame_buffer allocation.
 * Allocates three arrays of width*height uint32 accumulators (R, G, B) and zero-initializes them.
 */
static void init_accumulators()
{
    const size_t pixels = (size_t)width * (size_t)height;
    acc_r = new uint32_t[pixels](); // value-initialized to 0
    acc_g = new uint32_t[pixels]();
    acc_b = new uint32_t[pixels]();
}

/**
 * @brief Free per-pixel accumulator memory.
 *
 * Call this during cleanup when you free frame_buffer.
 */
static void free_accumulators()
{
    if (acc_r)
    {
        delete[] acc_r;
        acc_r = nullptr;
    }
    if (acc_g)
    {
        delete[] acc_g;
        acc_g = nullptr;
    }
    if (acc_b)
    {
        delete[] acc_b;
        acc_b = nullptr;
    }
}

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
    row_in_bit_plane = set_row_in_bit_plane(row_address, bit_plane);
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

void FM6126A_init_register()
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

/**
 * @brief Generate initialisation sequence for FM6126A based led matrix panels.
 *
 * First initialise all GPIOs connected to the led matrix panel.
 * Second send the initialisation sequence to the FM6126A based led matrix panel.
 * The source code is based on Pimoronis Hub75 driver, see https://github.com/pimoroni/pimoroni-pico/blob/main/drivers/hub75/hub75.cpp
 *
 */
void FM6126A_setup()
{
    FM6126A_init_register();

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
void create_hub75_driver(uint w, uint h, PanelType panel_type, bool inverted_stb)
{
    width = w;
    height = h;
#ifdef HUB75_MULTIPLEX_2_ROWS
    offset = width * (height >> 1);
#elif defined HUB75_MULTIPLEX_4_ROWS
    offset = width * (height >> 2);
#endif

    frame_buffer = new uint32_t[width * height](); // Allocate memory for frame buffer and zero-initialize

    init_accumulators();

    if (panel_type == PANEL_FM6126A)
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

    row_in_bit_plane = set_row_in_bit_plane(row_address, bit_plane);
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

#ifdef TEMPORAL_DITHERING
inline __attribute__((always_inline)) uint32_t temporal_dithering(size_t j, uint32_t pixel)
{
    uint8_t r = (pixel & 0x0000ff) >> 0;
    uint8_t g = (pixel & 0x00ff00) >> 8;
    uint8_t b = (pixel & 0xff0000) >> 16;

    // Add higher precision (14-bit) mapped values into accumulator
    acc_r[j] += lut[r];
    acc_g[j] += lut[g];
    acc_b[j] += lut[b];

    // Quantize down to 10-bit output
    uint32_t out_r = acc_r[j] >> ACC_SHIFT; // 10 bits
    uint32_t out_g = acc_g[j] >> ACC_SHIFT;
    uint32_t out_b = acc_b[j] >> ACC_SHIFT;

    // Subtract used portion, keep remainder for error feedback
    acc_r[j] -= (out_r << ACC_SHIFT);
    acc_g[j] -= (out_g << ACC_SHIFT);
    acc_b[j] -= (out_b << ACC_SHIFT);

    return (out_r << 20) | (out_g << 10) | out_b;
}

/**
 * @brief Update frame_buffer from PicoGraphics source (RGB888 / packed 32-bit),
 *        using accumulator temporal dithering while preserving the LUT mapping.
 *
 * The LUT (lut[]) maps 8-bit input -> 10-bit output (0..1023). We scale that
 * mapped value into the accumulator (left shift by ACC_SHIFT) and keep the
 * fractional remainder in the accumulator across frames.
 *
 * @param src Graphics object to be updated - RGB888 format, 24-bits in uint32_t array
 */
void update(
    PicoGraphics const *graphics // Graphics object to be updated - RGB888 format, 24-bits in uint32_t array
)
{
    if (graphics->pen_type == PicoGraphics::PEN_RGB888)
    {
        uint32_t const *src = static_cast<uint32_t const *>(graphics->frame_buffer);

        const size_t pixels = width * height;
#ifdef HUB75_MULTIPLEX_2_ROWS
        for (size_t i = 0, j = 0; i < pixels; i += 2, ++j)
        {
            frame_buffer[i] = temporal_dithering(j, src[j]);
            frame_buffer[i + 1] = temporal_dithering(j + offset, src[j + offset]);
        }
#elif defined HUB75_MULTIPLEX_4_ROWS
        // For four-rows-lit multiplexing we step by 4 and use offsets 0, offset, 2*offset, 3*offset
        for (size_t i = 0, j = 0; i < pixels; i += 4, ++j)
        {
            size_t j0 = j;
            size_t j1 = j0 + offset;
            size_t j2 = j1 + offset;
            size_t j3 = j2 + offset;

            frame_buffer[i] = temporal_dithering(j0, src[j0]);
            frame_buffer[i + 1] = temporal_dithering(j1, src[j1]);
            frame_buffer[i + 2] = temporal_dithering(j2, src[j2]);
            frame_buffer[i + 3] = temporal_dithering(j3, src[j3]);
        }
#endif
    }
}
#elif not defined TEMPORAL_DITHERING
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

        // Ramping up color resolution from 8 to 10 bits via CIE luminance respectively gamma table look-up.
        // Interweave pixels from intermediate buffer into target image to fit the format expected by Hub75 LED panel.
        uint j = 0;

#ifdef HUB75_MULTIPLEX_2_ROWS
        for (int i = 0; i < width * height; i += 2)
        {
            frame_buffer[i] = lut[(src[j] & 0x0000ff) >> 0] << 20 | lut[(src[j] & 0x00ff00) >> 8] << 10 | lut[(src[j] & 0xff0000) >> 16];
            frame_buffer[i + 1] = lut[(src[j + offset] & 0x0000ff) >> 0] << 20 | lut[(src[j + offset] & 0x00ff00) >> 8] << 10 | lut[(src[j + offset] & 0xff0000) >> 16];
            j++;
        }
#elif defined HUB75_MULTIPLEX_4_ROWS
        for (int i = 0; i < width * height; i += 4)
        {
            frame_buffer[i] = lut[(src[j] & 0x0000ff) >> 0] << 20 | lut[(src[j] & 0x00ff00) >> 8] << 10 | lut[(src[j] & 0xff0000) >> 16];
            frame_buffer[i + 1] = lut[(src[j + offset] & 0x0000ff) >> 0] << 20 | lut[(src[j + offset] & 0x00ff00) >> 8] << 10 | lut[(src[j + offset] & 0xff0000) >> 16];
            frame_buffer[i + 2] = lut[(src[j + 2 * offset] & 0x0000ff) >> 0] << 20 | lut[(src[j + 2 * offset] & 0x00ff00) >> 8] << 10 | lut[(src[j + 2 * offset] & 0xff0000) >> 16];
            frame_buffer[i + 3] = lut[(src[j + 3 * offset] & 0x0000ff) >> 0] << 20 | lut[(src[j + 3 * offset] & 0x00ff00) >> 8] << 10 | lut[(src[j + 3 * offset] & 0xff0000) >> 16];
            j++;
        }
#endif
    }
}
#endif

#ifdef TEMPORAL_DITHERING
inline __attribute__((always_inline)) uint32_t temporal_dithering_bgr(size_t j, uint8_t r, uint8_t g, uint8_t b)
{
    // Add higher precision (14-bit) mapped values into accumulator
    acc_r[j] += lut[r];
    acc_g[j] += lut[g];
    acc_b[j] += lut[b];

    // Quantize down to 10-bit output
    uint32_t out_r = acc_r[j] >> ACC_SHIFT; // 10 bits
    uint32_t out_g = acc_g[j] >> ACC_SHIFT;
    uint32_t out_b = acc_b[j] >> ACC_SHIFT;

    // Subtract used portion, keep remainder for error feedback
    acc_r[j] -= (out_r << ACC_SHIFT);
    acc_g[j] -= (out_g << ACC_SHIFT);
    acc_b[j] -= (out_b << ACC_SHIFT);

    return (out_r << 20) | (out_g << 10) | out_b;
}

/**
 * @brief Updates the frame buffer with pixel data from the source array.
 *
 * This function takes a source array of pixel data and updates the frame buffer
 * with interleaved pixel values. The pixel values are gamma-corrected to 10 bits using a lookup table.
 *
 * @param src Graphics object to be updated - RGB888 format, 24-bits in uint32_t array
 */
void update_bgr(const uint8_t *src)
{
    const size_t pixels = (size_t)width * (size_t)height;
    const uint rgb_offset = offset * 3;
#ifdef HUB75_MULTIPLEX_2_ROWS
    for (size_t i = 0, j = 0; i < pixels; j += 3, i += 2)
    {
        frame_buffer[i] = temporal_dithering_bgr(i, src[j], src[j + 1], src[j + 2]);
        frame_buffer[i + 1] = temporal_dithering_bgr(i, src[rgb_offset + j], src[rgb_offset + j + 1], src[rgb_offset + j + 2]);
    }
#elif defined HUB75_MULTIPLEX_4_ROWS
    for (size_t i = 0, j = 0; i < pixels; j += 3, i += 4)
    {
        size_t j0 = j;
        size_t j1 = j0 + rgb_offset;
        size_t j2 = j1 + rgb_offset;
        size_t j3 = j2 + rgb_offset;

        frame_buffer[i] = temporal_dithering_bgr(i, src[j0], src[j0 + 1], src[j0 + 2]);
        frame_buffer[i + 1] = temporal_dithering_bgr(i, src[j1], src[j1 + 1], src[j1 + 2]);
        frame_buffer[i + 2] = temporal_dithering_bgr(i, src[j2], src[j2 + 1], src[j2 + 2]);
        frame_buffer[i + 3] = temporal_dithering_bgr(i, src[j3], src[j3 + 1], src[j3 + 2]);
    }
#endif
}

#elif not defined TEMPORAL_DITHERING
/**
 * @brief Updates the frame buffer with pixel data from the source array.
 *
 * This function takes a source array of pixel data and updates the frame buffer
 * with interleaved pixel values. The pixel values are gamma-corrected to 10 bits using a lookup table.
 *
 * @param src Pointer to the source pixel data array (BGR888 format).
 */
void update_bgr(const uint8_t *src)
{
    uint rgb_offset = offset * 3;
    uint k = 0;
    // Ramping up color resolution from 8 to 10 bits via CIE luminance respectively gamma table look-up.
    // Interweave pixels as required by Hub75 LED panel matrix

#ifdef HUB75_MULTIPLEX_2_ROWS
    for (int j = 0; j < width * height; j += 2)
    {
        frame_buffer[j] = lut[src[k]] << 20 | lut[src[k + 1]] << 10 | lut[src[k + 2]];
        frame_buffer[j + 1] = lut[src[rgb_offset + k]] << 20 | lut[src[rgb_offset + k + 1]] << 10 | lut[src[rgb_offset + k + 2]];
        k += 3;
    }
#elif defined HUB75_MULTIPLEX_4_ROWS
    for (int j = 0; j < width * height; j += 4)
    {
        frame_buffer[j] = lut[src[k]] << 20 | lut[src[k + 1]] << 10 | lut[src[k + 2]];
        frame_buffer[j + 1] = lut[src[rgb_offset + k]] << 20 | lut[src[rgb_offset + k + 1]] << 10 | lut[src[rgb_offset + k + 2]];
        frame_buffer[j + 2] = lut[src[2 * rgb_offset + k]] << 20 | lut[src[2 * rgb_offset + k + 1]] << 10 | lut[src[2 * rgb_offset + k + 2]];
        frame_buffer[j + 3] = lut[src[3 * rgb_offset + k]] << 20 | lut[src[3 * rgb_offset + k + 1]] << 10 | lut[src[3 * rgb_offset + k + 2]];
        k += 3;
    }
#endif
}
#endif
