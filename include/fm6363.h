#pragma once

#include <cstdint>
#include "hub75.hpp" // for Hub75Config

// =============================================================================
// fm6363.h
//
// Command / configuration-register definitions for the FM6363 HUB75 driver IC family,
// taken from the FM6363 Programming Guide.
//
// Mirrors the role of rul6024.h: every command below is identified purely by
// the *number of DCLK rising edges seen while LE (LAT) is held high* — see
// fm6363_write_register() / fm6363_write_control_command() in
// fm6363.pio, and fm6363_setup() in fm6363.cpp for how they're used.
// =============================================================================

// ---- Control commands (LE-high pulse count == command signature) ----------
// Source: "Control Command" table, fm6363_control_command.png.
static constexpr uint32_t FM6363_CMD_DATA_LATCH = 1; // transfer serial data to buffers (no payload)
static constexpr uint32_t FM6363_CMD_WR_DBG = 2;     // write debug register       (REG5, HAS payload)
static constexpr uint32_t FM6363_CMD_VSYNC = 3;      // vertical synchronal signal (no payload)
static constexpr uint32_t FM6363_CMD_WR_CFG1 = 4;    // write configuration register 1 (REG1, HAS payload)
static constexpr uint32_t FM6363_CMD_RD_CFG1 = 5;    // read configuration register 1  (not used here)
static constexpr uint32_t FM6363_CMD_WR_CFG2 = 6;    // write configuration register 2 (REG2, HAS payload)
static constexpr uint32_t FM6363_CMD_RD_CFG2 = 7;    // read configuration register 2  (not used here)
static constexpr uint32_t FM6363_CMD_WR_CFG3 = 8;    // write configuration register 3 (REG3, HAS payload)
static constexpr uint32_t FM6363_CMD_RD_CFG3 = 9;    // read configuration register 3  (not used here)
static constexpr uint32_t FM6363_CMD_WR_CFG4 = 10;   // write configuration register 4 (REG4, HAS payload)
static constexpr uint32_t FM6363_CMD_RD_CFG4 = 11;   // read configuration register 4  (not used here)
static constexpr uint32_t FM6363_CMD_EN_OP = 12;     // enable all output channels  (no payload)
static constexpr uint32_t FM6363_CMD_DIS_OP = 13;    // disable all output channels (no payload)
static constexpr uint32_t FM6363_CMD_PRE_ACT = 14;   // pre-active command          (no payload)

// ---- Configuration register values -----------------------------------------
static constexpr uint16_t FM6363_CFG1_VALUE = 0x3FF0;
static constexpr uint16_t FM6363_CFG2_VALUE = 0xF39D;
static constexpr uint16_t FM6363_CFG3_VALUE = 0x3076;
static constexpr uint16_t FM6363_CFG4_VALUE = 0x5A00;
static constexpr uint16_t FM6363_CFG5_VALUE = 0x0000; // 0x4C1D;

// ---- REG2 per LED colour --------------------------------------------------
// From the register-map examples: only b14..b10 (ghost-eliminate level) differs per colour.
// Bits 9..1 (I_DIV4N + IGAIN, the current gain) of the CFG2 values below are OVERWRITTEN in
// fm6363_setup() with the value derived from Hub75PanelConfig::panel_gain (fm6363.cpp).

static constexpr uint16_t FM6363_CFG1_BY_COLOUR[3] = {0x3F5B /*R*/, 0x3F5B /*G*/, 0x3F5B /*B*/};
static constexpr uint16_t FM6363_CFG2_BY_COLOUR[3] = {0xFF00 /*R*/, 0xF300 /*G*/, 0xDF00 /*B*/};
static constexpr uint16_t FM6363_CFG4_BY_COLOUR[3] = {0xFF04 /*R*/, 0xFF04 /*G*/, 0xFF04 /*B*/};

static constexpr uint8_t FM6363_LANE_OF[6] = {0, 1, 2, 3, 4, 5};

static constexpr uint32_t FM6363_GCLK_RISES = 74;

constexpr bool fm6363_lane_map_valid()
{
    uint32_t seen = 0;
    for (uint32_t k = 0; k < 6; ++k)
    {
        if (FM6363_LANE_OF[k] > 5)
            return false;
        seen |= 1u << FM6363_LANE_OF[k];
    }
    return seen == 0x3Fu;
}
static_assert(fm6363_lane_map_valid(), "FM6363_LANE_OF must be a permutation of 0..5");

// Colour (0 = R, 1 = G, 2 = B) physically driven by data lane n
constexpr uint32_t fm6363_lane_colour(uint32_t lane)
{
    for (uint32_t k = 0; k < 6; ++k)
        if (FM6363_LANE_OF[k] == lane)
            return k % 3;
    return 0;
}

// -----------------------------------------------------------------------------
// fm6363_initialize()
//
// Claims a free PIO state machine covering the GPIO range used by this
// FM6363 chain (data pins + CLK/LE/OEN), runs the configuration sequence,
// then releases the state machine and PIO program again so FM6363 scanning
// (icnd2153_row / icnd2153_pixel_stream) can use that PIO block afterwards.
//
// Not reentrant: internally caches Cfg in a single file-scope static, so
// only one FM6363 chain can be initialized "in flight" at a time.
// Safe to call once per chain, sequentially, at start-up.
// -----------------------------------------------------------------------------
void fm6363_initialize(Hub75Config Cfg);