package netherite.oracle;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import com.google.gson.JsonPrimitive;
import java.io.BufferedOutputStream;
import java.io.DataOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.List;
import java.util.Random;
import java.util.zip.GZIPOutputStream;
import net.minecraft.block.Block;
import net.minecraft.entity.Entity;
import net.minecraft.entity.item.EntityItem;
import net.minecraft.entity.item.EntityXPOrb;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.world.ChunkPosition;
import net.minecraft.world.World;
import net.minecraft.world.WorldServer;

/**
 * Fuzz probe for the native explosion port: WorldServer.newExplosion over a raw
 * region scattered with blocks of every resistance class, with item entities
 * and experience orbs sitting in the blast. The native side replays the cases
 * and is compared against every recorded field.
 *
 * The region loads the way Move.java and EntityProbe.java load it
 * (Probe.rawChunks, cx-major with cz inner, population off). The scatter is
 * written to shapes.bin and applied through World.setBlock flag 2; its own
 * Random (fill_seed) is separate from the case chooser, so the replay aligns
 * the two streams independently. Torch placement is deterministic: the torch
 * draw is skipped when the column already has one or the block below is not
 * opaque and renderAsNormalBlock, and the torch goes in with metadata 0, which
 * is what a real placement without a facing does - BlockTorch.onBlockAdded
 * then picks the facing, through setBlockMetadataWithNotify, exactly as the
 * game would.
 *
 * Per case, in order:
 *   1. the chooser draws, from one Random(opseed): position, size, flaming,
 *      smoking, then the entities' placement and their motion overrides
 *   2. world.rand is seeded to opseed + case_step * case (each case starts its
 *      world Random fresh, the way DropsProbe does) and the case's entities
 *      spawn: EntityItem or EntityXPOrb the way EntityProbe spawns them (the
 *      constructor's Det OTHER draws, then the probe's motion override), never
 *      ticked
 *   3. the body of WorldServer.newExplosion, on the live Explosion object so
 *      the probe can read the private rng state after: doExplosionA, then
 *      doExplosionB(false) (no particle draws, the way the server passes it)
 *   4. the record: the affected positions in doExplosionB's order (the
 *      HashSet iteration order), the fire placements (the writes to id 51,
 *      in order), every block write the case made (through Rows' write
 *      listener), every entity the explosion spawned (drops and
 *      neighbor-pop drops), every case entity's after state, the region hash,
 *      the world Random's state, the explosion's private rng state and the Det
 *      OTHER math state
 *   5. the case's entities and everything the explosion spawned are taken out
 *      of the world, so the next case starts from the scatter only
 *
 * Item stacks are drawn from ITEM_IDS, which does not contain nether_star:
 * EntityItem.attackEntityFrom ignores explosion damage for that stack, and the
 * native entity pass has no explosion branch for it (a follow-up). Containers
 * (chests, furnaces, ...) are not in the scatter: Chunk.func_150807_a runs the
 * old block's breakBlock, and a container's drops its contents and draws from
 * Item.itemRand, which the entity list does not carry yet.
 *
 * Runs on its own thread (the OTHER role) while the server is parked.
 *
 * Output DIR/manifest.json, DIR/shapes.bin, DIR/cases.txt.gz, DIR/start.txt,
 * DIR/end.txt.
 */
final class ExplosionProbe
{
    static final long FILL_SEED = 0x5eed5eedL;
    static final int CASE_STEP = 1000003;

    /** The scatter ids: every resistance class, air pockets included. Torch
     * (50) is placed standing, at most one per column, only on a block that is
     * opaque and renderAsNormalBlock. No containers, no bush family (the
     * explosion would pop them through BlockBush.onNeighborBlockChange with no
     * drop - that is blockcb.c's existing case, but the scatter would spend
     * its draws oddly). */
    static final int[] SCATTER = {0, 0, 0, 0, 1, 3, 4, 5, 20, 49, 18, 16, 56, 21, 129, 35, 47, 2, 12, 79, 82, 9, 11, 50, 14, 15};

    /** The stacks placed entities carry: no nether_star. */
    static final int[] ITEM_IDS = {264, 265, 266, 337, 262, 331};

    /** The explosion sizes the chooser picks. */
    static final float[] SIZES = {0.5F, 1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F, 8.0F};

    static final long FNV_OFFSET = 0xcbf29ce484222325L;
    static final long FNV_PRIME = 0x100000001b3L;

