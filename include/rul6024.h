#pragma once
// =============================================================================
// rul6024.h
//
// Public interface and protocol constants for driving RUL6024 HUB75 panel
// driver ICs (Ruichips RUL6024V6, daisy-chained via SDI/SDO).
//
// This header is self-contained: anything a caller needs in order to call
// rul6024_initialize() lives here. It includes "hub75.hpp" itself (rather
// than relying on the includer to have done so first) so it compiles
// correctly regardless of #include order.
// =============================================================================

#include <cstdint>
#include "hub75.hpp"   // for Hub75Config

// -----------------------------------------------------------------------------
// RUL6024 command signatures ("LE length" protocol)
//
// The RUL6024 has no conventional address/data bus. A 16-bit shift register
// (SDI in / SDO out, daisy-chainable across many chips) is clocked in on
// CLK, six bits per chip per CLK pulse (one bit per RGB sub-pixel lane).
// Separately, LE (latch enable) is held HIGH for some number of the final
// CLK pulses of a transfer; the *count* of those LE-high pulses — sampled
// once CLK stops toggling — tells the chip what to do with the data that
// was just shifted in. Per the datasheet:
//
//   LE length     Meaning
//   ---------     ------------------------------------------------------
//   1, then 2     RESET_OEN  (time-sharing display reset, two-step)
//   3             DATA_LATCH (latch the 16-bit shift register out to the
//                  physical output channel — what makes a pixel write
//                  visible on the panel)
//   4–10          Reserved / undocumented by Ruichips
//   11            WR_REG1 (write configuration register 1)
//   12            WR_REG2 (write configuration register 2)
//
// Only the two commands this driver currently issues are defined below.
// If DATA_LATCH / RESET_OEN are ever needed again here (rather than being
// left to the normal row-scan PIO program), add:
//     #define CMD_RESET_OEN_STEP1  1
//     #define CMD_RESET_OEN_STEP2  2
//     #define CMD_DATA_LATCH       3
// -----------------------------------------------------------------------------
#define CMD_WREG1 11
#define CMD_WREG2 12

// -----------------------------------------------------------------------------
// WREG1 / WREG2 payloads — empirically determined, confirmed repeatable.
//
// The datasheet documents WREG1/WREG2's individual bit fields (white
// balance trim, current gain, latch mode, OE timing, shadow-elimination
// control, etc.) but not enough to derive a correct 16-bit value
// analytically for this panel. These two values were found by sweeping
// candidates on real hardware:
//
//   WREG1 = 0x3FB4
//   WREG2 = 0xD1FF
//
// Kept as named constants (rather than inlined hex literals in the .cpp)
// so a future re-tune touches one place, and so the values are visible
// under their own name in a debugger or a logic-analyzer capture diff.
// If you sweep new candidates, prefer editing these two lines over adding ad-hoc literals elsewhere.
// -----------------------------------------------------------------------------
constexpr uint16_t RUL6024_WREG1_VALUE = 0xfdff; // 0xFDcf; //0xFDFF; // 0xFDCB
// 0b1101
constexpr uint16_t RUL6024_WREG2_VALUE = 0xD1FF; // 0xffff; //0xFCFF;

// -----------------------------------------------------------------------------
// rul6024_initialize()
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
void rul6024_initialize(Hub75Config Cfg);
