package netheritemod;

import java.util.List;

/**
 * Pure cell-diff for partial {@code Chunk.fillChunk} recording. No Minecraft
 * types: pre/post arrays are packed {@code (blockId << 4) | meta} length 4096
 * with storage index {@code y<<8|z<<4|x} matching {@code BlockStateContainer}.
 *
 * <p>Deterministic emission order: storage index 0..4095 (then callers walk
 * sections 0..15). Unchanged packed values are skipped.
 */
public final class ChunkFillDiff {
    private ChunkFillDiff() {}

    /**
     * Append changed cells as {@code [x,y,z,id,meta]} into {@code out}.
     * @return number of changed cells appended
     */
    public static int appendSectionDiffs(List<int[]> out,
            int chunkX, int chunkZ, int section,
            int[] prePacked, int[] postPacked) {
        if (out == null || prePacked == null || postPacked == null) return 0;
        if (prePacked.length != 4096 || postPacked.length != 4096) return 0;
        if (section < 0 || section > 15) return 0;
        int baseX = chunkX << 4;
        int baseZ = chunkZ << 4;
        int baseY = section << 4;
        int n = 0;
        for (int i = 0; i < 4096; ++i) {
            if (prePacked[i] == postPacked[i]) continue;
            int lx = i & 15;
            int lz = (i >> 4) & 15;
            int ly = (i >> 8) & 15;
            int post = postPacked[i];
            out.add(new int[] {
                baseX | lx, baseY | ly, baseZ | lz, post >> 4, post & 15
            });
            ++n;
        }
        return n;
    }

    /** Pack id/meta the same way the fill capture path does. */
    public static int pack(int id, int meta) {
        return (id << 4) | (meta & 15);
    }
}
