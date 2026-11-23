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
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Random;
import java.util.zip.GZIPOutputStream;
import net.minecraft.block.BlockFalling;
import net.minecraft.entity.Entity;
import net.minecraft.entity.item.EntityItem;
import net.minecraft.entity.item.EntityMinecartChest;
import net.minecraft.entity.monster.EntityWitch;
import net.minecraft.entity.passive.EntityVillager;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.nbt.NBTTagList;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.tileentity.TileEntityMobSpawner;
import net.minecraft.world.World;
import net.minecraft.world.WorldServer;
import net.minecraft.world.chunk.Chunk;
import net.minecraft.world.gen.structure.MapGenMineshaft;
import net.minecraft.world.gen.structure.MapGenStructure;
import net.minecraft.world.gen.structure.MapGenStructureData;
import net.minecraft.world.gen.structure.StructureBoundingBox;
import net.minecraft.world.gen.structure.StructureStart;

/**
 * Structure block probe: how one mineshaft start's pieces place their blocks,
 * the reference for csrc/engine/structure_blocks.c and mineshaft_blocks.c.
 *
 * A region of raw chunks is loaded (no populate, so the terrain is bare and
 * every block write is the structures'), a fresh MapGenMineshaft with private
 * structure data picks the starts the game would (StructuresProbe's walk), and
 * every start that lies wholly inside the region is generated the way
 * population generates it: for each chunk its bounding box spans, in cx-major
 * order, the population Random seeded exactly as ChunkProviderGenerate
 * .populate seeds it for that chunk, then start.generateStructure(ws, rand,
 * the chunk's 16x16 box at +8). One start object is carried across all its
 * chunks, so a component that addComponentParts drops stays dropped and the
 * pieces' own state carries, as in a real run.
 *
 * Per chunk step the probe records: every write in Rows.onBlock order, the
 * tile entities the region holds afterwards (FeatureProbeDungeons' delta, the
 * mob spawners), the entities the step constructed, and the setblock probe's
 * 3x3 hash. A chest minecart is built and filled but never spawned while
 * config.yaml keeps minecarts off; the probe flips WorldConf.minecarts on for
 * its own run so the built carts show up in the world's entity list with their
 * loot, which moves no recorded value (the writes, the tile entities and the
 * hashes are the same both ways), and flips it off again.
 *
 * The OTHER role's Det streams (the probe thread is an OTHER thread) and
 * WorldServer.rand are snapshotted into the manifest before the first step, so
 * the native side can replay every draw the generation makes.
 *
 * Runs on its own thread (the OTHER role) while the server is parked, so no
 * CLIENT or SERVER RNG stream moves.
 *
 * Output DIR/manifest.json, DIR/steps.bin, DIR/writes.bin,
 * DIR/tileentities.bin, DIR/entities.bin, DIR/final.bin.gz. See the layout
 * strings in the manifest for the byte layouts.
 */
