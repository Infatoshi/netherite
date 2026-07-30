#!/usr/bin/env python3
"""Deterministic input stream for 13_ao_pack_helpers.
Each line: "br1 br2 br3 br4 f1hex f2hex f3hex f4hex"
 - br1..br4: signed int32 packed-brightness values (decimal). Includes zeros (to hit the
   br==0 substitution branch), typical 0x00RR00GG packed values, and full-range randoms.
 - f1..f4: vertex weights as raw IEEE-754 float bits (hex) so both sides read identical floats.
   Weights are in [0, 1.5] (typical AO blend weights, occasionally summing > 1)."""
import random
import struct

INT_MIN = -(2**31)
INT_MAX = 2**31 - 1


def fbits(f):
    return format(struct.unpack("<I", struct.pack("<f", f))[0], "x")


def main():
    rnd = random.Random(1311)
    out = []

    def emit(b, weights):
        out.append(" ".join(str(x) for x in b) + " " + " ".join(fbits(w) for w in weights))

    # edge cases: all-zero ints, single zeros, exact weight patterns
    emit([0, 0, 0, 0], [0.25, 0.25, 0.25, 0.25])
    emit([0, 0, 0, 0x00FF00FF], [1.0, 0.0, 0.0, 0.0])
    emit([0x00FF00FF, 0, 0, 0x00120034], [0.25, 0.25, 0.25, 0.25])
    emit([0x000000FF, 0x00FF0000, 0x00808080, 0x00010203], [0.5, 0.5, 0.0, 0.0])

    for _ in range(40000):
        # typical packed brightness: 0x00RR00GG-ish (low + mid bytes)
        b = []
        for _ in range(4):
            if rnd.random() < 0.15:
                b.append(0)
            else:
                hi = rnd.randint(0, 255)
                lo = rnd.randint(0, 255)
                b.append((hi << 16) | lo)
        w = [round(rnd.uniform(0.0, 1.5), 6) for _ in range(4)]
        emit(b, w)

    for _ in range(20000):
        # full-range signed int32 (stress wrap/sign in the sum + shift)
        b = [rnd.randint(INT_MIN, INT_MAX) for _ in range(4)]
        w = [round(rnd.uniform(0.0, 1.0), 6) for _ in range(4)]
        emit(b, w)

    print("\n".join(out))


if __name__ == "__main__":
    main()
