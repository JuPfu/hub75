// =============================================================================
// fm6363.cpp
//
// One-time configuration sequence for a chain of fm6363 HUB75 driver ICs.
// This runs BEFORE normal HUB75 scanning starts: it borrows the PIO/GPIO
// resources long enough to run PRE_ACT / EN_OP / VSYNC and shift the
// WR_CFG1..WR_CFG4 / WR_DBG register values into every daisy-chained chip,
// then hands the PIO block back so icnd2153_row / icnd2153_bitplane_stream
// can drive the panel normally.
//
// Directly modelled on rul6024.cpp — see icnd2153.pio's fm6363_write_register
// program for the shared wire protocol (identical to rul6024_write_register:
// a command is decoded purely by its trailing LE-high pulse count), and
// fm6363.h for the command-signature / register-value constants.
//
// Sequence, per fm6363_control_command.png:
//   1. PRE_ACT   (14 LE-high pulses, no payload)
//   2. EN_OP     (12 LE-high pulses, no payload)
//   3. VSYNC     ( 3 LE-high pulses, no payload)
//   4. WR_CFG1 .. WR_CFG4, WR_DBG — each carries `display_width` bits of
//      register data (the same 16-bit value repeated once per daisy-chained
//      chip), with the register's own trailing LE-high pulse count as the
//      command signature (4, 6, 8, 10, 2 respectively).
//   5. DATA_LATCH (1 LE-high pulse, no payload) — commits the shifted values.
//
// Sequence and per-register PRE_ACT framing confirmed against a real-world
// reference implementation for the same chip family: icn2053_start() in
// https://bitbucket.org/nadyrshin_ryu/icn2053_esp32_demo/src/master/main/icn2053/icn2053.c
// (bit-banged, ESP32). Its icn2053_SendDataToAllDrivers()/icn2053_SendData()
// send the identical 16-bit value to every chained chip and only raise LE
// for the trailing `latches` bits of the LAST chip's shift — which, since
// every chip's data is identical, is the same waveform as raising LE for
// the trailing `cmd_signature` bits of the whole `display_width`-bit image,
// i.e. exactly what fm6363_write_register() below already does.
// =============================================================================

#include <cstdint>
#include <cassert>
#include <algorithm>
#include <vector>

#include "pico/stdlib.h"
#include "hardware/pio.h"

#include "hub75.hpp"
#include "fm6363.pio.h"

#include "fm6363.h"

// Cached panel/pin configuration for the chain currently being initialized.
// NOTE: file-scope static -> not reentrant, same caveat as rul6024.cpp's cfg.
static Hub75Config cfg;

// -----------------------------------------------------------------------------
// register_dma_buffer layout — one expanded "DMA word per CLK pulse" image
// per configuration register (REG1..REG4 plus the debug register, REG5),
// each `display_width` uint32_t entries long. Same scheme as rul6024.cpp's
// register_dma_buffer, just with 5 slots instead of 2.
// -----------------------------------------------------------------------------
static constexpr uint32_t REGISTER_SLOT_CFG1 = 0;
static constexpr uint32_t REGISTER_SLOT_CFG2 = 1;
static constexpr uint32_t REGISTER_SLOT_CFG3 = 2;
static constexpr uint32_t REGISTER_SLOT_CFG4 = 3;
static constexpr uint32_t REGISTER_SLOT_DBG = 4;
static constexpr uint32_t REGISTER_SLOT_COUNT = 5;

static std::vector<uint32_t> register_dma_buffer;

// (Re)sizes register_dma_buffer for the given display_width. Must be called
// before the first register_slot() use for that display_width —
// fm6363_setup() does this as its first step.
static void ensure_register_dma_buffer_capacity(uint32_t display_width)
{
    assert(display_width > 0);
    assert(display_width % 16 == 0); // prepare_register_dma() assumes an integral number of 16-bit chips
    register_dma_buffer.assign(static_cast<size_t>(REGISTER_SLOT_COUNT) * display_width, 0);
}

