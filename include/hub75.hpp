#pragma once

#include "pico.h"

#include <array>
#include <cstddef>
#include <cstdint>

#include "hardware/dma.h"
#include "hardware/pio.h"
#include "pico/critical_section.h"
#include "pico/time.h"

#ifndef USE_PICO_GRAPHICS
#define USE_PICO_GRAPHICS true
#endif

#if USE_PICO_GRAPHICS == true
#include "pico_graphics.hpp"
#endif

// See README.md file chapter "How to Configure" for how to size a Hub75Config for your panel.

enum class Hub75ChainMode
{
    SERPENTINE,
    RASTER
};

// Selects which pixel-mapping algorithm feeds the panel's scan order.
enum class RowMapping
{
    Standard, // two or four rows lit simultaneously
    Split,    // four rows lit simultaneously - many P10 outdoor panels with split upper/lower-half addressing
    S31,      // four rows lit simultaneously - four-way interleaved quarter mapping, panels marketed as "...S31"
};

// Selects the panel-chip init sequence sent before streaming starts.
enum class Hub75PanelChip
{
    GENERIC,
    FM6126A,
    RUL6024,
};

enum class Hub75Rotation
{
    DEG_0 = 0,
    DEG_90 = 90,
    DEG_180 = 180,
    DEG_270 = 270,
};

// Physical panel topology, chaining and drive timing.
struct Hub75PanelConfig
{
    // Set matrix_panel_width/height to the width/height of a SINGLE matrix panel.
    // For chained panels, use chain_rows/chain_cols to describe the arrangement.
    uint32_t matrix_panel_width = 64;
    uint32_t matrix_panel_height = 64;

    // chain_rows: number of panels chained left-to-right in a single chain row.
    // chain_cols: number of chain rows stacked vertically (U-type / serpentine).
    //
    // Currently only serpentine (U-turn) chained topologies are supported.
    // Arbitrary panel rotations or non-serpentine cable layouts are not supported.
    //
    //   Chain row 0: left -> right
    //   Chain row 1: right -> left  (U-turn)
    //   Chain row 2: left -> right
    //   ...
    //
    // The signal input connector is always on the LEFT panel of chain row 0.
    //
    // Examples:
    //   Single panel:          chain_rows=1, chain_cols=1  (or omit both)
    //   2 panels side-by-side: chain_rows=2, chain_cols=1
    //   2x4 serpentine array:  chain_rows=2, chain_cols=4
    uint32_t chain_rows = 1;
    uint32_t chain_cols = 1;
    Hub75ChainMode chain_mode = Hub75ChainMode::SERPENTINE;

    // Scan rate 1:32 for a 64x64 matrix panel means 64 pixel height divided by 32 pixel results in 2 rows lit simultaneously.
    // Scan rate 1:16 for a 64x64 matrix panel means 64 pixel height divided by 16 pixel results in 4 rows lit simultaneously.
    // Scan rate 1:16 for a 64x32 matrix panel means 32 pixel height divided by 16 pixel results in 2 rows lit simultaneously.
    // Scan rate 1:8  for a 64x32 matrix panel means 32 pixel height divided by 8  pixel results in 4 rows lit simultaneously.
    // Scan rate 1:4  for a 32x16 matrix panel means 16 pixel height divided by 4  pixel results in 4 rows lit simultaneously.
    RowMapping panel_kind = RowMapping::Standard;

    // e.g. P3-64*64-32S-V2.0 might have a RUL6024 chip, if so, set panel_chip to Hub75PanelChip::RUL6024
    Hub75PanelChip panel_chip = Hub75PanelChip::GENERIC;

    bool inverted_stb = false;

    // To prevent flicker or ghosting it might be worth a try to reduce state machine speed.
    // For panels with height less or equal to 16 rows try a factor of 8.0f
    // For panels with height less or equal to 32 rows try a factor of 2.0f or 4.0f
    // Even for panels with height less or equal to 62 rows a factor of about 2.0f might solve such an issue
    float sm_clockdiv_factor = 1.0f;

    uint32_t base_latch_ns = 80;
    uint32_t base_addr_ns = 160;
};

