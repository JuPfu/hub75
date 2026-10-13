# Chained Panels

The driver supports driving multiple HUB75 panels connected in series from a single Raspberry Pi Pico.
Panels are arranged in a **serpentine (U-turn) topology**: data flows left-to-right through the first
chain row, then reverses and flows right-to-left through the second chain row, and so on.
The signal input connector is always on the **left panel of chain row 0**.

---

## Topology Overview

Panels are described by a two-dimensional grid:

- **`panel.chain_cols`** — number of panels chained left-to-right within a single chain row (horizontal extent).
- **`panel.chain_rows`** — number of chain rows stacked vertically (vertical extent).

The diagram below shows a `chain_cols = 3`, `chain_rows = 2` arrangement (six panels total):

```
Signal IN
    │
    ▼
┌───────┐   ┌───────┐   ┌───────┐
│  0,0  │──▶│  0,1  │──▶│  0,2  │   chain row 0  (left → right)
└───────┘   └───────┘   └───────┘
                                │
                        U-turn  ▼
┌───────┐   ┌───────┐   ┌───────┐
│  1,0  │◀──│  1,1  │◀──│  1,2  │   chain row 1  (right → left)
└───────┘   └───────┘   └───────┘
```

> **Physical wiring is unchanged.** The serpentine reversal is handled in software by the pixel mapping
> stage (`update()` / `update_bgr()`): panels on odd-numbered chain rows automatically receive their
> content rotated 180° so that the image appears upright on the display.

---

## Configuration Parameters

All chaining parameters are set in the `panel` sub-struct of `Hub75Config`.
`matrix_panel_width` and `matrix_panel_height` always describe **one physical panel**.
The total virtual display dimensions (`DISPLAY_WIDTH` / `DISPLAY_HEIGHT`, internal to
`Hub75Driver<Cfg>`) are derived automatically.

| Field | Default | Description |
|---|---|---|
| `panel.matrix_panel_width` | `64` | Width of a single physical panel in pixels |
| `panel.matrix_panel_height` | `64` | Height of a single physical panel in pixels |
| `panel.chain_cols` | `1` | Number of panels per chain row (horizontal) |
| `panel.chain_rows` | `1` | Number of chain rows stacked vertically |
| `panel.chain_mode` | `Hub75ChainMode::SERPENTINE` | Topology: `SERPENTINE` or `RASTER` |

The derived display dimensions are computed at compile time inside `Hub75Driver<Cfg>`:

```cpp
static constexpr uint32_t DISPLAY_WIDTH  = Cfg.panel.matrix_panel_width  * Cfg.panel.chain_cols;
static constexpr uint32_t DISPLAY_HEIGHT = Cfg.panel.matrix_panel_height * Cfg.panel.chain_rows;
```

