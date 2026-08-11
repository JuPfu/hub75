from sys import stdout

TABLE_SIZE = 256

# White Balance Scaling (0.0 to 1.0)
# Adjust these based on your specific panel's look
# Usually Green is brightest, so we might pull it back to 0.9
# Blue often needs to be stronger (1.0), Red around 0.8-0.9
RED_CAP = 0.988
GREEN_CAP = 1.00
BLUE_CAP = 1.00

# CCM cross-channel shifts — for documentation purposes only; 
# the actual effect occurs at compile time as a template parameter in hub75.hpp.
# shift=31 → deactivated, shift=6 → 1.6%, shift=7 → 0.8%
CCM_SHIFTS = {
    "RG": 6,   # green → red
    "RB": 31,  # blue  → red     (off)
    "GR": 31,  # red   → green   (off)
    "GB": 7,   # blue  → green
    "BR": 31,  # red   → blue    (off)
    "BG": 31,  # green → blue    (off)
}

# (bitplanes, resolution) pairs to generate tables for.
# In the template-driven version BITPLANES is a template parameter, not a
# preprocessor macro, so every resolution's tables must exist unconditionally
# and be selected at compile time (e.g. via if constexpr / partial specialization).
RESOLUTIONS = [(10, 1024), (8, 256)]

CHANNELS = [
    ("RED",   RED_CAP),
    ("GREEN", GREEN_CAP),
    ("BLUE",  BLUE_CAP),
]


def cie1931(L):
    L *= 100.0
    if L <= 8:
        return ((L + 16.0) / 116.0 - 4.0 / 29.0) * 3.0 * (6.0 / 29.0) ** 2
    else:
        return ((L + 16.0) / 116.0) ** 3


def make_table(resolution, cap):
    """Return a list of TABLE_SIZE rounded integer values."""
    return [
        round(cie1931(float(L) / (TABLE_SIZE - 1)) * (resolution - 1) * cap)
        for L in range(TABLE_SIZE)
    ]


def write_table(name, values, trailing_blank=True):
    stdout.write(f"static constexpr uint16_t {name}[{TABLE_SIZE}] = {{\n")
    rows = []
    for i in range(0, TABLE_SIZE, 16):
        rows.append("    " + ", ".join(str(v) for v in values[i:i + 16]) + ",")
    rows[-1] = rows[-1][:-1]  # drop trailing comma on the very last value
    stdout.write("\n".join(rows))
    stdout.write("};\n")
    if trailing_blank:
        stdout.write("\n")


def write_hpp():
    stdout.write("#pragma once\n\n")
    stdout.write("#include <cstdint>\n\n")
    stdout.write("// Deduced from https://jared.geek.nz/2013/02/linear-led-pwm/\n")
    stdout.write("// The CIE 1931 lightness formula is what actually describes how we perceive light.\n\n")

    # Shared table per resolution (GREEN_CAP == 1.0, so CIE10/CIE8 match the
    # green channel exactly — kept for callers that don't need per-channel CCM).
    for bitplanes, resolution in RESOLUTIONS:
        write_table(f"CIE{bitplanes}", make_table(resolution, GREEN_CAP))

    # Per-channel, white-balance-capped tables for each resolution.
    tables = [
        (f"CIE{bitplanes}_{name}", resolution, cap)
        for bitplanes, resolution in RESOLUTIONS
        for name, cap in CHANNELS
    ]
    for i, (table_name, resolution, cap) in enumerate(tables):
        write_table(table_name, make_table(resolution, cap), trailing_blank=(i < len(tables) - 1))


write_hpp()