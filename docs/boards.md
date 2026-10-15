# Boards

This section lists every LED matrix panel used during development and testing of this library. Each entry documents the panel hardware, its key electrical characteristics, the pixel mapping it requires, and a ready-to-paste `Hub75Config` configuration block.

> 💡 **Adding your own panel?** Use the template at the end of this section. Copy an existing entry whose scan rate and pixel mapping are closest to yours, adjust the values, and open a pull request — contributions welcome!

---

## Supported Panels Overview

| Panel family | Example | Key settings |
|---|---|---|
| Standard HUB75, binary row address | 64×64 (1/32), 64×32 (1/16), 32x16 (1/8 or 1/4) | panel dimensions plus `rowsel_n_pins` |
| HUB75 with special driver IC | P3-64×64-32S (RUL6024), FM6126A panels | `.panel_chip = Hub75PanelChip::RUL6024` / `FM6126A` |
| **New:** shift-register row advance | Waveshare 96×48 SM5368 v2 | `.address_type = RowAddressing::ABCShiftRegister` |
| Outdoor, four rows lit at once | outdoor panel | `.panel_kind = RowMapping::Split` / `S31` |
| **New:** ICND2153 (S)PWM | Indoor panel 128×64 | `.panel_class = PanelClass::PWM`,</br> `.panel_chip = Hub75PanelChip::ICND2153`,</br>`.bitplanes = 16` |


## Tested panels at a glance

