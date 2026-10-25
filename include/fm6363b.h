#pragma once

#include <cstdint>
#include "hub75.hpp" // for Hub75Config

// =============================================================================
// fm6363b.h
//
// Command / configuration-register definitions for the FM6363B HUB75 driver
// IC family, taken from the "Control Command" and "FM6363B Register Map"
// tables in fm6363b_control_command.png.
//
// Mirrors the role of rul6024.h: every command below is identified purely by
// the *number of DCLK rising edges seen while LE (LAT) is held high* — see
// fm6363b_write_register() / fm6363b_write_control_command() in
// icnd2153.pio, and fm6363b_setup() in fm6363b.cpp for how they're used.
// =============================================================================

// ---- Control commands (LE-high pulse count == command signature) ----------
// Source: "Control Command" table, fm6363b_control_command.png.
static constexpr uint32_t FM6363B_CMD_DATA_LATCH = 1; // transfer serial data to buffers (no payload)
static constexpr uint32_t FM6363B_CMD_WR_DBG = 2;     // write debug register       (REG5, HAS payload)
static constexpr uint32_t FM6363B_CMD_VSYNC = 3;      // vertical synchronal signal (no payload)
static constexpr uint32_t FM6363B_CMD_WR_CFG1 = 4;    // write configuration register 1 (REG1, HAS payload)
static constexpr uint32_t FM6363B_CMD_RD_CFG1 = 5;    // read configuration register 1  (not used here)
static constexpr uint32_t FM6363B_CMD_WR_CFG2 = 6;    // write configuration register 2 (REG2, HAS payload)
static constexpr uint32_t FM6363B_CMD_RD_CFG2 = 7;    // read configuration register 2  (not used here)
static constexpr uint32_t FM6363B_CMD_WR_CFG3 = 8;    // write configuration register 3 (REG3, HAS payload)
static constexpr uint32_t FM6363B_CMD_RD_CFG3 = 9;    // read configuration register 3  (not used here)
static constexpr uint32_t FM6363B_CMD_WR_CFG4 = 10;   // write configuration register 4 (REG4, HAS payload)
static constexpr uint32_t FM6363B_CMD_RD_CFG4 = 11;   // read configuration register 4  (not used here)
static constexpr uint32_t FM6363B_CMD_EN_OP = 12;     // enable all output channels  (no payload)
static constexpr uint32_t FM6363B_CMD_DIS_OP = 13;    // disable all output channels (no payload)
static constexpr uint32_t FM6363B_CMD_PRE_ACT = 14;   // pre-active command          (no payload)

// ---- Configuration register values -----------------------------------------
static constexpr uint16_t FM6363B_CFG1_VALUE = 0x3FF0;
static constexpr uint16_t FM6363B_CFG2_VALUE = 0xF39D;
static constexpr uint16_t FM6363B_CFG3_VALUE = 0x3076; // 0x60b6;  0011 00 00 0111 0110  0x3076
static constexpr uint16_t FM6363B_CFG4_VALUE = 0x5A00;
static constexpr uint16_t FM6363B_CFG5_VALUE = 0x4f1D; // 0x7E08;  0100 1111 0001 1101  0x4f1D

// ---- REG2 per LED colour --------------------------------------------------
// From the register-map examples: only b14..b10 (ghost-eliminate level) differs per colour.

// 0x0fb0, 0x0fb0, 0x60b6, 0x0fb0, 0x7e08 
// {0x7e08, 0x0fb0, 0xe6fc, 0x60b6,  0x5a70};

// static constexpr uint16_t FM6363B_CFG1_BY_COLOUR[3] = {0x3eAB /*R*/, 0x3eAB /*G*/, 0x3eAB /*B*/}; //0x3eab
// static constexpr uint16_t FM6363B_CFG2_BY_COLOUR[3] = {0xFC20, 0xF020, 0xDC20}; //  1101 1100 0010 0000  0xFC20
// static constexpr uint16_t FM6363B_CFG4_BY_COLOUR[3] = {0xEf05 /*R*/, 0xEf05 /*G*/, 0xEf05 /*B*/}; // 0x6f05  0110 1111 0000  0101

static constexpr uint16_t FM6363B_CFG1_BY_COLOUR[3] = {0x3FF0 /*R*/, 0x3FB0 /*G*/, 0x3FB0 /*B*/};
static constexpr uint16_t FM6363B_CFG2_BY_COLOUR[3] = {0xF39D /*R*/, 0xF39D /*G*/, 0xF39D /*B*/};
static constexpr uint16_t FM6363B_CFG4_BY_COLOUR[3] = {0xDA70 /*R*/, 0xDA70 /*G*/, 0xDA70 /*B*/}; //0xf0b6

static constexpr uint8_t FM6363B_LANE_OF[6] = {0, 1, 2, 3, 4, 5};

static constexpr uint32_t FM6363B_GCLK_RISES = 74;

constexpr bool FM6363B_lane_map_valid()
{
    uint32_t seen = 0;
    for (uint32_t k = 0; k < 6; ++k)
    {
        if (FM6363B_LANE_OF[k] > 5)
            return false;
        seen |= 1u << FM6363B_LANE_OF[k];
    }
    return seen == 0x3Fu;
}
static_assert(FM6363B_lane_map_valid(), "FM6363B_LANE_OF must be a permutation of 0..5");

// Colour (0 = R, 1 = G, 2 = B) physically driven by data lane n
constexpr uint32_t fm6363b_lane_colour(uint32_t lane)
{
    for (uint32_t k = 0; k < 6; ++k)
        if (FM6363B_LANE_OF[k] == lane)
            return k % 3;
    return 0;
}

// Software white balance, Q8 (256 = 1.0). Lower the colours that look too strong.
static constexpr uint16_t FM6363B_WHITE_BALANCE_Q8[3] = {256, 256, 256};

// -----------------------------------------------------------------------------
// fm6363b_initialize()
//
// Claims a free PIO state machine covering the GPIO range used by this
// FM6363B chain (data pins + CLK/LE/OEN), runs the configuration sequence,
// then releases the state machine and PIO program again so FM6363B scanning
// (icnd2153_row / icnd2153_pixel_stream) can use that PIO block afterwards.
//
// Not reentrant: internally caches Cfg in a single file-scope static, so
// only one FM6363B chain can be initialized "in flight" at a time.
// Safe to call once per chain, sequentially, at start-up.
// -----------------------------------------------------------------------------
void fm6363b_initialize(Hub75Config Cfg);
