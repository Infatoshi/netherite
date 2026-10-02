package netherite.oracle;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import com.google.gson.JsonPrimitive;
import java.io.DataOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.nio.charset.Charset;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import net.minecraft.block.Block;
import net.minecraft.client.Minecraft;
import net.minecraft.client.renderer.RenderBlocks;
import net.minecraft.client.renderer.Tessellator;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.world.ChunkCache;
import net.minecraft.world.World;
import net.minecraft.item.Item;
import net.minecraft.tileentity.TileEntityFlowerPot;
import net.minecraft.world.WorldServer;
import net.minecraft.world.chunk.Chunk;

/**
 * The mesh reference scenes: a grid far from spawn that holds every registered
 * block in every meta its class uses, each in its own cell on a stone floor
 * with air around it, plus neighborhood cells for the blocks whose mesh depends
 * on neighbors (fence runs and corners, stairs in every facing next to each
 * other, rail curves and slopes, redstone wire lines and corners, two-block
 * doors and beds, crops on farmland, torches on each wall, ladders and vines on
 * walls). The scene lives on the server world in raw chunks (Probe.rawChunks,
 * no population); every placement is a setBlock, so the light engine runs.
 *
 * The scene is built on its own thread while the server is parked (like
 * Probe.dump), then every scene section is meshed on the client thread with
 * RenderBlocks over a ChunkCache of the server world, exactly as MeshProbe
 * meshes the client world; the outputs are the same (sections.bin, chunks.bin,
 * table.bin, atlas.json, census). The scene sits at y 240..255 (section 15) on
 * a stone floor, so only section 15 of each scene chunk is meshed: the raw
 * terrain below never reaches sections.bin.
 *
 * Which metas a block gets is decided by its class, not by hand:
 *   BlockTorch / BlockRedstoneTorch  1..5, and 9..13 for the redstone ones
 *                                    (1..4 hang on a two-tall stone wall)
 *   BlockFire                        0..15 on stone, plus floating cells (no
 *                                    floor) and cells ringed with planks
 *   BlockRedstoneWire                0..15 isolated, meta forced after every
 *                                    other placement so nothing recomputes it
 *   BlockCrops                       0..7 on farmland (moisture 0 and 7)
 *   BlockDoor                        lower 0..3 | 4 open, upper 8 | 1 hinge |
 *                                    4 open, both halves always present
 *   BlockLadder                      2..5, each on a stone wall
 *   BlockRail / Powered / Detector   0..9
 *   BlockStairs                      0..7
 *   BlockLever                       0..15
 *   BlockCactus                      0 and 15
 *   BlockBed                         foot 0..3 with its head, and the occupied
 *                                    variants (type 14)
 *   BlockVine                        1, 2, 4, 8, 15 in a stone box (type 20)
 *   BlockFarmland                    0..7
 *   everything else                  0
 * The mesh3 lane's additions, below the first groups: every meta the remaining
 * render types branch on (repeaters, comparators, pistons and the moving
 * piston, panes, fence gates, lily pads, cauldrons, end portal frames, dragon
 * eggs, tripwire hooks and wire, anvils, hoppers, quartz), beds with the
 * occupied bit, stems with the fruit beside them, cocoa on jungle logs, flower
 * pots with their tile entity contents, and the neighborhoods those renderers
 * read: pane runs and corners of both kinds, a wall line along z, a tripwire
 * line between two hooks, a gate between two cobblestone walls, repeaters with
 * powered wire at their input sides.
 * Neighborhood groups: fence and pane and iron-bar and wall runs and corners,
 * stairs in every facing next to each other, a rail curve loop and slopes,
 * redstone wire lines and corners, a cactus column two blocks tall. A cell is
 * three blocks from its neighbors, so every cell has air around it; the groups
 * reserve their whole footprint from the same cursor.
 *
 * `make run CLASS=MeshScene SEED=2 NAME=scene-2 CMD='{"out":"/abs/dir"}'`.
 */
final class MeshScene
{
    /** the scene's chunk; the grid grows +x and +z from its origin block */
    static final int CHUNK_X = 1000, CHUNK_Z = 1000;
    /** the stone floor: y 240 puts the whole scene in section 15 */
    static final int FLOOR = 240;
    /** row width and row height in blocks; a unit never spans a row wrap */
    static final int ROW_W = 128, ROW_H = 6, GAP = 3;

    static final int STONE = 1, PLANKS = 5, FARMLAND = 60;

    private MeshScene() {}

    static void writeIntLE(DataOutputStream out, int v) throws Exception
    {
        out.write(v & 255);
        out.write(v >> 8 & 255);
        out.write(v >> 16 & 255);
        out.write(v >> 24 & 255);
    }

    static JsonObject run(final IntegratedServer server, final JsonObject cmd) throws Exception
    {
        final File dir = new File(cmd.get("out").getAsString());
        dir.mkdirs();
        MeshProbe.reflect();

        final Plan plan = new Plan();
        final Exception[] error = new Exception[1];
        Thread t = new Thread(new Runnable()
        {
            public void run()
            {
                try
                {
                    build(server, plan);
                }
                catch (Exception e)
                {
                    error[0] = e;
                }
            }
        }, "Oracle MeshScene");
        t.start();
        t.join();
        if (error[0] != null) throw error[0];

        return mesh(Minecraft.getMinecraft(), dir, plan, cmd);
    }

