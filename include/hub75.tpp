#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#include "hardware/clocks.h"
#include "pico/sync.h"

#include "hub75.pio.h"
#include "icnd2153.pio.h"

#include "icnd2153.h"
#include "rul6024.h"
#include "fm6126a.h"

#include "cie.hpp"

template <Hub75Config Cfg>
Hub75Driver<Cfg>::~Hub75Driver()
{
    unregister_instance();
}

// -----------------------------------------------------------------------------------------
// Lifecycle
// -----------------------------------------------------------------------------------------

template <Hub75Config Cfg>
void Hub75Driver<Cfg>::create()
{
    assert(driver_state_ == DriverState::Constructed);
    driver_state_ = DriverState::Created;

    dma_buffer_ = storage_.frame_buffer1_;
    frame_buffer_ = storage_.frame_buffer2_;

    // row_cmd_buffer1_/row_cmd_buffer2_ only exist in the HUB75 specialization of Hub75Storage.
    // PWM panels are driven by pixel_chan_ alone, with no row-command DMA stream at all.
    if constexpr (Cfg.panel.panel_class == PanelClass::HUB75)
    {
        dma_row_cmd_buffer_ = storage_.row_cmd_buffer1_;
        row_cmd_buffer_ = storage_.row_cmd_buffer2_;

        timing_init(clock_get_hz(clk_sys), SM_CLOCKDIV);
    }

    if constexpr (Cfg.panel.panel_chip == Hub75PanelChip::FM6126A)
        FM6126A_setup(Cfg.pins, Cfg.panel.matrix_panel_width);
    else if constexpr (Cfg.panel.panel_chip == Hub75PanelChip::RUL6024)
        rul6024_initialize(Cfg);
    else if constexpr (Cfg.panel.panel_chip == Hub75PanelChip::ICND2153)
        icnd2153_initialize(Cfg);

    configure_pio();
    setup_dma_transfers();

    if constexpr (Cfg.panel.panel_class == PanelClass::HUB75)
    {
        setup_bitplane_creation();
        setup_bitplane_stream_irq();
    }

    setup_display_irq();
    apply_brightness_();

    register_instance();
}

template <Hub75Config Cfg>
void Hub75Driver<Cfg>::start()
{
    assert(driver_state_ == DriverState::Created);
    driver_state_ = DriverState::Started;

    dma_buffer_ = storage_.frame_buffer2_;
    frame_buffer_ = storage_.frame_buffer1_;

    if constexpr (Cfg.panel.panel_class == PanelClass::HUB75)
    {
        dma_row_cmd_buffer_ = storage_.row_cmd_buffer2_;
        row_cmd_buffer_ = storage_.row_cmd_buffer1_;

        // row_chan_/row_ctrl_chan_ are only claimed and configured for HUB75 panels
        // (see setup_dma_transfers()) - they stay at their default -1 for PWM, so touching
        // them here would hand the DMA hardware an invalid channel number.
        dma_channel_set_read_addr(row_ctrl_chan_, &dma_row_cmd_buffer_, false);
        dma_channel_set_read_addr(pixel_ctrl_chan_, &dma_buffer_, false);

        swap_row_cmd_buffer_pending_ = false;
    }

    swap_frame_buffer_pending_ = false;

    dma_channel_set_read_addr(pixel_chan_, dma_buffer_, true);

    if constexpr (Cfg.panel.panel_class == PanelClass::PWM)
    {
        icnd2153_row_start(pio_config_.row_pio, pio_config_.sm_row, SCAN_DEPTH);
    }

    if constexpr (Cfg.panel.panel_class == PanelClass::HUB75)
    {
        dma_channel_set_read_addr(row_chan_, dma_row_cmd_buffer_, true);
    }
}

// -----------------------------------------------------------------------------------------
// Brightness control
// -----------------------------------------------------------------------------------------

template <Hub75Config Cfg>
void Hub75Driver<Cfg>::apply_brightness_()
{
    if constexpr (Cfg.panel.panel_class == PanelClass::HUB75)
        build_row_cmd_buffer(brightness_fp_);
    else
        build_pixel_stream(); // re-serialises the last rgb_buffer_ with the new scale
}

template <Hub75Config Cfg>
void Hub75Driver<Cfg>::setBasisBrightness(uint8_t factor)
{
    if constexpr (Cfg.panel.panel_class == PanelClass::PWM)
    {
        (void)factor;
    }
    else
    {
        basis_factor_ = (factor > 0u) ? factor : 1u;
        apply_brightness_();
    }
}

template <Hub75Config Cfg>
void Hub75Driver<Cfg>::setIntensity(float intensity, bool linear_brightness_control)
{
    if (intensity <= 0.0f)
    {
        brightness_fp_ = 0;
    }
    else if (intensity >= 1.0f)
    {
        brightness_fp_ = (1u << BRIGHTNESS_FP_SHIFT);
    }
    else
    {
        float y = intensity;
        if (linear_brightness_control)
        {
            // Convert perceptual input to linear light output.
            // Without this, the panel appears to jump from dark to bright very quickly because human vision is logarithmic.
            y = cie1931_inverse(intensity);
        }
        brightness_fp_ = (uint32_t)(y * (float)(1u << BRIGHTNESS_FP_SHIFT) + 0.5f);
    }

    apply_brightness_();
}

template <Hub75Config Cfg>
float Hub75Driver<Cfg>::cie1931_inverse(float t)
{
    // Inverse CIE 1931: perceptual input t (0..1) -> linear light Y (0..1)
    //
    // L* = t * 100  (scale from normalised to 0..100)
    // If L* > 8:    Y = ((L* + 16) / 116)^3
    // If L* <= 8:   Y = L* / 903.3
    if (t <= 0.0f)
        return 0.0f;
    if (t >= 1.0f)
        return 1.0f;

    float L = t * 100.0f;

    float Y;
    if (L > 8.0f)
    {
        float f = (L + 16.0f) / 116.0f;
        Y = f * f * f;
    }
    else
    {
        Y = L / 903.3f;
    }

    return std::clamp(Y, 0.0f, 1.0f);
}

template <Hub75Config Cfg>
std::array<uint32_t, 2> Hub75Driver<Cfg>::compute_bcm_cycles(uint32_t bitplane, uint32_t split_factor, uint32_t brightness_fp)
{
    // Full BCM period for this bit plane: doubles with each plane (1, 2, 4, 8 ...)
    // scaled by basis_factor_ for coarse panel calibration.
    uint32_t base_per_slice = (basis_factor_ << bitplane) / split_factor;
    // Lit portion: fraction of the full period during which OEn is asserted.
    // brightness_fp is Q16 fixed-point: 0 = off, 65536 = full brightness.
    uint32_t lit_cycles = (uint32_t)((base_per_slice * (uint64_t)brightness_fp) >> BRIGHTNESS_FP_SHIFT);
    // Dark portion: remaining time OEn is deasserted (panel off).
    // lit + dark = base, so total period is constant regardless of brightness.
    uint32_t dark_cycles = base_per_slice - lit_cycles;

    return std::array<uint32_t, 2>{lit_cycles, dark_cycles};
};

template <Hub75Config Cfg>
uint32_t Hub75Driver<Cfg>::encode_row_address(uint32_t row)
{
    if constexpr (Cfg.panel.address_type == RowAddressing::ABCShiftRegister)
    {
        constexpr uint32_t ROW_CLK = 1u << 0u;  // A
        constexpr uint32_t ROW_BK = 1u << 1u;   // B
        constexpr uint32_t ROW_DATA = 1u << 2u; // C

        // The row driver uses a one-hot row shift register:
        // row 0 injects a '1', all following rows clock that bit forward.
        uint32_t data_bit = (row == 0u) ? ROW_DATA : 0u;
        uint32_t phase0 = ROW_BK | data_bit;
        uint32_t phase1 = ROW_CLK | ROW_BK | data_bit;

        return phase0 | (phase1 << 3u);
    }
    else
    {
        return row;
    }
}

// Build row command buffer for a complete frame: timing + addressing sequences for all
// bitplanes x all scan rows. Not swapped in immediately - swap_row_cmd_buffer_pending_ is
// set and the swap happens in handle_ctrl_irq() at a safe point (frame boundary).
template <Hub75Config Cfg>
void Hub75Driver<Cfg>::build_row_cmd_buffer(uint32_t brightness_fp)
{
    uint32_t idx = 0;

    for (uint8_t bp : BCM_SEQUENCE)
    {
        uint32_t split_factor = 1;
        if constexpr (Cfg.color.balanced_light_output)
        {
            if constexpr (Cfg.color.bitplanes == 10)
            {
                // Split BP 9 into 4 parts, each part gets 1/4 of the duration
                if (bp == 9)
                    split_factor = 4;
                else if (bp == 8)
                    split_factor = 2;
            }
            else
            {
                // Split BP 7 into 3 parts, each part gets 1/3 of the duration
                if (bp == 7)
                    split_factor = 3;
                else if (bp == 6)
                    split_factor = 2;
            }
        }

        const auto [lit, dark] = compute_bcm_cycles(bp, split_factor, brightness_fp);

        for (uint32_t row = 0; row < SCAN_DEPTH; ++row)
        {
            uint32_t t_addr = timing_config_.addr_cycles + (bp >> 1); // address settle
            Hub75RowCmd *cmd = &row_cmd_buffer_[idx++];
            // Low ROW_ADDR_BITS = row address, the remaining 32-ROW_ADDR_BITS = t_addr.
            // t_addr format is panel dependent, can be binary or a shift-register
            // command sequence (ABCShiftRegister).
            cmd->addr_delay = (t_addr << ROW_ADDR_BITS) | (encode_row_address(row) & ROW_ADDR_MASK);
            cmd->lit_cycles = lit;
            cmd->dark_cycles = dark;
        }
    }
    swap_row_cmd_buffer_pending_ = true;
}

