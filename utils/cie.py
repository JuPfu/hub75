from sys import stdout

TABLE_SIZE = 256 
# Target 10-bit for your build_bitplanes (0-1023)
# Or 12-bit (0-4095) if you plan to implement temporal dithering later
RESOLUTION = 1024 

# White Balance Scaling (0.0 to 1.0)
# Adjust these based on your specific panel's look
# Usually Green is brightest, so we might pull it back to 0.9
# Blue often needs to be stronger (1.0), Red around 0.8-0.9
RED_CAP = 0.988
GREEN_CAP = 1.00
BLUE_CAP = 1.00

def cie1931(L):
    L *= 100.0
    if L <= 8:
        return ((L + 16.0) / 116.0 - 4.0 / 29.0) * 3.0 * (6.0 / 29.0)**2
    else:
        return ((L + 16.0) / 116.0)**3

def generate_table(name, cap):
    x = range(0, TABLE_SIZE)
    # Apply the CIE curve and then scale by the channel cap
    y = [cie1931(float(L) / (TABLE_SIZE - 1)) * (RESOLUTION - 1) * cap for L in x]
    
    stdout.write(f'static const uint16_t CIE_{name}[{TABLE_SIZE}] = {{')
    for i, L in enumerate(y):
        if i % 16 == 0:
            stdout.write('\n    ')
        stdout.write('% 5d,' % round(L))
    stdout.write('\n};\n\n')

generate_table("RED", RED_CAP)
generate_table("GREEN", GREEN_CAP)
generate_table("BLUE", BLUE_CAP)