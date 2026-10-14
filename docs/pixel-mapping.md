# How Pixel Mapping Works (General Idea)

HUB75 panels do not accept pixels in simple row-major order.

Instead, pixel data is shifted into the panel in the exact order expected by the
panel's internal shift registers and multiplexing logic.

Key properties:

- Pixels are shifted **column-wise**, not row-wise
- Multiple physical rows are driven **simultaneously**
- The shift buffer therefore always contains pixels from **different vertical regions**
- The exact ordering depends on:
  - how many rows are multiplexed
  - how the panel internally wires its row drivers

Each mapping describes how pixels from the linear source buffer (`src`) are reordered into the panel's shift buffer (`frame_buffer_`).

## Why This Mapping Step Exists

HUB75 panels do not accept pixels in simple top-to-bottom, left-to-right row-major order.
Their internal shift registers are loaded column by column, and multiple physical rows are
driven **simultaneously** in a single clock cycle. The exact arrangement depends on how many
rows the panel multiplexes at once and how its row drivers are internally wired.

The mapping step inside `update()` / `update_bgr()` bridges this gap. It reads pixels from the
flat source buffer your application writes to and places each one into the correct position in
the internal `frame_buffer_` that the DMA/PIO hardware will stream to the panel.

The mapping also applies the CIE 1931 look-up table (LUT) to each pixel as it is copied,
so there is no separate colour-correction pass — it is folded into the same loop at no
extra cost.

## Pixel Mapping — Choosing the Right `RowMapping` for Your Panel

`panel.panel_kind` (a `RowMapping` value) tells `update()` / `update_bgr()` how to reorder the pixels of your linear source buffer into the order your physical panel expects.

> ⚠️ A wrong `panel_kind` is one of the most common configuration mistakes. The panel lights up, but the image looks **scrambled, interleaved or repeated in strips**.

## At a Glance

