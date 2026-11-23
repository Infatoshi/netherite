package netherite.oracle;

import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
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
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Random;
import java.util.zip.GZIPOutputStream;
import net.minecraft.block.Block;
import net.minecraft.entity.Entity;
import net.minecraft.entity.item.EntityItem;
import net.minecraft.entity.item.EntityXPOrb;
import net.minecraft.entity.player.EntityPlayerMP;
import net.minecraft.item.Item;
import net.minecraft.item.ItemBed;
import net.minecraft.item.ItemBlock;
import net.minecraft.item.ItemDye;
import net.minecraft.item.ItemDoor;
import net.minecraft.item.ItemEnderEye;
import net.minecraft.item.ItemHoe;
import net.minecraft.item.ItemReed;
import net.minecraft.item.ItemRedstone;
import net.minecraft.item.ItemSeedFood;
import net.minecraft.item.ItemSeeds;
import net.minecraft.item.ItemSign;
import net.minecraft.item.ItemSkull;
import net.minecraft.item.ItemStack;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.tileentity.TileEntity;
import net.minecraft.world.World;
import net.minecraft.world.WorldServer;
import net.minecraft.world.chunk.Chunk;

/**
 * The placement probe: every item that places a block (ItemBlock and its
 * subclasses, ItemReed, ItemDoor, ItemBed, ItemSign, ItemSeeds, ItemSeedFood,
 * ItemSkull, ItemHoe, ItemRedstone, ItemEnderEye and ItemDye's cocoa), driven
 * through ItemStack.tryPlaceItemIntoWorld the way a right click on a block is,
 * the reference for the native port of the placer half of the item stack.
 *
 * One run: a raw region (Probe.rawChunks, population off) far from spawn,
 * loaded cx-major like the move probe, scattered with support blocks of many
 * kinds placed with setBlock flag 2 (solid, glass, logs, slabs, stairs,
 * farmland, chests, end portal frames, snow layers, vines, tall grass, water,
 * ladders, fence gates, reeds, web). The scatter rows are the case targets'
 * support: every placer sees targets it accepts and targets it refuses.
 *
 * The placer is the world's own player, in survival, posed per case (position,
 * yaw, pitch, sneaking); it is never moved with setPosition, so its bounding
 * box stays where the gate left it, far from the region, and every
 * checkNoEntityCollision of a case is over an empty set. Each case draws from
 * one Random(opseed) that also drew the shapes: the case seed (which the world
 * Random is reseeded to, so the drop paths stay reproducible), the item, its
 * damage (nextInt(15) for the dye, whose 15 is bonemeal, another lane's), the
 * stack size, the target (half the cases a shape column, half a random column
 * near the surface), the side, the hit vector, and the placer pose. Then it
 * calls stack.tryPlaceItemIntoWorld and records the return value, every block
 * write in order (Rows.writeListener, id 0xffff for a metadata-only write),
 * the item entities the case dropped (removed again before the next case, so
 * no case sees another's drops: vanilla's drop paths here are BlockReed's, and
 * the run asserts nothing else appeared), the tile entities the case left
 * different from the case before, an FNV-1a 64 hash of the 3x3 chunks around
 * the case position, and the Det digest line.
 *
 * Output DIR/manifest.json, DIR/shapes.bin, DIR/cases.bin, DIR/writes.bin,
 * DIR/drops.bin, DIR/tileentities.bin, DIR/digest.txt.gz, DIR/start.txt,
 * DIR/end.txt, DIR/final.bin.gz. See the layout strings in the manifest.
 */
public final class PlaceProbe
{
    /** The support scatter: {id, metaBase, metaWidth, weight}. Solid rows
     * dominate so ItemBlock placements mostly have a support to aim at. */
    static final int[][] SUPPORTS = {
        {1, 0, 1, 24}, {2, 0, 1, 8}, {3, 0, 1, 8}, {4, 0, 1, 12}, {12, 0, 1, 4}, {13, 0, 1, 4},
        {17, 0, 12, 10}, {162, 0, 8, 3}, {20, 0, 1, 4}, {24, 0, 4, 4}, {79, 0, 1, 3},
        {174, 0, 1, 3}, {85, 0, 6, 4}, {102, 0, 1, 4}, {139, 0, 1, 3}, {155, 0, 1, 2},
        {44, 0, 16, 8}, {53, 0, 8, 6}, {67, 0, 8, 6}, {109, 0, 8, 4}, {128, 0, 8, 3},
        {134, 0, 8, 3}, {135, 0, 8, 3}, {156, 0, 8, 3}, {96, 0, 16, 4}, {107, 0, 8, 4},
        {65, 2, 4, 4}, {106, 1, 15, 6}, {31, 0, 3, 6}, {32, 0, 1, 2}, {78, 0, 8, 8},
        {9, 0, 1, 6}, {60, 0, 8, 6}, {54, 0, 1, 5}, {146, 0, 1, 3}, {63, 0, 16, 4},
        {68, 2, 4, 3}, {120, 0, 8, 6}, {83, 0, 1, 3}, {30, 0, 1, 2}, {171, 0, 16, 3}, {88, 0, 1, 4},
        {121, 0, 1, 2}, {112, 0, 1, 2}, {110, 0, 1, 2}, {82, 0, 1, 2}, {172, 0, 1, 2},
    };
    static final int SURFACE_BAND = 3;
    static final int SHAPE_BYTES = 16;
    static final int CASE_BYTES = 88;
    static final int WRITE_BYTES = 16;
    static final int DROP_BYTES = 63;