// Logical display orientation.
struct Hub75ScreenConfig
{
    // At DEG_0 / DEG_180: src buffer is DISPLAY_WIDTH x DISPLAY_HEIGHT (row-major).
    // At DEG_90 / DEG_270: src buffer is DISPLAY_HEIGHT x DISPLAY_WIDTH (transposed).
    Hub75Rotation rotation = Hub75Rotation::DEG_0;
};

// Wiring of the HUB75 matrix.
struct Hub75PinConfig
{
    uint32_t data_base_pin = 0; // start gpio pin of consecutive color pins e.g., r1, g1, b1, r2, g2, b2
    uint32_t data_n_pins = 6;   // count of consecutive color pins usually 6
    uint32_t rowsel_base_pin = 6; // start gpio pin of address pins
    uint32_t rowsel_n_pins = 5;   // count of consecutive address pins - adapt to the number of address pins of your panel
    uint32_t clk_pin = 11;
    uint32_t strobe_pin = 12;
    uint32_t oen_pin = 13;
};

// Bit depth and color correction.
//
// Color Correction Matrix (CCM) cross-channel mixing is applied AFTER the CIE LUT lookup,
// on already CAP-scaled 10-bit values. Each coefficient is expressed as a right-shift
// amount (integer, no floats):
//   shift=0  -> add 100% of source channel (too much, don't use)
//   shift=5  -> add  3.1% of source channel
//   shift=6  -> add  1.6% of source channel
//   shift=7  -> add  0.8% of source channel
//   shift=8  -> add  0.4% of source channel
//   shift=9  -> add  0.2% of source channel  (barely perceptible)
//   shift=31 -> add  0%   (disabled / identity, use this to turn off a term)
struct Hub75ColorConfig
{
    uint32_t bitplanes = 10; // number of bit-planes used for BCM (Binary Code Modulation) - valid values are 8 or 10

    // Use separate CIE channels for improved color representation - needs more memory.
    bool separate_cie_channels = false;

    // High-weight bit-planes are split into multiple smaller slices within the BCM sequence.
    // This increases the effective refresh rate and cuts down flicker at the cost of some more memory consumption.
    bool balanced_light_output = true;

    uint32_t ccm_rg_shift = 31; // bits of Green added into Red output   (31 = off)
    uint32_t ccm_rb_shift = 31; // bits of Blue  added into Red output   (31 = off)
    uint32_t ccm_gr_shift = 31; // bits of Red   added into Green output (31 = off)
    uint32_t ccm_gb_shift = 31; // bits of Blue  added into Green output (31 = off)
    uint32_t ccm_br_shift = 31; // bits of Red   added into Blue output  (31 = off)
    uint32_t ccm_bg_shift = 31; // bits of Green added into Blue output  (31 = off)
};

struct Hub75Config
{
    Hub75PanelConfig panel{};
    Hub75ScreenConfig screen{};
    Hub75PinConfig pins{};
    Hub75ColorConfig color{};

    // For testing or debugging only: print frame frequency via printf.
    bool frame_rate_debug = false;
};

// Command structure for the row control PIO state machine. Each entry defines the timing
// and addressing for one row in a specific bitplane slice.
//
// Memory layout (packed, DMA streamed):
//   [0] addr_delay  : bits[4:0] row_address (A..E lines), bits[31:5] t_addr (PIO cycles)
//   [1] lit_cycles  : OE active duration (LEDs ON)
//   [2] dark_cycles : OE inactive duration (LEDs OFF)
//
// addr_delay is packed this way because the hub75_row PIO program consumes it as one
// 32-bit DMA word: `out pins, 5` peels off the row address, then `out x, 27` takes the
// rest straight into the address-settle wait loop.
//
// Must remain tightly packed (no padding) - consumed sequentially by DMA -> PIO.
struct Hub75RowCmd
{
    uint32_t addr_delay;
    uint32_t lit_cycles;
    uint32_t dark_cycles;
} __attribute__((packed));

// Non-template base so all Hub75Driver<Cfg> instances - regardless of Cfg - can share the
// single chip-wide DMA_IRQ_0 / DMA_IRQ_1 vectors. irq_set_exclusive_handler only allows one
// handler per IRQ line for the whole MCU, so the two IRQ handlers are installed exactly
// once here and dispatch to every registered instance.
class Hub75DriverBase
{
public:
    // Each instance claims 6 DMA channels (row_chan_, row_ctrl_chan_, pixel_chan_,
    // pixel_ctrl_chan_, read_chan_, write_chan_); RP2350 has 16 channels total, so 2
    // instances (12 channels) is the max that fits. The same limit applies due to PIO
    // ressources running out.
    static constexpr size_t MAX_INSTANCES = 2;

