#!/usr/bin/env python3
"""Strict owned-pixel gate for the 1.11.2 Structure Block editor.

The screen dims a live 3D world, so the comparison owns only pixels whose
result is independent of that underlay: complete opaque text boxes and fully
opaque widgets.png button texels. Oracle A/B-unstable pixels are excluded and
reported rather than charged to the native renderer.
"""

from __future__ import print_function

import argparse
import io
import os
import zipfile

import numpy as np
from PIL import Image

WIDTH, HEIGHT, SCALE = 854, 480, 2
GUI_WIDTH = (WIDTH + SCALE - 1) // SCALE
CX = GUI_WIDTH // 2


def rect(mask, x, y, width, height):
    mask[y * SCALE:(y + height) * SCALE,
         x * SCALE:(x + width) * SCALE] = True


def field_rects(mode):
    fields = []
    if mode != "data":
        fields.append((CX - 153, 39, 302, 22))
    if mode in ("save", "load"):
        for x in (CX - 153, CX - 73, CX + 7):
            fields.append((x, 79, 82, 22))
    if mode == "save":
        for x in (CX - 153, CX - 73, CX + 7):
            fields.append((x, 119, 82, 22))
    elif mode == "load":
        fields.extend(((CX - 153, 119, 82, 22),
                       (CX - 73, 119, 82, 22)))
    elif mode == "data":
        fields.append((CX - 153, 119, 242, 22))
    return fields


def buttons(mode):
    result = [
        (CX - 154, 210, 150, 20, True),
        (CX + 4, 210, 150, 20, True),
        (CX - 154, 185, 50, 20, True),
    ]
    if mode == "save":
        result.extend(((CX + 104, 185, 50, 20, True),
                       (CX + 104, 120, 50, 20, True),
                       (CX + 104, 160, 50, 20, True),
                       (CX + 104, 80, 50, 20, True)))
    elif mode == "load":
        result.extend(((CX + 104, 185, 50, 20, True),
                       (CX - 102, 185, 40, 20, True),
                       (CX - 61, 185, 40, 20, False),
                       (CX + 21, 185, 40, 20, True),
                       (CX + 62, 185, 40, 20, True),
                       (CX - 20, 185, 40, 20, True),
                       (CX + 104, 160, 50, 20, True),
                       (CX + 104, 80, 50, 20, True)))
    return result


def find_client_jar(root):
    path = os.path.join(
        root, "java", "Minecraft", "run", "gradle", "caches", "minecraft",
        "net", "minecraft", "minecraft", "1.11.2", "minecraft-1.11.2.jar")
    if not os.path.isfile(path):
        raise RuntimeError("Minecraft 1.11.2 client jar missing: " + path)
    return path


def button_alpha(root, width, enabled):
    with zipfile.ZipFile(find_client_jar(root)) as archive:
        raw = archive.read("assets/minecraft/textures/gui/widgets.png")
    image = Image.open(io.BytesIO(raw)).convert("RGBA")
    state = 1 if enabled else 0
    strip = np.asarray(image)[46 + state * 20:66 + state * 20, :, 3]
    half = width // 2
    pixels = np.concatenate(
        (strip[:, :half], strip[:, 200 - half:200]), axis=1)
    return np.repeat(np.repeat(pixels == 255, SCALE, axis=0), SCALE, axis=1)


def owned_mask(root, mode):
    mask = np.zeros((HEIGHT, WIDTH), dtype=bool)
    for item in field_rects(mode):
        rect(mask, *item)
    for x, y, width, height, enabled in buttons(mode):
        alpha = button_alpha(root, width, enabled)
        ys, xs = y * SCALE, x * SCALE
        mask[ys:ys + height * SCALE, xs:xs + width * SCALE] |= alpha
    return mask


def load_rgb(path):
    value = np.asarray(Image.open(path).convert("RGB"))
    if value.shape != (HEIGHT, WIDTH, 3):
        raise RuntimeError("wrong frame shape %r: %s" % (value.shape, path))
    return value.astype(np.int16)


def compare_arrays(root, mode, java_a, java_b, native):
    owned = owned_mask(root, mode)
    stable = owned & np.all(java_a == java_b, axis=2)
    unstable = int(np.count_nonzero(owned & ~stable))
    delta = np.max(np.abs(java_a - native), axis=2)
    differing = int(np.count_nonzero(stable & (delta != 0)))
    maximum = int(delta[stable].max()) if np.any(stable) else 255
    return owned, stable, unstable, differing, maximum


def mutation_selftest(root, goldens, candidate):
    errors = 0
    for mode in ("save", "load", "corner", "data"):
        java_a = load_rgb(os.path.join(
            goldens, "gui_structure_%s_a.png" % mode))
        java_b = load_rgb(os.path.join(
            goldens, "gui_structure_%s_b.png" % mode))
        native = load_rgb(os.path.join(
            candidate, "gui_structure_%s.ppm" % mode))
        _, stable, _, differing, _ = compare_arrays(
            root, mode, java_a, java_b, native)
        if differing:
            print("%-6s mutation self-test: baseline is not exact" % mode)
            errors += 1
            continue
        points = np.argwhere(stable)
        if points.size == 0:
            print("%-6s mutation self-test: no stable owned pixel" % mode)
            errors += 1
            continue
        y, x = points[len(points) // 2]
        changed = native.copy()
        changed[y, x, 0] += 1 if changed[y, x, 0] < 255 else -1
        _, _, _, mutated_diff, mutated_max = compare_arrays(
            root, mode, java_a, java_b, changed)
        caught = mutated_diff == 1 and mutated_max == 1
        print("%-6s mutation self-test: %s" % (
            mode, "caught" if caught else "MISSED"))
        if not caught:
            errors += 1
    if errors:
        print("structure GUI mutation self-test: FAIL (%d modes)" % errors)
        return 1
    print("structure GUI mutation self-test: PASS")
    return 0


def main():
    parser = argparse.ArgumentParser()
    root_default = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
    parser.add_argument("--root", default=root_default)
    parser.add_argument("--goldens", default=None)
    parser.add_argument("--candidate", default=None)
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    goldens = args.goldens or os.path.join(args.root, "verify", "ui_hud", "goldens")
    candidate = args.candidate or os.path.join(args.root, "verify", "ui_hud", "c_frames")
    if args.selftest:
        return mutation_selftest(args.root, goldens, candidate)
    failed = 0
    for mode in ("save", "load", "corner", "data"):
        java_a = load_rgb(os.path.join(goldens, "gui_structure_%s_a.png" % mode))
        java_b = load_rgb(os.path.join(goldens, "gui_structure_%s_b.png" % mode))
        native = load_rgb(os.path.join(candidate, "gui_structure_%s.ppm" % mode))
        owned, stable, unstable, differing, maximum = compare_arrays(
            args.root, mode, java_a, java_b, native)
        print("%-6s owned=%d stable=%d oracle_unstable=%d diff=%d max=%d" % (
            mode, int(owned.sum()), int(stable.sum()), unstable,
            differing, maximum))
        if stable.sum() < 20000 or differing != 0:
            failed += 1
    if failed:
        print("structure GUI owned pixels: FAIL (%d modes)" % failed)
        return 1
    print("structure GUI owned pixels: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