// -----------------------------------------------------------------------------------------
// Timing
// -----------------------------------------------------------------------------------------

template <Hub75Config Cfg>
uint Hub75Driver<Cfg>::ns_to_pio_cycles(uint32_t ns, float clk_sys_hz, float clkdiv)
{
    float t_cycle_ns = (clkdiv / clk_sys_hz) * 1e9f;
    return (uint)ceilf(ns / t_cycle_ns);
}

template <Hub75Config Cfg>
void Hub75Driver<Cfg>::timing_init(float clk_sys_hz, float clkdiv)
{
    timing_config_.latch_cycles = ns_to_pio_cycles(Cfg.panel.base_latch_ns, clk_sys_hz, clkdiv);
    timing_config_.addr_cycles = ns_to_pio_cycles(Cfg.panel.base_addr_ns, clk_sys_hz, clkdiv);
}

// -----------------------------------------------------------------------------------------
// IRQ handlers - dispatched through Hub75DriverBase's registry, see hub75.cpp
// -----------------------------------------------------------------------------------------

// DMA IRQ0: frame synchronisation and double-buffer swapping, at safe (frame-boundary) points.
template <Hub75Config Cfg>
void Hub75Driver<Cfg>::handle_ctrl_irq()
{
    if constexpr (Cfg.panel.panel_class == PanelClass::HUB75)
    {
        if (dma_channel_get_irq0_status(row_ctrl_chan_))
        {
            dma_channel_acknowledge_irq0(row_ctrl_chan_);

            if constexpr (Cfg.frame_rate_debug)
            {
                if (frame_count_ == 0)
                {
                    frame_time_start_ = get_absolute_time();
                }
                else if (frame_count_ >= FRAME_MEASURE_INTERVAL)
                {
                    frame_freq_us_ = (uint32_t)absolute_time_diff_us(frame_time_start_, get_absolute_time());
                    frame_count_ = -1; // reset so it measures again next interval

                    uint32_t freq = 1000000u * FRAME_MEASURE_INTERVAL / frame_freq_us_;
                    printf("Frame frequency: %u Hz\n", freq);
                    frame_freq_us_ = 0; // clear until next measurement
                }
                frame_count_++;
            }

            if (swap_row_cmd_buffer_pending_)
            {
                // dma_row_cmd_buffer_ -> active front buffer (DMA reads from it).
                // row_cmd_buffer_ -> back buffer (modified by setBasisBrightness).
                // Swap: the new back buffer becomes the new front buffer.
                Hub75RowCmd *new_front = row_cmd_buffer_;
                row_cmd_buffer_ = (new_front == storage_.row_cmd_buffer1_) ? storage_.row_cmd_buffer2_ : storage_.row_cmd_buffer1_;
                dma_row_cmd_buffer_ = new_front;

                dma_channel_set_read_addr(row_ctrl_chan_, &dma_row_cmd_buffer_, false);

                swap_row_cmd_buffer_pending_ = false;
            }
        }

        if (dma_channel_get_irq0_status(pixel_ctrl_chan_))
        {
            dma_channel_acknowledge_irq0(pixel_ctrl_chan_);

            if (swap_frame_buffer_pending_)
            {
                // dma_buffer_  -> active front buffer (DMA streams from it)
                // frame_buffer_ -> back buffer (refilled by handle_bitplane_irq)
                // Swap: the new back buffer becomes the new front buffer.
                uint8_t *new_front = frame_buffer_;
                frame_buffer_ = (new_front == storage_.frame_buffer1_) ? storage_.frame_buffer2_ : storage_.frame_buffer1_;
                dma_buffer_ = new_front;
                dma_channel_set_read_addr(pixel_ctrl_chan_, &dma_buffer_, false);

                swap_frame_buffer_pending_ = false;
            }
        }
    }
    else
    {
        if (dma_channel_get_irq0_status(pixel_chan_))
        {
            dma_channel_acknowledge_irq0(pixel_chan_);

            if (swap_frame_buffer_pending_)
            {
                uint8_t *new_front = frame_buffer_;
                frame_buffer_ = dma_buffer_;
                // frame_buffer_ = (new_front == storage_.frame_buffer1_) ? storage_.frame_buffer2_ : storage_.frame_buffer1_;
                dma_buffer_ = new_front;
                swap_frame_buffer_pending_ = false;
            }
            __dmb();
            dma_channel_set_read_addr(pixel_chan_, dma_buffer_, true); // restart: count is reloaded
            icnd2153_row_signal_frame(pio_config_.row_pio, pio_config_.sm_row, SCAN_DEPTH);
        }
    }
}

// DMA IRQ1: streaming pipeline for bitplane generation (storage_.rgb_buffer_ -> PIO -> frame_buffer_).
template <Hub75Config Cfg>
void Hub75Driver<Cfg>::handle_bitplane_irq()
{
    if (!dma_channel_get_irq1_status(read_chan_))
        return;

    dma_channel_acknowledge_irq1(read_chan_);

    // go through all bitplanes in BCM_SEQUENCE
    if (++bitplane_ < bcm_sequence_length)
    {
        // Set shift to suit next bitplane
        uint shamt = BCM_SEQUENCE[bitplane_];
        hub75_bitplane_setup_set_shift(pio_config_.pio_read, pio_config_.sm_read, pio_config_.offs_read, shamt);

        // Prepare DMA channels for building next bitplane
        uint8_t *plane_dst = frame_buffer_ + (bitplane_ * (TOTAL_PIXELS >> 1));
        dma_channel_set_write_addr(write_chan_, plane_dst, false);
        dma_channel_set_read_addr(read_chan_, storage_.rgb_buffer_, false);
        dma_start_channel_mask((1u << read_chan_) | (1u << write_chan_));
    }
    else
    {
        __dmb();

        // Reset shift for bitplane 0
        bitplane_ = 0;
        uint shamt = BCM_SEQUENCE[bitplane_];
        hub75_bitplane_setup_set_shift(pio_config_.pio_read, pio_config_.sm_read, pio_config_.offs_read, shamt);

        // frame_buffer_ rebuild is complete.
        // Signal to swap frame_buffer_
        // - to display new content of frame_buffer_ on matrix panel
        // - to make new "back-buffer" available for writing
        swap_frame_buffer_pending_ = true;
    }
}

// -----------------------------------------------------------------------------------------
// PIO / DMA setup
// -----------------------------------------------------------------------------------------

static void dma_setup_(uint ch, dma_channel_transfer_size size, bool read_inc, bool write_inc, uint dreq, uint chain_to,
                       volatile void *dst, const volatile void *src, uint32_t count)
{
    dma_channel_config c = dma_channel_get_default_config(ch);
    channel_config_set_transfer_data_size(&c, size);
    channel_config_set_read_increment(&c, read_inc);
    channel_config_set_write_increment(&c, write_inc);
    channel_config_set_dreq(&c, dreq);
    channel_config_set_high_priority(&c, true);
    channel_config_set_chain_to(&c, chain_to);
    dma_channel_configure(ch, &c, dst, src, dma_encode_transfer_count(count), false);
}

template <Hub75Config Cfg>
void Hub75Driver<Cfg>::setup_bitplane_creation()
{
    read_chan_ = dma_claim_unused_channel(true);
    write_chan_ = dma_claim_unused_channel(true);

    // --- READ CHANNEL (Memory -> PIO) ---
    dma_setup_(read_chan_,
               DMA_SIZE_32,
               true,
               false,
               pio_get_dreq(pio_config_.pio_read, pio_config_.sm_read, true),
               read_chan_,
               &pio_config_.pio_read->txf[pio_config_.sm_read],
               nullptr,
               TOTAL_PIXELS);

    // --- WRITE CHANNEL (PIO -> Memory) ---
    dma_setup_(write_chan_,
               DMA_SIZE_32,
               false,
               true,
               pio_get_dreq(pio_config_.pio_read, pio_config_.sm_read, false),
               write_chan_,
               nullptr,
               &pio_config_.pio_read->rxf[pio_config_.sm_read],
               (TOTAL_PIXELS >> 1) >> 2);
}

