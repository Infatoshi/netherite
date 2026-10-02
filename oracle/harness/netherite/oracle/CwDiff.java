package netherite.oracle;

import java.util.HashSet;
import java.util.Set;
import net.minecraft.client.Minecraft;
import net.minecraft.server.MinecraftServer;
import net.minecraft.util.MathHelper;
import net.minecraft.world.EnumSkyBlock;
import net.minecraft.world.WorldServer;
import net.minecraft.world.chunk.Chunk;
import net.minecraft.world.chunk.EmptyChunk;

/**
 * --cw-diff T,T2,...: after the client tick of each listed tick, every cell
 * of the nine by nine chunks around the player whose client world light or
 * block differs from the server world's, one "ORACLE CWDIFF" line each (the
 * first 60, then the count): what the client's own light keeping leaves
 * apart from the server's.
 */
final class CwDiff
{
    private CwDiff() {}

    static Set<Long> ticks;
    /** --cw-cells t:cx:cz:sec,...: every cell of those client sections */
    static java.util.List<long[]> cells = new java.util.ArrayList<long[]>();

    static void maybe(Minecraft mc, long tick)
    {
        if (ticks == null || !ticks.contains(tick) || mc.theWorld == null || mc.thePlayer == null) return;
        WorldServer ws = MinecraftServer.getServer().worldServerForDimension(mc.thePlayer.dimension);
        int pcx = MathHelper.floor_double(mc.thePlayer.posX / 16.0D), pcz = MathHelper.floor_double(mc.thePlayer.posZ / 16.0D);
        int n = 0;
        for (int cx = pcx - 4; cx <= pcx + 4; ++cx)
            for (int cz = pcz - 4; cz <= pcz + 4; ++cz)
            {
                Chunk a = mc.theWorld.getChunkFromChunkCoords(cx, cz);
                if (a instanceof EmptyChunk || !ws.getChunkProvider().chunkExists(cx, cz)) continue;
                Chunk b = ws.getChunkFromChunkCoords(cx, cz);
                for (int x = 0; x < 16; ++x)
                    for (int z = 0; z < 16; ++z)
                        for (int y = 0; y < 256; ++y)
                        {
                            int s1 = a.getSavedLightValue(EnumSkyBlock.Sky, x, y, z), s2 = b.getSavedLightValue(EnumSkyBlock.Sky, x, y, z);
                            int b1 = a.getSavedLightValue(EnumSkyBlock.Block, x, y, z), b2 = b.getSavedLightValue(EnumSkyBlock.Block, x, y, z);
                            int i1 = net.minecraft.block.Block.getIdFromBlock(a.func_150810_a(x, y, z));
                            int i2 = net.minecraft.block.Block.getIdFromBlock(b.func_150810_a(x, y, z));
                            if (s1 == s2 && b1 == b2 && i1 == i2) continue;
                            if (n++ < 60)
                                System.out.println("ORACLE CWDIFF t=" + tick + " " + (cx * 16 + x) + " " + y + " " + (cz * 16 + z) + " cw sky" + s1
                                    + " blk" + b1 + " id" + i1 + "  sv sky" + s2 + " blk" + b2 + " id" + i2);
                        }
            }
        System.out.println("ORACLE CWDIFF t=" + tick + " total " + n);
        for (long[] q : cells)
        {
            if (q[0] != tick) continue;
            Chunk a = mc.theWorld.getChunkFromChunkCoords((int)q[1], (int)q[2]);
            Chunk b = ws.getChunkFromChunkCoords((int)q[1], (int)q[2]);
            for (int x = 0; x < 16; ++x)
                for (int z = 0; z < 16; ++z)
                    for (int y = (int)q[3] * 16; y < (int)q[3] * 16 + 16; ++y)
                        System.out.println("ORACLE CWCELL t=" + tick + " " + (q[1] * 16 + x) + " " + y + " " + (q[2] * 16 + z) + " "
                            + a.getSavedLightValue(EnumSkyBlock.Sky, x, y, z) + " " + a.getSavedLightValue(EnumSkyBlock.Block, x, y, z) + " "
                            + net.minecraft.block.Block.getIdFromBlock(a.func_150810_a(x, y, z)) + " sv " + b.getSavedLightValue(EnumSkyBlock.Sky, x, y, z));
        }
        // each client section's light (sky, block per cell, x then z then y)
        // as FNV-1a 64: the native client world prints the same
        for (int cx = pcx - 4; cx <= pcx + 4; ++cx)
            for (int cz = pcz - 4; cz <= pcz + 4; ++cz)
            {
                Chunk a = mc.theWorld.getChunkFromChunkCoords(cx, cz);
                if (a instanceof EmptyChunk) continue;
                for (int sec = 0; sec < 16; ++sec)
                {
                    long h = 0xcbf29ce484222325L;
                    for (int x = 0; x < 16; ++x)
                        for (int z = 0; z < 16; ++z)
                            for (int y = sec * 16; y < sec * 16 + 16; ++y)
                            {
                                h = (h ^ a.getSavedLightValue(EnumSkyBlock.Sky, x, y, z)) * 0x100000001b3L;
                                h = (h ^ a.getSavedLightValue(EnumSkyBlock.Block, x, y, z)) * 0x100000001b3L;
                                h = (h ^ net.minecraft.block.Block.getIdFromBlock(a.func_150810_a(x, y, z))) * 0x100000001b3L;
                            }
                    System.out.println("ORACLE CWHASH t=" + tick + " " + cx + " " + cz + " " + sec + " " + Long.toHexString(h));
                }
            }
    }
}
