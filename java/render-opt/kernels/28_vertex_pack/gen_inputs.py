#!/usr/bin/env python3
"""Deterministic input stream for 28_vertex_pack.
Each line: 28 decimal int32 (quad vertex data, BLOCK format 7 ints/vertex)
  | 4 decimal int32 (brightness / lightmap UV ints) | 4 hex float-bits (color multipliers).

Within the 28 vertex ints, the meaningful overwritten lanes are:
  - color ints at indices 3,10,17,24 (full RGBA bytes randomized so every channel of the
    putColorMultiplier (int)(byte*mult) truncation is exercised),
  - lightmap ints at 6,13,20,27 (overwritten wholesale by putBrightness4).
Positions/uv lanes (the rest) are random ints; they pass through unchanged.
Color multipliers in [0, 1.2] (occasionally > 1 to stress the >255 clamp-free wrap)."""
import random
import struct


def fbits(f):
    return format(struct.unpack("<I", struct.pack("<f", f))[0], "x")


def i32(u):
    """wrap an unsigned 32-bit pattern to signed int32 (Java parseInt range)."""
    u &= 0xFFFFFFFF
    return u - 0x100000000 if u >= 0x80000000 else u


def main():
    rnd = random.Random(2828)
    out = []

    def emit(data, bright, cmul):
        out.append(" ".join(str(x) for x in data)
                   + " " + " ".join(str(x) for x in bright)
                   + " " + " ".join(fbits(x) for x in cmul))

    # edge cases
    emit([0] * 28, [0, 0, 0, 0], [0.0, 0.0, 0.0, 0.0])
    emit([0] * 28, [0xF000F0, 0xF000F0, 0xF000F0, 0xF000F0], [1.0, 1.0, 1.0, 1.0])
    # color lanes fully white, multipliers various
    d = [0] * 28
    for ci in (3, 10, 17, 24):
        d[ci] = i32((255 << 24) | (255 << 16) | (255 << 8) | 255)
    emit(d, [0xF000F0] * 4, [0.5, 0.25, 1.0, 0.0])

    for _ in range(50000):
        data = [rnd.randint(-(2**31), 2**31 - 1) for _ in range(28)]
        # give color lanes clean 0..255 bytes so each channel is meaningful
        for ci in (3, 10, 17, 24):
            a = rnd.randint(0, 255)
            r = rnd.randint(0, 255)
            g = rnd.randint(0, 255)
            b = rnd.randint(0, 255)
            data[ci] = i32((a << 24) | (b << 16) | (g << 8) | r)
        bright = [rnd.randint(0, 0xF000F0) for _ in range(4)]
        cmul = [round(rnd.uniform(0.0, 1.2), 6) for _ in range(4)]
        emit(data, bright, cmul)

    print("\n".join(out))


if __name__ == "__main__":
    main()