template <Hub75Config Cfg>
void Hub75Driver<Cfg>::setup_display_irq()
{
    if constexpr (Cfg.panel.panel_class == PanelClass::HUB75)
    {

        dma_channel_set_irq0_enabled(row_ctrl_chan_, true);
        dma_channel_set_irq0_enabled(pixel_ctrl_chan_, true);
    }
    else
    {
        dma_channel_set_irq0_enabled(pixel_chan_, true); // frame done = last word handed to the PIO
    }
}

template <Hub75Config Cfg>
void Hub75Driver<Cfg>::setup_bitplane_stream_irq()
{
    dma_channel_set_irq1_enabled(read_chan_, true);
}

// hub75_row(_inverted) and hub75_bitplane_stream synchronise with each other via PIO-block-
// local IRQ flags 0/1 (see src/hub75.pio: "wait 1 irq 0/1" / "irq nowait 0/1"). Those flags
// are local to one physical PIO block, so the two programs MUST share a block or the handshake
// silently breaks. The generic pio_claim_free_sm_and_add_program*() helpers have no
// "put it on this specific block" option, only "search all blocks in some order".
// This function claims every free SM on every OTHER block first, so the wrapped claim has
// no choice but to land on `target` (or fail cleanly), then releases the reservation again.
template <typename ClaimFn>
inline bool hub75_claim_on_pio(PIO target, ClaimFn &&claim_fn)
{
    bool reserved[NUM_PIOS][NUM_PIO_STATE_MACHINES] = {};
    for (uint i = 0; i < (uint)NUM_PIOS; ++i)
    {
        PIO pio = pio_get_instance(i);
        if (pio == target)
            continue;
        for (uint sm = 0; sm < NUM_PIO_STATE_MACHINES; ++sm)
        {
            if (!pio_sm_is_claimed(pio, sm))
            {
                pio_sm_claim(pio, sm);
                reserved[i][sm] = true;
            }
        }
    }

    bool ok = claim_fn();

    for (uint i = 0; i < (uint)NUM_PIOS; ++i)
    {
        PIO pio = pio_get_instance(i);
        for (uint sm = 0; sm < NUM_PIO_STATE_MACHINES; ++sm)
        {
            if (reserved[i][sm])
                pio_sm_unclaim(pio, sm);
        }
    }
    return ok;
}

// Configures the PIO state machines responsible for shifting pixel data and controlling
// row addressing, and claims hardware resources for them.
//
// hub75_bitplane_stream + hub75_row(_inverted) must share one 32-word PIO block
// (see hub75_claim_on_pio() above), and no two Hub75Driver instances can share a block
// for their pairs (IRQ-flag collision, see claim_pio_block_for_row_stream()).
// Each instance tries its own preferred block first (spreads instances apart when nothing
// else is competing for PIO), then falls back through the remaining blocks in turn instead of
// panicking. Block occupancy isn't only ours to plan: another PIO consumer (e.g. cyw43's wifi
// SPI state machine on a pico2_w, may already be sitting on the preferred block, or a prior
// instance's bitplane_setup program may have sneaked into it. If the row claim fails after
// the stream claim already succeeded on a candidate, the stream claim is rolled back before
// moving to the next candidate, so no candidate is left half-claimed.
template <Hub75Config Cfg>
void Hub75Driver<Cfg>::configure_pio()
{
    const size_t my_index = instance_count();
    const uint preferred = (uint)(NUM_PIOS - 1 - (my_index % (size_t)NUM_PIOS));

    uint candidates[NUM_PIOS];
    candidates[0] = preferred;
    for (uint i = NUM_PIOS, n = 1; i-- > 0;)
        if (i != preferred)
            candidates[n++] = i;

    bool placed = false;

    if constexpr (Cfg.panel.panel_class == PanelClass::HUB75)
    {
        for (uint c = 0; c < (uint)NUM_PIOS && !placed; ++c)
        {
            const uint pio_index = candidates[c];
            if (!claim_pio_block_for_row_stream(pio_index))
                continue; // another instance already dedicated this block to its own row+stream pair

            PIO candidate = pio_get_instance(pio_index);

            static constexpr uint32_t stream_lo = std::min(Cfg.pins.data_base_pin, Cfg.pins.clk_pin);
            static constexpr uint32_t stream_hi = std::max(Cfg.pins.data_base_pin + Cfg.pins.data_n_pins - 1, Cfg.pins.clk_pin);

            // gpio_count must span from the lowest to the highest GPIO actually used (out pins AND
            // side-set/CLK here), not just count them - pio_claim_free_sm_and_add_program_for_gpio_range()
            // uses it to pick/configure a PIO instance whose GPIO_BASE window covers both ends.
            bool stream_ok = hub75_claim_on_pio(candidate, [&] // λ-function - 	all variables used in the lambda are captured by reference
                                                { return pio_claim_free_sm_and_add_program_for_gpio_range(
                                                      &hub75_bitplane_stream_program,
                                                      &pio_config_.data_pio,
                                                      &pio_config_.sm_data,
                                                      &pio_config_.data_prog_offs,
                                                      stream_lo,
                                                      stream_hi - stream_lo + 1,
                                                      true); });

            if (stream_ok)
            {
                static constexpr uint32_t row_lo = std::min({Cfg.pins.rowsel_base_pin, Cfg.pins.strobe_pin, Cfg.pins.oen_pin});
                static constexpr uint32_t row_hi = std::max({Cfg.pins.rowsel_base_pin + Cfg.pins.rowsel_n_pins - 1, Cfg.pins.strobe_pin, Cfg.pins.oen_pin});

                // Inverted-STB panels are handled by inverting the STROBE pin at the GPIO pad
                // level (see hub75_row_program_init), so there is only one row program.
                bool row_ok = false;
                if constexpr (Cfg.panel.address_type == RowAddressing::ABCShiftRegister)
                {
                    row_ok = hub75_claim_on_pio(candidate, [&]
                                                { return pio_claim_free_sm_and_add_program_for_gpio_range(
                                                      &hub75_row_abc_shift_register_program,
                                                      &pio_config_.row_pio,
                                                      &pio_config_.sm_row,
                                                      &pio_config_.row_prog_offs,
                                                      row_lo,
                                                      row_hi - row_lo + 1,
                                                      true); });
                }
                else
                {
                    row_ok = hub75_claim_on_pio(candidate, [&]
                                                { return pio_claim_free_sm_and_add_program_for_gpio_range(
                                                      &hub75_row_program,
                                                      &pio_config_.row_pio,
                                                      &pio_config_.sm_row,
                                                      &pio_config_.row_prog_offs,
                                                      row_lo,
                                                      row_hi - row_lo + 1,
                                                      true); });
                }

                if (row_ok)
                {
                    placed = true;
                    break;
                }

                pio_remove_program_and_unclaim_sm(&hub75_bitplane_stream_program, pio_config_.data_pio, pio_config_.sm_data, pio_config_.data_prog_offs);
            }

            release_pio_block_for_row_stream(pio_index);
        }

        if (!placed)
        {
            panic("Failed to find a PIO block with room for hub75_bitplane_stream_program + "
                  "hub75_row_program (checked all %d blocks)\n",
                  (int)NUM_PIOS);
        }

        hub75_bitplane_stream_program_init(pio_config_.data_pio, pio_config_.sm_data, pio_config_.data_prog_offs, Cfg.pins.data_base_pin, Cfg.pins.clk_pin, BITPLANE_STREAM_LENGTH);

        // Implementation of Pimoronis anti ghosting solution: https://github.com/pimoroni/pimoroni-pico/commit/9e7c2640d426f7b97ca2d5e9161d3f0a00f21abf
        // base_latch_wait_cycles passed as parameter to hub75_row program.
        // inverted_stb inverts the STROBE pin at the GPIO pad level for panels with inverted latch polarity.
        if constexpr (Cfg.panel.address_type == RowAddressing::ABCShiftRegister)
        {
            hub75_row_abc_shift_register_program_init(pio_config_.row_pio, pio_config_.sm_row, pio_config_.row_prog_offs, Cfg.pins.rowsel_base_pin, Cfg.pins.rowsel_n_pins, Cfg.pins.strobe_pin, timing_config_.latch_cycles, Cfg.panel.inverted_stb);
        }
        else
        {
            hub75_row_program_init(pio_config_.row_pio, pio_config_.sm_row, pio_config_.row_prog_offs, Cfg.pins.rowsel_base_pin, Cfg.pins.rowsel_n_pins, Cfg.pins.strobe_pin, timing_config_.latch_cycles, Cfg.panel.inverted_stb);
        }
        // State machine for "parallelized" building of the bit-plane structure. No IRQ/GPIO use
        // (see src/hub75.pio), so unlike stream/row it isn't restricted to any particular block or
        // exclusive to one instance - the plain claim call already searches every block itself.
        if (!pio_claim_free_sm_and_add_program(
                &hub75_bitplane_setup_program,
                &pio_config_.pio_read,
                &pio_config_.sm_read,
                &pio_config_.offs_read))
        {
            panic("Failed to claim PIO SM for hub75_bitplane_setup_program\n");
        }
        hub75_bitplane_setup_program_init(pio_config_.pio_read, pio_config_.sm_read, pio_config_.offs_read);
    }
    else
    {
        for (uint c = 0; c < (uint)NUM_PIOS && !placed; ++c)
        {
            const uint pio_index = candidates[c];
            if (!claim_pio_block_for_row_stream(pio_index))
                continue; // another instance already dedicated this block to its own row+stream pair

            PIO candidate = pio_get_instance(pio_index);

            static constexpr uint32_t stream_lo = std::min(Cfg.pins.data_base_pin, Cfg.pins.clk_pin);
            static constexpr uint32_t stream_hi = std::max(Cfg.pins.data_base_pin + Cfg.pins.data_n_pins - 1, Cfg.pins.clk_pin + 1);

            // gpio_count must span from the lowest to the highest GPIO actually used (out pins AND
            // side-set/CLK+LE here), not just count them - pio_claim_free_sm_and_add_program_for_gpio_range()
            // uses it to pick/configure a PIO instance whose GPIO_BASE window covers both ends.
            bool stream_ok = hub75_claim_on_pio(candidate, [&] // λ-function - all variables used in the lambda are captured by reference
                                                { return pio_claim_free_sm_and_add_program_for_gpio_range(
                                                      &icnd2153_pixel_stream_program,
                                                      &pio_config_.data_pio,
                                                      &pio_config_.sm_data,
                                                      &pio_config_.data_prog_offs,
                                                      stream_lo,
                                                      stream_hi - stream_lo + 1,
                                                      true); });

            if (stream_ok)
            {
                // icnd2153_row only ever touches rowsel_base_pin's 3 pins (RA/RB/RC)
                static constexpr uint32_t row_lo = std::min(Cfg.pins.data_base_pin, Cfg.pins.oen_pin);
                static constexpr uint32_t row_hi = std::max(Cfg.pins.rowsel_base_pin + Cfg.pins.rowsel_n_pins - 1, Cfg.pins.oen_pin);

                bool row_ok = hub75_claim_on_pio(candidate, [&] // λ-function - all variables used in the lambda are captured by reference
                                                 { return pio_claim_free_sm_and_add_program_for_gpio_range(
                                                       &icnd2153_row_program,
                                                       &pio_config_.row_pio,
                                                       &pio_config_.sm_row,
                                                       &pio_config_.row_prog_offs,
                                                       row_lo,
                                                       row_hi - row_lo + 1,
                                                       true); });
                if (row_ok)
                {
                    placed = true;
                    break;
                }
            }

            release_pio_block_for_row_stream(pio_index);
        }

        if (!placed)
        {
            panic("Failed to find a PIO block with room for icnd2153_pixel_stream_program + icnd2153_row_program (checked all %d blocks)\n", (int)NUM_PIOS);
        }

        icnd2153_pixel_stream_program_init(pio_config_.data_pio, pio_config_.sm_data, pio_config_.data_prog_offs, Cfg.pins.data_base_pin, Cfg.pins.clk_pin);

        icnd2153_row_program_init(pio_config_.row_pio, pio_config_.sm_row, pio_config_.row_prog_offs, Cfg.pins.rowsel_base_pin, Cfg.pins.oen_pin, SCAN_DEPTH, 138);
    }
}

