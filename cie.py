# Generate lookup table for converting between perceived LED brightness and PWM

# Adapted from: https://jared.geek.nz/2013/feb/linear-led-pwm
# See also: https://ledshield.wordpress.com/2012/11/13/led-brightness-to-your-eye-gamma-correction-no/

from sys import stdout

TABLE_SIZE = 256 # Number of steps (brightness)
RESOLUTION = 2**10 # PWM resolution (10-bit = 1024)

def cie1931(L):
    L *= 100.0
    if L <= 8:
        return ((L + 16.0) / 116.0 - 4.0 / 29.0) * 3.0 * (6.0 / 29.0)**2
    else:
        return ((L + 16.0) / 116.0)**3

x = range(0, TABLE_SIZE)
y = [cie1931(float(L) / (TABLE_SIZE - 1)) * (RESOLUTION - 1) for L in x]

stdout.write('const uint16_t CIE[%d] = {' % TABLE_SIZE)

for i, L in enumerate(y):
    if i % 16 == 0:
        stdout.write('\n')
    stdout.write('% 5d,' % round(L))

stdout.write('\n};\n')