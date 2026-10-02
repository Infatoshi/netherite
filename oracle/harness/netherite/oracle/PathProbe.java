package netherite.oracle;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import com.google.gson.JsonPrimitive;
import java.io.BufferedOutputStream;
import java.io.DataOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.lang.reflect.Field;
import java.util.Random;
import java.util.zip.GZIPOutputStream;
import net.minecraft.block.Block;
import net.minecraft.entity.Entity;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.pathfinding.PathEntity;
import net.minecraft.pathfinding.PathPoint;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.util.MathHelper;
import net.minecraft.world.World;
import net.minecraft.world.WorldServer;
import net.minecraft.world.chunk.Chunk;

/**
 * The pathfinding probe: random World.getEntityPathToXYZ and
 * getPathEntityToEntity calls on a raw region scattered with the blocks paths
 * care about (fences, walls, fence gates, wooden and iron doors open and
 * closed, trapdoors, cactus, rails, ladders, leaves, slabs, stairs, stone
 * walls, pits, water and lava pools and streams).
 *
 * The region is loaded raw (population off), exactly as the setblock probe
 * loads one, and wider than the widest search window: the distance draws are
 * 16, 32 and 40, so the widest window (the entity-to-entity one) reaches
 * distance + 16 blocks from a start in the ops area, and the loaded square's
 * ring of 4 chunks (64 blocks) keeps every ChunkCache chunk preloaded. Each
 * shape is placed with setBlock flags 2 in its own 5x5 cell of a shuffled
 * cell grid, so patterns never overwrite each other and the only side effects
 * are the placements' own (the liquids' and rails' onBlockAdded).
 *
 * Pathfinding itself writes nothing: every case draws an entity size (the
 * vanilla mob widths and heights, plus the two slime sizes), a start on the
 * terrain, an XYZ target or a target entity, a distance and the four
 * PathFinder flags, then records the inputs and the resulting PathEntity
 * (every point, or null). There are no per-case chunk hashes; the final
 * region compare catches any placement that did not land the same way.
 *
 * Runs on its own thread (the OTHER role, like Probe and ChunkDump) while the
 * server is parked, so no CLIENT or SERVER RNG stream moves.
 *
 * Output DIR/manifest.json, DIR/shapes.bin, DIR/cases.bin. See the layout
 * strings in the manifest for the byte layouts.
 */
public final class PathProbe
{
    /** Entity sizes: width, height (vanilla mobs and the slime sizes). */
    static final float[][] SIZES = {
        {0.6F, 1.8F},   // player, zombie
        {0.9F, 1.3F},
        {0.9F, 0.9F},   // pig
        {1.4F, 0.9F},   // spider
        {0.4F, 0.7F},   // chicken
        {0.6F, 2.9F},
        {1.4F, 2.9F},
        {0.6F, 0.6F},   // slime size 1
        {2.4F, 2.4F},   // slime size 4
    };

    /** Search distances (the maxDistance argument). */
    static final float[] DISTS = {16.0F, 32.0F, 40.0F};

    /** Number of placement patterns (placePattern's switch). */
    static final int N_PATTERNS = 25;

    /** Cell edge in blocks: one pattern per 5x5 cell, its anchor at +2. */
    static final int CELL = 5;

