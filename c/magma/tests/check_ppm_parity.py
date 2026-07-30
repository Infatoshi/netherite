#!/usr/bin/env python3
import argparse
from pathlib import Path


def read_ppm(path: Path):
    data = path.read_bytes()
    end = 0
    for _ in range(3):
        end = data.index(b"\n", end) + 1
    magic, dimensions, maximum = data[:end].splitlines()
    width, height = (int(value) for value in dimensions.split())
    pixels = data[end:]
    if magic != b"P6" or maximum != b"255" or len(pixels) != width * height * 3:
        raise ValueError(f"invalid P6 PPM: {path}")
    return width, height, pixels


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("reference", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--max-changed-pixels", type=int, required=True)
    parser.add_argument("--max-channel-diff", type=int, required=True)
    args = parser.parse_args()

    width, height, reference = read_ppm(args.reference)
    candidate_width, candidate_height, candidate = read_ppm(args.candidate)
    if (candidate_width, candidate_height) != (width, height):
        raise SystemExit(
            f"PPM dimensions differ: reference={width}x{height} "
            f"candidate={candidate_width}x{candidate_height}"
        )

    differences = [
        index for index, (left, right) in enumerate(zip(reference, candidate))
        if left != right
    ]
    changed_pixels = len({index // 3 for index in differences})
    max_diff = max(
        (abs(reference[index] - candidate[index]) for index in differences),
        default=0,
    )
    message = (
        f"PPM parity changed_px={changed_pixels}/{width * height} "
        f"maxdiff={max_diff}"
    )
    if differences:
        index = differences[0]
        pixel, channel = divmod(index, 3)
        y, x = divmod(pixel, width)
        message += (
            f" first=({x},{y},c{channel}) "
            f"reference={reference[index]} candidate={candidate[index]}"
        )
    print(message)
    if changed_pixels > args.max_changed_pixels or max_diff > args.max_channel_diff:
        raise SystemExit(
            f"PPM parity exceeds changed_px<={args.max_changed_pixels} "
            f"maxdiff<={args.max_channel_diff}"
        )


if __name__ == "__main__":
    main()
