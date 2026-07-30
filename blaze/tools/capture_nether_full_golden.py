#!/usr/bin/env python3
"""Capture nether_full oracle output and write oracle/goldens/nether_full/Golden.java.

Seeds:
  12345 - empty control (terrain only, no fortress blocks in chunk 0,0)
  0, 7  - additional control seeds (terrain only)
  49    - fortress placement (nether brick/fence blocks overlap chunk 0,0)
"""
import base64
import gzip
import io
import os
import subprocess
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SEEDS = [12345, 0, 7, 49]


def build_cpu(tmp):
    out = os.path.join(tmp, "nether_full_cpu")
    subprocess.run(
        ["cc", "-O2", "-ffp-contract=off", "-Icore", "-o", out,
         os.path.join(ROOT, "cpu", "nether_full.c"), "-lm"],
        cwd=ROOT, check=True,
    )
    return out


def capture(cpu, seed):
    p = subprocess.run([cpu, str(seed)], capture_output=True, text=True, check=True)
    raw = p.stdout.encode()
    buf = io.BytesIO()
    with gzip.GzipFile(fileobj=buf, mode="wb", mtime=0) as gz:
        gz.write(raw)
    return base64.b64encode(buf.getvalue()).decode()


def main():
    with tempfile.TemporaryDirectory() as tmp:
        cpu = build_cpu(tmp)
        b64 = {s: capture(cpu, s) for s in SEEDS}

    lines = [
        "// Golden for nether_full: ChunkProviderHell terrain + MapGenNetherBridge placement.",
        "// Vanilla block ids on output (%04x). Seeds 12345/0/7=empty control (no fortress in 0,0).",
        "// Seed 49=fortress placement (nether brick/fence blocks in chunk 0,0).",
        "// Terrain = chunk_provider_nether.h (prepareHeights/buildSurfaces/caves);",
        "// structures = map_gen_fortress.h (structures_placement compose pattern).",
        "import java.io.*; import java.util.zip.*;",
        "public class Golden {",
    ]
    for s in SEEDS:
        lines.append(f'    static final String B64_{s} = "{b64[s]}";')
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
    out = os.path.join(ROOT, "oracle", "goldens", "nether_full", "Golden.java")
    with open(out, "w") as f:
        f.write("\n".join(lines))
    print(f"wrote {out} for seeds {SEEDS}")


if __name__ == "__main__":
    main()
