#include "pico/multicore.h"
#include "pico/stdlib.h"
#include <pico/time.h>

#include "examples/bouncing_balls.hpp"
#include "examples/fire_effect.hpp"


#include "hub75.hpp"

// Minimal two-panel demo: two independent Hub75Driver instances running side by side.

// Instance A - panel wired to GPIO 0-13.
constexpr Hub75Config panel_cfg_a{
    .panel = {
        .matrix_panel_width = 64,
        .matrix_panel_height = 64,
        .chain_rows = 1,
        .chain_cols = 1,
        .chain_mode = Hub75ChainMode::SERPENTINE,
        .panel_kind = RowMapping::Standard,
        .panel_chip = Hub75PanelChip::GENERIC,
        .inverted_stb = false,
        .sm_clockdiv_factor = 1.0f,
        .base_latch_ns = 80,
        .base_addr_ns = 120,
    },
    .screen = {
        .rotation = Hub75Rotation::DEG_90,
    },
    .pins = {
        .data_base_pin = 30,
        .data_n_pins = 6,
        .rowsel_base_pin = 36,
        .rowsel_n_pins = 3,
        .clk_pin = 41,
        .strobe_pin = 42,
        .oen_pin = 43,
    },
    .color = {
        .bitplanes = 10,
        .separate_cie_channels = true,
        .balanced_light_output = true,
        .ccm_rg_shift = 6,
        .ccm_gb_shift = 7,
    },
    .frame_rate_debug = true,
};

// Instance B - same color config as A, wired starting 14 pins further along so the two
// panels don't share any GPIO (data 14-19, rowsel 20-24, clk 25, strobe 26, oen 27).
constexpr Hub75Config panel_cfg_b{
    .panel = {
        .matrix_panel_width = 64,
        .matrix_panel_height = 64,
        .chain_rows = 1,
        .chain_cols = 1,
        .chain_mode = Hub75ChainMode::SERPENTINE,
        .panel_kind = RowMapping::Standard,
        .panel_chip = Hub75PanelChip::GENERIC,
        .inverted_stb = false,
        .sm_clockdiv_factor = 1.0f,
        .base_latch_ns = 80,
        .base_addr_ns = 120,
    },
    .screen = {
        .rotation = Hub75Rotation::DEG_270,
    },
    .pins = {
        .data_base_pin = 14,
        .data_n_pins = 6,
        .rowsel_base_pin = 20,
        .rowsel_n_pins = 3,
        .clk_pin = 26,
        .strobe_pin = 27,
        .oen_pin = 28,
    },
    .color = panel_cfg_a.color,
    .frame_rate_debug = false,
};

using PanelA = Hub75Driver<panel_cfg_a>;
using PanelB = Hub75Driver<panel_cfg_b>;

// Large fixed-size buffers - must have static storage duration, not live on the stack.
static PanelA driver_a;
static PanelB driver_b;


void core1_entry()
{
    driver_a.create();
    driver_b.create();

    driver_a.start();
    driver_b.start();

    // KEEP CORE 1 ALIVE - without this, Core 1's NVIC is torn down and the DMA IRQs stop firing.
    while (true)
    {
        tight_loop_contents();
    }
}

int main()
{
    stdio_init_all();

    // Wait up to 2 seconds for USB serial to connect
    absolute_time_t timeout = make_timeout_time_ms(2000);
    while (!stdio_usb_connected() && !time_reached(timeout)) {
        sleep_ms(10);
    }
    printf("USB connected!\n");

    multicore_reset_core1();
    multicore_launch_core1(core1_entry);

    static FireEffect<PanelA::SCREEN_WIDTH, PanelA::SCREEN_HEIGHT> fireEffect;
    static BouncingBalls<PanelB::SCREEN_WIDTH, PanelB::SCREEN_HEIGHT> bouncingBalls(10);


    while (true)
    {
        fireEffect.burn();
        bouncingBalls.bounce();
        driver_a.update(&fireEffect);
        driver_b.update(&bouncingBalls);

        sleep_ms(10);
    }
}
