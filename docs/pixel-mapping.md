## Pixel Mapping — Choosing the Right `RowMapping` for Your Panel

`panel.panel_kind` (a `RowMapping` enum value) tells the `update()` / `update_bgr()` method how
to reorder pixels from your application's linear source buffer into the shift-register order
that your physical panel expects.

> ⚠️ Setting the wrong `panel_kind` is one of the most common configuration mistakes.
> The panel will light up, but the image will look scrambled, interleaved, or repeated
> in blocks — depending on how far off the mapping is.

---

### Why This Mapping Step Exists

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

---

### The Three `RowMapping` Values at a Glance

| `panel_kind` | Rows lit simultaneously | Typical scan notation | Typical panel dimensions | Address lines needed |
|---|:---:|---|---|:---:|
| `RowMapping::Standard` | 2 | 1/8 S, 1/16 S, 1/32 S | 32×16 / 64×32 / 64×64 | 3 / 4 / 5 |
| `RowMapping::Split` | 4 | 1/4 S | 32×16 outdoor | 2 |
| `RowMapping::S31` | 4 | 1/16 S | 64×64 outdoor | 4 |

> 💡 The number of address lines (`pins.rowsel_n_pins`) must match the selected mapping.
> See [Step 2 — Scan Rate and Rows Lit Simultaneously](#step-2--scan-rate-and-rows-lit-simultaneously)
> for the formula that connects panel height, simultaneous rows, and pin count.

---

### `RowMapping::Standard` — Standard Two-Row Multiplexing

**Use this for the vast majority of HUB75 panels.** It is the correct choice whenever exactly
**two rows** of the panel are driven at the same time — one from the upper half and one from
the corresponding row in the lower half.

**How to recognise it:**

- The scan notation on the panel label contains `-16S-`, `-32S-`, or similar odd-sounding
  fractions (1/8, 1/16, 1/32 scan). A 64×64 panel labelled `-32S-` drives 64/32 = 2 rows
  simultaneously → use this value.
- The panel connector exposes **5 address pins** (A, B, C, D, E) for a 64×64 panel,
  or **4 pins** (A, B, C, D) for a 32×64 panel, matching the formula
  `rowsel_n_pins = log₂(height / 2)`.
- The driver chip is **ICND2012**, **FM6124**, **FM6126A**, **RUL6024**, or similar standard
  HUB75 LED driver.

**What the mapping does:**

Pixels are copied into the shift buffer in alternating pairs: one pixel from row `r` in the
upper half, followed immediately by the corresponding pixel from row `r` in the lower half.
The offset between the two halves is `matrix_panel_width × matrix_panel_height / 2`.

```c
// Simplified view of the mapping kernel
constexpr size_t offset = (matrix_panel_width * matrix_panel_height) / 2;
for (size_t fb = 0, j = 0; fb < total_pixels; fb += 2, ++j) {
    frame_buffer[fb]     = pack_lut_rgb(j,          src[j]);           // upper half
    frame_buffer[fb + 1] = pack_lut_rgb(j + offset, src[j + offset]);  // lower half
}
```

**Configuration:**

```cpp
// 64×64 standard panel, 1/32 scan (2 rows lit, 5 address pins)
constexpr Hub75Config panel_cfg{
    .panel = {
        .matrix_panel_width = 64,
        .matrix_panel_height = 64,
        // .panel_kind = RowMapping::Standard is the default — no need to set it explicitly
    },
    .pins = { .rowsel_n_pins = 5 },
};

// 64×32 standard panel, 1/16 scan (2 rows lit, 4 address pins)
constexpr Hub75Config panel_cfg_b{
    .panel = { .matrix_panel_width = 64, .matrix_panel_height = 32 },
    .pins = { .rowsel_n_pins = 4 },
};

// 32×16 standard panel, 1/8 scan (2 rows lit, 3 address pins)
constexpr Hub75Config panel_cfg_c{
    .panel = { .matrix_panel_width = 32, .matrix_panel_height = 16 },
    .pins = { .rowsel_n_pins = 3 },
};
```

> 💡 `RowMapping::Standard` is the **default**. You do not need to set `panel_kind`
> explicitly unless you are switching back from one of the other values.

---

### `RowMapping::Split` — Outdoor P10 Panel, Four-Row Multiplexing

Use this for the **P10-SMD 16×32** outdoor panel (3535 LED pitch, 1/4 scan) and any
electrically equivalent panel where **four rows** are driven simultaneously and only
**2 address lines** (A, B) are present.

**How to recognise it:**

- Panel label contains `-4S-` or similar 1/4 scan notation.
- Physical dimensions are **32 columns × 16 rows**.
- The connector exposes only **2 address pins** (A, B), matching
  `rowsel_n_pins = log₂(height / 4) = log₂(4) = 2`.
- The driver IC is typically **DP5020B** or equivalent.
- The panel is usually an **outdoor** type (weatherproof casing, brighter LEDs).

**What the mapping does:**

The panel's internal shift registers are arranged in four vertical quarters rather than
two halves. Pixels are interleaved in column-pair groups: a selector bit derived from the
panel width determines whether a column-pair slot draws from the first or the second half
of the available column pairs, and a group-row offset advances the source pointer at each
scan-group boundary.

```c
// Key constants for 32×16, 1/4 scan
// COLUMN_PAIRS        = 32 / 2 = 16
// HALF_PAIRS          = 16 / 2 = 8   (= PAIR_HALF_BIT)
// PAIR_HALF_SHIFT     = ctz(8) = 3
// SCAN_GROUPS         = 2^rowsel_n_pins = 4
// ROWS_PER_GROUP      = 16 / 4 = 4
// GROUP_ROW_OFFSET    = 4 × 32 = 128 source pixels
// HALF_PANEL_OFFSET   = (16 / 2) × 32 = 256 source pixels
```

**Configuration:**

```cpp
// 32×16 outdoor panel, 1/4 scan (4 rows lit, 2 address pins)
constexpr Hub75Config panel_cfg{
    .panel = {
        .matrix_panel_width = 32,
        .matrix_panel_height = 16,
        .panel_kind = RowMapping::Split,
        .sm_clockdiv_factor = 1.0f,   // increase if ghosting occurs
        .panel_chip = Hub75PanelChip::GENERIC,
    },
    .pins = { .rowsel_n_pins = 2 },
};
```

> ⚠️ **Do not use this value with larger panels or panels that have more than 2 address
> lines.** The mapping constants are compiled in from the width and height, but the
> quarter-split logic is specific to the 4S wiring architecture.

---

### `RowMapping::S31` — Outdoor P3 64×64 Panel, Four-Row Multiplexing

Use this for the **QP3 outdoor 64×64** panel (1415 / 1.4 mm LED pitch, 1/16 scan) and
panels with the same internal wiring convention — sometimes labelled `-16S-` on a 64-tall
panel, which gives 64/16 = 4 rows lit simultaneously.

**How to recognise it:**

- Panel label contains `-16S-` **on a 64-row-tall panel** (not 32-row). The key
  distinction from a standard `-16S-` panel is that this one lights **4** rows at once,
  not 2.
- Physical dimensions are **64 columns × 64 rows**.
- The connector exposes **4 address pins** (A, B, C, D), matching
  `rowsel_n_pins = log₂(height / 4) = log₂(16) = 4`.
- Compatible driver ICs include: **DP5125D**, **MBI5253**, **ICND2055**, **ICND2065**,
  **ICND2153S**, **CFD325**, **MBI5264**, **CFD555**, **ICND2165**.
- The panel is typically an **outdoor** type (weatherproof, high-brightness).
- Confirmed working board: **P3QD / QP3 Outdoor** (see [Boards](#boards)).

**What the mapping does:**

The panel divides its 64 rows into four equal quarters of 16 rows each. The shift buffer
is filled in a specific two-pass pattern per logical scan line: first the pixels from the
second and fourth quarter are interleaved into even output slots, then the pixels from the
first and third quarter are interleaved into the odd output slots (offset by
`line_offset = 2 × matrix_panel_width`). This unusual ordering matches the internal
scan-row pairing of this panel family.

```c
// Key geometry for 64×64, 1/16 scan
// quarter       = 64 × 64 / 4 = 1024 pixels each
// line_offset   = 2 × 64 = 128 output words
//
// Per logical row: even dst slots   ← quarters 2 & 4 (src pixels)
//                  odd  dst slots   ← quarters 1 & 3 (src pixels, at dst + line_offset)
```

**Configuration:**

```cpp
// 64×64 outdoor P3 panel, 1/16 scan (4 rows lit, 4 address pins)
constexpr Hub75Config panel_cfg{
    .panel = {
        .matrix_panel_width = 64,
        .matrix_panel_height = 64,
        .panel_kind = RowMapping::S31,
        .sm_clockdiv_factor = 2.75f,   // recommended starting value for this panel family
        .panel_chip = Hub75PanelChip::GENERIC,
    },
    .pins = { .rowsel_n_pins = 4 },
};
```

> ⚠️ A 64×64 panel with a `-16S-` label **could also be a standard two-row panel** at
> 1/32 scan that the manufacturer labelled inconsistently. If this value produces a
> scrambled image, try `RowMapping::Standard` with `rowsel_n_pins = 5` instead.

---

### How to Select the Correct Value — Decision Guide

```
Does your panel label contain "-16S-" or "-32S-" on a 64-tall panel
with 5 address pins (A–E)?
    └─ Yes → RowMapping::Standard  (standard 1/32 scan, 2 rows lit)

Does your panel label contain "-16S-" on a 64-tall panel
with 4 address pins (A–D)?
    └─ Yes → RowMapping::S31  (4 rows lit)
              If image is still scrambled, try RowMapping::Standard

Does your panel label contain "-4S-" and the panel is 32 × 16
with 2 address pins (A, B)?
    └─ Yes → RowMapping::Split

For any other panel, or when uncertain:
    └─ Start with RowMapping::Standard — it covers the majority of panels.
       If the image is stable but scrambled or interleaved, try the others.
```

**Quick check table:**

| Address pins on connector | Panel height | Rows lit simultaneously | Value to try first |
|:---:|:---:|:---:|---|
| 5 (A–E) | 64 | 2 | `RowMapping::Standard` |
| 4 (A–D) | 64 | 2 | `RowMapping::Standard` (e.g. 64×32) |
| 4 (A–D) | 64 | 4 | `RowMapping::S31` |
| 3 (A–C) | 16 | 2 | `RowMapping::Standard` |
| 2 (A, B) | 16 | 4 | `RowMapping::Split` |

---

### What Happens When You Set `panel_kind`

Setting `panel_kind` has **two effects**:

1. **Selects the pixel reorder algorithm** used inside `update()` / `update_bgr()`. The
   chosen algorithm determines how the driver maps each `(x, y)` coordinate from the
   source buffer to the correct slot in the internal `frame_buffer_` that will be streamed
   to the panel's shift registers.

2. **Implicitly fixes the memory layout** of `frame_buffer_`. Different mappings produce
   differently structured buffers. Activating a mapping that does not match the physical
   panel causes address aliasing — pixels from unrelated source rows land in the same
   shift-register slot — which is why the image appears scrambled rather than just offset.

The CIE/CCM colour correction (`pack_lut_rgb`) is applied **inside the same copy loop**
regardless of which value is active, so colour fidelity is unaffected by the choice of
mapping.

---

### Important Notes

- **`panel_kind` picks exactly one mapping at a time** — it's a single enum field, so
  there's no way to accidentally activate more than one (unlike the old preprocessor
  defines, where defining two mapping macros at once was a possible mistake).

- **`rowsel_n_pins` must match the mapping.** The number of address lines is not derived
  from `panel_kind`; you must set `pins.rowsel_n_pins` explicitly and consistently. An
  incorrect value causes wrong row selection independently of the pixel mapping.

- **The fields are set in your own `.cpp` file**, not by editing `hub75.hpp` directly. Build
  a `constexpr Hub75Config` value (see examples above). The `hub75.hpp` struct definitions
  show the available fields and their defaults but are not the intended configuration point.

- **Panel names are not standardised.** Two panels from different manufacturers with the
  same printed label may have different internal wiring. If the expected mapping produces a
  scrambled image, work through the decision guide above and test each option with a simple
  solid-colour or stripe test pattern — change **one parameter at a time**.

- **`sm_clockdiv_factor` may need adjustment for 4-row panels.** Panels using
  `RowMapping::Split` or `RowMapping::S31` sometimes exhibit ghosting
  or flicker at full PIO speed. Start with `sm_clockdiv_factor = 1.0f` and increase
  (e.g. `2.0f`, `2.75f`) if artefacts appear. See
  [Step 6 — State Machine Clock Divider](#step-6--state-machine-clock-divider-sm_clockdiv).


> 💡 You only need to set the fields that differ from these defaults. There is no need to
> repeat the entire struct for a standard setup.
