#!/usr/bin/env python3
"""Write oracle/goldens/ore_gen_natural_stone/Golden.java.

Primer input = verified caves_real CPU output (chunk 0,0). Ore pass = verbatim
WorldGenMinable.generate with StonePredicate (stone/granite/diorite/andesite).
Seeds: 12345, 0, 7.
"""
import base64
import gzip
import io
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SEEDS = [12345, 0, 7]


def build_caves_cpu(tmp):
    out = os.path.join(tmp, "caves_real_cpu")
    subprocess.run(
        ["cc", "-O2", "-ffp-contract=off", "-Icore", "-o", out,
         os.path.join(ROOT, "cpu", "caves_real.c"), "-lm"],
        cwd=ROOT, check=True,
    )
    return out


def capture_primer(cpu, seed):
    p = subprocess.run([cpu, str(seed)], capture_output=True, text=True, check=True)
    raw = p.stdout.encode()
    buf = io.BytesIO()
    with gzip.GzipFile(fileobj=buf, mode="wb", mtime=0) as gz:
        gz.write(raw)
    return base64.b64encode(buf.getvalue()).decode()


JAVA_HEAD = """\
// ore_gen_natural_stone: WorldGenMinable StonePredicate on verified caves_real primer (chunk 0,0).
// Primer = caves_real CPU output (embedded gzip). Ore = verbatim wgMinable from populate golden.
// Block ids: CB_* + granite/diorite/andesite (21-23), diamond ore (28). Output: 65536 x %04x.
import java.io.*;
import java.util.*;
import java.util.zip.*;

public class Golden {
    static final int STONE = 1, GRANITE = 21, DIORITE = 22, ANDESITE = 23, DIAMOND_ORE = 28;
    static final int POS_X = 8, POS_Y = 24, POS_Z = 8, NUM = 33;

    private static final float[] SIN_TABLE = new float[65536];
    static {
        for (int i = 0; i < 65536; ++i)
            SIN_TABLE[i] = (float)Math.sin((double)i * Math.PI * 2.0D / 65536.0D);
    }
    static float mhsin(float value) { return SIN_TABLE[(int)(value * 10430.378F) & 65535]; }
    static float mhcos(float value) { return SIN_TABLE[(int)(value * 10430.378F + 16384.0F) & 65535]; }
    static int mhfloor(double value) { int i = (int)value; return value < (double)i ? i - 1 : i; }

    static boolean pbNaturalStone(int c) {
        return c == STONE || c == GRANITE || c == DIORITE || c == ANDESITE;
    }

    static final int[] primer = new int[65536];

    static void loadPrimer(String b64) throws Exception {
        byte[] raw = Base64.getDecoder().decode(b64);
        try (BufferedReader br = new BufferedReader(new InputStreamReader(
                new GZIPInputStream(new ByteArrayInputStream(raw))))) {
            String line;
            int i = 0;
            while ((line = br.readLine()) != null)
                primer[i++] = Integer.parseInt(line.trim(), 16);
        }
    }

    static int cbGet(int x, int y, int z) {
        if (x < 0 || x >= 16 || y < 0 || y >= 256 || z < 0 || z >= 16) return 0;
        return primer[x << 12 | z << 8 | y];
    }

    static void cbSet(int x, int y, int z, int v) {
        if (x >= 0 && x < 16 && y >= 0 && y < 256 && z >= 0 && z < 16)
            primer[x << 12 | z << 8 | y] = v;
    }

    // verbatim WorldGenMinable.generate (StonePredicate = natural stone)
    static void wgMinable(Random rand, int posX, int posY, int posZ, int num, int ore) {
        float f = rand.nextFloat() * (float)Math.PI;
        double d0 = (double)((float)(posX + 8) + mhsin(f) * (float)num / 8.0F);
        double d1 = (double)((float)(posX + 8) - mhsin(f) * (float)num / 8.0F);
        double d2 = (double)((float)(posZ + 8) + mhcos(f) * (float)num / 8.0F);
        double d3 = (double)((float)(posZ + 8) - mhcos(f) * (float)num / 8.0F);
        double d4 = (double)(posY + rand.nextInt(3) - 2);
        double d5 = (double)(posY + rand.nextInt(3) - 2);
        for (int i = 0; i < num; ++i) {
            float f1 = (float)i / (float)num;
            double d6 = d0 + (d1 - d0) * (double)f1;
            double d7 = d4 + (d5 - d4) * (double)f1;
            double d8 = d2 + (d3 - d2) * (double)f1;
            double d9 = rand.nextDouble() * (double)num / 16.0D;
            double d10 = (double)(mhsin((float)Math.PI * f1) + 1.0F) * d9 + 1.0D;
            double d11 = (double)(mhsin((float)Math.PI * f1) + 1.0F) * d9 + 1.0D;
            int j = mhfloor(d6 - d10 / 2.0D), k = mhfloor(d7 - d11 / 2.0D), l = mhfloor(d8 - d10 / 2.0D);
            int i1 = mhfloor(d6 + d10 / 2.0D), j1 = mhfloor(d7 + d11 / 2.0D), k1 = mhfloor(d8 + d10 / 2.0D);
            for (int l1 = j; l1 <= i1; ++l1) {
                double d12 = ((double)l1 + 0.5D - d6) / (d10 / 2.0D);
                if (d12 * d12 < 1.0D) {
                    for (int i2 = k; i2 <= j1; ++i2) {
                        double d13 = ((double)i2 + 0.5D - d7) / (d11 / 2.0D);
                        if (d12 * d12 + d13 * d13 < 1.0D) {
                            for (int j2 = l; j2 <= k1; ++j2) {
                                double d14 = ((double)j2 + 0.5D - d8) / (d10 / 2.0D);
                                if (d12 * d12 + d13 * d13 + d14 * d14 < 1.0D) {
                                    if (pbNaturalStone(cbGet(l1, i2, j2)))
                                        cbSet(l1, i2, j2, ore);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

"""


def main():
    with tempfile.TemporaryDirectory() as tmp:
        cpu = build_caves_cpu(tmp)
        b64 = {s: capture_primer(cpu, s) for s in SEEDS}

    lines = [JAVA_HEAD]
    for s in SEEDS:
        lines.append(f'    static final String B64_{s} = "{b64[s]}";')
    lines += [
        "",
        "    public static void main(String[] args) throws Exception {",
        "        long seed = args.length > 0 ? Long.parseLong(args[0]) : 12345L;",
    ]
    for s in SEEDS:
        lines.append(f"        if (seed == {s}L) {{")
        lines.append(f"            loadPrimer(B64_{s});")
        lines.append("            wgMinable(new Random(seed), POS_X, POS_Y, POS_Z, NUM, DIAMOND_ORE);")
        lines.append("            StringBuilder sb = new StringBuilder();")
        lines.append("            for (int i = 0; i < 65536; ++i)")
        lines.append('                sb.append(String.format("%04x%n", primer[i] & 0xFFFF));')
        lines.append("            System.out.print(sb);")
        lines.append("            return;")
        lines.append("        }")
    lines += [
        '        System.err.println("unknown seed: " + seed);',
        "        System.exit(1);",
        "    }",
        "}",
        "",
    ]

    out = os.path.join(ROOT, "oracle", "goldens", "ore_gen_natural_stone", "Golden.java")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w") as f:
        f.write("\n".join(lines))
    print(f"wrote {out} for seeds {SEEDS}")


if __name__ == "__main__":
    main()