static float icnd2153_clkdiv_(uint32_t sys_hz, float floor_mhz, float margin_mhz) // f = max(floor, sys/5 - margin)
{
    return sys_hz / (std::max(floor_mhz, sys_hz / 5e6f - margin_mhz) * 1e6f);
}

// Configures multiple DMA channels to transfer pixel data, dummy pixel data, and output
// enable signal, to the PIO state machines controlling the HUB75 matrix. Also configures
// the DMA channel which gets active when an output enable signal has finished.
template <Hub75Config Cfg>
void Hub75Driver<Cfg>::setup_dma_transfers()
{
    if constexpr (Cfg.panel.panel_class == PanelClass::HUB75)
    {
        row_chan_ = dma_claim_unused_channel(true);
        row_ctrl_chan_ = dma_claim_unused_channel(true);

        // row channel
        dma_setup_(row_chan_,
                   DMA_SIZE_32,
                   true,
                   false,
                   pio_get_dreq(pio_config_.row_pio, pio_config_.sm_row, true),
                   row_ctrl_chan_,
                   &pio_config_.row_pio->txf[pio_config_.sm_row],
                   &dma_row_cmd_buffer_,
                   bcm_sequence_length * SCAN_DEPTH * row_cmd_struct_members);

        // row ctrl channel
        dma_setup_(row_ctrl_chan_,
                   DMA_SIZE_32,
                   false,
                   false,
                   DREQ_FORCE,
                   row_chan_,
                   &dma_hw->ch[row_chan_].read_addr,
                   &dma_row_cmd_buffer_,
                   1);

        // pixel channel
        pixel_chan_ = dma_claim_unused_channel(true);
        pixel_ctrl_chan_ = dma_claim_unused_channel(true);

        dma_setup_(pixel_chan_,
                   DMA_SIZE_8,
                   true,
                   false,
                   pio_get_dreq(pio_config_.data_pio, pio_config_.sm_data, true),
                   pixel_ctrl_chan_,
                   &pio_config_.data_pio->txf[pio_config_.sm_data],
                   &dma_buffer_,
                   (TOTAL_PIXELS >> 1) * bcm_sequence_length);

        // pixel ctrl channel
        dma_setup_(pixel_ctrl_chan_,
                   DMA_SIZE_32,
                   false,
                   false,
                   DREQ_FORCE,
                   pixel_chan_,
                   &dma_hw->ch[pixel_chan_].read_addr,
                   &dma_buffer_,
                   1);

        pio_sm_set_clkdiv(pio_config_.data_pio, pio_config_.sm_data, SM_CLOCKDIV);
        pio_sm_set_clkdiv(pio_config_.row_pio, pio_config_.sm_row, SM_CLOCKDIV);
    }
    else
    {
        // icnd2153_pixel_stream receives pixel data fed by DMA
        pixel_chan_ = dma_claim_unused_channel(true);

        // The pixel_chan iterates over all transactions in one big swoop.
        dma_setup_(pixel_chan_,
                   DMA_SIZE_32,
                   true,
                   false,
                   pio_get_dreq(pio_config_.data_pio, pio_config_.sm_data, true),
                   pixel_chan_,
                   &pio_config_.data_pio->txf[pio_config_.sm_data],
                   &dma_buffer_,
                   PWM_FRAME_BYTES / 4);

        uint32_t sys_clk_hz = clock_get_hz(clk_sys);

        // icnd2153 pixel stream can run between 15 MHz and 20 MHz
        float sm_clockdiv = icnd2153_clkdiv_(sys_clk_hz, 15.f, 15.f);
        pio_sm_set_clkdiv(pio_config_.data_pio, pio_config_.sm_data, sm_clockdiv);

        // icnd2153 row can run between 20 MHz and 25 MHz
        sm_clockdiv = icnd2153_clkdiv_(sys_clk_hz, 20.f, 10.f);
        pio_sm_set_clkdiv(pio_config_.row_pio, pio_config_.sm_row, sm_clockdiv);
    }
}

// -----------------------------------------------------------------------------------------
// Colour pipeline
// -----------------------------------------------------------------------------------------

// Derives a 256-entry gamma table for an arbitrary bitplane depth from a 16-bit master
// table by right-shifting each entry - used for any Cfg.color.bitplanes value that doesn't
// have its own hand-generated table (currently: anything other than 8, 10, or 16). This
// avoids needing to run cie.py and add a new CIEnn/_RED/_GREEN/_BLUE table to cie.hpp every
// time a different bitplane depth is tried; 8 and 10 stay on their existing, separately
// hand-tuned tables below since those are already in use by other panel configs.
static constexpr std::array<uint16_t, 256> derive_cie_table(const uint16_t *src16, unsigned bitplanes)
{
    std::array<uint16_t, 256> out{};
    const unsigned shift = 16u - bitplanes; // e.g. bitplanes=12 -> shift right by 4
    for (int v = 0; v < 256; ++v)
        out[static_cast<size_t>(v)] = static_cast<uint16_t>(src16[v] >> shift);
    return out;
}