// Returns a pointer to the start of `slot`'s region within register_dma_buffer.
// Requires ensure_register_dma_buffer_capacity(display_width) to have already
// been called for this display_width.
static inline uint32_t *register_slot(uint32_t slot, uint32_t display_width)
{
    assert(slot < REGISTER_SLOT_COUNT);
    size_t offset = static_cast<size_t>(slot) * display_width;
    assert(offset + display_width <= register_dma_buffer.size());
    return &register_dma_buffer[offset];
}

// -----------------------------------------------------------------------------
// prepare_register_dma()
//
// Expands one 16-bit register value into `display_width` 6-bit-per-lane DMA
// words, MSB first, repeated once per daisy-chained chip — identical scheme
// to rul6024.cpp's prepare_register_dma(). As with RUL6024, every chip in
// the chain currently receives the identical register value; there's no
// support yet for giving different chips different WR_CFG*/WR_DBG values.
// -----------------------------------------------------------------------------


// Expands six 16-bit lane values into `display_width` words (one per CLK pulse), MSB first,
// repeated once per chained chip. Bit n of every word goes to data lane n (data_base_pin + n).
static void prepare_register_dma_lanes(const uint16_t lane_value[6], uint32_t *dst, uint32_t display_width)
{
    const uint32_t chips = display_width / 16; // one 16-bit register per chained chip
    for (uint32_t chip = 0; chip < chips; ++chip)
        for (int bit = 15; bit >= 0; --bit)
        {
            uint32_t w = 0;
            for (uint32_t lane = 0; lane < 6; ++lane)
                w |= ((static_cast<uint32_t>(lane_value[lane]) >> bit) & 1u) << lane;
            *dst++ = w;
        }
}

// Same value on every lane (REG1, REG3, REG4, REG5)
static void prepare_register_dma(uint16_t value, uint32_t *dst, uint32_t display_width)
{
    const uint16_t lanes[6] = {value, value, value, value, value, value};
    prepare_register_dma_lanes(lanes, dst, display_width);
}

// -----------------------------------------------------------------------------
// panel_gain (percent, 0..100) -> CFG2 current-gain bits
//
// CFG2 bit 9 (I_DIV4N) and bits 8:1 (IGAIN) set the constant-current output
// (FM6363 Programming Guide, CFG2 / "Current gain"):
//   I_DIV4N = 1 (high gear):  Iout ~ IGAIN / 256
//   I_DIV4N = 0 (low gear):   Iout ~ IGAIN / 1024
//   IGAIN >= 64 is required.
//
// Both gears are put on ONE linear current scale I = IGAIN (low gear) or 4 * IGAIN
// (high gear), so the gear switch is seamless:
//   I =  64 ..  255   low gear   ->  12.5 % ..  49.8 % of the nominal current
//   I = 256 .. 1020   high gear  ->  50.0 % .. 199.2 % of the nominal current
// 100 % (I = 512, high gear with IGAIN = 128) is the current set by Rext.
//
// panel_gain is mapped linearly in current onto I_MIN .. I_MAX:
//   panel_gain =   0 -> I_MIN: lowest current the chip allows (12.5 %) - NOT "off", use setIntensity(0) for that
//   panel_gain = 100 -> I_MAX: 100 % nominal current. Raise I_MAX (up to 1020) only if the LEDs may be overdriven.
// -----------------------------------------------------------------------------
static constexpr uint32_t FM6363_GAIN_I_MIN = 64;
static constexpr uint32_t FM6363_GAIN_I_MAX = 512;
static_assert(FM6363_GAIN_I_MIN >= 64 && FM6363_GAIN_I_MAX <= 1020 && FM6363_GAIN_I_MIN < FM6363_GAIN_I_MAX,
              "gain scale must stay inside IGAIN 64..255 of both current gears");

static constexpr uint16_t FM6363_CFG2_GAIN_MASK = 0x03FE; // I_DIV4N (bit 9) + IGAIN (bits 8:1)

