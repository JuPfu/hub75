from sys import stdout

TABLE_SIZE = 256
RESOLUTION = 1024  # 10-bit (0-1023)

# White Balance Scaling (0.0 to 1.0)
# Adjust these based on your specific panel's look
# Usually Green is brightest, so we might pull it back to 0.9
# Blue often needs to be stronger (1.0), Red around 0.8-0.9
RED_CAP = 0.988
GREEN_CAP = 1.00
BLUE_CAP = 1.00

# CCM cross-channel shifts — nur zur Dokumentation/Vorschau hier,
# die eigentliche Wirkung passiert zur Compile-Zeit in hub75.hpp.
# shift=31 → deaktiviert, shift=6 → 1.6%, shift=7 → 0.8%
CCM_SHIFTS = {
    "RG": 6,  # Grün → Rot
    "RB": 31,  # Blau → Rot    (aus)
    "GR": 31,  # Rot  → Grün   (aus)
    "GB": 7,  # Blau → Grün
    "BR": 31,  # Rot  → Blau   (aus)
    "BG": 31,  # Grün → Blau   (aus)
}


def cie1931(L):
    L *= 100.0
    if L <= 8:
        return ((L + 16.0) / 116.0 - 4.0 / 29.0) * 3.0 * (6.0 / 29.0) ** 2
    else:
        return ((L + 16.0) / 116.0) ** 3


def generate_table(name, cap):
    x = range(0, TABLE_SIZE)
    # Apply the CIE curve and then scale by the channel cap
    y = [cie1931(float(L) / (TABLE_SIZE - 1)) * (RESOLUTION - 1) * cap for L in x]
    
    stdout.write(f'static const uint16_t CIE_{name}[{TABLE_SIZE}] = {{')
    for i, L in enumerate(y):
        if i % 16 == 0:
            stdout.write("\n    ")
        stdout.write("% 5d," % round(L))
    stdout.write("\n};\n\n")


def print_ccm_summary():
    stdout.write("// CCM Cross-term summary (shift → approximate percentage):\n")
    for key, shift in CCM_SHIFTS.items():
        if shift >= 31:
            pct = 0.0
            state = "disabled"
        else:
            pct = 100.0 / (1 << shift)
            state = "active"
        src = key[1]  # second char = source channel
        dst = key[0]  # first char  = destination channel
        stdout.write(
            f"//   CCM_{key}_SHIFT={shift:2d}  →  {pct:.1f}% of {src} "
            f"added into {dst}  ({state})\n"
        )
    stdout.write("\n")


print_ccm_summary()
generate_table("RED", RED_CAP)
generate_table("GREEN", GREEN_CAP)
generate_table("BLUE", BLUE_CAP)
