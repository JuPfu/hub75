# Dual-Instance Configuration

Because the panel/pin/colour/screen setup is now a **template argument** to `Hub75Driver<Cfg>`,
each distinct `constexpr Hub75Config` value produces its own driver **type**. This makes it
possible to run **two independently-configured HUB75 chains** — different dimensions, different
GPIO ranges, different chain layout, different rotation, even different colour tuning — from a
single RP2350, side by side in the same program. See `hub75_demo_dual.cpp` for a complete,
runnable example.

## Resource Limits

Each `Hub75Driver<Cfg>` instance claims:

- **6 DMA channels** (`row_chan_`, `row_ctrl_chan_`, `pixel_chan_`, `pixel_ctrl_chan_`,
  `read_chan_`, `write_chan_`) — RP2350 has 16 channels total, so **2 instances (12 channels)
  is the practical maximum** that fits.
- **A dedicated PIO block pair** for `hub75_row`/`hub75_row_inverted` and
  `hub75_bitplane_stream` (these two programs must share one physical PIO block per instance).
  A second instance's row+stream pair can never land on the same PIO block as the first — the
  driver enforces this automatically at `create()` time.

`Hub75DriverBase::MAX_INSTANCES` is `2` — this is a hard ceiling baked into the driver, not
just a documentation note. Every `Hub75Driver<Cfg>` instance registers itself with the shared
`Hub75DriverBase` on `create()` (and unregisters on destruction), so the two chip-wide
`DMA_IRQ_0`/`DMA_IRQ_1` handlers can dispatch to whichever instances are currently live —
regardless of which `Cfg` each one was built with.

> ⚠️ **The two instances must not share any GPIO pin.** Nothing in the type system checks this
> for you — pick non-overlapping `pins.data_base_pin`/`pins.rowsel_base_pin`/`clk_pin`/
> `strobe_pin`/`oen_pin` ranges for each `Hub75Config`, exactly as you would when wiring two
> physically separate panel chains.

## `hub75_demo_dual.cpp` Example

Two independent 64×32 chains (3 panels stacked vertically for instance A, 5 for instance B),
wired to non-overlapping GPIO ranges, each with its own rotation and its own driver type:

```cpp
#include "hub75.hpp"

// Instance A - panel wired to GPIO 0-13.
constexpr Hub75Config panel_cfg_a{
    .panel = {
        .matrix_panel_width = 64,
        .matrix_panel_height = 32,
        .chain_rows = 3,
        .chain_cols = 1,
        .chain_mode = Hub75ChainMode::SERPENTINE,
        .panel_kind = RowMapping::S31,
        .panel_chip = Hub75PanelChip::GENERIC,
        .sm_clockdiv_factor = 1.0f,
        .base_latch_ns = 80,
        .base_addr_ns = 120,
    },
    .screen = { .rotation = Hub75Rotation::DEG_90 },
    .pins = {
        .data_base_pin = 0, .data_n_pins = 6,
        .rowsel_base_pin = 6, .rowsel_n_pins = 3,
        .clk_pin = 11, .strobe_pin = 12, .oen_pin = 13,
    },
    .color = {
        .bitplanes = 10,
        .separate_cie_channels = true,
        .balanced_light_output = true,
        .ccm_rg_shift = 6,
        .ccm_gb_shift = 7,
    },
};

// Instance B - same color config as A, wired starting 14 pins further along so the two
// panels don't share any GPIO (data 14-19, rowsel 20-24, clk 25, strobe 26, oen 27).
constexpr Hub75Config panel_cfg_b{
    .panel = {
        .matrix_panel_width = 64,
        .matrix_panel_height = 32,
        .chain_rows = 5,
        .chain_cols = 1,
        .chain_mode = Hub75ChainMode::SERPENTINE,
        .panel_kind = RowMapping::S31,
        .panel_chip = Hub75PanelChip::GENERIC,
        .sm_clockdiv_factor = 1.0f,
        .base_latch_ns = 80,
        .base_addr_ns = 120,
    },
    .screen = { .rotation = Hub75Rotation::DEG_270 },
    .pins = {
        .data_base_pin = 14, .data_n_pins = 6,
        .rowsel_base_pin = 20, .rowsel_n_pins = 3,
        .clk_pin = 26, .strobe_pin = 27, .oen_pin = 28,
    },
    .color = panel_cfg_a.color,   // reuse instance A's colour tuning verbatim
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
    while (true) { tight_loop_contents(); }
}

int main()
{
    stdio_init_all();
    multicore_reset_core1();
    multicore_launch_core1(core1_entry);

    // ... application setup ...

    while (true)
    {
        // ... update whatever each panel should show, then push it out independently ...
        driver_a.update(/* ... */);
        driver_b.update(/* ... */);
        sleep_ms(10);
    }
}
```

The build target for the dual demo needs its own `CMakeLists.txt` entry (already present in
this repository's `CMakeLists.txt`):

```cmake
add_executable(hub75_demo_dual hub75_demo_dual.cpp)
target_compile_definitions(hub75_demo_dual PRIVATE
    USE_PICO_GRAPHICS=true
)
# ... target_sources / target_link_libraries / pico_generate_pio_header, same pattern as
#     the hub75_demo target — see this repository's CMakeLists.txt for the full block.
```

Note there is **no `HUB75_MULTICORE` switch** for the dual demo — with two driver instances
both needing to run continuously on DMA/PIO, `hub75_demo_dual.cpp` always launches both
`create()`/`start()` pairs together on core 1 and keeps application logic on core 0.
