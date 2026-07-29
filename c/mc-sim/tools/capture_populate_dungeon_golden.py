#!/usr/bin/env python3
"""Capture populate_dungeon_golden oracle output and write Golden.java.

Seeds:
  12345, 0, 7 - empty control (no dungeon placement; full 262144 %04x dump)
  88, 325, 576 - dungeon room block placement (delta %06x%04x lines)

Pipeline: core/populate_dungeon_golden.h = overworld terrain (st_run x4) +
populate lake+dungeon RNG (READ-ONLY populate.h wg_dungeons/wg_lakes).
"""
import base64
import gzip
import io
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SEEDS = [12345, 0, 7, 88, 325, 576]


def build_cpu(tmp):
    out = os.path.join(tmp, "populate_dungeon_golden_cpu")
    subprocess.run(
        ["cc", "-O2", "-ffp-contract=off", "-Icore", "-o", out,
         os.path.join(ROOT, "cpu", "populate_dungeon_golden.c"), "-lm"],
        cwd=ROOT, check=True,
    )
    return out


def capture(cpu, seed):
    p = subprocess.run([cpu, str(seed)], capture_output=True, text=True, check=True)
    raw = p.stdout.encode()
    buf = io.BytesIO()
    with gzip.GzipFile(fileobj=buf, mode="wb", mtime=0) as gz:
        gz.write(raw)
    return base64.b64encode(buf.getvalue()).decode(), len(p.stdout.splitlines())


def main():
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        cpu = build_cpu(tmp)
        data = {s: capture(cpu, s) for s in SEEDS}

    lines = [
        "// Golden for populate_dungeon_golden: WorldGenDungeons on overworld terrain.",
        "// Seeds 12345/0/7 = no dungeon placement (full 262144 %04x).",
        "// Seeds 88/325/576 = dungeon room delta (%06x%04x: index + PB_* block).",
        "// Terrain = overworld_full st_run x4; dungeons = populate lake+dungeon RNG.",
        "import java.io.*; import java.util.zip.*;",
        "public class Golden {",
    ]
    for s in SEEDS:
        b64, nlines = data[s]
        lines.append(f'    static final String B64_{s} = "{b64}";  // {nlines} lines')
    lines += [
        "    static void dumpGunzip(String b64) throws Exception {",
        "        byte[] raw = java.util.Base64.getDecoder().decode(b64);",
        "        try (GZIPInputStream g = new GZIPInputStream(new ByteArrayInputStream(raw));",
        "             InputStreamReader r = new InputStreamReader(g)) {",
        "            char[] buf = new char[8192]; int n;",
        "            while ((n = r.read(buf)) >= 0) System.out.print(new String(buf, 0, n));",
        "        }",
        "    }",
        "    public static void main(String[] args) throws Exception {",
        "        long seed = args.length > 0 ? Long.parseLong(args[0]) : 12345L;",
    ]
    for s in SEEDS:
        lines.append(f"        if (seed == {s}L) {{ dumpGunzip(B64_{s}); return; }}")
    lines += [
        "        System.err.println(\"unknown seed: \" + seed);",
        "        System.exit(1);",
        "    }",
        "}",
        "",
    ]
    out = os.path.join(ROOT, "oracle", "goldens", "populate_dungeon_golden", "Golden.java")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    print(f"wrote {out}")
    for s in SEEDS:
        print(f"  seed {s}: {data[s][1]} lines")


if __name__ == "__main__":
    main()
