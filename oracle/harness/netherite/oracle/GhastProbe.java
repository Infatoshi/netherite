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
import net.minecraft.entity.item.EntityItem;
import net.minecraft.entity.item.EntityXPOrb;
import net.minecraft.entity.monster.EntityGhast;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.entity.player.EntityPlayerMP;
import net.minecraft.network.NetworkManager;
import net.minecraft.network.NetHandlerPlayServer;
import net.minecraft.server.management.ItemInWorldManager;
import net.minecraft.server.MinecraftServer;
import net.minecraft.entity.projectile.EntityLargeFireball;
import net.minecraft.item.ItemStack;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.util.ChunkCoordinates;
import net.minecraft.block.material.Material;
import net.minecraft.init.Blocks;
import net.minecraft.util.MathHelper;
import net.minecraft.world.WorldServer;

/**
 * The ghast probe: EntityGhast and the EntityLargeFireball it fires, ticking
 * in a raw Nether region, the reference for the native port of EntityFlying's
 * movement (moveEntityWithHeading in air, no gravity), the ghast's
 * updateEntityActionState (the wandering waypoint and isCourseTraversable),
 * the player targeting (getClosestVulnerablePlayerToEntity, canEntityBeSeen),
 * the attack counter and the fireball's explosion, and the ghast's damage and
 * death.
 *
 * One case per run: raw Nether chunks (Probe.rawChunks, population off), a
 * probe player standing on the ground in the open (one run inside a
 * netherrack shell so no line of sight reaches it), and the ghasts spawned the
 * way natural spawning does it: EntityList.createEntityByName,
 * setLocationAndAngles, onSpawnWithEgg(null), world.spawnEntityInWorld, at
 * open-air positions the setup searches the region for (a 5x5 footprint with
 * solid ground at y-1 and air for four rows, the first y from 45 up).
 *
 * The tick is World.updateEntities' regular pass over the probe's own list
 * (which mirrors loadedEntityList minus the player): an entity dead at its
 * visit is skipped, the entities a tick spawned (the fireballs, the death
 * drops) are absorbed at the end of the list and ticked in the same pass, and
 * a dead entity leaves the list and its chunk the way the world removes it.
 * The probe player is spawned into the world but never ticked: the ghasts see
 * it through playerEntities, the fireballs and explosions damage it, and its
 * health, hurt times and knockback motion are recorded every tick.
 *
 * Every live entity is recorded after every tick: a 64-bit FNV-1a hash of the
 * canonical NBT text (StructuresProbe.canon of entity.writeToNBT), plus the
 * state the next tick reads that NBT does not carry (the ghast's waypoint,
 * attack counters, cooldowns, targeting and dataWatcher 16, the fireball's
 * acceleration and tile fields, the item's age and delay, each entity's own
 * Random state). Every 64th tick and the final tick carry the full canonical
 * NBT text. The digest line and the start/end snapshots carry the Det state,
 * including the world's Random (the explosions' rays and the drops draw from
 * it).
 *
 * Output DIR/manifest.json, DIR/shapes.bin, DIR/spawns.bin, DIR/ticks.bin.gz,
 * DIR/digest.txt.gz, DIR/nbt64.txt.gz, DIR/spawns.txt.gz, DIR/removals.txt.gz,
 * DIR/start.txt, DIR/end.txt, DIR/final.bin.gz.
 */
public final class GhastProbe
{
    /** The fixed per-entity state record. Keep in step with test_ghasts.c. */
    static final int ENT_STATE_BYTES = 232;
    static final int SPAWN_BYTES = 72;
    static final int PLAYER_STATE_BYTES = 112;

    static final int KIND_GHAST = 0, KIND_PLAYER = 1, KIND_FIREBALL = 2, KIND_ITEM = 3, KIND_ORB = 4;

    static final long FNV_OFFSET = 0xcbf29ce484222325L;
    static final long FNV_PRIME = 0x100000001b3L;

    private static EntityPlayer playerEntity;

    private GhastProbe() {}