template <Hub75Config Cfg>
constexpr const uint16_t *Hub75Driver<Cfg>::cie_red_table()
{
    if constexpr (Cfg.color.separate_cie_channels)
    {
        if constexpr (Cfg.color.bitplanes == 16)
            return CIE16_RED;
        else if constexpr (Cfg.color.bitplanes == 10)
            return CIE10_RED;
        else if constexpr (Cfg.color.bitplanes == 8)
            return CIE8_RED;
        else
        {
            static constexpr auto table = derive_cie_table(CIE16_RED, Cfg.color.bitplanes);
            return table.data();
        }
    }
    else
    {
        if constexpr (Cfg.color.bitplanes == 16)
            return CIE16;
        else if constexpr (Cfg.color.bitplanes == 10)
            return CIE10;
        else if constexpr (Cfg.color.bitplanes == 8)
            return CIE8;
        else
        {
            static constexpr auto table = derive_cie_table(CIE16, Cfg.color.bitplanes);
            return table.data();
        }
    }
}

template <Hub75Config Cfg>
constexpr const uint16_t *Hub75Driver<Cfg>::cie_green_table()
{
    if constexpr (Cfg.color.separate_cie_channels)
    {
        if constexpr (Cfg.color.bitplanes == 16)
            return CIE16_GREEN;
        else if constexpr (Cfg.color.bitplanes == 10)
            return CIE10_GREEN;
        else if constexpr (Cfg.color.bitplanes == 8)
            return CIE8_GREEN;
        else
        {
            static constexpr auto table = derive_cie_table(CIE16_GREEN, Cfg.color.bitplanes);
            return table.data();
        }
    }
    else
    {
        if constexpr (Cfg.color.bitplanes == 16)
            return CIE16;
        else if constexpr (Cfg.color.bitplanes == 10)
            return CIE10;
        else if constexpr (Cfg.color.bitplanes == 8)
            return CIE8;
        else
        {
            static constexpr auto table = derive_cie_table(CIE16, Cfg.color.bitplanes);
            return table.data();
        }
    }
}

template <Hub75Config Cfg>
constexpr const uint16_t *Hub75Driver<Cfg>::cie_blue_table()
{
    if constexpr (Cfg.color.separate_cie_channels)
    {
        if constexpr (Cfg.color.bitplanes == 16)
            return CIE16_BLUE;
        else if constexpr (Cfg.color.bitplanes == 10)
            return CIE10_BLUE;
        else if constexpr (Cfg.color.bitplanes == 8)
            return CIE8_BLUE;
        else
        {
            static constexpr auto table = derive_cie_table(CIE16_BLUE, Cfg.color.bitplanes);
            return table.data();
        }
    }
    else
    {
        if constexpr (Cfg.color.bitplanes == 16)
            return CIE16;
        else if constexpr (Cfg.color.bitplanes == 10)
            return CIE10;
        else if constexpr (Cfg.color.bitplanes == 8)
            return CIE8;
        else
        {
            static constexpr auto table = derive_cie_table(CIE16, Cfg.color.bitplanes);
            return table.data();
        }
    }
}

// Full cross-channel mixing on already-LUT-mapped 10/8-bit values. rv, gv, bv hold the LUT
// output on entry and the mixed, clamped result on return. The cross-terms are purely
// additive (superposition model):
//   r_out = r + (g >> RG_SHIFT) + (b >> RB_SHIFT)
//   g_out = g + (r >> GR_SHIFT) + (b >> GB_SHIFT)
//   b_out = b + (r >> BR_SHIFT) + (g >> BG_SHIFT)
template <Hub75Config Cfg>
constexpr void Hub75Driver<Cfg>::apply_ccm(uint32_t &rv, uint32_t &gv, uint32_t &bv)
{
    // shift == 31 means "off" (see Hub75ColorConfig doc comment). Skip disabled cross-terms
    // at compile time instead of computing a shift+add that would numerically fold to +0
    // anyway: this runs once per pixel, TOTAL_PIXELS times per update()/update_bgr() call.
    const uint32_t rv0 = rv, gv0 = gv, bv0 = bv;

    if constexpr (Cfg.color.ccm_rg_shift != 31 || Cfg.color.ccm_rb_shift != 31)
    {
        uint32_t r = rv0;
        if constexpr (Cfg.color.ccm_rg_shift != 31)
            r += gv0 >> Cfg.color.ccm_rg_shift;
        if constexpr (Cfg.color.ccm_rb_shift != 31)
            r += bv0 >> Cfg.color.ccm_rb_shift;
        rv = (r > CCM_MAX_VAL) ? CCM_MAX_VAL : r;
    }

    if constexpr (Cfg.color.ccm_gr_shift != 31 || Cfg.color.ccm_gb_shift != 31)
    {
        uint32_t g = gv0;
        if constexpr (Cfg.color.ccm_gr_shift != 31)
            g += rv0 >> Cfg.color.ccm_gr_shift;
        if constexpr (Cfg.color.ccm_gb_shift != 31)
            g += bv0 >> Cfg.color.ccm_gb_shift;
        gv = (g > CCM_MAX_VAL) ? CCM_MAX_VAL : g;
    }

    if constexpr (Cfg.color.ccm_br_shift != 31 || Cfg.color.ccm_bg_shift != 31)
    {
        uint32_t b = bv0;
        if constexpr (Cfg.color.ccm_br_shift != 31)
            b += rv0 >> Cfg.color.ccm_br_shift;
        if constexpr (Cfg.color.ccm_bg_shift != 31)
            b += gv0 >> Cfg.color.ccm_bg_shift;
        bv = (b > CCM_MAX_VAL) ? CCM_MAX_VAL : b;
    }
}

template <Hub75Config Cfg>
inline void Hub75Driver<Cfg>::store_pixel_(size_t &fb, uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t rv = cie_red_table()[r];
    uint32_t gv = cie_green_table()[g];
    uint32_t bv = cie_blue_table()[b];
    apply_ccm(rv, gv, bv);
    if constexpr (Cfg.panel.panel_class == PanelClass::HUB75)
    {
        if constexpr (Cfg.color.swap_rb_pins)
            storage_.rgb_buffer_[fb++] = (rv << 20) | (gv << 10) | bv;
        else
            storage_.rgb_buffer_[fb++] = (bv << 20) | (gv << 10) | rv;
    }
    else
    {
        // CIE-gamma + CCM correction for one PWM pixel, mirroring pack_lut_rgb_()'s HUB75 pipeline
        // but writing 3 separate storage_.rgb_buffer_ entries (R,G,B) instead of one bit-packed uint32_t,
        // since the PWM buffer holds one uint16_t per channel rather than one combined value per pixel.
        storage_.rgb_buffer_[fb++] = static_cast<uint16_t>(rv);
        storage_.rgb_buffer_[fb++] = static_cast<uint16_t>(gv);
        storage_.rgb_buffer_[fb++] = static_cast<uint16_t>(bv);
    }
}

// Returns the flat src-buffer index for display coordinate (dx, dy)
template <Hub75Config Cfg>
constexpr int Hub75Driver<Cfg>::rotated_src_index(int dx, int dy, int dw, int dh)
{
    if constexpr (Cfg.screen.rotation == Hub75Rotation::DEG_90)
    {
        // CW 90 deg: src(x,y) = (dy, dw - 1 - dx); src_width = dh
        return (dw - 1 - dx) * dh + dy;
    }
    else if constexpr (Cfg.screen.rotation == Hub75Rotation::DEG_180)
    {
        return (dh - 1 - dy) * dw + (dw - 1 - dx);
    }
    else if constexpr (Cfg.screen.rotation == Hub75Rotation::DEG_270)
    {
        // CW 270 deg (= CCW 90 deg): src(x,y) = (dh-1-dy, dx); src_width = dh
        return dx * dh + (dh - 1 - dy);
    }
    else
    {
        return dy * dw + dx;
    }
}

// 0xRRGGBB source (PicoGraphics)
template <Hub75Config Cfg>
inline void Hub75Driver<Cfg>::write_pixel(size_t &fb, const uint32_t *src, int dx_base, int dy, int i, int W, int H)
{
    const uint32_t px = src[rotated_src_index(dx_base + i, dy, W, H)];
    store_pixel_(fb, uint8_t(px >> 16), uint8_t(px >> 8), uint8_t(px));
}

// BGR888 byte triples
template <Hub75Config Cfg>
inline void Hub75Driver<Cfg>::write_pixel(size_t &fb, const uint8_t *src, int dx_base, int dy, int i, int W, int H)
{
    const uint8_t *s = &src[rotated_src_index(dx_base + i, dy, W, H) * 3];
    store_pixel_(fb, s[2], s[1], s[0]);
}

// v[0..5] = top R,G,B, bottom R,G,B (16-bit, already corrected). Writes 16 bytes, MSB first.
static inline void pwm_expand_channels_(const uint16_t *v, uint8_t *dst)
{
    for (uint32_t bit = 0; bit < 16; ++bit)
    {
        const uint32_t sh = 15u - bit;
        uint32_t w = 0;
        for (uint32_t k = 0; k < 6; ++k)
            w |= ((static_cast<uint32_t>(v[k]) >> sh) & 1u) << ICND2153_LANE_OF[k];
        dst[bit] = static_cast<uint8_t>(w);
    }
}

