#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "hardware/clocks.h"
#include "pico/sync.h"

#include "hub75.pio.h"

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
    dma_buffer_ = frame_buffer1_;
    frame_buffer_ = frame_buffer2_;

    dma_row_cmd_buffer_ = row_cmd_buffer1_;
    row_cmd_buffer_ = row_cmd_buffer2_;

    timing_init(clock_get_hz(clk_sys), SM_CLOCKDIV);

    if constexpr (Cfg.panel.panel_chip == Hub75PanelChip::FM6126A)
        FM6126A_setup(Cfg.pins, Cfg.panel.matrix_panel_width);
    else if constexpr (Cfg.panel.panel_chip == Hub75PanelChip::RUL6024)
        RUL6024_setup(Cfg.pins, Cfg.panel.matrix_panel_width);

    configure_pio();
    setup_dma_transfers();
    setup_bitplane_creation();
    setup_display_irq();
    setup_bitplane_stream_irq();
    build_row_cmd_buffer(brightness_fp_);

    register_instance();
}

template <Hub75Config Cfg>
void Hub75Driver<Cfg>::start()
{
    dma_row_cmd_buffer_ = row_cmd_buffer2_;
    row_cmd_buffer_ = row_cmd_buffer1_;

    dma_buffer_ = frame_buffer2_;
    frame_buffer_ = frame_buffer1_;

    swap_row_cmd_buffer_pending_ = false;
    swap_frame_buffer_pending_ = false;

    dma_channel_set_read_addr(row_ctrl_chan_, &dma_row_cmd_buffer_, false);
    dma_channel_set_read_addr(pixel_ctrl_chan_, &dma_buffer_, false);

    dma_channel_set_read_addr(pixel_chan_, dma_buffer_, true);
    dma_channel_set_read_addr(row_chan_, dma_row_cmd_buffer_, true);
}

// -----------------------------------------------------------------------------------------
// Brightness control
// -----------------------------------------------------------------------------------------

template <Hub75Config Cfg>
void Hub75Driver<Cfg>::setBasisBrightness(uint8_t factor)
{
    basis_factor_ = (factor > 0u) ? factor : 1u;
    build_row_cmd_buffer(brightness_fp_);
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

    build_row_cmd_buffer(brightness_fp_);
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
void Hub75Driver<Cfg>::compute_bcm_cycles(uint32_t bitplane, uint32_t brightness_fp, uint32_t &lit, uint32_t &dark) const
{
    // Full BCM period for this bit plane: doubles with each plane (1, 2, 4, 8 ...)
    // scaled by basis_factor_ for coarse panel calibration.
    uint32_t base = (basis_factor_ << bitplane);
    // Lit portion: fraction of the full period during which OEn is asserted.
    // brightness_fp is Q16 fixed-point: 0 = off, 65536 = full brightness.
    lit = (uint32_t)((base * (uint64_t)brightness_fp) >> BRIGHTNESS_FP_SHIFT);
    // Dark portion: remaining time OEn is deasserted (panel off).
    // lit + dark = base, so total period is constant regardless of brightness.
    dark = base - lit;
}

template <Hub75Config Cfg>
uint32_t Hub75Driver<Cfg>::encode_row_address(uint32_t row)
{
    return row & ADDR_MASK;
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

        uint32_t total_lit, total_dark;
        compute_bcm_cycles(bp, brightness_fp, total_lit, total_dark);

        uint32_t base_per_slice = (basis_factor_ << bp) / split_factor;
        uint32_t lit_cycles = (base_per_slice * brightness_fp) >> BRIGHTNESS_FP_SHIFT;
        uint32_t dark_cycles = base_per_slice - lit_cycles;

        for (uint32_t row = 0; row < SCAN_DEPTH; ++row)
        {
            uint32_t t_addr = timing_config_.addr_cycles + (bp >> 1); // address settle
            Hub75RowCmd *cmd = &row_cmd_buffer_[idx++];
            // low 5 bits = row address (hub75_row PIO consumes exactly 5 bits via `out pins, 5`),
            // upper 27 bits = t_addr, taken by the following `out x, 27`
            cmd->addr_delay = (t_addr << 5) | (encode_row_address(row) & 0x1Fu);
            cmd->lit_cycles = lit_cycles;
            cmd->dark_cycles = dark_cycles;
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
            row_cmd_buffer_ = (new_front == row_cmd_buffer1_) ? row_cmd_buffer2_ : row_cmd_buffer1_;
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
            frame_buffer_ = (new_front == frame_buffer1_) ? frame_buffer2_ : frame_buffer1_;
            dma_buffer_ = new_front;
            dma_channel_set_read_addr(pixel_ctrl_chan_, &dma_buffer_, false);

            swap_frame_buffer_pending_ = false;
        }
    }
}