    virtual ~Hub75DriverBase();

protected:
    Hub75DriverBase() = default;

    void register_instance();
    void unregister_instance();

    virtual void handle_ctrl_irq() = 0;
    virtual void handle_bitplane_irq() = 0;

private:
    static void global_ctrl_irq_handler();
    static void global_bitplane_irq_handler();

    static inline Hub75DriverBase *s_instances[MAX_INSTANCES] = {};
    static inline size_t s_instance_count = 0;
    static inline bool s_irq_installed = false;

    // Guards s_instances/s_instance_count/s_irq_installed against register_instance() and
    // unregister_instance() racing each other across cores, and against either racing the
    // global IRQ handlers below (which read the same state, possibly on the other core).
    // Initialized eagerly here (dynamic init of an inline variable runs on core0 before main(),
    // i.e. before core1 could ever be launched), so there's no lazy-init race to solve too.
    static inline critical_section_t s_instance_lock = [] {
        critical_section_t cs;
        critical_section_init(&cs);
        return cs;
    }();
};

template <Hub75Config Cfg>
class Hub75Driver : public Hub75DriverBase
{
private:
    static_assert(Cfg.panel.chain_rows >= 1, "chain_rows must be >= 1");
    static_assert(Cfg.panel.chain_cols >= 1, "chain_cols must be >= 1");
    static_assert(Cfg.color.bitplanes == 8 || Cfg.color.bitplanes == 10, "bitplanes must be 8 or 10");

    // Unrotated panel geometry: internal only. Callers should use SCREEN_WIDTH/SCREEN_HEIGHT
    // below, which takes rotation into account.
    static constexpr uint32_t DISPLAY_WIDTH = Cfg.panel.matrix_panel_width * Cfg.panel.chain_cols;
    static constexpr uint32_t DISPLAY_HEIGHT = Cfg.panel.matrix_panel_height * Cfg.panel.chain_rows;
    static_assert(DISPLAY_WIDTH % 2 == 0, "HUB75 bitstream expects even pixel pairs");

public:
    static constexpr size_t TOTAL_PIXELS = static_cast<size_t>(DISPLAY_WIDTH) * DISPLAY_HEIGHT;

    // SCREEN_WIDTH/SCREEN_HEIGHT follow screen rotation. Use those to size drawing routines
    // or framebuffers passed into update()/update_bgr().
    static constexpr uint32_t SCREEN_WIDTH =
        (Cfg.screen.rotation == Hub75Rotation::DEG_90 || Cfg.screen.rotation == Hub75Rotation::DEG_270)
            ? DISPLAY_HEIGHT
            : DISPLAY_WIDTH;
    static constexpr uint32_t SCREEN_HEIGHT =
        (Cfg.screen.rotation == Hub75Rotation::DEG_90 || Cfg.screen.rotation == Hub75Rotation::DEG_270)
            ? DISPLAY_WIDTH
            : DISPLAY_HEIGHT;
    static_assert(SCREEN_WIDTH == ((Cfg.screen.rotation == Hub75Rotation::DEG_90 || Cfg.screen.rotation == Hub75Rotation::DEG_270)
                                        ? Cfg.panel.chain_rows * Cfg.panel.matrix_panel_height
                                        : Cfg.panel.chain_cols * Cfg.panel.matrix_panel_width),
                  "Width/height mismatch for rotated display");
    static_assert(SCREEN_HEIGHT == ((Cfg.screen.rotation == Hub75Rotation::DEG_90 || Cfg.screen.rotation == Hub75Rotation::DEG_270)
                                         ? Cfg.panel.chain_cols * Cfg.panel.matrix_panel_width
                                         : Cfg.panel.chain_rows * Cfg.panel.matrix_panel_height),
                  "Width/height mismatch for rotated display");

    Hub75Driver() = default;
    Hub75Driver(const Hub75Driver &) = delete;
    Hub75Driver &operator=(const Hub75Driver &) = delete;
    ~Hub75Driver() override;