    /** the placement plan and the scene's block bounds */
    static final class Plan
    {
        final List<int[]> ops = new ArrayList<int[]>();         // x, y, z, id, meta
        final List<int[]> wireForce = new ArrayList<int[]>();   // x, y, z, meta
        final List<int[]> potData = new ArrayList<int[]>();     // x, y, z, plant id, plant data
        final JsonArray cells = new JsonArray();
        int minX = Integer.MAX_VALUE, minZ = Integer.MAX_VALUE, maxX = Integer.MIN_VALUE, maxZ = Integer.MIN_VALUE;
        JsonArray loaded;
    }

    static Plan plan;
    static WorldServer ws;
    static int curX, curZ;

    static int chunkOriginX() { return CHUNK_X << 4; }
    static int chunkOriginZ() { return CHUNK_Z << 4; }

    /**
     * The next free origin, `w` x `h` blocks, one empty ring around it. Rows
     * run +x from the grid origin; a filled row wraps to the next.
     */
    static int[] reserve(int w, int h)
    {
        if (curX + w + GAP > chunkOriginX() + ROW_W)
        {
            curX = chunkOriginX();
            curZ += ROW_H;
        }
        int[] r = {curX, curZ};
        curX += w + GAP;
        if (r[0] < plan.minX) plan.minX = r[0];
        if (r[1] < plan.minZ) plan.minZ = r[1];
        if (r[0] + w - 1 > plan.maxX) plan.maxX = r[0] + w - 1;
        if (r[1] + h - 1 > plan.maxZ) plan.maxZ = r[1] + h - 1;
        return r;
    }

    static void put(int x, int y, int z, int id, int meta)
    {
        plan.ops.add(new int[] {x, y, z, id, meta});
    }

    /** one block on a stone floor, air around it */
    static void cell(int id, int meta)
    {
        int[] r = reserve(1, 1);
        put(r[0], FLOOR, r[1], 1, 0);
        put(r[0], FLOOR + 1, r[1], id, meta);
        record(id, meta, r[0], r[1]);
    }

    /** one block on its own block above the stone floor (crops, farmland) */
    static void cellOn(int floorId, int floorMeta, int id, int meta)
    {
        int[] r = reserve(1, 2);
        put(r[0], FLOOR, r[1], 1, 0);
        put(r[0], FLOOR + 1, r[1], floorId, floorMeta);
        put(r[0], FLOOR + 2, r[1], id, meta);
        record(id, meta, r[0], r[1]);
    }

    /** a wall torch: a two-tall stone column behind it, at the meta's wall */
    static void torchCell(int id, int meta)
    {
        int[] r = reserve(1, 2);
        int dx = 0, dz = 0;

        if (meta == 1) dx = -1;
        else if (meta == 2) dx = 1;
        else if (meta == 3) dz = -1;
        else if (meta == 4) dz = 1;

        put(r[0], FLOOR, r[1], 1, 0);

        if (meta != 5 && meta != 0)
        {
            put(r[0] + dx, FLOOR + 1, r[1] + dz, 1, 0);
            put(r[0] + dx, FLOOR + 2, r[1] + dz, 1, 0);
        }

        put(r[0], FLOOR + 1, r[1], id, meta);
        record(id, meta, r[0], r[1]);
    }

    /** a ladder on a two-tall stone wall; meta 2..5 picks the wall */
    static void ladderCell(int meta)
    {
        int[] r = reserve(1, 2);
        int dx = 0, dz = 0;

        if (meta == 2) dz = 1;
        else if (meta == 3) dz = -1;
        else if (meta == 4) dx = 1;
        else dx = -1;

        put(r[0], FLOOR, r[1], 1, 0);
        put(r[0] + dx, FLOOR + 1, r[1] + dz, 1, 0);
        put(r[0] + dx, FLOOR + 2, r[1] + dz, 1, 0);
        put(r[0], FLOOR + 1, r[1], 65, meta);
        record(65, meta, r[0], r[1]);
    }

    /** a vine in a stone box (walls on all four sides); meta picks the sides */
    static void vineCell(int meta)
    {
        int[] r = reserve(3, 2);
        put(r[0], FLOOR, r[1] + 1, 1, 0);

        for (int dy = 1; dy <= 2; ++dy)
        {
            put(r[0] - 1, FLOOR + dy, r[1], 1, 0);
            put(r[0] + 1, FLOOR + dy, r[1], 1, 0);
            put(r[0], FLOOR + dy, r[1] - 1, 1, 0);
            put(r[0], FLOOR + dy, r[1] + 1, 1, 0);
        }

        put(r[0], FLOOR + 1, r[1], 106, meta);
        record(106, meta, r[0], r[1]);
    }

    /** lower + upper door halves, always together; open state lives in both */
    static void doorCell(int id, int facing, boolean open, boolean hinge)
    {
        int[] r = reserve(2, 2);
        put(r[0], FLOOR, r[1], 1, 0);
        put(r[0], FLOOR + 1, r[1], id, facing | (open ? 4 : 0));
        put(r[0], FLOOR + 2, r[1], id, 8 | (hinge ? 1 : 0) | (open ? 4 : 0));
        record(id, facing | (open ? 4 : 0), r[0], r[1]);
    }