    static Field explosionRngField;
    static Field itemHealthField;
    static Field orbHealthField;
    static Field itemAgeField;
    static Field orbAgeField;
    static Field itemDelayField;
    static Field itemHoverField;

    /** The write record, live while caseOpen (the Rows listener sees every
     * setBlock in the world, the scatter's included). */
    static final StringBuilder[] writesBuf = new StringBuilder[1];
    static final int[] writesCount = new int[1];
    static boolean caseOpen;

    private ExplosionProbe() {}

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
        }, "Oracle Explosion");
        t.start();
        t.join();
        if (error[0] != null) throw error[0];
        return result[0];
    }

    static JsonObject dump(IntegratedServer server, JsonObject cmd) throws Exception
    {
        int cx = cmd.get("cx").getAsInt(), cz = cmd.get("cz").getAsInt();
        int radius = cmd.has("radius") ? cmd.get("radius").getAsInt() : 2;
        int ring = cmd.has("ring") ? cmd.get("ring").getAsInt() : 3;
        int cases = cmd.has("cases") ? cmd.get("cases").getAsInt() : 3000;
        long opseed = cmd.has("opseed") ? cmd.get("opseed").getAsLong() : 11L;
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
        int ylo = 30, yhi = 70;

        // ------------------------------------------------------------- shapes
        Random fr = new Random(FILL_SEED);
        byte[] shape = new byte[16];
        OutputStream shapesOut = new BufferedOutputStream(new FileOutputStream(new File(dir, "shapes.bin")), 1 << 16);
        int nshapes = 0;

        for (int x = 0; x < width; ++x)
        {
            for (int z = 0; z < width; ++z)
            {
                boolean torch = false;

                for (int y = ylo; y <= yhi; ++y)
                {
                    int id = SCATTER[fr.nextInt(SCATTER.length)];
                    int meta = id == 35 ? fr.nextInt(16) : (id == 5 ? fr.nextInt(6) : (id == 17 ? fr.nextInt(4) : 0));

                    if (id == 50)
                    {
                        if (torch || !solidAt(ws, bx0 + x, y - 1, bz0 + z))
                        {
                            continue;
                        }

                        torch = true;
                    }
                    else if (id == 0)
                    {
                        continue;
                    }

                    ws.setBlock(bx0 + x, y, bz0 + z, Block.getBlockById(id), meta, 2);
                    le32(shape, 0, bx0 + x);
                    le32(shape, 4, y);
                    le32(shape, 8, bz0 + z);
                    shape[12] = (byte)id;
                    shape[13] = (byte)(id >> 8);
                    shape[14] = (byte)meta;
                    shape[15] = 0;
                    shapesOut.write(shape, 0, 16);
                    ++nshapes;
                }
            }
        }

        shapesOut.close();

        // ------------------------------------------------- snapshots and cases
        explosionRngField = field("net.minecraft.world.Explosion", "explosionRNG");
        itemHealthField = field("net.minecraft.entity.item.EntityItem", "health");
        orbHealthField = field("net.minecraft.entity.item.EntityXPOrb", "xpOrbHealth");
        itemAgeField = field("net.minecraft.entity.item.EntityItem", "age");
        orbAgeField = field("net.minecraft.entity.item.EntityXPOrb", "xpOrbAge");
        itemDelayField = field("net.minecraft.entity.item.EntityItem", "delayBeforeCanPickup");
        itemHoverField = field("net.minecraft.entity.item.EntityItem", "hoverStart");

        writeDetState(new File(dir, "start.txt"), seed);

        GZIPOutputStream casesGz = new GZIPOutputStream(new FileOutputStream(new File(dir, "cases.txt.gz")), 1 << 16);
        PrintWriter cw = new PrintWriter(new OutputStreamWriter(casesGz, "UTF-8"));

        writesBuf[0] = new StringBuilder();
        writesCount[0] = 0;

        Rows.writeListener = new Rows.WriteListener()
        {
            public void onWrite(World w, int x, int y, int z, int id, int meta)
            {
                if (!caseOpen) return;
                writesBuf[0].append(' ').append(x).append(' ').append(y).append(' ').append(z).append(' ').append(id).append(' ').append(meta);
                ++writesCount[0];
            }
        };

        Random r = new Random(opseed);
        long blocksDestroyed = 0, entitiesHit = 0, dropsSpawned = 0, firesPlaced = 0, totalAffected = 0;

        for (int ci = 0; ci < cases; ++ci)
        {
            // 1. the chooser draws
            int px = bx0 + 8 + r.nextInt(width - 16);
            int pz = bz0 + 8 + r.nextInt(width - 16);
            int py = ylo + 2 + r.nextInt(yhi - ylo - 4);
            float size = SIZES[r.nextInt(SIZES.length)];
            boolean flaming = r.nextInt(4) == 0;
            boolean smoking = r.nextInt(8) != 0;
            int nents = r.nextInt(4);
            boolean[] orb = new boolean[3];
            int[] ex = new int[3], ey = new int[3], ez = new int[3], eitem = new int[3], edamage = new int[3], ecount = new int[3], expv = new int[3];
            double[] emx = new double[3], emy = new double[3], emz = new double[3];

            for (int i = 0; i < nents; ++i)
            {
                orb[i] = r.nextInt(3) == 2;
                ex[i] = px + r.nextInt(11) - 5;
                ez[i] = pz + r.nextInt(11) - 5;
                ey[i] = py + r.nextInt(7) - 3;
                eitem[i] = ITEM_IDS[r.nextInt(ITEM_IDS.length)];
                edamage[i] = r.nextInt(16);
                ecount[i] = r.nextInt(4) + 1;
                expv[i] = r.nextInt(64) + 1;
                emx[i] = (r.nextDouble() - 0.5D) * 0.2D;
                emy[i] = r.nextDouble() * 0.1D;
                emz[i] = (r.nextDouble() - 0.5D) * 0.2D;
            }

            caseOpen = true;
            writesBuf[0].setLength(0);
            writesCount[0] = 0;

            long caseSeed = opseed + CASE_STEP * (long)ci;

            // 2. the case's entities
            List<Entity> ents = new ArrayList<Entity>();

            ws.rand.setSeed(caseSeed);

            for (int i = 0; i < nents; ++i)
            {
                Entity e;

                if (orb[i])
                {
                    e = new EntityXPOrb(ws, ex[i] + 0.5D, ey[i], ez[i] + 0.5D, expv[i]);
                }
                else
                {
                    EntityItem ei = new EntityItem(ws, ex[i] + 0.5D, ey[i], ez[i] + 0.5D);
                    ei.setEntityItemStack(new ItemStack(Item.getItemById(eitem[i]), ecount[i], edamage[i]));
                    e = ei;
                }

                e.motionX = emx[i];
                e.motionY = emy[i];
                e.motionZ = emz[i];

                if (!ws.spawnEntityInWorld(e)) throw new IllegalStateException("entity " + i + " did not spawn");
                ents.add(e);
            }

            // 3. the explosion: the body of WorldServer.newExplosion, on the
            // live object so the private rng can be read after
            long worldBefore = Det.state(ws.rand);
            net.minecraft.world.Explosion e2 = new net.minecraft.world.Explosion(ws, null, px, py, pz, size);
            e2.isFlaming = flaming;
            e2.isSmoking = smoking;

            Saver saver = new Saver();
            ws.addWorldAccess(saver);
            e2.doExplosionA();
            long worldMid = Det.state(ws.rand);
            e2.doExplosionB(false);
            ws.removeWorldAccess(saver);

            long rngAfter = Det.state((Random)explosionRngField.get(e2));
            long worldAfter = Det.state(ws.rand);
            long detMathAfter = Det.mathState(Det.OTHER);
            long detSeedAfter = Det.seederState(Det.OTHER);

            // 4. the record
            JsonArray affected = new JsonArray();

            for (Object o : (List<?>)e2.affectedBlockPositions)
            {
                ChunkPosition cp = (ChunkPosition)o;
                JsonArray a = new JsonArray();
                a.add(new JsonPrimitive(cp.field_151329_a));
                a.add(new JsonPrimitive(cp.field_151327_b));
                a.add(new JsonPrimitive(cp.field_151328_c));
                affected.add(a);
            }

            JsonArray fires = new JsonArray();
            JsonArray destroyed = new JsonArray();

            for (String wr : writesBuf[0].toString().split(" "))
            {
                // rewritten below; split done by the writer loop
                wr = wr;
                break;
            }

            // the write stream parsed back into events, in order
            String[] toks = writesBuf[0].length() == 0 ? new String[0] : writesBuf[0].toString().substring(1).split(" ");

            for (int wi = 0; wi + 4 < toks.length; wi += 5)
            {
                int wx = Integer.parseInt(toks[wi]);
                int wy = Integer.parseInt(toks[wi + 1]);
                int wz = Integer.parseInt(toks[wi + 2]);
                int wid = Integer.parseInt(toks[wi + 3]);

                if (wid == 0)
                {
                    JsonArray a = new JsonArray();
                    a.add(new JsonPrimitive(wx));
                    a.add(new JsonPrimitive(wy));
                    a.add(new JsonPrimitive(wz));
                    destroyed.add(a);
                    ++blocksDestroyed;
                }
                else if (wid == 51)
                {
                    JsonArray a = new JsonArray();
                    a.add(new JsonPrimitive(wx));
                    a.add(new JsonPrimitive(wy));
                    a.add(new JsonPrimitive(wz));
                    fires.add(a);
                    ++firesPlaced;
                }
            }

            JsonArray spawnedArr = new JsonArray();

            for (Entity se : saver.spawned)
            {
                JsonObject o = new JsonObject();
                o.addProperty("kind", se instanceof EntityXPOrb ? 2 : 1);
                o.addProperty("id", se.getEntityId());

                if (se instanceof EntityItem)
                {
                    ItemStack st = ((EntityItem)se).getEntityItem();
                    o.addProperty("item", Item.getIdFromItem(st.getItem()));
                    o.addProperty("damage", st.getItemDamage());
                    o.addProperty("count", st.stackSize);
                    o.addProperty("delay", itemDelayField.getInt(se));
                    o.addProperty("hover", Float.floatToRawIntBits(itemHoverField.getFloat(se)));
                }
                else
                {
                    o.addProperty("xp", ((EntityXPOrb)se).getXpValue());
                }

                doubles(o, se);
                spawnedArr.add(o);
            }

            dropsSpawned += spawnedArr.size();

            JsonArray afterArr = new JsonArray();
            JsonArray metaArr = new JsonArray();

            for (int i = 0; i < ents.size(); ++i)
            {
                Entity se = ents.get(i);
                JsonObject o = new JsonObject();
                o.addProperty("id", se.getEntityId());
                o.addProperty("health", se instanceof EntityXPOrb ? orbHealthField.getInt(se) : itemHealthField.getInt(se));
                o.addProperty("dead", se.isDead ? 1 : 0);
                o.addProperty("age", se instanceof EntityXPOrb ? orbAgeField.getInt(se) : itemAgeField.getInt(se));
                doubles(o, se);
                afterArr.add(o);
                ++entitiesHit;

                JsonArray b = new JsonArray();
                b.add(new JsonPrimitive(orb[i] ? 2 : 1));
                b.add(new JsonPrimitive(orb[i] ? 0 : eitem[i]));
                b.add(new JsonPrimitive(orb[i] ? 0 : edamage[i]));
                b.add(new JsonPrimitive(orb[i] ? 0 : ecount[i]));
                b.add(new JsonPrimitive(orb[i] ? expv[i] : 0));
                metaArr.add(b);
            }

            JsonObject line = new JsonObject();
            line.addProperty("case", ci);
            line.addProperty("seed", caseSeed);
            line.addProperty("x", px);
            line.addProperty("y", py);
            line.addProperty("z", pz);
            line.addProperty("size", Float.floatToRawIntBits(size));
            line.addProperty("flaming", flaming ? 1 : 0);
            line.addProperty("smoking", smoking ? 1 : 0);
            line.addProperty("nents", nents);
            line.addProperty("world_before", worldBefore);
            line.addProperty("world_mid", worldMid);
            line.addProperty("rng_after", rngAfter);
            line.addProperty("world_after", worldAfter);
            line.addProperty("det_math", detMathAfter);
            line.addProperty("det_seed", detSeedAfter);
            line.addProperty("hash", hashRegion(ws, bx0, bz0, width, ylo, yhi));
            line.addProperty("writesn", writesCount[0]);
            line.addProperty("writes", writesBuf[0].length() == 0 ? "" : writesBuf[0].toString().substring(1));
            line.add("affected", affected);
            line.add("destroyed", destroyed);
            line.add("fires", fires);
            line.add("spawned", spawnedArr);
            line.add("after", afterArr);
            line.add("entmeta", metaArr);
            cw.println(line.toString());
            totalAffected += affected.size();

            // 5. take everything out
            for (Entity te : ents) takeOut(ws, te);

            for (Entity te : saver.spawned) takeOut(ws, te);

            caseOpen = false;
        }

        cw.close();

        Rows.writeListener = null;
        writeDetState(new File(dir, "end.txt"), seed);

        JsonObject m = new JsonObject();
        m.addProperty("area", "explosions");
        m.addProperty("seed", seed);
        m.addProperty("cx", cx);
        m.addProperty("cz", cz);
        m.addProperty("radius", radius);
        m.addProperty("ring", ring);
        m.addProperty("cases", cases);
        m.addProperty("opseed", opseed);
        m.addProperty("fill_seed", FILL_SEED);
        m.addProperty("case_step", CASE_STEP);
        m.addProperty("ylo", ylo);
        m.addProperty("yhi", yhi);
        m.addProperty("nshapes", nshapes);
        m.addProperty("total_affected", totalAffected);
        m.addProperty("blocks_destroyed", blocksDestroyed);
        m.addProperty("entities_hit", entitiesHit);
        m.addProperty("drops_spawned", dropsSpawned);
        m.addProperty("fires_placed", firesPlaced);
        m.add("loaded", loaded);
        m.addProperty("scatter", join(SCATTER));
        m.addProperty("item_ids", join(ITEM_IDS));
        m.addProperty("sizes", sizesText());
        m.addProperty("shapesFile", "shapes.bin: 16 bytes per placed block: x,y,z int32 LE, id uint16 LE, meta uint8, pad uint8. The scatter Random (fill_seed) is separate from the case chooser; per cell y in ylo..yhi the id draw is nextInt(26) on the scatter table, then the meta draw (wool 0..15, planks 0..5, log 0..3), and a torch draw is skipped when the column already has one or the block below is not opaque and renderAsNormalBlock");
        m.addProperty("casesFile", "cases.txt.gz, one JSON object per case: case, seed (the world Random's seed, opseed + case_step * case), x, y, z (the center), size (raw float bits), flaming, smoking, nents, world_before (Det.state(world.rand) right after the seed, hex), world_mid (Det.state(world.rand) right after doExplosionA, hex), rng_after (the explosion's private Det.newRandom state after the run, hex), world_after, det_math and det_seed (Det's OTHER states after the case, hex), hash (FNV-1a over the region: ids then metas, y in ylo..yhi, x-major then z, offsets relative to bx0/bz0), writesn (the Rows write count), writes (the full Rows write stream, whitespace x y z id meta, meta writes carrying id -1), affected (the positions in doExplosionB's order, i.e. the HashSet iteration order), destroyed (the writes to id 0, in order), fires (the writes to id 51, in order), spawned (entities the explosion made, kind 1 item with item/damage/count/delay/hover or 2 orb with xp, plus the entity id and doubles), after (each case entity's id, health, dead, age and doubles), entmeta (each case entity's kind/item/damage/count/xp). The chooser's draw order: px, pz, py, size pick, flaming, smoking, then per entity: kind, ex, ez, ey, item, damage, count, xp, emx, emy, emz");
        m.addProperty("start_end", "start.txt and end.txt, the DetProbe snapshot format (resetSeed, worldSeed, nextId, digest per role, split per registered stream), before the scatter and after the last case");

        writeManifest(dir, m);
        return m;
    }

    static void writeManifest(File dir, JsonObject m) throws java.io.IOException
    {
        PrintWriter w = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "manifest.json")), "UTF-8"));
        w.print(m.toString());
        w.close();
    }

    static void doubles(JsonObject o, Entity e)
    {
        o.addProperty("x", Double.doubleToRawLongBits(e.posX));
        o.addProperty("y", Double.doubleToRawLongBits(e.posY));
        o.addProperty("z", Double.doubleToRawLongBits(e.posZ));
        o.addProperty("mx", Double.doubleToRawLongBits(e.motionX));
        o.addProperty("my", Double.doubleToRawLongBits(e.motionY));
        o.addProperty("mz", Double.doubleToRawLongBits(e.motionZ));
    }

    static String sizesText()
    {
        StringBuilder b = new StringBuilder();

        for (int i = 0; i < SIZES.length; ++i) b.append(i == 0 ? "" : ",").append(Float.floatToRawIntBits(SIZES[i]));

        return b.toString();
    }

    static String join(int[] a)
    {
        StringBuilder b = new StringBuilder();

        for (int v : a) b.append(b.length() == 0 ? "" : ",").append(v);

        return b.toString();
    }

    static boolean solidAt(WorldServer ws, int x, int y, int z)
    {
        Block b = ws.getBlock(x, y, z);
        return b.getMaterial().isOpaque() && b.renderAsNormalBlock();
    }

    /** FNV-1a over the region's blocks: ids for every cell y in ylo..yhi,
     * x-major then z (offsets from bx0, bz0), then the metas the same order. */
    static long hashRegion(WorldServer ws, int bx0, int bz0, int width, int ylo, int yhi)
    {
        long h = FNV_OFFSET;

        for (int x = 0; x < width; ++x)
        {
            for (int z = 0; z < width; ++z)
            {
                for (int y = ylo; y <= yhi; ++y)
                {
                    h = (h ^ Block.getIdFromBlock(ws.getBlock(bx0 + x, y, bz0 + z))) * FNV_PRIME;
                }
            }
        }

        for (int x = 0; x < width; ++x)
        {
            for (int z = 0; z < width; ++z)
            {
                for (int y = ylo; y <= yhi; ++y)
                {
                    h = (h ^ ws.getBlockMetadata(bx0 + x, y, bz0 + z)) * FNV_PRIME;
                }
            }
        }

        return h;
    }

    static void le32(byte[] b, int p, int v)
    {
        b[p] = (byte)v;
        b[p + 1] = (byte)(v >> 8);
        b[p + 2] = (byte)(v >> 16);
        b[p + 3] = (byte)(v >> 24);
    }

    static Field field(String cls, String name) throws Exception
    {
        Class<?> c = Class.forName(cls);
        Field f = c.getDeclaredField(name);
        f.setAccessible(true);
        return f;
    }

    /** Takes an entity out of the world completely: the chunk's list and the
     * loaded list both lose it now (World.removeEntity only marks it dead and
     * leaves the sweep to the tick; nothing ticks while the server is
     * parked). */
    static void takeOut(WorldServer ws, Entity e)
    {
        e.setDead();

        if (e.addedToChunk)
        {
            net.minecraft.world.chunk.Chunk c = ws.getChunkFromChunkCoords(e.chunkCoordX, e.chunkCoordZ);

            if (c != null) c.removeEntity(e);
        }

        ws.loadedEntityList.remove(e);
    }

    /** Keeps every entity the case's explosion spawns, in spawn order,
     * straight from the world's own onEntityCreate fan-out. Added for the two
     * calls and removed right after, like DropsProbe's Capture. */
    static final class Saver implements net.minecraft.world.IWorldAccess
    {
        final List<Entity> spawned = new ArrayList<Entity>();

        Saver() {}

        public void onEntityCreate(Entity e) { spawned.add(e); }
        public void onEntityDestroy(Entity e) {}
        public void markBlockForUpdate(int x, int y, int z) {}
        public void markBlockForRenderUpdate(int x, int y, int z) {}
        public void markBlockRangeForRenderUpdate(int x0, int y0, int z0, int x1, int y1, int z1) {}
        public void playSound(String s, double x, double y, double z, float v, float p) {}
        public void playSoundToNearExcept(net.minecraft.entity.player.EntityPlayer p, String s, double x, double y, double z, float v, float q) {}
        public void spawnParticle(String s, double x, double y, double z, double a, double b, double c) {}
        public void playRecord(String s, int x, int y, int z) {}
        public void broadcastSound(int a, int b, int c, int d, int e2) {}
        public void playAuxSFX(net.minecraft.entity.player.EntityPlayer p, int a, int x, int y, int z, int v) {}
        public void destroyBlockPartially(int a, int x, int y, int z, int v) {}
        public void onStaticEntitiesChanged() {}
    }

    static void writeDetState(File f, long seed) throws Exception
    {
        PrintWriter w = new PrintWriter(new OutputStreamWriter(new FileOutputStream(f), "UTF-8"));
        w.println("resetSeed " + seed);
        w.println("worldSeed " + Det.worldSeed);
        StringBuilder b = new StringBuilder("nextId");

        for (int role = 0; role < Det.ROLES; ++role) b.append(' ').append(Det.nextId[role]);

        w.println(b.toString());

        for (int role = 0; role < Det.ROLES; ++role)
        {
            w.println("digest " + role + " " + hex(Det.seederState(role)) + " " + hex(Det.mathState(role)) + " " + hex(Det.splitState(role)));
        }

        synchronized (Det.class)
        {
            for (Det.SplitRandom s : Det.splits)
            {
                b.setLength(0);
                b.append("split ").append(s.name);

                for (int role = 0; role < Det.ROLES; ++role) b.append(' ').append(hex(Det.state(s.d[role])));

                for (int role = 0; role < Det.ROLES; ++role) b.append(' ').append(s.used[role] ? 1 : 0);

                w.println(b.toString());
            }
        }

        w.close();
    }

    static String hex(long v)
    {
        return String.format("%016x", Long.valueOf(v));
    }
}