    /**
     * The probe player: an EntityPlayerMP constructed the way
     * ServerConfigurationManager does it (MinecraftServer, the world, the
     * offline profile, an ItemInWorldManager) with a dummy NetworkManager, so
     * the SFX and explosion packet fan-outs that cast playerEntities to
     * EntityPlayerMP find a player with a net handler to queue into. The
     * Nether's provider.hasNoSky is true, so the constructor draws nothing.
     */
    public static class ProbePlayer extends EntityPlayerMP
    {
        public ProbePlayer(WorldServer w) throws Exception
        {
            super(MinecraftServer.getServer(), w, new com.mojang.authlib.GameProfile(null, "Player"), new ItemInWorldManager(w));
            new NetHandlerPlayServer(MinecraftServer.getServer(), new NetworkManager(false), this);
            /* EntityPlayerMP's 60-tick post-join invulnerability: the probe
             * never ticks this player, so the countdown would never run; the
             * fireballs and explosions must land. */
            Field f = findField(this, "field_147101_bU");
            f.setInt(this, 0);
        }
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
                    result[0] = dump(server, cmd);
                }
                catch (Exception e)
                {
                    error[0] = e;
                }
            }
        }, "Oracle GhastProbe");
        t.start();
        t.join();
        if (error[0] != null) throw error[0];
        return result[0];
    }

    static JsonObject dump(IntegratedServer server, JsonObject cmd) throws Exception
    {
        if (server.worldServers[1] == null) throw new IllegalStateException("no nether world");

        WorldServer ws = server.worldServers[1];
        int cx = cmd.has("cx") ? cmd.get("cx").getAsInt() : 200;
        int cz = cmd.has("cz") ? cmd.get("cz").getAsInt() : 200;
        int radius = cmd.has("radius") ? cmd.get("radius").getAsInt() : 3;
        int ring = cmd.has("ring") ? cmd.get("ring").getAsInt() : 6;
        int ghasts = cmd.has("ghasts") ? cmd.get("ghasts").getAsInt() : 44;
        int ticks = cmd.has("ticks") ? cmd.get("ticks").getAsInt() : 2400;
        long opseed = cmd.has("opseed") ? cmd.get("opseed").getAsLong() : 13L;
        int nbtEvery = cmd.has("nbtEvery") ? cmd.get("nbtEvery").getAsInt() : 64;
        boolean walls = cmd.has("walls") && cmd.get("walls").getAsBoolean();
        File dir = new File(cmd.get("out").getAsString());
        dir.mkdirs();

        Trace.restart();   /* the trace holds this run, not the world's startup */

        long seed = ws.getSeed();
        Probe.rawChunks = true;

        // Load the region first (cx-major), then search it for open cavern air.
        // The whole tick stays inside the loaded set: no chunk generation can
        // happen (the checkChunksExist guard would stop the ticks first), and
        // the final count assert proves none happened.
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

        // ------------------------------------------------------------ shapes
        OutputStream shapesRaw = new BufferedOutputStream(new FileOutputStream(new File(dir, "shapes.bin")), 1 << 16);
        byte[] shape = new byte[16];
        int shapeCount = 0;

        // The player's position first: a 7x7 footprint with solid ground at
        // y-1 and air for y..y+5 (the open pocket the run happens in). The
        // ghasts then draw offsets around it, so the player is within reach of
        // every ghast (the attack needs the target within 64 blocks) and a
        // fireball has open air to fly through.
        // The player's position first (a 5x5 footprint with solid ground at
        // y-1 and air for four rows), then the cavern around it is carved
        // open, then the ghasts draw offsets inside the carved space, so a
        // fireball always has open air to fly through.
        Random r = new Random(opseed);
        List<int[]> positions = new ArrayList<int[]>();
        List<Float> yaws = new ArrayList<Float>();

        // the player
        {
            boolean found = false;

            for (int tries = 0; tries < 600 && !found; ++tries)
            {
                int px = bx0 + r.nextInt(width);
                int pz = bz0 + r.nextInt(width);
                int py = findOpen(ws, px, pz, 2, 4);

                if (py >= 0)
                {
                    found = true;
                    positions.add(new int[] {px, py, pz});
                }
            }

            if (!found) throw new IllegalStateException("no open air position for the probe player");
            yaws.add(r.nextFloat() * 360.0F);
        }

        int[] playerPos = positions.get(0);

        // the cavern: the (2*AR+1)^2 footprint around the player cleared to
        // air from its y to y + ARROWS, the floor left as the terrain had it
        final int AR = 15, ARFLOOR = 8, ARROWS = 9;

        for (int x = playerPos[0] - AR; x <= playerPos[0] + AR; ++x)
        {
            for (int z = playerPos[2] - AR; z <= playerPos[2] + AR; ++z)
            {
                for (int y = playerPos[1] - ARFLOOR; y <= playerPos[1] + ARROWS; ++y)
                {
                    if (ws.getBlock(x, y, z) == Blocks.air) continue;

                    ws.setBlock(x, y, z, Blocks.air, 0, 2);
                    le32(shape, 0, x);
                    le32(shape, 4, y);
                    le32(shape, 8, z);
                    shape[12] = 0;
                    shape[13] = 0;
                    shape[14] = 0;
                    shape[15] = 0;
                    shapesRaw.write(shape, 0, 16);
                    ++shapeCount;
                }
            }
        }

        if (walls)
        {
            // A hollow netherrack shell around the player, so no line of
            // sight reaches it from outside.
            for (int dx = -7; dx <= 7; ++dx)
            {
                for (int dz = -7; dz <= 7; ++dz)
                {
                    for (int dy = -3; dy <= 6; ++dy)
                    {
                        /* solid: no ray can reach the player's eye through it */

                        int x = playerPos[0] + dx, y = playerPos[1] + dy, z = playerPos[2] + dz;
                        ws.setBlock(x, y, z, Block.getBlockById(87), 0, 2);
                        le32(shape, 0, x);
                        le32(shape, 4, y);
                        le32(shape, 8, z);
                        shape[12] = (byte)87;
                        shape[13] = 0;
                        shape[14] = 0;
                        shape[15] = 0;
                        shapesRaw.write(shape, 0, 16);
                        ++shapeCount;
                    }
                }
            }
        }

        // the ghasts: offsets inside the carved space, four blocks below the
        // player in mid-air (the carve is air down to y - 8 and up to y + 9),
        // so the fireballs rise into the player and no ghast's 4x4 box
        // hovers at the player's level to shield it
        for (int i = 1; i <= ghasts; ++i)
        {
            boolean found = false;
            int px = 0, py = 0, pz = 0;

            for (int tries = 0; tries < 600 && !found; ++tries)
            {
                px = playerPos[0] + r.nextInt(2 * AR - 5) - (AR - 3);
                pz = playerPos[2] + r.nextInt(2 * AR - 5) - (AR - 3);

                if (walls && Math.abs(px - playerPos[0]) <= 8 && Math.abs(pz - playerPos[2]) <= 8)
                {
                    continue;
                }

                py = airAt(ws, px, pz, playerPos[1] - 4, 2, 5) ? playerPos[1] - 4 : -1;

                if (py >= 0)
                {
                    found = true;
                    positions.add(new int[] {px, py, pz});
                }
            }

            if (!found) throw new IllegalStateException("no open air position for ghast " + i);
            yaws.add(r.nextFloat() * 360.0F);
        }

        shapesRaw.close();

        // ------------------------------------------------------------- start
        writeDetState(new File(dir, "start.txt"), seed, ws);

        // ------------------------------------------------------------ spawns
        Map<Entity, Integer> spawnIndex = new IdentityHashMap<Entity, Integer>();
        List<Entity> list = new ArrayList<Entity>();
        byte[] spawnBuf = new byte[SPAWN_BYTES];
        OutputStream spawnsOut = new BufferedOutputStream(new FileOutputStream(new File(dir, "spawns.bin")), 1 << 16);
        GZIPOutputStream spawnsTxtGz = new GZIPOutputStream(new FileOutputStream(new File(dir, "spawns.txt.gz")), 1 << 16);
        PrintWriter sw = new PrintWriter(new OutputStreamWriter(spawnsTxtGz, "UTF-8"));

        int entitiesBefore = ws.loadedEntityList.size();

        // The player: spawn_index 0, never ticked, in playerEntities.
        {
            EntityPlayer p = new ProbePlayer(ws);
            p.setLocationAndAngles(playerPos[0] + 0.5D, playerPos[1], playerPos[2] + 0.5D, yaws.get(0), 0.0F);

            if (!ws.spawnEntityInWorld(p)) throw new IllegalStateException("the probe player did not spawn");
            playerEntity = p;

            int pi = 0;
            pi = le32(spawnBuf, pi, 0);
            pi = le32(spawnBuf, pi, p.getEntityId());
            pi = le32(spawnBuf, pi, KIND_PLAYER);
            pi = le64(spawnBuf, pi, Double.doubleToRawLongBits(p.posX));
            pi = le64(spawnBuf, pi, Double.doubleToRawLongBits(p.posY));
            pi = le64(spawnBuf, pi, Double.doubleToRawLongBits(p.posZ));
            pi = le32(spawnBuf, pi, Float.floatToRawIntBits(p.rotationYaw));
            pi = le32(spawnBuf, pi, Float.floatToRawIntBits(p.rotationPitch));
            pi = le32(spawnBuf, pi, Float.floatToRawIntBits(p.getHealth()));
            spawnBuf[pi++] = 1;
            while (pi < SPAWN_BYTES) spawnBuf[pi++] = 0;
            spawnsOut.write(spawnBuf, 0, SPAWN_BYTES);
            sw.println(0 + " " + p.getEntityId() + " Player " + hex(Float.floatToRawIntBits(p.getHealth())));
        }

        for (int i = 1; i <= ghasts; ++i)
        {
            int[] pos = positions.get(i);
            Entity e = EntityList.createEntityByName("Ghast", ws);

            if (e == null) throw new IllegalStateException("no entity for Ghast");

            /* the ghast's position is off the .5 grid: a ghast on the exact
             * diagonal from the player aims its eye ray through block
             * corners, which the ray trace's boundary walk skips */
            e.setLocationAndAngles(pos[0] + 0.437D, pos[1], pos[2] + 0.813D, yaws.get(i), 0.0F);
            ((EntityLiving)e).onSpawnWithEgg(null);

            if (!ws.spawnEntityInWorld(e)) throw new IllegalStateException("ghast " + i + " did not spawn");

            spawnIndex.put(e, Integer.valueOf(i));
            list.add(e);

            int pi = 0;
            pi = le32(spawnBuf, pi, i);
            pi = le32(spawnBuf, pi, e.getEntityId());
            pi = le32(spawnBuf, pi, KIND_GHAST);
            pi = le64(spawnBuf, pi, Double.doubleToRawLongBits(e.posX));
            pi = le64(spawnBuf, pi, Double.doubleToRawLongBits(e.posY));
            pi = le64(spawnBuf, pi, Double.doubleToRawLongBits(e.posZ));
            pi = le32(spawnBuf, pi, Float.floatToRawIntBits(e.rotationYaw));
            pi = le32(spawnBuf, pi, Float.floatToRawIntBits(e.rotationPitch));
            spawnBuf[pi++] = 0;
            while (pi < SPAWN_BYTES) spawnBuf[pi++] = 0;
            spawnsOut.write(spawnBuf, 0, SPAWN_BYTES);
            sw.println(i + " " + e.getEntityId() + " Ghast " + canon(e));
        }

        spawnsOut.close();
        sw.close();
        spawnsTxtGz.close();

        if (ws.loadedEntityList.size() != entitiesBefore + list.size() + 1)
        {
            throw new IllegalStateException("the world holds " + ws.loadedEntityList.size() + " entities, "
                + entitiesBefore + " before the setup and " + (list.size() + 1) + " spawned (one is the player)");
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

        int absorbed = ws.loadedEntityList.size();
        int chunksBefore = ws.theChunkProviderServer.getLoadedChunkCount();
        int fireballsFired = 0;
        int itemsSpawned = 0;
        int orbsSpawned = 0;
        int playerDamageTicks = 0;
        float playerPrevHealth = playerEntity.getHealth();
        int ghastDeaths = 0;
        int maxList = list.size();
        byte[] rec = new byte[ENT_STATE_BYTES];
        byte[] pRec = new byte[PLAYER_STATE_BYTES];

        for (int t = 0; t < ticks; ++t)
        {
            // World.updateEntities' regular pass over the probe's list
            for (int i = 0; i < list.size(); ++i)
            {
                Entity e = list.get(i);

                if (!e.isDead)
                {
                    updateEntity(ws, e);
                }

                // the entities this tick spawned: the fireballs, the death drops
                while (absorbed < ws.loadedEntityList.size())
                {
                    Entity n = (Entity)ws.loadedEntityList.get(absorbed);
                    int kind = kindOf(n);

                    if (kind == KIND_FIREBALL) ++fireballsFired;
                    else if (kind == KIND_ITEM) ++itemsSpawned;
                    else if (kind == KIND_ORB) ++orbsSpawned;

                    spawnIndex.put(n, Integer.valueOf(list.size()));
                    list.add(n);
                    absorbed++;
                }

                if (e.isDead)
                {
                    int si = spawnIndex.get(e).intValue();
                    int kind = kindOf(e);

                    if (kind == KIND_GHAST) ++ghastDeaths;

                    if (e.addedToChunk && ws.theChunkProviderServer.chunkExists(e.chunkCoordX, e.chunkCoordZ))
                    {
                        ws.getChunkFromChunkCoords(e.chunkCoordX, e.chunkCoordZ).removeEntity(e);
                    }

                    list.remove(i--);

                    float health = e instanceof EntityLivingBase ? ((EntityLivingBase)e).getHealth() : -1.0F;

                    rw.println(t + " " + si + " " + e.getEntityId() + " " + kind + " "
                        + (health >= 0.0F ? hex(Float.floatToRawIntBits(health)) : "-1") + " " + intField(e, "fire"));
                }
            }

            if (playerEntity.getHealth() != playerPrevHealth) ++playerDamageTicks;
            playerPrevHealth = playerEntity.getHealth();

            // the tick header (tick, count), the player's record, then the
            // state of every live entity in list order
            byte[] head = new byte[8];
            int hp = 0;
            hp = le32(head, hp, t);
            hp = le32(head, hp, list.size());
            ticksOut.write(head, 0, 8);

            writePlayerState(pRec, t);
            ticksOut.write(pRec, 0, PLAYER_STATE_BYTES);

            for (int i = 0; i < list.size(); ++i)
            {
                int n = writeState(rec, t, list.get(i), spawnIndex);

                if (n != recordSize(kindOf(list.get(i)))) throw new IllegalStateException("record for kind "
                    + kindOf(list.get(i)) + " wrote " + n + " bytes, the layout says " + recordSize(kindOf(list.get(i))));

                ticksOut.write(rec, 0, n);
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
        m.addProperty("kind", "ghasts");
        m.addProperty("dim", -1);
        m.addProperty("cx", cx);
        m.addProperty("cz", cz);
        m.addProperty("radius", radius);
        m.addProperty("ring", ring);
        m.addProperty("ghasts", ghasts);
        m.addProperty("ticks", ticks);
        m.addProperty("opseed", opseed);
        m.addProperty("nbtEvery", nbtEvery);
        m.addProperty("walls", walls);
        m.addProperty("order", "cx-major: for cx in " + x0 + ".." + x1 + ", for cz in " + z0 + ".." + z1);
        m.add("loaded", loaded);
        m.addProperty("kinds", "0 Ghast, 1 the probe player (not ticked), 2 EntityLargeFireball, 3 item, 4 xp orb");
        m.addProperty("shapes_layout", "16 bytes per placement, in placement order: x int32 LE, y int32 LE, z int32 LE, id uint16 LE, meta uint8, pad uint8 (the walls run's netherrack shell only; an empty stream otherwise)");
        m.addProperty("spawn_layout", SPAWN_BYTES + " bytes per spawn, in spawn order (the player first): spawn_index int32 LE, entity_id int32 LE, kind int32 LE, x, y, z double LE (after setLocationAndAngles), rotationYaw float32 LE, rotationPitch float32 LE, health float32 LE (the player only), pad");
        m.addProperty("spawn_draws", "per entity i (the player first) from Random(opseed), in order: x = bx0 + nextInt(width), z = bz0 + nextInt(width) (a walls run redraws any ghast position within 6 blocks of the player, which consumes the draws again), a y search that draws nothing (the first y in 45..116 where the 5x5 footprint at (x-2..x+2, z-2..z+2) has non-air at y-1 and air for y..y+3; a position with no such y is redrawn, up to 400 tries), then yaw = nextFloat()*360");
        m.addProperty("player", "the probe player is a bare EntityPlayer (Player, the offline UUID) spawned at spawn_index 0 and never ticked: the ghasts see it through playerEntities, the fireballs and explosions damage it, and its state is recorded every tick");
        m.addProperty("tick_loop", "per tick t: for (i = 0; i < list.size(); ++i) { if (!e.isDead) World.updateEntityWithOptionalForce(e, true) with the checkChunksExist guard; absorb every entity the world's loadedEntityList gained during the tick to the end of the list (fireballs, death drops); when e just died: the chunk's removeEntity by (chunkCoordX, chunkCoordZ) when addedToChunk and the chunk exists, then list.remove(i--) and the removal line; } then the tick header (tick, count), the player's record, then one state record per live entity in the list's order, then the full NBT block when nbtEvery cuts one, then the digest line");
        m.addProperty("ent_state_layout", "per record, in list order, per tick: spawn_index int32 LE, entity_id int32 LE, kind int32 LE, then per kind - "
            + "ghast (kind 0): nbt_hash uint64, x, y, z, motionX, motionY, motionZ double LE, rotationYaw, rotationPitch, prevRotationYaw, prevRotationPitch, rotationYawHead, renderYawOffset float32 LE, health, prevHealth float32 LE, hurtTime, deathTime, hurtResistantTime int32 LE, lastDamage float32 LE, entityAge, ticksExisted, prevAttackCounter, attackCounter, courseChangeCooldown, aggroCooldown int32 LE, waypointX, waypointY, waypointZ double LE, targeted int32 LE (1 when the target is the player), data16 int32 LE, limbSwing, limbSwingAmount, field_110154_aX, field_70764_aw float32 LE, explosionStrength int32 LE, rand uint64 LE (the entity's own Random state), flags int32 LE (bit 0 addedToChunk, bit 1 onGround, bit 2 dead) - "
            + "fireball (kind 2): nbt_hash uint64, x, y, z, motionX, motionY, motionZ, accelerationX, accelerationY, accelerationZ double LE, rotationYaw, rotationPitch, prevYaw, prevPitch float32 LE, ticksAlive, ticksInAir, tileX, tileY, tileZ, inTile, inGround, explosionPower, fire int32 LE, rand uint64 LE, flags int32 LE (bit 0 addedToChunk, bit 1 onGround, bit 2 dead) - "
            + "item (kind 3): nbt_hash uint64, x, y, z, motionX, motionY, motionZ double LE, rotationYaw, rotationPitch float32 LE, health, age, delay, fire int32 LE, hoverStart float32 LE, stack item, damage, count int32 LE, rand uint64 LE, flags int32 LE (bit 0 addedToChunk, bit 1 onGround, bit 2 dead) - "
            + "orb (kind 4): nbt_hash uint64, x, y, z, motionX, motionY, motionZ double LE, rotationYaw, rotationPitch float32 LE, health, age, xpValue, fire int32 LE, rand uint64 LE, flags int32 LE (bit 0 addedToChunk, bit 1 onGround, bit 2 dead)");
        m.addProperty("player_layout", PLAYER_STATE_BYTES + " bytes after the tick header: entity_id int32 LE, x, y, z, motionX, motionY, motionZ double LE, rotationYaw, rotationPitch float32 LE, health, prevHealth, absorption float32 LE, hurtTime, maxHurtTime, deathTime, hurtResistantTime int32 LE, lastDamage float32 LE, entityAge, isDead int32 LE, rand uint64 LE, pad");
        m.addProperty("ticks_layout", "per tick: tick int32 LE, count int32 LE, the player record (" + PLAYER_STATE_BYTES + " bytes), then count state records");
        m.addProperty("nbt64", "nbt64.txt.gz: the first eight ticks, every " + nbtEvery + "th tick and the final tick, line t <tick>, then one line per live list entity in list order: <spawn_index> <the canonical NBT text of entity.writeToNBT(new NBTTagCompound())>");
        m.addProperty("spawns_txt", "spawns.txt.gz: one line per initial spawn (the player first): <spawn_index> <entity_id> <kind name> <the player's health as float bits, or the canonical NBT at spawn>");
        m.addProperty("removals", "removals.txt.gz, one line per removal at the tick it happened: <tick> <spawn_index> <entity_id> <kind> <health as hex float bits or -1> <fire>");
        m.addProperty("digest", "digest.txt.gz, one line per tick: t <tick>, then per role 0..3: role <r> seeder <hex> math <hex> split <hex>, then nextId <the OTHER role's next id>, worldRand <world.rand state hex>, worldRandGauss <0 or 1> the pending nextGaussian flag");
        m.addProperty("start_end", "start.txt and end.txt, the DetProbe snapshot format (resetSeed, worldSeed, nextId, digest per role, split per registered stream) plus worldRand <state> <pending gaussian flag>, before the first spawn and after the last tick");
        m.addProperty("final_layout", "final.bin.gz, per loaded chunk in load order: cx int32 BE from DataOutputStream.writeInt, cz int32 BE, ids uint16 LE (65536), metas uint8 (65536), heightMap 256 int32 LE, precipitationHeightMap 256 int32 LE, heightMapMinimum int32 LE, section mask uint16 LE");
        m.addProperty("counts", "ghasts " + ghasts + ", fireballs fired " + fireballsFired + ", items spawned " + itemsSpawned
            + ", orbs spawned " + orbsSpawned + ", player damage ticks " + playerDamageTicks + ", ghast deaths " + ghastDeaths
            + ", max list " + maxList);
        PrintWriter mw = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "manifest.json")), "UTF-8"));
        mw.println(m.toString());
        mw.close();

        JsonObject res = new JsonObject();
        res.addProperty("dir", dir.getPath());
        res.addProperty("ghasts", ghasts);
        res.addProperty("ticks", ticks);
        res.addProperty("shapes", shapeCount);
        res.addProperty("fireballs", fireballsFired);
        res.addProperty("items", itemsSpawned);
        res.addProperty("orbs", orbsSpawned);
        res.addProperty("playerDamageTicks", playerDamageTicks);
        res.addProperty("ghastDeaths", ghastDeaths);
        res.addProperty("maxList", maxList);
        return res;
    }

    // -------------------------------------------------------------- helpers

    /** The first y in 45..116 where the (2*foot+1) x (2*foot+1) footprint
     * has non-air at y-1 and air for y..y+rows-1, or -1. Reads the world,
     * draws nothing. */
    static int findOpen(WorldServer ws, int x, int z, int foot, int rows)
    {
        for (int y = 45; y <= 116; ++y)
        {
            if (openAt(ws, x, z, y, foot, rows)) return y;
        }

        return -1;
    }

    /** The y in 45..116 whose open box is nearest targetY (ties: the smaller
     * y). The scan itself draws nothing. */
    static int findOpenNear(WorldServer ws, int x, int z, int targetY)
    {
        int best = -1, bestDist = 1 << 30;

        for (int y = 45; y <= 116; ++y)
        {
            if (!openAt(ws, x, z, y, 2, 4)) continue;

            int d = Math.abs(y - targetY);

            if (d < bestDist)
            {
                bestDist = d;
                best = y;
            }
        }

        return best;
    }

    /** The (2*foot+1) x (2*foot+1) footprint at (x, z): air for y..y+rows-1,
     * no floor requirement. */
    static boolean airAt(WorldServer ws, int x, int z, int y, int foot, int rows)
    {
        if (y + rows - 1 > 120) return false;

        for (int dx = -foot; dx <= foot; ++dx)
        {
            for (int dz = -foot; dz <= foot; ++dz)
            {
                for (int yy = y; yy < y + rows; ++yy)
                {
                    if (ws.getBlock(x + dx, yy, z + dz) != Blocks.air) return false;
                }
            }
        }

        return true;
    }

    /** The (2*foot+1) x (2*foot+1) footprint at (x, z): non-air at y-1 and
     * air for y..y+rows-1. */
    static boolean openAt(WorldServer ws, int x, int z, int y, int foot, int rows)
    {
        if (y + rows - 1 > 120) return false;

        for (int dx = -foot; dx <= foot; ++dx)
        {
            for (int dz = -foot; dz <= foot; ++dz)
            {
                if (ws.getBlock(x + dx, y - 1, z + dz) == Blocks.air) return false;

                for (int yy = y; yy < y + rows; ++yy)
                {
                    if (ws.getBlock(x + dx, yy, z + dz) != Blocks.air) return false;
                }
            }
        }

        return true;
    }

    static int kindOf(Entity e)
    {
        if (e instanceof EntityGhast) return KIND_GHAST;
        if (e instanceof EntityPlayer) return KIND_PLAYER;
        if (e instanceof EntityLargeFireball) return KIND_FIREBALL;
        if (e instanceof EntityItem) return KIND_ITEM;
        if (e instanceof EntityXPOrb) return KIND_ORB;
        return -1;
    }

    /** entity.writeToNBT, canonical text, the nbtjson.c form. */
    static String canon(Entity e)
    {
        NBTTagCompound tag = new NBTTagCompound();
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

    /** World.updateEntityWithOptionalForce(e, true), including the
     * checkChunksExist guard around the whole body. */
    static void updateEntity(WorldServer ws, Entity e)
    {
        int var3 = MathHelper.floor_double(e.posX);
        int var4 = MathHelper.floor_double(e.posZ);

        if (!chunksExist(ws, var3, var4))
        {
            return;
        }

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

    /** World.checkChunksExist(x - 32, 0, z - 32, x + 32, 0, z + 32). */
    static boolean chunksExist(WorldServer ws, int x, int z)
    {
        int cx0 = (x - 32) >> 4, cx1 = (x + 32) >> 4;
        int cz0 = (z - 32) >> 4, cz1 = (z + 32) >> 4;

        for (int cx = cx0; cx <= cx1; ++cx)
        {
            for (int cz = cz0; cz <= cz1; ++cz)
            {
                if (!ws.theChunkProviderServer.chunkExists(cx, cz)) return false;
            }
        }

        return true;
    }

    /** The fixed per-entity state record; returns the record's length. */
    static int writeState(byte[] b, int tick, Entity e, Map<Entity, Integer> spawnIndex) throws Exception
    {
        int p = 0;
        int kind = kindOf(e);
        p = le32(b, p, spawnIndex.get(e).intValue());
        p = le32(b, p, e.getEntityId());
        p = le32(b, p, kind);
        p = le64(b, p, nbtHash(e));

        if (kind == KIND_GHAST)
        {
            EntityGhast g = (EntityGhast)e;
            p = le64(b, p, Double.doubleToRawLongBits(g.posX));
            p = le64(b, p, Double.doubleToRawLongBits(g.posY));
            p = le64(b, p, Double.doubleToRawLongBits(g.posZ));
            p = le64(b, p, Double.doubleToRawLongBits(g.motionX));
            p = le64(b, p, Double.doubleToRawLongBits(g.motionY));
            p = le64(b, p, Double.doubleToRawLongBits(g.motionZ));
            p = le32(b, p, Float.floatToRawIntBits(g.rotationYaw));
            p = le32(b, p, Float.floatToRawIntBits(g.rotationPitch));
            p = le32(b, p, Float.floatToRawIntBits(g.prevRotationYaw));
            p = le32(b, p, Float.floatToRawIntBits(g.prevRotationPitch));
            p = le32(b, p, Float.floatToRawIntBits(floatField(g, "rotationYawHead")));
            p = le32(b, p, Float.floatToRawIntBits(g.renderYawOffset));
            p = le32(b, p, Float.floatToRawIntBits(g.getHealth()));
            p = le32(b, p, Float.floatToRawIntBits(floatField(g, "prevHealth")));
            p = le32(b, p, g.hurtTime);
            p = le32(b, p, g.deathTime);
            p = le32(b, p, g.hurtResistantTime);
            p = le32(b, p, Float.floatToRawIntBits(floatField(g, "lastDamage")));
            p = le32(b, p, intField(g, "entityAge"));
            p = le32(b, p, g.ticksExisted);
            p = le32(b, p, g.prevAttackCounter);
            p = le32(b, p, g.attackCounter);
            p = le32(b, p, g.courseChangeCooldown);
            p = le32(b, p, intField(g, "aggroCooldown"));
            p = le64(b, p, Double.doubleToRawLongBits(g.waypointX));
            p = le64(b, p, Double.doubleToRawLongBits(g.waypointY));
            p = le64(b, p, Double.doubleToRawLongBits(g.waypointZ));
            p = le32(b, p, field(g, "targetedEntity") != null ? 1 : 0);
            p = le32(b, p, g.getDataWatcher().getWatchableObjectByte(16));
            p = le32(b, p, Float.floatToRawIntBits(floatField(g, "limbSwing")));
            p = le32(b, p, Float.floatToRawIntBits(g.limbSwingAmount));
            p = le32(b, p, Float.floatToRawIntBits(floatField(g, "field_110154_aX")));
            p = le32(b, p, Float.floatToRawIntBits(floatField(g, "field_70764_aw")));
            p = le32(b, p, intField(g, "explosionStrength"));
            p = le64(b, p, Det.state(((EntityLiving)g).getRNG()));
            p = le32(b, p, (g.addedToChunk ? 1 : 0) | (g.onGround ? 2 : 0) | (g.isDead ? 4 : 0));
            return p;
        }

        if (kind == KIND_FIREBALL)
        {
            EntityLargeFireball f = (EntityLargeFireball)e;
            p = le64(b, p, Double.doubleToRawLongBits(f.posX));
            p = le64(b, p, Double.doubleToRawLongBits(f.posY));
            p = le64(b, p, Double.doubleToRawLongBits(f.posZ));
            p = le64(b, p, Double.doubleToRawLongBits(f.motionX));
            p = le64(b, p, Double.doubleToRawLongBits(f.motionY));
            p = le64(b, p, Double.doubleToRawLongBits(f.motionZ));
            p = le64(b, p, Double.doubleToRawLongBits(f.accelerationX));
            p = le64(b, p, Double.doubleToRawLongBits(f.accelerationY));
            p = le64(b, p, Double.doubleToRawLongBits(f.accelerationZ));
            p = le32(b, p, Float.floatToRawIntBits(f.rotationYaw));
            p = le32(b, p, Float.floatToRawIntBits(f.rotationPitch));
            p = le32(b, p, Float.floatToRawIntBits(f.prevRotationYaw));
            p = le32(b, p, Float.floatToRawIntBits(f.prevRotationPitch));
            p = le32(b, p, intField(f, "ticksAlive"));
            p = le32(b, p, intField(f, "ticksInAir"));
            p = le32(b, p, intField(f, "field_145795_e"));
            p = le32(b, p, intField(f, "field_145793_f"));
            p = le32(b, p, intField(f, "field_145794_g"));
            Object tile = field(f, "field_145796_h");
            p = le32(b, p, tile == null ? 0 : Block.getIdFromBlock((Block)tile));
            p = le32(b, p, intField(f, "inGround"));
            p = le32(b, p, f.field_92057_e);
            p = le32(b, p, intField(f, "fire"));
            p = le64(b, p, Det.state((Random)findField(f, "rand").get(f)));
            p = le32(b, p, (f.addedToChunk ? 1 : 0) | (f.onGround ? 2 : 0) | (f.isDead ? 4 : 0));
            return p;
        }

        if (kind == KIND_ITEM)
        {
            EntityItem it = (EntityItem)e;
            p = le64(b, p, Double.doubleToRawLongBits(it.posX));
            p = le64(b, p, Double.doubleToRawLongBits(it.posY));
            p = le64(b, p, Double.doubleToRawLongBits(it.posZ));
            p = le64(b, p, Double.doubleToRawLongBits(it.motionX));
            p = le64(b, p, Double.doubleToRawLongBits(it.motionY));
            p = le64(b, p, Double.doubleToRawLongBits(it.motionZ));
            p = le32(b, p, Float.floatToRawIntBits(it.rotationYaw));
            p = le32(b, p, Float.floatToRawIntBits(it.rotationPitch));
            p = le32(b, p, intField(it, "health"));
            p = le32(b, p, it.age);
            p = le32(b, p, it.delayBeforeCanPickup);
            p = le32(b, p, intField(it, "fire"));
            p = le32(b, p, Float.floatToRawIntBits(floatField(it, "hoverStart")));
            ItemStack st = it.getEntityItem();
            p = le32(b, p, st == null ? 0 : net.minecraft.item.Item.getIdFromItem(st.getItem()));
            p = le32(b, p, st == null ? 0 : st.getItemDamage());
            p = le32(b, p, st == null ? 0 : st.stackSize);
            p = le64(b, p, Det.state((Random)findField(it, "rand").get(it)));
            p = le32(b, p, (it.addedToChunk ? 1 : 0) | (it.onGround ? 2 : 0) | (it.isDead ? 4 : 0));
            return p;
        }

        if (kind == KIND_ORB)
        {
            EntityXPOrb o = (EntityXPOrb)e;
            p = le64(b, p, Double.doubleToRawLongBits(o.posX));
            p = le64(b, p, Double.doubleToRawLongBits(o.posY));
            p = le64(b, p, Double.doubleToRawLongBits(o.posZ));
            p = le64(b, p, Double.doubleToRawLongBits(o.motionX));
            p = le64(b, p, Double.doubleToRawLongBits(o.motionY));
            p = le64(b, p, Double.doubleToRawLongBits(o.motionZ));
            p = le32(b, p, Float.floatToRawIntBits(o.rotationYaw));
            p = le32(b, p, Float.floatToRawIntBits(o.rotationPitch));
            p = le32(b, p, intField(o, "xpOrbHealth"));
            p = le32(b, p, o.xpOrbAge);
            p = le32(b, p, intField(o, "xpValue"));
            p = le32(b, p, intField(o, "fire"));
            p = le64(b, p, Det.state((Random)findField(o, "rand").get(o)));
            p = le32(b, p, (o.addedToChunk ? 1 : 0) | (o.onGround ? 2 : 0) | (o.isDead ? 4 : 0));
            return p;
        }

        throw new IllegalStateException("kind " + kind + " reached writeState");
    }

    static int recordSize(int kind)
    {
        return kind == KIND_GHAST ? 204 : (kind == KIND_FIREBALL ? 156 : (kind == KIND_ITEM ? 120 : (kind == KIND_ORB ? 104 : -1)));
    }

    static void writePlayerState(byte[] b, int tick) throws Exception
    {
        EntityPlayer pl = playerEntity;
        int p = 0;
        p = le32(b, p, pl.getEntityId());
        p = le64(b, p, Double.doubleToRawLongBits(pl.posX));
        p = le64(b, p, Double.doubleToRawLongBits(pl.posY));
        p = le64(b, p, Double.doubleToRawLongBits(pl.posZ));
        p = le64(b, p, Double.doubleToRawLongBits(pl.motionX));
        p = le64(b, p, Double.doubleToRawLongBits(pl.motionY));
        p = le64(b, p, Double.doubleToRawLongBits(pl.motionZ));
        p = le32(b, p, Float.floatToRawIntBits(pl.rotationYaw));
        p = le32(b, p, Float.floatToRawIntBits(pl.rotationPitch));
        p = le32(b, p, Float.floatToRawIntBits(pl.getHealth()));
        p = le32(b, p, Float.floatToRawIntBits(floatField(pl, "prevHealth")));
        p = le32(b, p, Float.floatToRawIntBits(pl.getAbsorptionAmount()));
        p = le32(b, p, pl.hurtTime);
        p = le32(b, p, intField(pl, "maxHurtTime"));
        p = le32(b, p, pl.deathTime);
        p = le32(b, p, pl.hurtResistantTime);
        p = le32(b, p, Float.floatToRawIntBits(floatField(pl, "lastDamage")));
        p = le32(b, p, intField(pl, "entityAge"));
        p = le32(b, p, intField(pl, "isDead"));
        p = le64(b, p, Det.state((Random)findField(pl, "rand").get(pl)));
        while (p < PLAYER_STATE_BYTES) b[p++] = 0;
    }

    // ------------------------------------------------------------ reflection

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

    static int intField(Object o, String name)
    {
        Object v = field(o, name);

        if (v instanceof Boolean) return ((Boolean)v).booleanValue() ? 1 : 0;

        return ((Number)v).intValue();
    }

    static float floatField(Object o, String name)
    {
        return ((Number)field(o, name)).floatValue();
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
        b.append(" worldRandGauss ").append(findField(wr, "haveNextNextGaussian").getBoolean(wr) ? 1 : 0);
        w.println(b.toString());
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
        b.append("worldRand ").append(hex(Det.state((Random)wr))).append(' ').append(findField(wr, "haveNextNextGaussian").getBoolean(wr) ? 1 : 0);
        w.println(b.toString());

        w.close();
    }

    static String hex(long bits)
    {
        StringBuilder b = new StringBuilder(16);

        for (int i = 15; i >= 0; --i) b.append("0123456789abcdef".charAt((int)((bits >>> (i * 4)) & 15L)));

        return b.toString();
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