The raw display dimensions are only used internally; use the
[rotation-aware](#display-rotation) `Panel::SCREEN_WIDTH` and `Panel::SCREEN_HEIGHT` static
members to initialize graphics libraries or allocate framebuffers.

### Chain Modes

| Mode | Description |
|---|---|
| `Hub75ChainMode::SERPENTINE` | Odd chain rows are reversed 180° (U-turn topology, default) |
| `Hub75ChainMode::RASTER` | All panels in the same orientation; no reversal applied |

Use `Hub75ChainMode::RASTER` only if your physical cable layout already compensates for
direction changes (non-standard wiring).

---

## Code Example

The example below configures a **2×3 array** of 64×64 panels (total virtual display: 192×128 pixels):

```cpp
constexpr Hub75Config panel_cfg{
    .panel = {
        .matrix_panel_width = 64,         // width of one physical panel
        .matrix_panel_height = 64,        // height of one physical panel
        .chain_rows = 2,                  // 2 chain rows stacked vertically
        .chain_cols = 3,                  // 3 panels side-by-side per chain row
        .chain_mode = Hub75ChainMode::SERPENTINE,  // U-turn topology (default)
    },
};

using Panel = Hub75Driver<panel_cfg>;
```

This yields `DISPLAY_WIDTH=192` and `DISPLAY_HEIGHT=128`.
Your application draws into a framebuffer of exactly that size; the driver handles all panel
addressing and serpentine reordering transparently.

---

## Source Buffer Layout

The `update()` and `update_bgr()` functions expect a flat source buffer whose pixels are laid out
in **row-major order** across the full virtual display:

```
pixel(x, y) = src[y * DISPLAY_WIDTH + x]          // update()     — RGB888 packed
pixel(x, y) = src[(y * DISPLAY_WIDTH + x) * 3]    // update_bgr() — BGR888 byte triplets
```

The driver internally translates this linear layout into the correct per-panel, per-row addressing
required by the HUB75 protocol, including the 180° rotation for reversed panels in serpentine mode.

---

## Single-Panel Optimisation

When `panel.chain_cols == 1 && panel.chain_rows == 1`, the compiler selects a dedicated
single-panel fast path that skips all chain-related loop overhead. No special configuration is
required; the optimisation is applied automatically at compile time via `if constexpr` guards.

---

## Supported Panel Types and Chaining

All three supported panel mapping modes work with chained configurations:

| `panel_kind` value | Chain support |
|---|---|
| `RowMapping::Standard` (default) | ✔ all `chain_rows` × `chain_cols` combinations |
| `RowMapping::Split` | ✔ all `chain_rows` × `chain_cols` combinations |
| `RowMapping::S31` | ✔ all `chain_rows` × `chain_cols` combinations |

---

## Memory Considerations

Memory requirements scale linearly with the total number of panels:

```
TOTAL_PIXELS = matrix_panel_width × matrix_panel_height × chain_rows × chain_cols
```

Each additional panel increases the size of the `rgb_buffer_`, the `frame_buffer_` (all bitplanes),
and the `row_cmd_buffer_` proportionally. For large arrays on the RP2040 (264 KB SRAM), verify that
total buffer allocation fits within available memory before enabling `color.balanced_light_output = true`
and/or `color.separate_cie_channels = true`, as both options increase memory usage further.

---

## Quick-Reference: Common Configurations

| Array | `chain_cols` | `chain_rows` | `DISPLAY_WIDTH` | `DISPLAY_HEIGHT` |
|---|---|---|---|---|
| Single 64×64 panel | `1` | `1` | 64 | 64 |
| Two panels side-by-side | `2` | `1` | 128 | 64 |
| Four panels (2×2) | `2` | `2` | 128 | 128 |
| Six panels (3×2) | `3` | `2` | 192 | 128 |
| Eight panels (4×2) | `4` | `2` | 256 | 128 |

# Display Rotation

The driver supports software rotation of the displayed image by `0°`, `90°`, `180°`, or `270°`,
independent of how the panels are physically wired. This is useful when the matrix chain has to be
mounted in an orientation (e.g. portrait instead of landscape) that doesn't match the natural
left-to-right, top-to-bottom layout of the signal chain.

Rotation is set once, at compile time, and applies to the whole display — including chained arrays.

---

## Configuration

Set `screen.rotation` in your `Hub75Config`:

```cpp
constexpr Hub75Config panel_cfg{
    // ... panel / pins / etc ...
    .screen = {
        .rotation = Hub75Rotation::DEG_90,   // DEG_0 (default), DEG_90, DEG_180, or DEG_270
    },
};
```

| Value | Effect |
|---|---|
| `Hub75Rotation::DEG_0` (default) | No rotation |
| `Hub75Rotation::DEG_90` | Rotated 90° clockwise |
| `Hub75Rotation::DEG_180` | Rotated 180° (upside down) |
| `Hub75Rotation::DEG_270` | Rotated 270° clockwise (= 90° counter-clockwise) |

Only these four values exist — `Hub75Rotation` is a scoped enum, so anything else is a compile
error, not a silently-accepted invalid value.

---

## Physical Panel vs. Logical Source Buffer

`DISPLAY_WIDTH` and `DISPLAY_HEIGHT` (internal to `Hub75Driver<Cfg>`; see
[Configuration Parameters](#configuration-parameters) above) always describe the **physical**
matrix chain — `matrix_panel_width × chain_cols` and `matrix_panel_height × chain_rows`. These
two values never change when you set `screen.rotation`; the physical wiring obviously doesn't
change just because you rotate the image.

What *does* change is the shape of the buffer your application has to draw into and hand to
`update()` / `update_bgr()` — the **logical source buffer**. At `DEG_90`/`DEG_270` the logical
buffer is the *transpose* of the physical chain, because you're effectively drawing into a
canvas that is then turned sideways onto the panel:

```
Physical chain:  DISPLAY_WIDTH × DISPLAY_HEIGHT     (fixed — depends only on panel + chain layout)
Logical buffer:  depends on screen.rotation          (this is what YOU must size correctly)
```

> **This is the one part of rotation the driver cannot do for you.** It derives `DISPLAY_WIDTH` /
> `DISPLAY_HEIGHT` purely from your physical panel and chain configuration, and applies the rotation
> internally when sampling from your buffer — but nothing checks at compile time or runtime that the
> buffer you actually pass in has the right shape for the rotation you configured.

---

To help avoiding mistakes, `Hub75Driver<Cfg>` exposes the rotation-aware `Panel::SCREEN_WIDTH`
and `Panel::SCREEN_HEIGHT` static members (where `Panel = Hub75Driver<panel_cfg>`). They contain
the current width and height of your logical screen.

## Screen Dimensions per Rotation Value

| `screen.rotation` | Screen width | Screen height |
|---|---|---|
| `DEG_0` | `DISPLAY_WIDTH` | `DISPLAY_HEIGHT` |
| `DEG_90` | `DISPLAY_HEIGHT` | `DISPLAY_WIDTH` |
| `DEG_180` | `DISPLAY_WIDTH` | `DISPLAY_HEIGHT` |
| `DEG_270` | `DISPLAY_HEIGHT` | `DISPLAY_WIDTH` |

---

## Setting Up the Source Buffer

**`update_bgr()`** takes a raw byte buffer. Use the provided rotation-aware constants instead of `DISPLAY_WIDTH × DISPLAY_HEIGHT`:

```cpp
uint8_t src_buffer[Panel::SCREEN_WIDTH * Panel::SCREEN_HEIGHT * 3]; // BGR888
```
The underlying memory allocation stays the same, but the code gets much more readable. The graphics library can be initialized with the exact same constants as well:

**`update()`** takes a `PicoGraphics` canvas:

```cpp
PicoGraphics_PenRGB888 graphics(Panel::SCREEN_WIDTH, Panel::SCREEN_HEIGHT, frame_buffer);
```

---

## Combining Rotation with Chained Panels

Rotation and serpentine chaining are independent and compose cleanly: `panel.chain_mode =
Hub75ChainMode::SERPENTINE` handles the 180° per-panel correction needed for the physical
U-turn cabling (see
[How Serpentine Reversal Works Internally](#how-serpentine-reversal-works-internally)), while
`screen.rotation` applies on top of that, to the display as a whole. You don't need to do
anything differently for chained arrays.