template <Hub75Config Cfg>
__attribute__((optimize("unroll-loops"))) void Hub75Driver<Cfg>::build_pixel_stream()
{
    if constexpr (Cfg.panel.panel_class == PanelClass::PWM)
    {
        static_assert(ROWS_IN_PARALLEL == 2, "PWM stream assumes 2 parallel rows");
        static_assert(Cfg.color.bitplanes == 16, "PWM stream carries 16 bit per channel");

        const uint32_t scale = pwm_scale_q16_(); // 0..65536

        uint8_t *const out = frame_buffer_;

        constexpr uint32_t CHIPS_PER_PANEL = Cfg.panel.matrix_panel_width / 16;
        constexpr uint32_t PANELS = Cfg.panel.chain_rows * Cfg.panel.chain_cols;
        constexpr uint32_t CHIPS_PER_LANE = PANELS * CHIPS_PER_PANEL; // == CHAIN_WIDTH / 16

        uint32_t fb_index = 0;

        out[fb_index++] = (SCAN_DEPTH * 16 - 1) & 0xFF;
        out[fb_index++] = ((SCAN_DEPTH * 16 - 1) >> 8) & 0xFF;
        out[fb_index++] = (CHAIN_WIDTH - 2) & 0xFF;
        out[fb_index++] = ((CHAIN_WIDTH - 2) >> 8) & 0xFF;

        for (uint32_t row = 0; row < SCAN_DEPTH; ++row)
        {
            for (uint32_t channel = 0; channel < 16; ++channel) // first packet = OUT15
            {
                for (uint32_t m = 0; m < CHIPS_PER_LANE; ++m) // m = position in the transaction, first-sent first
                {
                    // Chip whose pixels travel at this position.
                    // Panel order can be flipped without touching the chip order inside a panel.
                    const uint32_t chip = Cfg.panel.pwm_reverse_chain_order
                                              ? (PANELS - 1 - m / CHIPS_PER_PANEL) * CHIPS_PER_PANEL + m % CHIPS_PER_PANEL
                                              : m;

                    // position inside this address row's pixel stream; update_bgr() already put
                    // panels, serpentine reversal and the paired rows into this order
                    const uint32_t pos = chip * 16 + channel;
                    const uint16_t *p = &storage_.rgb_buffer_[((row * CHAIN_WIDTH + pos) * ROWS_IN_PARALLEL) * 3];
                    if (scale >= 65536u)
                        pwm_expand_channels_(p, &out[fb_index]);
                    else
                    {
                        uint16_t v[6];
                        for (uint32_t k = 0; k < 6; ++k)
                            v[k] = static_cast<uint16_t>((static_cast<uint32_t>(p[k]) * scale + 32768u) >> 16);
                        pwm_expand_channels_(v, &out[fb_index]);
                    }
                    fb_index += 16;
                }
            }
        }

        __dmb();
        swap_frame_buffer_pending_ = true;
    }
}

// Calculate offset for current row in panel with coordinates (v, h) in positive or negative
// ('reverse') direction.
template <Hub75Config Cfg>
int32_t Hub75Driver<Cfg>::map_panel_row(int row, int v, int h, bool reverse)
{
    // Reverse physical panel column order for serpentine odd chain rows
    const int32_t phys_h = reverse ? (static_cast<int32_t>(Cfg.panel.chain_cols) - 1 - h) : h;

    // Reverse over full panel height (not just SCAN_DEPTH) so that combined with a negative
    // stride_to_paired_row step in the caller, both paired rows land at the correct mirrored
    // source positions.
    const int32_t local_row = reverse ? (static_cast<int32_t>(Cfg.panel.matrix_panel_height) - 1 - row) : row;

    // Top-left pixel of this panel in the row-major source framebuffer:
    //   v panels down     -> v * matrix_panel_height full source rows
    //   phys_h panels right -> phys_h * matrix_panel_width columns
    const int32_t panel_top_left = v * static_cast<int32_t>(Cfg.panel.matrix_panel_height * DISPLAY_WIDTH) +
                                   phys_h * static_cast<int32_t>(Cfg.panel.matrix_panel_width);

    return panel_top_left + local_row * static_cast<int32_t>(DISPLAY_WIDTH);
}

// -----------------------------------------------------------------------------------------
// Frame buffer updates - map logical framebuffer into HUB75 scanline order
// -----------------------------------------------------------------------------------------

