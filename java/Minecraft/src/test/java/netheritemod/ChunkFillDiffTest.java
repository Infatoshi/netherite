package netheritemod;

import java.util.ArrayList;
import java.util.List;

/**
 * Focused pure-Java test for bulk fillChunk cell diffs (no Minecraft runtime).
 * Run: see scripts/test_chunk_fill_diff.sh
 *
 * Covers: unchanged skip, deterministic order, synthetic bulk &gt;64 cells
 * (Forge clumpingThreshold default), world-coord packing.
 */
public final class ChunkFillDiffTest {
    public static void main(String[] args) {
        int fails = 0;
        fails += checkUnchangedSkipped();
        fails += checkDeterministicOrder();
        fails += checkBulkOver64();
        fails += checkWorldCoords();
        if (fails != 0) {
            System.err.println("ChunkFillDiffTest FAILED: " + fails + " check(s)");
            System.exit(1);
        }
        System.out.println("ChunkFillDiffTest: PASS");
    }

    private static int checkUnchangedSkipped() {
        int[] pre = new int[4096];
        int[] post = new int[4096];
        for (int i = 0; i < 4096; ++i) {
            pre[i] = ChunkFillDiff.pack(1, 0);
            post[i] = ChunkFillDiff.pack(1, 0);
        }
        List<int[]> out = new ArrayList<int[]>();
        int n = ChunkFillDiff.appendSectionDiffs(out, 0, 0, 4, pre, post);
        if (n != 0 || !out.isEmpty()) {
            System.err.println("unchanged: expected 0, got " + n);
            return 1;
        }
        return 0;
    }

    private static int checkDeterministicOrder() {
        int[] pre = new int[4096];
        int[] post = new int[4096];
        /* Flip three indices out of order; emission must follow index order. */
        int iA = 10;   /* early */
        int iB = 2000; /* mid */
        int iC = 4000; /* late */
        post[iB] = ChunkFillDiff.pack(2, 0);
        post[iC] = ChunkFillDiff.pack(3, 1);
        post[iA] = ChunkFillDiff.pack(4, 2);
        List<int[]> out = new ArrayList<int[]>();
        int n = ChunkFillDiff.appendSectionDiffs(out, 0, 0, 0, pre, post);
        if (n != 3 || out.size() != 3) {
            System.err.println("order: expected 3 changes, got " + n);
            return 1;
        }
        if (out.get(0)[3] != 4 || out.get(1)[3] != 2 || out.get(2)[3] != 3) {
            System.err.println("order: ids not in index order");
            return 1;
        }
        return 0;
    }

    private static int checkBulkOver64() {
        /* Forge clumpingThreshold default is 64; bulk path must accept more. */
        final int N = 80;
        int[] pre = new int[4096];
        int[] post = new int[4096];
        for (int i = 0; i < N; ++i)
            post[i] = ChunkFillDiff.pack(1, i & 15);
        List<int[]> out = new ArrayList<int[]>();
        int n = ChunkFillDiff.appendSectionDiffs(out, 1, -2, 5, pre, post);
        if (n != N || out.size() != N) {
            System.err.println("bulk>64: expected " + N + ", got " + n);
            return 1;
        }
        /* First cell: chunk (1,-2) section 5, index 0 -> (16, 80, -32) stone */
        int[] first = out.get(0);
        if (first[0] != 16 || first[1] != 80 || first[2] != -32
                || first[3] != 1 || first[4] != 0) {
            System.err.println("bulk>64: first cell wrong: "
                + first[0] + "," + first[1] + "," + first[2]
                + " id=" + first[3] + " meta=" + first[4]);
            return 1;
        }
        return 0;
    }

    private static int checkWorldCoords() {
        int[] pre = new int[4096];
        int[] post = new int[4096];
        /* index for local (3, 7, 5): y<<8|z<<4|x = 7<<8 | 5<<4 | 3 */
        int idx = (7 << 8) | (5 << 4) | 3;
        post[idx] = ChunkFillDiff.pack(12, 3);
        List<int[]> out = new ArrayList<int[]>();
        int n = ChunkFillDiff.appendSectionDiffs(out, -1, 2, 3, pre, post);
        if (n != 1) {
            System.err.println("coords: expected 1, got " + n);
            return 1;
        }
        int[] e = out.get(0);
        /* base (-16, 48, 32) + (3, 7, 5) */
        if (e[0] != -13 || e[1] != 55 || e[2] != 37 || e[3] != 12 || e[4] != 3) {
            System.err.println("coords: got " + e[0] + "," + e[1] + "," + e[2]
                + " id=" + e[3] + " meta=" + e[4]);
            return 1;
        }
        return 0;
    }
}
