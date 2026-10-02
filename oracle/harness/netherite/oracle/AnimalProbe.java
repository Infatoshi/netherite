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
import java.lang.reflect.Array;
import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.IdentityHashMap;
import java.util.List;
import java.util.Map;
import java.util.Random;
import java.util.zip.GZIPOutputStream;
import net.minecraft.block.Block;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityAgeable;
import net.minecraft.entity.EntityLiving;
import net.minecraft.entity.EntityLivingBase;
import net.minecraft.entity.EntityList;
import net.minecraft.entity.ai.EntityAITasks;
import net.minecraft.entity.ai.EntityJumpHelper;
import net.minecraft.entity.ai.EntityLookHelper;
import net.minecraft.entity.ai.EntityMoveHelper;
import net.minecraft.entity.passive.EntityChicken;
import net.minecraft.entity.passive.EntityPig;
import net.minecraft.entity.passive.EntitySheep;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.item.ItemStack;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.util.MathHelper;
import net.minecraft.world.WorldServer;

/**
 * The animal probe: the living entities (pig, cow, mooshroom, chicken, sheep)
 * ticking in a flattened raw region, the reference for the native port of
 * EntityLivingBase, EntityLiving, EntityAgeable, EntityAnimal, the farm animals
 * and the entity AI (EntityAITasks, the look/move/jump/senses helpers,
 * PathNavigate, RandomPositionGenerator).
 *
 * One case per run: a raw region (Probe.rawChunks, population off) chosen for a
 * flat, dry 5x5-chunk area, flattened to one grass level with a dirt body, with
 * a water pool, a lava pit, a cactus row, fences around the border and pits.
 * Then the animals are spawned the way SpawnerAnimals does it: a fresh instance
 * by name (EntityList.createEntityByName), setLocationAndAngles, the probe's
 * overrides, onSpawnWithEgg(null), world.spawnEntityInWorld. Some are babies,
 * some are in love next to each other so they breed, some start high so they
 * fall, some are next to the lava, the cactus and the pool.
 *
 * The tick is the World.updateEntities entity pass over the probe's own list in
 * spawn order (which is what World.loadedEntityList is for these runs): the
 * tick bookkeeping, onUpdate, then the chunk membership, dead entities removed
 * the way the world removes them, and entities a tick spawns (babies, eggs,
 * death drops, XP orbs) absorbed at the end of the list and ticked in the same
 * pass.
 *
 * Every live entity is recorded after every tick: a 64-bit FNV-1a hash of the
 * canonical NBT text StructuresProbe.canon writes (the digest the tape rows
 * carry), plus the state the next tick reads that NBT does not carry (EntityLiving.entityAge,
 * ticksExisted, the AI's tick counter and running task entries and each task's
 * counter, PathNavigate's path and stuck-detection state, the move, look, jump
 * and body helpers, livingSoundTime, the sheep's eat timer and the chicken's egg
 * timer). Every 64th tick and the final tick carry the full canonical NBT text.
 *
 * Output DIR/manifest.json, DIR/shapes.bin, DIR/spawns.bin, DIR/ticks.bin.gz,
 * DIR/digest.txt.gz, DIR/nbt64.txt.gz, DIR/spawns.txt.gz, DIR/removals.txt.gz,
 * DIR/start.txt, DIR/end.txt, DIR/final.bin.gz.
 */
public final class AnimalProbe
{
    /** The animal kinds the probe spawns, in the manifest's order. */
    static final String[] KINDS = {"Pig", "Cow", "MushroomCow", "Chicken", "Sheep"};

    static final int KIND_PIG = 0, KIND_COW = 1, KIND_MOOSHROOM = 2, KIND_CHICKEN = 3, KIND_SHEEP = 4;

    /** The fixed per-entity state record. Keep in step with test_animals.c. */
    static final int ENT_STATE_BYTES = 272;
    static final int SPAWN_BYTES = 100;

    static final long FNV_OFFSET = 0xcbf29ce484222325L;
    static final long FNV_PRIME = 0x100000001b3L;

