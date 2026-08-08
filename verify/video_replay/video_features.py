#!/usr/bin/env python3
"""Extract renderer-agnostic observations and motion proposals from a video.

The search does not compare compressed RGB directly with magma's renderer.
It uses coarse, texture-suppressed edges for endpoint scoring and phase
correlation for a per-frame camera-motion proposal.  HUD, subtitles, debug
text, and timer overlays are masked without relying on their exact contents.
"""
import argparse
import json
import pathlib
import subprocess

import numpy as np


WIDTH = 64
HEIGHT = 36
SCHEMA = 3


def _box_blur(gray):
    """A dependency-free 5x5 box blur over an N,H,W uint8 array."""
    value = gray.astype(np.uint32)
    padded = np.pad(value, ((0, 0), (2, 2), (2, 2)), mode="edge")
    integral = np.pad(padded, ((0, 0), (1, 0), (1, 0))).cumsum(1).cumsum(2)
    total = (integral[:, 5:, 5:] - integral[:, :-5, 5:] -
             integral[:, 5:, :-5] + integral[:, :-5, :-5])
    return (total // 25).astype(np.uint8)


def structural_edges(gray):
    smooth = _box_blur(gray)
    gx = np.zeros_like(smooth, dtype=np.int16)
    gy = np.zeros_like(smooth, dtype=np.int16)
    gx[:, :, 1:-1] = (smooth[:, :, 2:].astype(np.int16) -
                      smooth[:, :, :-2].astype(np.int16))
    gy[:, 1:-1, :] = (smooth[:, 2:, :].astype(np.int16) -
                      smooth[:, :-2, :].astype(np.int16))
    magnitude = np.abs(gx) + np.abs(gy)
    edge = magnitude >= 22

    # The hotbar/hand/subtitle region is not part of the world camera.  Timer
    # and debug overlays are identified as unusually dense 4x4 edge tiles.
    mask = np.ones_like(edge, dtype=bool)
    mask[:, HEIGHT - 7:, :] = False
    for y in range(0, HEIGHT - 7, 4):
        for x in range(0, WIDTH, 4):
            tile = edge[:, y:y + 4, x:x + 4]
            dense = tile.mean(axis=(1, 2)) > 0.58
            mask[dense, y:y + 4, x:x + 4] = False
    return edge.astype(np.uint8), mask.astype(np.uint8)


def phase_shifts(gray, mask):
    """Return integer image translations aligning frame i with frame i-1."""
    smooth = _box_blur(gray).astype(np.float32)
    shifts = np.zeros((len(gray), 2), dtype=np.int8)
    window_y = np.hanning(HEIGHT).astype(np.float32)
    window_x = np.hanning(WIDTH).astype(np.float32)
    window = window_y[:, None] * window_x[None, :]
    for index in range(1, len(gray)):
        # Dense world texture is useful to phase correlation even when the
        # endpoint scorer masks it as likely text.  Only exclude the fixed HUD
        # band here; applying the adaptive text mask can erase a small scene.
        valid = np.ones((HEIGHT, WIDTH), dtype=np.float32)
        valid[HEIGHT - 7:] = 0.0
        a = (smooth[index - 1] - smooth[index - 1].mean()) * window * valid
        b = (smooth[index] - smooth[index].mean()) * window * valid
        fa = np.fft.rfft2(a)
        fb = np.fft.rfft2(b)
        cross = fa * np.conj(fb)
        cross /= np.maximum(np.abs(cross), 1e-7)
        corr = np.fft.irfft2(cross, s=a.shape)
        y, x = np.unravel_index(np.argmax(corr), corr.shape)
        if y > HEIGHT // 2:
            y -= HEIGHT
        if x > WIDTH // 2:
            x -= WIDTH
        shifts[index] = np.clip((y, x), -12, 12)
    return shifts


def classify_frames(gray, edges, masks):
    texture = (edges & masks).sum(axis=(1, 2)) / np.maximum(
        masks.sum(axis=(1, 2)), 1)
    mean = gray.mean(axis=(1, 2))
    spread = gray.std(axis=(1, 2))
    loading = ((mean < 45) & (spread < 35)) | (texture < 0.012)
    usable = (~loading) & (texture > 0.025)
    return loading.astype(np.uint8), usable.astype(np.uint8), texture


def decode_video(path, fps):
    command = [
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-i", str(path),
        "-vf", f"fps={fps},scale={WIDTH}:{HEIGHT}:flags=area",
        "-f", "rawvideo", "-pix_fmt", "rgb24", "-",
    ]
    proc = subprocess.Popen(command, stdout=subprocess.PIPE)
    raw = proc.stdout.read()
    rc = proc.wait()
    if rc:
        raise RuntimeError(f"ffmpeg feature decode failed with rc={rc}")
    frame_bytes = WIDTH * HEIGHT * 3
    if not raw or len(raw) % frame_bytes:
        raise RuntimeError(f"short or misaligned ffmpeg output: {len(raw)} bytes")
    return np.frombuffer(raw, dtype=np.uint8).reshape(-1, HEIGHT, WIDTH, 3)


def detect_third_person(path, fps):
    """Detect the centered player silhouette without a skin-specific model."""
    width, height = 160, 90
    command = [
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-i", str(path),
        "-vf", f"fps={fps},scale={width}:{height}:flags=area",
        "-f", "rawvideo", "-pix_fmt", "rgb24", "-",
    ]
    proc = subprocess.Popen(command, stdout=subprocess.PIPE)
    frame_bytes = width * height * 3
    result = []
    while True:
        raw = proc.stdout.read(frame_bytes)
        if not raw:
            break
        if len(raw) != frame_bytes:
            proc.kill()
            raise RuntimeError("short high-resolution classifier frame")
        rgb = np.frombuffer(raw, dtype=np.uint8).reshape(height, width, 3)
        crop = rgb[28:72, 55:105].astype(np.int16)
        red = ((crop[..., 0] > 50) &
               (crop[..., 0] > crop[..., 1] * 1.5) &
               (crop[..., 0] > crop[..., 2] * 1.5))
        visited = np.zeros(red.shape, dtype=bool)
        found = False
        for sy, sx in np.argwhere(red):
            if visited[sy, sx]:
                continue
            stack = [(int(sy), int(sx))]
            visited[sy, sx] = True
            count = 0
            min_x = max_x = int(sx)
            min_y = max_y = int(sy)
            while stack:
                y, x = stack.pop()
                count += 1
                min_x = min(min_x, x); max_x = max(max_x, x)
                min_y = min(min_y, y); max_y = max(max_y, y)
                for ny, nx in ((y - 1, x), (y + 1, x),
                               (y, x - 1), (y, x + 1)):
                    if (0 <= ny < red.shape[0] and 0 <= nx < red.shape[1]
                            and red[ny, nx] and not visited[ny, nx]):
                        visited[ny, nx] = True
                        stack.append((ny, nx))
            box_w = max_x - min_x + 1
            box_h = max_y - min_y + 1
            center_x = (min_x + max_x) * 0.5 + 55
            center_y = (min_y + max_y) * 0.5 + 28
            # A first-person arm can swing through the bottom of this crop
            # while mining.  The third-person torso/head stays around the
            # crosshair; requiring its red component above y=60 separates the
            # two without depending on the exact skin colors elsewhere.
            if (8 <= count <= 80 and box_w <= 14 and box_h <= 16 and
                    65 <= center_x <= 95 and 35 <= center_y <= 60):
                found = True
                break
        result.append(found)
    if proc.wait():
        raise RuntimeError("ffmpeg third-person classifier failed")
    detected = np.asarray(result, dtype=np.uint8)
    # A mining arm or one red world texel can satisfy the spatial test for a
    # single decoded frame.  F5 is a camera mode, so require 0.3 seconds of
    # persistence while retaining every frame of a confirmed interval.
    minimum = max(3, int(round(fps * 0.5)))
    filtered = np.zeros_like(detected)
    start = None
    for index, value in enumerate(np.r_[detected, 0]):
        if value and start is None:
            start = index
        elif not value and start is not None:
            if index - start >= minimum:
                filtered[start:index] = 1
            start = None
    return filtered


def interaction_hints(gray, third_person):
    """Detect likely repeated hand actions from video motion alone.

    Mining/attacking moves the lower-right hand strongly while a stationary
    world remains stable.  A centered temporal window makes the hint robust
    to the two endpoints of the swing.  It is deliberately only a proposal:
    engine ray/dig state must still validate candidates.
    """
    value = gray.astype(np.int16)
    delta = np.zeros_like(value, dtype=np.float32)
    delta[1:] = np.abs(value[1:] - value[:-1])
    hand = delta[:, 18:, 44:].mean(axis=(1, 2))
    world = delta[:, :18, :44].mean(axis=(1, 2))
    window = 5
    kernel = np.ones(window, dtype=np.float32) / window
    hand_smooth = np.convolve(hand, kernel, mode="same")
    world_smooth = np.convolve(world, kernel, mode="same")
    hint = ((hand_smooth > 4.0) &
            (hand_smooth > world_smooth * 0.72 + 1.5) &
            ~third_person.astype(bool))
    return hint.astype(np.uint8)


def gui_hints(rgb, gray):
    """Detect the fixed gray GuiContainer panel after coarse video scaling."""
    value = rgb.astype(np.int16)
    high = value.max(axis=3)
    low = value.min(axis=3)
    neutral = ((high - low < 20) & (high > 70) & (high < 235))
    panel = neutral[:, 4:31, 30:63]
    count = panel.sum(axis=(1, 2))
    row = panel.sum(axis=2).max(axis=1)
    column = panel.sum(axis=1).max(axis=1)
    mean = gray.mean(axis=(1, 2))
    raw = ((mean < 58) & (count >= 65) & (count <= 150) &
           (row >= 6) & (row <= 12) & (column >= 8) & (column <= 17))
    # GUI screens persist across multiple decoded frames. Remove one-frame
    # stone/iron alignments which happen to imitate the low-resolution panel.
    minimum = 3
    result = np.zeros_like(raw, dtype=np.uint8)
    start = None
    for index, present in enumerate(np.r_[raw, False]):
        if present and start is None:
            start = index
        elif not present and start is not None:
            if index - start >= minimum:
                result[start:index] = 1
            start = None
    return result


def extract(video, output, fps):
    rgb = decode_video(video, fps)
    gray = np.rint(rgb[..., 0].astype(np.float32) * 0.299 +
                   rgb[..., 1].astype(np.float32) * 0.587 +
                   rgb[..., 2].astype(np.float32) * 0.114).astype(np.uint8)
    edges, masks = structural_edges(gray)
    shifts = phase_shifts(gray, masks)
    loading, usable, texture = classify_frames(gray, edges, masks)
    third_person = detect_third_person(video, fps)
    if len(third_person) != len(gray):
        raise RuntimeError("third-person classifier frame count mismatch")
    output.parent.mkdir(parents=True, exist_ok=True)
    gui = gui_hints(rgb, gray)
    third_person &= ~gui
    attack_hint = interaction_hints(gray, third_person)
    np.savez_compressed(
        output, schema=np.array([SCHEMA], dtype=np.int16),
        fps=np.array([fps], dtype=np.float32), rgb=rgb, gray=gray, edges=edges,
        masks=masks, shifts=shifts, loading=loading, usable=usable,
        texture=texture.astype(np.float32), third_person=third_person,
        attack_hint=attack_hint, gui=gui)
    return {
        "frames": len(gray), "fps": fps,
        "duration_s": len(gray) / fps,
        "usable_frames": int(usable.sum()),
        "loading_frames": int(loading.sum()),
        "output": str(output),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("video", type=pathlib.Path)
    parser.add_argument("--out", type=pathlib.Path, required=True)
    parser.add_argument("--fps", type=float, default=10.0)
    args = parser.parse_args()
    if args.fps <= 0 or args.fps > 20:
        parser.error("--fps must be in (0,20]")
    print(json.dumps(extract(args.video.resolve(), args.out.resolve(),
                             args.fps), sort_keys=True))


if __name__ == "__main__":
    main()