// Updates the frame buffer from a source array of BGR888 byte-triples, CIE-corrected and
// interleaved into the layout required by the configured panel_kind.
template <Hub75Config Cfg>
void Hub75Driver<Cfg>::update_bgr(const uint8_t *src)
{
    constexpr int W = DISPLAY_WIDTH;
    constexpr int H = DISPLAY_HEIGHT;

    if constexpr (Cfg.panel.panel_kind == RowMapping::Standard)
    {
        // U-Type Serpentine Chaining (BGR byte layout). Step between paired rows within a
        // single panel's SCAN_DEPTH - not DISPLAY_HEIGHT / ROWS_IN_PARALLEL. The two only
        // coincide when chain_rows == 1.
        constexpr int rows_per_bank = SCAN_DEPTH;

        size_t fb_index = 0;

        for (int row = 0; row < rows_per_bank; row++)
        {
            for (int v = 0; v < static_cast<int>(Cfg.panel.chain_rows); v++)
            {
                const bool reverse = (Cfg.panel.chain_mode == Hub75ChainMode::SERPENTINE) && (v & 1);

                for (int h = 0; h < static_cast<int>(Cfg.panel.chain_cols); h++)
                {
                    const int32_t row_base = map_panel_row(row, v, h, reverse);

                    // row_base (pixel-domain) is only guaranteed W-aligned when
                    // chain_cols == 1; decompose fully once per (row, v, h).
                    const int dx_base = row_base % W;
                    const int dy_base = row_base / W;

                    if (reverse)
                    {
                        for (int i = static_cast<int>(Cfg.panel.matrix_panel_width) - 1; i >= 0; --i)
                        {
                            for (int p = 0; p < static_cast<int>(ROWS_IN_PARALLEL); ++p)
                            {
                                const int dy = dy_base - p * rows_per_bank;
                                write_pixel(fb_index, src, dx_base, dy, i, W, H);
                            }
                        }
                    }
                    else
                    {
                        for (int i = 0; i < static_cast<int>(Cfg.panel.matrix_panel_width); ++i)
                        {
                            for (int p = 0; p < static_cast<int>(ROWS_IN_PARALLEL); ++p)
                            {
                                const int dy = dy_base + p * rows_per_bank;
                                write_pixel(fb_index, src, dx_base, dy, i, W, H);
                            }
                        }
                    }
                }
            }
        }
    }
    else if constexpr (Cfg.panel.panel_kind == RowMapping::Split)
    {
        // Split-half mapping. Four rows per address. Used by many P10 outdoor panels with split upper/lower-half addressing.

        constexpr int COLUMN_PAIRS = Cfg.panel.matrix_panel_width >> 1;
        constexpr int HALF_PAIRS = COLUMN_PAIRS >> 1;

        constexpr int PAIR_HALF_BIT = HALF_PAIRS;
        constexpr int PAIR_HALF_SHIFT = __builtin_ctz(HALF_PAIRS);

        constexpr int ROW_STRIDE = Cfg.panel.matrix_panel_width;
        constexpr int ROWS_PER_GROUP = Cfg.panel.matrix_panel_height / SCAN_GROUPS;
        constexpr int GROUP_ROW_OFFSET = ROWS_PER_GROUP * ROW_STRIDE;
        constexpr int HALF_PANEL_OFFSET = (Cfg.panel.matrix_panel_height >> 1) * ROW_STRIDE;

        // Always 4 by construction (HALF_PAIRS == MATRIX_PANEL_WIDTH / 4);
        // spelled out via MATRIX_PANEL_WIDTH/HALF_PAIRS to keep the relationship
        // to the single-panel formula explicit rather than a bare magic number.
        constexpr int NUM_OCTANTS = Cfg.panel.matrix_panel_width / HALF_PAIRS;
        constexpr int NUM_ADDRESSES = Cfg.panel.matrix_panel_height / NUM_OCTANTS;

        static_assert(NUM_ADDRESSES == static_cast<int>(SCAN_DEPTH),
                      "ROW_MAP_SPLIT requires ROWSEL_N_PINS chosen so that "
                      "static_cast<int>(SCAN_DEPTH) == Cfg.panel.matrix_panel_height / 4 "
                      "(this panel family addresses 4 column-octants per row-select address)");

        size_t fb_index = 0;

        for (int address = 0; address < NUM_ADDRESSES; ++address)
        {
            for (int v = 0; v < Cfg.panel.chain_rows; ++v)
            {
                const bool reverse = (Cfg.panel.chain_mode == Hub75ChainMode::SERPENTINE) && (v & 1);

                for (int h = 0; h < Cfg.panel.chain_cols; ++h)
                {
                    const int phys_h = reverse ? (Cfg.panel.chain_cols - 1 - h) : h;

                    for (int octant = 0; octant < NUM_OCTANTS; ++octant)
                    {
                        const int line = address * NUM_OCTANTS + octant;

                        for (int counter = 0; counter < COLUMN_PAIRS; ++counter)
                        {
                            // Panel-native split-half addressing — identical formula
                            // to the single-panel branch above, evaluated per (line, counter)
                            // instead of the flat j counter (line*COLUMN_PAIRS + counter == j).
                            const int32_t local_index = !(counter & PAIR_HALF_BIT) ? (line << PAIR_HALF_SHIFT) + counter : GROUP_ROW_OFFSET + (line << PAIR_HALF_SHIFT) + (counter - HALF_PAIRS);
                            const int32_t local_index2 = local_index + HALF_PANEL_OFFSET;

                            int local_row = local_index / Cfg.panel.matrix_panel_width;
                            int local_col = local_index % Cfg.panel.matrix_panel_width;
                            int local_row2 = local_index2 / Cfg.panel.matrix_panel_width;
                            int local_col2 = local_index2 % Cfg.panel.matrix_panel_width;

                            if (reverse)
                            {
                                // 180° source mirror only — write order/slot stays fixed.
                                local_row = Cfg.panel.matrix_panel_height - 1 - local_row;
                                local_col = Cfg.panel.matrix_panel_width - 1 - local_col;
                                local_row2 = Cfg.panel.matrix_panel_height - 1 - local_row2;
                                local_col2 = Cfg.panel.matrix_panel_width - 1 - local_col2;
                            }

                            const int dx = phys_h * Cfg.panel.matrix_panel_width + local_col;
                            const int dy = v * Cfg.panel.matrix_panel_height + local_row;
                            const int dx2 = phys_h * Cfg.panel.matrix_panel_width + local_col2;
                            const int dy2 = v * Cfg.panel.matrix_panel_height + local_row2;

                            write_pixel(fb_index, src, dx, dy, 0, W, H);
                            write_pixel(fb_index, src, dx2, dy2, 0, W, H);
                        }
                    }
                }
            }
        }
    }
    else // RowMapping::S31
    {
        // P3 chained, with display rotation support (BGR byte layout). U-Type Serpentine

        size_t fb_index = 0;

        for (int row = 0; row < SCAN_DEPTH; row++)
        {
            for (int v = 0; v < static_cast<int>(Cfg.panel.chain_rows); v++)
            {
                const bool reverse = (Cfg.panel.chain_mode == Hub75ChainMode::SERPENTINE) && (v & 1);

                for (int h = 0; h < static_cast<int>(Cfg.panel.chain_cols); h++)
                {
                    const int32_t row_base = map_panel_row(row, v, h, reverse);

                    // S31 quarter-row layout
                    const int32_t sign = reverse ? -1 : 1;
                    const int32_t base0 = row_base + sign * 0 * stride_to_paired_row;
                    const int32_t base1 = row_base + sign * 1 * stride_to_paired_row;
                    const int32_t base2 = row_base + sign * 2 * stride_to_paired_row;
                    const int32_t base3 = row_base + sign * 3 * stride_to_paired_row;

                    // baseN is only guaranteed W-aligned when chain_cols == 1.
                    // Decompose fully (dx_base AND dy) once per quarter-row.
                    const int dx_base0 = base0 % W, dy0 = base0 / W;
                    const int dx_base1 = base1 % W, dy1 = base1 / W;
                    const int dx_base2 = base2 % W, dy2 = base2 / W;
                    const int dx_base3 = base3 % W, dy3 = base3 / W;

                    if (reverse)
                    {
                        for (int i = static_cast<int>(Cfg.panel.matrix_panel_width) - 1; i >= 0; --i)
                        {
                            write_pixel(fb_index, src, dx_base1, dy1, i, W, H);
                            write_pixel(fb_index, src, dx_base3, dy3, i, W, H);
                        }
                        for (int i = static_cast<int>(Cfg.panel.matrix_panel_width) - 1; i >= 0; --i)
                        {
                            write_pixel(fb_index, src, dx_base0, dy0, i, W, H);
                            write_pixel(fb_index, src, dx_base2, dy2, i, W, H);
                        }
                    }
                    else
                    {
                        for (int i = 0; i < static_cast<int>(Cfg.panel.matrix_panel_width); ++i)
                        {
                            write_pixel(fb_index, src, dx_base1, dy1, i, W, H);
                            write_pixel(fb_index, src, dx_base3, dy3, i, W, H);
                        }

                        for (int i = 0; i < static_cast<int>(Cfg.panel.matrix_panel_width); ++i)
                        {
                            write_pixel(fb_index, src, dx_base0, dy0, i, W, H);
                            write_pixel(fb_index, src, dx_base2, dy2, i, W, H);
                        }
                    }
                }
            }
        }
    }
    if constexpr (Cfg.panel.panel_class == PanelClass::HUB75)
    {
        // Kick off building bitplanes from storage_.rgb_buffer_ to be written to frame_buffer_
        dma_channel_set_write_addr(write_chan_, frame_buffer_, false);
        dma_channel_set_read_addr(read_chan_, storage_.rgb_buffer_, false);
        dma_start_channel_mask((1u << read_chan_) | (1u << write_chan_));
    }
    else
    {
        build_pixel_stream();
    }
}

