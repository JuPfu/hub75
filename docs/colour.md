## Improved Colour Perception

The graphics system for the demo effects operates in **RGB888** format (8 bits per channel, 24 bits per pixel). To better match human vision, colours are mapped using the [CIE 1931 lightness curve](https://jared.geek.nz/2013/02/linear-led-pwm/). This mapping effectively expands the usable range to **10 bits per channel**.

The HUB75 driver takes advantage of this: its PIO/DMA pipeline packs each pixel as a **32-bit word** with 10 bits for red, 10 bits for green, and 10 bits for blue.

---

### Balanced Light Output

In standard Binary Code Modulation (BCM), each bitplane is displayed for a duration proportional to its bit weight — the MSB plane stays on for half the entire frame period. At low brightness settings or when the display content changes rapidly, this long uninterrupted ON-period becomes visible as flicker.

**Balanced Light Output** addresses this by splitting high-weight bitplanes into multiple smaller segments, distributing them evenly across the BCM sequence. The total illumination time per bitplane remains identical — only the distribution changes. This increases the effective refresh rate and eliminates visible flicker, even at low brightness levels.

#### Example: 10-bit color depth (`bitplanes = 10`)

Without Balanced Light Output, the BCM sequence processes bitplanes 0–9 in a single pass (10 steps):

```c
// Standard BCM — 10 steps
static const uint8_t BCM_SEQUENCE[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
```

A first step towards **Balanced Light Output** is the reordering of the bitplanes as is done in the current implementation when `color.balanced_light_output` is set to `false`. This already has an effect to mitigate flicker by mixing long and short ON-periods.

```c
// Reordered BCM — 10 steps
static const uint8_t BCM_SEQUENCE[] = { 0, 9, 2, 7, 4, 5, 1, 8, 3, 6 };
```

Enabling `color.balanced_light_output = true` produces a 14-step sequence instead. Bitplane 9 (highest weight) is split into **4 segments**, bitplane 8 into **2 segments** — all other bitplanes appear once:

```c
// Balanced Light Output — 14 steps
static const uint8_t BCM_SEQUENCE[] = {
    9, 0, 1, 2, 8, 3, 4, 9, 5, 9, 6, 8, 7, 9
//  ^           ^        ^     ^     ^     ^
//  |           |        |     |     |     bitplane 9 (4x total)
//  bitplane 9 (1st)     |     bitplane 9 (3rd)
//              |        bitplane 9 (2nd)
//              bitplane 8 (1st)     |
//                                   bitplane 8 (2nd)
};
```

> **Note:** The sum of all BCM cycle durations is identical in both sequences — Balanced Light Output does not affect overall brightness, only the temporal distribution of light.

#### Visual comparison

```
Standard BCM (10 steps):
|0|1|2|3|4|5|6|7|████8████|████████████████9████████████████|
                           ↑ long ON-period → visible flicker

Balanced Light Output (14 steps):
|███9███|0|1|2|██8██|3|4|███9███|5|███9███|6|██8██|7|███9███|
 ↑               ↑          ↑         ↑        ↑        ↑
 MSB segments spread evenly across the frame → no flicker
```

Balanced Light Output improves the *temporal* distribution of light. The next section addresses *spectral* accuracy — correcting the colour cross-channel bleed that makes neutral grey appear tinted on real panels.

## Colour Correction Matrix

### Overview

HUB75 LED matrix panels do not reproduce colour faithfully out of the box. Two independent sources of error contribute to inaccurate colour:

**1. Per-channel luminance non-linearity** — already corrected by the [CIE 1931 lightness curve](https://jared.geek.nz/2013/02/linear-led-pwm/) baked into `CIE_RED`, `CIE_GREEN`, and `CIE_BLUE`. The per-channel white-balance scaling factors `RED_CAP`, `GREEN_CAP`, and `BLUE_CAP` in `cie.py` handle the remaining per-channel gain difference.

**2. Spectral cross-channel bleed** — *not* corrected by the CIE LUTs. Real LEDs emit light across a broader spectrum than their nominal colour. A red LED radiates slightly into the orange-green range; a green LED dominates perceived brightness. The result is that neutral grey (`R = G = B`) appears tinted, saturated colours look shifted, and skin tones are rendered incorrectly.

A **Colour Correction Matrix (CCM)** addresses this second source of error by mixing a small, controlled fraction of each channel's CIE-corrected output into its neighbours. This is the same technique used in professional LED processors and ICC display profiles.

---

### Two-Stage Colour Pipeline

The full colour pipeline works in two consecutive stages, each handling a distinct correction:

```
 8-bit input  ┌─────────────────────────────────────────┐  10-bit output
 R, G, B ───▶ │  Stage 1: CIE LUT + per-channel CAP     │ ──▶ rv, gv, bv
              │  (baked into CIE_RED/GREEN/BLUE)        │
              └─────────────────────────────────────────┘
                                   │
                                   ▼
              ┌─────────────────────────────────────────┐  10-bit output
              │  Stage 2: CCM cross-channel mixing      │ ──▶ rv′, gv′, bv′
              │  (additive, integer shift arithmetic)   │
              └─────────────────────────────────────────┘
                                   │
                                   ▼
                        32-bit packed pixel word
                        (bv′ << 20) | (gv′ << 10) | rv′
```

The two stages are **orthogonal**: the CIE LUT correction and the `RED_CAP` / `GREEN_CAP` / `BLUE_CAP` scaling factors in `cie.py` remain completely unchanged when CCM is enabled. CCM operates on the already CIE- and CAP-corrected 10-bit values.

---

### Mathematical Model

The CCM adds cross-channel contributions using a superposition model:

```
r′ = clamp( rv  +  (gv >> CCM_RG_SHIFT)  +  (bv >> CCM_RB_SHIFT) )
g′ = clamp( gv  +  (rv >> CCM_GR_SHIFT)  +  (bv >> CCM_GB_SHIFT) )
b′ = clamp( bv  +  (rv >> CCM_BR_SHIFT)  +  (gv >> CCM_BG_SHIFT) )
```

Each coefficient is expressed as an integer **right-shift amount**, which avoids floating-point arithmetic entirely. The equivalent fractional contribution is:

| Shift value | Added fraction | Typical use |
|:-----------:|:--------------:|:------------|
| `5`  | ≈ 3.1 % | Strong correction |
| `6`  | ≈ 1.6 % | Moderate correction |
| `7`  | ≈ 0.8 % | Fine correction |
| `8`  | ≈ 0.4 % | Very fine correction |
| `9`  | ≈ 0.2 % | Barely perceptible |
| `31` | = 0.0 % | **Disabled** (identity, no bleed) |

Setting a shift to `31` disables that cross-term completely — a shift of 31 on a 10-bit value always produces zero. All six cross-terms default to `31`, so CCM is **off by default** and introduces no change to the output unless explicitly configured.

---

### Configuration in Code

CCM is configured exclusively through the `color` fields of `Hub75Config` — no source file
edits are required beyond the one-line change to `pack_lut_rgb_`.

```cpp
constexpr Hub75Config panel_cfg{
    // ... panel / pins / etc ...
    .color = {
        .separate_cie_channels = true,   // required — CCM needs per-channel LUTs

        // Colour Correction Matrix cross-terms.
        // Omitting a field leaves it at the default of 31 (= disabled).
        .ccm_rg_shift = 6,                // ~1.6% of Green added into Red
        .ccm_gb_shift = 7,                // ~0.8% of Blue  added into Green
        // .ccm_rb_shift = 31,            // disabled
        // .ccm_gr_shift = 31,            // disabled
        // .ccm_br_shift = 31,            // disabled
        // .ccm_bg_shift = 31,            // disabled
    },
};
```

> ⚠️ CCM requires `color.separate_cie_channels = true`. With `separate_cie_channels = false`
> all three channels share a single `CIE` table and cross-channel mixing has no meaningful
> effect.

---

### The `cie.py` LUT Generator

`cie.py` generates the complete `cie.hpp` header, including all `#if SEPARATE_CIE_CHANNELS` and `#if BITPLANES` preprocessor branches for both 8-bit and 10-bit colour depth. The per-channel scaling factors `RED_CAP`, `GREEN_CAP`, `BLUE_CAP` handle white-balance gain differences between the three LED colours independently of the CCM cross-terms.

Regenerate after any change to the CAP values:

```bash
python3 utils/cie.py > cie.hpp
```

The CCM shift fields in `Hub75Config` are unaffected and do not require a re-run of `cie.py`.

---

### Tuning Procedure

Colour calibration follows a strict order: white-balance first, cross-channel correction second. Mixing the two steps produces confusing interactions.

#### Step 1 — Establish a baseline

Set all six CCM shift fields to `31` in your `Hub75Config` (or omit them entirely — `31` is the default). Rebuild and flash. This confirms that the output is identical to the pre-CCM state and gives you a known reference point.

#### Step 2 — Use a grey-ramp test image

A grey-ramp effect has been added to `hub75_demo.cpp`:

```cpp
// Diagnostic grey ramp — equal R, G, B at every luminance level.
// On a perfectly calibrated panel this ramp appears neutral grey
// from black to white with no colour tint at any brightness.
    ...
    else if (demo_index == 7)
    {
        greyScaleStripes.drawStripes();
        update(&greyScaleStripes);
    }
```

Observe the ramp carefully:

| Visible tint | Likely cause | First term to try |
|:-------------|:-------------|:------------------|
| Warm / orange-red cast | Green leaking into Red | `CCM_RG_SHIFT=6` |
| Cool / blue-green cast | Blue leaking into Green | `CCM_GB_SHIFT=7` |
| Red cast in bright areas | Blue leaking into Red | `CCM_RB_SHIFT=7` |
| Green cast overall | Red leaking into Green | `CCM_GR_SHIFT=7` |
| Yellow cast | Both Red and Green too high | Reduce `RED_CAP` in `cie.py` first |

#### Step 3 — Tune one term at a time

Enable only the single most visible cross-term and rebuild. Never change more than one shift value per build-flash-observe cycle. The following starting values work well for most common indoor HUB75 panels:

```cpp
.ccm_rg_shift = 6,   // ~1.6% Green → Red  (most panels need this)
.ccm_gb_shift = 7,   // ~0.8% Blue  → Green
```

Each increment of the shift value **halves** the contribution. Each decrement **doubles** it. A shift of `5` (≈ 3.1%) is usually already too strong and risks a visible tint in the opposite direction.

#### Step 4 — Verify with saturated primaries

Display solid full-brightness red (`255, 0, 0`), green (`0, 255, 0`), and blue (`0, 0, 255`) in turn. Each primary should appear as a pure, uncontaminated hue. Then display the additive secondaries — yellow (`255, 255, 0`), cyan (`0, 255, 255`), magenta (`255, 0, 255`) — and verify they look balanced.

#### Step 5 — Check with a real image

Use a photographic test image containing known neutral greys, skin tones, and saturated colours. The grey areas should remain neutral; skin tones should appear natural; saturated hues should not appear shifted relative to the original.

#### Step 6 — Final white-balance trim

If a residual gain imbalance remains after CCM is set (e.g. pure white still looks faintly warm), adjust `RED_CAP`, `GREEN_CAP`, or `BLUE_CAP` in `cie.py`, regenerate the LUT tables, and rebuild. Do not use CCM cross-terms to compensate for a simple gain imbalance — that is the job of the CAP factors.

---

### Runtime Cost

`CCM_APPLY` consists of six integer right-shifts, six integer additions, and three saturating comparisons. On the RP2040 (Cortex-M0+) and RP2350 (Cortex-M33) this amounts to approximately 8–10 additional clock cycles per pixel, executed only during `update()` or `update_bgr()`.

For a 64 × 64 panel at 266 MHz the total overhead per `update()` call is approximately **0.12 µs** — completely negligible compared to the DMA and PIO transfer time.

## Brightness Control

In addition to bitplane modulation, the driver supports **software-based brightness regulation**. This allows easy adjustment of overall panel brightness without hardware changes.

### API Functions

```cpp
// Set the baseline brightness scaling factor (default = 6, range 1–255).
// Larger values increase brightness but also raise OEn frequency.
void setBasisBrightness(uint8_t factor);

// Set fine-grained brightness intensity as a fraction [0.0 – 1.0].
void setIntensity(float intensity);
```

### How it Works

- <code>setBasisBrightness(basis)</code>

  Defines the top brightness.

  Example: <code>setBasisBrightness(6)</code> → default brightness range for typical 64×64 panels. \
  Larger factors give more headroom for brightness but consume more **Binary Coded Modulation (BCM)** time slices.

- <code>setIntensity(intensity)</code>
  
  Fine-grained adjustment from 0.0 (dark/off) to 1.0 (full brightness).\
  This function scales the effective duty cycle within the current baseline brightness range.

```cpp
// Example: brighten the panel, then dim at runtime
setBasisBrightness(8); // Start with baseline factor 8 for a brighter panel
setIntensity(0.5f);    // Show at 50% of that baseline
```

### Default Settings

- <code>basis_factor = 6u</code>
- <code>intensity = 1.0f</code>
  (full brightness within the baseline)

This corresponds to the same brightness as earlier driver revisions without adjustment.

### Practical Notes

- Increasing the basis factor may increase peak current consumption.
- For indoor use, values between 4–8 are usually sufficient.
- For dimmer environments, you can keep the baseline factor low (e.g. 4) and rely on setIntensity() for smooth runtime control.
- Both functions are non-blocking and can be called during normal operation.