// Returns the bits to be OR-ed into CFG2 (already positioned in bits 9:1).
static constexpr uint16_t fm6363_gain_to_cfg2_bits(uint32_t percent)
{
    if (percent > 100u)
        percent = 100u;

    // common current scale, rounded to nearest
    const uint32_t i = FM6363_GAIN_I_MIN + (percent * (FM6363_GAIN_I_MAX - FM6363_GAIN_I_MIN) + 50u) / 100u;

    uint32_t gear = 0u; // I_DIV4N
    uint32_t igain = i;
    if (i > 255u)
    {
        gear = 1u;
        igain = (i + 2u) / 4u; // high gear: one IGAIN step is worth 4 units of I
    }
    if (igain > 255u)
        igain = 255u;

    return static_cast<uint16_t>((gear << 9) | (igain << 1));
}

static_assert(fm6363_gain_to_cfg2_bits(0) == 0x0080, "0 %: low gear, IGAIN 64");
static_assert(fm6363_gain_to_cfg2_bits(100) == 0x0300, "100 %: high gear, IGAIN 128");
static_assert(fm6363_gain_to_cfg2_bits(250) == fm6363_gain_to_cfg2_bits(100), "values above 100 are clamped");

// -----------------------------------------------------------------------------
// fm6363_setup()
//
// Runs the configuration sequence for one fm6363 chain — see the sequence
// and open-questions notes at the top of this file.
// -----------------------------------------------------------------------------
static void fm6363_setup(PIO pio, uint sm, uint offset)
{
    uint32_t display_width = cfg.panel.matrix_panel_width * cfg.panel.chain_cols * cfg.panel.chain_rows;

    // Must happen before any register_slot() call below.
    ensure_register_dma_buffer_capacity(display_width);

    uint32_t *cfg1_buf = register_slot(REGISTER_SLOT_CFG1, display_width);
    uint32_t *cfg2_buf = register_slot(REGISTER_SLOT_CFG2, display_width);
    uint32_t *cfg3_buf = register_slot(REGISTER_SLOT_CFG3, display_width);
    uint32_t *cfg4_buf = register_slot(REGISTER_SLOT_CFG4, display_width);
    uint32_t *dbg_buf = register_slot(REGISTER_SLOT_DBG, display_width);

    // not tested yet - set number of lines to half the physical rows of the panel
    uint16_t cfg1_lane[6];
    for (uint32_t n = 0; n < 6; ++n)
    {
        cfg1_lane[n] = static_cast<uint16_t>(FM6363_CFG1_BY_COLOUR[fm6363_lane_colour(n)]);
        cfg1_lane[n] = (cfg1_lane[n] & 0xC0FF) | (((cfg.panel.matrix_panel_height >> 1) - 1) << 8);
    }

    prepare_register_dma_lanes(cfg1_lane, cfg1_buf, display_width);

    // REG2: the blanking level ADJ (b14..b10) depends on the LED colour the lane drives,
    // the current gain (b9..b1) comes from panel_gain and is the same for all colours.
    const uint16_t gain_bits = fm6363_gain_to_cfg2_bits(cfg.panel.panel_gain);
    uint16_t cfg2_lane[6];
    for (uint32_t n = 0; n < 6; ++n)
    {
        cfg2_lane[n] = static_cast<uint16_t>((FM6363_CFG2_BY_COLOUR[fm6363_lane_colour(n)] & ~FM6363_CFG2_GAIN_MASK) | gain_bits);
    }
    prepare_register_dma_lanes(cfg2_lane, cfg2_buf, display_width);

    prepare_register_dma(FM6363_CFG3_VALUE, cfg3_buf, display_width);

    uint16_t cfg4_lane[6];
    for (uint32_t n = 0; n < 6; ++n)
    {
        cfg4_lane[n] = static_cast<uint16_t>(FM6363_CFG4_BY_COLOUR[fm6363_lane_colour(n)]);
    }
    prepare_register_dma_lanes(cfg4_lane, cfg4_buf, display_width);

    prepare_register_dma(FM6363_CFG5_VALUE, dbg_buf, display_width);

    icnd2153_write_register_program_init(pio, sm, offset, cfg.pins.data_base_pin, cfg.pins.clk_pin);

    // ---- 1. Pre-activate, enable outputs, vertical sync — all no-payload ----
    // Pre-active command
    icnd2153_write_control_command(pio, sm, FM6363_CMD_PRE_ACT);

    // Enable all output channels
    icnd2153_write_control_command(pio, sm, FM6363_CMD_EN_OP);

    // Vertical sync. signal
    icnd2153_write_control_command(pio, sm, FM6363_CMD_VSYNC);

    // ---- 2. Configuration registers 1..4, then the debug register (REG5) ----
    // Pre-active command
    icnd2153_write_control_command(pio, sm, FM6363_CMD_PRE_ACT);
    // Write configuration register 1
    icnd2153_write_register(pio, sm, display_width, FM6363_CMD_WR_CFG1, cfg1_buf);

    // Pre-active command
    icnd2153_write_control_command(pio, sm, FM6363_CMD_PRE_ACT);
    // Write configuration register 2
    icnd2153_write_register(pio, sm, display_width, FM6363_CMD_WR_CFG2, cfg2_buf);

    // Pre-active command
    icnd2153_write_control_command(pio, sm, FM6363_CMD_PRE_ACT);
    // Write configuration register 3
    icnd2153_write_register(pio, sm, display_width, FM6363_CMD_WR_CFG3, cfg3_buf);

    // Pre-active command
    icnd2153_write_control_command(pio, sm, FM6363_CMD_PRE_ACT);
    // Write configuration register 4
    icnd2153_write_register(pio, sm, display_width, FM6363_CMD_WR_CFG4, cfg4_buf);

    // Pre-active command
    icnd2153_write_control_command(pio, sm, FM6363_CMD_PRE_ACT);
    // Write debug register
    icnd2153_write_register(pio, sm, display_width, FM6363_CMD_WR_DBG, dbg_buf);
}