    /** foot + head of one bed; the occupied bit does not change the mesh, but
     * every meta the class uses is on the floor */
    static void bedCell(int facing, boolean occupied)
    {
        int[] r = reserve(3, 3);
        int[][] d = {{0, 1}, {-1, 0}, {0, -1}, {1, 0}};
        int bx = r[0] + 1, bz = r[1] + 1;
        int occ = occupied ? 4 : 0;

        for (int dx = 0; dx < 3; ++dx)
        {
            for (int dz = 0; dz < 3; ++dz)
            {
                put(r[0] + dx, FLOOR, r[1] + dz, 1, 0);
            }
        }

        put(bx, FLOOR + 1, bz, 26, facing | occ);
        put(bx + d[facing][0], FLOOR + 1, bz + d[facing][1], 26, 8 | facing | occ);
        record(26, facing | occ, bx, bz);
    }

    /**
     * A run of `n` copies of one block, a corner of three, or a plus, on one
     * shared stone slab; the neighborhood cases the renderers branch on.
     */
    static void run(int id, int meta, int n, boolean corner, boolean plus)
    {
        int w = plus ? 3 : n;
        int h = plus || corner ? 2 : 1;
        int[] r = reserve(w, h);

        for (int dx = 0; dx < w; ++dx)
        {
            for (int dz = 0; dz < h; ++dz)
            {
                put(r[0] + dx, FLOOR, r[1] + dz, 1, 0);
            }
        }

        for (int dx = 0; dx < w; ++dx)
        {
            put(r[0] + dx, FLOOR + 1, r[1], id, meta);
        }

        if (corner || plus)
        {
            for (int dz = 1; dz < h; ++dz)
            {
                put(r[0], FLOOR + 1, r[1] + dz, id, meta);
            }

            for (int dx = 1; dx < w; ++dx)
            {
                put(r[0] + dx, FLOOR + 1, r[1] + 1, id, meta);
            }
        }

        record(id, meta, r[0], r[1]);
    }

    /**
     * The four rail curve metas as a closed 2x2 loop, then two slopes (metas
     * 2 and 3) with a stone block under their raised end, beside the loop.
     */
    static void railGroup()
    {
        int[] r = reserve(4, 3);

        for (int dx = 0; dx < 4; ++dx)
        {
            for (int dz = 0; dz < 3; ++dz)
            {
                put(r[0] + dx, FLOOR, r[1] + dz, 1, 0);
            }
        }

        put(r[0], FLOOR + 1, r[1], 66, 6);
        put(r[0] + 1, FLOOR + 1, r[1], 66, 9);
        put(r[0], FLOOR + 1, r[1] + 1, 66, 7);
        put(r[0] + 1, FLOOR + 1, r[1] + 1, 66, 8);
        put(r[0] + 2, FLOOR + 1, r[1], 66, 2);
        put(r[0] + 2, FLOOR + 1, r[1] + 1, 66, 3);
        put(r[0], FLOOR + 1, r[1] + 2, 66, 2);
        put(r[0], FLOOR + 2, r[1] + 2, 1, 0);   // a stone above the slope's north end
        record(66, 2, r[0], r[1] + 2);
    }

    /** wire lines, a corner and a cross; the isolated powered wires are cells */
    static void wireGroup()
    {
        int[] r = reserve(5, 3);

        for (int dx = 0; dx < 5; ++dx)
        {
            for (int dz = 0; dz < 3; ++dz)
            {
                put(r[0] + dx, FLOOR, r[1] + dz, 1, 0);
            }
        }

        for (int dx = 0; dx < 5; ++dx) put(r[0] + dx, FLOOR + 1, r[1], 55, 0);
        put(r[0], FLOOR + 1, r[1] + 1, 55, 0);
        put(r[0], FLOOR + 1, r[1] + 2, 55, 0);
        put(r[0] + 1, FLOOR + 1, r[1] + 2, 55, 0);
        record(55, 0, r[0], r[1]);
    }

    /** a fire cell: on stone, or floating, optionally ringed with planks */
    static void fireCell(int meta, boolean floating, boolean ringed)
    {
        int[] r = reserve(3, 1);
        int cx = r[0] + 1, cz = r[1] + 1;

        for (int dx = 0; dx < 3; ++dx)
        {
            for (int dz = 0; dz < 3; ++dz)
            {
                boolean ring = Math.abs(dx - 1) + Math.abs(dz - 1) == 1;

                if (!floating) put(r[0] + dx, FLOOR, r[1] + dz, 1, 0);

                if (ringed && ring) put(r[0] + dx, FLOOR + 1, r[1] + dz, 5, 0);
            }
        }

        put(cx, FLOOR + 1, cz, 51, meta);
        record(51, meta, cx, cz);
    }

    /* ---------------------------------------------------------------- mesh3
     * Helpers for the remaining render types: floating cells, stems with a
     * fruit neighbour, cocoa on a jungle log, flower pots with their tile
     * entity contents, a tripwire line between two hooks, a repeater with
     * powered wire at its input sides, a gate between two cobblestone walls
     * and a wall run along z. */

    /** one block in mid air, no floor under it */
    static void floatCell(int id, int meta)
    {
        int[] r = reserve(1, 1);
        put(r[0], FLOOR + 1, r[1], id, meta);
        record(id, meta, r[0], r[1]);
    }

