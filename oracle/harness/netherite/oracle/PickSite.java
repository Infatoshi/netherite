package netherite.oracle;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import java.util.ArrayList;
import java.util.List;
import net.minecraft.block.Block;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.world.WorldServer;
import net.minecraft.world.chunk.Chunk;

/**
 * The pickaxe tape's site finder: scans the loaded chunks around the world
 * spawn for tree trunks and the ground under and around the player, read-only,
 * on its own thread while the server is parked. The result names each tree
 * (its lowest log, its top log and the leaf cloud's extents), the ground level
 * next to it, and the first stone under that ground, so the script can aim
 * look angles at face centres.
 */
final class PickSite
{
    private PickSite() {}

    static class Tree
    {
        int x, y, z;          // lowest log
        int top;              // highest log's y
        int count;            // logs in the trunk column
        int side_x, side_z;   // a free-standing column next to the trunk's base? (unused)
    }

    static JsonObject run(final IntegratedServer server, final JsonObject cmd) throws Exception
    {
        final JsonObject[] result = new JsonObject[1];
        final Exception[] error = new Exception[1];
        Thread t = new Thread(new Runnable()
        {
            public void run()
            {
                try
                {
                    result[0] = scan(server, cmd);
                }
                catch (Exception e)
                {
                    error[0] = e;
                }
            }
        }, "Oracle PickSite");
        t.start();
        t.join();
        if (error[0] != null) throw error[0];
        return result[0];
    }