    // Configures DMA and PIO subsystems and claims hardware resources. Call once before start().
    void create();

    // Starts DMA transfers. The driver keeps running on DMA/PIO with negligible CPU load
    // from this point on - only the IRQ handlers execute, driven by hardware.
    void start();

    void update_bgr(const uint8_t *src);
#if USE_PICO_GRAPHICS == true
    void update(pimoroni::PicoGraphics const *graphics);
#endif

    // Coarse brightness calibration factor for the panel (default 6, range 1-255).
    void setBasisBrightness(uint8_t factor);

    // Fine brightness/intensity control in range [0.0f, 1.0f].
    void setIntensity(float intensity, bool linear_brightness_control = true);

private:
    // --- panel/addressing constants -----------------------------------------------------------
    static constexpr uint32_t ADDR_PINS = Cfg.pins.rowsel_n_pins;
    static constexpr uint32_t ADDR_MASK = (1u << ADDR_PINS) - 1u;
    static constexpr uint32_t SCAN_DEPTH = 1u << ADDR_PINS; // e.g. 16 for 1/16 scan
    static constexpr uint32_t ROWS_IN_PARALLEL = Cfg.panel.matrix_panel_height / SCAN_DEPTH;
    static constexpr uint32_t SCAN_GROUPS = SCAN_DEPTH; // alias, used for RowMapping::Split panels

    static constexpr uint32_t LINE_OFFSET =
        ((Cfg.panel.matrix_panel_width * Cfg.panel.chain_rows * Cfg.panel.chain_cols) >> 1u) * ROWS_IN_PARALLEL;
    static constexpr int32_t BITPLANE_STREAM_LENGTH = static_cast<int32_t>(LINE_OFFSET);

    static constexpr int32_t stride_row = static_cast<int32_t>(Cfg.panel.matrix_panel_width * Cfg.panel.chain_cols);
    static constexpr int32_t stride_to_paired_row = static_cast<int32_t>(SCAN_DEPTH * DISPLAY_WIDTH);

    static_assert(static_cast<size_t>(SCAN_DEPTH) * Cfg.panel.chain_rows * Cfg.panel.chain_cols *
                          Cfg.panel.matrix_panel_width * ROWS_IN_PARALLEL ==
                      TOTAL_PIXELS,
                  "rgb_buffer total writes must equal TOTAL_PIXELS - check rowsel_n_pins vs matrix_panel_height, and chain_rows/chain_cols");

    // --- BCM sequence -------------------------------------------------------------------------
    static constexpr auto compute_bcm_sequence()
    {
        if constexpr (Cfg.color.bitplanes == 10)
        {
            if constexpr (Cfg.color.balanced_light_output)
                // Split BP 9 into 4 parts, BP 8 into 2 parts.
                return std::array<uint8_t, 14>{9, 0, 8, 1, 9, 2, 7, 3, 9, 4, 8, 5, 9, 6};
            else
                return std::array<uint8_t, 10>{0, 9, 2, 7, 4, 5, 1, 8, 3, 6};
        }
        else
        {
            if constexpr (Cfg.color.balanced_light_output)
                // Split BP 7 into 3 parts, BP 6 into 2 parts.
                return std::array<uint8_t, 11>{7, 0, 6, 1, 7, 2, 5, 3, 7, 4, 6};
            else
                return std::array<uint8_t, 8>{0, 7, 2, 5, 1, 6, 3, 4};
        }
    }

    static constexpr auto BCM_SEQUENCE = compute_bcm_sequence();
    static constexpr size_t bcm_sequence_length = BCM_SEQUENCE.size();
    static constexpr uint32_t row_cmd_struct_members = sizeof(Hub75RowCmd) / sizeof(uint32_t);

    static constexpr uint32_t CCM_MAX_VAL = (Cfg.color.bitplanes == 10) ? 1023u : 255u;
    static constexpr uint32_t BRIGHTNESS_FP_SHIFT = 16u;
    static constexpr float SM_CLOCKDIV = (Cfg.panel.sm_clockdiv_factor < 1.0f) ? 1.0f : Cfg.panel.sm_clockdiv_factor;
    static constexpr int FRAME_MEASURE_INTERVAL = 100; // for testing/debugging only, see Cfg.frame_rate_debug

