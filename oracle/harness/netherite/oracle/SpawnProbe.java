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
import java.util.Calendar;
import java.util.Iterator;
import java.util.List;
import java.util.Random;
import java.util.Set;
import java.util.zip.GZIPOutputStream;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityLiving;
import net.minecraft.entity.EnumCreatureType;
import net.minecraft.entity.monster.EntitySkeleton;
import net.minecraft.entity.monster.EntitySlime;
import net.minecraft.entity.monster.EntitySpider;
import net.minecraft.entity.monster.EntityZombie;
import net.minecraft.entity.passive.EntityBat;
import net.minecraft.entity.passive.EntityChicken;
import net.minecraft.entity.passive.EntitySheep;
import net.minecraft.entity.passive.EntitySquid;
import net.minecraft.entity.passive.EntityWolf;
import net.minecraft.entity.player.EntityPlayerMP;
import net.minecraft.item.ItemStack;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.util.ChunkCoordinates;
import net.minecraft.util.MathHelper;
import net.minecraft.world.ChunkCoordIntPair;
import net.minecraft.world.World;
import net.minecraft.world.WorldServer;
import net.minecraft.world.biome.BiomeGenBase;
import net.minecraft.world.chunk.Chunk;

/**
 * The mob spawning probe, three kinds by the command's "kind":
 *
 *   "spawner"  SpawnerAnimals.findChunksForSpawning run the way
 *              WorldServer.tick runs it (doMobSpawning on), tick after tick,
 *              from a real world state, with one parked player standing in a
 *              chosen biome. The spawned mobs are recorded and never ticked
 *              (this probe drives ws.tick() only, never updateEntities), so a
 *              spawned mob is a constructed record: class, position, the Det
 *              draws already spent, onSpawnWithEgg's equipment.
 *
 *   "scene"    a squid and bat scene ticked on the probe's own list the way
 *              AnimalProbe ticks its animals, the reference for the two
 *              kinds' native movement.
 *
 *   "populate" SpawnerAnimals.performWorldGenSpawning over the populate
 *              probe's regions: the ring load, the populate calls in the
 *              populate probe's order, and per call the worldgen spawns with
 *              World.rand's state entering the spawning stage.
 *
 * Output DIR/manifest.json (the start snapshot's manifest plus the probe's own
 * keys), the snapshot files, DIR/tickwrites.bin, DIR/tickrows.jsonl.gz,
 * DIR/spawns.jsonl.gz and DIR/end.txt (the Det state at the end, AnimalProbe's
 * writeDetState layout).
 */
