#!/usr/bin/env python3
"""Deterministic seeded inputs for the frustum-plane-extract kernel.

Each record = 32 raw-float-bits hex ints (8 hex digits): 16 projection-matrix floats then
16 modelview-matrix floats, both column-major (as OpenGL glGetFloat GL_PROJECTION_MATRIX /
GL_MODELVIEW_MATRIX would return them). Matrices are realistic: a perspective projection
times a random camera view (yaw/pitch rotation + eye translation), so the extracted planes
have non-degenerate (non-zero) normals. Floats are emitted as raw bits so the golden (Java)
and candidate (C) parse byte-identical inputs (no decimal-parse ambiguity).
"""
import math
import random
import struct
import sys

N = 30000


def fbits(x):
    return "%08x" % struct.unpack("<I", struct.pack("<f", x))[0]


def perspective(fovy_deg, aspect, near, far):
    # column-major (OpenGL), index = col*4 + row
    f = 1.0 / math.tan(math.radians(fovy_deg) * 0.5)
    m = [0.0] * 16
    m[0] = f / aspect          # col0 row0
    m[5] = f                   # col1 row1
    m[10] = (far + near) / (near - far)   # col2 row2
    m[11] = -1.0               # col2 row3
    m[14] = (2.0 * far * near) / (near - far)  # col3 row2
    return m


def matmul_rm(a, b):
    # 4x4 row-major multiply
    out = [0.0] * 16
    for r in range(4):
        for c in range(4):
            s = 0.0
            for k in range(4):
                s += a[r * 4 + k] * b[k * 4 + c]
            out[r * 4 + c] = s
    return out


def rm_to_cm(m):
    # row-major -> column-major flatten: cm[col*4+row] = m[row*4+col]
    return [m[r * 4 + c] for c in range(4) for r in range(4)]


def view_matrix(yaw, pitch, eye):
    cy, sy = math.cos(yaw), math.sin(yaw)
    cp, sp = math.cos(pitch), math.sin(pitch)
    # Ry(yaw) row-major
    Ry = [cy, 0, sy, 0,
          0, 1, 0, 0,
          -sy, 0, cy, 0,
          0, 0, 0, 1]
    # Rx(pitch) row-major
    Rx = [1, 0, 0, 0,
          0, cp, -sp, 0,
          0, sp, cp, 0,
          0, 0, 0, 1]
    T = [1, 0, 0, -eye[0],
         0, 1, 0, -eye[1],
         0, 0, 1, -eye[2],
         0, 0, 0, 1]
    return matmul_rm(matmul_rm(Rx, Ry), T)  # row-major


def main():
    rnd = random.Random(20251)
    out = []
    for _ in range(N):
        fov = rnd.uniform(50.0, 100.0)
        aspect = rnd.choice([16.0 / 9.0, 4.0 / 3.0, 1.0, 21.0 / 9.0]) * rnd.uniform(0.9, 1.1)
        near = rnd.uniform(0.02, 0.1)
        far = rnd.uniform(256.0, 4096.0)
        proj_cm = perspective(fov, aspect, near, far)

        yaw = rnd.uniform(-math.pi, math.pi)
        pitch = rnd.uniform(-math.pi / 2.0, math.pi / 2.0)
        eye = [rnd.uniform(-2000.0, 2000.0), rnd.uniform(0.0, 256.0), rnd.uniform(-2000.0, 2000.0)]
        view_rm = view_matrix(yaw, pitch, eye)
        view_cm = rm_to_cm(view_rm)

        vals = proj_cm + view_cm
        out.append(" ".join(fbits(v) for v in vals))
    sys.stdout.write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
