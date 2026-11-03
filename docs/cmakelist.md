# CMake Configuration

This page explains everything you may need to change in [`CMakeLists.txt`](../CMakeLists.txt#28) (or in the VS Code Raspberry Pi Pico extension) to build the `Hub75 driver` for **your** board.

> **Rule of thumb:** `CMakeLists.txt` decides *how the firmware is built* (target board, chip variant, which libraries). The C++ configuration in `hub75_demo.cpp` (`constexpr Hub75Config`) decides *how the panel is wired and driven*. See [Configuration in Code](../README.md#configuration-in-code) and [Configuration reference](configuration.md).

## Contents

- [CMake Configuration](#cmake-configuration)
  - [Contents](#contents)
  - [Which settings do I need?](#which-settings-do-i-need)
  - [Standard boards: Pico, Pico W, Pico 2, Pico 2 W](#standard-boards-pico-pico-w-pico-2-pico-2-w)
  - [Selecting a board in VS Code ("Switch Board")](#selecting-a-board-in-vs-code-switch-board)
  - [Other boards from the board list](#other-boards-from-the-board-list)
  - [Bare RP2350 and RP2350B (professional use)](#bare-rp2350-and-rp2350b-professional-use)
    - [Step 1: CMakeLists.txt](#step-1-cmakeliststxt)
    - [Step 2: Pin configuration in your application](#step-2-pin-configuration-in-your-application)
    - [Choosing PICO\_RP2350A correctly](#choosing-pico_rp2350a-correctly)
  - [Build flags reference](#build-flags-reference)
  - [Using hub75 as a library](#using-hub75-as-a-library)
  - [Troubleshooting](#troubleshooting)

---

## Which settings do I need?

| Your hardware | `PICO_BOARD` | `PICO_PLATFORM` | `PICO_RP2350A=0` | GPIO range | Section |
|:--|:--|:--|:--|:--|:--|
| Raspberry Pi **Pico** (RP2040) | `pico` | – (implied by board) | no | 0–29 | [Standard boards](#standard-boards-pico-pico-w-pico-2-pico-2-w) |
| Raspberry Pi **Pico W** (RP2040) | `pico_w` | – | no | 0–29 | [Standard boards](#standard-boards-pico-pico-w-pico-2-pico-2-w) |
| Raspberry Pi **Pico 2** (RP2350A) | `pico2` (**default**) | – | no | 0–29 | [Standard boards](#standard-boards-pico-pico-w-pico-2-pico-2-w) |
| Raspberry Pi **Pico 2 W** (RP2350A) | `pico2_w` | – | no | 0–29 | [Standard boards](#standard-boards-pico-pico-w-pico-2-pico-2-w) |
| Another ready-made board with RP2040/RP2350 | name from the board list | – | depends on package | depends on board | [Other boards](#other-boards-from-the-board-list) |
| **Bare RP2350A** (own PCB, no board definition) | `none` | `rp2350` | no | 0–29 | [Bare RP2350](#bare-rp2350-and-rp2350b-professional-use) |
| **Bare RP2350B** (own PCB, no board definition) | `none` | `rp2350` | **yes** | 0–47 | [Bare RP2350](#bare-rp2350-and-rp2350b-professional-use) |

> 💡 If you own a standard Pico / Pico 2 board and wired the panel as shown in [Wiring Details](../README.md#wiring-details), the only line you may have to touch is `PICO_BOARD`.

---

## Standard boards: Pico, Pico W, Pico 2, Pico 2 W

These four boards are the "standard" targets of the driver. The default configuration in `CMakeLists.txt` selects the **Pico 2**:

```cmake
# ⚠️ look at or near line 28
set(PICO_BOARD pico2 CACHE STRING "Board type")  # pico2 = RP2350; switch to pico for RP2040

# ⚠️ look at or near line 79
target_compile_definitions(hub75_demo PRIVATE
    # PICO_RP2350A=0             # PICO_RP2350A=0` means not a RP2350A but a RP2350B microcontroller - uncomment for RP235xB microcontroller only!
    USE_PICO_GRAPHICS=true       # set to false if you use hub75 as a library
    HUB75_MULTICORE=true         # use core1 for the hub75 driver
)
```

To change the target, replace the board name in the `set(PICO_BOARD ...)` line:

| Board | Chip | Line to use |
|:--|:--|:--|
| Raspberry Pi Pico | RP2040 | `set(PICO_BOARD pico CACHE STRING "Board type")` |
| Raspberry Pi Pico W | RP2040 | `set(PICO_BOARD pico_w CACHE STRING "Board type")` |
| Raspberry Pi Pico 2 | RP2350A | `set(PICO_BOARD pico2 CACHE STRING "Board type")` |
| Raspberry Pi Pico 2 W | RP2350A | `set(PICO_BOARD pico2_w CACHE STRING "Board type")` |

What the driver does on these boards:

- The `Hub75 driver` runs on the second core (`core1`), your application logic runs on `core0` (`HUB75_MULTICORE=true`).
- The graphics are drawn with Pimoroni's PicoGraphics library (`USE_PICO_GRAPHICS=true`).
- The default pin mapping (GPIO 0–13) works unchanged on all four boards.

> ⚠️ The demo has been tested on **Pico 2 (RP2350A)** and **RP2350B**. On an RP2040 (Pico, Pico W) you may need to comment out some demo effects because of tighter memory constraints. Open an issue if you need help.

---

## Selecting a board in VS Code ("Switch Board")

With the **Raspberry Pi Pico** extension for VS Code you do not have to edit `CMakeLists.txt` by hand:

1. Open the project (see [How to Use This Project in VSCode](../README.md#how-to-use-this-project-in-vscode)).
2. Use the **Switch Board** entry of the Raspberry Pi Pico extension (side bar panel, or the board name shown in the bottom status bar).
3. Pick your board from the list. The list is long: it contains all boards known to the installed Pico SDK (Raspberry Pi, Pimoroni, Adafruit, SparkFun, Waveshare, ...).
4. Let CMake re-configure the project, then build.

Things worth knowing:

- **`CACHE` and precedence.** `set(PICO_BOARD pico2 CACHE STRING ...)` only provides a *default*. A value that is already in the CMake cache, for example one passed by the extension via `-DPICO_BOARD=...`, wins over it. That is why the board chosen in VS Code can differ from the board named in `CMakeLists.txt`.
- **Re-configure after switching.** If the build still targets the old board, delete the `build` directory (or run *CMake: Delete Cache and Reconfigure*) and build again.
- **Switch Board covers the board only.** It does not know about `PICO_RP2350A=0`, `USE_PICO_GRAPHICS` or `HUB75_MULTICORE`. These flags stay in `target_compile_definitions` of `CMakeLists.txt`.
- **Check the build output.** At the start of the CMake configuration the Pico SDK prints the selected board and platform. Compare them with what you expect.

---

## Other boards from the board list

Any board offered by *Switch Board* can be used, as long as it is based on an RP2040 or RP2350 and exposes enough GPIOs for the panel. The name shown in the list is the value for `PICO_BOARD`.
A complete list of supported boards can be found here [boards](https://github.com/raspberrypi/pico-sdk/tree/master/src/boards/include/boards).

Checklist for a board that is not a "standard" Pico:

1. **Chip and package.** RP2040 and RP2350**A** (30 GPIOs) behave like the standard boards. An RP2350**B** (48 GPIOs) needs the extra settings described in [Bare RP2350 and RP2350B](#bare-rp2350-and-rp2350b-professional-use). Boards whose SDK board definition already declares the RP2350B package may not need `PICO_RP2350A=0` explicitly; check the board header in the Pico SDK (`src/boards/include/boards/<board>.h`) if unsure.
2. **Free GPIOs.** The driver needs three continuous blocks of GPIOs (colour, address, control), see [Pin Assignment](../README.md#pin-assignment-default). Make sure your board does not use some of them for onboard hardware (LEDs, flash, wireless chip, SD card, ...). If it does, move the blocks via `.pins` in `Hub75Config`.
3. **Memory.** RP2040 boards have less RAM than RP2350 boards. Use the memory/quality switches `color.bitplanes`, `color.balanced_light_output` and `color.separate_cie_channels` to adapt.
4. **Report back.** Boards that worked (or did not) are valuable feedback. Please open an issue on [GitHub](https://github.com/JuPfu/hub75), see also [Supported, available and tested boards](boards.md).

---

## Bare RP2350 and RP2350B (professional use)

Use this variant if you build your own PCB around an RP2350 chip, or if you want the additional GPIOs of the **RP2350B**, which comes in the larger QFN-80 package with 48 GPIOs. Without a ready-made board definition you must tell the Pico SDK which chip you use.

### Step 1: CMakeLists.txt

Replace the board line and add the platform:

```cmake
# ⚠️ look at or near line 26
# set(PICO_BOARD pico2 CACHE STRING "Board type")          # <- comment out

# The following two lines must be uncommented to compile for bare RP2350 without a board
set(PICO_PLATFORM rp2350)
set(PICO_BOARD none CACHE STRING "Board type")
```

For an **RP2350A** without board definition these two lines are all that changes.

For an **RP2350B** additionally define `PICO_RP2350A=0`:

```cmake
# ⚠️ look at or near line 78
target_compile_definitions(hub75_demo PRIVATE
    PICO_RP2350A=0               # RP2350B microcontrollers only - remove or comment out for RP2350A
    USE_PICO_GRAPHICS=true       # set to false if you use hub75 as a library
    HUB75_MULTICORE=true         # use core1 for the hub75 driver
)
```

### Step 2: Pin configuration in your application

The RP2350B has more GPIOs than the Pico 2, so the panel can be wired to the upper pins. The example below uses GPIO 30 to 43:

```cpp
constexpr Hub75Config panel_cfg{
    .panel = {
        .matrix_panel_width = 64,   // your matrix panel width
        .matrix_panel_height = 64,  // your matrix panel height
    },
    .pins = {
        .data_base_pin = 30,        // R0, G0, B0, R1, G1, B1 -> GPIO 30..35
        .data_n_pins = 6,
        .rowsel_base_pin = 36,      // A, B, C, D, E          -> GPIO 36..40
        .rowsel_n_pins = 5,
        .clk_pin = 41,              // CLK
        .strobe_pin = 42,           // STROBE (LATCH)
        .oen_pin = 43,              // OE (GCLK for PWM panel class)
    },
};
```

| Group | RP2350A / Pico 2 (default) | RP2350B (example) |
|:--|:--|:--|
| Colour data R0 G0 B0 R1 G1 B1 | GPIO 0–5 | GPIO 30–35 |
| Address A B C D E | GPIO 6–10 | GPIO 36–40 |
| CLK / STROBE / OE | GPIO 11 / 12 / 13 | GPIO 41 / 42 / 43 |

> ⚠️ The colour, address and control pins must each form a continuous block in ascending order. This rule is the same on all chips.

### Choosing PICO_RP2350A correctly

| Chip | `PICO_RP2350A` | Meaning |
|:--|:--|:--|
| RP2350A (30 GPIOs, Pico 2) | not set (default) | The SDK assumes the A package |
| RP2350B (48 GPIOs) | `PICO_RP2350A=0` | Tells the SDK that GPIO 30–47 exist |

If `PICO_RP2350A=0` is missing on an RP2350B design, GPIOs above 29 are not available to the SDK and the panel stays dark or the build complains about invalid pin numbers.

---

## Build flags reference

Only flags that describe *how the project is built* live in `CMakeLists.txt`. Everything about wiring and timing is configured in C++ via `Hub75Config`.

| Flag / variable | Where | Default | Purpose |
|:--|:--|:--|:--|
| `PICO_BOARD` | `set(... CACHE STRING)` or VS Code | `pico2` | Target board; `none` for a bare chip |
| `PICO_PLATFORM` | `set(...)` | implied by board | `rp2350` for a bare RP2350 (only together with `PICO_BOARD none`) |
| `PICO_RP2350A=0` | `target_compile_definitions` | not set | RP2350**B** package (48 GPIOs) |
| `USE_PICO_GRAPHICS` | `target_compile_definitions` | `true` | Use Pimoroni's PicoGraphics library; set `false` if you use hub75 as a library |
| `HUB75_MULTICORE` | `target_compile_definitions` | `true` | Run the `Hub75 driver` on `core1`, your application on `core0` |

---

## Using hub75 as a library

If you include the driver in your own project instead of building the demo:

- Set `USE_PICO_GRAPHICS=false` if your application does not use PicoGraphics.
- Choose `PICO_BOARD` (and, if needed, `PICO_PLATFORM` / `PICO_RP2350A=0`) in the `CMakeLists.txt` of **your** project, exactly as described above.
- Put your `constexpr Hub75Config` into your application code and instantiate `Hub75Driver<Cfg>` with static storage duration, see [Configuration in Code](../README.md#configuration-in-code).

---

## Troubleshooting

| Symptom | Likely cause | Remedy |
|:--|:--|:--|
| Build targets the wrong board after *Switch Board* | Old value in CMake cache | Delete `build` directory or *Delete Cache and Reconfigure* |
| `PICO_BOARD` in `CMakeLists.txt` seems to be ignored | `CACHE` default is overridden by a cached or command-line value | Change the board via *Switch Board*, or clean the cache |
| Errors about GPIO numbers above 29 | RP2350B without `PICO_RP2350A=0` | Add the definition, see [RP2350B](#bare-rp2350-and-rp2350b-professional-use) |
| Panel stays dark, build is fine | Pins in `Hub75Config` do not match the wiring | Compare with [Pin Assignment](../README.md#pin-assignment-default) |
| Out of memory on RP2040 | Heavy colour settings | Reduce `bitplanes`, disable `separate_cie_channels` or `balanced_light_output`, or comment out demo effects |
| Flicker or ghosting | Driver too fast for the panel | Try a larger `sm_clockdiv_factor`, see [Configuration reference](configuration.md) |

Still stuck? See [Troubleshooting](troubleshooting.md) or open an issue on [GitHub](https://github.com/JuPfu/hub75).