// DMA IRQ1: streaming pipeline for bitplane generation (rgb_buffer_ -> PIO -> frame_buffer_).
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
        dma_channel_set_read_addr(read_chan_, rgb_buffer_, false);
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

template <Hub75Config Cfg>
void Hub75Driver<Cfg>::setup_bitplane_creation()
{
    read_chan_ = dma_claim_unused_channel(true);
    write_chan_ = dma_claim_unused_channel(true);

    // --- READ CHANNEL (Memory -> PIO) ---
    dma_channel_config read_chan_config = dma_channel_get_default_config(read_chan_);
    channel_config_set_transfer_data_size(&read_chan_config, DMA_SIZE_32);
    channel_config_set_read_increment(&read_chan_config, true);
    channel_config_set_write_increment(&read_chan_config, false);
    // DREQ: Wait for PIO TX FIFO space
    channel_config_set_dreq(&read_chan_config, pio_get_dreq(pio_config_.pio_read, pio_config_.sm_read, true));
    channel_config_set_high_priority(&read_chan_config, true);

    dma_channel_configure(
        read_chan_,
        &read_chan_config,
        &pio_config_.pio_read->txf[pio_config_.sm_read], // Write to PIO TX FIFO
        nullptr,                                         // Read address set later
        dma_encode_transfer_count(TOTAL_PIXELS),         // Total pixel (pairs) to process
        false                                            // Don't start yet
    );

    // --- WRITE CHANNEL (PIO -> Memory) ---
    dma_channel_config write_chan_config = dma_channel_get_default_config(write_chan_);
    channel_config_set_transfer_data_size(&write_chan_config, DMA_SIZE_32); // PIO pushes 4 bytes
    channel_config_set_read_increment(&write_chan_config, false);
    channel_config_set_write_increment(&write_chan_config, true);
    // DREQ: Wait for PIO RX FIFO data
    channel_config_set_dreq(&write_chan_config, pio_get_dreq(pio_config_.pio_read, pio_config_.sm_read, false));

    channel_config_set_high_priority(&write_chan_config, true);

    dma_channel_configure(
        write_chan_,
        &write_chan_config,
        nullptr,                                             // Write address set later
        &pio_config_.pio_read->rxf[pio_config_.sm_read],     // Read from PIO RX FIFO
        dma_encode_transfer_count((TOTAL_PIXELS >> 1) >> 2), // Two colour informations per byte (xxr0g0b0r1b1g1) => (TOTAL_PIXELS >> 1)
                                                             // 4 bytes put in a transfered word => ((TOTAL_PIXELS >> 1) >> 2)
        false                                                // Don't start yet
    );
}

