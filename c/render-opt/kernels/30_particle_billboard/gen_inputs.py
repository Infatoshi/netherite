#!/usr/bin/env python3
"""Deterministic input stream for 30_particle_billboard.
One particle per line, fields in this exact order:
  texIdxX texIdxY scale prevX prevY prevZ posX posY posZ partialTicks interpX interpY interpZ
  light angle prevAngle camDirX camDirY camDirZ rotX rotZ rotYZ rotXY rotXZ red green blue alpha
Encodings: texIdx* and light are decimal int; scale/partialTicks/angle/prevAngle/rot*/r/g/b/a are
hex IEEE-754 float-bits; prev*/pos*/interp*/camDir* are hex IEEE-754 double long-bits. (Raw bits so
golden and candidate read identical values.)

Coverage: angle==0 (fast path) and angle!=0 (sin/cos quaternion rotate); zero rotation components
and zero camDir components to drive the Vec3d -0.0 -> +0.0 normalization; typical billboard params
(rotationX/Z/YZ are the camera basis, ~unit; XY/XZ usually 0)."""
import random
import struct


def fb(f):
    return format(struct.unpack("<I", struct.pack("<f", f))[0], "x")


def db(d):
    return format(struct.unpack("<Q", struct.pack("<d", d))[0], "x")


def main():
    rnd = random.Random(3030)
    out = []

    def emit(texx, texy, scale, prev, pos, pt, interp, light, angle, pangle,
             camdir, rot, rgba):
        row = [str(texx), str(texy), fb(scale)]
        row += [db(v) for v in prev]
        row += [db(v) for v in pos]
        row += [fb(pt)]
        row += [db(v) for v in interp]
        row += [str(light), fb(angle), fb(pangle)]
        row += [db(v) for v in camdir]
        row += [fb(v) for v in rot]
        row += [fb(v) for v in rgba]
        out.append(" ".join(row))

    def rnd_particle(angle_zero, force_zeros=False):
        texx = rnd.randint(0, 15)
        texy = rnd.randint(0, 15)
        scale = rnd.uniform(0.05, 4.0)
        base = [rnd.uniform(-1000, 1000) for _ in range(3)]
        prev = base
        pos = [base[a] + rnd.uniform(-0.5, 0.5) for a in range(3)]
        pt = rnd.uniform(0.0, 1.0)
        interp = [rnd.uniform(-1000, 1000) for _ in range(3)]
        light = rnd.randint(0, 0xF000F0)
        angle = 0.0 if angle_zero else rnd.uniform(-6.3, 6.3)
        pangle = 0.0 if angle_zero else rnd.uniform(-6.3, 6.3)
        # camera view dir ~ unit vector
        cd = [rnd.uniform(-1, 1) for _ in range(3)]
        n = (cd[0] ** 2 + cd[1] ** 2 + cd[2] ** 2) ** 0.5 or 1.0
        camdir = [c / n for c in cd]
        # rotation basis: X/Z/YZ ~ unit-ish, XY/XZ often 0
        rot = [rnd.uniform(-1, 1), rnd.uniform(-1, 1), rnd.uniform(-1, 1), 0.0, 0.0]
        if force_zeros:
            # drive -0.0 normalization: zero some rot comps and camdir comps
            rot = [0.0, rnd.uniform(-1, 1), 0.0, 0.0, 0.0]
            camdir = [0.0, 1.0, 0.0]
        else:
            if rnd.random() < 0.3:
                rot[3] = rnd.uniform(-0.5, 0.5)
                rot[4] = rnd.uniform(-0.5, 0.5)
        rgba = [rnd.uniform(0.0, 1.0) for _ in range(4)]
        emit(texx, texy, scale, prev, pos, pt, interp, light, angle, pangle, camdir, rot, rgba)

    # explicit edge cases
    rnd_particle(angle_zero=True, force_zeros=True)
    rnd_particle(angle_zero=False, force_zeros=True)
    for _ in range(20000):
        rnd_particle(angle_zero=True)
    for _ in range(20000):
        rnd_particle(angle_zero=False)
    for _ in range(2000):
        rnd_particle(angle_zero=False, force_zeros=True)

    print("\n".join(out))


if __name__ == "__main__":
    main()