    /** a stem with the fruit block on one side (direction 0..3) */
    static void stemCell(int stem, int fruit, int direction)
    {
        int[] r = reserve(3, 3);

        for (int dx = 0; dx < 3; ++dx)
        {
            for (int dz = 0; dz < 3; ++dz)
            {
                put(r[0] + dx, FLOOR, r[1] + dz, 1, 0);
            }
        }

        put(r[0] + 1, FLOOR + 1, r[1] + 1, FARMLAND, 0);
        put(r[0] + 1, FLOOR + 2, r[1] + 1, stem, 7);
        int fx = r[0] + 1 + (direction == 0 ? -1 : direction == 1 ? 1 : 0);
        int fz = r[1] + 1 + (direction == 2 ? -1 : direction == 3 ? 1 : 0);
        put(fx, FLOOR + 1, fz, fruit, 0);
        record(stem, 7, r[0] + 1, r[1] + 1);
    }

    /** a cocoa pod on a jungle log at its facing side */
    static void cocoaCell(int direction, int age)
    {
        int[] r = reserve(2, 2);
        int[][] d = {{0, 1}, {-1, 0}, {0, -1}, {1, 0}};
        int cx = r[0] + (direction == 1 ? 1 : 0), cz = r[1] + (direction == 0 ? 0 : 1);

        for (int dx = 0; dx < 2; ++dx)
        {
            for (int dz = 0; dz < 2; ++dz)
            {
                put(r[0] + dx, FLOOR, r[1] + dz, 1, 0);
            }
        }

        put(cx, FLOOR + 1, cz, 127, age << 2 | direction);
        put(cx + d[direction][0], FLOOR + 1, cz + d[direction][1], 17, 3);
        record(127, age << 2 | direction, cx, cz);
    }

    /** a flower pot on the floor; the tile entity data is applied in apply() */
    static void potCell(int plant, int data)
    {
        int[] r = reserve(1, 2);
        put(r[0], FLOOR, r[1], 1, 0);
        put(r[0], FLOOR + 1, r[1], 140, 0);
        plan.potData.add(new int[] {r[0], FLOOR + 1, r[1], plant, data});
        record(140, 0, r[0], r[1]);
    }

    /** hooks at the ends, three wire cells between, along x; every cell is
     * recorded so the wire metas are forced last */
    static void tripwireLine(int hookWest, int hookEast, int wireMeta)
    {
        int[] r = reserve(5, 3);

        for (int dx = 0; dx < 5; ++dx)
        {
            for (int dz = 0; dz < 3; ++dz)
            {
                put(r[0] + dx, FLOOR, r[1] + dz, 1, 0);
            }
        }

        put(r[0], FLOOR + 1, r[1] + 1, 131, hookWest);

        for (int dx = 1; dx <= 3; ++dx)
        {
            put(r[0] + dx, FLOOR + 1, r[1] + 1, 132, wireMeta);
            record(132, wireMeta, r[0] + dx, r[1] + 1);
        }

        put(r[0] + 4, FLOOR + 1, r[1] + 1, 131, hookEast);
        record(131, hookWest, r[0], r[1] + 1);
        record(131, hookEast, r[0] + 4, r[1] + 1);
    }

    /** a repeater with wire cells at both input sides, forced to meta 15 last */
    static void repeaterPowered(int id, int facing)
    {
        int[] r = reserve(3, 3);

        for (int dx = 0; dx < 3; ++dx)
        {
            for (int dz = 0; dz < 3; ++dz)
            {
                put(r[0] + dx, FLOOR, r[1] + dz, 1, 0);
            }
        }

        put(r[0] + 1, FLOOR + 1, r[1] + 1, id, facing);
        record(id, facing, r[0] + 1, r[1] + 1);

        // func_149902_h reads x-1 and x+1 for facings 0 and 2, z-1 and z+1 for 1 and 3
        if (facing == 0 || facing == 2)
        {
            put(r[0], FLOOR + 1, r[1] + 1, 55, 0);
            record(55, 15, r[0], r[1] + 1);
            put(r[0] + 2, FLOOR + 1, r[1] + 1, 55, 0);
            record(55, 15, r[0] + 2, r[1] + 1);
        }
        else
        {
            put(r[0] + 1, FLOOR + 1, r[1], 55, 0);
            record(55, 15, r[0] + 1, r[1]);
            put(r[0] + 1, FLOOR + 1, r[1] + 2, 55, 0);
            record(55, 15, r[0] + 1, r[1] + 2);
        }
    }

    /** a fence gate between two cobblestone walls across its opening axis */
    static void gateWallCell(int facing)
    {
        int[] r = reserve(3, 3);

        for (int dx = 0; dx < 3; ++dx)
        {
            for (int dz = 0; dz < 3; ++dz)
            {
                put(r[0] + dx, FLOOR, r[1] + dz, 1, 0);
            }
        }

        put(r[0] + 1, FLOOR + 1, r[1] + 1, 107, facing);
        record(107, facing, r[0] + 1, r[1] + 1);

        if (facing == 0 || facing == 2)
        {
            put(r[0], FLOOR + 1, r[1] + 1, 139, 0);
            record(139, 0, r[0], r[1] + 1);
            put(r[0] + 2, FLOOR + 1, r[1] + 1, 139, 0);
            record(139, 0, r[0] + 2, r[1] + 1);
        }
        else
        {
            put(r[0] + 1, FLOOR + 1, r[1], 139, 0);
            record(139, 0, r[0] + 1, r[1]);
            put(r[0] + 1, FLOOR + 1, r[1] + 2, 139, 0);
            record(139, 0, r[0] + 1, r[1] + 2);
        }
    }

