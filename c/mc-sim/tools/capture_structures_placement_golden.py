#!/usr/bin/env python3
"""Capture structures_placement oracle output and write oracle/goldens/structures_placement/Golden.java.

Seeds:
  12345 - empty control (terrain only, no mineshaft/stronghold blocks in chunk 0,0)
  143   - mineshaft/stronghold placement (11 structure blocks)
  310   - mineshaft/stronghold placement (78 structure blocks)

The Java golden embeds gzip-compressed canonical output from the fixed vanilla-id pipeline
(core/structures_placement.h). Terrain generation is the same verbatim MC path as
oracle/goldens/chunk_provider/Golden.java (CB_* shims remapped to vanilla block ids on output).
Structure registration + placement follows core/map_gen_mineshaft.h + map_gen_stronghold.h
(ports of MC 1.11.2 Structure*Pieces / MapGenStructure.generate + generateStructure).
"""
import base64
import gzip
import io
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SEEDS = [12345, 143, 310]


def build_cpu(tmp):
    out = os.path.join(tmp, "structures_placement_cpu")
    subprocess.run(
        ["cc", "-O2", "-ffp-contract=off", "-Icore", "-o", out,
         os.path.join(ROOT, "cpu", "structures_placement.c"), "-lm"],
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
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        cpu = build_cpu(tmp)
        b64 = {s: capture(cpu, s) for s in SEEDS}

    lines = [
        "// Golden for structures_placement: chunk_provider terrain + mineshaft/stronghold placement.",
        "// Vanilla block ids on output (%04x). Seeds 12345=empty control, 143/310=structure placement.",
        "// Terrain path = verbatim oracle/goldens/chunk_provider/Golden.java (CB_* -> vanilla on dump).",
        "// Structure path = core/map_gen_mineshaft.h + map_gen_stronghold.h (MC 1.11.2 Structure*Pieces port).",
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
    out = os.path.join(ROOT, "oracle", "goldens", "structures_placement", "Golden.java")
    with open(out, "w") as f:
        f.write("\n".join(lines))
    print(f"wrote {out} for seeds {SEEDS}")


if __name__ == "__main__":
    main()