static void fm6363_wait_idle(PIO pio, uint sm, uint offset)
{
    while (!pio_sm_is_tx_fifo_empty(pio, sm))
        tight_loop_contents();
    while (pio_sm_get_pc(pio, sm) != offset)
        tight_loop_contents();
    pio_sm_exec(pio, sm, pio_encode_set(pio_pins, 0b100));
}

// -----------------------------------------------------------------------------
// fm6363_initialize()
//
// Entry point: claims a PIO state machine covering every GPIO this chain's
// control-command sequence touches (6 data lanes + CLK/LE/PWCLK), runs
// fm6363_setup(), then releases the program and state machine so the
// normal HUB75 row/bitplane PIO programs can use that PIO block. Structured
// identically to rul6024_initialize().
// -----------------------------------------------------------------------------
void fm6363_initialize(Hub75Config Cfg)
{
    cfg = Cfg;

    uint sm;
    PIO pio;
    uint offset;

    // Every GPIO this chain's PIO program will touch, so pio_claim_free_...
    // below can compute the minimal contiguous GPIO window to request.
    size_t gpio_pins[] = {
        cfg.pins.data_base_pin,
        cfg.pins.data_base_pin + 5, // last of the 6 RGB data lanes
        cfg.pins.clk_pin,           // CLK
        cfg.pins.strobe_pin,        // LE / LAT
        cfg.pins.oen_pin};          // PWCLK / OEN
    size_t n = sizeof(gpio_pins) / sizeof(gpio_pins[0]);

    size_t min_gpio = *std::min_element(gpio_pins, gpio_pins + n);
    size_t max_gpio = *std::max_element(gpio_pins, gpio_pins + n);

    // Same RP2350B / PIO2 rationale as rul6024_initialize(): force_pio2=true
    // ensures we land on a PIO block that can actually reach GPIO 30-47.
    if (!pio_claim_free_sm_and_add_program_for_gpio_range(
            &icnd2153_write_register_program,
            &pio,
            &sm,
            &offset,
            min_gpio,
            static_cast<uint>(max_gpio - min_gpio + 1),
            true))
    {
        panic("Failed to claim PIO SM for fm6363_write_register_program\n");
        return;
    }

    // setup initialisation sequence and emit it to panel
    fm6363_setup(pio, sm, offset);

    fm6363_wait_idle(pio, sm, offset);

    // disable state machine
    pio_sm_set_enabled(pio, sm, false);

    // remove fm6363_write_register_program and unclaim state machine
    pio_remove_program(pio, &icnd2153_write_register_program, offset);
    pio_sm_unclaim(pio, sm);
}