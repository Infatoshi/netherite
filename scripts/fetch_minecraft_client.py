#!/usr/bin/env python3
"""Fetch the official Minecraft 1.11.2 client jar for local asset generation.

The jar is Mojang content.  It is written only to the ignored ForgeGradle cache,
verified against the SHA-1 and size in Mojang's version manifest, and must never
be committed or redistributed.  The caller is responsible for owning Minecraft
and accepting Mojang's terms, exactly as with the official launcher.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import tempfile
import urllib.request


VERSION = "1.11.2"
MANIFEST_URL = "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json"
USER_AGENT = "netherite-local-asset-bootstrap/1"


def fetch_json(url: str) -> dict:
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=60) as response:
        return json.load(response)


def sha1_file(path: Path) -> tuple[str, int]:
    digest = hashlib.sha1()
    size = 0
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
            size += len(chunk)
    return digest.hexdigest(), size


def client_descriptor() -> dict:
    manifest = fetch_json(MANIFEST_URL)
    version = next((entry for entry in manifest["versions"]
                    if entry["id"] == VERSION), None)
    if version is None:
        raise RuntimeError(f"Mojang manifest has no version {VERSION}")
    details = fetch_json(version["url"])
    try:
        return details["downloads"]["client"]
    except KeyError as exc:
        raise RuntimeError("Mojang version metadata has no client download") from exc


def download_client(destination: Path, force: bool) -> None:
    descriptor = client_descriptor()
    expected_sha1 = descriptor["sha1"].lower()
    expected_size = int(descriptor["size"])

    if destination.is_file() and not force:
        actual_sha1, actual_size = sha1_file(destination)
        if actual_sha1 == expected_sha1 and actual_size == expected_size:
            print(f"Minecraft {VERSION} client already verified: {destination}")
            return
        raise RuntimeError(
            f"existing jar failed verification: {destination}\n"
            f"expected sha1={expected_sha1} size={expected_size}, "
            f"got sha1={actual_sha1} size={actual_size}; rerun with --force")

    destination.parent.mkdir(parents=True, exist_ok=True)
    request = urllib.request.Request(descriptor["url"],
                                     headers={"User-Agent": USER_AGENT})
    digest = hashlib.sha1()
    size = 0
    temporary: Path | None = None
    try:
        with urllib.request.urlopen(request, timeout=120) as response:
            with tempfile.NamedTemporaryFile(
                    prefix=f"minecraft-{VERSION}-", suffix=".jar",
                    dir=destination.parent, delete=False) as output:
                temporary = Path(output.name)
                while True:
                    chunk = response.read(1024 * 1024)
                    if not chunk:
                        break
                    output.write(chunk)
                    digest.update(chunk)
                    size += len(chunk)
        actual_sha1 = digest.hexdigest()
        if actual_sha1 != expected_sha1 or size != expected_size:
            raise RuntimeError(
                "downloaded client jar failed Mojang verification: "
                f"expected sha1={expected_sha1} size={expected_size}, "
                f"got sha1={actual_sha1} size={size}")
        os.chmod(temporary, 0o644)
        os.replace(temporary, destination)
        temporary = None
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)

    print(f"Minecraft {VERSION} client verified: {destination}")
    print("Local asset input only; do not commit or redistribute this jar.")


def main() -> int:
    repo = Path(__file__).resolve().parent.parent
    default = (repo / "java" / "Minecraft" / "run" / "gradle" / "caches" /
               "minecraft" / "net" / "minecraft" / "minecraft" / VERSION /
               f"minecraft-{VERSION}.jar")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=default)
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()
    download_client(args.output.expanduser().resolve(), args.force)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