    // --- CIE LUT selection --------------------------------------------------------------------
    static constexpr const uint16_t *cie_red_table();
    static constexpr const uint16_t *cie_green_table();
    static constexpr const uint16_t *cie_blue_table();

    static constexpr void apply_ccm(uint32_t &rv, uint32_t &gv, uint32_t &bv);
    static inline uint32_t pack_lut_rgb(uint32_t colour);
    static inline uint32_t pack_lut_rgb_(uint8_t r, uint8_t g, uint8_t b);

    static inline constexpr int rotated_src_index(int dx, int dy, int dw, int dh);
    static inline uint32_t rot_lut(const uint32_t *src, int dx_base, int dy, int i, int W, int H);
    static inline uint32_t rot_lut_rgb(const uint8_t *src, int dx_base, int dy, int i, int W, int H);
    static inline int32_t map_panel_row(int row, int v, int h, bool reverse);

    // --- Timing -------------------------------------------------------------------------------
    // Cached PIO-cycle counts derived from Cfg.panel.base_{latch,addr}_ns and the actual
    // clk_sys/clkdiv at init time - read every row build, so therefore cached here.
    struct TimingConfig
    {
        uint16_t latch_cycles;
        uint16_t addr_cycles;
    };

    static inline uint ns_to_pio_cycles(uint32_t ns, float clk_sys_hz, float clkdiv);
    void timing_init(float clk_sys_hz, float clkdiv);

    static float cie1931_inverse(float t);
    void compute_bcm_cycles(uint32_t bitplane, uint32_t brightness_fp, uint32_t &lit, uint32_t &dark) const;
    static uint32_t encode_row_address(uint32_t row);
    void build_row_cmd_buffer(uint32_t brightness_fp);

    // --- PIO / DMA setup ----------------------------------------------------------------------
    struct PioConfig
    {
        uint sm_data = 0;
        PIO data_pio = nullptr;
        uint data_prog_offs = 0;
        uint sm_row = 0;
        PIO row_pio = nullptr;
        uint row_prog_offs = 0;

        uint sm_read = 0;
        PIO pio_read = nullptr;
        uint offs_read = 0;
    };

    void configure_pio();
    void setup_dma_transfers();
    void setup_bitplane_creation();
    void setup_display_irq();
    void setup_bitplane_stream_irq();

    void handle_ctrl_irq() override;
    void handle_bitplane_irq() override;

    // --- State --------------------------------------------------------------------------------
    alignas(4) uint8_t frame_buffer1_[(TOTAL_PIXELS >> 1) * bcm_sequence_length];
    alignas(4) uint8_t frame_buffer2_[(TOTAL_PIXELS >> 1) * bcm_sequence_length];

    alignas(4) Hub75RowCmd row_cmd_buffer1_[SCAN_DEPTH * bcm_sequence_length];
    alignas(4) Hub75RowCmd row_cmd_buffer2_[SCAN_DEPTH * bcm_sequence_length];

    alignas(4) uint32_t rgb_buffer_[TOTAL_PIXELS];

    uint8_t *frame_buffer_ = nullptr; // Back buffer - written by bitplane builder (handle_bitplane_irq)
    uint8_t *dma_buffer_ = nullptr;   // Front buffer - read by pixel_chan DMA -> panel streamer

    Hub75RowCmd *row_cmd_buffer_ = nullptr;
    Hub75RowCmd *dma_row_cmd_buffer_ = nullptr;

    volatile bool swap_row_cmd_buffer_pending_ = false;
    volatile bool swap_frame_buffer_pending_ = false;

    TimingConfig timing_config_{};

    int row_chan_ = -1;
    int row_ctrl_chan_ = -1;
    int pixel_chan_ = -1;
    int pixel_ctrl_chan_ = -1;

    int read_chan_ = -1;
    int write_chan_ = -1;

    PioConfig pio_config_{};

    uint32_t bitplane_ = 0;

    // Brightness as fixed-point Q16 (because it may be changed at runtime).
    uint32_t brightness_fp_ = (1u << BRIGHTNESS_FP_SHIFT);
    uint32_t basis_factor_ = 6u;

    // Only touched when Cfg.frame_rate_debug is set.
    int frame_count_ = 0;
    uint32_t frame_freq_us_ = 0;
    absolute_time_t frame_time_start_{};
};

#include "hub75.tpp"