    static final long FNV_OFFSET = 0xcbf29ce484222325L;
    static final long FNV_PRIME = 0x100000001b3L;

    /** FNV-1a 64 of a byte array. */
    static long hashBytes(byte[] b)
    {
        long h = FNV_OFFSET;

        for (int i = 0; i < b.length; ++i) h = (h ^ (b[i] & 255)) * FNV_PRIME;

        return h;
    }

    private PlaceProbe() {}

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
        }, "Oracle PlaceProbe");
        t.start();
        t.join();
        if (error[0] != null) throw error[0];
        return result[0];
    }

    static JsonObject dump(IntegratedServer server, JsonObject cmd) throws Exception
    {
        int cx = cmd.get("cx").getAsInt(), cz = cmd.get("cz").getAsInt();
        int radius = cmd.has("radius") ? cmd.get("radius").getAsInt() : 3;
        int ring = cmd.has("ring") ? cmd.get("ring").getAsInt() : 1;
        int cases = cmd.has("cases") ? cmd.get("cases").getAsInt() : 40000;
        int nShapes = cmd.has("shapes") ? cmd.get("shapes").getAsInt() : 1500;
        long opseed = cmd.has("opseed") ? cmd.get("opseed").getAsLong() : 11L;
        File dir = new File(cmd.get("out").getAsString());
        dir.mkdirs();

        WorldServer ws = server.worldServers[0];
        long seed = ws.getSeed();
        Probe.rawChunks = true;

        // the placer: the world's own player, in survival, never repositioned
        // with setPosition (its entity box stays far from the probe region)
        EntityPlayerMP p = (EntityPlayerMP)server.getConfigurationManager().playerEntityList.get(0);
        p.capabilities.isCreativeMode = false;
        p.capabilities.allowEdit = true;

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
        Random r = new Random(opseed);

        int total = 0;

        for (int[] row : SUPPORTS) total += row[3];

        // ------------------------------------------------------------- shapes
        byte[] shape = new byte[SHAPE_BYTES];
        OutputStream shapesOut = new BufferedOutputStream(new FileOutputStream(new File(dir, "shapes.bin")), 1 << 16);
        List<int[]> shapes = new ArrayList<int[]>();
        List<Integer> farmShapes = new ArrayList<Integer>();   // farmland
        List<Integer> soulShapes = new ArrayList<Integer>();   // soul sand
        List<Integer> sandShapes = new ArrayList<Integer>();   // sand
        List<Integer> grassShapes = new ArrayList<Integer>();  // grass or dirt
        List<Integer> darkShapes = new ArrayList<Integer>();   // grass or dirt under a stone cap
        List<Integer> frameShapes = new ArrayList<Integer>();  // end portal frames without an eye
        List<Integer> jungleLogs = new ArrayList<Integer>();   // logs with a jungle species

        for (int i = 0; i < nShapes; ++i)
        {
            int x = bx0 + r.nextInt(width);
            int z = bz0 + r.nextInt(width);
            int yDrawn = 55 + r.nextInt(46);
            int pick = r.nextInt(total);
            int acc = 0;
            int[] row = SUPPORTS[SUPPORTS.length - 1];

            for (int[] cand : SUPPORTS)
            {
                acc += cand[3];

                if (pick < acc)
                {
                    row = cand;
                    break;
                }
            }

            int meta = row[2] > 0 ? row[1] + r.nextInt(row[2]) : row[1];
            int surface = Math.min(99, Math.max(56, ws.getHeightValue(x, z)));
            int y = yDrawn <= surface + SURFACE_BAND && yDrawn >= surface - SURFACE_BAND ? yDrawn : surface - 1;

            ws.setBlock(x, y, z, Block.getBlockById(row[0]), meta, 2);

            le32(shape, 0, x);
            le32(shape, 4, y);
            le32(shape, 8, z);
            shape[12] = (byte)row[0];
            shape[13] = (byte)(row[0] >> 8);
            shape[14] = (byte)meta;
            shape[15] = 0;
            shapesOut.write(shape);
            shapes.add(new int[] {x, y, z});

            if (row[0] == 60 || row[0] == 88 || row[0] == 12)
            {
                (row[0] == 60 ? farmShapes : row[0] == 88 ? soulShapes : sandShapes).add(shapes.size() - 1);
            }

            if (row[0] == 120 && (meta & 4) == 0)
            {
                frameShapes.add(shapes.size() - 1);   // an eyeless end portal frame
            }

            if (row[0] == 2 || row[0] == 3)
            {
                // every fourth grass or dirt shape sits in a stone box: a
                // fully dark cell the mushrooms can live in
                if (r.nextInt(4) == 0)
                {
                    int[][] box = {{x - 1, y + 1, z}, {x + 1, y + 1, z}, {x, y + 1, z - 1}, {x, y + 1, z + 1},
                                   {x, y + 2, z}};

                    for (int[] b : box)
                    {
                        ws.setBlock(b[0], b[1], b[2], Block.getBlockById(1), 0, 2);
                        le32(shape, 0, b[0]);
                        le32(shape, 4, b[1]);
                        le32(shape, 8, b[2]);
                        shape[12] = 1;
                        shape[13] = 0;
                        shape[14] = 0;
                        shape[15] = 0;
                        shapesOut.write(shape);
                        shapes.add(new int[] {b[0], b[1], b[2]});
                    }

                    darkShapes.add(shapes.size() - 6);   /* the grass cell */
                }
                else
                {
                    grassShapes.add(shapes.size() - 1);
                }
            }

            if (row[0] == 17 && (meta & 3) == 3)
            {
                jungleLogs.add(shapes.size() - 1);

                // the cocoa pod's cell: the +x neighbour is cleared, so a pod
                // placed from the log's +x face has somewhere to go
                ws.setBlock(x + 1, y, z, Block.getBlockById(0), 0, 2);
                le32(shape, 0, x + 1);
                le32(shape, 4, y);
                le32(shape, 8, z);
                shape[12] = 0;
                shape[13] = 0;
                shape[14] = 0;
                shape[15] = 0;
                shapesOut.write(shape);
                shapes.add(new int[] {x + 1, y, z});
            }
        }

        // an optional pre-placed chest pair, for the double chest facing's
        // verification: fixed cells, no draws from r, and written as shape
        // rows so the replay places them the same way
        if (cmd.has("pair"))
        {
            JsonArray pp = cmd.get("pair").getAsJsonArray();
            int px0 = pp.get(0).getAsInt(), py0 = pp.get(1).getAsInt(), pz0 = pp.get(2).getAsInt();

            for (int dx = 0; dx < 2; ++dx)
            {
                ws.setBlock(px0 + dx, py0, pz0, Block.getBlockById(54), 0, 2);
                le32(shape, 0, px0 + dx);
                le32(shape, 4, py0);
                le32(shape, 8, pz0);
                shape[12] = 54;
                shape[13] = 0;
                shape[14] = 0;
                shape[15] = 0;
                shapesOut.write(shape);
                shapes.add(new int[] {px0 + dx, py0, pz0});
            }
        }

        shapesOut.close();

        // ------------------------------------------------------------- start
        EntityProbe.writeDetState(new File(dir, "start.txt"), seed);

        // ------------------------------------------------------------ writers
        OutputStream casesOut = new BufferedOutputStream(new FileOutputStream(new File(dir, "cases.bin")), 1 << 16);
        OutputStream writesOut = new BufferedOutputStream(new FileOutputStream(new File(dir, "writes.bin")), 1 << 16);
        OutputStream dropsOut = new BufferedOutputStream(new FileOutputStream(new File(dir, "drops.bin")), 1 << 16);
        TEs tes = new TEs(dir);
        GZIPOutputStream digGz = new GZIPOutputStream(new FileOutputStream(new File(dir, "digest.txt.gz")), 1 << 16);
        PrintWriter dw = new PrintWriter(new OutputStreamWriter(digGz, "UTF-8"));

        Rows.writeListener = new Rows.WriteListener()
        {
            public void onWrite(World w, int x, int y, int z, int id, int meta)
            {
                recordWrite(x, y, z, id, meta);
            }
        };
        sink = writesOut;

        int entitiesAtStart = ws.loadedEntityList.size();
        int placed = 0, refused = 0, totalWrites = 0, totalDrops = 0;

        // debugging channel: per case, the FNV of each watched chunk's bytes
        List<int[]> watch = new ArrayList<int[]>();

        if (cmd.has("watch"))
        {
            for (JsonElement e : cmd.get("watch").getAsJsonArray())
            {
                JsonArray a = e.getAsJsonArray();
                watch.add(new int[] {a.get(0).getAsInt(), a.get(1).getAsInt()});
            }
        }

        DataOutputStream watchOut = null;
        byte[] watchBuf = null;

        int dumpAt = cmd.has("dumpAt") ? cmd.get("dumpAt").getAsInt() : -1;
        List<int[]> dumpChunks = new ArrayList<int[]>();

        if (cmd.has("dumpChunks"))
        {
            for (JsonElement e : cmd.get("dumpChunks").getAsJsonArray())
            {
                JsonArray a = e.getAsJsonArray();
                dumpChunks.add(new int[] {a.get(0).getAsInt(), a.get(1).getAsInt()});
            }
        }

        if (!watch.isEmpty())
        {
            watchOut = new DataOutputStream(new GZIPOutputStream(new FileOutputStream(new File(dir, "watch.bin.gz")), 1 << 16));
            watchBuf = new byte[Probe.CHUNK_BYTES];
        }

        List<Item> items = placerItems();
        int nItems = items.size();
        int[] itemCases = new int[32000], itemPlaced = new int[32000], itemRefused = new int[32000];
        byte[] hashBuf = new byte[Probe.CHUNK_BYTES];
        byte[] rec = new byte[CASE_BYTES];
        byte[] drec = new byte[DROP_BYTES];

        // -------------------------------------------------------- startchunks
        java.lang.reflect.Field gap = Chunk.class.getDeclaredField("isGapLightingUpdated");
        gap.setAccessible(true);
        DataOutputStream sc = new DataOutputStream(new GZIPOutputStream(new FileOutputStream(new File(dir, "startchunks.bin.gz")), 1 << 16));
        byte[] sbuf = new byte[Probe.CHUNK_BYTES];
        byte[] scols = new byte[256];

        for (int lx = x0; lx <= x1; ++lx)
        {
            for (int lz = z0; lz <= z1; ++lz)
            {
                Chunk c = ws.getChunkFromChunkCoords(lx, lz);
                Probe.fillChunkBytes(c, sbuf);
                sc.write(leBytes(lx));
                sc.write(leBytes(lz));
                sc.write(sbuf);

                for (int k = 0; k < 256; ++k) scols[k] = (byte)(c.updateSkylightColumns[k] ? 1 : 0);
                sc.write(scols);
                sc.write(gap.getBoolean(c) ? 1 : 0);
            }
        }

        sc.close();

        for (int i = 0; i < cases; ++i)
        {
            long caseSeed = r.nextLong();
            int pick = r.nextInt(nItems);
            Item it = items.get(pick);
            int itemId = Item.getIdFromItem(it);
            int damage = it instanceof ItemDye ? r.nextInt(15) : (it.getHasSubtypes() ? r.nextInt(16) : 0);
            int count = r.nextInt(6) + 1;

            // the support pin: the seeds, the seed foods, the carrots, the
            // potatoes, the cactus, the sapling, the flowers, the double
            // plant and the mushrooms only ever plant on one support block's
            // top face, and the hoes only till grass or dirt, so their
            // targets come from that support's shape pool with the top face;
            // the cocoa pin aims the dye at a jungle log with its +x
            // neighbour cleared. Every other case draws its target and side
            // the plain way.
            // an ItemBlock's registry id is its block id
            int blockOfItem = it instanceof ItemBlock ? itemId : -1;
            List<Integer> pinPool = null;

            if (it instanceof ItemSeedFood || blockOfItem == 141 || blockOfItem == 142 ||
                (it instanceof ItemSeeds && itemId != 372))
            {
                pinPool = farmShapes;                 // the farmland soils
            }
            else if (itemId == 372)
            {
                pinPool = soulShapes;                 // nether wart's soul sand
            }
            else if (blockOfItem == 32 || blockOfItem == 81)
            {
                pinPool = sandShapes;                 // the sand floors
            }
            else if (blockOfItem == 39 || blockOfItem == 40)
            {
                pinPool = darkShapes;                 // the mushrooms' dark cells
            }
            else if (blockOfItem == 6 || blockOfItem == 31 || blockOfItem == 37 || blockOfItem == 38 ||
                     blockOfItem == 175 || it instanceof ItemHoe)
            {
                pinPool = grassShapes;                // grass or dirt
            }
            else if (blockOfItem == 120)
            {
                pinPool = frameShapes;                // the empty end portal frames
            }

            boolean pinJungle = it instanceof ItemDye && damage == 3;

            int x, z, y, side;

            if (pinJungle && !jungleLogs.isEmpty())
            {
                int s = jungleLogs.get(r.nextInt(jungleLogs.size()));
                x = shapes.get(s)[0];
                y = shapes.get(s)[1];
                z = shapes.get(s)[2];
                side = 2 + r.nextInt(4);   // a horizontal face
            }
            else if (pinPool != null && !pinPool.isEmpty())
            {
                int s = pinPool.get(r.nextInt(pinPool.size()));
                x = shapes.get(s)[0];
                y = shapes.get(s)[1];
                z = shapes.get(s)[2];
                side = 1;                  // the top face
            }
            else if (r.nextInt(2) == 0)
            {
                int s = r.nextInt(shapes.size());
                x = shapes.get(s)[0];
                y = shapes.get(s)[1];
                z = shapes.get(s)[2];
                side = r.nextInt(6);
            }
            else
            {
                x = bx0 + r.nextInt(width);
                z = bz0 + r.nextInt(width);
                int surface = Math.min(99, Math.max(56, ws.getHeightValue(x, z)));
                y = Math.min(254, Math.max(1, surface + r.nextInt(9) - 4));
                side = r.nextInt(6);
            }

            float hx = r.nextFloat(), hy = r.nextFloat(), hz = r.nextFloat();

            double px = x + r.nextInt(21) - 10 + (r.nextDouble() - 0.5D);
            double py = y + r.nextInt(9) - 4;
            double pz = z + r.nextInt(21) - 10 + (r.nextDouble() - 0.5D);
            float yaw = (float)(r.nextDouble() * 360.0D - 180.0D);
            float pitch = (float)(r.nextDouble() * 180.0D - 90.0D);
            boolean sneak = r.nextInt(2) == 1;

            // ------------------------------------------------------- the case
            p.posX = px;
            p.posY = py;
            p.posZ = pz;
            p.rotationYaw = yaw;
            p.rotationPitch = pitch;
            p.setSneaking(sneak);

            ws.rand.setSeed(caseSeed);
            sinkCount = 0;
            ItemStack stack = new ItemStack(it, count, damage);
            boolean ret = stack.tryPlaceItemIntoWorld(p, ws, x, y, z, side, hx, hy, hz);
            int writeCount = sinkCount;

            // the item entities the case dropped, recorded and taken out again
            int drops = 0;
            List<Entity> spawned = ws.loadedEntityList;

            for (int j = spawned.size() - 1; j >= entitiesAtStart; --j)
            {
                Entity e = (Entity)spawned.get(j);
                int q = 0;
                drec[q++] = (byte)(e instanceof EntityXPOrb ? 1 : 0);
                q = le16(drec, q, e instanceof EntityItem ? Item.getIdFromItem(((EntityItem)e).getEntityItem().getItem()) : 0);
                q = le16(drec, q, e instanceof EntityItem ? ((EntityItem)e).getEntityItem().getItemDamage() : 0);
                drec[q++] = (byte)(e instanceof EntityItem ? ((EntityItem)e).getEntityItem().stackSize : 0);
                q = le32(drec, q, e instanceof EntityXPOrb ? ((EntityXPOrb)e).getXpValue() : 0);
                q = le64(drec, q, Double.doubleToRawLongBits(e.posX));
                q = le64(drec, q, Double.doubleToRawLongBits(e.posY));
                q = le64(drec, q, Double.doubleToRawLongBits(e.posZ));
                q = le64(drec, q, Double.doubleToRawLongBits(e.motionX));
                q = le64(drec, q, Double.doubleToRawLongBits(e.motionY));
                q = le64(drec, q, Double.doubleToRawLongBits(e.motionZ));
                q = le32(drec, q, Float.floatToRawIntBits(e.rotationYaw));
                drec[q++] = 0;
                dropsOut.write(drec);
                ++drops;
                // the world's tick loop would remove it; the probe does it
                // now, the way the chunk's list and the loaded list go
                if (e.addedToChunk && ws.theChunkProviderServer.chunkExists(e.chunkCoordX, e.chunkCoordZ))
                    ws.getChunkFromChunkCoords(e.chunkCoordX, e.chunkCoordZ).removeEntity(e);
                ws.loadedEntityList.remove(e);
            }

            if (ws.loadedEntityList.size() != entitiesAtStart) throw new IllegalStateException("case " + i + " left entities behind");

            if (ret) ++placed;
            else ++refused;
            ++itemCases[itemId];

            if (ret) ++itemPlaced[itemId];
            else ++itemRefused[itemId];
            totalWrites += writeCount;
            totalDrops += drops;

            // ------------------------------------------------------- the record
            int q = 0;
            q = le16(rec, q, itemId);
            q = le16(rec, q, damage);
            rec[q++] = (byte)count;
            rec[q++] = (byte)side;
            rec[q++] = (byte)(sneak ? 1 : 0);
            rec[q++] = (byte)(ret ? 1 : 0);
            q = le32(rec, q, x);
            q = le32(rec, q, y);
            q = le32(rec, q, z);
            q = leF(rec, q, hx);
            q = leF(rec, q, hy);
            q = leF(rec, q, hz);
            q = leD(rec, q, px);
            q = leD(rec, q, py);
            q = leD(rec, q, pz);
            q = leF(rec, q, yaw);
            q = leF(rec, q, pitch);
            q = le64(rec, q, caseSeed);
            q = le32(rec, q, writeCount);
            rec[q++] = (byte)stack.stackSize;
            q = le16(rec, q, stack.getItemDamage());
            rec[q++] = (byte)drops;
            q = le64(rec, q, Probe.hashAround(ws, x >> 4, z >> 4, hashBuf));
            casesOut.write(rec);

            if (watchOut != null)
            {
                for (int[] wc : watch)
                {
                    Probe.fillChunkBytes(ws.getChunkFromChunkCoords(wc[0], wc[1]), watchBuf);
                    watchOut.writeLong(hashBytes(watchBuf));
                }
            }

            // debugging: full bytes of listed chunks after one case
            if (dumpAt >= 0 && i == dumpAt)
            {
                DataOutputStream da = new DataOutputStream(new GZIPOutputStream(new FileOutputStream(new File(dir, "at" + dumpAt + ".bin.gz")), 1 << 16));
                byte[] abuf = new byte[Probe.CHUNK_BYTES];
                byte[] acols = new byte[256];

                for (int[] wc : dumpChunks)
                {
                    Chunk c = ws.getChunkFromChunkCoords(wc[0], wc[1]);
                    Probe.fillChunkBytes(c, abuf);
                    da.write(leBytes(wc[0]));
                    da.write(leBytes(wc[1]));
                    da.write(abuf);

                    for (int k = 0; k < 256; ++k) acols[k] = (byte)(c.updateSkylightColumns[k] ? 1 : 0);
                    da.write(acols);
                    da.write(gap.getBoolean(c) ? 1 : 0);
                }

                da.close();
            }

            tes.record(ws, x0, x1, z0, z1);
            EntityProbe.writeDetLine(dw, i);
        }

        casesOut.close();
        writesOut.close();
        dropsOut.close();
        tes.close();

        if (watchOut != null) watchOut.close();
        dw.close();
        digGz.close();
        Rows.writeListener = null;

        // debugging: one more watch sample after the sweep, before final
        if (watchBuf != null)
        {
            DataOutputStream wend = new DataOutputStream(new GZIPOutputStream(new FileOutputStream(new File(dir, "watchend.bin.gz")), 1 << 16));

            for (int[] wc : watch)
            {
                Probe.fillChunkBytes(ws.getChunkFromChunkCoords(wc[0], wc[1]), watchBuf);
                wend.writeLong(hashBytes(watchBuf));
            }

            wend.close();
        }

        EntityProbe.writeDetState(new File(dir, "end.txt"), seed);

        // ------------------------------------------------------------- final
        DataOutputStream out = new DataOutputStream(new GZIPOutputStream(new FileOutputStream(new File(dir, "final.bin.gz")), 1 << 16));
        byte[] buf = new byte[Probe.CHUNK_BYTES];
        byte[] cols = new byte[256];

        for (int lx = x0; lx <= x1; ++lx)
        {
            for (int lz = z0; lz <= z1; ++lz)
            {
                Chunk c = ws.getChunkFromChunkCoords(lx, lz);
                Probe.fillChunkBytes(c, buf);
                out.write(leBytes(lx));
                out.write(leBytes(lz));
                out.write(buf);

                for (int k = 0; k < 256; ++k) cols[k] = (byte)(c.updateSkylightColumns[k] ? 1 : 0);
                out.write(cols);
                out.write(gap.getBoolean(c) ? 1 : 0);
            }
        }
        out.close();

        // ---------------------------------------------------------- manifest
        JsonArray counts = new JsonArray();

        for (int id = 0; id < 32000; ++id)
        {
            if (itemCases[id] == 0) continue;
            JsonArray row = new JsonArray();
            row.add(new JsonPrimitive(id));
            row.add(new JsonPrimitive(itemCases[id]));
            row.add(new JsonPrimitive(itemPlaced[id]));
            row.add(new JsonPrimitive(itemRefused[id]));
            counts.add(row);
        }

        JsonObject m = new JsonObject();
        m.addProperty("seed", seed);
        m.addProperty("kind", "place");
        m.addProperty("cx", cx);
        m.addProperty("cz", cz);
        m.addProperty("radius", radius);
        m.addProperty("ring", ring);
        m.addProperty("cases", cases);
        m.addProperty("shapes", shapes.size());
        m.addProperty("opseed", opseed);
        m.addProperty("order", "cx-major: for cx in " + x0 + ".." + x1 + ", for cz in " + z0 + ".." + z1);
        m.add("loaded", loaded);
        m.addProperty("supports_table", SUPPORTS.length + " rows of {id, meta base, meta width, weight}, " + total + " total weight");
        m.addProperty("shapes_layout", "16 bytes per shape, in placement order: x int32 LE, y int32 LE, z int32 LE, id uint16 LE, meta uint8, pad uint8");
        m.addProperty("shapes_draws", "per shape from Random(opseed), in this order: x = (cx-radius)*16 + nextInt((2*radius+1)*16); z the same with cz; yDrawn = 55 + nextInt(46); pick = nextInt(" + total + ") over the table's weights in order; meta = base + nextInt(width) when width > 0 else the base; then y = yDrawn when yDrawn is within SURFACE_BAND = " + SURFACE_BAND + " of the column surface (min(99, max(56, heightValue(x, z)))) and y = surface - 1 otherwise. Then setBlock(x, y, z, id, meta, 2). No draws come from the world Random (light passes draw nothing), so the scatter leaves exactly the blocks the table names plus the callbacks a flag-2 write runs");
        m.addProperty("placer", "the world's own player, capabilities.allowEdit = true and isCreativeMode = false; the probe never calls setPosition, so the player's entity box stays far from the region and every case's checkNoEntityCollision is over an empty set. Per case the fields posX, posY, posZ, rotationYaw, rotationPitch and the sneaking flag are the case's draws. placer_y_offset (BlockPistonBase.func_150071_a reads posY + 1.82 - yOffset) is " + p.yOffset);
        m.addProperty("case_layout", CASE_BYTES + " bytes per case: item uint16 LE, damage uint16 LE, count uint8, side uint8, sneak uint8, return uint8, x int32 LE, y int32 LE, z int32 LE, hit_x float32 LE, hit_y float32 LE, hit_z float32 LE, placer_x double LE, placer_y double LE, placer_z double LE, yaw float32 LE, pitch float32 LE, case_seed int64 LE, writes uint32 LE, stack_size uint8 after, stack_damage uint16 LE after, drop_count uint8, hash uint64 LE (FNV-1a 64 of the 3x3 chunks around the case's chunk, Probe.hashAround)");
        m.addProperty("placer_y_offset", p.yOffset);
        m.addProperty("case_draws", "per case from the same Random(opseed), in this order: case_seed = nextLong(); item = items[nextInt(nItems)] (nItems = " + nItems + ", the placer items id-ascending); damage = nextInt(15) for the dye (15 is bonemeal, the growth lane's) else nextInt(16) when getHasSubtypes() else 0; count = nextInt(6) + 1; the target: nextInt(2) == 0 takes shapes[nextInt(nShapes)] (its x, y and z) and otherwise draws x = (cx-radius)*16 + nextInt((2*radius+1)*16), z the same with cz and y = clamp(surface + nextInt(9) - 4, 1, 254); side = nextInt(6); hit = three nextFloat; placer x = x + nextInt(21) - 10 + (nextDouble() - 0.5), placer y = y + nextInt(9) - 4, placer z the same shape as x, yaw = (float)(nextDouble() * 360 - 180), pitch = (float)(nextDouble() * 180 - 90), sneak = nextInt(2) == 1. No other draws: tryPlaceItemIntoWorld draws from no stream but the world Random (drops) and Item.itemRand (the eye's smoke), both recorded or replayed");
        m.addProperty("write_layout", WRITE_BYTES + " bytes per write, every case's writes in order: x int32 LE, y int32 LE, z int32 LE, id uint16 LE, meta uint8, pad uint8");
        m.addProperty("write_rule", "every call Rows.onBlock gets from a write that changed the block, in call order; setBlockMetadataWithNotify reports id -1 the same way (0xffff). The shapes' own writes are not recorded: the listener installs after the scatter");
        m.addProperty("drops_layout", DROP_BYTES + " bytes per drop: kind uint8 (0 item, 1 xp), item uint16 LE, damage uint16 LE, count uint8, xp uint32 LE, x, y, z double LE, motionX, motionY, motionZ double LE, yaw float32 LE, pad uint8; the entities each case spawned, in world-list order, and taken out of the world before the next case");
        m.addProperty("drops_note", "the drop paths a pure placement can fire are BlockReed's (a reed that lost its support through a neighbour change): each drop's EntityItem carries the reeds item, its position from the three world-Random offsets of Block.dropBlockAsItem_do (the world Random was reseeded to the case's case_seed first) and its motion and yaw from Math.random. The record's xp fields stay 0 for these; the run asserts nothing else ever spawns");
        m.addProperty("tile_entities", "tileentities.bin");
        m.addProperty("tile_entity_layout", "per case: count uint32 LE, then per entry x int32 LE, y int32 LE, z int32 LE, kind uint8, len uint32 LE, len bytes of the entity's writeToNBT as canonical NBT text; the entries are what the case left different from the case before, sorted by x, then z, then y. The baseline is the world after the shapes");
        m.addProperty("tile_entity_kinds", "1 chest, 2 mob spawner, 3 furnace, 4 dispenser, 5 dropper, 6 hopper, 7 brewing stand, 8 sign, 9 skull, 10 flower pot, 11 comparator, 12 enchanting table, 13 ender chest, 14 command block, 15 beacon, 16 jukebox, 17 note block, 18 daylight detector, 19 end portal, 20 piston");
        m.addProperty("digest", "digest.txt.gz, one line per case: t <case>, then per role 0..3: role <r> seeder <hex> math <hex> split <hex>, then nextId <the OTHER role's next id> (EntityProbe.writeDetLine's format)");
        m.addProperty("start_end", "start.txt and end.txt, the DetProbe snapshot format (resetSeed, worldSeed, nextId, digest per role, split per registered stream), before the sweep and after the last case");
        m.addProperty("startchunks", "startchunks.bin.gz, the whole region in load order in the final.bin.gz format, written after the shapes and before the first case");

        if (dumpAt >= 0)
        {
            StringBuilder dc = new StringBuilder();

            for (int[] wc : dumpChunks) dc.append(wc[0]).append(",").append(wc[1]).append(" ");

            m.addProperty("dump_at", dumpAt);
            m.addProperty("dump_chunks", dc.toString().trim());
        }

        m.addProperty("item_rand", "the eye of ender cases that fill an end portal frame draw Item.itemRand 32 times (two nextFloat per particle of the 16-particle smoke loop, which runs on the server world before the frame check); the split digest line tracks it");
        m.addProperty("world_rand", "the world Random is reseeded to the case's case_seed before each case (a probe pin: no placement path draws it, so the reseed changes nothing; it only makes the drop paths reproducible). Vanilla's chunk and light paths draw nothing");
        m.addProperty("counts", "placed " + placed + ", refused " + refused + ", writes " + totalWrites + ", drops " + totalDrops);
        m.add("item_counts", counts);

        if (!watch.isEmpty())
        {
            JsonArray wl = new JsonArray();
            StringBuilder wlm = new StringBuilder();

            for (int[] wc : watch)
            {
                JsonArray pair = new JsonArray();
                pair.add(new JsonPrimitive(wc[0]));
                pair.add(new JsonPrimitive(wc[1]));
                wl.add(pair);
                wlm.append(wc[0]).append(",").append(wc[1]).append(" ");
            }

            m.addProperty("watch", wlm.toString().trim());
            m.add("watch_chunks", wl);
        }

        PrintWriter mw = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "manifest.json")), "UTF-8"));
        mw.println(m.toString());
        mw.close();

        JsonObject res = new JsonObject();
        res.addProperty("dir", dir.getPath());
        res.addProperty("cases", cases);
        res.addProperty("shapes", shapes.size());
        res.addProperty("placed", placed);
        res.addProperty("refused", refused);
        res.addProperty("writes", totalWrites);
        res.addProperty("drops", totalDrops);
        res.addProperty("tileEntities", tes.entries());
        res.add("items", counts);

        return res;
    }

    // ------------------------------------------------------- the write listener

    static OutputStream sink;
    static int sinkCount;

    /** Called from Rows.onBlock with an already-changed block, in call order. */
    static void recordWrite(int x, int y, int z, int id, int meta)
    {
        if (sink == null) throw new IllegalStateException("a write outside a case");

        byte[] b = new byte[WRITE_BYTES];

        le32(b, 0, x);
        le32(b, 4, y);
        le32(b, 8, z);

        try
        {
            sink.write(b, 0, 12);
            sink.write(new byte[] {(byte)id, (byte)(id >> 8), (byte)meta, 0});
        }
        catch (IOException e)
        {
            throw new RuntimeException("write " + sinkCount, e);
        }

        ++sinkCount;
    }

    // --------------------------------------------------------- the tile entities

    static final class Entry implements Comparable<Entry>
    {
        final int x, y, z, kind;
        final String text;

        Entry(int x, int y, int z, int kind, String text)
        {
            this.x = x;
            this.y = y;
            this.z = z;
            this.kind = kind;
            this.text = text;
        }

        public int compareTo(Entry o)
        {
            if (x != o.x) return x < o.x ? -1 : 1;
            if (z != o.z) return z < o.z ? -1 : 1;
            if (y != o.y) return y < o.y ? -1 : 1;
            return 0;
        }
    }

    /** The region's tile entities after each case, diffed against the case
     * before, in FeatureProbeDungeons.Tiles's record format. */
    static final class TEs
    {
        private final OutputStream out;
        private List<Entry> prev = new ArrayList<Entry>();
        private int entries;

        TEs(File dir) throws IOException
        {
            this.out = new BufferedOutputStream(new FileOutputStream(new File(dir, "tileentities.bin")), 1 << 16);
        }

        void record(WorldServer ws, int lx0, int lx1, int lz0, int lz1) throws IOException
        {
            List<Entry> cur = snapshot(ws, lx0, lx1, lz0, lz1);
            List<Entry> delta = new ArrayList<Entry>();
            int i = 0, j = 0;

            while (i < this.prev.size() || j < cur.size())
            {
                Entry a = i < this.prev.size() ? this.prev.get(i) : null;
                Entry b = j < cur.size() ? cur.get(j) : null;
                int c = a == null ? 1 : b == null ? -1 : a.compareTo(b);

                if (c < 0)
                {
                    delta.add(new Entry(a.x, a.y, a.z, 0, ""));
                    ++i;
                }
                else if (c > 0)
                {
                    delta.add(b);
                    ++j;
                }
                else
                {
                    if (!a.text.equals(b.text)) delta.add(b);
                    ++i;
                    ++j;
                }
            }

            byte[] head = new byte[17];
            le32(head, 0, delta.size());
            this.out.write(head, 0, 4);

            for (Entry e : delta)
            {
                le32(head, 0, e.x);
                le32(head, 4, e.y);
                le32(head, 8, e.z);
                head[12] = (byte)e.kind;
                le32(head, 13, e.text.length());
                this.out.write(head);
                this.out.write(e.text.getBytes(StandardCharsets.UTF_8));
                ++this.entries;
            }

            this.prev = cur;
        }

        int entries()
        {
            return this.entries;
        }

        void close() throws IOException
        {
            this.out.close();
        }

        static List<Entry> snapshot(WorldServer ws, int lx0, int lx1, int lz0, int lz1)
        {
            List<Entry> out = new ArrayList<Entry>();

            for (int lx = lx0; lx <= lx1; ++lx)
            {
                for (int lz = lz0; lz <= lz1; ++lz)
                {
                    Chunk c = ws.getChunkFromChunkCoords(lx, lz);

                    for (Object o : c.chunkTileEntityMap.values())
                    {
                        TileEntity te = (TileEntity)o;
                        int kind = kindOf(te);

                        if (kind == 0) throw new IllegalStateException("the region holds a " + te.getClass().getName() + "; the record has no kind for it");

                        NBTTagCompound t = new NBTTagCompound();
                        te.writeToNBT(t);
                        out.add(new Entry(te.field_145851_c, te.field_145848_d, te.field_145849_e, kind, StructuresProbe.canon(t).toString()));
                    }
                }
            }

            Collections.sort(out);
            return out;
        }

        static int kindOf(TileEntity te)
        {
            if (te instanceof net.minecraft.tileentity.TileEntityChest) return 1;
            if (te instanceof net.minecraft.tileentity.TileEntityMobSpawner) return 2;
            if (te instanceof net.minecraft.tileentity.TileEntityFurnace) return 3;
            if (te instanceof net.minecraft.tileentity.TileEntityDispenser) return 4;
            if (te instanceof net.minecraft.tileentity.TileEntityDropper) return 5;
            if (te instanceof net.minecraft.tileentity.TileEntityHopper) return 6;
            if (te instanceof net.minecraft.tileentity.TileEntityBrewingStand) return 7;
            if (te instanceof net.minecraft.tileentity.TileEntitySign) return 8;
            if (te instanceof net.minecraft.tileentity.TileEntitySkull) return 9;
            if (te instanceof net.minecraft.tileentity.TileEntityFlowerPot) return 10;
            if (te instanceof net.minecraft.tileentity.TileEntityComparator) return 11;
            if (te instanceof net.minecraft.tileentity.TileEntityEnchantmentTable) return 12;
            if (te instanceof net.minecraft.tileentity.TileEntityEnderChest) return 13;
            if (te instanceof net.minecraft.tileentity.TileEntityCommandBlock) return 14;
            if (te instanceof net.minecraft.tileentity.TileEntityBeacon) return 15;
            if (te instanceof net.minecraft.block.BlockJukebox.TileEntityJukebox) return 16;
            if (te instanceof net.minecraft.tileentity.TileEntityNote) return 17;
            if (te instanceof net.minecraft.tileentity.TileEntityDaylightDetector) return 18;
            if (te instanceof net.minecraft.tileentity.TileEntityEndPortal) return 19;
            if (te instanceof net.minecraft.tileentity.TileEntityPiston) return 20;
            return 0;
        }
    }

    // ------------------------------------------------------------- helpers

    static List<Item> placerItems()
    {
        List<Item> out = new ArrayList<Item>();

        for (int id = 0; id <= 32000; ++id)
        {
            Item it = Item.getItemById(id);

            if (it != null && isPlacer(it)) out.add(it);
        }

        return out;
    }

    static boolean isPlacer(Item it)
    {
        return it instanceof ItemBlock || it instanceof ItemReed || it instanceof ItemDoor || it instanceof ItemBed
            || it instanceof ItemSign || it instanceof ItemSeeds || it instanceof ItemSeedFood || it instanceof ItemSkull
            || it instanceof ItemHoe || it instanceof ItemRedstone || it instanceof ItemEnderEye || it instanceof ItemDye;
    }

    static byte[] leBytes(int v)
    {
        return new byte[] {(byte)v, (byte)(v >> 8), (byte)(v >> 16), (byte)(v >> 24)};
    }

    static int le16(byte[] a, int o, int v)
    {
        a[o] = (byte)v;
        a[o + 1] = (byte)(v >> 8);
        return o + 2;
    }

    static int le32(byte[] a, int o, int v)
    {
        for (int i = 0; i < 4; ++i) a[o + i] = (byte)(v >> (8 * i));
        return o + 4;
    }

    static int le64(byte[] a, int o, long v)
    {
        le32(a, o, (int)v);
        le32(a, o + 4, (int)(v >> 32));
        return o + 8;
    }

    static int leF(byte[] a, int o, float v)
    {
        return le32(a, o, Float.floatToRawIntBits(v));
    }

    static int leD(byte[] a, int o, double v)
    {
        return le64(a, o, Double.doubleToRawLongBits(v));
    }
}