| `panel_kind` | Rows lit at once | Scan rate | Typical panel | Address pins |
|---|:---:|---|---|:---:|
| [`RowMapping::Standard`](#rowmappingstandard) *(default)* | 2 | 1/8, 1/16, 1/32 | Indoor 32×16, 64×32, 64×64 | 3 / 4 / 5 |
| [`RowMapping::Split`](#rowmappingsplit) | 4 | 1/4 | Outdoor P10, 32×16 | 2 |
| [`RowMapping::S31`](#rowmappings31) | 4 | 1/16 | Outdoor P3, 64×64 | 4 |

If you are unsure, **start with `Standard`** - it covers the vast majority of panels.

## Which One Do I Need?

Count the **address pins** on the panel connector (A, B, C, …) and note the **panel height**. Then

```
rows lit at once = panel height / 2^(address pins)
```

| Height | Address pins | Scan rate | Rows lit | Try first |
|:---:|:---:|---|:---:|---|
| 16 | 3 (A–C) | 1/8 | 2 | `Standard` |
| 32 | 4 (A–D) | 1/16 | 2 | `Standard` |
| 64 | 5 (A–E) | 1/32 | 2 | `Standard` |
| 16 | 2 (A, B) | 1/4 | 4 | `Split` |
| 64 | 4 (A–D) | 1/16 | 4 | `S31` |

Panel labels can help: `-4S-` on a 32×16 outdoor panel points to `Split`; `-16S-` on a **64-row** panel with 4 address pins points to `S31`; everything else is `Standard`.

> 💡 Panel names are not standardised - two panels with the same label can be wired differently. If the first choice gives a stable but scrambled image, try the next candidate. Use a simple solid-colour or stripe test pattern and change **one parameter at a time**.

> ⚠️ A 64×64 panel labelled `-16S-` may also be a normal two-row 1/32 panel that was labelled inconsistently. If `S31` looks scrambled, try `Standard` with `rowsel_n_pins = 5`.

## What the Mapping Does

HUB75 panels do not take pixels in plain row-major order. Their shift registers are loaded **column by column**, and **several physical rows are driven at the same time**, so each shifted word contains pixels from different vertical regions of the image. Which regions depends on how many rows are multiplexed and how the row drivers are wired inside the panel.

The mapping step in `update()` / `update_bgr()` reads the pixels of your source buffer (`src`) and writes each one to its correct slot in the internal `frame_buffer_`, which DMA/PIO then streams to the panel. The CIE 1931 look-up table and colour correction are applied in the same copy loop, so the choice of mapping does not change colour fidelity.

---

## `RowMapping::Standard`

**The default and the right choice for most panels:** exactly **two rows** are lit at a time - one from the upper half and the matching one from the lower half.

**How to recognise it**

- Scan notation on the label such as `-16S-` or `-32S-` (1/8, 1/16, 1/32 scan). A 64×64 panel labelled `-32S-` lights 64/32 = 2 rows.
- 5 address pins (A–E) on a 64×64 panel, 4 pins (A–D) on a 64×32 panel, 3 pins (A–C) on a 32×16 panel.
- Driver chips such as ICND2012, FM6124, FM6126A or RUL6024. The chip determines `panel_chip` (initialisation) - it has **no influence** on `panel_kind`.

**Configuration**

```cpp
// 64×64, 1/32 scan: 2 rows lit, 5 address pins
constexpr Hub75Config cfg_64x64{
    .panel = {
        .matrix_panel_width  = 64,
        .matrix_panel_height = 64,
        .panel_kind = RowMapping::Standard,  // default, may be omitted
    },
    .pins = { .rowsel_n_pins = 5 },
};

// 64×32, 1/16 scan: 2 rows lit, 4 address pins, RUL6024 driver chips
constexpr Hub75Config cfg_64x32{
    .panel = {
        .matrix_panel_width  = 64,
        .matrix_panel_height = 32,
        .panel_chip = Hub75PanelChip::RUL6024,
    },
    .pins = { .rowsel_n_pins = 4 },
};

// 32×16, 1/8 scan: 2 rows lit, 3 address pins, FM6126A driver chips
constexpr Hub75Config cfg_32x16{
    .panel = {
        .matrix_panel_width  = 32,
        .matrix_panel_height = 16,
        .panel_chip = Hub75PanelChip::FM6126A,
    },
    .pins = { .rowsel_n_pins = 3 },
};
```

### How It Works

The following example uses a **64×32 panel at 1/16 scan** (4 address pins, 16 addresses, 2 rows per address). Replace the numbers with your own panel width, height and `rowsel_n_pins`.

#### 1. Which rows share an address

Each address lights **two rows, `SCAN_DEPTH` apart**: row *R* in the upper half and row *R + SCAN_DEPTH* in the lower half.

<img src="../assets/standard-addressing-row-groups.svg" alt="The 32 rows of a panel as a vertical stack. For addresses 0, 1 and 15 the two lit rows are highlighted, 16 rows apart" width="680">

| Address | Rows lit simultaneously |
|:---:|:---:|
| 0 | 0 and 16 |
| 1 | 1 and 17 |
| ⋮ | ⋮ |
| 15 | 15 and 31 |

In general:

```
SCAN_DEPTH       = number of addresses (1/16 scan → 16)
ROWS_IN_PARALLEL = matrix_panel_height / SCAN_DEPTH
address          = row % SCAN_DEPTH
rows of address A: A, A + SCAN_DEPTH, A + 2·SCAN_DEPTH, …   (ROWS_IN_PARALLEL rows)
```

#### 2. How the data of one address is written

The full panel width is written in **one continuous left-to-right sweep**, without splitting it into column bands. For every column, **two RGB triplets are combined into one six-value word**: `R0 G0 B0` of the upper row and `R1 G1 B1` of its partner row `SCAN_DEPTH` rows below. This word is shifted out with a single `CLK` pulse on the six consecutive GPIOs starting at `pins.data_base_pin`.

<img src="../assets/standard-addressing-sweep.svg" alt="One address written in a single left-to-right sweep, with one column zoomed in to show the R0 G0 B0 R1 G1 B1 word per CLK pulse" width="680">

For address 0 of the example the stream looks like this:

```
col 0:  R0 G0 B0 R1 G1 B1     (pixel of row 0, then pixel of row 16)
col 1:  R0 G0 B0 R1 G1 B1
 ...
col 63: R0 G0 B0 R1 G1 B1
```

After these 64 words `STROBE` (latch) fires and the row address advances.

---

## `RowMapping::Split`

For **outdoor P10 panels (32×16, 1/4 scan)** and electrically equivalent panels: **four rows** are lit at once and only **two address pins** (A, B) exist.

**How to recognise it**

- Label contains `-4S-` or another 1/4 scan notation.
- 32 columns × 16 rows, **2 address pins**: `rowsel_n_pins = log₂(height / 4) = 2`.
- Driver IC typically DP5020B or equivalent; usually an outdoor type (weatherproof, bright LEDs).

**What the mapping does**

The shift registers are arranged in **four vertical quarters** instead of two halves. Pixels are interleaved in column-pair groups: a selector bit derived from the panel width decides whether a column-pair slot draws from the first or the second half of the column pairs, and a group-row offset moves the source pointer at every scan-group boundary.

<details>
<summary>Key constants for 32×16, 1/4 scan</summary>

```
COLUMN_PAIRS      = 32 / 2        = 16
HALF_PAIRS        = 16 / 2        = 8      (= PAIR_HALF_BIT)
PAIR_HALF_SHIFT   = ctz(8)        = 3
SCAN_GROUPS       = 2^rowsel_n_pins = 4
ROWS_PER_GROUP    = 16 / 4        = 4
GROUP_ROW_OFFSET  = 4 × 32        = 128 source pixels
HALF_PANEL_OFFSET = (16 / 2) × 32 = 256 source pixels
```
</details>

**Configuration**

```cpp
// 32×16 outdoor panel, 1/4 scan: 4 rows lit, 2 address pins
constexpr Hub75Config cfg_p10{
    .panel = {
        .matrix_panel_width  = 32,
        .matrix_panel_height = 16,
        .panel_kind = RowMapping::Split,
        .sm_clockdiv_factor = 1.0f,   // increase if ghosting occurs
    },
    .pins = { .rowsel_n_pins = 2 },
};
```

---

## `RowMapping::S31`

For **outdoor P3 64×64 panels (1/16 scan)** such as the QP3 outdoor panel, and panels with the same internal wiring. Often labelled `-16S-` on a 64-row panel: 64/16 = **4 rows** lit at once.

**How to recognise it**

- `-16S-` label on a **64-row** panel - a standard `-16S-` panel lights 2 rows, this one lights 4.
- 64 columns × 64 rows, **4 address pins**: `rowsel_n_pins = log₂(height / 4) = 4`.
- Compatible driver ICs: DP5125D, MBI5253, ICND2055, ICND2065, ICND2153S, CFD325, MBI5264, CFD555, ICND2165.
- Typically an outdoor type (weatherproof, high brightness). Confirmed working board: **P3QD / QP3 Outdoor** (see [Boards](#boards)).

**What the mapping does**

The 64 rows form **four quarters of 16 rows**. For each logical scan line the shift buffer is filled in two passes: first the pixels of the **second and fourth** quarter are interleaved into the **even** output slots, then those of the **first and third** quarter into the **odd** slots, offset by `line_offset = 2 × matrix_panel_width`. This ordering matches the scan-row pairing of this panel family.

<details>
<summary>Key geometry for 64×64, 1/16 scan</summary>

```
quarter     = 64 × 64 / 4 = 1024 pixels each
line_offset = 2 × 64      = 128 output words

per logical row: even dst slots ← quarters 2 and 4 (src pixels)
                 odd  dst slots ← quarters 1 and 3 (src pixels, at dst + line_offset)
```
</details>

**Configuration**

```cpp
// 64×64 outdoor P3 panel, 1/16 scan: 4 rows lit, 4 address pins
constexpr Hub75Config cfg_qp3{
    .panel = {
        .matrix_panel_width  = 64,
        .matrix_panel_height = 64,
        .panel_kind = RowMapping::S31,
        .sm_clockdiv_factor = 2.75f,  // recommended starting value for this panel family
    },
    .pins = { .rowsel_n_pins = 4 },
};
```

---

## Good to Know

- **`panel_kind` is one enum value** - exactly one mapping is active at a time
- **`rowsel_n_pins` is not derived from `panel_kind`** - set `pins.rowsel_n_pins` yourself to the number of address pins. A wrong value selects the wrong rows, independent of the pixel mapping.
- **Four-row panels (`Split`, `S31`) may ghost or flicker at full PIO speed** - start with `sm_clockdiv_factor = 1.0f` and raise it (`2.0f`, `2.75f`, …) if artefacts appear.
- **Configure in your own `.cpp` file**, not in `hub75.hpp`: build a `constexpr Hub75Config` as shown above. Set only the fields that differ from the defaults - there is no need to repeat the whole struct.