#if USE_PICO_GRAPHICS == true
// Updates the frame buffer from a PicoGraphics source (RGB888 / packed 32-bit), CIE-corrected
// and interleaved into the layout required by the configured panel_kind.
template <Hub75Config Cfg>
void Hub75Driver<Cfg>::update(pimoroni::PicoGraphics const *graphics)
{
    if (graphics->pen_type != pimoroni::PicoGraphics::PEN_RGB888)
        return;

    if (graphics->bounds.w != static_cast<int>(SCREEN_WIDTH) || graphics->bounds.h != static_cast<int>(SCREEN_HEIGHT))
    {
        printf("\n[HUB75 ERROR] Dimension Mismatch!\n");
        printf("Expected: %ux%u, Got: %dx%d\n", SCREEN_WIDTH, SCREEN_HEIGHT, graphics->bounds.w, graphics->bounds.h);

        const char *const error_msg =
            (Cfg.screen.rotation == Hub75Rotation::DEG_90 || Cfg.screen.rotation == Hub75Rotation::DEG_270)
                ? "For rotation 90/270, width must be DISPLAY_HEIGHT and height must be DISPLAY_WIDTH!"
                : "For rotation 0/180, width must be DISPLAY_WIDTH and height must be DISPLAY_HEIGHT!";

        // Hard panic halts both pico cores and prints a clean debug trace over the terminal
        panic(error_msg);
    }

    uint32_t const *src = static_cast<uint32_t const *>(graphics->frame_buffer);

    constexpr int W = DISPLAY_WIDTH;
    constexpr int H = DISPLAY_HEIGHT;

    if constexpr (Cfg.panel.panel_kind == RowMapping::Standard)
    {
        // U-Type Serpentine Chaining.
        //
        // Example: six matrix panels of width 32 columns and height 32 rows are chained
        // as: 0 -> 1 -> 2 -> 3 -> 4 -> 5. This results in a long matrix panel with 192
        // columns and 32 rows. To get a rectangular 64x96 chained matrix panel instead,
        // align the panels with unmodified connections:
        //
        //                       0 -> 1 U-turn to panel 2
        //                            |
        //                            v
        //    U-turn to panel 4  3 <- 2
        //                       |
        //                       v
        //                       4 -> 5
        //
        // The connections between each of the panels remain unchanged, but now content of
        // panels 2 and 3 is rotated 180 deg and panel 2 sits below panel 1, panel 3 below panel 0.
        // The next U-turn positions panel 4 below panel 3 and panel 5 below panel 2.
        // We compensate the physical rotation with a software rotation.

        // NOTE: rows_per_bank is the step between paired rows *within a single panel's
        // SCAN_DEPTH*, not DISPLAY_HEIGHT / ROWS_IN_PARALLEL. The two coincide only when
        // chain_rows == 1. SCAN_DEPTH is the authoritative source.
        constexpr int rows_per_bank = SCAN_DEPTH;

        size_t fb_index = 0;

        for (int row = 0; row < rows_per_bank; row++) // row: current row
        {
            for (int v = 0; v < static_cast<int>(Cfg.panel.chain_rows); v++) // v: panel in row (vertical chain)
            {
                const bool reverse = (Cfg.panel.chain_mode == Hub75ChainMode::SERPENTINE) ? (v & 1) : false;

                for (int h = 0; h < static_cast<int>(Cfg.panel.chain_cols); h++) // h: panel in column (horizontal chain)
                {
                    // row_base: row offset for panel coordinates (v, h), reverse: U-turn descriptor
                    const int32_t row_base = map_panel_row(row, v, h, reverse);

                    // row_base is only guaranteed W-aligned when chain_cols == 1
                    // (phys_h * matrix_panel_width is otherwise a sub-row offset).
                    const int dx_base = row_base % W;
                    const int dy_base = row_base / W;

                    if (reverse)
                    {
                        // Serpentine physical 180 deg correction (chain topology):
                        //   - scan row reversed  -> map_panel_row
                        //   - i traversal        -> reversed below
                        //   - multiplex ordering -> reversed below
                        for (int i = static_cast<int>(Cfg.panel.matrix_panel_width) - 1; i >= 0; --i)
                        {
                            for (int p = 0; p < static_cast<int>(ROWS_IN_PARALLEL); ++p)
                            {
                                const int dy = dy_base - p * rows_per_bank;
                                write_pixel(fb_index, src, dx_base, dy, i, W, H);
                            }
                        }
                    }
                    else
                    {
                        for (int i = 0; i < static_cast<int>(Cfg.panel.matrix_panel_width); ++i)
                        {
                            for (int p = 0; p < static_cast<int>(ROWS_IN_PARALLEL); ++p)
                            {
                                const int dy = dy_base + p * rows_per_bank;
                                write_pixel(fb_index, src, dx_base, dy, i, W, H);
                            }
                        }
                    }
                }
            }
        }
    }
    else if constexpr (Cfg.panel.panel_kind == RowMapping::Split)
    {
        // Split-half mapping. Four rows per address. Used by many P10 outdoor panels with split upper/lower-half addressing.

        constexpr int COLUMN_PAIRS = Cfg.panel.matrix_panel_width >> 1;
        constexpr int HALF_PAIRS = COLUMN_PAIRS >> 1;

        constexpr int PAIR_HALF_BIT = HALF_PAIRS;
        constexpr int PAIR_HALF_SHIFT = __builtin_ctz(HALF_PAIRS);

        constexpr int ROW_STRIDE = Cfg.panel.matrix_panel_width;
        constexpr int ROWS_PER_GROUP = Cfg.panel.matrix_panel_height / SCAN_GROUPS;
        constexpr int GROUP_ROW_OFFSET = ROWS_PER_GROUP * ROW_STRIDE;
        constexpr int HALF_PANEL_OFFSET = (Cfg.panel.matrix_panel_height >> 1) * ROW_STRIDE;

        // Always 4 by construction (HALF_PAIRS == MATRIX_PANEL_WIDTH / 4);
        // spelled out via MATRIX_PANEL_WIDTH/HALF_PAIRS to keep the relationship to the single-panel formula explicit rather than a bare magic number.
        constexpr int NUM_OCTANTS = Cfg.panel.matrix_panel_width / HALF_PAIRS;
        constexpr int NUM_ADDRESSES = Cfg.panel.matrix_panel_height / NUM_OCTANTS;

        static_assert(NUM_ADDRESSES == static_cast<int>(SCAN_DEPTH),
                      "ROW_MAP_SPLIT requires ROWSEL_N_PINS chosen so that "
                      "static_cast<int>(SCAN_DEPTH) == Cfg.panel.matrix_panel_height / 4 "
                      "(this panel family addresses 4 column-octants per row-select address)");

        size_t fb_index = 0;

        for (int address = 0; address < NUM_ADDRESSES; ++address)
        {
            for (int v = 0; v < static_cast<int>(Cfg.panel.chain_rows); ++v)
            {
                const bool reverse = (Cfg.panel.chain_mode == Hub75ChainMode::SERPENTINE) && (v & 1);

                for (int h = 0; h < static_cast<int>(Cfg.panel.chain_cols); ++h)
                {
                    const int phys_h = reverse ? (static_cast<int>(Cfg.panel.chain_cols) - 1 - h) : h;

                    for (int octant = 0; octant < NUM_OCTANTS; ++octant)
                    {
                        const int line = address * NUM_OCTANTS + octant;

                        for (int counter = 0; counter < COLUMN_PAIRS; ++counter)
                        {
                            // Panel-native split-half addressing — identical formula to the single-panel branch above, evaluated per (line, counter)
                            // instead of the flat j counter (line*COLUMN_PAIRS + counter == j).
                            const int32_t local_index = !(counter & PAIR_HALF_BIT) ? (line << PAIR_HALF_SHIFT) + counter : GROUP_ROW_OFFSET + (line << PAIR_HALF_SHIFT) + (counter - HALF_PAIRS);
                            const int32_t local_index2 = local_index + HALF_PANEL_OFFSET;

                            int local_row = local_index / Cfg.panel.matrix_panel_width;
                            int local_col = local_index % Cfg.panel.matrix_panel_width;
                            int local_row2 = local_index2 / Cfg.panel.matrix_panel_width;
                            int local_col2 = local_index2 % Cfg.panel.matrix_panel_width;

                            if (reverse)
                            {
                                // 180° source mirror only — write order/slot stays fixed.
                                local_row = Cfg.panel.matrix_panel_height - 1 - local_row;
                                local_col = Cfg.panel.matrix_panel_width - 1 - local_col;
                                local_row2 = Cfg.panel.matrix_panel_height - 1 - local_row2;
                                local_col2 = Cfg.panel.matrix_panel_width - 1 - local_col2;
                            }

                            const int dx = phys_h * Cfg.panel.matrix_panel_width + local_col;
                            const int dy = v * Cfg.panel.matrix_panel_height + local_row;
                            const int dx2 = phys_h * Cfg.panel.matrix_panel_width + local_col2;
                            const int dy2 = v * Cfg.panel.matrix_panel_height + local_row2;

                            write_pixel(fb_index, src, dx, dy, 0, W, H);
                            write_pixel(fb_index, src, dx2, dy2, 0, W, H);
                        }
                    }
                }
            }
        }
    }
    else // RowMapping::S31
    {
        // P3 chained, with display rotation support.

        size_t fb_index = 0;

        for (int row = 0; row < static_cast<int>(SCAN_DEPTH); row++)
        {
            for (int v = 0; v < static_cast<int>(Cfg.panel.chain_rows); v++)
            {
                const bool reverse = (Cfg.panel.chain_mode == Hub75ChainMode::SERPENTINE) && (v & 1);

                for (int h = 0; h < static_cast<int>(Cfg.panel.chain_cols); h++)
                {
                    const int32_t row_base = map_panel_row(row, v, h, reverse);

                    // S31 quarter-row layout
                    const int32_t sign = reverse ? -1 : 1;
                    const int32_t base0 = row_base + sign * 0 * stride_to_paired_row;
                    const int32_t base1 = row_base + sign * 1 * stride_to_paired_row;
                    const int32_t base2 = row_base + sign * 2 * stride_to_paired_row;
                    const int32_t base3 = row_base + sign * 3 * stride_to_paired_row;

                    const int dx_base0 = base0 % W, dy0 = base0 / W;
                    const int dx_base1 = base1 % W, dy1 = base1 / W;
                    const int dx_base2 = base2 % W, dy2 = base2 / W;
                    const int dx_base3 = base3 % W, dy3 = base3 / W;

                    if (reverse)
                    {
                        // Serpentine physical 180 deg correction (chain topology):
                        //   - scan row reversed    -> map_panel_row
                        //   - i reversed           -> below
                        //   - sign on quarter rows -> above
                        for (int i = static_cast<int>(Cfg.panel.matrix_panel_width) - 1; i >= 0; --i)
                        {
                            write_pixel(fb_index, src, dx_base1, dy1, i, W, H);
                            write_pixel(fb_index, src, dx_base3, dy3, i, W, H);
                        }
                        for (int i = static_cast<int>(Cfg.panel.matrix_panel_width) - 1; i >= 0; --i)
                        {
                            write_pixel(fb_index, src, dx_base0, dy0, i, W, H);
                            write_pixel(fb_index, src, dx_base2, dy2, i, W, H);
                        }
                    }
                    else
                    {
                        for (int i = 0; i < static_cast<int>(Cfg.panel.matrix_panel_width); ++i)
                        {
                            write_pixel(fb_index, src, dx_base1, dy1, i, W, H);
                            write_pixel(fb_index, src, dx_base3, dy3, i, W, H);
                        }
                        for (int i = 0; i < static_cast<int>(Cfg.panel.matrix_panel_width); ++i)
                        {
                            write_pixel(fb_index, src, dx_base0, dy0, i, W, H);
                            write_pixel(fb_index, src, dx_base2, dy2, i, W, H);
                        }
                    }
                }
            }
        }
    }

    if constexpr (Cfg.panel.panel_class == PanelClass::HUB75)
    {
        // Kick off building bitplanes from storage_.rgb_buffer_ to be written to frame_buffer_
        dma_channel_set_write_addr(write_chan_, frame_buffer_, false);
        dma_channel_set_read_addr(read_chan_, storage_.rgb_buffer_, false);
        dma_start_channel_mask((1u << read_chan_) | (1u << write_chan_));
    }
    else
    {
        build_pixel_stream();
    }
}
#endif
