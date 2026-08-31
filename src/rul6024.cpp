// =============================================================================
// rul6024.cpp
//
// One-time configuration ("register write") sequence for a chain of RUL6024 HUB75 driver ICs.
// This runs BEFORE normal HUB75 scanning starts:
//   it borrows the PIO/GPIO resources long enough to shift WREG1 and WREG2 into every daisy-chained chip,
//   then hands the PIO block back so hub75_row / hub75_bitplane_stream can drive the panel normally.
//
// Protocol background: see rul6024.h for the LE-length command scheme and the WREG1/WREG2 values used here.
// =============================================================================

#include <cstdlib>
#include <cstdint>
#include <cassert>
#include <algorithm>
#include <vector>

#include "pico/stdlib.h"
#include "hardware/pio.h"

#include "hub75.hpp"
#include "hub75.pio.h"

#include "rul6024.h"

// Cached panel/pin configuration for the chain currently being initialized.
// NOTE: file-scope static -> not reentrant. See the rul6024_initialize()
// doc comment in rul6024.h for the implication (one chain in flight at a time).
static Hub75Config cfg;

// -----------------------------------------------------------------------------
// register_dma_buffer layout
//
// rul6024_write_register() (PIO helper, see hub75.pio) expects one already-expanded
// "DMA word per CLK pulse" image per register write:
// one uint32_t per output CLK cycle, each holding a 6-bit value (one bit per RGB sub-pixel lane)
// for that cycle. prepare_register_dma() produces exactly `display_width` such words per register
// (display_width = one CLK pulse per bit position, repeated across the whole daisy chain), where
//     display_width = cfg.panel.matrix_panel_width * cfg.panel.chain_cols
//
// register_dma_buffer holds REGISTER_SLOT_COUNT such images side-by-side,
// addressed by slot index rather than a hardcoded byte offset.
// There is no compile-time cap on display_width: the buffer is sized in ensure_register_dma_buffer_capacity() below,
// driven directly by the display_width computed from whatever Hub75Config was actually passed to rul6024_initialize() —
// so any panel/chain geometry gets a correctly sized buffer, rather than only whichever width the constant happened to
// be hand-picked for.
// -----------------------------------------------------------------------------
static constexpr uint32_t REGISTER_SLOT_WREG1 = 0;
static constexpr uint32_t REGISTER_SLOT_WREG2 = 1;
#ifdef RUL6024_PROBE_RESERVED
static constexpr uint32_t REGISTER_SLOT_TEST = 2;
static constexpr uint32_t REGISTER_SLOT_COUNT = 3;
#else
static constexpr uint32_t REGISTER_SLOT_COUNT = 2; // no TEST slot when not probing
#endif

// Backing storage for all REGISTER_SLOT_COUNT images, sized to exactly
// REGISTER_SLOT_COUNT * display_width uint32_t
// entries by ensure_register_dma_buffer_capacity() the moment display_width is known (once per rul6024_initialize() call —
// not a per-frame allocation, so the one-time heap use here is not a real-time concern).
static std::vector<uint32_t> register_dma_buffer;

// (Re)sizes register_dma_buffer for the given display_width. Must be called before the first register_slot()
// use for that display_width — rul6024_setup() does this as its first step, right after computing display_width from cfg.
static void ensure_register_dma_buffer_capacity(uint32_t display_width)
{
    assert(display_width > 0);
    assert(display_width % 16 == 0); // prepare_register_dma() assumes an integral number of 16-bit chips
    register_dma_buffer.assign(static_cast<size_t>(REGISTER_SLOT_COUNT) * display_width, 0);
}

// Returns a pointer to the start of `slot`'s region within register_dma_buffer,
// sized for the given display_width. Centralizing this (rather than repeating `slot * display_width` at each call site)
// means there is exactly one place that can get the indexing wrong. Requires ensure_register_dma_buffer_capacity(display_width)
// to have already been called for this display_width.
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
// Expands one 16-bit register value into `display_width` 6-bit-per-lane DMA words, MSB (bit 15) first, 
// matching the chip's documented shift order ("the data transmitted to the chip first is the high bit 
// of the register"). Each 16-bit value is repeated back-to-back to fill the whole chain: 
// display_width / 16 gives the number of daisy-chained chips, so e.g. for display_width = 64 
// the same 16-bit value is                             written 4 times in a row, one per chip in the chain.
//
// Each output word is either:
//     RUL6024_DATA_HIGH (0x3f) -> all six data lanes driven HIGH this cycle
//     RUL6024_DATA_LOW  (0x00) -> all six data lanes driven LOW this cycle
//
// i.e. every chip in the chain receives the identical register value —
// there is currently no support for giving different chips in one chain different WREG1/WREG2 values.
// -----------------------------------------------------------------------------
static constexpr uint8_t RUL6024_DATA_HIGH = 0x3f;
static constexpr uint8_t RUL6024_DATA_LOW = 0x00;

static void prepare_register_dma(uint16_t value, uint32_t *dst, uint32_t display_width)
{
    int repeats_per_chain = display_width / 16; // one 16-bit register per chained chip
    for (int chip = 0; chip < repeats_per_chain; ++chip)
    {
        for (int bit = 15; bit >= 0; --bit) // MSB first
        {
            *dst++ = (value & (1u << bit)) ? RUL6024_DATA_HIGH : RUL6024_DATA_LOW;
        }
    }
}