template <Hub75Config Cfg>
void Hub75Driver<Cfg>::setup_display_irq()
{
    dma_channel_set_irq0_enabled(row_ctrl_chan_, true);
    dma_channel_set_irq0_enabled(pixel_ctrl_chan_, true);
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
            // Inverted-STB panels are handled by inverting the STROBE pin at the GPIO pad
            // level (see hub75_row_program_init), so there is only one row program.
            bool row_ok = hub75_claim_on_pio(candidate, [&]
                                             { return pio_claim_free_sm_and_add_program_for_gpio_range(
                                                   &hub75_row_program,
                                                   &pio_config_.row_pio,
                                                   &pio_config_.sm_row,
                                                   &pio_config_.row_prog_offs,
                                                   Cfg.pins.rowsel_base_pin,
                                                   Cfg.pins.oen_pin - Cfg.pins.rowsel_base_pin + 1, true); });

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
    hub75_row_program_init(pio_config_.row_pio, pio_config_.sm_row, pio_config_.row_prog_offs, Cfg.pins.rowsel_base_pin, Cfg.pins.rowsel_n_pins, Cfg.pins.strobe_pin, timing_config_.latch_cycles, Cfg.panel.inverted_stb);

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

// Configures multiple DMA channels to transfer pixel data, dummy pixel data, and output
// enable signal, to the PIO state machines controlling the HUB75 matrix. Also configures
// the DMA channel which gets active when an output enable signal has finished.
template <Hub75Config Cfg>
void Hub75Driver<Cfg>::setup_dma_transfers()
{
    row_chan_ = dma_claim_unused_channel(true);
    row_ctrl_chan_ = dma_claim_unused_channel(true);

    // row channel
    dma_channel_config row_chan_config = dma_channel_get_default_config(row_chan_);

    channel_config_set_transfer_data_size(&row_chan_config, DMA_SIZE_32);
    channel_config_set_read_increment(&row_chan_config, true);
    channel_config_set_write_increment(&row_chan_config, false);

    channel_config_set_high_priority(&row_chan_config, true);

    channel_config_set_dreq(&row_chan_config, pio_get_dreq(pio_config_.row_pio, pio_config_.sm_row, true));

    channel_config_set_chain_to(&row_chan_config, row_ctrl_chan_);

    // One big transfer of the complete content of dma_row_cmd_buffer_.
    // The dma_row_cmd_buffer_
    //    - has (mostly) different lit cycles and dark cycles for each bitplane
    //    - has the addresses of each row in each bitplane
    dma_channel_configure(row_chan_,
                          &row_chan_config,
                          &pio_config_.row_pio->txf[pio_config_.sm_row],
                          dma_row_cmd_buffer_,
                          dma_encode_transfer_count(bcm_sequence_length * SCAN_DEPTH * row_cmd_struct_members),
                          false);

    // row ctrl channel
    dma_channel_config row_ctrl_chan_config = dma_channel_get_default_config(row_ctrl_chan_);

    channel_config_set_transfer_data_size(&row_ctrl_chan_config, DMA_SIZE_32);
    channel_config_set_read_increment(&row_ctrl_chan_config, false);
    channel_config_set_write_increment(&row_ctrl_chan_config, false);

    channel_config_set_dreq(&row_ctrl_chan_config, DREQ_FORCE);

    channel_config_set_high_priority(&row_ctrl_chan_config, true);

    channel_config_set_chain_to(&row_ctrl_chan_config, row_chan_);

    // When row_chan_ has finished a complete frame (each row in each bitplane) has been emitted.
    // The row_ctrl_chan_ resets the start address of row_chan_ to dma_row_cmd_buffer_.
    dma_channel_configure(row_ctrl_chan_, &row_ctrl_chan_config, &dma_hw->ch[row_chan_].read_addr, dma_row_cmd_buffer_, dma_encode_transfer_count(1), false);

    // pixel channel
    pixel_chan_ = dma_claim_unused_channel(true);
    pixel_ctrl_chan_ = dma_claim_unused_channel(true);

    dma_channel_config pixel_chan_config = dma_channel_get_default_config(pixel_chan_);

    channel_config_set_transfer_data_size(&pixel_chan_config, DMA_SIZE_8);
    channel_config_set_read_increment(&pixel_chan_config, true);
    channel_config_set_write_increment(&pixel_chan_config, false);

    channel_config_set_dreq(&pixel_chan_config, pio_get_dreq(pio_config_.data_pio, pio_config_.sm_data, true));

    channel_config_set_high_priority(&pixel_chan_config, true);

    channel_config_set_chain_to(&pixel_chan_config, pixel_ctrl_chan_);

    // Due to DMA channel row_chan_ the complete pre-build bit planes can be passed to DMA channel pixel_chan_.
    // The pixel_chan_ iterates over all bitplanes in one big swoop.
    dma_channel_configure(pixel_chan_,
                          &pixel_chan_config,
                          &pio_config_.data_pio->txf[pio_config_.sm_data],
                          dma_buffer_,
                          dma_encode_transfer_count((TOTAL_PIXELS >> 1) * bcm_sequence_length),
                          false);

    // pixel ctrl channel
    dma_channel_config pixel_ctrl_chan_config = dma_channel_get_default_config(pixel_ctrl_chan_);

    channel_config_set_transfer_data_size(&pixel_ctrl_chan_config, DMA_SIZE_32);
    channel_config_set_read_increment(&pixel_ctrl_chan_config, false);
    channel_config_set_write_increment(&pixel_ctrl_chan_config, false);

    channel_config_set_dreq(&pixel_ctrl_chan_config, DREQ_FORCE);

    channel_config_set_high_priority(&pixel_ctrl_chan_config, true);

    channel_config_set_chain_to(&pixel_ctrl_chan_config, pixel_chan_);

    // When pixel_chan_ has finished a complete frame (each row in each bitplane) has been emitted.
    // The pixel_ctrl_chan_ resets the start address of pixel_chan_ to dma_buffer_.
    dma_channel_configure(pixel_ctrl_chan_, &pixel_ctrl_chan_config, &dma_hw->ch[pixel_chan_].read_addr, dma_buffer_, dma_encode_transfer_count(1), false);

    pio_sm_set_clkdiv(pio_config_.data_pio, pio_config_.sm_data, SM_CLOCKDIV);
    pio_sm_set_clkdiv(pio_config_.row_pio, pio_config_.sm_row, SM_CLOCKDIV);
}

// -----------------------------------------------------------------------------------------
// Colour pipeline
// -----------------------------------------------------------------------------------------

template <Hub75Config Cfg>
constexpr const uint16_t *Hub75Driver<Cfg>::cie_red_table()
{
    if constexpr (Cfg.color.separate_cie_channels)
        return (Cfg.color.bitplanes == 10) ? CIE10_RED : CIE8_RED;
    else
        return (Cfg.color.bitplanes == 10) ? CIE10 : CIE8;
}

template <Hub75Config Cfg>
constexpr const uint16_t *Hub75Driver<Cfg>::cie_green_table()
{
    if constexpr (Cfg.color.separate_cie_channels)
        return (Cfg.color.bitplanes == 10) ? CIE10_GREEN : CIE8_GREEN;
    else
        return (Cfg.color.bitplanes == 10) ? CIE10 : CIE8;
}

template <Hub75Config Cfg>
constexpr const uint16_t *Hub75Driver<Cfg>::cie_blue_table()
{
    if constexpr (Cfg.color.separate_cie_channels)
        return (Cfg.color.bitplanes == 10) ? CIE10_BLUE : CIE8_BLUE;
    else
        return (Cfg.color.bitplanes == 10) ? CIE10 : CIE8;
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

// Apply LUT and pack into 30-bit RGB (10 bits per channel)
template <Hub75Config Cfg>
uint32_t Hub75Driver<Cfg>::pack_lut_rgb(uint32_t colour)
{
    uint32_t rv = cie_red_table()[(colour >> 16u) & 0xFFu];
    uint32_t gv = cie_green_table()[(colour >> 8u) & 0xFFu];
    uint32_t bv = cie_blue_table()[colour & 0xFFu];
    apply_ccm(rv, gv, bv);
    return (bv << 20u) | (gv << 10u) | rv;
}

// Apply LUT and pack into 30-bit RGB (10 bits per channel)
template <Hub75Config Cfg>
uint32_t Hub75Driver<Cfg>::pack_lut_rgb_(uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t rv = cie_red_table()[r];
    uint32_t gv = cie_green_table()[g];
    uint32_t bv = cie_blue_table()[b];
    apply_ccm(rv, gv, bv);
    return (bv << 20u) | (gv << 10u) | rv;
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

// Shared rotation-lookup helper for the chained-panel branches: a flat panel-side index
// `dx_base` is only guaranteed to be a multiple of W when chain_cols == 1; for chain_cols > 1
// map_panel_row() returns a row_base that already carries a horizontal panel offset, which is
// NOT, in general, a multiple of DISPLAY_WIDTH. Callers decompose the row_base into
// (dx_base, dy) once per row-group and pass the column-local `i` here.
template <Hub75Config Cfg>
uint32_t Hub75Driver<Cfg>::rot_lut(const uint32_t *src, int dx_base, int dy, int i, int W, int H)
{
    return pack_lut_rgb(src[rotated_src_index(dx_base + i, dy, W, H)]);
}

// BGR/uint8_t* byte-triple variant for update_bgr()
template <Hub75Config Cfg>
uint32_t Hub75Driver<Cfg>::rot_lut_rgb(const uint8_t *src, int dx_base, int dy, int i, int W, int H)
{
    const int32_t rot = rotated_src_index(dx_base + i, dy, W, H) * 3;
    return pack_lut_rgb_(src[rot + 2], src[rot + 1], src[rot]);
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
        if constexpr (Cfg.panel.chain_cols == 1 && Cfg.panel.chain_rows == 1)
        {
            // Single panel, with display rotation support (BGR byte layout).
            constexpr int rows_per_bank = H / ROWS_IN_PARALLEL;

            int32_t fb_index = 0;
            int dx = 0;
            int row_in_bank = 0;

            for (int32_t i = 0; i < stride_to_paired_row; ++i)
            {
                for (int p = 0; p < static_cast<int>(ROWS_IN_PARALLEL); ++p)
                {
                    const int dy = p * rows_per_bank + row_in_bank;
                    rgb_buffer_[fb_index++] = rot_lut_rgb(src, dx, dy, 0, W, H);
                }

                if (++dx == W)
                {
                    dx = 0;
                    ++row_in_bank;
                }
            }
        }
        else
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
                                    rgb_buffer_[fb_index++] = rot_lut_rgb(src, dx_base, dy, i, W, H);
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
                                    rgb_buffer_[fb_index++] = rot_lut_rgb(src, dx_base, dy, i, W, H);
                                }
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

                            rgb_buffer_[fb_index++] = rot_lut_rgb(src, dx, dy, 0, W, H);
                            rgb_buffer_[fb_index++] = rot_lut_rgb(src, dx2, dy2, 0, W, H);
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

                    // S31 quarter-row layout (pixel domain - rot_lut_rgb converts to bytes)
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
                            rgb_buffer_[fb_index++] = rot_lut_rgb(src, dx_base1, dy1, i, W, H);
                            rgb_buffer_[fb_index++] = rot_lut_rgb(src, dx_base3, dy3, i, W, H);
                        }
                        for (int i = static_cast<int>(Cfg.panel.matrix_panel_width) - 1; i >= 0; --i)
                        {
                            rgb_buffer_[fb_index++] = rot_lut_rgb(src, dx_base0, dy0, i, W, H);
                            rgb_buffer_[fb_index++] = rot_lut_rgb(src, dx_base2, dy2, i, W, H);
                        }
                    }
                    else
                    {
                        for (int i = 0; i < static_cast<int>(Cfg.panel.matrix_panel_width); ++i)
                        {
                            rgb_buffer_[fb_index++] = rot_lut_rgb(src, dx_base1, dy1, i, W, H);
                            rgb_buffer_[fb_index++] = rot_lut_rgb(src, dx_base3, dy3, i, W, H);
                        }

                        for (int i = 0; i < static_cast<int>(Cfg.panel.matrix_panel_width); ++i)
                        {
                            rgb_buffer_[fb_index++] = rot_lut_rgb(src, dx_base0, dy0, i, W, H);
                            rgb_buffer_[fb_index++] = rot_lut_rgb(src, dx_base2, dy2, i, W, H);
                        }
                    }
                }
            }
        }
    }

    // Kick off building bitplanes from rgb_buffer_ to be written to frame_buffer_
    dma_channel_set_write_addr(write_chan_, frame_buffer_, false);
    dma_channel_set_read_addr(read_chan_, rgb_buffer_, false);
    dma_start_channel_mask((1u << read_chan_) | (1u << write_chan_));
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
        if constexpr (Cfg.panel.chain_cols == 1 && Cfg.panel.chain_rows == 1)
        {
            // Single panel, with display rotation support.
            constexpr int rows_per_bank = H / ROWS_IN_PARALLEL;

            int32_t fb_index = 0;
            int dx = 0;          // column:       0 .. W-1, then wraps
            int row_in_bank = 0; // row within one bank: 0 .. rows_per_bank-1

            for (int32_t i = 0; i < stride_to_paired_row; ++i)
            {
                for (int p = 0; p < static_cast<int>(ROWS_IN_PARALLEL); ++p)
                {
                    // dy = which display row: bank p starts at p * rows_per_bank
                    const int dy = p * rows_per_bank + row_in_bank;
                    rgb_buffer_[fb_index++] = pack_lut_rgb(src[rotated_src_index(dx, dy, W, H)]);
                }

                // Advance column; roll over into next row-within-bank
                if (++dx == W)
                {
                    dx = 0;
                    ++row_in_bank; // at most H/ROWS_IN_PARALLEL increments total
                }
            }
        }
        else
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

            int32_t fb_index = 0;

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
                            // Display rotation is composited independently via rot_lut().
                            for (int i = static_cast<int>(Cfg.panel.matrix_panel_width) - 1; i >= 0; --i)
                            {
                                for (int p = 0; p < static_cast<int>(ROWS_IN_PARALLEL); ++p)
                                {
                                    const int dy = dy_base - p * rows_per_bank;
                                    rgb_buffer_[fb_index++] = rot_lut(src, dx_base, dy, i, W, H);
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
                                    rgb_buffer_[fb_index++] = rot_lut(src, dx_base, dy, i, W, H);
                                }
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

                            rgb_buffer_[fb_index++] = rot_lut(src, dx, dy, 0, W, H);
                            rgb_buffer_[fb_index++] = rot_lut(src, dx2, dy2, 0, W, H);
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
                            rgb_buffer_[fb_index++] = rot_lut(src, dx_base1, dy1, i, W, H);
                            rgb_buffer_[fb_index++] = rot_lut(src, dx_base3, dy3, i, W, H);
                        }
                        for (int i = static_cast<int>(Cfg.panel.matrix_panel_width) - 1; i >= 0; --i)
                        {
                            rgb_buffer_[fb_index++] = rot_lut(src, dx_base0, dy0, i, W, H);
                            rgb_buffer_[fb_index++] = rot_lut(src, dx_base2, dy2, i, W, H);
                        }
                    }
                    else
                    {
                        for (int i = 0; i < static_cast<int>(Cfg.panel.matrix_panel_width); ++i)
                        {
                            rgb_buffer_[fb_index++] = rot_lut(src, dx_base1, dy1, i, W, H);
                            rgb_buffer_[fb_index++] = rot_lut(src, dx_base3, dy3, i, W, H);
                        }
                        for (int i = 0; i < static_cast<int>(Cfg.panel.matrix_panel_width); ++i)
                        {
                            rgb_buffer_[fb_index++] = rot_lut(src, dx_base0, dy0, i, W, H);
                            rgb_buffer_[fb_index++] = rot_lut(src, dx_base2, dy2, i, W, H);
                        }
                    }
                }
            }
        }
    }

    // Kick off building bitplanes from rgb_buffer_ to be written to frame_buffer_
    dma_channel_set_write_addr(write_chan_, frame_buffer_, false);
    dma_channel_set_read_addr(read_chan_, rgb_buffer_, false);
    dma_start_channel_mask((1u << read_chan_) | (1u << write_chan_));
}
#endif
