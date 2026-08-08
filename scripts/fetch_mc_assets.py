"""Pre-seed the ForgeGradle 2 asset cache over https.

ForgeGradle FG_2.2 has http://resources.download.minecraft.net baked in;
Mojang's CDN now answers plain-HTTP asset requests with 400, which kills
setupDecompWorkspace on any fresh clone (github issue #2). FG's
DownloadAssetsTask skips every object whose file exists with a matching
SHA1, so downloading the index and objects here first makes the baked-in
http path dead code.

Usage: uv run --no-project python scripts/fetch_mc_assets.py <assets-dir>
where <assets-dir> is <gradle-home>/caches/minecraft/assets (created if
missing). Stdlib only; safe to rerun (validates hashes, downloads only
what is missing or corrupt).
"""

import concurrent.futures
import hashlib
import json
import sys
import urllib.request
from pathlib import Path

MANIFEST = "https://launchermeta.mojang.com/mc/game/version_manifest.json"
RESOURCES = "https://resources.download.minecraft.net"
MC_VERSION = "1.11.2"


def fetch(url):
    with urllib.request.urlopen(url, timeout=60) as r:
        return r.read()


def sha1(path):
    h = hashlib.sha1()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def get_object(objects_dir, hash_):
    dst = objects_dir / hash_[:2] / hash_
    if dst.exists() and sha1(dst) == hash_:
        return False
    dst.parent.mkdir(parents=True, exist_ok=True)
    data = fetch(f"{RESOURCES}/{hash_[:2]}/{hash_}")
    got = hashlib.sha1(data).hexdigest()
    if got != hash_:
        raise RuntimeError(f"hash mismatch for {hash_}: got {got}")
    dst.write_bytes(data)
    return True


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__.strip())
    assets = Path(sys.argv[1])
    manifest = json.loads(fetch(MANIFEST))
    version = next(v for v in manifest["versions"] if v["id"] == MC_VERSION)
    vjson = json.loads(fetch(version["url"]))
    aidx = vjson["assetIndex"]

    idx_path = assets / "indexes" / f"{aidx['id']}.json"
    if idx_path.exists() and sha1(idx_path) == aidx["sha1"]:
        idx_data = idx_path.read_bytes()
    else:
        idx_data = fetch(aidx["url"])
        got = hashlib.sha1(idx_data).hexdigest()
        if got != aidx["sha1"]:
            raise RuntimeError(f"asset index hash mismatch: got {got}")
        idx_path.parent.mkdir(parents=True, exist_ok=True)
        idx_path.write_bytes(idx_data)

    hashes = sorted({o["hash"] for o in json.loads(idx_data)["objects"].values()})
    objects = assets / "objects"
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as ex:
        fetched = sum(ex.map(lambda h: get_object(objects, h), hashes))
    print(f"assets ready: {len(hashes)} objects ({fetched} downloaded, "
          f"{len(hashes) - fetched} already valid) in {assets}")


if __name__ == "__main__":
    main()