// -----------------------------------------------------------------------------
// rul6024_setup()
//
// Runs the confirmed-working configuration sequence for one RUL6024 chain:
//
//   1. Build the WREG1 / WREG2 DMA images (and, optionally, a probe image
//      for the reserved 4–10 command range — see RUL6024_PROBE_RESERVED below).
//   2. Initialize the rul6024_write_register PIO program on the given state machine.
//   3. Write CMD_WREG1, then CMD_WREG2, in that order.
//
// Unlike an earlier iteration of this file, this sequence does NOT send
// CMD_DATA_LATCH afterwards.
// Testing confirms panel initialization is reliable without CMD_DATA_LATCH and 
// CMD_RESET_OEN them for this single-chain configuration. 
// If you extend this to a chained/multi-group setup and see initialization become flaky again,
// this is the first place to revisit — reintroduce DATA_LATCH/RESET_OEN behind a flag and compare.
// -----------------------------------------------------------------------------
void rul6024_setup(PIO pio, uint sm, uint offset)
{
    uint32_t display_width = cfg.panel.matrix_panel_width * cfg.panel.chain_cols;

    // Must happen before any register_slot() call below: sizes
    // register_dma_buffer for exactly this chain's display_width.
    ensure_register_dma_buffer_capacity(display_width);

    uint32_t *wreg1_buf = register_slot(REGISTER_SLOT_WREG1, display_width);
    uint32_t *wreg2_buf = register_slot(REGISTER_SLOT_WREG2, display_width);

    prepare_register_dma(RUL6024_WREG1_VALUE, wreg1_buf, display_width);
    prepare_register_dma(RUL6024_WREG2_VALUE, wreg2_buf, display_width);

    rul6024_write_register_program_init(pio, sm, offset, cfg.pins.data_base_pin, cfg.pins.clk_pin);

    // ---------------------------------------------------------------------
    // Optional: probe the undocumented "reserved" 4–10 command range.
    //
    // Off by default (REGISTER_SLOT_TEST/its buffer space don't even exist
    // when this isn't defined — see REGISTER_SLOT_COUNT above). Enable with
    // -DRUL6024_PROBE_RESERVED, edit test_data[] to the 16-bit value you
    // want tried for each register (index 0 = register 4, ... index 6 =
    // register 10), and observe the panel after each individual write —
    // ideally one register at a time, not all seven in one boot, so an
    // observed effect can be attributed to a specific register.
    // ---------------------------------------------------------------------
#ifdef RUL6024_PROBE_RESERVED
    uint32_t *test_buf = register_slot(REGISTER_SLOT_TEST, display_width);
    uint16_t test_data[] = {0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000}; // registers 4..10

    for (uint32_t reg = 4; reg <= 10; ++reg)
    {
        prepare_register_dma(test_data[reg - 4], test_buf, display_width);
        rul6024_write_register(pio, sm, display_width, reg, test_buf);
    }
#endif

    // rul6024_write_register(pio, sm, display_width, CMD_WREG1-1, wreg1_buf);
    // rul6024_write_register(pio, sm, display_width, CMD_WREG2-1, wreg2_buf);
    rul6024_write_register(pio, sm, display_width, CMD_WREG1, wreg1_buf);
    rul6024_write_register(pio, sm, display_width, CMD_WREG2, wreg2_buf);
}

// -----------------------------------------------------------------------------
// rul6024_initialize()
//
// Entry point: claims a PIO state machine covering every GPIO this chain's
// register-write sequence touches (6 data lanes + CLK/LE/OEN), runs
// rul6024_setup(), then releases the program and state machine so the
// normal HUB75 row/bitplane PIO programs can use that PIO block.
// -----------------------------------------------------------------------------
void rul6024_initialize(Hub75Config Cfg)
{
    cfg = Cfg;

    uint sm;
    PIO pio;
    uint offset;

    // Every GPIO this chain's PIO program will touch, so pio_claim_free_...
    // below can compute the minimal contiguous GPIO window to request.
    // data_base_pin .. data_base_pin+5 covers the 6 RGB data lanes;
    // clk_pin, strobe_pin (LE) and oen_pin must be the remaining three
    // "set pins" used by rul6024_write_register (see hub75.pio — 
    // they are assumed consecutive: clk_pin, clk_pin+1, clk_pin+2).
    size_t gpio_pins[] = {
        cfg.pins.data_base_pin,
        cfg.pins.data_base_pin + 5, // last of the 6 RGB data lanes
        cfg.pins.clk_pin,
        cfg.pins.strobe_pin,
        cfg.pins.oen_pin};
    size_t n = sizeof(gpio_pins) / sizeof(gpio_pins[0]);

    size_t min_gpio = *std::min_element(gpio_pins, gpio_pins + n);
    size_t max_gpio = *std::max_element(gpio_pins, gpio_pins + n);

    // On RP2350B, GPIO 30-47 are only reachable via PIO2. Passing `true`
    // (force_pio2 / "require exact instance", per this project's helper)
    // ensures we land on a PIO block that can actually reach these pins,
    // rather than silently claiming an SM that can't.
    if (!pio_claim_free_sm_and_add_program_for_gpio_range(
            &rul6024_write_register_program,
            &pio,
            &sm,
            &offset,
            min_gpio,
            // Needs the lowest and highest GPIO actually used across all of
            // the state machine's pin groups (out, set, in, side-set) so it
            // can pick a PIO instance whose addressable window covers both
            // ends.
            static_cast<uint>(max_gpio - min_gpio + 1),
            true))
    {
        panic("Failed to claim PIO SM for rul6024_write_register_program\n");
    }

    if (sm < 0)
    {
        printf("rul6024_initialize: No free SM on this PIO instance!\n");
        return;
    }

    rul6024_setup(pio, sm, offset);
    pio_sm_set_enabled(pio, sm, false);

    // remove rul6024_write_register_program and unclaim state machine
    pio_remove_program(pio, &rul6024_write_register_program, offset);
    pio_sm_unclaim(pio, sm);
}