final class StructureBlocksProbe
{
    private StructureBlocksProbe() {}

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
        }, "Oracle Structure Blocks Probe");
        t.start();
        t.join();
        if (error[0] != null) throw error[0];
        return result[0];
    }

    static JsonObject dump(IntegratedServer server, JsonObject cmd) throws Exception
    {
        int x0 = cmd.get("x0").getAsInt(), z0 = cmd.get("z0").getAsInt();
        int x1 = cmd.get("x1").getAsInt(), z1 = cmd.get("z1").getAsInt();
        int count = cmd.has("count") ? cmd.get("count").getAsInt() : 64;
        File dir = new File(cmd.get("out").getAsString());
        dir.mkdirs();

        // which structure and which world: "structure" is the native type's
        // name (structure.c), "dim" the dimension; the defaults record what
        // this probe recorded before it took either
        String structure = cmd.has("structure") ? cmd.get("structure").getAsString() : "Mineshaft";
        int dim = cmd.has("dim") ? cmd.get("dim").getAsInt() : 0;
        WorldServer ws = server.worldServerForDimension(dim);
        long seed = ws.getSeed();
        Probe.rawChunks = true;

        // a fresh generator with private structure data, offered the region the
        // way the game discovers starts: every chunk of the region is offered,
        // and each candidate's RNG is seeded from its own coordinates
        MapGenStructure mineshaft = generator(structure);
        Field dataField = field(MapGenStructure.class, "field_143029_e");
        dataField.set(mineshaft, new net.minecraft.world.gen.structure.MapGenStructureData(mineshaft.func_143025_a()));

        for (int cx = x0; cx <= x1; ++cx)
        {
            for (int cz = z0; cz <= z1; ++cz)
            {
                mineshaft.func_151539_a(ws.getChunkProvider(), ws, cx, cz, null);
            }
        }

        java.util.Map map = (java.util.Map)field(MapGenStructure.class, "structureMap").get(mineshaft);
        List<Pick> all = new ArrayList<Pick>();

        for (Object o : map.entrySet())
        {
            java.util.Map.Entry entry = (java.util.Map.Entry)o;
            long key = ((Long)entry.getKey()).longValue();
            int cx = (int)key, cz = (int)(key >> 32);
            StructureStart start = (StructureStart)entry.getValue();
            StructureBoundingBox bb = start.getBoundingBox();
            // a populated chunk's +8 box spans [c*16+8, c*16+23]; the chunks
            // whose box meets the start's are the ones that generate it
            int cMinX = (bb.minX - 8) >> 4, cMaxX = (bb.maxX - 8) >> 4;
            int cMinZ = (bb.minZ - 8) >> 4, cMaxZ = (bb.maxZ - 8) >> 4;

            if (cMinX >= x0 && cMaxX <= x1 && cMinZ >= z0 && cMaxZ <= z1)
                all.add(new Pick(start, cx, cz, cMinX, cMaxX, cMinZ, cMaxZ));
        }

        Collections.sort(all);
        List<Pick> picks = all.subList(0, Math.min(count, all.size()));

        // the region plus one chunk of margin in every direction: a step's +8
        // box and its 3x3 hash reach one chunk past the start's own set
        JsonArray loaded = new JsonArray();

        for (int lx = x0 - 1; lx <= x1 + 1; ++lx)
        {
            for (int lz = z0 - 1; lz <= z1 + 1; ++lz)
            {
                if (ws.getChunkFromChunkCoords(lx, lz) == null) throw new IllegalStateException("chunk (" + lx + "," + lz + ") did not load");
                JsonArray pair = new JsonArray();
                pair.add(new JsonPrimitive(lx));
                pair.add(new JsonPrimitive(lz));
                loaded.add(pair);
            }
        }

        OutputStream writesOut = new BufferedOutputStream(new FileOutputStream(new File(dir, "writes.bin")), 1 << 16);
        OutputStream stepsOut = new BufferedOutputStream(new FileOutputStream(new File(dir, "steps.bin")), 1 << 16);
        OutputStream entsOut = new BufferedOutputStream(new FileOutputStream(new File(dir, "entities.bin")), 1 << 16);
        FeatureProbeDungeons.Tiles tiles = new FeatureProbeDungeons.Tiles(dir);

        // Det's OTHER role streams and World.rand at the moment generation
        // starts; everything after is the generation's own
        JsonObject det = new JsonObject();
        det.addProperty("seeder_other", Det.seederState(Det.OTHER));
        det.addProperty("math_other", Det.mathState(Det.OTHER));
        det.addProperty("next_id_other", Det.nextId[Det.OTHER]);
        det.addProperty("worldrand", Det.state(ws.rand));
        det.addProperty("note", "the probe thread is the OTHER role; WorldServer.rand was seeded when the server built the world");

        byte[] buf = new byte[Probe.CHUNK_BYTES];
        int totalWrites = 0, webs = 0, rails = 0, steps = 0;
        long prevEnts = (long)ws.loadedEntityList.size();

        probeWrites = writesOut;
        Rows.writeListener = new Rows.WriteListener()
        {
            public void onWrite(World w, int x, int y, int z, int id, int meta)
            {
                StructureBlocksProbe.onWrite(x, y, z, id, meta);
            }
        };

        // config.yaml keeps minecarts off: a corridor's chest cart is built and
        // filled but never spawned. The flag is on while this run generates so
        // the probe can observe the built carts and their loot through the
        // world's entity list; it touches no recorded value (the block writes,
        // the tile entities and the hashes are the same both ways).
        WorldConf.minecarts = true;
        int carts = 0, items = 0;

        try
        {
            for (Pick p : picks)
            {
                for (int cx = p.cMinX; cx <= p.cMaxX; ++cx)
                {
                    for (int cz = p.cMinZ; cz <= p.cMaxZ; ++cz)
                    {
                        Random pr = new Random(0);
                        pr.setSeed(seed);
                        long var7 = pr.nextLong() / 2L * 2L + 1L;
                        long var9 = pr.nextLong() / 2L * 2L + 1L;
                        pr.setSeed((long)cx * var7 + (long)cz * var9 ^ seed);

                        StructureBoundingBox box = new StructureBoundingBox((cx << 4) + 8, (cz << 4) + 8, (cx << 4) + 23, (cz << 4) + 23);
                        BlockFalling.field_149832_M = true;
                        stepWrites = 0;
                        stepWebs = 0;
                        stepRails = 0;
                        p.start.generateStructure(ws, pr, box);
                        BlockFalling.field_149832_M = false;

                        long hash = Probe.hashAround(ws, cx, cz, buf);
                        tiles.record(ws, x0 - 1, x1 + 1, z0 - 1, z1 + 1);

                        webs += stepWebs;
                        rails += stepRails;
                        totalWrites += stepWrites;

                        byte[] sb = new byte[32];
                        le32(sb, 0, steps);
                        le32(sb, 4, p.cx);
                        le32(sb, 8, p.cz);
                        le32(sb, 12, cx);
                        le32(sb, 16, cz);
                        le32(sb, 20, stepWrites);
                        le64(sb, 24, hash);
                        stepsOut.write(sb);
                        ++steps;

                        recordEntities(entsOut, ws, prevEnts);
                        prevEnts = (long)ws.loadedEntityList.size();
                    }
                }
            }
        }
        finally
        {
            WorldConf.minecarts = false;
            Rows.writeListener = null;
        }

        writesOut.close();
        stepsOut.close();
        entsOut.close();
        tiles.close();

        // the region's final tile entities, for the report (the per-step
        // record is the delta the native side checks)
        int spawners = 0;

        for (int lx = x0 - 1; lx <= x1 + 1; ++lx)
        {
            for (int lz = z0 - 1; lz <= z1 + 1; ++lz)
            {
                Chunk c = ws.getChunkFromChunkCoords(lx, lz);

                for (Object o : c.chunkTileEntityMap.values())
                {
                    if (o instanceof TileEntityMobSpawner) ++spawners;
                }
            }
        }

        DataOutputStream out = new DataOutputStream(new GZIPOutputStream(new FileOutputStream(new File(dir, "final.bin.gz")), 1 << 16));
        byte[] cols = new byte[256];
        byte[] fbuf = new byte[Probe.CHUNK_BYTES];

        for (int ax = x0 - 1; ax <= x1 + 1; ++ax)
        {
            for (int az = z0 - 1; az <= z1 + 1; ++az)
            {
                Chunk c = ws.getChunkFromChunkCoords(ax, az);
                Probe.fillChunkBytes(c, fbuf);
                writeLe32(out, ax);
                writeLe32(out, az);
                out.write(fbuf);

                for (int i = 0; i < 256; ++i) cols[i] = (byte)(c.updateSkylightColumns[i] ? 1 : 0);
                out.write(cols);
                out.write(field(Chunk.class, "isGapLightingUpdated").getBoolean(c) ? 1 : 0);
            }
        }

        out.close();

        JsonObject m = new JsonObject();
        m.addProperty("seed", seed);
        m.addProperty("kind", "sblocks");
        if (cmd.has("structure")) m.addProperty("structure", structure);
        if (cmd.has("dim")) m.addProperty("dim", dim);
        m.addProperty("x0", x0);
        m.addProperty("z0", z0);
        m.addProperty("x1", x1);
        m.addProperty("z1", z1);
        m.addProperty("count", count);
        JsonArray st = new JsonArray();

        for (Pick p : picks)
        {
            JsonArray a = new JsonArray();
            a.add(new JsonPrimitive(p.cx));
            a.add(new JsonPrimitive(p.cz));
            st.add(a);
        }

        m.add("starts", st);
        m.add("loaded", loaded);
        m.add("det", det);
        m.addProperty("steps", steps);
        m.addProperty("writes", totalWrites);
        m.addProperty("webs", webs);
        m.addProperty("rails", rails);
        m.addProperty("spawners", spawners);
        m.addProperty("carts", countCarts);
        m.addProperty("items", countItems);
        m.addProperty("witches", countWitches);
        m.addProperty("villagers", countVillagers);
        m.addProperty("order", "starts by chunk cx then cz; per start, its chunk set cx-major with cz inner; per step "
            + "the population Random is seeded as ChunkProviderGenerate.populate seeds it, then "
            + "start.generateStructure(ws, rand, the chunk's 16x16 box at +8)");
        m.addProperty("chunk_rule", "a start's chunks: c from (minX - 8) >> 4 to (maxX - 8) >> 4 and z the same, "
            + "the chunks whose +8 16x16 box meets the start's bounding box; the box at +8 spans two chunks in x and "
            + "z, so every write of a step lands in the step chunk or its x/z neighbour");
        m.addProperty("seed_draws", "rand.setSeed(seed); nextLong() / 2 * 2 + 1 twice; setSeed(cx * a + cz * b ^ seed); "
            + "that is the rand state populate reaches when its first generator, the mineshaft one, runs");
        m.addProperty("step_layout", "32 bytes per step: step index uint32, start chunk cx int32, start chunk cz int32, "
            + "step chunk cx int32, step chunk cz int32, number of writes uint32 LE, FNV-1a 64 hash uint64 LE");
        m.addProperty("write_layout", "16 bytes per write, every step's writes in order: x int32 LE, y int32 LE, z int32 "
            + "LE, id uint16 LE (0xffff for a metadata-only write), meta uint8, pad uint8");
        m.addProperty("tile_entities", "tileentities.bin");
        m.addProperty("tile_entity_layout", FeatureProbeDungeons.LAYOUT);
        m.addProperty("tile_entity_note", FeatureProbeDungeons.NOTE + "; the recorded region is the loaded box, and a "
            + "step's entries are what that step left different from the step before (a mineshaft leaves mob "
            + "spawners only)");
        m.addProperty("entity_layout", "per step: count uint32 LE, then per entity: kind uint8 (1 chest minecart, 2 item "
            + "drop, 3 villager, 4 witch), entity id uint32 LE, UUID most and least int64 LE; a chest minecart adds posX, posY, "
            + "posZ double LE and len uint32 LE + len bytes of the canonical NBT of its Items list; an item drop adds "
            + "posX, posY, posZ double LE, rotationYaw and hoverStart as raw float bits, motionX and motionZ double LE, "
            + "item uint16 LE, damage uint16 LE, count uint32 LE; a villager adds posX, posY, posZ double LE and "
            + "profession uint32 LE; a witch (the witch hut's) adds posX, posY, posZ double LE");
        m.addProperty("entities_note", "the entities the world holds after each step, diffed against the step before, in "
            + "spawn order (loadedEntityList is append-only and nothing removes an entity while the probe runs)");
        m.addProperty("minecarts", "world.conf keeps minecarts off, so a corridor's chest cart is built and filled but "
            + "never spawned; the probe flips WorldConf.minecarts on while generating to observe the built carts and "
            + "their loot through the world's entity list. The construction and its Det draws (entity id, entity rand, "
            + "UUID) are the same both ways, and no block, tile entity or hash can see the spawn");
        m.addProperty("det_note", "the OTHER role's seeder, math stream and entity-id counter, and WorldServer.rand, "
            + "snapshotted before the first step; the generation touches them only when it constructs an entity "
            + "(Det.nextEntityId, Det.newRandom, Det.uuid, an EntityItem's four Det.mathRandom draws) and, for an item "
            + "drop, World.rand (Block.dropBlockAsItemWithChance and dropBlockAsItem_do)");
        m.addProperty("hash", "the setblock probe's: FNV-1a 64 (offset 0xcbf29ce484222325, prime 0x100000001b3) over the "
            + "3x3 chunks around the step's chunk, dx outer -1..1, dz inner -1..1; a chunk that is not loaded "
            + "contributes one 0 byte");
        m.addProperty("chunk_bytes", "the setblock probe's: ids uint16 LE (65536, index x << 12 | z << 8 | y), metas "
            + "uint8 (65536), sky light uint8 (65536), block light uint8 (65536), heightMap 256 int32 LE, "
            + "precipitationHeightMap 256 int32 LE (both Java index order z << 4 | x), heightMapMinimum int32 LE, "
            + "section mask uint16 LE");
        m.addProperty("final_layout", "final.bin.gz, per loaded chunk in load order: cx int32 LE, cz int32 LE, the chunk "
            + "bytes above, updateSkylightColumns 256 bytes (0/1), isGapLightingUpdated 1 byte");
        PrintWriter w = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "manifest.json")), "UTF-8"));
        w.println(m.toString());
        w.close();

        JsonObject r = new JsonObject();
        r.addProperty("dir", dir.getPath());
        r.addProperty("chunks", loaded.size());
        r.addProperty("starts", picks.size());
        r.addProperty("steps", steps);
        r.addProperty("writes", totalWrites);
        r.addProperty("webs", webs);
        r.addProperty("rails", rails);
        r.addProperty("spawners", spawners);
        r.addProperty("carts", countCarts);
        r.addProperty("items", countItems);
        r.addProperty("witches", countWitches);
        r.addProperty("villagers", countVillagers);
        return r;
    }

    // ------------------------------------------------------------ recording

    static OutputStream probeWrites;
    static int stepWrites, stepWebs, stepRails;
    static int countCarts, countItems, countVillagers, countWitches;

    /** One write through Rows.onBlock, in call order. */
    static void onWrite(int x, int y, int z, int id, int meta)
    {
        byte[] b = new byte[12];
        le32(b, 0, x);
        le32(b, 4, y);
        le32(b, 8, z);
        int iid = id & 65535;

        try
        {
            probeWrites.write(b);
            probeWrites.write(new byte[] {(byte)id, (byte)(id >> 8), (byte)meta, 0});
        }
        catch (IOException e)
        {
            throw new RuntimeException("write " + stepWrites, e);
        }

        if (iid == 30) ++stepWebs;
        else if (iid == 66) ++stepRails;
        ++stepWrites;
    }

    /** The entities the world holds now, diffed against the step before: this
     * step's spawns, in spawn order. loadedEntityList is append-only and
     * nothing removes an entity while the probe runs. */
    static void recordEntities(OutputStream out, WorldServer ws, long prev) throws IOException
    {
        int n = (int)(ws.loadedEntityList.size() - prev);
        byte[] head = new byte[4];
        le32(head, 0, n);
        out.write(head);

        for (int i = (int)prev; i < ws.loadedEntityList.size(); ++i)
        {
            Entity en = (Entity)ws.loadedEntityList.get(i);
            byte[] b = new byte[25];
            b[4] = (byte)(en instanceof EntityMinecartChest ? 1 : en instanceof EntityVillager ? 3
                : en instanceof EntityWitch ? 4 : 2);
            le32(b, 5, en.getEntityId());
            le64(b, 9, en.getUniqueID().getMostSignificantBits());
            le64(b, 17, en.getUniqueID().getLeastSignificantBits());
            out.write(b);

            if (en instanceof EntityWitch)
            {
                /* the witch hut's occupant: the entity constructor's Det draws
                 * (id, rand, uuid) and setLocationAndAngles are all of it a
                 * native run has to reproduce */
                ++countWitches;
                writeDouble(out, en.posX);
                writeDouble(out, en.posY);
                writeDouble(out, en.posZ);
            }
            else if (en instanceof EntityMinecartChest)
            {
                ++countCarts;
                writeDouble(out, en.posX);
                writeDouble(out, en.posY);
                writeDouble(out, en.posZ);
                NBTTagList list = new NBTTagList();

                for (int s = 0; s < 36; ++s)
                {
                    ItemStack stack = ((EntityMinecartChest)en).getStackInSlot(s);

                    if (stack != null)
                    {
                        NBTTagCompound t = new NBTTagCompound();
                        t.setByte("Slot", (byte)s);
                        stack.writeToNBT(t);
                        list.appendTag(t);
                    }
                }

                NBTTagCompound root = new NBTTagCompound();
                root.setTag("Items", list);
                writeText(out, StructuresProbe.canon(root).toString());
            }
            else if (en instanceof EntityVillager)
            {
                ++countVillagers;
                writeDouble(out, en.posX);
                writeDouble(out, en.posY);
                writeDouble(out, en.posZ);
                byte[] prof = new byte[4];
                le32(prof, 0, ((EntityVillager)en).getProfession());
                out.write(prof);
            }
            else
            {
                EntityItem it = (EntityItem)en;
                writeDouble(out, en.posX);
                writeDouble(out, en.posY);
                writeDouble(out, en.posZ);
                writeFloat(out, en.rotationYaw);
                writeFloat(out, it.hoverStart);
                writeDouble(out, en.motionX);
                writeDouble(out, en.motionZ);
                byte[] s = new byte[12];
                le32(s, 0, Item.getIdFromItem(it.getEntityItem().getItem()));
                le32(s, 4, it.getEntityItem().getItemDamage());
                le32(s, 8, it.getEntityItem().stackSize);
                out.write(s);
                ++countItems;
            }
        }
    }

    /** A fresh generator of the named type, as the chunk provider builds it. */
    static MapGenStructure generator(String name)
    {
        if (name.equals("Mineshaft")) return new MapGenMineshaft();
        if (name.equals("Stronghold")) return new net.minecraft.world.gen.structure.MapGenStronghold();
        if (name.equals("Temple")) return new net.minecraft.world.gen.structure.MapGenScatteredFeature();
        if (name.equals("Village")) return new net.minecraft.world.gen.structure.MapGenVillage();
        if (name.equals("Fortress")) return new net.minecraft.world.gen.structure.MapGenNetherBridge();
        throw new IllegalArgumentException("oracle: no structure type " + name);
    }

    /** One mineshaft start to generate, with its chunk set. */
    private static final class Pick implements Comparable<Pick>
    {
        final StructureStart start;
        final int cx, cz, cMinX, cMaxX, cMinZ, cMaxZ;

        Pick(StructureStart start, int cx, int cz, int cMinX, int cMaxX, int cMinZ, int cMaxZ)
        {
            this.start = start;
            this.cx = cx;
            this.cz = cz;
            this.cMinX = cMinX;
            this.cMaxX = cMaxX;
            this.cMinZ = cMinZ;
            this.cMaxZ = cMaxZ;
        }

        public int compareTo(Pick o)
        {
            if (this.cx != o.cx) return this.cx < o.cx ? -1 : 1;
            if (this.cz != o.cz) return this.cz < o.cz ? -1 : 1;
            return 0;
        }
    }

    private static void writeDouble(OutputStream o, double v) throws IOException
    {
        long bits = Double.doubleToRawLongBits(v);

        for (int i = 0; i < 8; ++i) o.write((int)(bits >> (8 * i)) & 255);
    }

    private static void writeFloat(OutputStream o, float v) throws IOException
    {
        int bits = Float.floatToRawIntBits(v);

        for (int i = 0; i < 4; ++i) o.write(bits >> (8 * i) & 255);
    }

    private static void writeText(OutputStream o, String text) throws IOException
    {
        byte[] b = text.getBytes("UTF-8");
        byte[] head = new byte[4];
        le32(head, 0, b.length);
        o.write(head);
        o.write(b);
    }

    private static void le32(byte[] a, int o, int v)
    {
        a[o] = (byte)v;
        a[o + 1] = (byte)(v >> 8);
        a[o + 2] = (byte)(v >> 16);
        a[o + 3] = (byte)(v >> 24);
    }

    private static void le64(byte[] a, int o, long v)
    {
        for (int i = 0; i < 8; ++i) a[o + i] = (byte)(v >> (8 * i));
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