public final class SpawnProbe
{
    private SpawnProbe() {}

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
        }, "Oracle SpawnProbe");
        t.start();
        t.join();
        if (error[0] != null) throw error[0];
        return result[0];
    }

    static JsonObject dump(IntegratedServer server, JsonObject cmd) throws Exception
    {
        String kind = cmd.has("kind") ? cmd.get("kind").getAsString() : "spawner";

        if (kind.equals("scene")) return dumpScene(server, cmd);
        if (kind.equals("populate")) return dumpPopulate(server, cmd);
        if (kind.equals("diag")) return dumpDiag(server, cmd);
        return dumpSpawner(server, cmd);
    }

    /* Diagnostic, not a recording: split a tick's World.rand steps between the
     * spawner and the tick body, by re-running findChunksForSpawning by hand
     * over a saved state. The spawns it makes a second time are removed; the
     * run is a throwaway. */
    static JsonObject dumpDiag(IntegratedServer server, JsonObject cmd) throws Exception
    {
        int ticks = cmd.has("ticks") ? cmd.get("ticks").getAsInt() : 5;
        WorldServer ws = server.worldServers[0];
        EntityPlayerMP player = (EntityPlayerMP)ws.playerEntities.get(0);
        double px = cmd.has("px") ? cmd.get("px").getAsDouble() : player.posX;
        double py = cmd.has("py") ? cmd.get("py").getAsDouble() : player.posY;
        double pz = cmd.has("pz") ? cmd.get("pz").getAsDouble() : player.posZ;

        wi_setSpawn(ws, MathHelper.floor_double(px), (int)py, MathHelper.floor_double(pz));

        Thread self = Thread.currentThread();
        Det.clientThread = null;
        Det.serverThread = self;
        Det.inRender = false;

        Object spawner = Snapshot.objField(ws, "animalSpawner");
        java.lang.reflect.Method find = spawner.getClass().getDeclaredMethod("findChunksForSpawning", WorldServer.class, boolean.class, boolean.class, boolean.class);
        find.setAccessible(true);

        new File(cmd.get("out").getAsString()).mkdirs();
        List<String> lines = new ArrayList<String>();
        int baseline = ws.loadedEntityList.size();

        for (int t = 1; t <= ticks; ++t)
        {
            long s0 = Det.state(ws.rand);
            ws.tick();
            long endState = Det.state((Random)ws.rand);

            // restore and run the spawner by hand
            setRandState(ws.rand, s0);
            int n0 = ws.loadedEntityList.size();
            find.invoke(spawner, ws, Boolean.TRUE, Boolean.TRUE,
                 Boolean.valueOf(ws.getWorldInfo().getWorldTotalTime() % 400L == 0L));
            int spSteps = (int)stepsBetween(s0, Det.state((Random)ws.rand));

            // drop the shadow spawns, restore the tick's end state
            for (int i = ws.loadedEntityList.size() - 1; i >= n0; --i)
            {
                Entity e = (Entity)ws.loadedEntityList.get(i);

                if (e.addedToChunk && ws.theChunkProviderServer.chunkExists(e.chunkCoordX, e.chunkCoordZ))
                {
                    ws.getChunkFromChunkCoords(e.chunkCoordX, e.chunkCoordZ).removeEntity(e);
                }

                ws.loadedEntityList.remove(i);
            }

            setRandState(ws.rand, endState);

            lines.add("t=" + t + " total=" + stepsBetween(s0, endState) + " spawner=" + spSteps
                + " body=" + (stepsBetween(s0, endState) - spSteps));
            baseline = ws.loadedEntityList.size();
        }

        PrintWriter w = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(cmd.get("out").getAsString(), "diag.txt")), "UTF-8"));

        for (String l : lines) w.println(l);

        w.close();

        JsonObject r = new JsonObject();
        r.addProperty("ok", true);
        r.addProperty("lines", lines.size());
        return r;
    }

    /* Det's streams, for the diagnostic's save and restore: the four seeder
     * and math Randoms' states, the id counters, and the splits. */
    static long[] detSnapshot()
    {
        long[] snap = new long[4 * (Det.ROLES * 2 + 1) + Det.splits.size() * 8 + 8];
        int p = 0;

        for (int role = 0; role < Det.ROLES; ++role)
        {
            snap[p++] = Det.state(Det.seeder[role]);
            snap[p++] = Det.state(Det.math[role]);
            snap[p++] = Det.nextId[role];
        }

        synchronized (Det.class)
        {
            for (Det.SplitRandom s : Det.splits)
            {
                for (int role = 0; role < Det.ROLES; ++role) snap[p++] = Det.state(s.d[role]);

                for (int role = 0; role < Det.ROLES; ++role) snap[p++] = s.used[role] ? 1 : 0;
            }
        }

        return snap;
    }

    static void detRestore(long[] snap) throws Exception
    {
        int p = 0;

        for (int role = 0; role < Det.ROLES; ++role)
        {
            setRandState(Det.seeder[role], snap[p++]);
            setRandState(Det.math[role], snap[p++]);
            Det.nextId[role] = (int)snap[p++];
        }

        synchronized (Det.class)
        {
            for (Det.SplitRandom s : Det.splits)
            {
                for (int role = 0; role < Det.ROLES; ++role) setRandState(s.d[role], snap[p++]);

                p += Det.ROLES;   /* the used flags are digests only */
            }
        }
    }

    static long stepsBetween(long from, long to)
    {
        long M = (1L << 48) - 1;
        long cur = from & M;
        long target = to & M;
        long n = 0;

        while (cur != target && n < 1000000L)
        {
            cur = (cur * 0x5DEECE66DL + 11L) & M;
            ++n;
        }

        return n;
    }

    /* Det.state reads Random.seed (an AtomicLong); write it back directly. */
    static void setRandState(Random r, long v) throws Exception
    {
        java.lang.reflect.Field f = Random.class.getDeclaredField("seed");
        f.setAccessible(true);
        ((java.util.concurrent.atomic.AtomicLong)f.get(r)).set(v & ((1L << 48) - 1));
    }

    /* ------------------------------------------------------------------ */
    /* the spawner kind                                                    */
    /* ------------------------------------------------------------------ */

    static JsonObject dumpSpawner(IntegratedServer server, JsonObject cmd) throws Exception
    {
        File dir = new File(cmd.get("out").getAsString());
        dir.mkdirs();

        int ticks = cmd.has("ticks") ? cmd.get("ticks").getAsInt() : 4800;
        long day = cmd.has("day") ? cmd.get("day").getAsLong() : 13000L;
        boolean rain = cmd.has("rain") && cmd.get("rain").getAsBoolean();
        boolean thunder = cmd.has("thunder") && cmd.get("thunder").getAsBoolean();
        boolean dig = cmd.has("dig") && cmd.get("dig").getAsBoolean();

        Trace.restart();   /* the trace holds this run, not the world's startup */

        /* the setup below (the ring loads' population, the digging) is not
         * part of the recorded ticks; the trace holds the ticks only */
        Trace.pause();

        WorldServer ws = server.worldServers[0];
        long seed = ws.getSeed();
        EntityPlayerMP player = (EntityPlayerMP)ws.playerEntities.get(0);

        // the spot: the command's px/pz (already chosen for the biome), the y
        // is the command's too so the player can stand on the ocean surface or
        // in a dug room
        double px = cmd.has("px") ? cmd.get("px").getAsDouble() : 0.5D;
        double py = cmd.has("py") ? cmd.get("py").getAsDouble() : 80.0D;
        double pz = cmd.has("pz") ? cmd.get("pz").getAsDouble() : 0.5D;

        String find = cmd.has("find") ? cmd.get("find").getAsString() : null;

        if (find != null)
        {
            int fx = 0, fz = 0;
            boolean found = false;

            for (int rad = 64; rad <= 8192 && !found; rad += 64)
            {
                for (int a = -rad; a <= rad && !found; a += 32)
                {
                    int[] cand = { rad, -rad, a, a };

                    for (int c = 0; c < 4 && !found; ++c)
                    {
                        int sx = c < 2 ? cand[c] : a;
                        int sz = c < 2 ? a : cand[2 + (c - 2)];

                        // the four edges of the square, walk both axes
                        if (c == 0) { sx = a; sz = rad; }
                        else if (c == 1) { sx = a; sz = -rad; }
                        else if (c == 2) { sx = rad; sz = a; }
                        else { sx = -rad; sz = a; }

                        BiomeGenBase b = ws.getWorldChunkManager().getBiomeGenAt(sx, sz);

                        if (find.equals("ocean"))
                        {
                            if (b.biomeID == 0 || b.biomeID == 24) { fx = sx; fz = sz; found = true; }
                        }
                        else if (find.equals("mushroom"))
                        {
                            if (b.biomeID == 14) { fx = sx; fz = sz; found = true; }
                        }
                        else if (b.biomeID == 1 || b.biomeID == 4 || b.biomeID == 5 || b.biomeID == 27 || b.biomeID == 29 || b.biomeID == 35)
                        {
                            fx = sx;
                            fz = sz;
                            found = true;
                        }
                    }
                }
            }

            if (!found) throw new IllegalStateException("no " + find + " biome within the spiral");
            px = fx + 0.5D;
            pz = fz + 0.5D;
        }

        // the spawn point follows the player, so the 576-distance spawn check
        // and unloadChunksIfNotNearSpawn both see the new place; the spawner's
        // own gate is what the native side reads out of the snapshot
        wi_setSpawn(ws, MathHelper.floor_double(px), (int)py, MathHelper.floor_double(pz));

        // loadChunkOnProvideRequest = false: World reads outside the loaded
        // set see the empty chunk instead of generating one
        Object cps = ws.theChunkProviderServer;
        Snapshot.field(cps, "loadChunkOnProvideRequest").setBoolean(cps, false);

        // the player moves: position, the chunk membership the entity pass
        // would have fixed, and the managed position the PlayerManager reads
        int ocx = player.chunkCoordX, ocz = player.chunkCoordZ;

        if (player.addedToChunk && ws.theChunkProviderServer.chunkExists(ocx, ocz))
        {
            ws.getChunkFromChunkCoords(ocx, ocz).removeEntity(player);
        }

        player.setPosition(px, py, pz);
        player.chunkCoordX = MathHelper.floor_double(px / 16.0D);
        player.chunkCoordY = MathHelper.floor_double(py / 16.0D);
        player.chunkCoordZ = MathHelper.floor_double(pz / 16.0D);
        player.addedToChunk = true;
        ws.getChunkFromChunkCoords(player.chunkCoordX, player.chunkCoordZ).addEntity(player);
        player.managedPosX = px;
        player.managedPosZ = pz;

        // the chunks around the player: 21x21, the view radius the server
        // keeps around one player. The loads run here, before any recorded
        // tick, and they are honest worldgen: the population's worldgen
        // spawning draws World.rand (recorded in the snapshot) and constructs
        // its animals on this thread, whose Det draws go to the OTHER role.
        int pcx = MathHelper.floor_double(px / 16.0D);
        int pcz = MathHelper.floor_double(pz / 16.0D);

        for (int lx = pcx - 10; lx <= pcx + 10; ++lx)
        {
            for (int lz = pcz - 10; lz <= pcz + 10; ++lz)
            {
                if (ws.theChunkProviderServer.loadChunk(lx, lz) == null) throw new IllegalStateException("chunk (" + lx + "," + lz + ") did not load");
            }
        }

        // the player's height over the terrain: the first air above the ground
        // (land) or above the water (ocean); the player is re-placed there so
        // the spawner's checks read a standing player, and the manifest's py
        // (what the native replay sets) is the player's true y
        if (cmd.has("yauto") && cmd.get("yauto").getAsInt() == 1)
        {
            BiomeGenBase spot = ws.getBiomeGenForCoords((int)px, (int)pz);

            if (spot.biomeID == 0 || spot.biomeID == 24) py = ws.getPrecipitationHeight((int)px, (int)pz);
            else py = ws.getHeightValue((int)px, (int)pz);

            player.setPosition(px, py, pz);
            player.chunkCoordY = MathHelper.floor_double(py / 16.0D);
            wi_setSpawn(ws, MathHelper.floor_double(px), (int)py, MathHelper.floor_double(pz));
        }

        // the world holds exactly the pre-probe entities plus the ones the
        // loads' population placed: the removal below is the only change
        int entitiesBeforeLoads = ws.loadedEntityList.size();

        // the dug room: a 7x7 chamber 40 blocks north-east of the player (past
        // the 24-block despawn radius, inside the 17x17 chunk set) with two
        // torches one block apart in one corner, so the light rules get
        // boundary values
        if (dig)
        {
            int ry = 30;
            int rx = MathHelper.floor_double(px) + 37, rz = MathHelper.floor_double(pz) + 37;

            for (int x = 0; x < 7; ++x)
            {
                for (int z = 0; z < 7; ++z)
                {
                    for (int y = ry; y < ry + 3; ++y) ws.setBlock(rx + x, y, rz + z, net.minecraft.init.Blocks.air, 0, 3);
                }
            }

            ws.setBlock(rx, ry, rz, net.minecraft.init.Blocks.torch, 0, 3);
            ws.setBlock(rx + 2, ry, rz, net.minecraft.init.Blocks.torch, 0, 3);
        }

        // torches the command lists, for light boundaries on the surface
        if (cmd.has("torches"))
        {
            for (com.google.gson.JsonElement e : cmd.get("torches").getAsJsonArray())
            {
                JsonArray p3 = e.getAsJsonArray();
                ws.setBlock(p3.get(0).getAsInt(), p3.get(1).getAsInt(), p3.get(2).getAsInt(), net.minecraft.init.Blocks.torch, 0, 3);
            }
        }

        // no pre-existing living entity: the worldgen animals of the spawn
        // area and the loaded ring would sit over the spawner's caps forever.
        // updateEntities never runs here, so removeEntity (which only marks
        // dead) is not enough; the removal is the world's own: the chunk list
        // and the loaded list, in one pass, the way updateEntities does it.
        List living = new ArrayList();
        int kept = 0;

        for (int i = 0; i < ws.loadedEntityList.size(); ++i)
        {
            Entity e = (Entity)ws.loadedEntityList.get(i);

            if (e instanceof EntityLiving) living.add(e);
            else ++kept;
        }

        for (int i = 0; i < living.size(); ++i)
        {
            Entity e = (Entity)living.get(i);

            if (e.addedToChunk && ws.theChunkProviderServer.chunkExists(e.chunkCoordX, e.chunkCoordZ))
            {
                ws.getChunkFromChunkCoords(e.chunkCoordX, e.chunkCoordZ).removeEntity(e);
            }

            ws.loadedEntityList.remove(e);
        }

        if (ws.loadedEntityList.size() != kept) throw new IllegalStateException("entity list did not shrink to " + kept);
        if (living.size() == 0 && entitiesBeforeLoads > 1) { /* a spot whose ring had no animals is fine */ }

        // the clocks the recorded ticks start from
        ws.getWorldInfo().setWorldTime(day);

        if (rain)
        {
            ws.getWorldInfo().setRaining(true);
            ws.getWorldInfo().setRainTime(cmd.has("rainTime") ? cmd.get("rainTime").getAsInt() : 12000);
        }

        if (thunder)
        {
            ws.getWorldInfo().setThundering(true);
            ws.getWorldInfo().setThunderTime(cmd.has("thunderTime") ? cmd.get("thunderTime").getAsInt() : 12000);
        }

        // the spawn flags the tick reads, forced on for the record
        Snapshot.field(ws, "spawnHostileMobs").setBoolean(ws, true);
        Snapshot.field(ws, "spawnPeacefulMobs").setBoolean(ws, true);
        ws.getGameRules().setOrCreateGameRule("doMobSpawning", "true");

        // the join's unload queue is empty already (nothing was marked); keep
        // it that way and pin the PlayerManager's 8000-tick pass so the
        // inhabitedTime of the loaded chunks cannot move inside the run (the
        // local difficulty func_147462_b reads it)
        Set unloadQueue = (Set<String>)(Set)Snapshot.objField(cps, "chunksToUnload");
        unloadQueue.clear();
        Object pm = Snapshot.objField(ws, "thePlayerManager");
        Snapshot.field(pm, "previousTotalWorldTime").setLong(pm, ws.getTotalWorldTime());

        // ------------------------------------------------------------- record
        final byte[] wbuf = new byte[16 * 4096];
        final int[] wlen = { 0 };
        final int[] wcount = { 0 };
        DataOutputStream wr = new DataOutputStream(new BufferedOutputStream(new FileOutputStream(new File(dir, "tickwrites.bin")), 1 << 20));
        PrintWriter rows = new PrintWriter(new OutputStreamWriter(new GZIPOutputStream(new FileOutputStream(new File(dir, "tickrows.jsonl.gz")), 1 << 16), "UTF-8"));
        PrintWriter spawns = new PrintWriter(new OutputStreamWriter(new GZIPOutputStream(new FileOutputStream(new File(dir, "spawns.jsonl.gz")), 1 << 16), "UTF-8"));

        Rows.writeListener = new Rows.WriteListener()
        {
            public void onWrite(World w, int x, int y, int z, int id, int meta)
            {
                if (wlen[0] + 16 > wbuf.length)
                {
                    try { ServerTickProbe.flushWrites(wr, wbuf, wlen); }
                    catch (Exception e) { throw new RuntimeException(e); }
                }
                ServerTickProbe.put32(wbuf, wlen[0], x);
                ServerTickProbe.put32(wbuf, wlen[0] + 4, y);
                ServerTickProbe.put32(wbuf, wlen[0] + 8, z);
                wbuf[wlen[0] + 12] = (byte)id;
                wbuf[wlen[0] + 13] = (byte)(id >> 8);
                wbuf[wlen[0] + 14] = (byte)meta;
                wbuf[wlen[0] + 15] = 0;
                wlen[0] += 16;
                ++wcount[0];
            }
        };

        Thread self = Thread.currentThread();
        Thread oldClient = Det.clientThread, oldServer = Det.serverThread;
        boolean oldRender = Det.inRender;
        Det.clientThread = null;
        Det.serverThread = self;
        Det.inRender = false;

        // the loaded set the ticks must not change, ServerTickProbe's baseline
        ServerTickProbe.chunkSnapshot(ws);

        // the state the native side needs out of the live world before the
        // snapshot: the active set's iteration order (rebuilt every tick from
        // the parked player, so one snapshot of the order stands), the
        // difficulty, the calendar month and day the bat rule reads
        int ambStart = Snapshot.intField(ws, "ambientTickCountdown");
        int difficulty = ws.difficultySetting.getDifficultyId();
        Calendar cal = Oracle.calendar();
        int calMonth = cal.get(2) + 1, calDay = cal.get(5);
        JsonArray active = new JsonArray();
        boolean activeTaken = false;

        JsonObject scmd = new JsonObject();
        scmd.addProperty("out", dir.getPath());
        JsonObject snap = Snapshot.dump(server, scmd);

        int spawned = 0, wrote = 0;

        String diagSplit = null;

        try
        {

            if (cmd.has("diag") && cmd.get("diag").getAsInt() == 1)
            {
                // split tick 1's World.rand steps between the spawner and the
                // tick body: run the tick, then restore the pre-tick state and
                // run the spawner by hand, then put everything back
                long s0 = Det.state((Random)ws.rand);
                int n0 = ws.loadedEntityList.size();
                ws.tick();
                long endState = Det.state((Random)ws.rand);
                long[] detsEnd = detSnapshot();

                setRandState(ws.rand, s0);

                Object shadow = Snapshot.objField(ws, "animalSpawner");
                java.lang.reflect.Method m = shadow.getClass().getDeclaredMethod("findChunksForSpawning", WorldServer.class, boolean.class, boolean.class, boolean.class);
                m.setAccessible(true);
                m.invoke(shadow, ws, Boolean.TRUE, Boolean.TRUE,
                    Boolean.valueOf(ws.getWorldInfo().getWorldTotalTime() % 400L == 0L));
                int spSteps = (int)stepsBetween(s0, Det.state((Random)ws.rand));

                for (int i = ws.loadedEntityList.size() - 1; i >= n0; --i)
                {
                    Entity e = (Entity)ws.loadedEntityList.get(i);

                    if (e.addedToChunk && ws.theChunkProviderServer.chunkExists(e.chunkCoordX, e.chunkCoordZ))
                    {
                        ws.getChunkFromChunkCoords(e.chunkCoordX, e.chunkCoordZ).removeEntity(e);
                    }

                    ws.loadedEntityList.remove(i);
                }

                setRandState(ws.rand, endState);
                detRestore(detsEnd);

                diagSplit = "total=" + stepsBetween(s0, endState) + " spawner=" + spSteps;
            }

            Trace.resume();

            for (int t = 1; t <= ticks; ++t)
            {
                int nLoaded = ws.loadedEntityList.size();
                wcount[0] = 0;
                ws.tick();
                ServerTickProbe.flushWrites(wr, wbuf, wlen);
                int w = wcount[0];
                ServerTickProbe.chunkMove(ws, t, true);

                for (int i = nLoaded; i < ws.loadedEntityList.size(); ++i)
                {
                    spawnLine(spawns, t, i - nLoaded, (Entity)ws.loadedEntityList.get(i), ws);
                    ++spawned;
                }

                rows.println(ServerTickProbe.tickRow(ws, t, w, ws.loadedEntityList.size() - nLoaded, 0).toString());
                wrote += w;

                if (!activeTaken)
                {
                    for (Object o : (Iterable)Snapshot.objField(ws, "activeChunkSet"))
                    {
                        ChunkCoordIntPair p = (ChunkCoordIntPair)o;
                        JsonArray pair = new JsonArray();
                        pair.add(new JsonPrimitive(p.chunkXPos));
                        pair.add(new JsonPrimitive(p.chunkZPos));
                        active.add(pair);
                    }

                    activeTaken = true;
                }

                if (t % 400 == 0) System.out.println("SPAWNER t=" + t + " writes=" + wrote + " spawned=" + spawned);
            }
        }
        finally
        {
            Det.clientThread = oldClient;
            Det.serverThread = oldServer;
            Det.inRender = oldRender;
            Rows.writeListener = null;
        }

        wr.close();
        rows.close();
        spawns.close();
        AnimalProbe.writeDetState(new File(dir, "end.txt"), seed, ws);

        // ------------------------------------------------------------ manifest
        String manifest = readText(new File(dir, "manifest.json"));
        JsonObject m = (JsonObject)new com.google.gson.JsonParser().parse(manifest);
        JsonObject p = new JsonObject();
        p.addProperty("class", "SpawnProbe");
        p.addProperty("kind", "spawner");
        p.addProperty("ticks", ticks);
        p.addProperty("day", day);
        p.addProperty("rain", rain ? 1 : 0);
        p.addProperty("thunder", thunder ? 1 : 0);
        p.addProperty("dig", dig ? 1 : 0);
        p.addProperty("px", ServerTickProbe.dbits(px));
        p.addProperty("py", ServerTickProbe.dbits(py));
        p.addProperty("pz", ServerTickProbe.dbits(pz));
        p.addProperty("difficulty", difficulty);
        p.addProperty("ambientTickCountdown", ambStart);
        p.addProperty("activeChunks", active.size());
        p.add("active", active);
        if (diagSplit != null) p.addProperty("diagSplit", diagSplit);
        p.addProperty("calMonth", calMonth);
        p.addProperty("calDay", calDay);
        p.addProperty("noChunkGeneration", 1);
        p.addProperty("writes", wrote);
        p.addProperty("spawns", spawned);
        p.addProperty("viewDistance", (Integer)Snapshot.invoke(server.getConfigurationManager(), "getViewDistance"));
        p.addProperty("spawnX", ws.getWorldInfo().getSpawnX());
        p.addProperty("spawnY", ws.getWorldInfo().getSpawnY());
        p.addProperty("spawnZ", ws.getWorldInfo().getSpawnZ());
        p.addProperty("biome", biomeAt(ws, (int)px, (int)pz).biomeID);
        p.addProperty("entitiesBeforeLoads", entitiesBeforeLoads);
        p.addProperty("livingRemoved", living.size());
        m.add("probe", p);
        PrintWriter mw = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "manifest.json")), "UTF-8"));
        mw.println(m.toString());
        mw.close();

        JsonObject r = new JsonObject();
        r.addProperty("spawned", spawned);
        r.addProperty("writes", wrote);
        r.addProperty("dir", dir.getPath());
        return r;
    }

    /* one spawn the tick made, the fields the native record compares and the
     * canonical NBT the mob lanes pick the state up from */
    static void spawnLine(PrintWriter w, int t, int i, Entity e, WorldServer ws) throws Exception
    {
        JsonObject o = new JsonObject();
        o.addProperty("t", t);
        o.addProperty("i", i);
        o.addProperty("cls", e.getClass().getSimpleName());
        o.addProperty("id", e.getEntityId());
        o.addProperty("x", ServerTickProbe.dbits(e.posX));
        o.addProperty("y", ServerTickProbe.dbits(e.posY));
        o.addProperty("z", ServerTickProbe.dbits(e.posZ));
        o.addProperty("mx", ServerTickProbe.dbits(e.motionX));
        o.addProperty("my", ServerTickProbe.dbits(e.motionY));
        o.addProperty("mz", ServerTickProbe.dbits(e.motionZ));
        o.addProperty("yaw", ServerTickProbe.fbits(e.rotationYaw));
        o.addProperty("rnd", Snapshot.hex(Det.state(AnimalProbe.entityRand(e)), 12));
        o.addProperty("uuidM", "l:" + e.getUniqueID().getMostSignificantBits());
        o.addProperty("uuidL", "l:" + e.getUniqueID().getLeastSignificantBits());

        if (e instanceof net.minecraft.entity.EntityLivingBase)
        {
            net.minecraft.entity.EntityLivingBase lb = (net.minecraft.entity.EntityLivingBase)e;
            o.addProperty("ap", ServerTickProbe.fbits(AnimalProbe.findField(lb, "field_70770_ap").getFloat(lb)));
            o.addProperty("ao", ServerTickProbe.fbits(AnimalProbe.findField(lb, "field_70769_ao").getFloat(lb)));
        }

        if (e instanceof EntityLiving)
        {
            EntityLiving l = (EntityLiving)e;

            if (e instanceof EntitySlime) o.addProperty("slime", ((EntitySlime)e).getSlimeSize());
            if (e instanceof EntitySheep) o.addProperty("fleece", ((EntitySheep)e).getFleeceColor());
            if (e instanceof EntitySkeleton) o.addProperty("skel", ((EntitySkeleton)e).getSkeletonType());
            if (e instanceof EntityZombie) o.addProperty("villager", ((EntityZombie)e).isVillager() ? 1 : 0);
            if (e instanceof EntityZombie) o.addProperty("pickup", l.canPickUpLoot() ? 1 : 0);

            JsonArray eq = new JsonArray();
            ItemStack[] items = (ItemStack[])AnimalProbe.field(l, "equipment");

            for (int s = 0; s < 5; ++s)
            {
                JsonArray st = new JsonArray();
                ItemStack it = items == null ? null : items[s];

                if (it == null)
                {
                    st.add(new JsonPrimitive(-1));
                    st.add(new JsonPrimitive(-1));
                    st.add(new JsonPrimitive(0));
                    st.add(new JsonPrimitive(""));
                }
                else
                {
                    st.add(new JsonPrimitive(net.minecraft.item.Item.getIdFromItem(it.getItem())));
                    st.add(new JsonPrimitive(it.getItemDamage()));
                    st.add(new JsonPrimitive(it.stackSize));
                    NBTTagCompound tag = it.getTagCompound();
                    st.add(new JsonPrimitive(tag == null ? "" : StructuresProbe.canon(tag).toString()));
                }

                eq.add(st);
            }

            o.add("eq", eq);

            if (e.ridingEntity != null) o.addProperty("riding", e.ridingEntity.getEntityId());
            if (e.riddenByEntity != null) o.addProperty("riddenBy", e.riddenByEntity.getEntityId());
        }

        NBTTagCompound tag = new NBTTagCompound();
        e.writeToNBT(tag);
        o.add("nbt", StructuresProbe.canon(tag));
        w.println(o.toString());
    }

    static BiomeGenBase biomeAt(WorldServer ws, int x, int z)
    {
        return ws.getBiomeGenForCoords(x, z);
    }

    static void wi_setSpawn(WorldServer ws, int x, int y, int z)
    {
        ws.getWorldInfo().setSpawnPosition(x, y, z);
    }

    static String readText(File f) throws Exception
    {
        java.io.ByteArrayOutputStream b = new java.io.ByteArrayOutputStream();
        java.io.FileInputStream in = new java.io.FileInputStream(f);
        byte[] buf = new byte[8192];
        int n;

        while ((n = in.read(buf)) > 0) b.write(buf, 0, n);

        in.close();
        return new String(b.toByteArray(), "UTF-8");
    }

    /* ------------------------------------------------------------------ */
    /* the scene kind: squids and bats ticked on the probe's own list      */
    /* ------------------------------------------------------------------ */

    static JsonObject dumpScene(IntegratedServer server, JsonObject cmd) throws Exception
    {
        File dir = new File(cmd.get("out").getAsString());
        dir.mkdirs();

        int ticks = cmd.has("ticks") ? cmd.get("ticks").getAsInt() : 2400;
        int nbtEvery = cmd.has("nbtEvery") ? cmd.get("nbtEvery").getAsInt() : 64;
        long opseed = cmd.has("opseed") ? cmd.get("opseed").getAsLong() : 11L;

        Trace.restart();

        WorldServer ws = server.worldServers[0];
        long seed = ws.getSeed();
        EntityPlayerMP player = (EntityPlayerMP)ws.playerEntities.get(0);

        // the chunk loads are recorded raw (the same dump the populate probe's
        // replay reads): the arena build's setBlocks run on top of them
        PopulateProbe.rawStart(dir);

        // the scene writes its own manifest (the snapshot manifest needs no
        // server parking for a probe list the tick loop drives)
        {
            JsonObject m = new JsonObject();
            m.addProperty("kind", "scene");
            m.addProperty("seed", seed);
            m.addProperty("cmdpx", cmd.has("px"));
            PrintWriter mw = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "manifest.json")), "UTF-8"));
            mw.println(m.toString());
            mw.close();
        }

        // the scene: a water arena for the squids (a 16x16 pool eight deep
        // with a shallower shelf and a dry ledge), and a dark chamber for the
        // bats above the water. The player stands on the dry ledge.
        double px = cmd.get("px").getAsDouble(), py = cmd.get("py").getAsDouble(), pz = cmd.get("pz").getAsDouble();

        wi_setSpawn(ws, MathHelper.floor_double(px), (int)py, MathHelper.floor_double(pz));

        Object cps = ws.theChunkProviderServer;
        Snapshot.field(cps, "loadChunkOnProvideRequest").setBoolean(cps, false);

        int pcx = MathHelper.floor_double(px / 16.0D), pcz = MathHelper.floor_double(pz / 16.0D);

        for (int lx = pcx - 3; lx <= pcx + 3; ++lx)
        {
            for (int lz = pcz - 3; lz <= pcz + 3; ++lz)
            {
                if (ws.theChunkProviderServer.loadChunk(lx, lz) == null) throw new IllegalStateException("chunk did not load");
            }
        }

        // the ground level and the pool
        int bx = MathHelper.floor_double(px) - 24, bz = MathHelper.floor_double(pz) - 8;
        int groundY = (int)py - 1;

        // the build's own setBlocks can wake entities (a flow, a falling
        // block); the scene's list holds its own spawns only, so the count
        // the tail check anchors on is taken after the build
        int entitiesBefore;

        // the build: the ground, the pool, the walls and the bat chamber (the
        // bodies below), then the count
        for (int x = 0; x < 48; ++x)
        {
            for (int z = 0; z < 32; ++z)
            {
                for (int y = groundY; y < groundY + 1; ++y) ws.setBlock(bx + x, y, bz + z, net.minecraft.init.Blocks.dirt, 0, 3);

                for (int y = groundY + 1; y < groundY + 12; ++y) ws.setBlock(bx + x, y, bz + z, net.minecraft.init.Blocks.air, 0, 3);
            }
        }

        // the pool: x 8..23 of the arena, water from groundY up to groundY+7,
        // a shelf of stone at groundY+3 for the shallow half
        for (int x = 8; x < 24; ++x)
        {
            for (int z = 0; z < 32; ++z)
            {
                int depth = x < 16 ? 7 : 4;

                for (int y = groundY + 1; y <= groundY + 1 + depth; ++y) ws.setBlock(bx + x, y, bz + z, net.minecraft.init.Blocks.water, 0, 3);
            }
        }

        // walls so nothing walks out; the pool's own corners are stone
        for (int x = 0; x < 48; ++x)
        {
            ws.setBlock(bx + x, groundY + 8, bz, net.minecraft.init.Blocks.cobblestone, 0, 3);
            ws.setBlock(bx + x, groundY + 8, bz + 31, net.minecraft.init.Blocks.cobblestone, 0, 3);
        }

        for (int z = 0; z < 32; ++z)
        {
            ws.setBlock(bx, groundY + 8, bz + z, net.minecraft.init.Blocks.cobblestone, 0, 3);
            ws.setBlock(bx + 47, groundY + 8, bz + z, net.minecraft.init.Blocks.cobblestone, 0, 3);
        }

        // the bat chamber: a dark 9x5x4 room one wall away from the pool
        int cx0 = bx + 26, cz0 = bz + 12, cy0 = groundY + 8;

        for (int x = 0; x < 9; ++x)
        {
            for (int z = 0; z < 5; ++z)
            {
                for (int y = 0; y < 4; ++y) ws.setBlock(cx0 + x, cy0 + y, cz0 + z, net.minecraft.init.Blocks.air, 0, 3);
            }
        }

        for (int x = -1; x <= 9; ++x)
        {
            for (int z = -1; z <= 5; ++z)
            {
                ws.setBlock(cx0 + x, cy0 - 1, cz0 + z, net.minecraft.init.Blocks.cobblestone, 0, 3);
                ws.setBlock(cx0 + x, cy0 + 4, cz0 + z, net.minecraft.init.Blocks.cobblestone, 0, 3);
            }
        }

        for (int y = 0; y < 4; ++y)
        {
            for (int z = -1; z <= 5; ++z)
            {
                ws.setBlock(cx0 - 1, cy0 + y, cz0 + z, net.minecraft.init.Blocks.cobblestone, 0, 3);
                ws.setBlock(cx0 + 9, cy0 + y, cz0 + z, net.minecraft.init.Blocks.cobblestone, 0, 3);
            }
        }

        for (int y = 0; y < 4; ++y)
        {
            for (int x = -1; x <= 9; ++x)
            {
                ws.setBlock(cx0 + x, cy0 + y, cz0 - 1, net.minecraft.init.Blocks.cobblestone, 0, 3);
                ws.setBlock(cx0 + x, cy0 + y, cz0 + 5, net.minecraft.init.Blocks.cobblestone, 0, 3);
            }
        }

        ws.setBlock(cx0 - 1, cy0, cz0 + 2, net.minecraft.init.Blocks.air, 0, 3);   /* the door: the player's way in */

        entitiesBefore = ws.loadedEntityList.size();

        player.setPosition(px, cy0 + 1.0D, pz + 2.5D);
        player.chunkCoordX = MathHelper.floor_double(player.posX / 16.0D);
        player.chunkCoordY = MathHelper.floor_double(player.posY / 16.0D);
        player.chunkCoordZ = MathHelper.floor_double(player.posZ / 16.0D);
        player.addedToChunk = true;
        Chunk pchunk = ws.getChunkFromChunkCoords(player.chunkCoordX, player.chunkCoordZ);
        pchunk.removeEntity(player);
        pchunk.addEntity(player);

        AnimalProbe.writeDetState(new File(dir, "start.txt"), seed, ws);
        PopulateProbe.rawClose();

        // ------------------------------------------------------------ spawns
        Random r = new Random(opseed);
        java.util.Map<Entity, Integer> spawnIndex = new java.util.IdentityHashMap<Entity, Integer>();
        List<Entity> list = new ArrayList<Entity>();
        byte[] spawnBuf = new byte[AnimalProbe.SPAWN_BYTES];
        OutputStream spawnsRaw = new BufferedOutputStream(new FileOutputStream(new File(dir, "spawns.bin")), 1 << 16);
        GZIPOutputStream spawnsTxtGz = new GZIPOutputStream(new FileOutputStream(new File(dir, "spawns.txt.gz")), 1 << 16);
        PrintWriter sw = new PrintWriter(new OutputStreamWriter(spawnsTxtGz, "UTF-8"));

        // 24 squids across the pool at varied depths, 8 bats in the chamber
        for (int i = 0; i < 32; ++i)
        {
            boolean squid = i < 24;
            int x, y, z;

            if (squid)
            {
                x = bx + 9 + r.nextInt(14);
                z = bz + 2 + r.nextInt(28);
                y = groundY + 2 + r.nextInt(6);
            }
            else
            {
                x = cx0 + 1 + r.nextInt(7);
                z = cz0 + 1 + r.nextInt(3);
                y = cy0 + r.nextInt(3);
            }

            float yaw = r.nextFloat() * 360.0F;
            Entity e = EntityList_create(squid ? "Squid" : "Bat", ws);
            e.setLocationAndAngles(x + 0.5D, y, z + 0.5D, yaw, 0.0F);
            ((EntityLiving)e).onSpawnWithEgg(null);

            if (!ws.spawnEntityInWorld(e)) throw new IllegalStateException("scene entity " + i + " did not spawn");

            spawnIndex.put(e, Integer.valueOf(i));
            list.add(e);

            int p = 0;
            p = AnimalProbe.le32(spawnBuf, p, i);
            p = AnimalProbe.le32(spawnBuf, p, e.getEntityId());
            p = AnimalProbe.le32(spawnBuf, p, squid ? 0 : 1);
            p = AnimalProbe.le64(spawnBuf, p, Double.doubleToRawLongBits(e.posX));
            p = AnimalProbe.le64(spawnBuf, p, Double.doubleToRawLongBits(e.posY));
            p = AnimalProbe.le64(spawnBuf, p, Double.doubleToRawLongBits(e.posZ));
            p = AnimalProbe.le32(spawnBuf, p, Float.floatToRawIntBits(e.rotationYaw));
            spawnsRaw.write(spawnBuf, 0, AnimalProbe.SPAWN_BYTES);
            sw.println(i + " " + e.getEntityId() + " " + (squid ? "Squid" : "Bat") + " " + AnimalProbe.canon(e));
        }

        spawnsRaw.close();
        sw.close();
        spawnsTxtGz.close();

        if (ws.loadedEntityList.size() != entitiesBefore + list.size())
        {
            throw new IllegalStateException("the world holds " + ws.loadedEntityList.size() + " entities, " + entitiesBefore + " before the setup and " + list.size() + " spawned");
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

        int absorbed = ws.loadedEntityList.size();
        byte[] entRec = new byte[AnimalProbe.ENT_STATE_BYTES];
        int deathTick = -1, deaths = 0;

        for (int t = 0; t < ticks; ++t)
        {
            Trace.t("tick", Integer.valueOf(t));

            for (int i = 0; i < list.size(); ++i)
            {
                Entity e = list.get(i);

                if (e.isDead) continue;

                ws.updateEntity(e);

                if (ws.loadedEntityList.size() > absorbed)
                {
                    for (int a = absorbed; a < ws.loadedEntityList.size(); ++a)
                    {
                        Entity n = (Entity)ws.loadedEntityList.get(a);

                        if (spawnIndex.containsKey(n)) continue;

                        spawnIndex.put(n, Integer.valueOf(list.size()));
                        list.add(n);
                    }

                    absorbed = ws.loadedEntityList.size();
                }

                if (e.isDead)
                {
                    if (deathTick < 0) deathTick = t;

                    ++deaths;

                    if (e.addedToChunk && ws.theChunkProviderServer.chunkExists(e.chunkCoordX, e.chunkCoordZ))
                    {
                        ws.getChunkFromChunkCoords(e.chunkCoordX, e.chunkCoordZ).removeEntity(e);
                    }

                    list.remove(i--);

                    rw.println(t + " " + spawnIndex.get(e).intValue() + " " + e.getEntityId() + " " + (e instanceof net.minecraft.entity.EntityLivingBase
                        ? hex(Float.floatToRawIntBits(((net.minecraft.entity.EntityLivingBase)e).getHealth())) : "-1") + " " + intOf(e, "fire"));
                }
            }

            byte[] head = new byte[8];
            int hp = 0;
            hp = AnimalProbe.le32(head, hp, t);
            hp = AnimalProbe.le32(head, hp, list.size());
            ticksOut.write(head, 0, 8);

            for (int i = 0; i < list.size(); ++i)
            {
                AnimalProbe.writeState(entRec, t, list.get(i), spawnIndex, ws);
                ticksOut.write(entRec, 0, AnimalProbe.ENT_STATE_BYTES);
            }

            if (nbtEvery > 0 && (t < 8 || t % nbtEvery == nbtEvery - 1 || t == ticks - 1))
            {
                nw.println("t " + t);

                for (int i = 0; i < list.size(); ++i)
                {
                    Entity e = list.get(i);
                    nw.println(spawnIndex.get(e).intValue() + " " + AnimalProbe.canon(e));
                }
            }

            AnimalProbe.writeDetLine(dw, t, ws);
        }

        ticksOut.close();
        ticksGz.close();
        dw.close();
        digestGz.close();
        rw.close();
        remGz.close();
        nw.close();
        nbtGz.close();
        AnimalProbe.writeDetState(new File(dir, "end.txt"), seed, ws);

        // ------------------------------------------------------------ manifest
        String manifest = readText(new File(dir, "manifest.json"));
        JsonObject m = (JsonObject)new com.google.gson.JsonParser().parse(manifest);
        JsonObject p = new JsonObject();
        p.addProperty("class", "SpawnProbe");
        p.addProperty("kind", "scene");
        p.addProperty("ticks", ticks);
        p.addProperty("opseed", opseed);
        p.addProperty("px", ServerTickProbe.dbits(px));
        p.addProperty("py", ServerTickProbe.dbits(py));
        p.addProperty("pz", ServerTickProbe.dbits(pz));
        p.addProperty("groundY", groundY);
        p.addProperty("bx", bx);
        p.addProperty("bz", bz);
        p.addProperty("cx0", cx0);
        p.addProperty("cz0", cz0);
        p.addProperty("cy0", cy0);
        p.addProperty("calMonth", Oracle.calendar().get(2) + 1);
        p.addProperty("calDay", Oracle.calendar().get(5));
        p.addProperty("deaths", deaths);
        m.add("probe", p);
        PrintWriter mw = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "manifest.json")), "UTF-8"));
        mw.println(m.toString());
        mw.close();

        JsonObject res = new JsonObject();
        res.addProperty("entities", list.size());
        res.addProperty("deaths", deaths);
        res.addProperty("dir", dir.getPath());
        return res;
    }

    /* ------------------------------------------------------------------ */
    /* the populate kind: performWorldGenSpawning over populate regions    */
    /* ------------------------------------------------------------------ */

    /* the populate kind's stage marks, the stage Random's state at each
     * vanilla marker, collected while one call runs */
    static JsonArray popMarks;

    static JsonObject dumpPopulate(IntegratedServer server, JsonObject cmd) throws Exception
    {
        File dir = new File(cmd.get("out").getAsString());
        dir.mkdirs();

        int x0 = cmd.get("x0").getAsInt(), z0 = cmd.get("z0").getAsInt();
        int width = cmd.has("width") ? cmd.get("width").getAsInt() : 3;

        Trace.restart();

        WorldServer ws = server.worldServers[0];
        long seed = ws.getSeed();
        Probe.rawChunks = true;

        // the probe thread's Det draws: register it as the server thread so
        // the worldgen constructors run on the SERVER role (the native check
        // seeds Det the same way)
        Thread self = Thread.currentThread();
        Det.clientThread = null;
        Det.serverThread = self;

        // the raw chunk dump opens before the ring load: the structure
        // generators carve during provideChunk, so the native side replays
        // these bytes instead of generating (the same replay the populate
        // probe's test runs)
        PopulateProbe.rawStart(dir);

        // the ring load exactly as PopulateProbe does it: cx-major for cx in
        // x0-1..x1+1, cz inner. Raw loads (population off): the population
        // happens inside the calls themselves, the way the server's own
        // populate walks run it.
        int x1 = x0 + width - 1, z1 = z0 + width - 1;

        for (int lx = x0 - 1; lx <= x1 + 1; ++lx)
        {
            for (int lz = z0 - 1; lz <= z1 + 1; ++lz)
            {
                if (ws.getChunkFromChunkCoords(lx, lz) == null) throw new IllegalStateException("chunk (" + lx + "," + lz + ") did not load");
            }
        }

        // the Det state entering the calls, AnimalProbe's writeDetState layout
        AnimalProbe.writeDetState(new File(dir, "start.txt"), seed, ws);

        // the populate calls, cx-major: for cx in x0..x1, for cz in z0..z1.
        // Per call: the biome at the chunk's (x+16, z+16), World.rand's state
        // entering and leaving (the spawning stage's picks and the sheep
        // fleeces are the World.rand draws a call makes), and the worldgen
        // spawn records (class, position, yaw, the entity Random's state, the
        // UUID, the sheep's fleece).
        GZIPOutputStream callsGz = new GZIPOutputStream(new FileOutputStream(new File(dir, "calls.jsonl.gz")), 1 << 16);
        PrintWriter cw = new PrintWriter(new OutputStreamWriter(callsGz, "UTF-8"));

        PopulateProbe.setMarkSink(new PopulateProbe.MarkSink()
        {
            public void mark(String stage, long seed)
            {
                JsonArray m = new JsonArray();
                m.add(new JsonPrimitive(stage));
                m.add(new JsonPrimitive(seed));
                popMarks.add(m);
            }
        });

        int calls = 0;
        long spawns = 0;

        for (int cx = x0; cx <= x1; ++cx)
        {
            for (int cz = z0; cz <= z1; ++cz)
            {
                JsonObject call = new JsonObject();
                call.addProperty("cx", cx);
                call.addProperty("cz", cz);
                call.addProperty("biome", ws.getBiomeGenForCoords(cx * 16 + 16, cz * 16 + 16).biomeID);
                call.addProperty("wrb", Snapshot.hex(Det.state((Random)ws.rand), 12));

                int n0 = ws.loadedEntityList.size();

                popMarks = new JsonArray();
                ws.theChunkProviderServer.populate(ws.theChunkProviderServer, cx, cz);
                call.add("marks", popMarks);
                popMarks = null;

                call.addProperty("wra", Snapshot.hex(Det.state((Random)ws.rand), 12));

                JsonArray sp = new JsonArray();

                for (int i = n0; i < ws.loadedEntityList.size(); ++i)
                {
                    Entity e = (Entity)ws.loadedEntityList.get(i);
                    JsonObject o = new JsonObject();
                    o.addProperty("cls", e.getClass().getSimpleName());
                    o.addProperty("x", ServerTickProbe.dbits(e.posX));
                    o.addProperty("y", ServerTickProbe.dbits(e.posY));
                    o.addProperty("z", ServerTickProbe.dbits(e.posZ));
                    o.addProperty("yaw", ServerTickProbe.fbits(e.rotationYaw));
                    o.addProperty("rnd", Snapshot.hex(Det.state(AnimalProbe.entityRand(e)), 12));
                    o.addProperty("uuidM", "l:" + e.getUniqueID().getMostSignificantBits());
                    o.addProperty("uuidL", "l:" + e.getUniqueID().getLeastSignificantBits());

                    if (e instanceof EntitySheep) o.addProperty("fleece", ((EntitySheep)e).getFleeceColor());

                    sp.add(o);
                    ++spawns;
                }

                call.add("spawns", sp);
                cw.println(call.toString());
                ++calls;
            }
        }

        cw.close();
        callsGz.close();
        PopulateProbe.rawClose();

        Probe.rawChunks = false;
        Det.clientThread = Thread.currentThread();
        Det.serverThread = null;

        // the Det state leaving the calls
        AnimalProbe.writeDetState(new File(dir, "end.txt"), seed, ws);

        // the probe's own manifest: the test's input
        JsonObject m = new JsonObject();
        m.addProperty("kind", "populate-spawn");
        m.addProperty("seed", seed);
        m.addProperty("x0", x0);
        m.addProperty("z0", z0);
        m.addProperty("width", width);
        m.addProperty("calls", calls);
        m.addProperty("spawns", spawns);
        PrintWriter mw = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "manifest.json")), "UTF-8"));
        mw.println(m.toString());
        mw.close();

        JsonObject res = new JsonObject();
        res.addProperty("calls", calls);
        res.addProperty("spawns", spawns);
        res.addProperty("dir", dir.getPath());
        return res;
    }

    static Entity EntityList_create(String name, WorldServer ws)
    {
        return net.minecraft.entity.EntityList.createEntityByName(name, ws);
    }

    static int intOf(Entity e, String name)
    {
        return AnimalProbe.intField(e, name);
    }

    static String hex(int bits)
    {
        StringBuilder b = new StringBuilder(8);

        for (int i = 7; i >= 0; --i) b.append("0123456789abcdef".charAt((bits >>> (i * 4)) & 15));

        return b.toString();
    }
}
