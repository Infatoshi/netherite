package netherite.oracle;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import com.google.gson.JsonPrimitive;
import com.mojang.authlib.GameProfile;
import java.io.BufferedOutputStream;
import java.io.DataOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.IdentityHashMap;
import java.util.List;
import java.util.Map;
import java.util.Random;
import java.util.zip.GZIPOutputStream;
import net.minecraft.block.Block;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityList;
import net.minecraft.entity.EntityLiving;
import net.minecraft.entity.EntityLivingBase;
import net.minecraft.entity.SharedMonsterAttributes;
import net.minecraft.entity.monster.EntityMagmaCube;
import net.minecraft.entity.monster.EntitySlime;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.init.Items;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.util.DamageSource;
import net.minecraft.util.MathHelper;
import net.minecraft.world.EnumDifficulty;
import net.minecraft.world.World;
import net.minecraft.world.WorldServer;

/**
 * The slime probe: EntitySlime and EntityMagmaCube of every size (1, 2, 4)
 * ticking in a flattened raw region (the overworld for slimes, the Nether for
 * magma cubes) next to a probe player, the reference for the native port of
 * EntitySlime, EntityMagmaCube and the player half of the collision attack.
 *
 * The region is flattened to one solid level with dirt/netherrack, a wall
 * around the border, and the probe player stands in the middle. The mobs are
 * spawned the way SpawnerAnimals builds them: a fresh instance by name
 * (EntityList.createEntityByName, whose constructor draws the size and the
 * jump delay from the entity's own Random), setLocationAndAngles, the probe's
 * forced size (setSlimeSize, no draw), onSpawnWithEgg(null),
 * world.spawnEntityInWorld. A subset starts high and falls (the overworld
 * slimes die on landing and split), and in the Nether some magma cubes take a
 * probe damage event (magma cubes have no fall damage and no natural killer),
 * so both runs carry deaths and splits.
 *
 * The player is a concrete EntityPlayer (SlimeProbe.ProbePlayer) added to the
 * chunk's entity list, loadedEntityList and playerEntities, with its max
 * health raised so it survives the run. getClosestVulnerablePlayerToEntity and
 * despawnEntity's getClosestPlayerToEntity read playerEntities, so the slimes
 * find it; its own onLivingUpdate runs the collision loop that calls each
 * nearby slime's onCollideWithPlayer.
 *
 * The tick is World.updateEntities' entity pass over the probe's own list in
 * spawn order: the player first, then the mobs, the tick bookkeeping, onUpdate,
 * the chunk membership, dead entities removed the way the world removes them,
 * and entities a tick spawns (split children, magma cream drops) absorbed at
 * the end of the list and ticked in the same pass.
 *
 * Every live entity is recorded after every tick: a 64-bit FNV-1a hash of the
 * canonical NBT text (StructuresProbe.canon of entity.writeToNBT), plus the
 * state the next tick reads that NBT does not carry (the slime's jump delay,
 * the three squish fields, moveForward/moveStrafing, the jump/hurt counters,
 * the body and head rotation, the water/air/collision flags, the entity's own
 * Random state). The player has its own per-tick record (health, food stats,
 * the hurt timers, motion and position). Every 64th tick and the final tick
 * carry the full canonical NBT text.
 *
 * Output DIR/manifest.json, DIR/shapes.bin, DIR/spawns.bin, DIR/ticks.bin.gz,
 * DIR/events.txt.gz, DIR/digest.txt.gz, DIR/nbt64.txt.gz, DIR/spawns.txt.gz,
 * DIR/removals.txt.gz, DIR/start.txt, DIR/end.txt, DIR/final.bin.gz.
 */
public final class SlimeProbe
{
    /** The kinds the probe spawns, in the manifest's order. */
    static final String[] KINDS = {"Slime", "LavaSlime"};
    static final int KIND_SLIME = 0, KIND_MAGMA = 1;

    /** The fixed per-entity state record. Keep in step with test_slimes.c. */
    static final int REC_BYTES = 112;
    static final int SPAWN_BYTES = 100;
    static final int PLAYER_BYTES = 144;

    static final long FNV_OFFSET = 0xcbf29ce484222325L;
    static final long FNV_PRIME = 0x100000001b3L;

    /** setSlimeSize is protected on EntitySlime; the probe forces the size so a
     * run holds a fixed count of each size. */
    private static Method SET_SIZE;

    /** EntityTracker subclass that tracks nothing.
     *
     * World.spawnEntityInWorld ends in the world's IWorldAccess chain
     * (WorldManager.onEntityAdded -> EntityTracker.addEntityToTracker), which
     * sends every tracked entity's spawn packet to every entry of
     * world.playerEntities as an EntityPlayerMP. The probe's player is a plain
     * EntityPlayer and the probe ticks its own entities, so the tracker is
     * taken out of the loop for the run. */
    public static class SilentTracker extends net.minecraft.entity.EntityTracker
    {
        public SilentTracker(WorldServer ws)
        {
            super(ws);
        }

        public void addEntityToTracker(Entity e)
        {
        }

        public void addEntityToTracker(Entity e, int a, int b)
        {
        }

        public void addEntityToTracker(Entity e, int a, int b, boolean c)
        {
        }

        public void updateTrackedEntities()
        {
        }

        public void func_151247_a(Entity e, net.minecraft.network.Packet p)
        {
        }

        public void func_151248_b(Entity e, net.minecraft.network.Packet p)
        {
        }

        public void removeEntityFromAllTrackingPlayers(Entity e)
        {
        }

        public void removePlayerFromTrackers(net.minecraft.entity.player.EntityPlayerMP p)
        {
        }

        public void func_85172_a(net.minecraft.entity.player.EntityPlayerMP p, net.minecraft.world.chunk.Chunk c)
        {
        }
    }

