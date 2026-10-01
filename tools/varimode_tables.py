#!/usr/bin/env python3
"""The lookup tables of firmware/varimode/varimode_fixed.asm, from first principles.

The firmware measures the input in half-semitone steps (h = floor(60 * code / 512), the 10-bit
ADC code taken as 0..5 V over 60 semitones) and looks up, for each of the 24 half-steps in an
octave, the nearest degree of the chosen scale. Then it looks up the PWM duty for the note.

    varimode_tables.py          prints the DT lines to paste into the assembly

The test in tests/Pic16 compares the assembled firmware with an independent quantizer, input by
input, so a wrong table cannot get through.
"""
SCALES = [            # in the firmware's mode order: the bands of the mode pin
    ("MAJOR",       [0, 2, 4, 5, 7, 9, 11]),
    ("MAJOR PENT",  [0, 2, 4, 7, 9]),
    ("MINOR",       [0, 2, 3, 5, 7, 8, 10]),
    ("MINOR PENT",  [0, 3, 5, 7, 10]),
    ("CHROMATIC",   list(range(12))),
]


def nearest(degrees, x):
    """The in-scale semitone nearest x, searching the octave either side. x is never exactly
    midway between two degrees: the half-step regions are probed at their centres."""
    best = None
    for k in (-1, 0, 1):
        for d in degrees:
            n = 12 * k + d
            if best is None or abs(n - x) < abs(best - x):
                best = n
    return best


def mode_table():
    rows = []
    for name, deg in SCALES:
        row = []
        for hp in range(24):
            x = (hp + 0.5) / 2.0
            n = nearest(deg, x)
            assert 0 <= n <= 12, (name, hp, n)
            row.append(n)
        rows.append((name, row))
    return rows


def duty(q):
    """The PWM duty that puts q semitones (q/12 volts) on a 5 V, 10-bit output."""
    return min(1023, int(round(q * 1024.0 / 60.0)))


if __name__ == '__main__':
    print("; mode table: 5 scales x 24 half-steps -> nearest degree, in semitones above the octave's C (0..12)")
    for name, row in mode_table():
        print("        DT      " + ",".join("%d" % v for v in row[:12]) + "      ; %s, first half" % name)
        print("        DT      " + ",".join("%d" % v for v in row[12:]) + "      ; %s, second half" % name)
    print("; duty table: CCPR1L (duty >> 2) for notes 0..60")
    hi = [duty(q) >> 2 for q in range(61)]
    lo = [duty(q) & 3 for q in range(61)]
    for i in range(0, 61, 12):
        print("        DT      " + ",".join("%d" % v for v in hi[i:i + 12]))
    print("; duty table: the two DC1B bits for notes 0..60")
    for i in range(0, 61, 12):
        print("        DT      " + ",".join("%d" % v for v in lo[i:i + 12]))