| # | Panel label | Dimensions | Scan / rows lit | `panel_kind` | Driver chip(s) | `panel_chip` |
|:---:|---|:---:|:---:|---|---|---|
| [1](#1-p3qd-64x64-21--p3-64x64-2012-21a-10) | P3QD-64x64-21 | 64 × 64 | 1/32 S · 2 rows | `RowMapping::Standard` | RUC7258D, FM6124DJ | `Hub75PanelChip::GENERIC` |
| [2](#2-p3-64x64-32s-v20--2310p3) | P3-64x64-32S-V2.0 | 64 × 64 | 1/32 S · 2 rows | `RowMapping::Standard` | RUC7258D, RUL6024 | `Hub75PanelChip::RUL6024` |
| [3](#3-qp3-outdoor--p3-1415-rowmappings31) | QP3 Outdoor P3-1415 | 64 × 64 | 1/16 S · 4 rows | `RowMapping::S31` | DP5125D | `Hub75PanelChip::GENERIC` |
| [4](#4-p10-smd-16x32-b-rowmappingsplit) | P10-SMD-16x32-b | 16 × 32 | 1/4 S · 4 rows | `RowMapping::Split` | DP5020B | `Hub75PanelChip::GENERIC` |
| [5](#5-X1.875-128x64-1515-32S-4L-V3.0) | X1.875-128x64-1515-32S-4L-V3.0 | 128x64 | 1/32 S · 2 rows | `RowMapping::Standard` | ICND2153 | `Hub75PanelChip::ICND2153` |

---

## 1. P3QD-64x64-21 / P3-64x64-2012-21A-1.0 (`ROW_MAP_STANDARD`)

### Hardware

| Property | Value |
|---|---|
| Dimensions | 64 × 64 pixels |
| LED pitch | P3 (3 mm) |
| Scan rate | 1/32 S — **2 rows lit simultaneously** |
| Address pins | 5 (A, B, C, D, E) |
| Driver ICs | RUC7258D, FM6124DJ |
| Panel type | Indoor |
| BCM mode | Plane-wise |

### Pixel Mapping

Standard two-row multiplexing. Upper and lower halves are interleaved column by column.
Use `RowMapping::Standard` (this is also the default when `panel_kind` is not set).

### Configuration

```cpp
constexpr Hub75Config panel_cfg{
    .panel = {
        .matrix_panel_width = 64,           // panel width in pixels
        .matrix_panel_height = 64,          // panel height in pixels
        .panel_kind = RowMapping::Standard,
        .panel_chip = Hub75PanelChip::GENERIC,
        .inverted_stb = false,
        .sm_clockdiv_factor = 1.0f,
        .base_latch_ns = 80,
        .base_addr_ns = 160,
    },
    .pins = {
        .data_base_pin = 0,                 // first colour data GPIO (R0)
        .data_n_pins = 6,                   // R0 G0 B0 R1 G1 B1
        .rowsel_base_pin = 6,                // first address GPIO (A)
        .rowsel_n_pins = 5,                  // A B C D E — 5 pins for 1/32 scan
        .clk_pin = 11,
        .strobe_pin = 12,
        .oen_pin = 13,
    },
    .color = {
        .bitplanes = 10,
        .balanced_light_output = true,
        .separate_cie_channels = true,
    },
    .frame_rate_debug = false,              // set to true only for debugging
};

using Panel = Hub75Driver<panel_cfg>;
```

`HUB75_MULTICORE=true` remains a `CMakeLists.txt` build flag — see
[Remaining `CMakeLists.txt` Build Flags](#remaining-cmakeliststxt-build-flags).

> 💡 `RowMapping::Standard` is the driver default and does not need to be set
> explicitly unless you are switching from a different mapping.

---

## 2. P3-64x64-32S-V2.0 / 2310P3

### Hardware

| Property | Value |
|---|---|
| Dimensions | 64 × 64 pixels |
| LED pitch | P3 (3 mm) |
| Scan rate | 1/32 S — **2 rows lit simultaneously** |
| Address pins | 5 (A, B, C, D, E) |
| Driver ICs | RUC7258D, **RUL6024** |
| Panel type | Indoor |
| BCM mode | Plane-wise |

### Pixel Mapping

Same two-row multiplexing as board 1. The difference is the RUL6024 driver IC, which
requires a dedicated initialisation sequence — set `panel_chip = Hub75PanelChip::RUL6024`.
Forgetting this causes incorrect brightness or distorted colours even though the pixel
mapping itself is identical to `Hub75PanelChip::GENERIC` panels.

> ⚠️ **How to identify the RUL6024:** Inspect the back of the panel. The chip is
> typically the largest IC on the PCB and is marked `RUL6024`. If in doubt, start with
> `Hub75PanelChip::GENERIC`; the worst outcome is incorrect brightness, not panel damage.

### Configuration

```cpp
constexpr Hub75Config panel_cfg{
    .panel = {
        .matrix_panel_width = 64,
        .matrix_panel_height = 64,
        .panel_kind = RowMapping::Standard,
        .panel_chip = Hub75PanelChip::RUL6024,   // ← required for RUL6024 init sequence
        .inverted_stb = false,
        .sm_clockdiv_factor = 1.0f,
        .base_latch_ns = 80,
        .base_addr_ns = 160,
    },
    .pins = {
        .data_base_pin = 0,
        .data_n_pins = 6,
        .rowsel_base_pin = 6,
        .rowsel_n_pins = 5,                      // A B C D E — 5 pins for 1/32 scan
        .clk_pin = 11,
        .strobe_pin = 12,
        .oen_pin = 13,
    },
    .color = {
        .bitplanes = 10,
        .balanced_light_output = true,
        .separate_cie_channels = true,
    },
    .frame_rate_debug = false,
};

using Panel = Hub75Driver<panel_cfg>;
```

#### 4. Chained panels (`CHAIN_COLS`/`CHAIN_ROWS` > 1)

For a given address, each panel's full width is written contiguously before the stream moves to the next panel in the chain. Serpentine
(`CHAIN_MODE_SERPENTINE`) 180° rows reverse the column sweep direction (`i` counts down instead of up) and negate the bank offset
(`dy = dy_base - p * rows_per_bank`) so the *source* content comes out right-side-up — the row-select address progression itself is never
reversed, since that's a hardware timing signal shared by the whole chain.

#### 5. Comparison with `ROW_MAP_SPLIT`

| | `ROW_MAP_STANDARD` | `ROW_MAP_SPLIT` |
|---|---|---|
| Rows per address | `ROWS_IN_PARALLEL` (typically 2) | 4 |
| Row spacing | Single fixed step (`SCAN_DEPTH`) | Two nested steps (`ROWS_PER_GROUP` and `MATRIX_PANEL_HEIGHT/2`) |
| Row sweep | One continuous pass, full width | Split into 4 column octants per address |
| Typical panels | Most indoor panels | P10-style outdoor panels with split upper/lower-half addressing |

See the `ROW_MAP_SPLIT` section above for the corresponding schematics on that scheme.

---

## 3. QP3 Outdoor / P3-1415 (`RowMapping::S31`)

### Hardware

| Property | Value |
|---|---|
| Dimensions | 64 × 64 pixels |
| LED pitch | P3 / 1415 (1.4 mm) |
| Scan rate | 1/16 S — **4 rows lit simultaneously** |
| Address pins | 4 (A, B, C, D) |
| Driver ICs | DP5125D · also compatible: MBI5253, ICND2055, ICND2065, ICND2153S, CFD325, MBI5264, CFD555, ICND2165 |
| Panel type | Outdoor (weatherproof, high brightness) |
| BCM mode | Plane-wise |

### Pixel Mapping

This panel uses a non-standard four-row multiplexing layout. The 64 rows are divided
into four equal quarters; each scan cycle drives one row from each quarter
simultaneously. The shift buffer is filled in a two-pass pattern per scan line:
second and fourth quarter pixels first (even output slots), then first and third quarter
pixels (odd output slots, offset by `2 × matrix_panel_width`).

Set `panel_kind = RowMapping::S31`. Do **not** use `RowMapping::Standard` for this
panel — despite what the `-16S-` label might suggest for a 64-row panel, this is a
4-row simultaneous design, not 2-row.

> ⚠️ **Tested with RP2350B (GPIO 30–43).** The example below uses the RP2350B pin
> mapping. For a standard Pico / RP2040 substitute `data_base_pin = 0`,
> `rowsel_base_pin = 6`, `clk_pin = 11`, `strobe_pin = 12`, `oen_pin = 13` and remove the
> `PICO_RP2350A=0` build flag.

### Configuration

For bare RP2350 (no named board), add these two lines **before**
`include(pico_sdk_import.cmake)` in `CMakeLists.txt`:

```cmake
set(PICO_PLATFORM rp2350)
set(PICO_BOARD none CACHE STRING "Board type")
```

The remaining build-system flags:

```cmake
target_compile_definitions(hub75_demo PRIVATE
    PICO_RP2350A=0                  # RP2350B — omit for RP2350A or RP2040
    USE_PICO_GRAPHICS=true          # false = use hub75 as a pure library
    HUB75_MULTICORE=true
)
```

Then the panel configuration in code:

```cpp
constexpr Hub75Config panel_cfg{
    .panel = {
        .matrix_panel_width = 64,
        .matrix_panel_height = 64,
        .panel_kind = RowMapping::S31,       // non-standard 4-row pixel mapping
        .panel_chip = Hub75PanelChip::GENERIC,
        .inverted_stb = false,
        .sm_clockdiv_factor = 2.75f,         // recommended starting value; reduce if too slow
        .base_latch_ns = 100,                // slightly wider timing margins for outdoor panel
        .base_addr_ns = 260,
    },
    .pins = {
        .data_base_pin = 30,                 // RP2350B GPIO block starts at 30
        .data_n_pins = 6,
        .rowsel_base_pin = 36,               // RP2350B address pins start at 36
        .rowsel_n_pins = 4,                  // A B C D — 4 pins for 1/16 scan, 4 rows lit
        .clk_pin = 41,
        .strobe_pin = 42,
        .oen_pin = 43,
    },
    .color = {
        .bitplanes = 10,
        .balanced_light_output = true,
        .separate_cie_channels = true,
    },
    .frame_rate_debug = false,               // set to true only for debugging
};

using Panel = Hub75Driver<panel_cfg>;
```

**Recommended system clock:** 266 MHz (`set_sys_clock_khz(266000, true)` in `main.c`).

> 💡 If ghosting or flicker appears, try increasing `sm_clockdiv_factor` in steps
> of 0.25 (e.g. `3.0f`, `3.5f`). Panels from different batches of the same model
> sometimes tolerate slightly different timing.

---

## 4. P10-SMD-16x32-b (`RowMapping::Split`)

### Hardware

| Property | Value |
|---|---|
| Dimensions | 16 × 32 pixels |
| LED pitch | P10 / 3535 (10 mm) |
| Scan rate | 1/4 S — **4 rows lit simultaneously** |
| Address pins | 2 (A, B) |
| Driver ICs | DP5020B |
| Panel type | Outdoor (weatherproof, very high brightness) |
| BCM mode | Line-wise |

### Pixel Mapping

Four-row multiplexing with a column-pair interleave scheme. The 16 rows are divided into four vertical quarters of 4 rows each. 
Pixels are placed into the shift buffer in column-pair groups; a selector bit derived from the panel width determines whether a
pair slot draws from the first or second half of the column pairs within the current scan group.

Set `panel_kind = RowMapping::Split`. Note that although both this panel and board 3 light
4 rows simultaneously, their internal wiring conventions differ — `RowMapping::Split` and
`RowMapping::S31` are **not** interchangeable.

### Configuration

```cpp
constexpr Hub75Config panel_cfg{
    .panel = {
        .matrix_panel_width = 32,
        .matrix_panel_height = 16,
        .panel_kind = RowMapping::Split,     // 4-row outdoor panel pixel mapping
        .panel_chip = Hub75PanelChip::GENERIC,
        .inverted_stb = false,
        .sm_clockdiv_factor = 1.0f,          // increase to 2.0f or 4.0f if ghosting occurs
        .base_latch_ns = 80,
        .base_addr_ns = 160,
    },
    .pins = {
        .data_base_pin = 0,
        .data_n_pins = 6,
        .rowsel_base_pin = 6,
        .rowsel_n_pins = 2,                  // A B only — 2 pins for 1/4 scan, 4 rows lit
        .clk_pin = 11,
        .strobe_pin = 12,
        .oen_pin = 13,
    },
    .color = {
        .bitplanes = 10,
        .balanced_light_output = true,
        .separate_cie_channels = true,
    },
    .frame_rate_debug = false,
};

using Panel = Hub75Driver<panel_cfg>;
```

> 💡 P10 outdoor panels are electrically robust and typically tolerate a wide range
> of clock divider settings. Start at `sm_clockdiv_factor = 1.0f` and only adjust if
> you observe ghosting on bright content.

---

## Template for a New Board

When adding a new panel to this section, copy the block below, fill in every field,
and add a row to the [overview table](#overview) at the top of this section.

```markdown
## N. <Panel label / model number>

### Hardware

| Property | Value |
|---|---|
| Dimensions | W × H pixels |
| LED pitch | Pn (n mm) |
| Scan rate | 1/nS — **n rows lit simultaneously** |
| Address pins | n (A, B, …) |
| Driver ICs | <chip name(s)> |
| Panel type | Indoor / Outdoor |
| BCM mode | Plane-wise / Line-wise |

### Pixel Mapping

<Brief description of why this mapping applies and any gotchas.>

Use `panel_kind = <RowMapping::Standard | RowMapping::Split | RowMapping::S31>`.

### Configuration

```cpp
constexpr Hub75Config panel_cfg{
    .panel = {
        .matrix_panel_width = <W>,
        .matrix_panel_height = <H>,
        .panel_kind = <row_mapping>,          // pixel mapping topology — see Step 3
        .panel_chip = <Hub75PanelChip::GENERIC | FM6126A | RUL6024>,
        .inverted_stb = false,
        .sm_clockdiv_factor = 1.0f,
        .base_latch_ns = 80,
        .base_addr_ns = 160,
    },
    .pins = {
        .data_base_pin = <pin>,
        .data_n_pins = 6,
        .rowsel_base_pin = <pin>,
        .rowsel_n_pins = <n>,                 // log₂(H / rows_lit_simultaneously)
        .clk_pin = <pin>,
        .strobe_pin = <pin>,
        .oen_pin = <pin>,
    },
    .color = {
        .bitplanes = 10,
        .balanced_light_output = true,
        .separate_cie_channels = true,
    },
    .frame_rate_debug = false,
};

using Panel = Hub75Driver<panel_cfg>;
```

> Add any panel-specific notes, tested MCU, system clock, or known quirks here.
```