    /** a run of `n` copies along z, optionally with a corner to the +x side */
    static void runZ(int id, int meta, int n, boolean corner)
    {
        int w = corner ? 2 : 1;
        int[] r = reserve(w, n);

        for (int dx = 0; dx < w; ++dx)
        {
            for (int dz = 0; dz < n; ++dz)
            {
                put(r[0] + dx, FLOOR, r[1] + dz, 1, 0);
            }
        }

        for (int dz = 0; dz < n; ++dz)
        {
            put(r[0], FLOOR + 1, r[1] + dz, id, meta);
        }

        if (corner)
        {
            put(r[0] + 1, FLOOR + 1, r[1], id, meta);
        }

        record(id, meta, r[0], r[1]);
    }

    static void record(int id, int meta, int x, int z)
    {
        if (id == 55)
        {
            plan.wireForce.add(new int[] {x, FLOOR + 1, z, meta});
            return;
        }

        JsonArray a = new JsonArray();
        a.add(new JsonPrimitive(id));
        a.add(new JsonPrimitive(meta));
        a.add(new JsonPrimitive(x));
        a.add(new JsonPrimitive(z));
        plan.cells.add(a);
    }

    /**
    /**
     * Build on the worker thread: plan every unit first (pure arithmetic), then
     * load the region the plan needs, then apply the placements in order, then
     * force the wire metas last. Planning first keeps the chunk square fixed
     * before any chunk is loaded, so the mesh radius never generates a chunk.
     */
    static void build(IntegratedServer server, Plan p) throws Exception
    {
        plan = p;
        curX = chunkOriginX();
        curZ = chunkOriginZ();

        // 1. every registered block, meta 0, one cell each
        for (int id = 1; id < 4096; ++id)
        {
            if (!Block.blockRegistry.containsID(id)) continue;
            cell(id, 0);
        }

        // 2. the meta variants the classes use
        for (int meta = 1; meta <= 5; ++meta)
        {
            torchCell(50, meta);
            torchCell(75, meta);
            torchCell(76, meta);
        }

        for (int meta = 9; meta <= 13; ++meta)
        {
            torchCell(75, meta);
            torchCell(76, meta);
        }

        for (int meta = 0; meta <= 15; ++meta) fireCell(meta, false, false);

        fireCell(0, false, true);
        fireCell(7, false, true);
        fireCell(0, true, false);
        fireCell(7, true, false);

        for (int id : new int[] {59, 141, 142})
        {
            for (int meta = 0; meta <= 7; ++meta) cellOn(60, 0, id, meta);
        }

        for (int meta = 0; meta <= 7; ++meta) cellOn(1, 0, 60, meta);

        for (int id : new int[] {64, 71})
        {
            for (int facing = 0; facing <= 3; ++facing)
            {
                for (int open = 0; open <= 1; ++open)
                {
                    for (int hinge = 0; hinge <= 1; ++hinge)
                    {
                        doorCell(id, facing, open != 0, hinge != 0);
                    }
                }
            }
        }

        for (int meta = 2; meta <= 5; ++meta) ladderCell(meta);

        for (int id : new int[] {66, 27, 28})
        {
            for (int meta = 0; meta <= 9; ++meta) cell(id, meta);
        }

        for (int id : new int[] {53, 67, 45, 109, 114, 128, 156, 134, 135, 136, 163, 164})
        {
            for (int meta = 0; meta <= 7; ++meta) cell(id, meta);
        }

        for (int meta = 0; meta <= 15; ++meta) cell(69, meta);

        for (int meta : new int[] {0, 15}) cell(81, meta);

        for (int meta : new int[] {1, 2, 4, 8, 15}) vineCell(meta);

        for (int facing = 0; facing <= 3; ++facing)
        {
            bedCell(facing, false);
            bedCell(facing, true);
        }

        // 3. the neighborhood groups
        run(85, 0, 5, true, true);
        run(113, 0, 5, true, false);
        run(102, 0, 5, true, true);
        run(101, 0, 5, true, true);
        run(139, 0, 5, true, false);
        run(139, 1, 5, true, false);
        run(53, 0, 4, true, false);
        run(53, 4, 4, true, false);
        run(66, 0, 5, false, false);
        run(27, 0, 5, false, false);

        railGroup();
        wireGroup();

        // 4. every meta the remaining render types branch on, and the
        // neighborhoods they read (run/corner panes of both kinds, a wall line
        // along z, a tripwire line between hooks, a gate between walls, stems
        // with the fruit beside them, cocoa on jungle logs, flower pots with
        // their tile entity contents, repeaters with powered wire at their
        // input sides)
        for (int id : new int[] {93, 94, 149, 150})
        {
            for (int meta = 0; meta <= 15; ++meta) cell(id, meta);
        }

        for (int facing = 0; facing <= 3; ++facing)
        {
            repeaterPowered(93, facing);
        }

        repeaterPowered(94, 0);

        for (int meta = 0; meta <= 15; ++meta)
        {
            cell(33, meta);
            cell(29, meta);
        }

        for (int meta = 0; meta <= 7; ++meta) cell(36, meta);

        for (int meta = 0; meta <= 15; ++meta) cell(160, meta);
        run(160, 0, 5, true, true);
        run(160, 7, 4, true, false);

        for (int id : new int[] {104, 105})
        {
            for (int meta = 0; meta <= 7; ++meta) cellOn(FARMLAND, 0, id, meta);
        }

        for (int direction = 0; direction <= 3; ++direction)
        {
            stemCell(104, 86, direction);
            stemCell(105, 103, direction);
        }

        for (int meta = 0; meta <= 15; ++meta) cell(107, meta);
        gateWallCell(2);
        gateWallCell(3);

        for (int meta = 0; meta <= 3; ++meta) cell(111, meta);

        for (int meta = 0; meta <= 3; ++meta) cell(118, meta);

        for (int meta = 0; meta <= 7; ++meta) cell(120, meta);

        for (int meta = 0; meta <= 1; ++meta) cell(122, meta);

        for (int direction = 0; direction <= 3; ++direction)
        {
            for (int age = 0; age <= 2; ++age) cocoaCell(direction, age);
        }

        for (int meta = 0; meta <= 15; ++meta) cell(131, meta);
        floatCell(131, 4);
        floatCell(131, 12);
        tripwireLine(7, 5, 0);
        tripwireLine(15, 13, 2);

        for (int meta = 0; meta <= 15; ++meta) cell(132, meta);

        for (int meta = 0; meta <= 15; ++meta) cell(145, meta);

        for (int plant : new int[] {6, 31, 32, 37, 38, 39, 40, 81})
        {
            if (plant == 6)
            {
                for (int data = 0; data <= 5; ++data) potCell(plant, data);
            }
            else if (plant == 31)
            {
                potCell(plant, 2);
            }
            else
            {
                potCell(plant, 0);
            }
        }

        for (int meta = 0; meta <= 15; ++meta) cell(154, meta);

        for (int meta = 0; meta <= 4; ++meta) cell(155, meta);

        runZ(139, 0, 5, true);

        apply(server, p);
    }

