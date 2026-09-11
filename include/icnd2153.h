#pragma once

#include <cstdint>

// =============================================================================
// icnd2153.h
//
// Command / configuration-register definitions for the ICND2153 HUB75 driver
// IC family, taken from the "Control Command" and "ICND2153 Register Map"
// tables in icnd2153_control_command.png.
//
// Mirrors the role of rul6024.h: every command below is identified purely by
// the *number of DCLK rising edges seen while LE (LAT) is held high* — see
// icnd2153_write_register() / icnd2153_write_control_command() in
// icnd2153.pio, and icnd2153_setup() in icnd2153.cpp for how they're used.
// =============================================================================

// ---- Control commands (LE-high pulse count == command signature) ----------
// Source: "Control Command" table, icnd2153_control_command.png.
static constexpr uint32_t ICND2153_CMD_DATA_LATCH = 1;  // transfer serial data to buffers (no payload)
static constexpr uint32_t ICND2153_CMD_WR_DBG = 2;      // write debug register       (REG5, HAS payload)
static constexpr uint32_t ICND2153_CMD_VSYNC = 3;       // vertical synchronal signal (no payload)
static constexpr uint32_t ICND2153_CMD_WR_CFG1 = 4;     // write configuration register 1 (REG1, HAS payload)
static constexpr uint32_t ICND2153_CMD_RD_CFG1 = 5;     // read configuration register 1  (not used here)
static constexpr uint32_t ICND2153_CMD_WR_CFG2 = 6;     // write configuration register 2 (REG2, HAS payload)
static constexpr uint32_t ICND2153_CMD_RD_CFG2 = 7;     // read configuration register 2  (not used here)
static constexpr uint32_t ICND2153_CMD_WR_CFG3 = 8;     // write configuration register 3 (REG3, HAS payload)
static constexpr uint32_t ICND2153_CMD_RD_CFG3 = 9;     // read configuration register 3  (not used here)
static constexpr uint32_t ICND2153_CMD_WR_CFG4 = 10;    // write configuration register 4 (REG4, HAS payload)
static constexpr uint32_t ICND2153_CMD_RD_CFG4 = 11;    // read configuration register 4  (not used here)
static constexpr uint32_t ICND2153_CMD_EN_OP = 12;      // enable all output channels  (no payload)
static constexpr uint32_t ICND2153_CMD_DIS_OP = 13;     // disable all output channels (no payload)
static constexpr uint32_t ICND2153_CMD_PRE_ACT = 14;    // pre-active command          (no payload)

// ---- Configuration register values -----------------------------------------
// "Setting Example" rows from the ICND2153 Register Map. REG1, REG3 and REG4
// are identical across the RED/GREEN/BLUE examples; only REG2 (current gain)
// differs, so it's the one broken out per colour below.
//
// TODO(Jürgen): confirm which of these three you actually need — most panels
// use a single shared register set for the whole chain rather than one per
// colour. If your panel is one of those, just pick one CFG2 value (or a
// custom-tuned one) and drop the other two.
static constexpr uint16_t ICND2153_CFG1_VALUE = 0x1F70; // 0x0F70;
static constexpr uint16_t ICND2153_CFG2_VALUE = 0xffff; //0x7F9C;
static constexpr uint16_t ICND2153_CFG2_VALUE_GREEN = 0x679C;
static constexpr uint16_t ICND2153_CFG2_VALUE_BLUE = 0x5F9C;
static constexpr uint16_t ICND2153_CFG3_VALUE = 0x40F3; // 0x40F7;
static constexpr uint16_t ICND2153_CFG4_VALUE = 0x0000; // 0x0040;
static constexpr uint16_t ICND2153_CFG5_VALUE = 0x0000; // 0x0008; // debug register (REG5) example value

// -----------------------------------------------------------------------------
// icnd2153_initialize()
//
// Claims a free PIO state machine covering the GPIO range used by this
// RUL6024 chain (data pins + CLK/LE/OEN), runs the WREG1/WREG2
// configuration sequence, then releases the state machine and PIO program
// again so normal HUB75 scanning (hub75_row / hub75_bitplane_stream) can
// use that PIO block afterwards.
//
// Not reentrant: internally caches Cfg in a single file-scope static, so
// only one RUL6024 chain can be initialized "in flight" at a time.
// Safe to call once per chain, sequentially, at start-up.
// -----------------------------------------------------------------------------
void icnd2153_initialize(Hub75Config Cfg);
