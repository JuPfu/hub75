## `Hub75Config` Reference

`Hub75Config` is the single object that describes your complete setup: the **panel**, the **screen orientation**, the **wiring** and the **colour pipeline**. It is passed as a template argument, so everything is evaluated at **compile time** and wrong combinations are rejected by `static_assert` with a message telling you which field to fix.

```cpp
constexpr Hub75Config panel_cfg{ /* ... */ };
Hub75Driver<panel_cfg> driver;
```

### Structure at a Glance

| Member | Type | Describes |
|---|---|---|
| [`panel`](#hub75panelconfig-panel) | `Hub75PanelConfig` | Physical panel: size, chaining, addressing, driver chip, signal timing |
| [`screen`](#hub75screenconfig-screen) | `Hub75ScreenConfig` | Logical orientation of the display |
| [`pins`](#hub75pinconfig-pins) | `Hub75PinConfig` | GPIO wiring between Pico and panel |
| [`color`](#hub75colorconfig-color) | `Hub75ColorConfig` | Bit depth, grey-level generation and colour correction |
| [`frame_rate_debug`](#frame_rate_debug) | `bool` | Print frame rate via `printf` (default `false`) |

> ⚠️ **Field order matters.** C++ designated initialisers must follow the declaration order of the struct. Within every sub-struct the order is the one used in the tables below; you may omit any field, but not swap them. Omitted fields keep their default.

### The Full Default Configuration

Everything you leave out has this value:

```cpp
constexpr Hub75Config panel_cfg{
    .panel = {
        .matrix_panel_width  = 64,
        .matrix_panel_height = 64,
        .chain_rows = 1,
        .chain_cols = 1,
        .panel_class = PanelClass::HUB75,
        .chain_mode  = Hub75ChainMode::SERPENTINE,
        .panel_kind  = RowMapping::Standard,
        .address_type = RowAddressing::Binary,
        .scan_mode = 0,                            // 0 = deduce
        .panel_chip = Hub75PanelChip::GENERIC,
        .pwm_reverse_chain_order = false,
        .inverted_stb = false,
        .sm_clockdiv_factor = 1.0f,
        .base_latch_ns = 80,
        .base_addr_ns  = 160,
    },
    .screen = { .rotation = Hub75Rotation::DEG_0 },
    .pins = {
        .data_base_pin = 0,  .data_n_pins = 6,
        .rowsel_base_pin = 6, .rowsel_n_pins = 5,
        .clk_pin = 11, .strobe_pin = 12, .oen_pin = 13,
    },
    .color = {
        .bitplanes = 10,
        .separate_cie_channels = false,
        .balanced_light_output = true,
        .swap_rb_pins = false,
        .ccm_rg_shift = 31, .ccm_rb_shift = 31,    // 31 = off
        .ccm_gr_shift = 31, .ccm_gb_shift = 31,
        .ccm_br_shift = 31, .ccm_bg_shift = 31,
    },
    .frame_rate_debug = false,
};
```

---

# `Hub75PanelConfig panel`

Describes **the physical panel**: its size, how several panels are chained, how its rows are addressed, which driver chip it carries and how fast the signals are driven.

| Field | Type | Default | Purpose |
|---|---|---|---|
| [`matrix_panel_width`](#matrix_panel_width--matrix_panel_height) | `uint32_t` | `64` | Width of **one** panel in pixels |
| [`matrix_panel_height`](#matrix_panel_width--matrix_panel_height) | `uint32_t` | `64` | Height of **one** panel in pixels |
| [`chain_rows`](#chain_rows--chain_cols) | `uint32_t` | `1` | Panels stacked vertically |
| [`chain_cols`](#chain_rows--chain_cols) | `uint32_t` | `1` | Panels side by side |
| [`panel_class`](#panel_class) | `PanelClass` | `HUB75` | Classic HUB75 or PWM-chip panel |
| [`chain_mode`](#chain_mode) | `Hub75ChainMode` | `SERPENTINE` | Cabling pattern of a multi-panel chain |
| [`panel_kind`](#panel_kind) | `RowMapping` | `Standard` | Pixel mapping onto the panel's scan order |
| [`address_type`](#address_type) | `RowAddressing` | `Binary` | How the panel selects the active row(s) |
| [`scan_mode`](#scan_mode) | `uint32_t` | `0` | Number of row addresses ("N" in 1:N); `0` = deduce |
| [`panel_chip`](#panel_chip) | `Hub75PanelChip` | `GENERIC` | Init sequence for the panel's driver IC |
| [`pwm_reverse_chain_order`](#pwm_reverse_chain_order) | `bool` | `false` | PWM panels only: reverse panel order in chain |
| [`inverted_stb`](#inverted_stb) | `bool` | `false` | Invert the strobe (latch) signal |
| [`sm_clockdiv_factor`](#sm_clockdiv_factor) | `float` | `1.0f` | Slow the PIO state machines down |
| [`base_latch_ns`](#base_latch_ns--base_addr_ns) | `uint32_t` | `80` | Latch settle time in ns |
| [`base_addr_ns`](#base_latch_ns--base_addr_ns) | `uint32_t` | `160` | Row-address settle time in ns |

> 💡 **Don't want to set all of this by hand?** For common panels a [panel profile](#shortcut-panel-profiles) fills in size, address pin count, addressing type and R/B swap for you.

### `matrix_panel_width` / `matrix_panel_height`

**Size of a single physical panel in pixels** - never the size of the whole chain.

| | |
|---|---|
| Default | `64` × `64` |
| Typical values | 64×32, 64×64, 80×40, 96×48, 32×16 |

Rules enforced at compile time:

- The total display width (`matrix_panel_width` × panels side by side) must be **even**.
- For `PanelClass::PWM` the width must be a **multiple of 16** (one driver chip serves 16 columns).
- Height together with `scan_mode`/`rowsel_n_pins` must result in 2 or 4 rows lit at once (see [`scan_mode`](#scan_mode)).

### `chain_rows` / `chain_cols`

Describe how several identical panels are arranged into one large display.

| Field | Meaning | Default |
|---|---|---|
| `chain_cols` | Number of panels **side by side** (left → right) in one chain row | `1` |
| `chain_rows` | Number of chain rows **stacked vertically** | `1` |

```
display width  = matrix_panel_width  × chain_cols
display height = matrix_panel_height × chain_rows
```

| Arrangement | `chain_cols` | `chain_rows` | Result for 64×64 panels |
|---|---|---|---|
| One panel | 1 | 1 | 64 × 64 |
| Two side by side | 2 | 1 | 128 × 64 |
| Two stacked | 1 | 2 | 64 × 128 |
| 2 × 2 block | 2 | 2 | 128 × 128 |

The signal input connector is always on the **left panel of the first chain row**. How the following panels are cabled is selected with [`chain_mode`](#chain_mode). With a rotated screen, `SCREEN_WIDTH`/`SCREEN_HEIGHT` of the driver already take the rotation into account.

### `panel_class`

Selects the fundamental control principle of the panel.

| Value | Meaning |
|---|---|
| `PanelClass::HUB75` *(default)* | Standard HUB75 panel. Grey levels are produced in software by BCM (Binary Code Modulation) with `bitplanes` = `8` or `10`. |
| `PanelClass::PWM` | Panels with PWM driver chips (e.g. ICND2153). The chips generate grey levels themselves, so the driver streams 16-bit values per channel. Requires `color.bitplanes = 16`, a panel width that is a multiple of 16, and `oen_pin` is used as **GCLK**. |

### `chain_mode`

Cabling pattern when more than one chain row exists. Irrelevant for a single chain row.

| Value | Cabling |
|---|---|
| `Hub75ChainMode::SERPENTINE` *(default)* | U-turn: row 0 runs left → right, row 1 right → left, row 2 left → right, … Panels of the reversed rows are mounted upside down, which the driver compensates (180° rotation). |
| `Hub75ChainMode::RASTER` | Every chain row is cabled in the same direction. |

```
SERPENTINE (2 chain rows)
  IN → [ P0 ][ P1 ] ─┐
  ┌─ [ P3 ][ P2 ] ←──┘    (P2/P3 mounted upside down)
```

> ⚠️ Only serpentine (U-turn) topologies are currently documented as supported. Use `SERPENTINE` unless you have verified `RASTER` with your wiring.

### `panel_kind`

Selects the **pixel-mapping algorithm** that turns the frame buffer into the panel's scan order. Panels of the same size can be wired internally in different ways.

| Value | Rows lit at once | Typical panels |
|---|---|---|
| `RowMapping::Standard` *(default)* | 2 or 4 | The vast majority of indoor panels |
| `RowMapping::Split` | 4 | Many P10 outdoor panels with split upper/lower-half addressing |
| `RowMapping::S31` | 4 | Four-way interleaved quarter mapping, panels often marketed as "…S31" |

If the image looks right but is **sliced, interleaved or repeated in strips**, the mapping is the first suspect.

### `address_type`

How the panel selects the row(s) currently lit.

| Value | Mechanism | Pins |
|---|---|---|
| `RowAddressing::Binary` *(default)* | The row number is put in binary on the address pins A…E. | `rowsel_n_pins` = 1…5, as many as the panel has |
| `RowAddressing::ABCShiftRegister` | One-hot row number clocked through a shift register in the row driver (74HC595 type, e.g. SM5368, DP32020). **A** = row clock, **B** = BK (discharge enable), **C** = row data. Row 0 injects the 1, every following row clocks it one step on. | `rowsel_n_pins` **must be 3** |

With a shift register the three pins are *not* a binary address, so the scan depth is derived from the panel height instead (see [`scan_mode`](#scan_mode)).

### `scan_mode`

The number of distinct row addresses - the **N in the panel's "1:N" scan rate**.

| Value | Meaning |
|---|---|
| `0` *(default)* | Deduce automatically (see below) |
| `N > 0` | Use exactly `N` |

**Automatic deduction**

- `Binary` addressing and a **power-of-two** panel height: `N = 2^rowsel_n_pins`
- otherwise (height such as 40 or 48, or shift-register addressing): `N = matrix_panel_height / 2`

```
rows lit simultaneously = matrix_panel_height / scan_mode     (must be 2 or 4)
```

| Panel | `rowsel_n_pins` | Scan rate | Rows lit | `scan_mode` |
|---|---|---|---|---|
| 64×64 | 5 | 1:32 | 2 | `0` (auto → 32) |
| 64×64 | 4 | 1:16 | 4 | `0` (auto → 16) |
| 64×32 | 4 | 1:16 | 2 | `0` (auto → 16) |
| 64×32 | 3 | 1:8 | 4 | `0` (auto → 8) |
| 32×16 | 2 | 1:4 | 4 | `0` (auto → 4) |
| 80×40 | 5 | 1:20 | 2 | `0` (auto → 20) |
| 96×48 | 5 | 1:24 | 2 | `0` (auto → 24) |
| 96×48, shift register | 3 | 1:24 | 2 | `0` (auto → 24) |

Set `scan_mode` explicitly only if your panel's real scan rate differs from what is deduced. It must not exceed `matrix_panel_height / 2`.

### `panel_chip`

Selects the **initialisation sequence** sent to the panel's driver IC before streaming starts. Many panels need it, otherwise they stay dark, show wrong colours or dim.

| Value | When to use |
|---|---|
| `Hub75PanelChip::GENERIC` *(default)* | Plain driver chips that need no init |
| `Hub75PanelChip::FM6126A` | Panels with FM6126A chips - they typically stay blank or garbled without the init sequence |
| `Hub75PanelChip::RUL6024` | Panels such as *P3-64\*64-32S-V2.0* that carry a RUL6024 |
| `Hub75PanelChip::ICND2153` | PWM-class panels with ICND2153 chips |

Check the chip markings on the back of the panel or the shop listing. If a panel stays dark although the wiring is right, `FM6126A` or `RUL6024` is a good first try.

### `pwm_reverse_chain_order`

**PWM panels only.** Reverses the order of the *panels* in a chain (the order of chips inside one panel is not affected). Set `true` if the panels of a chain appear in the wrong positions. Ignored for `PanelClass::HUB75`.

### `inverted_stb`

| Value | Meaning |
|---|---|
| `false` *(default)* | Strobe (latch) is active as usual |
| `true` | Strobe signal is inverted - for boards/level shifters that invert it |

If the display never updates properly although the wiring is verified, try toggling this.

### `sm_clockdiv_factor`

Divider for the PIO state machine clock. The driver is fast; on some panels this causes **flicker or ghosting**, and slowing the state machines down helps.

| Value | Effect |
|---|---|
| `1.0f` *(default)* | Full speed |
| `> 1.0f` | State machines run proportionally slower |
| `< 1.0f` | No effect - **clamped to `1.0f`** |

| Panel height | Try |
|---|---|
| ≤ 16 rows | `8.0f` |
| ≤ 32 rows | `2.0f` or `4.0f` |
| ≤ 64 rows | about `2.0f` |

Slower state machines lower the refresh rate, so use the smallest factor that cures the problem.

### `base_latch_ns` / `base_addr_ns`

Wait times that give the panel electronics time to settle. The driver converts them into PIO cycles from the actual system clock and `sm_clockdiv_factor`.

| Field | Default | Waits for |
|---|---|---|
| `base_latch_ns` | `80` | Latch signal to stabilise |
| `base_addr_ns` | `160` | Row address lines to stabilise |

Increase them (e.g. `180` / `260` as in `hub75_demo.cpp`) if you see ghosting, a faint image of the neighbouring row or flicker - typically with long cables or slow panels. Keep them as low as possible: they cost refresh rate.

### Shortcut: Panel Profiles

For frequently used panels, `make_hub75_config()` applies a named preset and overwrites exactly these fields: `matrix_panel_width`, `matrix_panel_height`, `rowsel_n_pins`, `address_type`, `swap_rb_pins`. Everything else (wiring, chaining, bit depth, …) comes from your base config.

| Profile | Size | Scan | `rowsel_n_pins` | `address_type` | `swap_rb_pins` |
|---|---|---|---|---|---|
| `Hub75PanelProfile::CUSTOM` *(default)* | - | - | - | no preset, use your own values | |
| `P64X32_1_16` | 64×32 | 1:16 | 4 | `Binary` | `false` |
| `P64X64_1_32` | 64×64 | 1:32 | 5 | `Binary` | `false` |
| `P80X40_1_20` | 80×40 | 1:20 | 5 | `Binary` | `false` |
| `P96X48_1_24` | 96×48 | 1:24 | 5 | `Binary` | `false` |
| `P96X48_1_24_SR` | 96×48 | 1:24 | 3 | `ABCShiftRegister` | `true` |

```cpp
inline constexpr Hub75Config MyConfig = make_hub75_config(
    Hub75PanelProfile::P96X48_1_24_SR,
    Hub75Config{
        .pins  = {.data_base_pin = 0, .rowsel_base_pin = 6,
                  .clk_pin = 11, .strobe_pin = 12, .oen_pin = 13},
        .color = {.bitplanes = 10},
    });
```

---

# `Hub75ScreenConfig screen`

Describes the **logical orientation** of the display, independent of how the panels are mounted.

| Field | Type | Default | Purpose |
|---|---|---|---|
| `rotation` | `Hub75Rotation` | `DEG_0` | Rotation of the screen |

### `rotation`

| Value | Screen | Source buffer layout |
|---|---|---|
| `Hub75Rotation::DEG_0` *(default)* | as built | `DISPLAY_WIDTH × DISPLAY_HEIGHT`, row-major |
| `Hub75Rotation::DEG_90` | rotated 90° | `DISPLAY_HEIGHT × DISPLAY_WIDTH` (transposed) |
| `Hub75Rotation::DEG_180` | upside down | `DISPLAY_WIDTH × DISPLAY_HEIGHT`, row-major |
| `Hub75Rotation::DEG_270` | rotated 270° | `DISPLAY_HEIGHT × DISPLAY_WIDTH` (transposed) |

Always size your drawing code and framebuffers with the driver's constants and not with the panel size:

```cpp
using Driver = Hub75Driver<panel_cfg>;
// Driver::SCREEN_WIDTH, Driver::SCREEN_HEIGHT - already rotation-aware
```

Example: a 2×1 chain of 64×64 panels (`chain_cols = 2`) is 128 × 64 at `DEG_0`/`DEG_180` and 64 × 128 at `DEG_90`/`DEG_270`.

---

# `Hub75PinConfig pins`

Describes the **wiring** between the Pico and the panel connector. The defaults occupy GPIO 0-13 in one contiguous block.

| Field | Type | Default | Purpose |
|---|---|---|---|
| `data_base_pin` | `uint32_t` | `0` | First GPIO of the colour data block |
| `data_n_pins` | `uint32_t` | `6` | Number of colour data pins |
| `rowsel_base_pin` | `uint32_t` | `6` | First GPIO of the row-select (address) block |
| `rowsel_n_pins` | `uint32_t` | `5` | Number of row-select pins |
| `clk_pin` | `uint32_t` | `11` | Pixel clock (CLK) |
| `strobe_pin` | `uint32_t` | `12` | Latch (STROBE / LAT) |
| `oen_pin` | `uint32_t` | `13` | Output enable (OE); **GCLK** for the PWM panel class |

### `data_base_pin` / `data_n_pins`

The colour pins must be **consecutive GPIOs** in the order `R1, G1, B1, R2, G2, B2` (upper and lower half of the panel), starting at `data_base_pin`. `data_n_pins` is `6` for every standard panel.

| Default wiring | GPIO |
|---|---|
| R1 G1 B1 | 0 1 2 |
| R2 G2 B2 | 3 4 5 |

If red and blue are swapped in the image because of the panel's internal wiring, use [`color.swap_rb_pins`](#swap_rb_pins) instead of re-soldering.

### `rowsel_base_pin` / `rowsel_n_pins`

The address pins `A, B, C, D, E` must also be **consecutive GPIOs** starting at `rowsel_base_pin`. `rowsel_n_pins` is **the number of address pins your panel connector actually has** and cannot be deduced from the panel height (a 64-row panel may be 5 lines / 2 rows lit, or 4 lines / 4 rows lit).

| Panel | Typical `rowsel_n_pins` |
|---|---|
| 32×16 | 2 |
| 64×32 | 4 (or 3 for 1:8) |
| 64×64 | 5 (or 4 for 1:16) |
| 80×40, 96×48 | 5 |
| any panel with `ABCShiftRegister` | **3** (A = row clock, B = BK, C = row data) |

Valid range is 1-5. The value `0` is rejected by a `static_assert`.

### `clk_pin`, `strobe_pin`, `oen_pin`

Single control signals, each on any free GPIO:

| Field | Panel pin | Function |
|---|---|---|
| `clk_pin` | CLK | Shifts the pixel data into the panel |
| `strobe_pin` | LAT / STB | Latches the shifted data (see also [`inverted_stb`](#inverted_stb)) |
| `oen_pin` | OE | Output enable - controls brightness via on-time. With PWM panels this pin carries **GCLK**. |

> 💡 Pick one wiring pattern and keep it - changing the GPIOs only needs changes here; nothing else in the config depends on the pin numbers.

---

# `Hub75ColorConfig color`

Controls **how many grey levels** are generated, how they are distributed over time, and how colours are corrected.

| Field | Type | Default | Purpose |
|---|---|---|---|
| [`bitplanes`](#bitplanes) | `uint32_t` | `10` | Bit depth of the BCM sequence |
| [`separate_cie_channels`](#separate_cie_channels) | `bool` | `false` | Individual gamma (CIE) table per colour channel |
| [`balanced_light_output`](#balanced_light_output) | `bool` | `true` | Split heavy bit-planes into slices |
| [`swap_rb_pins`](#swap_rb_pins) | `bool` | `false` | Swap red and blue |
| [`ccm_rg_shift`](#colour-correction-matrix-ccm) | `uint32_t` | `31` (off) | Green → Red |
| [`ccm_rb_shift`](#colour-correction-matrix-ccm) | `uint32_t` | `31` (off) | Blue → Red |
| [`ccm_gr_shift`](#colour-correction-matrix-ccm) | `uint32_t` | `31` (off) | Red → Green |
| [`ccm_gb_shift`](#colour-correction-matrix-ccm) | `uint32_t` | `31` (off) | Blue → Green |
| [`ccm_br_shift`](#colour-correction-matrix-ccm) | `uint32_t` | `31` (off) | Red → Blue |
| [`ccm_bg_shift`](#colour-correction-matrix-ccm) | `uint32_t` | `31` (off) | Green → Blue |

### `bitplanes`

Number of bit-planes used for **BCM (Binary Code Modulation)**: every plane is shown for a time proportional to its bit weight, so more planes give finer grey steps.

| Value | For | Remark |
|---|---|---|
| `8` | `PanelClass::HUB75` | Less memory, higher refresh rate |
| `10` *(default)* | `PanelClass::HUB75` | Finer dark gradients, more memory |
| `16` | `PanelClass::PWM` | Mandatory for PWM panels |

Any other value is rejected at compile time, and `8`/`10` is rejected for PWM panels (and `16` for HUB75).

### `separate_cie_channels`

The driver converts linear 8-bit colour values to the brightness curve of the human eye with a **CIE 1931 lookup table**.

| Value | Effect |
|---|---|
| `false` *(default)* | One shared table for all channels |
| `true` | Separate table per colour channel - better colour representation, **needs more memory** |

### `balanced_light_output`

BCM shows heavy bit-planes for a long time in one piece, which can make the image flicker. With balancing the heavy planes are **split into several smaller slices** distributed over the sequence.

| Value | Effect |
|---|---|
| `true` *(default)* | Higher effective refresh rate, less flicker, **a little more memory** |
| `false` | One slice per bit-plane |

| `bitplanes` | `balanced_light_output` | Slices per frame | What is split |
|---|---|---|---|
| 10 | `true` | 14 | plane 9 into 4 parts, plane 8 into 2 parts |
| 10 | `false` | 10 | - |
| 8 | `true` | 11 | plane 7 into 3 parts, plane 6 into 2 parts |
| 8 | `false` | 8 | - |

**Memory (HUB75 class).** The driver holds two frame buffers (double buffering) of

```
(total_pixels / 2) × slices per frame   bytes each
```

For a single 64×64 panel (4096 pixels), both buffers together need:

| `bitplanes` | `balanced_light_output` | Frame buffers |
|---|---|---|
| 10 | `true` | 56 KiB |
| 10 | `false` | 40 KiB |
| 8 | `true` | 44 KiB |
| 8 | `false` | 32 KiB |

On top come the RGB buffer (4 bytes per pixel) and the row command buffers. Plan accordingly for large chains.

### `swap_rb_pins`

Swaps the red and blue channel **in software**. Use it when red objects appear blue (some panels are wired this way). The profile `P96X48_1_24_SR` sets it to `true` automatically.

### Colour Correction Matrix (CCM)

Cheap panels often show a slight colour cast because the LEDs bleed into each other perceptually. The CCM mixes a small portion of one channel into another **after** the CIE lookup, on the already scaled bit-plane values.

Each of the six coefficients is a **right-shift amount** (integer arithmetic, no floats):

| Shift | Mixed-in share |
|---|---|
| `0` | 100 % (far too much - don't use) |
| `5` | ≈ 3.1 % |
| `6` | ≈ 1.6 % |
| `7` | ≈ 0.8 % |
| `8` | ≈ 0.4 % |
| `9` | ≈ 0.2 % (barely perceptible) |
| `31` *(default)* | **0 % - disabled** |

| Field | Reads as |
|---|---|
| `ccm_rg_shift` | share of **G**reen added into the **R**ed output |
| `ccm_rb_shift` | share of **B**lue added into the **R**ed output |
| `ccm_gr_shift` | share of **R**ed added into the **G**reen output |
| `ccm_gb_shift` | share of **B**lue added into the **G**reen output |
| `ccm_br_shift` | share of **R**ed added into the **B**lue output |
| `ccm_bg_shift` | share of **G**reen added into the **B**lue output |

The first letter is the **target**, the second the **source** channel.

```cpp
.color = {
    .ccm_rg_shift = 6,   // ~1.6 % green into red
    .ccm_gb_shift = 7,   // ~0.8 % blue into green
},                       // all other terms stay off (31)
```

Start with a single term at `7`-`9` and look at skin tones or white; increase only if needed.

---

# `frame_rate_debug`

The only top-level field of `Hub75Config`.

| Value | Effect |
|---|---|
| `false` *(default)* | Silent |
| `true` | Prints the measured frame frequency via `printf` (e.g. to the VS Code serial monitor), averaged over blocks of 100 frames |

For testing and debugging only - set it to `false` for production.

---

## Compile-Time Checks

Wrong settings fail the build instead of misbehaving at runtime. The most common messages and what they mean:

| Violated rule | Fix |
|---|---|
| `chain_rows` / `chain_cols` ≥ 1 | Use at least 1 |
| `bitplanes` invalid for the panel class | `8`/`10` for HUB75, `16` for PWM |
| Total display width must be even | Adjust width or `chain_cols` |
| `rowsel_n_pins` outside 1-5 | Set to the panel's real address pin count |
| `ABCShiftRegister` needs `rowsel_n_pins == 3` | Set it to `3` |
| Scan depth > `matrix_panel_height / 2` | Lower `scan_mode` or `rowsel_n_pins` |
| Rows lit per address not 2 or 4 | Adjust `scan_mode`, `rowsel_n_pins` or the height |
| PWM: width not a multiple of 16 | Use a multiple of 16 |
| Configured `rowsel_n_pins` too small for the height | Raise `rowsel_n_pins` (or set `scan_mode`) |

**Resource limits:** each driver instance claims 6 DMA channels and one PIO block for its row/stream pair, so at most **2 instances** can run at the same time.

---

## Complete Example

```cpp
// hub75_demo.cpp / your own .cpp file
constexpr Hub75Config panel_cfg{
    .panel = {
        .matrix_panel_width  = 64,
        .matrix_panel_height = 64,
        .chain_rows = 1,                            // panels stacked vertically
        .chain_cols = 1,                            // panels side by side
        .panel_class  = PanelClass::HUB75,
        .chain_mode   = Hub75ChainMode::SERPENTINE,
        .panel_kind   = RowMapping::Standard,
        .address_type = RowAddressing::Binary,
        .scan_mode    = 0,                          // deduce
        .panel_chip   = Hub75PanelChip::GENERIC,
        .pwm_reverse_chain_order = false,
        .inverted_stb = false,
        .sm_clockdiv_factor = 1.0f,
        .base_latch_ns = 180,
        .base_addr_ns  = 260,
    },
    .screen = {
        .rotation = Hub75Rotation::DEG_0,
    },
    .pins = {
        .data_base_pin = 0,   .data_n_pins = 6,
        .rowsel_base_pin = 6, .rowsel_n_pins = 5,
        .clk_pin = 11, .strobe_pin = 12, .oen_pin = 13,
    },
    .color = {
        .bitplanes = 10,
        .separate_cie_channels = true,
        .balanced_light_output = true,
        .swap_rb_pins = false,
        .ccm_rg_shift = 6,
        .ccm_gb_shift = 7,
    },
    .frame_rate_debug = false,
};

Hub75Driver<panel_cfg> driver;
```

---

## Troubleshooting at a Glance

| Symptom | Check |
|---|---|
| Compile error mentioning `rowsel_n_pins` or `scan_mode` | Pin count and height don't give 2 or 4 lit rows - see [`scan_mode`](#scan_mode) |
| Panel stays dark | `panel_chip`, `inverted_stb`, wiring in `pins` |
| Image sliced / interleaved | `panel_kind` (`Split`, `S31`), `scan_mode` |
| Red and blue swapped | `color.swap_rb_pins` |
| Colour cast | CCM shifts (`ccm_*_shift`) |
| Visible flicker in the image | `balanced_light_output = true`, `bitplanes` |
| Ghosting / flicker on rows | `sm_clockdiv_factor`, then `base_latch_ns` / `base_addr_ns` |
| Panels in a chain in wrong order or upside down | `chain_mode`, `chain_rows`/`chain_cols`, `pwm_reverse_chain_order` |
| Image rotated | `screen.rotation` |
| Out of memory | `bitplanes = 8`, `balanced_light_output = false`, `separate_cie_channels = false` |