    /**
     * Load the scene's region raw (no population), then apply every planned
     * placement: WorldServer.setBlock(x, y, z, block, meta, 2), which runs the
     * light engine and skips neighbor notifications, in the plan's order. The
     * isolated wire cells are re-set with their metas last, so nothing
     * recomputes them afterwards.
     */
    static void apply(IntegratedServer server, Plan p) throws Exception
    {
        ws = server.worldServers[0];
        Probe.rawChunks = true;

        // the plan's chunk box plus two chunks: the ChunkCache margin, the
        // 17-block light guard and the mesh's radius + 1 all fit inside it
        int cx0 = (p.minX >> 4) - 2, cx1 = (p.maxX >> 4) + 2;
        int cz0 = (p.minZ >> 4) - 2, cz1 = (p.maxZ >> 4) + 2;
        JsonArray loaded = new JsonArray();

        for (int cx = cx0; cx <= cx1; ++cx)
        {
            for (int cz = cz0; cz <= cz1; ++cz)
            {
                if (ws.getChunkFromChunkCoords(cx, cz) == null)
                    throw new IllegalStateException("chunk (" + cx + "," + cz + ") did not load");
                JsonArray pair = new JsonArray();
                pair.add(new JsonPrimitive(cx));
                pair.add(new JsonPrimitive(cz));
                loaded.add(pair);
            }
        }

        p.loaded = loaded;

        for (int[] op : p.ops)
        {
            ws.setBlock(op[0], op[1], op[2], Block.getBlockById(op[3]), op[4], 2);
        }

        // the flower pot tile entities: setBlock creates the empty pot TE, so
        // each cell already holds one to fill
        for (int[] pd : p.potData)
        {
            TileEntityFlowerPot te = (TileEntityFlowerPot)ws.getTileEntity(pd[0], pd[1], pd[2]);

            if (te == null) throw new IllegalStateException("flower pot at " + pd[0] + "," + pd[1] + "," + pd[2] + " has no tile entity");

            te.func_145964_a(Item.getItemFromBlock(Block.getBlockById(pd[3])), pd[4]);
        }

        for (int[] w : p.wireForce)
        {
            ws.setBlock(w[0], w[1], w[2], Block.getBlockById(55), w[3], 2);
        }

        Probe.rawChunks = false;
    }
    /**
     * Mesh on the client thread, exactly as MeshProbe meshes the client world:
     * the same Tessellator save and restore, the same per-chunk ChunkCache, the
     * same record layout, only over the scene's chunks and section 15.
     */
    static JsonObject mesh(Minecraft mc, File dir, Plan p, JsonObject cmd) throws Exception
    {
        RenderBlocks.fancyGrass = mc.gameSettings.fancyGraphics;
        World world = ws;
        int camX = (p.minX + p.maxX) / 2, camZ = (p.minZ + p.maxZ) / 2;
        int camCx = camX >> 4, camCz = camZ >> 4;

        // the camera block: air high above the scene, so the extra
        // inside-the-block draw never fires
        int px = camX, py = FLOOR + 8, pz = camZ;

        // the radius box around the camera chunk that holds every scene chunk
        int radius = 0;

        for (int[] corner : new int[][] {{p.minX, p.minZ}, {p.maxX, p.minZ}, {p.minX, p.maxZ}, {p.maxX, p.maxZ}})
        {
            int d = Math.max(Math.abs((corner[0] >> 4) - camCx), Math.abs((corner[1] >> 4) - camCz));
            if (d > radius) radius = d;
        }

        for (int dx = -radius; dx <= radius; ++dx)
        {
            for (int dz = -radius; dz <= radius; ++dz)
            {
                Chunk c = world.getChunkFromChunkCoords(camCx + dx, camCz + dz);
                if (c == null) throw new IllegalStateException("chunk (" + (camCx + dx) + "," + (camCz + dz) + ") is not loaded");
            }
        }

        Tessellator tess = Tessellator.instance;
        if ((Boolean)MeshProbe.fIsDrawing.get(tess)) throw new IllegalStateException("the Tessellator is mid-draw; MeshScene must run between frames");
        long[] saved = MeshProbe.save(tess);

        Map<Long, ChunkCache> caches = new HashMap<Long, ChunkCache>();
        MeshProbe.Mesh m = new MeshProbe.Mesh();
        DataOutputStream out = new DataOutputStream(new java.io.BufferedOutputStream(new FileOutputStream(new File(dir, "sections.bin")), 1 << 16));

        // chunk order +x then +z, section 15 only, pass 0 then 1. The scene
        // sits at y 240..255, so no other section holds scene blocks.
        for (int dx = -radius; dx <= radius; ++dx)
        {
            for (int dz = -radius; dz <= radius; ++dz)
            {
                Chunk c = world.getChunkFromChunkCoords(camCx + dx, camCz + dz);
                MeshProbe.meshSection(world, c, 15, px, py, pz, caches, m, out);
            }
        }
        out.close();

        MeshProbe.restore(tess, saved);
        String shaAll = MeshProbe.hex(m.md.digest());
        String sha0 = MeshProbe.hex(m.pass[0].digest());
        String sha1 = MeshProbe.hex(m.pass[1].digest());
        int sprites = MeshProbe.atlas(mc, dir, m);
        int verticesTotal = m.vertices[0] + m.vertices[1];
        MeshProbe.writeChunks(world, camCx, camCz, radius, false, dir);

        // the tile entities the mesher's world needs (a flower pot's contents;
        // ChunkCache.getTileEntity is not part of chunks.bin)
        if (!p.potData.isEmpty())
        {
            DataOutputStream te = new DataOutputStream(new java.io.BufferedOutputStream(new FileOutputStream(new File(dir, "tileentities.bin")), 1 << 16));
            MeshScene.writeIntLE(te, p.potData.size());

            for (int[] pd : p.potData)
            {
                for (int i = 0; i < 5; ++i) MeshScene.writeIntLE(te, pd[i]);
            }

            te.close();
        }

        JsonObject table = RenderTable.write(mc, dir);

        JsonObject r = new JsonObject();
        r.addProperty("dir", dir.getPath());
        r.addProperty("seed", Oracle.seed);
        r.addProperty("scene_chunk", CHUNK_X + "," + CHUNK_Z);
        r.addProperty("floor_y", FLOOR);
        r.addProperty("cells", p.cells.size());
        r.addProperty("camera_block", px + "," + py + "," + pz);
        r.addProperty("radius_chunks", radius);
        r.addProperty("sections_nonempty", m.sections);
        r.addProperty("records", m.records);
        r.addProperty("vertices_pass0", m.vertices[0]);
        r.addProperty("vertices_pass1", m.vertices[1]);
        r.addProperty("vertices_total", verticesTotal);
        r.addProperty("raw_ints", m.ints);
        r.add("render_types", MeshProbe.toJson(m.renderTypes));
        r.addProperty("sprites", sprites);
        r.addProperty("sha1_meshes", shaAll);
        r.addProperty("sha1_pass0", sha0);
        r.addProperty("sha1_pass1", sha1);

        JsonObject man = new JsonObject();
        man.addProperty("seed", Oracle.seed);
        man.addProperty("world_seed", world.getSeed());
        man.addProperty("mc", "1.7.10");
        man.addProperty("scene", true);
        man.addProperty("scene_chunk", CHUNK_X + "," + CHUNK_Z);
        man.addProperty("floor_y", FLOOR);
        man.addProperty("sections_meshed", "15");
        man.addProperty("scene_bounds", p.minX + "," + p.minZ + " to " + p.maxX + "," + p.maxZ);
        man.addProperty("camera_chunk", camCx + "," + camCz);
        man.addProperty("player_chunk", camCx + "," + camCz);
        man.addProperty("player_block", px + "," + py + "," + pz);
        man.addProperty("radius_chunks", radius);
        man.addProperty("sections_total", (2 * radius + 1) * (2 * radius + 1) * 16);
        man.addProperty("sections_nonempty", m.sections);
        man.addProperty("sections_pass0", m.nonEmpty[0]);
        man.addProperty("sections_pass1", m.nonEmpty[1]);
        man.addProperty("records", m.records);
        man.addProperty("vertices_pass0", m.vertices[0]);
        man.addProperty("vertices_pass1", m.vertices[1]);
        man.addProperty("vertices_total", verticesTotal);
        man.addProperty("raw_ints", m.ints);
        man.add("render_types", MeshProbe.toJson(m.renderTypes));
        man.add("features", MeshProbe.toJson(m.features));
        man.addProperty("has_texture_records", m.hasTexture);
        man.addProperty("has_color_records", m.hasColor);
        man.addProperty("has_brightness_records", m.hasBrightness);
        man.addProperty("has_normals_records", m.hasNormals);
        man.addProperty("sprites", sprites);
        man.addProperty("inside_draws", m.insideDraws);
        man.addProperty("inside_vertices", m.insideVerts);
        man.addProperty("inside_note", "the scene's camera block is air high above the scene, so the extra inside-the-block draw never fires.");
        man.addProperty("atlas_json", "{\"atlas_width\":w,\"atlas_height\":h,\"mip_levels\":n,\"aniso\":n,\"count\":n,\"sprites\":[{\"name\":str,\"x\":pixels,\"y\":pixels,\"w\":pixels,\"h\":pixels,\"minU\":rawbits,\"maxU\":rawbits,\"minV\":rawbits,\"maxV\":rawbits,\"rotated\":bool}]}");
        man.addProperty("vertex_layout", "each vertex is 8 int32: x, y, z, u, v, color, normal, brightness. x/y/z, u/v are float bits; color is RGBA8 packed little endian; normal is 3 signed bytes (x, y, z) normalized by 127 packed as byte0 | byte1 << 8 | byte2 << 16; brightness is sky << 16 | block. Coordinates are relative to the section origin (renderer translation is -origin).");
        man.addProperty("sections_bin_layout", "little endian records in order cx asc, cz asc, section asc, pass asc: int32 cx | int32 cz | int32 section (0..15) | int32 pass (0 or 1) | int32 vertexCount | int32 hasFlags (1 texture, 2 color, 4 brightness, 8 normals) | vertexCount * 8 int32 raw vertex data. A record exists only when vertexCount > 0. Only section 15 exists in a scene: the scene sits at y 240..255.");
        man.addProperty("sha1_layout", "sha1_meshes is SHA-1 over the records in sections.bin order: for each record, 16 bytes little-endian (cx, cz, section, pass) then the vertexCount * 8 raw int32 as they appear. sha1_passN is the same over the pass N records only.");
        man.addProperty("passes", "pass 0 is opaque, pass 1 is translucent; RenderBlocks sees both through one ChunkCache per section, exactly as updateRenderer builds them.");
        man.addProperty("graphics", "fancy:" + (mc.gameSettings.fancyGraphics ? 1 : 0)
            + " ao:" + mc.gameSettings.ambientOcclusion
            + " mipmap:" + mc.gameSettings.mipmapLevels
            + " aniso:" + mc.gameSettings.anisotropicFiltering);
        man.addProperty("sha1_meshes", shaAll);
        man.addProperty("sha1_pass0", sha0);
        man.addProperty("sha1_pass1", sha1);
        man.addProperty("chunks_bin_layout", "for every chunk within radius + 1 of the camera chunk, cx-major (dx outer ascending, dz inner ascending from -radius-1 to radius+1): int32 cx LE, int32 cz LE, the chunk bytes of Probe's chunk_bytes layout, then the chunk's 256 biome ids (getBiomeArray(), index z << 4 | x, the raw byte read as id & 255).");
        man.addProperty("tileentities_bin_layout", "written when the scene holds flower pots with contents (ChunkCache.getTileEntity is not part of chunks.bin): int32 count LE, then count * 5 int32 LE: x, y, z, the plant's block id, the plant's data (TileEntityFlowerPot.func_145965_a / func_145966_b after apply()). Absent when there is no flower pot with contents.");
        man.add("table", table);
        man.add("census", MeshProbe.censusJson(m));
        man.addProperty("census_layout", "how many times each (block id : meta : render type : path) was passed to renderBlockByRenderType, the extra inside-the-block draw excluded. path is ao / ao-partial / colormult / colormult-partial for render type 0 (ambient occlusion enabled and light value 0 picks ao; a partial bounding box picks -partial) and rtN otherwise.");
        man.add("block_classes", MeshProbe.classJson(m));
        man.add("loaded", p.loaded);
        man.addProperty("loaded_layout", "the chunk load order, cx-major; it fixes which chunk generated first and so the light state the scene starts from");
        man.add("cells", p.cells);
        man.addProperty("cells_layout", "[id, meta, x, z] per placed unit cell; a wire cell (55) records the meta that is forced at the very end of the build, after every other placement");
        man.addProperty("build", "Probe.rawChunks is true while the scene loads; every placement is WorldServer.setBlock(x, y, z, block, meta, 2), which runs the light engine (and skips neighbor notifications), in the order the cells above list; the wire metas are forced last the same way. Built on the worker thread while the server is parked, meshed on the client thread.");

        PrintWriter pw = new PrintWriter(new java.io.OutputStreamWriter(new FileOutputStream(new File(dir, "manifest.json")), Charset.forName("UTF-8")));
        pw.println(man.toString());
        pw.close();
        return r;
    }
}