    private PathProbe() {}

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
                    result[0] = dump(server, cmd);
                }
                catch (Exception e)
                {
                    error[0] = e;
                }
            }
        }, "Oracle Path Probe");
        t.start();
        t.join();
        if (error[0] != null) throw error[0];
        return result[0];
    }

    static JsonObject dump(IntegratedServer server, JsonObject cmd) throws Exception
    {
        int cx = cmd.get("cx").getAsInt(), cz = cmd.get("cz").getAsInt();
        int radius = cmd.has("radius") ? cmd.get("radius").getAsInt() : 2;
        int ring = cmd.has("ring") ? cmd.get("ring").getAsInt() : 4;
        int cases = cmd.has("cases") ? cmd.get("cases").getAsInt() : 30000;
        long opseed = cmd.has("opseed") ? cmd.get("opseed").getAsLong() : 1L;
        File dir = new File(cmd.get("out").getAsString());
        dir.mkdirs();

        WorldServer ws = server.worldServers[0];
        long seed = ws.getSeed();
        Probe.rawChunks = true;

        int x0 = cx - radius - ring, x1 = cx + radius + ring;
        int z0 = cz - radius - ring, z1 = cz + radius + ring;
        JsonArray loaded = new JsonArray();

        for (int lx = x0; lx <= x1; ++lx)
        {
            for (int lz = z0; lz <= z1; ++lz)
            {
                if (ws.getChunkFromChunkCoords(lx, lz) == null) throw new IllegalStateException("chunk (" + lx + "," + lz + ") did not load");
                JsonArray pair = new JsonArray();
                pair.add(new JsonPrimitive(lx));
                pair.add(new JsonPrimitive(lz));
                loaded.add(pair);
            }
        }

        int width = (2 * radius + 1) * 16;
        int bx0 = (cx - radius) * 16, bz0 = (cz - radius) * 16;
        int cw = width / CELL;
        int cells = cw * cw;
        Random r = new Random(opseed);

        // ------------------------------------------------------------ shapes
        // Fisher-Yates over the cell indices, then one pattern per shape in
        // its own cell, so no pattern ever overwrites another's blocks.
        int[] order = new int[cells];

        for (int i = 0; i < cells; ++i) order[i] = i;

        for (int i = cells - 1; i > 0; --i)
        {
            int j = r.nextInt(i + 1);
            int t = order[i];
            order[i] = order[j];
            order[j] = t;
        }

        byte[] srow = new byte[16];
        OutputStream shapesOut = new BufferedOutputStream(new FileOutputStream(new File(dir, "shapes.bin")), 1 << 16);
        int nwrites = 0;

        for (int i = 0; i < cells; ++i)
        {
            int px = bx0 + (order[i] % cw) * CELL + CELL / 2;
            int pz = bz0 + (order[i] / cw) * CELL + CELL / 2;
            int h = ws.getHeightValue(px, pz);
            placePattern(ws, r, px, h, pz, r.nextInt(N_PATTERNS));

            for (int k = 0; k < LAST_WRITES; ++k)
            {
                le32(srow, 0, wx[k]);
                le32(srow, 4, wy[k]);
                le32(srow, 8, wz[k]);
                srow[12] = (byte)wid[k];
                srow[13] = (byte)(wid[k] >> 8);
                srow[14] = (byte)wmeta[k];
                srow[15] = 0;
                shapesOut.write(srow);
            }

            nwrites += LAST_WRITES;
        }

        shapesOut.close();

        // ------------------------------------------------------------- cases
        SizeProbe ent = new SizeProbe(ws);
        SizeProbe tgt = new SizeProbe(ws);
        byte[] cbuf = new byte[73];
        OutputStream casesOut = new BufferedOutputStream(new FileOutputStream(new File(dir, "cases.bin")), 1 << 16);
        int nulls = 0, fulls = 0, partials = 0;
        int[][] bySize = new int[SIZES.length][3];
        int[][] byDist = new int[DISTS.length][3];
        int[][] byFlags = new int[16][3];

        for (int i = 0; i < cases; ++i)
        {
            int sizeIdx = r.nextInt(SIZES.length);
            float w = SIZES[sizeIdx][0], ht = SIZES[sizeIdx][1];
            ent.setBox(w, ht);
            int sx = bx0 + r.nextInt(width);
            int sz = bz0 + r.nextInt(width);
            int sy = ws.getHeightValue(sx, sz) - 1 + r.nextInt(3);
            ent.setPosition((double)sx + 0.5D, (double)sy, (double)sz + 0.5D);
            int method = r.nextInt(2);
            int distIdx = r.nextInt(3);
            float dist = DISTS[distIdx];
            int di = (int)dist;
            int txx = sx + r.nextInt(2 * di + 1) - di;

            if (txx < bx0) txx += width;
            else if (txx >= bx0 + width) txx -= width;

            int tzz = sz + r.nextInt(2 * di + 1) - di;

            if (tzz < bz0) tzz += width;
            else if (tzz >= bz0 + width) tzz -= width;

            int tx = txx, tz = tzz;
            boolean f1 = r.nextBoolean(), f2 = r.nextBoolean(), f3 = r.nextBoolean(), f4 = r.nextBoolean();

            double txD, tyD, tzD;
            PathEntity p;

            if (method == 0)
            {
                int ty = ws.getHeightValue(tx, tz) - 1 + r.nextInt(3);
                txD = (double)((float)tx + 0.5F);
                tyD = (double)((float)ty + 0.5F);
                tzD = (double)((float)tz + 0.5F);
                p = ws.getEntityPathToXYZ(ent, tx, ty, tz, dist, f1, f2, f3, f4);
            }
            else
            {
                int ty = ws.getHeightValue(tx, tz) + r.nextInt(2);
                tgt.setBox(w, ht);
                tgt.setPosition((double)tx + 0.5D, (double)ty, (double)tz + 0.5D);
                txD = tgt.posX;
                tyD = tgt.boundingBox.minY;
                tzD = tgt.posZ;
                p = ws.getPathEntityToEntity(ent, tgt, dist, f1, f2, f3, f4);
            }

            int kind; // 0 null, 1 full, 2 partial
            int npts = 0;

            if (p == null)
            {
                kind = 0;
                ++nulls;
            }
            else
            {
                npts = p.getCurrentPathLength();
                PathPoint last = p.getFinalPathPoint();
                int ex = MathHelper.floor_double(txD - (double)(w / 2.0F));
                int ey = MathHelper.floor_double(tyD);
                int ez = MathHelper.floor_double(tzD - (double)(w / 2.0F));
                boolean full = last.xCoord == ex && last.yCoord == ey && last.zCoord == ez;

                if (full) { kind = 1; ++fulls; }
                else { kind = 2; ++partials; }
            }

            bySize[sizeIdx][kind]++;
            byDist[distIdx][kind]++;
            int fb = (f1 ? 1 : 0) | (f2 ? 2 : 0) | (f3 ? 4 : 0) | (f4 ? 8 : 0);
            byFlags[fb][kind]++;

            cbuf[0] = (byte)sizeIdx;
            cbuf[1] = (byte)method;
            cbuf[2] = (byte)fb;
            cbuf[3] = (byte)distIdx;
            le32(cbuf, 4, MathHelper.floor_double(ent.boundingBox.minX));
            le32(cbuf, 8, MathHelper.floor_double(ent.boundingBox.minY + 0.5D));
            le32(cbuf, 12, MathHelper.floor_double(ent.boundingBox.minZ));
            le64(cbuf, 16, Double.doubleToRawLongBits(ent.posX));
            le64(cbuf, 24, Double.doubleToRawLongBits(ent.posY));
            le64(cbuf, 32, Double.doubleToRawLongBits(ent.posZ));
            le64(cbuf, 40, Double.doubleToRawLongBits(txD));
            le64(cbuf, 48, Double.doubleToRawLongBits(tyD));
            le64(cbuf, 56, Double.doubleToRawLongBits(tzD));
            le32(cbuf, 64, Float.floatToRawIntBits(dist));
            cbuf[68] = (byte)(p == null ? 0 : 1);
            le32(cbuf, 69, npts);
            casesOut.write(cbuf, 0, 73);

            if (p != null)
            {
                byte[] pb = new byte[npts * 12];
                int q = 0;

                for (int k = 0; k < npts; ++k)
                {
                    PathPoint pt = p.getPathPointFromIndex(k);
                    le32(pb, q, pt.xCoord);
                    le32(pb, q + 4, pt.yCoord);
                    le32(pb, q + 8, pt.zCoord);
                    q += 12;
                }

                casesOut.write(pb);
            }
        }

        casesOut.close();

        // ------------------------------------------------------- final region
        Field gap = field(Chunk.class, "isGapLightingUpdated");
        DataOutputStream out = new DataOutputStream(new GZIPOutputStream(new FileOutputStream(new File(dir, "final.bin.gz")), 1 << 16));
        byte[] buf = new byte[Probe.CHUNK_BYTES];
        byte[] cols = new byte[256];

        for (int lx = x0; lx <= x1; ++lx)
        {
            for (int lz = z0; lz <= z1; ++lz)
            {
                Chunk c = ws.getChunkFromChunkCoords(lx, lz);
                Probe.fillChunkBytes(c, buf);
                writeLe32(out, lx);
                writeLe32(out, lz);
                out.write(buf);

                for (int i2 = 0; i2 < 256; ++i2) cols[i2] = (byte)(c.updateSkylightColumns[i2] ? 1 : 0);
                out.write(cols);
                out.write(gap.getBoolean(c) ? 1 : 0);
            }
        }

        out.close();

        // ------------------------------------------------------------ manifest
        JsonObject m = new JsonObject();
        m.addProperty("seed", seed);
        m.addProperty("kind", "paths");
        m.addProperty("cx", cx);
        m.addProperty("cz", cz);
        m.addProperty("radius", radius);
        m.addProperty("ring", ring);
        m.addProperty("cases", cases);
        m.addProperty("opseed", opseed);
        m.addProperty("order", "cx-major: for cx in " + x0 + ".." + x1 + ", for cz in " + z0 + ".." + z1);
        m.add("loaded", loaded);
        m.addProperty("sizes", sizesTable());
        m.addProperty("dists", "16, 32, 40 (index distIdx)");
        m.addProperty("patterns", patternsTable());
        m.addProperty("shapes_layout", "16 bytes per placed block, in placement order over all shapes: x int32 LE, y int32 LE, "
            + "z int32 LE, id uint16 LE, meta uint8, pad uint8 (always 0)");
        m.addProperty("shapes_draws", "per shape, from Random(opseed): cell = the Fisher-Yates order of " + cells + " cell"
            + " indices (" + cw + "x" + cw + " cells of " + CELL + "x" + CELL + " blocks over the ops area; shuffle: for i in "
            + (cells - 1) + "..1, j = nextInt(i + 1), swap order[i] and order[j]); anchor px = (cx-radius)*16 + (cell % " + cw
            + ")*" + CELL + " + 2, pz the same with cz; h = getHeightValue(px, pz); pattern = nextInt(" + N_PATTERNS
            + "); then the pattern's own draws (see patterns), each block of the pattern written with setBlock(x, y, z, id, meta, 2)");
        m.addProperty("patterns_note", "every pattern fits its 5x5 cell, so patterns never overwrite each other; the only block"
            + " callbacks a placement can run are the liquids' onBlockAdded (a lava pool can meet ocean water, turning the"
            + " lava to obsidian or cobblestone) and the rails' (the Rail re-metading, which cannot drop a rail: each rail sits"
            + " on its own column's surface)");
        m.addProperty("case_draws", "per case from Random(opseed): sizeIdx = nextInt(" + SIZES.length + "); sx = (cx-radius)*16 + "
            + "nextInt(width), sz the same with cz; sy = getHeightValue(sx, sz) - 1 + nextInt(3); method = nextInt(2); "
            + "distIdx = nextInt(3), dist = {16, 32, 40}[distIdx], di = (int)dist; tx = sx + nextInt(2*di + 1) - di, wrapped once "
            + "into the ops square (+= or -= width when it falls out), tz the same with sz and cz; the four flags as nextBoolean() "
            + "in the order openDoors, closedDoors, avoidsWater, canSwim; then y: for method 0 (XYZ) ty = getHeightValue(tx, tz) - 1 "
            + "+ nextInt(3) and the call is ws.getEntityPathToXYZ(ent, tx, ty, tz, dist, ...); for method 1 (entity) "
            + "ty = getHeightValue(tx, tz) + nextInt(2), the target entity is a fresh SizeProbe of the same size at "
            + "(tx + 0.5, ty, tz + 0.5), and the call is ws.getPathEntityToEntity(ent, tgt, dist, ...)");
        m.addProperty("case_layout", "per case: sizeIdx uint8, method uint8 (0 XYZ, 1 entity), flags uint8 (bit 0 openDoors, "
            + "bit 1 closedDoors, bit 2 avoidsWater, bit 3 canSwim), distIdx uint8, startX/startY/startZ int32 LE "
            + "(floor(minX), floor(minY + 0.5), floor(minZ) of the probe entity, for the replay's own check), posX, posY, posZ "
            + "double LE, targetX, targetY, targetZ double LE (the entity-to-entity method records the target entity's posX, "
            + "boundingBox.minY, posZ; the XYZ method records (float)(int) + 0.5 of each coordinate), dist float32 LE, "
            + "result uint8 (0 null, 1 path), point count uint32 LE, then point count times x, y, z int32 LE (73 bytes plus "
            + "12 per point; null cases have no points)");
        m.addProperty("final_layout", "final.bin.gz, per loaded chunk in load order: cx int32 LE, cz int32 LE, the setblock "
            + "probe's chunk bytes, updateSkylightColumns 256 bytes (0/1), isGapLightingUpdated 1 byte");
        m.addProperty("entity", "netherite.oracle.PathProbe.SizeProbe extends net.minecraft.entity.Entity: no behavior of its "
            + "own, never spawned and never ticked, only setSize and setPosition are called on it; yOffset and ySize stay 0, "
            + "so boundingBox.minY == posY");
        m.addProperty("window_note", "getEntityPathToXYZ builds its ChunkCache over floor(pos) +/- (dist + 8), "
            + "getPathEntityToEntity over floor(pos) +/- (dist + 16); with the widest distance (40) the widest window is 56"
            + " blocks and the ring of 4 chunks (64) around the ops area preloads every chunk in it, so no chunk is generated"
            + " during the cases and every path read agrees with the loaded world. getVerticalOffset reads the entity's world"
            + " (not the cache), so only the canEntityDrown branch would see the cache's window; the probe entities are never"
            + " in water, and the branch is dead here");
        m.addProperty("counts", "nulls " + nulls + ", fulls " + fulls + ", partials " + partials);

        JsonArray js = new JsonArray();

        for (int i = 0; i < SIZES.length; ++i)
        {
            JsonObject c = new JsonObject();
            c.addProperty("size", i);
            c.addProperty("nulls", bySize[i][0]);
            c.addProperty("fulls", bySize[i][1]);
            c.addProperty("partials", bySize[i][2]);
            js.add(c);
        }

        m.add("counts_by_size", js);
        JsonArray jd = new JsonArray();

        for (int i = 0; i < DISTS.length; ++i)
        {
            JsonObject c = new JsonObject();
            c.addProperty("dist", DISTS[i]);
            c.addProperty("nulls", byDist[i][0]);
            c.addProperty("fulls", byDist[i][1]);
            c.addProperty("partials", byDist[i][2]);
            jd.add(c);
        }

        m.add("counts_by_dist", jd);
        JsonArray jf = new JsonArray();

        for (int i = 0; i < 16; ++i)
        {
            JsonObject c = new JsonObject();
            c.addProperty("flags", i);
            c.addProperty("nulls", byFlags[i][0]);
            c.addProperty("fulls", byFlags[i][1]);
            c.addProperty("partials", byFlags[i][2]);
            jf.add(c);
        }

        m.add("counts_by_flags", jf);

        PrintWriter w = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "manifest.json")), "UTF-8"));
        w.println(m.toString());
        w.close();

        JsonObject res = new JsonObject();
        res.addProperty("dir", dir.getPath());
        res.addProperty("chunks", loaded.size());
        res.addProperty("cases", cases);
        res.addProperty("writes", nwrites);
        res.addProperty("nulls", nulls);
        res.addProperty("fulls", fulls);
        res.addProperty("partials", partials);
        return res;
    }

    // ------------------------------------------------------------ the shapes

    /** The last pattern's writes, in write order (the caller flushes them). */
    static final int[] wx = new int[64], wy = new int[64], wz = new int[64], wid = new int[64], wmeta = new int[64];
    static int LAST_WRITES;

    static void put(WorldServer ws, int x, int y, int z, int id, int meta)
    {
        ws.setBlock(x, y, z, Block.getBlockById(id), meta, 2);

        if (LAST_WRITES >= 64) throw new IllegalStateException("pattern wrote more than 64 blocks");
        wx[LAST_WRITES] = x;
        wy[LAST_WRITES] = y;
        wz[LAST_WRITES] = z;
        wid[LAST_WRITES] = id;
        wmeta[LAST_WRITES] = meta;
        ++LAST_WRITES;
    }

    /**
     * One placement pattern at its anchor (x, y, z), where y is the anchor
     * column's heightValue (the first air above the surface). The draw order
     * of the pattern's own Random draws is part of the record contract; the
     * native side replays it. Block ids: 1 stone, 9 water, 10 flowing lava,
     * 11 lava, 27 golden rail, 28 detector rail, 44 stone slab, 53 oak stairs,
     * 64 wooden door, 65 ladder, 66 rail, 71 iron door, 81 cactus, 85 fence,
     * 96 trapdoor, 107 fence gate, 139 cobblestone wall.
     */
    static void placePattern(WorldServer ws, Random r, int x, int y, int z, int pat)
    {
        LAST_WRITES = 0;

        switch (pat)
        {
        case 0: // fence line east-west
            for (int d = -1; d <= 1; ++d) put(ws, x + d, y, z, 85, 0);
            break;

        case 1: // fence line north-south
            for (int d = -1; d <= 1; ++d) put(ws, x, y, z + d, 85, 0);
            break;

        case 2: // cobblestone wall line
        {
            int meta = r.nextInt(2);
            for (int d = -1; d <= 1; ++d) put(ws, x + d, y, z, 139, meta);
            break;
        }

        case 3: // stone wall, 4 wide, 2 high
            for (int d = -1; d <= 2; ++d) { put(ws, x + d, y, z, 1, 0); put(ws, x + d, y + 1, z, 1, 0); }
            break;

        case 4: // pit, 2x2, 3 deep (nothing placed when the ground is too thin)
            if (y - 3 >= 1)
                for (int dy = 1; dy <= 3; ++dy)
                    for (int dx = -1; dx <= 0; ++dx)
                        for (int dz = -1; dz <= 0; ++dz)
                            put(ws, x + dx, y - dy, z + dz, 0, 0);
            break;

        case 5: // fence gate, facing and open bit drawn
        {
            int meta = r.nextInt(4) | (r.nextBoolean() ? 4 : 0);
            put(ws, x, y, z, 107, meta);
            break;
        }

        case 6: // wooden door, closed, both halves
        {
            int facing = r.nextInt(4);
            put(ws, x, y, z, 64, facing);
            put(ws, x, y + 1, z, 64, 8);
            break;
        }

        case 7: // wooden door, open, both halves
        {
            int facing = r.nextInt(4);
            put(ws, x, y, z, 64, facing | 4);
            put(ws, x, y + 1, z, 64, 8);
            break;
        }

        case 8: // iron door, closed, both halves
        {
            int facing = r.nextInt(4);
            put(ws, x, y, z, 71, facing);
            put(ws, x, y + 1, z, 71, 8);
            break;
        }

        case 9: // trapdoor, any facing and open bit
            put(ws, x, y, z, 96, r.nextInt(8));
            break;

        case 10: // cactus, 2 high
            put(ws, x, y, z, 81, 0);
            put(ws, x, y + 1, z, 81, 0);
            break;

        case 11: // rails east-west
            for (int d = -1; d <= 1; ++d) put(ws, x + d, y, z, 66, 0);
            break;

        case 12: // rails north-south
            for (int d = -1; d <= 1; ++d) put(ws, x, y, z + d, 66, 1);
            break;

        case 13: // golden rail, any meta
            put(ws, x, y, z, 27, r.nextInt(8));
            break;

        case 14: // detector rail, any meta
            put(ws, x, y, z, 28, r.nextInt(10));
            break;

        case 15: // ladder on a stone post
        {
            int facing = r.nextInt(4);
            int sx = x + (facing == 2 ? 1 : facing == 3 ? -1 : 0);
            int sz = z + (facing == 0 ? 1 : facing == 1 ? -1 : 0);
            put(ws, sx, y, sz, 1, 0);
            put(ws, x, y, z, 65, 2 + facing);
            break;
        }

        case 16: // leaves, 2x2x2
            for (int dx = 0; dx <= 1; ++dx)
                for (int dy = 0; dy <= 1; ++dy)
                    for (int dz = 0; dz <= 1; ++dz)
                        put(ws, x + dx, y + dy, z + dz, 18, 0);
            break;

        case 17: // slabs: two bottoms and a top
            put(ws, x, y, z, 44, 0);
            put(ws, x + 1, y, z, 44, 0);
            put(ws, x + 2, y, z, 44, 8);
            break;

        case 18: // two oak stairs, any metas
            put(ws, x, y, z, 53, r.nextInt(8));
            put(ws, x + 1, y, z, 53, r.nextInt(8));
            break;

        case 19: // water pool, 3x3 in the surface layer
            for (int dx = -1; dx <= 1; ++dx)
                for (int dz = -1; dz <= 1; ++dz)
                    put(ws, x + dx, y - 1, z + dz, 9, 0);
            break;

        case 20: // lava pool, 3x3 in the surface layer
            for (int dx = -1; dx <= 1; ++dx)
                for (int dz = -1; dz <= 1; ++dz)
                    put(ws, x + dx, y - 1, z + dz, 11, 0);
            break;

        case 21: // flowing water stream, 3 in a row
            for (int d = -1; d <= 1; ++d) put(ws, x + d, y - 1, z, 8, 7);
            break;

        case 22: // flowing lava stream, 3 in a row
            for (int d = -1; d <= 1; ++d) put(ws, x + d, y - 1, z, 10, 7);
            break;

        case 23: // water column, 2 high
            put(ws, x, y, z, 9, 0);
            put(ws, x, y + 1, z, 9, 0);
            break;

        case 24: // 1x2 hole in the ground
            if (y - 2 >= 1) { put(ws, x, y - 1, z, 0, 0); put(ws, x, y - 2, z, 0, 0); }
            break;
        }
    }

    static String sizesTable()
    {
        StringBuilder b = new StringBuilder();

        for (int i = 0; i < SIZES.length; ++i) b.append(i == 0 ? "" : "; ").append(i).append(": ").append(SIZES[i][0]).append('x').append(SIZES[i][1]);

        return b.toString();
    }

    static String patternsTable()
    {
        StringBuilder b = new StringBuilder();
        b.append("0 fence x3 east-west; 1 fence x3 north-south; 2 cobblestone wall x3 (meta nextInt(2)); "
            + "3 stone wall 4x2; 4 pit 2x2x3 air (skipped when y - 3 < 1); 5 fence gate (meta nextInt(4) | nextBoolean() << 2); "
            + "6 wooden door closed (facing nextInt(4), upper meta 8); 7 wooden door open (facing | 4, upper meta 8); "
            + "8 iron door closed (facing nextInt(4), upper meta 8); 9 trapdoor (meta nextInt(8)); 10 cactus x2; "
            + "11 rails x3 east-west meta 0; 12 rails x3 north-south meta 1; 13 golden rail (meta nextInt(8)); "
            + "14 detector rail (meta nextInt(10)); 15 ladder on a stone post (facing nextInt(4), meta 2 + facing); "
            + "16 leaves 2x2x2; 17 slabs (two bottoms meta 0, one top meta 8); 18 oak stairs x2 (meta nextInt(8) each); "
            + "19 water pool 3x3 at y - 1; 20 lava pool 3x3 at y - 1; 21 flowing water x3 meta 7 at y - 1; "
            + "22 flowing lava x3 meta 7 at y - 1; 23 water column x2; 24 pit 1x2 air");
        return b.toString();
    }

    // -------------------------------------------------------------- the entity

    /** The probe's entity: a plain Entity whose only extra is a public setSize. */
    public static final class SizeProbe extends Entity
    {
        public SizeProbe(World p_i1582_1_)
        {
            super(p_i1582_1_);
        }

        public void setBox(float w, float h)
        {
            this.setSize(w, h);
        }

        protected void entityInit() {}

        /** The vanilla save/load pair, empty: the probe records raw fields instead. */
        protected void readEntityFromNBT(NBTTagCompound p_70037_1_) {}

        protected void writeEntityToNBT(NBTTagCompound p_70014_1_) {}
    }

    // ------------------------------------------------------------------ bytes

    static void le32(byte[] a, int o, int v)
    {
        a[o] = (byte)v;
        a[o + 1] = (byte)(v >> 8);
        a[o + 2] = (byte)(v >> 16);
        a[o + 3] = (byte)(v >> 24);
    }

    static void le64(byte[] a, int o, long v)
    {
        for (int i = 0; i < 8; ++i) a[o + i] = (byte)(v >> (8 * i));
    }

    static void le16(byte[] a, int o, int v)
    {
        a[o] = (byte)v;
        a[o + 1] = (byte)(v >> 8);
    }

    private static void writeLe32(OutputStream o, int v) throws IOException
    {
        o.write(v & 255);
        o.write(v >> 8 & 255);
        o.write(v >> 16 & 255);
        o.write(v >> 24 & 255);
    }

    private static Field field(Class<?> c, String name) throws Exception
    {
        Field f = c.getDeclaredField(name);
        f.setAccessible(true);
        return f;
    }
}
