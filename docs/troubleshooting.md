# Troubleshooting

If your panel does not behave as expected, do **not** change multiple configuration options at once.
Most issues can be isolated by checking **one dimension at a time**.

The sections below are ordered from **most common** to **least common** problems.

---

## 1. Panel Stays Completely Dark

### Check the obvious first

* Is the panel powered with the **correct voltage** (usually 5 V)?
* Is the power supply strong enough (HUB75 panels can draw several amps)?
* Is `pins.oen_pin` wired correctly and not permanently disabling output?

### Configuration checks

* Verify `panel.matrix_panel_width` and `panel.matrix_panel_height`
* Verify `pins.rowsel_n_pins` matches the number of address pins on the panel (A, B, C, …)

If `pins.rowsel_n_pins` is too large or too small, **no rows will be selected correctly**.

---

## 2. Panel Lights Up, But Only Shows Noise or Garbage

This usually indicates a **pixel mapping mismatch**.

### What to check

* Try a different `panel_kind` value:

  ```cpp
  .panel_kind = RowMapping::Standard,
  // .panel_kind = RowMapping::Split,
  // .panel_kind = RowMapping::S31,
  ```

### Typical symptoms

| Symptom                       | Likely cause                     |
| ----------------------------- | -------------------------------- |
| Completely scrambled image    | Wrong `panel_kind`               |
| Image mirrored or interleaved | Wrong internal wiring assumption |
| Repeating blocks or patterns  | Mapping partially correct        |

> 💡 If the image is *stable but wrong*, the scan rate is likely correct and only the mapping needs adjustment.

---

## 3. Image Looks Correct, But Rows Are Missing or Repeated

This usually points to a **row addressing issue**.

### Check

* `pins.rowsel_n_pins`
* Panel height (`panel.matrix_panel_height`)

### Rule reminder

```
multiplexed_rows = matrix_panel_height / 2^rowsel_n_pins
```

If this value does not match the panel's actual multiplexing, rows will be:

* skipped
* duplicated
* shifted

---

## 4. Image Is Correct but Flickers or Shows Ghosting

This is typically a **timing issue**.

### Things to try

1. Tune `sm_clockdiv_factor`:

   ```cpp
   .panel = { .sm_clockdiv_factor = 1.0f },
   ```

2. Increase the divider if necessary:

   * ≤ 16 rows → try `8.0f`
   * ≤ 32 rows → try `2.0f` or `4.0f`

### Also check

* Power supply quality
* Cable length and signal integrity
* Ground connection between MCU and panel

---

## 5. Panel Updates Sporadically or Only Every Few Frames

This often indicates **strobe polarity mismatch**.

Try

```cpp
.panel = { .inverted_stb = true },
```

If the panel suddenly becomes stable, the latch signal is inverted on your board.

---

## 6. Colors Look Wrong or Are Too Dim / Too Bright

### Check

* Panel driver chip type:

  ```cpp
  .panel_chip = Hub75PanelChip::GENERIC,
  // .panel_chip = Hub75PanelChip::FM6126A,
  // .panel_chip = Hub75PanelChip::RUL6024,
  ```

If the panel contains an FM6126A or RUL6024 chip and is not initialized correctly:

* brightness may be wrong
* colors may look distorted
* output may be unstable

### How to verify

* Inspect the **back of the panel**
* Look for chip markings (FM6126A, RUL6024)

---

## 7. When Nothing Makes Sense Anymore 😄

Follow this **minimal recovery procedure**:

1. Use the simplest known-good configuration:

   ```cpp
   .panel_kind = RowMapping::Standard,   // driver default
   ```

2. Verify:

   * correct width and height
   * correct `pins.rowsel_n_pins`

3. Use a **simple test pattern**:

   * solid colors
   * vertical and horizontal stripes

4. Change **one parameter at a time**