    /** A concrete EntityPlayer: EntityPlayer has no abstract method left. */
    public static class ProbePlayer extends EntityPlayer
    {
        public ProbePlayer(World p_i45324_1_)
        {
            super(p_i45324_1_, new GameProfile(null, "ProbePlayer"));
        }

        public void addChatMessage(net.minecraft.util.IChatComponent p_145747_1_)
        {
        }

        public boolean canCommandSenderUseCommand(int p_70003_1_, String p_70003_2_)
        {
            return true;
        }

        public net.minecraft.util.ChunkCoordinates getPlayerCoordinates()
        {
            return new net.minecraft.util.ChunkCoordinates(MathHelper.floor_double(this.posX),
                MathHelper.floor_double(this.posY), MathHelper.floor_double(this.posZ));
        }
    }

    private SlimeProbe() {}

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
        }, "Oracle SlimeProbe");
        t.start();
        t.join();
        if (error[0] != null) throw error[0];
        return result[0];
    }

    static JsonObject dump(IntegratedServer server, JsonObject cmd) throws Exception
    {
        int dim = cmd.has("dim") ? cmd.get("dim").getAsInt() : 0;
        int radius = cmd.has("radius") ? cmd.get("radius").getAsInt() : 1;
        int ring = cmd.has("ring") ? cmd.get("ring").getAsInt() : 2;
        int perSize = cmd.has("perSize") ? cmd.get("perSize").getAsInt() : 20;
        int ticks = cmd.has("ticks") ? cmd.get("ticks").getAsInt() : 2400;
        long opseed = cmd.has("opseed") ? cmd.get("opseed").getAsLong() : 11L;
        int nbtEvery = cmd.has("nbtEvery") ? cmd.get("nbtEvery").getAsInt() : 64;
        File dir = new File(cmd.get("out").getAsString());
        dir.mkdirs();

        Trace.restart();

        WorldServer ws = server.worldServerForDimension(dim);
        long seed = ws.getSeed();
        Probe.rawChunks = true;

        ws.difficultySetting = EnumDifficulty.NORMAL;
        ws.skylightSubtracted = 0;

        // the probe's own player is a plain EntityPlayer: take the tracker out
        // of the world so nothing casts it to EntityPlayerMP
        try
        {
            Field trackerField = WorldServer.class.getDeclaredField("theEntityTracker");
            trackerField.setAccessible(true);
            trackerField.set(ws, new SilentTracker(ws));
        }
        catch (Exception e)
        {
            throw new IllegalStateException("could not replace the entity tracker", e);
        }

        int cx = cmd.has("cx") ? cmd.get("cx").getAsInt() : 0;
        int cz = cmd.has("cz") ? cmd.get("cz").getAsInt() : 0;

        // Load the region first, then search it for the flattest, dry area.
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
        int bestX = cx, bestZ = cz, bestHi = 0, bestSpan = Integer.MAX_VALUE, bestRank = 3;

        if (dim == 0)
        {
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
        }

        x0 = cx - radius - ring;
        x1 = cx + radius + ring;
        z0 = cz - radius - ring;
        z1 = cz + radius + ring;

        // The loaded set an entity can reach: the region's own generation pulls
        // in the ring around it (the chunk provider's terrain pass loads its
        // neighbours), and an entity that walks out of the region still has to
        // agree on which chunks exist, because chunkExists decides addedToChunk
        // and with it whether the entity ticks at all. A margin of pad chunks is
        // loaded explicitly so the set does not depend on what generation
        // happened to pull in, and the manifest records the provider's own set
        // (its insertion order) clipped to the box: a chunk outside it is one
        // neither side ever ticks in.
        int pad = cmd.has("pad") ? cmd.get("pad").getAsInt() : 3;

        for (int lx = x0 - pad; lx <= x1 + pad; ++lx)
        {
            for (int lz = z0 - pad; lz <= z1 + pad; ++lz)
            {
                if (ws.getChunkFromChunkCoords(lx, lz) == null) throw new IllegalStateException("chunk (" + lx + "," + lz + ") did not load");
            }
        }

        final int boxX0 = x0 - pad, boxX1 = x1 + pad, boxZ0 = z0 - pad, boxZ1 = z1 + pad;

        {
            java.util.List<?> list;

            try
            {
                Field f = net.minecraft.world.gen.ChunkProviderServer.class.getDeclaredField("loadedChunks");
                f.setAccessible(true);
                list = (java.util.List<?>)f.get(ws.theChunkProviderServer);
            }
            catch (Exception e)
            {
                throw new IllegalStateException("could not read the loaded chunk list", e);
            }

            for (Object o : list)
            {
                net.minecraft.world.chunk.Chunk ch = (net.minecraft.world.chunk.Chunk)o;

                if (ch.xPosition < boxX0 || ch.xPosition > boxX1 || ch.zPosition < boxZ0 || ch.zPosition > boxZ1) continue;

                JsonArray pair = new JsonArray();
                pair.add(new JsonPrimitive(ch.xPosition));
                pair.add(new JsonPrimitive(ch.zPosition));
                loaded.add(pair);
            }
        }

        if (loaded.size() != (boxX1 - boxX0 + 1) * (boxZ1 - boxZ0 + 1))
            throw new IllegalStateException("the loaded box holds " + loaded.size() + " chunks, expected "
                + (boxX1 - boxX0 + 1) * (boxZ1 - boxZ0 + 1));

        // The Nether floor is a rugged column of netherrack under a bedrock
        // ceiling, so there is no flat area to search for: the platform sits at
        // a fixed level, filled from below and cleared above.
        int platformY = dim == 0 ? bestHi + 1 : 64;
        int bx0 = (cx - radius) * 16, bz0 = (cz - radius) * 16;
        int width = area * 16;

        int entitiesBefore = ws.loadedEntityList.size();

        // After the region is loaded, no query may generate a chunk: a
        // generation reseeds World.rand and the native engine cannot generate,
        // so an unloaded chunk must read as air on both sides.
        ws.theChunkProviderServer.loadChunkOnProvideRequest = false;

        // ------------------------------------------------------------ shapes
        OutputStream shapesRaw = new BufferedOutputStream(new FileOutputStream(new File(dir, "shapes.bin")), 1 << 16);
        byte[] shape = new byte[16];
        int shapeCount = 0;
        int body = dim == 0 ? 3 : 87, top = dim == 0 ? 2 : 87;
        int wall = dim == 0 ? 4 : 87;

        for (int x = bx0; x < bx0 + width; ++x)
        {
            for (int z = bz0; z < bz0 + width; ++z)
            {
                int h = heightOf(ws, x, z);

                if (dim == 0)
                {
                    for (int y = h; y < platformY; ++y) shapeCount += place(ws, shapesRaw, shape, x, y, z, y == platformY - 1 ? top : body, 0);
                }
                else
                {
                    /* the whole column is written: the native world is the
                     * overworld generator, so nothing under the Nether platform
                     * can be left to the generators to agree on */
                    for (int y = 0; y < platformY - 1; ++y) shapeCount += place(ws, shapesRaw, shape, x, y, z, body, 0);

                    shapeCount += place(ws, shapesRaw, shape, x, platformY - 1, z, top, 0);

                    for (int y = platformY; y < 128; ++y) shapeCount += place(ws, shapesRaw, shape, x, y, z, 0, 0);
                }
            }
        }

        // The wall around the border of the area, four high from the surface the
        // entities stand on (platformY is the level they are placed at and walk
        // on; the platform's own top block is platformY - 1). It starts at
        // platformY, not above it: a size-1 slime is 0.6 high and walks straight
        // through a wall that begins one block up, and an entity that leaves the
        // area reaches the ring, where the two sides generate different terrain
        // (the oracle's world holds the populated spawn chunks, the native only
        // the raw chunks the manifest names).
        for (int x = 0; x < width; ++x)
        {
            for (int y = 0; y <= 3; ++y)
            {
                shapeCount += place(ws, shapesRaw, shape, bx0 + x, platformY + y, bz0, wall, 0);
                shapeCount += place(ws, shapesRaw, shape, bx0 + x, platformY + y, bz0 + width - 1, wall, 0);
            }
        }

        for (int z = 0; z < width; ++z)
        {
            for (int y = 0; y <= 3; ++y)
            {
                shapeCount += place(ws, shapesRaw, shape, bx0, platformY + y, bz0 + z, wall, 0);
                shapeCount += place(ws, shapesRaw, shape, bx0 + width - 1, platformY + y, bz0 + z, wall, 0);
            }
        }

        shapesRaw.close();

        writeDetState(new File(dir, "start.txt"), seed, ws);

        // ------------------------------------------------------------ spawns
        SET_SIZE = EntitySlime.class.getDeclaredMethod("setSlimeSize", int.class);
        SET_SIZE.setAccessible(true);

        Random r = new Random(opseed);
        List<Entity> list = new ArrayList<Entity>();
        Map<Entity, Integer> spawnIndex = new IdentityHashMap<Entity, Integer>();
        byte[] spawnBuf = new byte[SPAWN_BYTES];
        OutputStream spawnsOut = new BufferedOutputStream(new FileOutputStream(new File(dir, "spawns.bin")), 1 << 16);
        GZIPOutputStream spawnsTxtGz = new GZIPOutputStream(new FileOutputStream(new File(dir, "spawns.txt.gz")), 1 << 16);
        PrintWriter sw = new PrintWriter(new OutputStreamWriter(spawnsTxtGz, "UTF-8"));

        int[] spawnedSize = new int[3];      /* counts of size 1, 2, 4 */
        int[] kindSize = new int[2];         /* counts of slime, magma */
        int[] damageTick = new int[0];

        // The player stands at the centre of the platform.
        double playerX = bx0 + width / 2 + 0.5D, playerZ = bz0 + width / 2 + 0.5D;

        int mobs = 3 * perSize;

        for (int i = 0; i < mobs + 1; ++i)
        {
            boolean isPlayer = i == 0;
            int kind = dim == 0 ? KIND_SLIME : KIND_MAGMA;
            double x, y, z;

            if (isPlayer)
            {
                x = playerX;
                y = platformY;
                z = playerZ;
            }
            else
            {
                int slot = i - 1;
                // The probe's own placement draws: near the player for the
                // every-third mob, high in the sky for the falling ones, else
                // anywhere on the platform.
                if (slot % 3 == 0)
                {
                    x = playerX + 1.0D + r.nextInt(3) + 0.5D;
                    z = playerZ + 1.0D + r.nextInt(3) + 0.5D;
                    y = platformY + 1;
                }
                else if (slot % 3 == 1)
                {
                    x = bx0 + 4.0D + r.nextInt(width - 8) + 0.5D;
                    z = bz0 + 4.0D + r.nextInt(width - 8) + 0.5D;
                    y = platformY + 22 + r.nextInt(8);
                }
                else
                {
                    x = bx0 + 4.0D + r.nextInt(width - 8) + 0.5D;
                    z = bz0 + 4.0D + r.nextInt(width - 8) + 0.5D;
                    y = platformY + 1;
                }
            }

            float yaw = r.nextFloat() * 360.0F;

            Entity e;

            if (isPlayer)
            {
                e = new ProbePlayer(ws);
            }
            else
            {
                e = EntityList.createEntityByName(KINDS[kind], ws);
            }

            if (e == null) throw new IllegalStateException("no entity for the probe");

            e.setLocationAndAngles(x, y, z, yaw, 0.0F);

            int size = 0;

            if (isPlayer)
            {
                ((EntityLivingBase)e).getEntityAttribute(SharedMonsterAttributes.maxHealth).setBaseValue(2000.0D);
                ((EntityLivingBase)e).setHealth(2000.0F);

                // The 36 main inventory slots each hold a stone stack, so no
                // drop can be picked up: an item's onCollideWithPlayer would
                // otherwise pull it into the inventory, a path this lane does
                // not model. The armor slots stay empty (getTotalArmorValue 0).
                for (int slot = 0; slot < 36; ++slot)
                {
                    ((EntityPlayer)e).inventory.mainInventory[slot] = new ItemStack(Block.getBlockById(1), 64);
                }
            }
            else
            {
                size = 1 << ((i - 1) / perSize);   /* 1, 2 then 4 */
                SET_SIZE.invoke(e, Integer.valueOf(size));
                ((EntityLiving)e).onSpawnWithEgg(null);
            }

            if (isPlayer)
            {
                // The probe's player joins the chunk's entity list,
                // loadedEntityList and playerEntities: the server's tracker
                // would cast top layer entries to EntityPlayerMP, but the
                // server is parked for the whole run.
                net.minecraft.world.chunk.Chunk chunk = ws.getChunkFromChunkCoords(
                    MathHelper.floor_double(e.posX / 16.0D), MathHelper.floor_double(e.posZ / 16.0D));
                chunk.addEntity(e);
                ws.loadedEntityList.add(e);
                ws.playerEntities.add(e);
                e.addedToChunk = true;
            }
            else if (!ws.spawnEntityInWorld(e)) throw new IllegalStateException("entity " + i + " did not spawn at (" + x + "," + y + "," + z + ")");

            spawnIndex.put(e, Integer.valueOf(i));
            list.add(e);

            if (!isPlayer)
            {
                ++kindSize[kind];
                if (size == 1) ++spawnedSize[0];
                else if (size == 2) ++spawnedSize[1];
                else ++spawnedSize[2];
            }

            int p = 0;
            p = le32(spawnBuf, p, i);
            p = le32(spawnBuf, p, e.getEntityId());
            p = le32(spawnBuf, p, isPlayer ? 2 : kind);
            p = le32(spawnBuf, p, size);
            p = le64(spawnBuf, p, Double.doubleToRawLongBits(e.posX));
            p = le64(spawnBuf, p, Double.doubleToRawLongBits(e.posY));
            p = le64(spawnBuf, p, Double.doubleToRawLongBits(e.posZ));
            p = le32(spawnBuf, p, Float.floatToRawIntBits(e.rotationYaw));
            p = le32(spawnBuf, p, Float.floatToRawIntBits(e.rotationPitch));
            p = le64(spawnBuf, p, Double.doubleToRawLongBits(e.motionX));
            p = le64(spawnBuf, p, Double.doubleToRawLongBits(e.motionY));
            p = le64(spawnBuf, p, Double.doubleToRawLongBits(e.motionZ));
            p = le32(spawnBuf, p, isPlayer ? 1 : 0);
            spawnsOut.write(spawnBuf, 0, SPAWN_BYTES);

            sw.println(i + " " + e.getEntityId() + " " + (isPlayer ? "Player" : KINDS[kind]) + " " + size + " " + canon(e));
        }

        spawnsOut.close();
        sw.close();
        spawnsTxtGz.close();

        if (ws.loadedEntityList.size() != entitiesBefore + list.size())
        {
            throw new IllegalStateException("the world holds " + ws.loadedEntityList.size() + " entities, "
                + entitiesBefore + " before the setup and " + list.size() + " spawned");
        }

        // The magma cube deaths: EntityMagmaCube has no fall damage and nothing
        // else in the probe kills it, so the probe lands one lethal hit on every
        // tenth magma cube at a fixed tick. Each hit and each death is written
        // to events.txt.gz so the native replay repeats the same schedule.
        int[] dmgTarget = new int[mobs];
        int ndmg = 0;

        if (dim != 0)
        {
            for (int slot = 0; slot < mobs; ++slot)
            {
                if (slot % 10 == 6) dmgTarget[ndmg++] = slot + 1;   /* list index */
            }
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
        GZIPOutputStream evGz = new GZIPOutputStream(new FileOutputStream(new File(dir, "events.txt.gz")), 1 << 16);
        PrintWriter ew = new PrintWriter(new OutputStreamWriter(evGz, "UTF-8"));

        int chunksBefore = ws.theChunkProviderServer.getLoadedChunkCount();
        int[] deathsKind = new int[2];
        int[] deathsSize = new int[3];
        int[] splitsKind = new int[2];
        int[] splitsChildren = new int[2];
        int[] dropsItem = new int[3001];
        int dropCount = 0, orbCount = 0;
        int maxList = list.size();
        int absorbed = ws.loadedEntityList.size();
        byte[] rec = new byte[REC_BYTES];
        byte[] prec = new byte[PLAYER_BYTES];
        int firstDeathTick = -1, firstSplitTick = -1;
        int damageToPlayer = 0;
        double playerHealthMin = 2000.0;
        Entity player = list.get(0);
        int[] deathsExpected = new int[2];

        for (int t = 0; t < ticks; ++t)
        {
            Trace.t("tick", Integer.valueOf(t));

            // the probe's scheduled magma cube damage events
            for (int k = 0; k < ndmg; ++k)
            {
                int li = dmgTarget[k];
                int when = 60 + (k % 5) * 40;

                if (t == when)
                {
                    Entity e = list.get(li);
                    ((EntityLivingBase)e).attackEntityFrom(causeProbeDamage(), 1000.0F);
                    ew.println(t + " damage " + spawnIndex.get(e).intValue() + " " + e.getEntityId());
                }
            }

            for (int i = 0; i < list.size(); ++i)
            {
                Entity e = list.get(i);

                if (e.isDead) continue;

                double hpBefore = e instanceof EntityLivingBase ? ((EntityLivingBase)e).getHealth() : 0.0D;
                updateEntity(ws, e);

                if (e instanceof EntityPlayer && ((EntityLivingBase)e).getHealth() < hpBefore)
                {
                    ++damageToPlayer;
                    ew.println(t + " playerhit " + spawnIndex.get(e).intValue() + " "
                        + hex(Float.floatToRawIntBits((float)hpBefore)) + " "
                        + hex(Float.floatToRawIntBits((float)((EntityLivingBase)e).getHealth())));
                }

                if (ws.loadedEntityList.size() > absorbed)
                {
                    for (int a = absorbed; a < ws.loadedEntityList.size(); ++a)
                    {
                        Entity n = (Entity)ws.loadedEntityList.get(a);

                        if (spawnIndex.containsKey(n)) continue;

                        spawnIndex.put(n, Integer.valueOf(list.size()));
                        list.add(n);

                        if (n instanceof EntitySlime)
                        {
                            int nk = n instanceof EntityMagmaCube ? KIND_MAGMA : KIND_SLIME;
                            ++splitsKind[nk];
                            ++splitsChildren[nk];
                            if (firstSplitTick < 0) firstSplitTick = t;
                        }
                        else
                        {
                            ItemStack st = itemStackOf(n);

                            if (st != null)
                            {
                                int id = Item.getIdFromItem(st.getItem());
                                if (id >= 0 && id < dropsItem.length) ++dropsItem[id];
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
                    int si = spawnIndex.get(e).intValue();

                    if (e instanceof EntityLivingBase)
                    {
                        int nk = e instanceof EntityMagmaCube ? KIND_MAGMA : (e instanceof EntitySlime ? KIND_SLIME : -1);

                        if (nk >= 0)
                        {
                            ++deathsKind[nk];
                            int sz = e instanceof EntitySlime ? ((EntitySlime)e).getSlimeSize() : 0;
                            if (sz == 1) ++deathsSize[0];
                            else if (sz == 2) ++deathsSize[1];
                            else if (sz == 4) ++deathsSize[2];
                        }
                    }

                    if (firstDeathTick < 0) firstDeathTick = t;

                    if (e.addedToChunk && ws.theChunkProviderServer.chunkExists(e.chunkCoordX, e.chunkCoordZ))
                    {
                        ws.getChunkFromChunkCoords(e.chunkCoordX, e.chunkCoordZ).removeEntity(e);
                    }

                    rw.println(t + " " + si + " " + e.getEntityId() + " " + (e instanceof EntityLivingBase
                        ? hex(Float.floatToRawIntBits(((EntityLivingBase)e).getHealth())) : "-1"));

                    list.remove(i--);
                }

            }

            // Every entity stays inside the region: those chunks are loaded raw
            // and generated identically on both sides, while outside it the
            // oracle's world also holds the populated spawn area, which the
            // native cannot reproduce. A run where an entity gets out is not a
            // usable recording.
            for (int i = 0; i < list.size(); ++i)
            {
                Entity e = list.get(i);
                int ecx = MathHelper.floor_double(e.posX / 16.0D);
                int ecz = MathHelper.floor_double(e.posZ / 16.0D);

                if (ecx < x0 || ecx > x1 || ecz < z0 || ecz > z1)
                    throw new IllegalStateException("entity " + spawnIndex.get(e) + " left the loaded region at tick " + t
                        + ": chunk (" + ecx + "," + ecz + "), the region is (" + x0 + ".." + x1 + ", " + z0 + ".." + z1 + ")");
            }

            byte[] head = new byte[8];
            int hp = 0;
            hp = le32(head, hp, t);
            hp = le32(head, hp, list.size());
            ticksOut.write(head, 0, 8);

            for (int i = 0; i < list.size(); ++i)
            {
                writeState(rec, list.get(i), spawnIndex, ws);
                ticksOut.write(rec, 0, REC_BYTES);
            }

            writePlayer(prec, ws, list.get(0));
            ticksOut.write(prec, 0, PLAYER_BYTES);

            if (player instanceof EntityLivingBase && ((EntityLivingBase)player).getHealth() < playerHealthMin)
            {
                playerHealthMin = ((EntityLivingBase)player).getHealth();
            }

            if (nbtEvery > 0 && (t < 8 || t % nbtEvery == nbtEvery - 1 || t == ticks - 1))
            {
                nw.println("t " + t);

                for (int i = 0; i < list.size(); ++i)
                {
                    nw.println(spawnIndex.get(list.get(i)).intValue() + " " + canon(list.get(i)));
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
        ew.close();
        evGz.close();

        writeDetState(new File(dir, "end.txt"), seed, ws);

        // the probe's player leaves the world's player list before the server
        // resumes (a plain EntityPlayer is not what the tracker expects)
        ws.playerEntities.remove(list.get(0));

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
        m.addProperty("kind", "slimes");
        m.addProperty("dim", dim);
        m.addProperty("cx", cx);
        m.addProperty("cz", cz);
        m.addProperty("radius", radius);
        m.addProperty("ring", ring);
        m.addProperty("platformY", platformY);
        m.addProperty("skylightSubtracted", ws.skylightSubtracted);
        m.addProperty("difficulty", "NORMAL");
        m.addProperty("perSize", perSize);
        m.addProperty("mobs", mobs);
        m.addProperty("ticks", ticks);
        m.addProperty("opseed", opseed);
        m.addProperty("nbtEvery", nbtEvery);
        m.addProperty("order", "the chunk provider's own insertion order, clipped to the box x " + boxX0 + ".." + boxX1
            + ", z " + boxZ0 + ".." + boxZ1 + " (the region is x " + x0 + ".." + x1 + ", z " + z0 + ".." + z1 + ")");
        m.addProperty("pad", pad);
        m.addProperty("box", "x " + boxX0 + ".." + boxX1 + ", z " + boxZ0 + ".." + boxZ1 + ": every chunk in it is loaded on both sides; outside it the oracle's world also holds the spawn area, which the native does not load, and no entity may go there (the run aborts if one does)");
        m.add("loaded", loaded);
        JsonArray compare = new JsonArray();

        for (int lx = cx - radius; lx <= cx + radius; ++lx)
        {
            for (int lz = cz - radius; lz <= cz + radius; ++lz)
            {
                JsonArray pair = new JsonArray();
                pair.add(new JsonPrimitive(lx));
                pair.add(new JsonPrimitive(lz));
                compare.add(pair);
            }
        }

        m.add("compare", compare);
        m.addProperty("compare_note", "the final block compare covers the platform's own chunks; the ring chunks are loaded only so no query leaves the loaded set, and in the Nether they hold the native world's overworld-generated terrain");
        m.addProperty("kinds", "0 Slime, 1 MagmaCube; the spawn record's kind 2 is the probe player");
        m.addProperty("shapes_layout", "16 bytes per placement, in placement order: x int32 LE, y int32 LE, z int32 LE, id uint16 LE, meta uint8, pad uint8");
        m.addProperty("platform", "the 3x3-chunk area flattened to platformY: per column x-major, for y from heightValue(x, z) to platformY - 1 setBlock(id " + body + ", meta 0, flags 2) and at platformY setBlock(id " + top + "); then a wall three high (id " + wall + ") along the four edges of the area at platformY+1..platformY+3");
        m.addProperty("spawn_layout", SPAWN_BYTES + " bytes per spawn, in spawn order: spawn_index int32 LE, entity_id int32 LE, kind int32 LE (0 slime, 1 magma cube, 2 player), size int32 LE, x, y, z double LE (after setLocationAndAngles), rotationYaw float32 LE, rotationPitch float32 LE, motionX, motionY, motionZ double LE, is_player int32 LE");
        m.addProperty("spawn_draws", "spawn 0 is the player at (bx0 + width/2 + 0.5, platformY, bz0 + width/2 + 0.5). Per mob slot s (0-based) from Random(opseed): x/z/y as (s % 3 == 0: x = playerX + 1 + nextInt(3) + 0.5, z = playerZ + 1 + nextInt(3) + 0.5, y = platformY + 1; s % 3 == 1: x = bx0 + 4 + nextInt(width - 8) + 0.5, z = bz0 + 4 + nextInt(width - 8) + 0.5, y = platformY + 22 + nextInt(8); else x = bx0 + 4 + nextInt(width - 8) + 0.5, z = bz0 + 4 + nextInt(width - 8) + 0.5, y = platformY + 1), then yaw = nextFloat() * 360. Each mob's size is 1 << (s / perSize): perSize mobs of size 1, then perSize of size 2, then perSize of size 4. Then: the kind's constructor (EntitySlime: rand.nextInt(3) for the natural size, then rand.nextInt(20) + 10 for the jump delay; EntityMagmaCube draws nothing extra), setSlimeSize(the forced size), onSpawnWithEgg(null) (getEntityAttribute(followRange) gaussian from the entity's own Random), spawnEntityInWorld.");
        m.addProperty("tick_loop", "per tick t: the probe's scheduled damage events (dim != 0 only: for k in order, the magma cube at list index 1 + 10*k + 6 takes attackEntityFrom(probe generic, 1000) at tick 60 + (k % 5) * 40, logged in events.txt.gz); then for (i = 0; i < list.size(); ++i) { skip when isDead; World.updateEntityWithOptionalForce(e, true); absorb every entity the world's loadedEntityList gained during the tick to the end of the list; when the entity just died, the chunk's removeEntity by (chunkCoordX, chunkCoordZ) when addedToChunk and the chunk exists, then list.remove(i--), then the removal line; } then one record per live entity in the list's order (list[0] is the player), then the player record, then the full NBT block when nbtEvery cuts one, then the digest line");
        m.addProperty("rec_layout", REC_BYTES + " bytes per record, in list order, per tick: " +
            "spawn_index int32, entity_id int32, kind int32 (0 slime, 1 magma cube, -1 item), " +
            "nbt_hash uint64 (FNV-1a over the canonical NBT text of entity.writeToNBT(new NBTTagCompound())), " +
            "ticks_existed int32, entity_age int32, slime_jump_delay int32, move_forward float32, move_strafing float32, " +
            "rotation_yaw_head float32, render_yaw_offset float32, squish_amount float32, squish_factor float32, prev_squish_factor float32, " +
            "living_sound_time int32, jump_ticks int32, hurt_resistant_time int32, max_hurt_resistant_time int32, recently_hit int32, " +
            "last_damage float32, flags int32 (bit 0 addedToChunk, bit 1 onGround, bit 2 isJumping, bit 3 isAirBorne, bit 4 inWater, bit 5 isCollidedHorizontally), " +
            "rand_state uint64 (Det.state of the entity's own Random), slime_size int32, hurt_time int32, max_hurt_time int32, pad int32");
        m.addProperty("player_layout", PLAYER_BYTES + " bytes, one record after the entity records each tick: " +
            "entity_id int32, health float32, prev_health float32, absorption float32, last_damage float32, " +
            "entity_age int32, death_time int32, hurt_time int32, max_hurt_time int32, hurt_resistant_time int32, max_hurt_resistant_time int32, recently_hit int32, " +
            "food_level int32, food_saturation float32, food_exhaustion float32, food_timer int32, prev_food_level int32 (offsets 0..64) " +
            "pos_x, pos_y, pos_z double at 68, 76, 84; motion_x, motion_y, motion_z double at 92, 100, 108; " +
            "rotation_yaw float32 at 116, rotation_pitch float32 at 120, flags int32 at 124 (bit 0 onGround, bit 1 addedToChunk, bit 2 isAirBorne, bit 3 inWater), rand_state uint64 at 128, pad to " + PLAYER_BYTES);
        m.addProperty("ticks_layout", "per tick: tick int32 LE, count int32 LE, then count records, then one player record");
        m.addProperty("nbt64", "nbt64.txt.gz: the first eight ticks, every " + nbtEvery + "th tick and the final tick, line t <tick>, then one line per live entity in list order: <spawn_index> <the canonical NBT text of entity.writeToNBT(new NBTTagCompound())>");
        m.addProperty("spawns_txt", "spawns.txt.gz: one line per initial spawn: <spawn_index> <entity_id> <kind name> <size> <canonical NBT at spawn>");
        m.addProperty("removals", "removals.txt.gz, one line per removal at the tick it happened: <tick> <spawn_index> <entity_id> <health as hex float bits or -1>");
        m.addProperty("events", "events.txt.gz: 'damage <tick> <spawn_index> <entity_id>' for a probe damage event; 'playerhit <tick> <spawn_index> <health before hex float> <after hex float>' when the player lost health");
        m.addProperty("digest", "digest.txt.gz, one line per tick: t <tick>, then per role 0..3: role <r> seeder <hex> math <hex> split <hex>, then nextId <the OTHER role's next id>, worldRand <world.rand state hex>, worldRandGauss <0 or 1> the pending nextGaussian flag");
        m.addProperty("player_inventory", "the probe player's 36 main inventory slots each hold a stone stack so no item drop can be picked up (EntityItem.onCollideWithPlayer is then a no-op); the armor slots are empty, so getTotalArmorValue is 0");
        m.addProperty("start_end", "start.txt and end.txt, the DetProbe snapshot format before the first spawn and after the last tick");
        m.addProperty("final_layout", "final.bin.gz, per loaded chunk in load order: cx int32 BE, cz int32 BE, ids uint16 LE (65536), metas uint8 (65536), sky light uint8 (65536), then heightMap 256 int32 LE, precipitationHeightMap 256 int32 LE, heightMapMinimum int32 LE, section mask uint16 LE");
        StringBuilder hist = new StringBuilder();

        for (int id = 0; id < dropsItem.length; ++id)
        {
            if (dropsItem[id] > 0) hist.append(id).append('x').append(dropsItem[id]).append(' ');
        }

        m.addProperty("drop_ids", "item entities spawned during the run, by item id: " + hist.toString());
        m.addProperty("counts", "mobs " + mobs + " (slime " + kindSize[0] + ", magma cube " + kindSize[1] + "), sizes (1: " + spawnedSize[0]
            + ", 2: " + spawnedSize[1] + ", 4: " + spawnedSize[2] + "), splits " + splitsChildren[0] + "+" + splitsChildren[1]
            + " children (slime, magma), deaths " + deathsKind[0] + "+" + deathsKind[1] + " (by size 1: " + deathsSize[0] + ", 2: "
            + deathsSize[1] + ", 4: " + deathsSize[2] + "), drops " + dropCount + ", orbs " + orbCount + ", max list " + maxList
            + ", first death tick " + firstDeathTick + ", first split tick " + firstSplitTick + ", player hits " + damageToPlayer
            + ", lowest player health " + playerHealthMin);
        PrintWriter mw = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "manifest.json")), "UTF-8"));
        mw.println(m.toString());
        mw.close();

        JsonObject res = new JsonObject();
        res.addProperty("dir", dir.getPath());
        res.addProperty("mobs", mobs);
        res.addProperty("ticks", ticks);
        res.addProperty("shapes", shapeCount);
        res.addProperty("splits", splitsChildren[0] + splitsChildren[1]);
        res.addProperty("deaths", deathsKind[0] + deathsKind[1]);
        res.addProperty("drops", dropCount);
        res.addProperty("maxList", maxList);
        res.addProperty("playerHits", damageToPlayer);
        res.addProperty("playerHealthMin", playerHealthMin);
        res.addProperty("firstDeathTick", firstDeathTick);
        res.addProperty("firstSplitTick", firstSplitTick);
        return res;
    }

    // -------------------------------------------------------------- helpers

    static DamageSource causeProbeDamage()
    {
        return DamageSource.generic;
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
        if (e instanceof EntityPlayer) return 2;
        if (e instanceof EntityMagmaCube) return 1;
        if (e instanceof EntitySlime) return 0;
        return -1;
    }

    static ItemStack itemStackOf(Entity e)
    {
        if (e instanceof net.minecraft.entity.item.EntityItem) return ((net.minecraft.entity.item.EntityItem)e).getEntityItem();
        return null;
    }

    static String canon(Entity e)
    {
        NBTTagCompound tag = new NBTTagCompound();
        e.writeToNBT(tag);
        return StructuresProbe.canon(tag).toString();
    }

    /** Entity.writeToNBT, canonical text, the nbtjson.c form. */
    static long nbtHash(Entity e) throws Exception
    {
        byte[] b = canon(e).getBytes("UTF-8");
        long h = FNV_OFFSET;

        for (int i = 0; i < b.length; ++i) h = (h ^ (b[i] & 255)) * FNV_PRIME;

        return h;
    }

    /** World.updateEntityWithOptionalForce(e, true). */
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

    /** One entity record. */
    static void writeState(byte[] b, Entity e, Map<Entity, Integer> spawnIndex, WorldServer ws) throws Exception
    {
        int p = 0;
        EntityLiving lv = e instanceof EntityLiving ? (EntityLiving)e : null;
        EntityLivingBase lb = e instanceof EntityLivingBase ? (EntityLivingBase)e : null;
        EntitySlime sl = e instanceof EntitySlime ? (EntitySlime)e : null;

        p = le32(b, p, spawnIndex.get(e).intValue());
        p = le32(b, p, e.getEntityId());
        p = le32(b, p, kindOf(e));
        p = le64(b, p, nbtHash(e));
        p = le32(b, p, e.ticksExisted);
        p = le32(b, p, lb != null ? intField(lb, "entityAge") : 0);
        p = le32(b, p, sl != null ? intField(sl, "slimeJumpDelay") : 0);
        p = le32(b, p, lb != null ? Float.floatToRawIntBits(lb.moveForward) : 0);
        p = le32(b, p, lb != null ? Float.floatToRawIntBits(lb.moveStrafing) : 0);
        p = le32(b, p, lb != null ? Float.floatToRawIntBits(lb.rotationYawHead) : 0);
        p = le32(b, p, lb != null ? Float.floatToRawIntBits(lb.renderYawOffset) : 0);
        p = le32(b, p, sl != null ? Float.floatToRawIntBits(sl.squishAmount) : 0);
        p = le32(b, p, sl != null ? Float.floatToRawIntBits(sl.squishFactor) : 0);
        p = le32(b, p, sl != null ? Float.floatToRawIntBits(sl.prevSquishFactor) : 0);
        p = le32(b, p, lv != null ? lv.livingSoundTime : 0);
        p = le32(b, p, lb != null ? intField(lb, "jumpTicks") : 0);
        p = le32(b, p, lb != null ? lb.hurtResistantTime : 0);
        p = le32(b, p, lb != null ? lb.maxHurtResistantTime : 0);
        p = le32(b, p, lb != null ? intField(lb, "recentlyHit") : 0);
        p = le32(b, p, lb != null ? Float.floatToRawIntBits(floatField(lb, "lastDamage")) : 0);
        int flags = (e.addedToChunk ? 1 : 0) | (e.onGround ? 2 : 0)
            | ((lb != null && boolField(lb, "isJumping")) ? 4 : 0) | (e.isAirBorne ? 8 : 0) | (boolField(e, "inWater") ? 16 : 0)
            | (e.isCollidedHorizontally ? 32 : 0);
        p = le32(b, p, flags);
        p = le64(b, p, Det.state(e instanceof EntityLiving ? ((EntityLiving)e).getRNG() : entityRand(e)));
        p = le32(b, p, sl != null ? sl.getSlimeSize() : 0);
        p = le32(b, p, lb != null ? lb.hurtTime : 0);
        p = le32(b, p, lb != null ? lb.maxHurtTime : 0);
        p = le32(b, p, 0);

        if (p != REC_BYTES) throw new IllegalStateException("record wrote " + p + " bytes, the layout says " + REC_BYTES);
    }

    /** The player record. */
    static void writePlayer(byte[] b, WorldServer ws, Entity e) throws Exception
    {
        EntityPlayer pl = (EntityPlayer)e;
        int p = 0;
        p = le32(b, p, e.getEntityId());
        p = le32(b, p, Float.floatToRawIntBits(pl.getHealth()));
        p = le32(b, p, Float.floatToRawIntBits(pl.prevHealth));
        p = le32(b, p, Float.floatToRawIntBits(pl.getAbsorptionAmount()));
        p = le32(b, p, Float.floatToRawIntBits(floatField(pl, "lastDamage")));
        p = le32(b, p, intField(pl, "entityAge"));
        p = le32(b, p, pl.deathTime);
        p = le32(b, p, pl.hurtTime);
        p = le32(b, p, pl.maxHurtTime);
        p = le32(b, p, pl.hurtResistantTime);
        p = le32(b, p, pl.maxHurtResistantTime);
        p = le32(b, p, intField(pl, "recentlyHit"));
        p = le32(b, p, pl.getFoodStats().getFoodLevel());
        p = le32(b, p, Float.floatToRawIntBits(pl.getFoodStats().getSaturationLevel()));
        p = le32(b, p, Float.floatToRawIntBits(((Float)findField(pl.getFoodStats(), "foodExhaustionLevel").get(pl.getFoodStats())).floatValue()));
        p = le32(b, p, intField(pl.getFoodStats(), "foodTimer"));
        p = le32(b, p, pl.getFoodStats().getPrevFoodLevel());
        p = le64(b, p, Double.doubleToRawLongBits(pl.posX));      /* 68 */
        p = le64(b, p, Double.doubleToRawLongBits(pl.posY));      /* 76 */
        p = le64(b, p, Double.doubleToRawLongBits(pl.posZ));      /* 84 */
        p = le64(b, p, Double.doubleToRawLongBits(pl.motionX));   /* 92 */
        p = le64(b, p, Double.doubleToRawLongBits(pl.motionY));   /* 100 */
        p = le64(b, p, Double.doubleToRawLongBits(pl.motionZ));   /* 108 */
        p = le32(b, p, Float.floatToRawIntBits(pl.rotationYaw));  /* 116 */
        p = le32(b, p, Float.floatToRawIntBits(pl.rotationPitch));/* 120 */
        int flags = (pl.onGround ? 1 : 0) | (e.addedToChunk ? 2 : 0) | (e.isAirBorne ? 4 : 0) | (boolField(e, "inWater") ? 8 : 0);
        p = le32(b, p, flags);                                    /* 124 */
        p = le64(b, p, Det.state(pl.getRNG()));                   /* 128 */
        p = le32(b, p, 0);
        p = le32(b, p, 0);

        if (p != PLAYER_BYTES) throw new IllegalStateException("player record wrote " + p + " bytes, the layout says " + PLAYER_BYTES);
    }

    static Random entityRand(Entity e) throws Exception
    {
        return (Random)findField(e, "rand").get(e);
    }

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
        b.append(" worldRandGauss ").append(boolField(wr, "haveNextNextGaussian") ? 1 : 0);
        w.println(b.toString());
    }

    static float floatField(Object o, String name)
    {
        try
        {
            return findField(o, name).getFloat(o);
        }
        catch (Exception e)
        {
            throw new RuntimeException(e);
        }
    }

    static boolean boolField(Object o, String name)
    {
        try
        {
            return findField(o, name).getBoolean(o);
        }
        catch (Exception e)
        {
            throw new RuntimeException(e);
        }
    }

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
        b.append("worldRand ").append(hex(Det.state((Random)wr))).append(' ').append(boolField(wr, "haveNextNextGaussian") ? 1 : 0);
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