    static JsonObject scan(IntegratedServer server, JsonObject cmd)
    {
        WorldServer ws = server.worldServers[0];
        int cx = ws.getSpawnPoint().posX >> 4, cz = ws.getSpawnPoint().posZ >> 4;
        int r = cmd.has("r") ? cmd.get("r").getAsInt() : 4;
        int idLog = Block.getIdFromBlock(net.minecraft.init.Blocks.log);
        int idLeaves = Block.getIdFromBlock(net.minecraft.init.Blocks.leaves);

        List<Tree> trees = new ArrayList<Tree>();

        // column scan: a log with air (or leaves) at its four horizontal sides at
        // the same y opens a candidate trunk; the trunk is the column above it
        for (int dcx = -r; dcx <= r; ++dcx)
        {
            for (int dcz = -r; dcz <= r; ++dcz)
            {
                if (!ws.getChunkProvider().chunkExists(cx + dcx, cz + dcz)) continue;

                Chunk ch = ws.getChunkProvider().provideChunk(cx + dcx, cz + dcz);
                if (ch == null || ch.isEmpty()) continue;

                for (int lx = 0; lx < 16; ++lx)
                {
                    for (int lz = 0; lz < 16; ++lz)
                    {
                        int top = ch.getTopFilledSegment() + 15;

                        for (int y = 0; y <= top; ++y)
                        {
                            if (Block.getIdFromBlock(ch.func_150810_a(lx, y, lz)) != idLog) continue;
                            int wx = (cx + dcx << 4) + lx, wz = (cz + dcz << 4) + lz;

                            // the column top
                            int ty = y;
                            while (ty + 1 <= top && Block.getIdFromBlock(ch.func_150810_a(lx, ty + 1, lz)) == idLog) ++ty;

                            boolean touching = false;

                            for (int dy = y; dy <= ty; ++dy)
                            {
                                if (Block.getIdFromBlock(ch.func_150810_a(lx + 1 < 16 ? lx + 1 : lx, dy, lz)) != idLog) touching = false;
                            }

                            // one tree per column: keep the lowest
                            Tree tr = new Tree();
                            tr.x = wx;
                            tr.y = y;
                            tr.z = wz;
                            tr.top = ty;
                            tr.count = ty - y + 1;
                            trees.add(tr);
                            y = ty + 1;
                        }
                    }
                }
            }
        }

        JsonObject out = new JsonObject();
        out.addProperty("spawn_x", ws.getSpawnPoint().posX);
        out.addProperty("spawn_y", ws.getSpawnPoint().posY);
        out.addProperty("spawn_z", ws.getSpawnPoint().posZ);
        JsonArray ts = new JsonArray();

        for (Tree tr : trees)
        {
            // skip trunks another trunk stands beside (multi-canopy trees): any log
            // horizontally adjacent at the base's level means part of the same crown
            boolean adj = false;
            Chunk ch = ws.getChunkProvider().provideChunk(tr.x >> 4, tr.z >> 4);

            if (!ch.isEmpty())
            {
                for (int dx = -1; dx <= 1 && !adj; ++dx)
                {
                    for (int dz = -1; dz <= 1 && !adj; ++dz)
                    {
                        if (dx == 0 && dz == 0) continue;
                        int nx = (tr.x & 15) + dx, nz = (tr.z & 15) + dz;

                        if (nx < 0 || nx > 15 || nz < 0 || nz > 15) continue;
                        if (Block.getIdFromBlock(ch.func_150810_a(nx, tr.y, nz)) == idLog) adj = true;
                    }
                }
            }

            JsonObject o = new JsonObject();
            o.addProperty("x", tr.x);
            o.addProperty("y", tr.y);
            o.addProperty("z", tr.z);
            o.addProperty("top", tr.top);
            o.addProperty("logs", tr.count);
            o.addProperty("adj", adj ? 1 : 0);
            // the ground next to the trunk: the highest solid block in the four
            // side columns (leaves and logs excluded), and the first stone below it
            int gid = -1, gy = -1, gz = -1, gstone = -1;
            int[][] dirs = {{2, 0}, {-2, 0}, {0, 2}, {0, -2}};

            for (int[] d : dirs)
            {
                int bx = tr.x + d[0], bz = tr.z + d[1];
                Chunk bc = ws.getChunkProvider().provideChunk(bx >> 4, bz >> 4);
                if (bc.isEmpty()) continue;
                int topb = bc.getTopFilledSegment() + 15;

                for (int y = topb; y > 0; --y)
                {
                    int id = Block.getIdFromBlock(bc.func_150810_a(bx & 15, y, bz & 15));

                    if (id == 0 || id == idLog || id == idLeaves) continue;
                    if (gy < 0 || y > gy)
                    {
                        gy = y;
                        gid = id;
                        gz = bz;
                        gstone = -1;

                        for (int sy = y; sy > 0; --sy)
                        {
                            int sid = Block.getIdFromBlock(bc.func_150810_a(bx & 15, sy, bz & 15));
                            if (sid == Block.getIdFromBlock(net.minecraft.init.Blocks.stone)) { gstone = sy; break; }
                            if (sid != 0 && sid != gid && sid != idLeaves && sid != idLog) { break; }
                        }
                    }

                    break;
                }
            }

            o.addProperty("gx", tr.x);
            o.addProperty("gy", gy);
            o.addProperty("gz", gz);
            o.addProperty("gblock", gid);
            o.addProperty("stone_y", gstone);
            ts.add(o);
        }

        out.add("trees", ts);

        // stone the surface exposes: columns where a stone block has an air
        // neighbour, with the cell above it, within r chunks of the spawn
        int idStone = Block.getIdFromBlock(net.minecraft.init.Blocks.stone);
        int idGrass = Block.getIdFromBlock(net.minecraft.init.Blocks.grass);
        int idDirt = Block.getIdFromBlock(net.minecraft.init.Blocks.dirt);
        JsonArray st = new JsonArray();

        for (int dcx = -r; dcx <= r; ++dcx)
        {
            for (int dcz = -r; dcz <= r; ++dcz)
            {
                if (!ws.getChunkProvider().chunkExists(cx + dcx, cz + dcz)) continue;

                Chunk ch = ws.getChunkProvider().provideChunk(cx + dcx, cz + dcz);
                if (ch == null || ch.isEmpty()) continue;
                int top = ch.getTopFilledSegment() + 15;

                for (int lx = 0; lx < 16; ++lx)
                {
                    for (int lz = 0; lz < 16; ++lz)
                    {
                        for (int y = 60; y <= top; ++y)
                        {
                            if (Block.getIdFromBlock(ch.func_150810_a(lx, y, lz)) != idStone) continue;

                            int air = 0, up = -1;

                            if (Block.getIdFromBlock(ch.func_150810_a(lx, y + 1, lz)) == 0 && y + 1 <= top) { ++air; up = 1; }
                            if (lx > 0 && Block.getIdFromBlock(ch.func_150810_a(lx - 1, y, lz)) == 0) ++air;
                            if (lx < 15 && Block.getIdFromBlock(ch.func_150810_a(lx + 1, y, lz)) == 0) ++air;
                            if (lz > 0 && Block.getIdFromBlock(ch.func_150810_a(lx, y, lz - 1)) == 0) ++air;
                            if (lz < 15 && Block.getIdFromBlock(ch.func_150810_a(lx, y, lz + 1)) == 0) ++air;

                            // only the top-exposed stone: diggable straight down,
                            // with the grass or dirt or air column above it intact
                            if (up != 1 || air != 1) continue;

                            // the y one above must have been grass, dirt or air all
                            // the way up so the stone is on the surface
                            int wy = y + 1, cover = 0;

                            for (int yy = wy; yy <= top; ++yy)
                            {
                                int id = Block.getIdFromBlock(ch.func_150810_a(lx, yy, lz));
                                if (id == 0) continue;
                                if (id != idGrass && id != idDirt) { cover = -1; break; }
                                ++cover;
                            }

                            if (cover < 0 || cover > 3) continue;

                            JsonObject o = new JsonObject();
                            o.addProperty("x", (cx + dcx << 4) + lx);
                            o.addProperty("y", y);
                            o.addProperty("z", (cz + dcz << 4) + lz);
                            o.addProperty("cover", cover);
                            st.add(o);
                        }
                    }
                }
            }
        }

        out.add("stone", st);
        return out;
    }
}