    private AnimalProbe() {}

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
        }, "Oracle AnimalProbe");
        t.start();
        t.join();
        if (error[0] != null) throw error[0];
        return result[0];
    }

    static JsonObject dump(IntegratedServer server, JsonObject cmd) throws Exception
    {
        int cx = cmd.get("cx").getAsInt(), cz = cmd.get("cz").getAsInt();
        int radius = cmd.has("radius") ? cmd.get("radius").getAsInt() : 2;
        int ring = cmd.has("ring") ? cmd.get("ring").getAsInt() : 1;
        int animals = cmd.has("animals") ? cmd.get("animals").getAsInt() : 220;
        int ticks = cmd.has("ticks") ? cmd.get("ticks").getAsInt() : 2400;
        long opseed = cmd.has("opseed") ? cmd.get("opseed").getAsLong() : 11L;
        int nbtEvery = cmd.has("nbtEvery") ? cmd.get("nbtEvery").getAsInt() : 64;
        File dir = new File(cmd.get("out").getAsString());
        dir.mkdirs();

        Trace.restart();   /* the trace holds this run, not the world's startup */

        WorldServer ws = server.worldServers[0];
        long seed = ws.getSeed();
        Probe.rawChunks = true;

        // Load the region first, then search it for the flattest, dry area.
        // The choose-then-load order matters: the search's candidates must be
        // inside the loaded set, and after the choice the whole ring around the
        // CHOSEN centre is loaded, so no path search during the ticks can ask
        // for a chunk outside it (which would generate it, and generation
        // reseeds World.rand through the structure maps).
        int x0 = cx - radius - ring, x1 = cx + radius + ring;
        int z0 = cz - radius - ring, z1 = cz + radius + ring;
        JsonArray loaded = new JsonArray();

        for (int lx = x0; lx <= x1; ++lx)
        {
            for (int lz = z0; lz <= z1; ++lz)
            {
                if (ws.getChunkFromChunkCoords(lx, lz) == null) throw new IllegalStateException("chunk (" + lx + "," + lz + ") did not load");
            }
        }

        int area = 2 * radius + 1;
        int bestX = cx, bestZ = cz, bestHi = 0, bestSpan = Integer.MAX_VALUE, bestRank = 2;

        for (int ax = x0 + radius; ax <= x1 - radius; ++ax)
        {
            for (int az = z0 + radius; az <= z1 - radius; ++az)
            {
                int lo = 999, hi = -999;

                for (int lx = ax - radius; lx <= ax + radius; ++lx)
                {
                    for (int lz = az - radius; lz <= az + radius; ++lz)
                    {
                        for (int sx = 0; sx < 16; ++sx)
                        {
                            for (int sz = 0; sz < 16; ++sz)
                            {
                                int h = heightOf(ws, lx * 16 + sx, lz * 16 + sz);
                                if (h < lo) lo = h;
                                if (h > hi) hi = h;
                            }
                        }
                    }
                }

                // dry ground first (lo >= 64 is above sea level), then the
                // flattest area
                int rank = lo >= 64 ? 0 : 1;

                if (rank < bestRank || (rank == bestRank && hi - lo < bestSpan))
                {
                    bestRank = rank;
                    bestSpan = hi - lo;
                    bestX = ax;
                    bestZ = az;
                    bestHi = hi;
                }
            }
        }

        cx = bestX;
        cz = bestZ;

        // The full ring around the chosen centre, in the recorded cx-major
        // order: the chunks of the first load are already there.
        x0 = cx - radius - ring;
        x1 = cx + radius + ring;
        z0 = cz - radius - ring;
        z1 = cz + radius + ring;

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

        // The platform sits one above the highest column of the chosen area, so
        // no natural block pokes through it.
        int platformY = bestHi + 1;

        int bx0 = (cx - radius) * 16, bz0 = (cz - radius) * 16;
        int width = area * 16;

        // The world's entities before anything is written: the spawn-chunk mobs
        // worldgen placed far away are in loadedEntityList and never appear in
        // the probe's region, so the count is the baseline the setup must not
        // change.
        int entitiesBefore = ws.loadedEntityList.size();

        // ------------------------------------------------------------ shapes
        OutputStream shapesRaw = new BufferedOutputStream(new FileOutputStream(new File(dir, "shapes.bin")), 1 << 16);
        byte[] shape = new byte[16];
        int shapeCount = 0;

        // The platform: every column of the area filled with dirt up to
        // platformY - 1 and grass on top. Written in column order, x outer.
        for (int x = bx0; x < bx0 + width; ++x)
        {
            for (int z = bz0; z < bz0 + width; ++z)
            {
                int h = heightOf(ws, x, z);

                for (int y = h; y < platformY; ++y) shapeCount += place(ws, shapesRaw, shape, x, y, z, y == platformY - 1 ? 2 : 3, 0);
            }
        }

        // The water pool: a 7x7 hole three deep in one corner, filled with
        // still water; the lava pit: a 5x5 hole three deep in the other.
        int poolX = bx0 + 6, poolZ = bz0 + 6;
        int lavaX = bx0 + width - 12, lavaZ = bz0 + width - 12;

        for (int x = 0; x < 7; ++x)
        {
            for (int z = 0; z < 7; ++z)
            {
                for (int y = 1; y <= 3; ++y) shapeCount += place(ws, shapesRaw, shape, poolX + x, platformY - y, poolZ + z, 9, 0);
            }
        }

        for (int x = 0; x < 5; ++x)
        {
            for (int z = 0; z < 5; ++z)
            {
                for (int y = 1; y <= 3; ++y) shapeCount += place(ws, shapesRaw, shape, lavaX + x, platformY - y, lavaZ + z, 11, 0);
            }
        }

        // Two pits (three deep) the animals fall into, and a cactus row with
        // sand under it.
        for (int x = 0; x < 4; ++x)
        {
            for (int z = 0; z < 4; ++z)
            {
                for (int y = 1; y <= 3; ++y) shapeCount += place(ws, shapesRaw, shape, bx0 + 30 + x, platformY - y, bz0 + 12 + z, 0, 0);
                for (int y = 1; y <= 3; ++y) shapeCount += place(ws, shapesRaw, shape, bx0 + 12 + x, platformY - y, bz0 + width - 8 + z, 0, 0);
            }
        }

        for (int z = 0; z < 10; ++z)
        {
            shapeCount += place(ws, shapesRaw, shape, bx0 + 40, platformY, bz0 + 30 + z, 12, 0);
            shapeCount += place(ws, shapesRaw, shape, bx0 + 40, platformY + 1, bz0 + 30 + z, 81, 0);
        }

        // The fence around the border of the area.
        for (int x = 0; x < width; ++x)
        {
            shapeCount += place(ws, shapesRaw, shape, bx0 + x, platformY, bz0, 85, 0);
            shapeCount += place(ws, shapesRaw, shape, bx0 + x, platformY, bz0 + width - 1, 85, 0);
        }

        for (int z = 0; z < width; ++z)
        {
            shapeCount += place(ws, shapesRaw, shape, bx0, platformY, bz0 + z, 85, 0);
            shapeCount += place(ws, shapesRaw, shape, bx0 + width - 1, platformY, bz0 + z, 85, 0);
        }

        shapesRaw.close();

        // ------------------------------------------------------------- start
        writeDetState(new File(dir, "start.txt"), seed, ws);

        // ------------------------------------------------------------ spawns
        Random r = new Random(opseed);
        Map<Entity, Integer> spawnIndex = new IdentityHashMap<Entity, Integer>();
        List<Entity> list = new ArrayList<Entity>();
        byte[] spawnBuf = new byte[SPAWN_BYTES];
        OutputStream spawnsOut = new BufferedOutputStream(new FileOutputStream(new File(dir, "spawns.bin")), 1 << 16);
        GZIPOutputStream spawnsTxtGz = new GZIPOutputStream(new FileOutputStream(new File(dir, "spawns.txt.gz")), 1 << 16);
        PrintWriter sw = new PrintWriter(new OutputStreamWriter(spawnsTxtGz, "UTF-8"));

        int[] spawnedKind = new int[KINDS.length];
        int pairX = 0, pairY = 0, pairZ = 0;

        for (int i = 0; i < animals; ++i)
        {
            // Pairs: animal 2p and 2p+1 are the same kind and sit one block
            // apart, so the in-love pairs (pair % 3 == 0) find each other and
            // breed. The every-11th animal starts high and falls, the every-13th
            // next to the lava, the every-17th at the pool, the every-19th next
            // to the cactus, the every-7th is a baby.
            int pair = i / 2;
            int kind = pair % KINDS.length;
            int x, y, z;

            if (i % 2 == 1)
            {
                // the second animal of a pair stands one block east of the
                // first, so a mate is within reach on the first tick; no draw
                x = pairX + 1;
                y = pairY;
                z = pairZ;
            }
            else
            {
                if (i % 11 == 5)
                {
                    x = bx0 + 4 + r.nextInt(width - 8);
                    z = bz0 + 4 + r.nextInt(width - 8);
                    y = platformY + 18 + r.nextInt(8);
                }
                else if (i % 13 == 7)
                {
                    x = lavaX + 5 + r.nextInt(4);
                    z = lavaZ + r.nextInt(5);
                    y = platformY + 1;
                }
                else if (i % 17 == 3)
                {
                    x = poolX + 7 + r.nextInt(4);
                    z = poolZ + r.nextInt(7);
                    y = platformY + 1;
                }
                else if (i % 19 == 9)
                {
                    x = bx0 + 39 + r.nextInt(3);
                    z = bz0 + 30 + r.nextInt(10);
                    y = platformY + 1;
                }
                else
                {
                    x = bx0 + 3 + r.nextInt(width - 6);
                    z = bz0 + 3 + r.nextInt(width - 6);
                    y = platformY + 1;
                }

                pairX = x;
                pairY = y;
                pairZ = z;
            }

            boolean inLove = pair % 3 == 0;
            boolean baby = i % 7 == 2;
            boolean saddled = kind == KIND_PIG && i % 5 == 0;
            boolean sheared = kind == KIND_SHEEP && i % 3 == 0;
            float yaw = r.nextFloat() * 360.0F;

            Entity e = EntityList.createEntityByName(KINDS[kind], ws);

            if (e == null) throw new IllegalStateException("no entity for " + KINDS[kind]);

            e.setLocationAndAngles(x + 0.5D, y, z + 0.5D, yaw, 0.0F);

            if (baby) ((EntityAgeable)e).setGrowingAge(-24000 + r.nextInt(1200));
            if (inLove) setField(e, "inLove", Integer.valueOf(600));
            if (saddled) ((EntityPig)e).setSaddled(true);
            if (sheared) ((EntitySheep)e).setSheared(true);
            if (kind == KIND_CHICKEN) ((EntityChicken)e).timeUntilNextEgg = 300 + r.nextInt(1200);

            ((EntityLiving)e).onSpawnWithEgg(null);

            if (!ws.spawnEntityInWorld(e)) throw new IllegalStateException("entity " + i + " did not spawn at (" + x + "," + y + "," + z + ")");

            spawnIndex.put(e, Integer.valueOf(i));
            list.add(e);
            ++spawnedKind[kind];

            int p = 0;
            p = le32(spawnBuf, p, i);
            p = le32(spawnBuf, p, e.getEntityId());
            p = le32(spawnBuf, p, kind);
            p = le64(spawnBuf, p, Double.doubleToRawLongBits(e.posX));
            p = le64(spawnBuf, p, Double.doubleToRawLongBits(e.posY));
            p = le64(spawnBuf, p, Double.doubleToRawLongBits(e.posZ));
            p = le32(spawnBuf, p, Float.floatToRawIntBits(e.rotationYaw));
            p = le32(spawnBuf, p, Float.floatToRawIntBits(e.rotationPitch));
            p = le64(spawnBuf, p, Double.doubleToRawLongBits(e.motionX));
            p = le64(spawnBuf, p, Double.doubleToRawLongBits(e.motionY));
            p = le64(spawnBuf, p, Double.doubleToRawLongBits(e.motionZ));
            p = le32(spawnBuf, p, ((EntityAgeable)e).getGrowingAge());
            p = le32(spawnBuf, p, intField(e, "inLove"));
            p = le32(spawnBuf, p, saddled ? 1 : 0);
            p = le32(spawnBuf, p, kind == KIND_CHICKEN ? ((EntityChicken)e).timeUntilNextEgg : 0);
            p = le32(spawnBuf, p, sheared ? 1 : 0);
            spawnsOut.write(spawnBuf, 0, SPAWN_BYTES);

            sw.println(i + " " + e.getEntityId() + " " + KINDS[kind] + " " + canon(e));
        }

        spawnsOut.close();
        sw.close();
        spawnsTxtGz.close();

        // The world holds exactly the probe's animals: a setup write that spawned
        // an entity (a falling block, a water flow) would draw Det streams and
        // change the world's chunk lists, which the native replay would not see.
        if (ws.loadedEntityList.size() != entitiesBefore + list.size())
        {
            throw new IllegalStateException("the world holds " + ws.loadedEntityList.size() + " entities, "
                + entitiesBefore + " before the setup and " + list.size() + " spawned");
        }

        // -------------------------------------------------------------- ticks
        GZIPOutputStream ticksGz = new GZIPOutputStream(new FileOutputStream(new File(dir, "ticks.bin.gz")), 1 << 16);
        OutputStream ticksOut = new BufferedOutputStream(ticksGz, 1 << 16);
        GZIPOutputStream digestGz = new GZIPOutputStream(new FileOutputStream(new File(dir, "digest.txt.gz")), 1 << 16);
        PrintWriter dw = new PrintWriter(new OutputStreamWriter(digestGz, "UTF-8"));
        GZIPOutputStream remGz = new GZIPOutputStream(new FileOutputStream(new File(dir, "removals.txt.gz")), 1 << 16);
        PrintWriter rw = new PrintWriter(new OutputStreamWriter(remGz, "UTF-8"));
        GZIPOutputStream nbtGz = new GZIPOutputStream(new FileOutputStream(new File(dir, "nbt64.txt.gz")), 1 << 16);
        PrintWriter nw = new PrintWriter(new OutputStreamWriter(nbtGz, "UTF-8"));

        Field isDeadField = Entity.class.getDeclaredField("isDead");
        isDeadField.setAccessible(true);

        int chunksBefore = ws.theChunkProviderServer.getLoadedChunkCount();
        int[] deathsKind = new int[KINDS.length];
        int[] birthsKind = new int[KINDS.length];
        int[] dropsItem = new int[3001];
        int orbCount = 0;
        int dropCount = 0, eggCount = 0;
        int maxList = list.size();
        int absorbed = ws.loadedEntityList.size();
        byte[] entRec = new byte[ENT_STATE_BYTES];
        int birthTick = -1, firstDeathTick = -1;

        for (int t = 0; t < ticks; ++t)
        {
            Trace.t("tick", Integer.valueOf(t));
            // World.updateEntities' entity pass, plus the spawn absorption the
            // world's own loop gets for free from loadedEntityList.
            for (int i = 0; i < list.size(); ++i)
            {
                Entity e = list.get(i);

                if (e.isDead) continue;

                updateEntity(ws, e);

                // the entities this tick spawned: babies, eggs, drops, orbs
                if (ws.loadedEntityList.size() > absorbed)
                {
                    for (int a = absorbed; a < ws.loadedEntityList.size(); ++a)
                    {
                        Entity n = (Entity)ws.loadedEntityList.get(a);

                        if (spawnIndex.containsKey(n)) continue;

                        spawnIndex.put(n, Integer.valueOf(list.size()));
                        list.add(n);

                        if (n instanceof EntityLivingBase)
                        {
                            ++birthsKind[kindOf(n)];
                            birthTick = t;
                        }
                        else
                        {
                            ItemStack st = itemStackOf(n);

                            if (st != null)
                            {
                                int id = net.minecraft.item.Item.getIdFromItem(st.getItem());
                                if (id >= 0 && id < dropsItem.length) ++dropsItem[id];
                                if (id == 344) ++eggCount;
                                ++dropCount;
                            }
                            else if (n instanceof net.minecraft.entity.item.EntityXPOrb)
                            {
                                ++orbCount;
                            }
                        }

                        if (list.size() > maxList) maxList = list.size();
                    }

                    absorbed = ws.loadedEntityList.size();
                }

                if (e.isDead)
                {
                    int reason = 1;
                    int si = spawnIndex.get(e).intValue();

                    if (e instanceof EntityLivingBase) ++deathsKind[kindOf(e)];
                    if (firstDeathTick < 0) firstDeathTick = t;

                    if (e.addedToChunk && ws.theChunkProviderServer.chunkExists(e.chunkCoordX, e.chunkCoordZ))
                    {
                        ws.getChunkFromChunkCoords(e.chunkCoordX, e.chunkCoordZ).removeEntity(e);
                    }

                    list.remove(i--);

                    rw.println(t + " " + si + " " + e.getEntityId() + " " + reason + " " + (e instanceof EntityLivingBase
                        ? hex(Float.floatToRawIntBits(((EntityLivingBase)e).getHealth())) : "-1") + " " + intField(e, "fire"));
                }
            }

            // the tick header (tick, count), then the state of every live entity
            byte[] head = new byte[8];
            int hp = 0;
            hp = le32(head, hp, t);
            hp = le32(head, hp, list.size());
            ticksOut.write(head, 0, 8);

            for (int i = 0; i < list.size(); ++i)
            {
                writeState(entRec, t, list.get(i), spawnIndex, ws);
                ticksOut.write(entRec, 0, ENT_STATE_BYTES);
            }

            if (nbtEvery > 0 && (t < 8 || t % nbtEvery == nbtEvery - 1 || t == ticks - 1))
            {
                nw.println("t " + t);

                for (int i = 0; i < list.size(); ++i)
                {
                    Entity e = list.get(i);
                    nw.println(spawnIndex.get(e).intValue() + " " + canon(e));
                }
            }

            writeDetLine(dw, t, ws);
        }

        ticksOut.close();
        ticksGz.close();
        dw.close();
        digestGz.close();
        rw.close();
        remGz.close();
        nw.close();
        nbtGz.close();

        writeDetState(new File(dir, "end.txt"), seed, ws);

        // No chunk may have been generated during the ticks: generation reseeds
        // World.rand through the structure maps, and the native replay does not
        // generate, so a run that generated could not be replayed.
        if (ws.theChunkProviderServer.getLoadedChunkCount() != chunksBefore)
        {
            throw new IllegalStateException("the world generated chunks during the ticks: "
                + chunksBefore + " chunks before, " + ws.theChunkProviderServer.getLoadedChunkCount() + " after");
        }

        // --------------------------------------------------------- final state
        DataOutputStream fin = new DataOutputStream(new GZIPOutputStream(new FileOutputStream(new File(dir, "final.bin.gz")), 1 << 16));
        byte[] chunkBytes = new byte[Probe.CHUNK_BYTES];

        for (int lx = x0; lx <= x1; ++lx)
        {
            for (int lz = z0; lz <= z1; ++lz)
            {
                net.minecraft.world.chunk.Chunk c = ws.getChunkFromChunkCoords(lx, lz);
                Probe.fillChunkBytes(c, chunkBytes);
                fin.writeInt(lx);
                fin.writeInt(lz);
                // ids uint16 (131072), metas (65536), then the two height maps,
                // the minimum and the section mask uint16
                fin.write(chunkBytes, 0, 65536 * 3);
                fin.write(chunkBytes, 65536 * 5, 1024 + 1024 + 4);
                fin.writeByte(chunkBytes[65536 * 5 + 2052]);
                fin.writeByte(chunkBytes[65536 * 5 + 2053]);
            }
        }

        fin.close();

        // ------------------------------------------------------------ manifest
        JsonObject m = new JsonObject();
        m.addProperty("seed", seed);
        m.addProperty("kind", "animals");
        m.addProperty("cx", cx);
        m.addProperty("cz", cz);
        m.addProperty("radius", radius);
        m.addProperty("ring", ring);
        m.addProperty("platformY", platformY);
        m.addProperty("skylightSubtracted", ws.skylightSubtracted);
        m.addProperty("animals", animals);
        m.addProperty("ticks", ticks);
        m.addProperty("opseed", opseed);
        m.addProperty("nbtEvery", nbtEvery);
        m.addProperty("order", "cx-major: for cx in " + x0 + ".." + x1 + ", for cz in " + z0 + ".." + z1);
        m.add("loaded", loaded);
        m.addProperty("kinds", "0 Pig, 1 Cow, 2 MushroomCow, 3 Chicken, 4 Sheep");
        m.addProperty("skylight", "World.skylightSubtracted at the run: the server is parked, so it is constant and the light reads use it");
        m.addProperty("shapes_layout", "16 bytes per placement, in placement order: x int32 LE, y int32 LE, z int32 LE, id uint16 LE, meta uint8, pad uint8");
        m.addProperty("platform", "the 5x5-chunk area flattened to platformY: per column x-major, for y from heightValue(x, z) to platformY - 1 setBlock(id 3, meta 0, flags 2) and at platformY setBlock(id 2, meta 0, flags 2); then the pool (7x7 at (bx0+6, bz0+6), y platformY-1 .. platformY-3, id 9), the lava pit (5x5 at (bx0+width-12, bz0+width-12), same three rows, id 11), two 4x4x3 pits (id 0) at (bx0+30, bz0+12) and (bx0+12, bz0+width-8), the cactus row (sand id 12 at platformY, cactus id 81 at platformY+1 at (bx0+40, bz0+30+z) for z 0..9) and the fence border (id 85 at platformY along the four edges of the area)");
        m.addProperty("spawn_layout", SPAWN_BYTES + " bytes per spawn, in spawn order: spawn_index int32 LE, entity_id int32 LE, kind int32 LE, x, y, z double LE (after setLocationAndAngles), rotationYaw float32 LE, rotationPitch float32 LE, motionX, motionY, motionZ double LE, growingAge int32 LE, inLove int32 LE, saddled int32 LE, timeUntilNextEgg int32 LE (0 for the other kinds), sheared int32 LE");
        m.addProperty("spawn_draws", "per animal i from Random(opseed), in this order: the placement (i % 11 == 5: x = bx0+4+nextInt(width-8), z the same, y = platformY+18+nextInt(8); else i % 13 == 7: x = lavaX+5+nextInt(4), z = lavaZ+nextInt(5), y = platformY+1; else i % 17 == 3: x = poolX+7+nextInt(4), z = poolZ+nextInt(7), y = platformY+1; else i % 19 == 9: x = bx0+39+nextInt(3), z = bz0+30+nextInt(10), y = platformY+1; else x = bx0+3+nextInt(width-6), z = bz0+3+nextInt(width-6), y = platformY+1), yaw = nextFloat()*360, then a baby draw nextInt(1200) when i % 7 == 2 and an egg-timer draw nextInt(600) when the kind is Chicken. Then: kind = i % 5; inLove = i % 6 < 2; baby = i % 7 == 2; saddled = kind == 0 && i % 5 == 0, sheared = kind == 4 && i % 3 == 0. Then EntityList.createEntityByName (the constructor's draws), setLocationAndAngles(x+0.5, y, z+0.5, yaw, 0), setGrowingAge(-24000+draw) when a baby, inLove = 600 when in love, setSaddled(true) when saddled, setSheared(true) when sheared, timeUntilNextEgg = 100+draw when a chicken, onSpawnWithEgg(null), world.spawnEntityInWorld");
        m.addProperty("tick_loop", "per tick t: for (i = 0; i < list.size(); ++i) { skip when isDead; World.updateEntityWithOptionalForce(e, true); absorb every entity the world's loadedEntityList gained during the tick to the end of the list (a birth when it is an EntityLivingBase, otherwise an item or an orb); when the entity just died: the chunk's removeEntity by (chunkCoordX, chunkCoordZ) when addedToChunk and the chunk exists, then list.remove(i--), then the removal line; } then one state record per live entity in the list's order, then the full NBT block when nbtEvery cuts one, then the digest line");
        m.addProperty("ent_state_layout", ENT_STATE_BYTES + " bytes per record, in list order, per tick: " +
            "spawn_index int32, entity_id int32, kind int32, nbt_hash uint64 (FNV-1a over the canonical NBT text of entity.writeToNBT(new NBTTagCompound())), " +
            "entity_age int32, ticks_existed int32, ai_tick_count int32, ai_executing int32 (bit i set when EntityAITasks.taskEntries[i] is in executingTaskEntries), " +
            "task_state int32 x 10 (slot i is the i-th task entry's counter: EntityAIMate.spawnBabyDelay, EntityAITempt.delayTemptCounter, EntityAIFollowParent.field_75345_d, EntityAIWatchClosest.lookTime, EntityAILookIdle.idleTime, EntityAIEatGrass.field_151502_a, else -1), " +
            "nav_has_path int32, nav_index int32, nav_length int32, nav_total_ticks int32, nav_ticks_at_last_pos int32, nav_speed double, nav_lx double, nav_ly double, nav_lz double, nav_hash uint64 (FNV-1a over the path's (x, y, z) int32 triples in order), " +
            "mh_update int32, mh_x double, mh_y double, mh_z double, mh_speed double, " +
            "look_is_looking int32, look_x double, look_y double, look_z double, look_delta_yaw float32, look_delta_pitch float32, " +
            "jump_is_jumping int32, move_forward float32, move_strafing float32, rotation_yaw_head float32, render_yaw_offset float32, " +
            "living_sound_time int32, body_counter int32, body_yaw float32, sheep_timer int32, egg_timer int32, breeding int32, revenge_timer int32, flags int32 (bit 0 addedToChunk, bit 1 onGround), rand_state uint64 (Det.state of the entity's own Random), pad int32");
        m.addProperty("ticks_layout", "per tick: tick int32 LE, count int32 LE, then count state records");
        m.addProperty("nbt64", "nbt64.txt.gz: the first eight ticks, every " + nbtEvery + "th tick and the final tick, line t <tick>, then one line per live entity in list order: <spawn_index> <the canonical NBT text of entity.writeToNBT(new NBTTagCompound())>");
        m.addProperty("spawns_txt", "spawns.txt.gz: one line per initial spawn: <spawn_index> <entity_id> <kind name> <canonical NBT at spawn>");
        m.addProperty("removals", "removals.txt.gz, one line per removal at the tick it happened: <tick> <spawn_index> <entity_id> <reason> <health as hex float bits or -1> <fire>; reason 1 is a dead entity leaving the list");
        m.addProperty("digest", "digest.txt.gz, one line per tick: t <tick>, then per role 0..3: role <r> seeder <hex> math <hex> split <hex>, then nextId <the OTHER role's next id>, worldRand <world.rand state hex>, worldRandGauss <0 or 1> the pending nextGaussian flag");
        m.addProperty("start_end", "start.txt and end.txt, the DetProbe snapshot format (resetSeed, worldSeed, nextId, digest per role, split per registered stream), plus worldRand <state> <pending gaussian flag>) before the first spawn and after the last tick");
        m.addProperty("final_layout", "final.bin.gz, per loaded chunk in load order: cx int32 BE from DataOutputStream.writeInt, cz int32 BE, ids uint16 LE (65536), metas uint8 (65536), heightMap 256 int32 LE, precipitationHeightMap 256 int32 LE, heightMapMinimum int32 LE, section mask uint16 LE");
        StringBuilder hist = new StringBuilder();

        for (int id = 0; id < dropsItem.length; ++id)
        {
            if (dropsItem[id] > 0) hist.append(id).append('x').append(dropsItem[id]).append(' ');
        }

        m.addProperty("drop_ids", "item entities spawned during the run, by item id: " + hist.toString());
        m.addProperty("task_lists", taskLists());
        m.addProperty("counts", "entities " + animals + " (pig " + spawnedKind[0] + ", cow " + spawnedKind[1] + ", mooshroom " + spawnedKind[2]
            + ", chicken " + spawnedKind[3] + ", sheep " + spawnedKind[4] + "), births " + sum(birthsKind) + " (pig " + birthsKind[0] + ", cow "
            + birthsKind[1] + ", mooshroom " + birthsKind[2] + ", chicken " + birthsKind[3] + ", sheep " + birthsKind[4] + "), deaths "
            + sum(deathsKind) + " (pig " + deathsKind[0] + ", cow " + deathsKind[1] + ", mooshroom " + deathsKind[2] + ", chicken "
            + deathsKind[3] + ", sheep " + deathsKind[4] + "), drops " + dropCount + " (eggs " + eggCount + "), orbs " + orbCount
            + ", max list " + maxList + ", first birth tick " + birthTick + ", first death tick " + firstDeathTick);
        PrintWriter mw = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "manifest.json")), "UTF-8"));
        mw.println(m.toString());
        mw.close();

        JsonObject res = new JsonObject();
        res.addProperty("dir", dir.getPath());
        res.addProperty("animals", animals);
        res.addProperty("ticks", ticks);
        res.addProperty("shapes", shapeCount);
        res.addProperty("births", sum(birthsKind));
        res.addProperty("deaths", sum(deathsKind));
        res.addProperty("drops", dropCount);
        res.addProperty("eggs", eggCount);
        res.addProperty("orbs", orbCount);
        res.addProperty("maxList", maxList);
        res.addProperty("birthTick", birthTick);
        res.addProperty("deathTick", firstDeathTick);
        return res;
    }

    // -------------------------------------------------------------- helpers

    /** The task entries each kind registers, in order, as class names. */
    static String taskLists()
    {
        StringBuilder b = new StringBuilder();
        String[] kinds = {"Pig", "Cow", "MushroomCow", "Chicken", "Sheep"};
        String[][] lists = {
            {"Swimming", "Panic", "ControlledByPlayer", "Mate", "Tempt", "Tempt", "FollowParent", "Wander", "WatchClosest", "LookIdle"},
            {"Swimming", "Panic", "Mate", "Tempt", "FollowParent", "Wander", "WatchClosest", "LookIdle"},
            {"Swimming", "Panic", "Mate", "Tempt", "FollowParent", "Wander", "WatchClosest", "LookIdle"},
            {"Swimming", "Panic", "Mate", "Tempt", "FollowParent", "Wander", "WatchClosest", "LookIdle"},
            {"Swimming", "Panic", "Mate", "Tempt", "FollowParent", "EatGrass", "Wander", "WatchClosest", "LookIdle"},
        };

        for (int i = 0; i < kinds.length; ++i)
        {
            b.append(i).append(" ").append(kinds[i]).append(':');

            for (int j = 0; j < lists[i].length; ++j) b.append(j == 0 ? " " : ",").append(lists[i][j]);

            b.append("; ");
        }

        return b.toString();
    }

    static int sum(int[] a)
    {
        int s = 0;

        for (int i = 0; i < a.length; ++i) s += a[i];

        return s;
    }

    static int heightOf(WorldServer ws, int x, int z)
    {
        return ws.getHeightValue(x, z);
    }

    static int place(WorldServer ws, OutputStream out, byte[] buf, int x, int y, int z, int id, int meta) throws Exception
    {
        ws.setBlock(x, y, z, Block.getBlockById(id), meta, 2);
        le32(buf, 0, x);
        le32(buf, 4, y);
        le32(buf, 8, z);
        le16(buf, 12, id);
        buf[14] = (byte)meta;
        buf[15] = 0;
        out.write(buf, 0, 16);
        return 1;
    }

    static int kindOf(Entity e)
    {
        String n = EntityList.getEntityString(e);
        if ("Pig".equals(n)) return KIND_PIG;
        if ("Cow".equals(n)) return KIND_COW;
        if ("MushroomCow".equals(n)) return KIND_MOOSHROOM;
        if ("Chicken".equals(n)) return KIND_CHICKEN;
        if ("Sheep".equals(n)) return KIND_SHEEP;
        return -1;
    }

    static ItemStack itemStackOf(Entity e)
    {
        if (e instanceof net.minecraft.entity.item.EntityItem) return ((net.minecraft.entity.item.EntityItem)e).getEntityItem();
        return null;
    }

    /** entity.writeToNBT, canonical text, the nbtjson.c form. */
    static String canon(Entity e)
    {
        net.minecraft.nbt.NBTTagCompound tag = new net.minecraft.nbt.NBTTagCompound();
        e.writeToNBT(tag);
        return StructuresProbe.canon(tag).toString();
    }

    /** FNV-1a 64 over the canonical text's UTF-8 bytes. */
    static long nbtHash(Entity e) throws Exception
    {
        byte[] b = canon(e).getBytes("UTF-8");
        long h = FNV_OFFSET;

        for (int i = 0; i < b.length; ++i) h = (h ^ (b[i] & 255)) * FNV_PRIME;

        return h;
    }

    /** The world's entity pass for one entity, World.updateEntityWithOptionalForce(e, true). */
    static void updateEntity(WorldServer ws, Entity e)
    {
        e.lastTickPosX = e.posX;
        e.lastTickPosY = e.posY;
        e.lastTickPosZ = e.posZ;
        e.prevRotationYaw = e.rotationYaw;
        e.prevRotationPitch = e.rotationPitch;

        if (e.addedToChunk)
        {
            ++e.ticksExisted;
            e.onUpdate();
        }

        if (Double.isNaN(e.posX) || Double.isInfinite(e.posX)) e.posX = e.lastTickPosX;
        if (Double.isNaN(e.posY) || Double.isInfinite(e.posY)) e.posY = e.lastTickPosY;
        if (Double.isNaN(e.posZ) || Double.isInfinite(e.posZ)) e.posZ = e.lastTickPosZ;
        if (Double.isNaN((double)e.rotationPitch) || Double.isInfinite((double)e.rotationPitch)) e.rotationPitch = e.prevRotationPitch;
        if (Double.isNaN((double)e.rotationYaw) || Double.isInfinite((double)e.rotationYaw)) e.rotationYaw = e.prevRotationYaw;

        int var6 = MathHelper.floor_double(e.posX / 16.0D);
        int var7 = MathHelper.floor_double(e.posY / 16.0D);
        int var8 = MathHelper.floor_double(e.posZ / 16.0D);

        if (!e.addedToChunk || e.chunkCoordX != var6 || e.chunkCoordY != var7 || e.chunkCoordZ != var8)
        {
            if (e.addedToChunk && ws.theChunkProviderServer.chunkExists(e.chunkCoordX, e.chunkCoordZ))
            {
                ws.getChunkFromChunkCoords(e.chunkCoordX, e.chunkCoordZ).removeEntityAtIndex(e, e.chunkCoordY);
            }

            if (ws.theChunkProviderServer.chunkExists(var6, var8))
            {
                e.addedToChunk = true;
                ws.getChunkFromChunkCoords(var6, var8).addEntity(e);
            }
            else
            {
                e.addedToChunk = false;
            }
        }

    }

    /** The fixed per-entity state record. */
    static void writeState(byte[] b, int tick, Entity e, Map<Entity, Integer> spawnIndex, WorldServer ws) throws Exception
    {
        int p = 0;
        EntityLiving lv = e instanceof EntityLiving ? (EntityLiving)e : null;
        EntityLivingBase lb = e instanceof EntityLivingBase ? (EntityLivingBase)e : null;

        p = le32(b, p, spawnIndex.get(e).intValue());
        p = le32(b, p, e.getEntityId());
        p = le32(b, p, kindOf(e));
        p = le64(b, p, nbtHash(e));
        p = le32(b, p, lb != null ? intField(lb, "entityAge") : 0);
        p = le32(b, p, e.ticksExisted);

        Object tasks = lv != null ? field(lv, "tasks") : null;
        int tickCount = tasks != null ? intOf(tasks, "tickCount") : -1;
        p = le32(b, p, tickCount);

        List<?> entries = tasks != null ? (List<?>)field(tasks, "taskEntries") : null;
        List<?> executing = tasks != null ? (List<?>)field(tasks, "executingTaskEntries") : null;
        int mask = 0;
        int[] slots = new int[10];

        for (int i = 0; i < slots.length; ++i) slots[i] = -1;

        if (entries != null)
        {
            for (int i = 0; i < entries.size(); ++i)
            {
                Object action = field(entries.get(i), "action");

                if (executing != null && executing.contains(entries.get(i))) mask |= 1 << i;
                if (i < slots.length) slots[i] = taskCounter(action);
            }
        }

        p = le32(b, p, mask);

        for (int i = 0; i < slots.length; ++i) p = le32(b, p, slots[i]);

        Object nav = lv != null ? lv.getNavigator() : null;
        Object path = nav != null ? field(nav, "currentPath") : null;
        p = le32(b, p, path != null ? 1 : 0);
        p = le32(b, p, path != null ? intOf(path, "currentPathIndex") : 0);
        p = le32(b, p, path != null ? intOf(path, "pathLength") : 0);
        p = le32(b, p, nav != null ? intOf(nav, "totalTicks") : 0);
        p = le32(b, p, nav != null ? intOf(nav, "ticksAtLastPos") : 0);
        p = le64(b, p, nav != null ? Double.doubleToRawLongBits(((Double)field(nav, "speed")).doubleValue()) : 0);
        Object lpc = nav != null ? field(nav, "lastPosCheck") : null;
        p = le64(b, p, lpc != null ? Double.doubleToRawLongBits(((net.minecraft.util.Vec3)lpc).xCoord) : 0);
        p = le64(b, p, lpc != null ? Double.doubleToRawLongBits(((net.minecraft.util.Vec3)lpc).yCoord) : 0);
        p = le64(b, p, lpc != null ? Double.doubleToRawLongBits(((net.minecraft.util.Vec3)lpc).zCoord) : 0);

        long nh = FNV_OFFSET;

        if (path != null)
        {
            Object points = field(path, "points");
            int n = Array.getLength(points);

            for (int i = 0; i < n; ++i)
            {
                Object pt = Array.get(points, i);
                int px = intOf(pt, "xCoord"), py = intOf(pt, "yCoord"), pz = intOf(pt, "zCoord");

                for (int k = 0; k < 4; ++k) nh = (nh ^ ((px >> (8 * k)) & 255)) * FNV_PRIME;
                for (int k = 0; k < 4; ++k) nh = (nh ^ ((py >> (8 * k)) & 255)) * FNV_PRIME;
                for (int k = 0; k < 4; ++k) nh = (nh ^ ((pz >> (8 * k)) & 255)) * FNV_PRIME;
            }
        }

        p = le64(b, p, nh);

        Object mh = lv != null ? lv.getMoveHelper() : null;
        p = le32(b, p, mh != null && boolOf(mh, "update") ? 1 : 0);
        p = le64(b, p, mh != null ? Double.doubleToRawLongBits(((Double)field(mh, "posX")).doubleValue()) : 0);
        p = le64(b, p, mh != null ? Double.doubleToRawLongBits(((Double)field(mh, "posY")).doubleValue()) : 0);
        p = le64(b, p, mh != null ? Double.doubleToRawLongBits(((Double)field(mh, "posZ")).doubleValue()) : 0);
        p = le64(b, p, mh != null ? Double.doubleToRawLongBits(((Double)field(mh, "speed")).doubleValue()) : 0);

        Object lk = lv != null ? lv.getLookHelper() : null;
        p = le32(b, p, lk != null && boolOf(lk, "isLooking") ? 1 : 0);
        p = le64(b, p, lk != null ? Double.doubleToRawLongBits(((Double)field(lk, "posX")).doubleValue()) : 0);
        p = le64(b, p, lk != null ? Double.doubleToRawLongBits(((Double)field(lk, "posY")).doubleValue()) : 0);
        p = le64(b, p, lk != null ? Double.doubleToRawLongBits(((Double)field(lk, "posZ")).doubleValue()) : 0);
        p = le32(b, p, lk != null ? Float.floatToRawIntBits(((Float)field(lk, "deltaLookYaw")).floatValue()) : 0);
        p = le32(b, p, lk != null ? Float.floatToRawIntBits(((Float)field(lk, "deltaLookPitch")).floatValue()) : 0);

        Object jh = lv != null ? lv.getJumpHelper() : null;
        p = le32(b, p, jh != null && boolOf(jh, "isJumping") ? 1 : 0);
        p = le32(b, p, lv != null ? Float.floatToRawIntBits(lv.moveForward) : 0);
        p = le32(b, p, lv != null ? Float.floatToRawIntBits(lv.moveStrafing) : 0);
        p = le32(b, p, lb != null ? Float.floatToRawIntBits(lb.rotationYawHead) : 0);
        p = le32(b, p, lb != null ? Float.floatToRawIntBits(lb.renderYawOffset) : 0);

        p = le32(b, p, lv != null ? lv.livingSoundTime : 0);

        Object bh = lv != null ? field(lv, "bodyHelper") : null;
        p = le32(b, p, bh != null ? intOf(bh, "field_75666_b") : 0);
        p = le32(b, p, bh != null ? Float.floatToRawIntBits(((Float)field(bh, "field_75667_c")).floatValue()) : 0);
        p = le32(b, p, e instanceof EntitySheep ? intOf(e, "sheepTimer") : -1);
        p = le32(b, p, e instanceof EntityChicken ? ((EntityChicken)e).timeUntilNextEgg : -1);
        p = le32(b, p, e instanceof net.minecraft.entity.passive.EntityAnimal ? intOf(e, "breeding") : 0);
        p = le32(b, p, lb != null ? intOf(lb, "revengeTimer") : 0);
        p = le32(b, p, (e.addedToChunk ? 1 : 0) | (e.onGround ? 2 : 0));
        p = le64(b, p, Det.state(e instanceof EntityLiving ? ((EntityLiving)e).getRNG() : entityRand(e)));
        p = le32(b, p, 0);

        if (p != ENT_STATE_BYTES) throw new IllegalStateException("state record wrote " + p + " bytes, the layout says " + ENT_STATE_BYTES);
    }

    /** The per-entity Random of an item or an orb, through Entity.rand. */
    static Random entityRand(Entity e) throws Exception
    {
        return (Random)findField(e, "rand").get(e);
    }

    static int taskCounter(Object action) throws Exception
    {
        if (action == null) return -1;

        String c = action.getClass().getSimpleName();

        if (c.equals("EntityAIMate")) return intOf(action, "spawnBabyDelay");
        if (c.equals("EntityAITempt")) return intOf(action, "delayTemptCounter");
        if (c.equals("EntityAIFollowParent")) return intOf(action, "field_75345_d");
        if (c.equals("EntityAIWatchClosest")) return intOf(action, "lookTime");
        if (c.equals("EntityAILookIdle")) return intOf(action, "idleTime");
        if (c.equals("EntityAIEatGrass")) return intOf(action, "field_151502_a");
        return -1;
    }

    /** A declared field by name, this class or any superclass. */
    static Field findField(Object o, String name)
    {
        for (Class<?> c = o.getClass(); c != null; c = c.getSuperclass())
        {
            try
            {
                Field f = c.getDeclaredField(name);
                f.setAccessible(true);
                return f;
            }
            catch (NoSuchFieldException e)
            {
                // keep walking
            }
        }

        throw new IllegalStateException("no field " + name + " on " + o.getClass());
    }

    static Object field(Object o, String name)
    {
        try
        {
            return findField(o, name).get(o);
        }
        catch (Exception e)
        {
            throw new RuntimeException(e);
        }
    }

    static int intOf(Object o, String name)
    {
        return ((Number)field(o, name)).intValue();
    }

    static boolean boolOf(Object o, String name)
    {
        return ((Boolean)field(o, name)).booleanValue();
    }

    static void setField(Object o, String name, Object v)
    {
        try
        {
            findField(o, name).set(o, v);
        }
        catch (Exception e)
        {
            throw new RuntimeException(e);
        }
    }

    static int intField(Object o, String name)
    {
        try
        {
            return findField(o, name).getInt(o);
        }
        catch (Exception e)
        {
            throw new RuntimeException(e);
        }
    }

    /** One digest line: the three Det states per role, the OTHER next id, world.rand. */
    static void writeDetLine(PrintWriter w, int t, WorldServer ws) throws Exception
    {
        StringBuilder b = new StringBuilder();
        b.append("t ").append(t);

        for (int role = 0; role < Det.ROLES; ++role)
        {
            b.append(" role ").append(role).append(' ').append(hex(Det.seederState(role))).append(' ')
                .append(hex(Det.mathState(role))).append(' ').append(hex(Det.splitState(role)));
        }

        synchronized (Det.nextId)
        {
            b.append(" nextId ").append(Det.nextId[Det.OTHER]);
        }

        Object wr = ws.rand;
        b.append(" worldRand ").append(hex(Det.state((Random)wr)));
        b.append(" worldRandGauss ").append(boolOf(wr, "haveNextNextGaussian") ? 1 : 0);
        w.println(b.toString());
    }

    /** The DetProbe snapshot format, at the call site. */
    static void writeDetState(File f, long seed, WorldServer ws) throws Exception
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

        Object wr = ws.rand;
        b.setLength(0);
        b.append("worldRand ").append(hex(Det.state((Random)wr))).append(' ').append(boolOf(wr, "haveNextNextGaussian") ? 1 : 0);
        w.println(b.toString());

        w.close();
    }

    static String hex(long bits)
    {
        StringBuilder b = new StringBuilder(16);

        for (int i = 15; i >= 0; --i) b.append("0123456789abcdef".charAt((int)((bits >>> (i * 4)) & 15L)));

        return b.toString();
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
}