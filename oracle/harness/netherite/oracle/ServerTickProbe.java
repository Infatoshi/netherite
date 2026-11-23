package netherite.oracle;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import com.google.gson.JsonPrimitive;
import java.io.BufferedOutputStream;
import java.io.DataOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.util.List;
import java.util.Random;
import java.util.Set;
import java.util.TreeSet;
import java.util.zip.GZIPOutputStream;
import net.minecraft.block.Block;
import net.minecraft.block.BlockEventData;
import net.minecraft.entity.Entity;
import net.minecraft.entity.item.EntityItem;
import net.minecraft.entity.effect.EntityLightningBolt;
import net.minecraft.entity.player.EntityPlayerMP;
import net.minecraft.item.ItemStack;
import net.minecraft.util.MathHelper;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.world.ChunkCoordIntPair;
import net.minecraft.world.NextTickListEntry;
import net.minecraft.world.World;
import net.minecraft.world.WorldServer;
import net.minecraft.world.chunk.Chunk;
import net.minecraft.world.chunk.storage.ExtendedBlockStorage;
import net.minecraft.world.storage.WorldInfo;

/**
 * Server tick probe: WorldServer.tick() (and World.tick() inside it) on the
 * overworld, tick after tick, from a real spawn-area state.
 *
 * The start state is a snapshot in the tape format (Snapshot.java's writer:
 * chunks, entities, pending block ticks, worldinfo, worldstate, det), taken
 * after the mob spawning gamerule is forced off and after the command's weather
 * is forced, so the same files the native tape replay loads are the files the
 * native server tick loads. Then ws.tick() runs N times on this probe's own
 * thread while the server is parked (the player stands still and no entity is
 * ever ticked), with the thread bound to Det's SERVER role: the draws the tick
 * makes inside entity constructors and Math.random go to the streams a real
 * server tick draws from.
 *
 * Recorded per tick, in order:
 *   tickwrites.bin      every block write (x, y, z int32, id, meta) through
 *                       Rows.onBlock, concatenated; the count is in the row
 *   spawns.jsonl.gz     every entity the tick spawned, with its Det draws
 *                       already spent: class, id, position, motion, item
 *   tickrows.jsonl.gz   the scalars: World.rand state, updateLCG, world time
 *                       and total time, the rain and thunder flags, timers and
 *                       strengths, skylightSubtracted, the pending list size and
 *                       a digest of its first entries in tree order, the block
 *                       event queue, and Det's SERVER digests
 * And once at the end: end-chunks.bin.gz, end-chunkstate.jsonl,
 * end-ticks.jsonl.gz, end-worldstate.nbt, end-worldinfo.nbt (the same layouts
 * the start snapshot uses).
 *
 * Runs on its own thread (the OTHER role, like Probe and ChunkDump) while the
 * server is parked, so no CLIENT or SERVER RNG stream moves outside the tick.
 *
 * Output DIR/manifest.json (the snapshot manifest, with the probe's own keys
 * and the layout strings added), DIR/tickrows.jsonl.gz, DIR/tickwrites.bin,
 * DIR/spawns.jsonl.gz, the snapshot files, and the end-* files.
 */
public final class ServerTickProbe
{
    /** Entries of the pending list the per-tick digest covers. */
    static final int SAMPLE = 16;

    private ServerTickProbe() {}

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
        }, "Oracle Server Tick Probe");
        t.start();
        t.join();
        if (error[0] != null) throw error[0];
        return result[0];
    }

    static JsonObject dump(IntegratedServer server, JsonObject cmd) throws Exception
    {
        File dir = new File(cmd.get("out").getAsString());
        dir.mkdirs();
        if (!dir.isDirectory()) throw new IllegalStateException("ServerTickProbe: " + dir + " is not a directory");

        int ticks = cmd.has("ticks") ? cmd.get("ticks").getAsInt() : 2400;
        boolean rain = cmd.has("rain") && cmd.get("rain").getAsBoolean();
        boolean thunder = cmd.has("thunder") && cmd.get("thunder").getAsBoolean();
        boolean strict = !cmd.has("strict") || cmd.get("strict").getAsBoolean();
        int rainTime = cmd.has("rainTime") ? cmd.get("rainTime").getAsInt() : 12000;
        int thunderTime = cmd.has("thunderTime") ? cmd.get("thunderTime").getAsInt() : 12000;

        WorldServer ws = server.worldServers[0];
        WorldInfo wi = ws.getWorldInfo();

        // the lane never spawns mobs: the gamerule is what WorldServer.tick reads
        String wasMobSpawning = ws.getGameRules().getGameRuleStringValue("doMobSpawning");
        ws.getGameRules().setOrCreateGameRule("doMobSpawning", "false");

        // force the weather so the ice, snow and thunder branches run; the
        // strengths ramp from 0 the vanilla way (isRaining() is > 0.2)
        if (rain)
        {
            wi.setRaining(true);
            wi.setRainTime(rainTime);
        }

        if (thunder)
        {
            wi.setThundering(true);
            wi.setThunderTime(thunderTime);
        }

        // the chunk set the tick walks, in the HashSet's own iteration order;
        // the player is parked, so it is the same set in the same order every
        // tick and the native side can hold the recorded order
        int view = (Integer)Snapshot.invoke(server.getConfigurationManager(), "getViewDistance");
        JsonArray active = new JsonArray();

        for (Object o : (Iterable)Snapshot.objField(ws, "activeChunkSet"))
        {
            ChunkCoordIntPair p = (ChunkCoordIntPair)o;
            JsonArray pair = new JsonArray();
            pair.add(new JsonPrimitive(p.chunkXPos));
            pair.add(new JsonPrimitive(p.chunkZPos));
            active.add(pair);
        }

        // the block ids a random tick can land on in the loaded set: every id
        // whose block ticks randomly and appears in a section that needs
        // random ticking. The native dispatch has to cover all of them.
        JsonArray tickIds = randomTickIds(ws);

        // Chunk generation, population and unloading belong to the worldgen
        // lanes. Two probe settings keep this tick inside the loaded set:
        //
        //   loadChunkOnProvideRequest = false  World.getBlock (and the light
        //     engine, the precipitation height, getSavedLightValue) reads an
        //     EmptyChunk outside the loaded set instead of generating one:
        //     air, light 0, sky invisible, and a write there is dropped. The
        //     native world core mirrors it with its own no-generate flag.
        //   the join's unload queue is cancelled  its 52 chunks stand inside
        //     the tick's own set, and unloading them would only make the next
        //     tick see air where the recording says blocks.
        //
        // Both are recorded; the run fails if the loaded set moves anyway.
        Object cps = ws.theChunkProviderServer;
        Snapshot.field(cps, "loadChunkOnProvideRequest").setBoolean(cps, false);
        Set<String> unloadQueue = (Set<String>)(Set)Snapshot.objField(cps, "chunksToUnload");
        int unloadQueueAtStart = unloadQueue.size();
        unloadQueue.clear();
        int chunksAtStart = loadedChunks(ws);

        // the state the tick body reads that the snapshot files do not carry:
        // the ambient countdown the chunk walk decrements and the mood sound
        // draws behind (World's constructor seeded it from World.rand), the
        // difficulty the lightning bolt's fire placement reads, and the parked
        // player's own position (getClosestPlayer and the per-tick light check
        // walk it; the player is never updated, so it is constant)
        int ambStart = Snapshot.intField(ws, "ambientTickCountdown");
        int difficulty = ws.difficultySetting.getDifficultyId();
        EntityPlayerMP parked = ws.playerEntities.isEmpty() ? null : (EntityPlayerMP)ws.playerEntities.get(0);
        JsonObject scmd = new JsonObject();
        scmd.addProperty("out", dir.getPath());
        JsonObject snap = Snapshot.dump(server, scmd);

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
                    try { flushWrites(wr, wbuf, wlen); }
                    catch (Exception e) { throw new RuntimeException(e); }
                }
                put32(wbuf, wlen[0], x);
                put32(wbuf, wlen[0] + 4, y);
                put32(wbuf, wlen[0] + 8, z);
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

        chunkSnapshot(ws);
        int wrote = 0, spawned = 0, events = 0;

        try
        {
            for (int t = 1; t <= ticks; ++t)
            {
                int nLoaded = ws.loadedEntityList.size();
                int nWeather = ws.weatherEffects.size();
                wcount[0] = 0;
                ws.tick();
                flushWrites(wr, wbuf, wlen);
                int w = wcount[0];
                chunkMove(ws, t, strict);


                for (int i = nLoaded; i < ws.loadedEntityList.size(); ++i)
                {
                    spawnLine(spawns, t, 0, (Entity)ws.loadedEntityList.get(i));
                    ++spawned;
                }

                for (int i = nWeather; i < ws.weatherEffects.size(); ++i)
                {
                    spawnLine(spawns, t, 1, (Entity)ws.weatherEffects.get(i));
                    ++spawned;
                }

                int[] ev = blockEvents(ws);

                if (ev.length > 0)
                {
                    events += ev.length;

                    for (int i = 0; i < ev.length; i += 5)
                    {
                        JsonObject o = new JsonObject();
                        o.addProperty("t", t);
                        o.addProperty("x", ev[i]);
                        o.addProperty("y", ev[i + 1]);
                        o.addProperty("z", ev[i + 2]);
                        o.addProperty("id", ev[i + 3]);
                        o.addProperty("ev", ev[i + 4]);
                        spawns.println(o.toString());
                    }
                }

                int sNow = ws.loadedEntityList.size() + ws.weatherEffects.size();
                rows.println(tickRow(ws, t, w, sNow - nLoaded - nWeather, ev.length).toString());
                wrote += w;

                if (t % 200 == 0) System.out.println("SERVERTICK t=" + t + " writes=" + wrote + " spawned=" + spawned);
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

        // ------------------------------------------------------------ the end
        int chunksEnd = writeRegion(ws, dir);
        PrintWriter et = new PrintWriter(new OutputStreamWriter(new GZIPOutputStream(new FileOutputStream(new File(dir, "end-ticks.jsonl.gz")), 1 << 16), "UTF-8"));

        for (Object o : iter(ws, "pendingTickListEntriesTreeSet")) et.println(Snapshot.tickLine((NextTickListEntry)o, "pending"));
        for (Object o : iter(ws, "pendingTickListEntriesThisTick")) et.println(Snapshot.tickLine((NextTickListEntry)o, "thisTick"));
        et.close();

        Snapshot.writeFile(new File(dir, "end-worldstate.nbt"), Snapshot.worldState(ws));
        JsonObject ewi = (JsonObject)StructuresProbe.canon(wi.getNBTTagCompound());
        ewi.add("LastPlayed", new JsonPrimitive("l:0"));
        Snapshot.writeFile(new File(dir, "end-worldinfo.nbt"), Snapshot.canonText(ewi));

        // ------------------------------------------------------ the manifest
        String manifest = readText(new File(dir, "manifest.json"));
        JsonObject m = (JsonObject)new com.google.gson.JsonParser().parse(manifest);
        JsonObject p = new JsonObject();
        p.addProperty("class", "ServerTickProbe");
        p.addProperty("ticks", ticks);
        p.addProperty("rain", rain ? 1 : 0);
        p.addProperty("thunder", thunder ? 1 : 0);
        p.addProperty("rainTime", rainTime);
        p.addProperty("thunderTime", thunderTime);
        p.addProperty("doMobSpawningWas", wasMobSpawning);
        p.addProperty("viewDistance", view);
        p.addProperty("activeChunks", active.size());
        p.addProperty("unloadQueueAtStart", unloadQueueAtStart);
        p.addProperty("noChunkGeneration", 1);
        p.addProperty("chunksAtStart", chunksAtStart);
        p.addProperty("ambientTickCountdown", ambStart);
        p.addProperty("difficulty", difficulty);
        p.addProperty("playerX", parked == null ? "d:0000000000000000" : dbits(parked.posX));
        p.addProperty("playerY", parked == null ? "d:0000000000000000" : dbits(parked.posY));
        p.addProperty("playerZ", parked == null ? "d:0000000000000000" : dbits(parked.posZ));
        p.addProperty("playerYaw", parked == null ? "f:00000000" : fbits(parked.rotationYaw));
        p.addProperty("writes", wrote);
        p.addProperty("spawns", spawned);
        p.addProperty("blockEvents", events);
        p.addProperty("chunksAtEnd", chunksEnd);
        p.addProperty("pendingAtEnd", pendingAtEnd(ws));
        m.add("servertick", p);
        m.add("activeChunkOrder", active);
        m.add("randomTickIds", tickIds);
        JsonObject f = (JsonObject)m.get("files");
        f.addProperty("tickrows.jsonl.gz", "gzip of one object per tick: t, writes, spawns, events, rand (l:), lcg, time (l:), total (l:), rain, thunder, rainTime, thunderTime, rainS/thunderS/prevRainS/prevThunderS (f: raw bits), sky, amb, pending, thisTick, pfirst (the first pending entry as [x,y,z,id,t,pri,eid] as a JSON array of strings), pdig (l: the digest below), sseed/smath/sstat (l:), sid, sleep, queue");
        f.addProperty("tickwrites.bin", "per write: x int32 LE, y int32 LE, z int32 LE, id int16 LE (0xffff is a metadata-only write), meta uint8, one pad byte; the per-tick counts are in tickrows.jsonl.gz");
        f.addProperty("spawns.jsonl.gz", "one object per spawned entity (t, dim, id, class, x/y/z as d: raw bits, mx/my/mz, and the class's own fields) and per block event (t, x, y, z, id, ev)");
        f.addProperty("end-chunks.bin.gz", "the snapshot's chunks.bin.gz layout, at the end of the last tick");
        f.addProperty("end-chunkstate.jsonl", "the snapshot's chunkstate.jsonl layout, at the end");
        f.addProperty("end-ticks.jsonl.gz", "the snapshot's ticks.jsonl.gz layout, at the end (tree order, then thisTick)");
        f.addProperty("end-worldstate.nbt", "the snapshot's worldstate.nbt layout, at the end");
        f.addProperty("end-worldinfo.nbt", "the snapshot's worldinfo.nbt layout, at the end");
        m.addProperty("pdig", "FNV-1a 64 over the first min(16, pending) entries in tree order: (x,y,z,id,t,pri,eid) each folded as a 64-bit value, then the count; 0xcbf29ce484222325 offset, 0x100000001b3 prime");
        m.addProperty("active_chunk_order", "the World.activeChunkSet iteration order the tick walks, [cx,cz] per entry; the player is parked, so it does not change");
        m.addProperty("random_tick_ids", "ids of blocks with getTickRandomly() that stand in the tick's chunk set at tick 0");
        Snapshot.writeFile(new File(dir, "manifest.json"), m.toString());

        System.out.println("ORACLE SERVERTICK " + dir + " ticks=" + ticks + " writes=" + wrote + " spawns=" + spawned
            + " events=" + events + " chunksEnd=" + chunksEnd + " pendingEnd=" + pendingAtEnd(ws)
            + " randEnd=" + Det.state(ws.rand));

        JsonObject r = new JsonObject();
        r.addProperty("dir", dir.getPath());
        r.addProperty("ticks", ticks);
        r.addProperty("writes", wrote);
        r.addProperty("spawns", spawned);
        r.addProperty("chunksAtEnd", chunksEnd);
        r.addProperty("pendingAtEnd", pendingAtEnd(ws));
        r.addProperty("chunks", snap.get("chunks").getAsInt());
        return r;
    }

    // ------------------------------------------------------------------ rows

    /** One tick's scalars, in the order the manifest's layout lists them. */
    static JsonObject tickRow(WorldServer ws, int t, int writes, int spawned, int events) throws Exception
    {
        WorldInfo wi = ws.getWorldInfo();
        JsonObject o = new JsonObject();
        o.addProperty("t", t);
        o.addProperty("writes", writes);
        o.addProperty("spawns", spawned);
        o.addProperty("events", events);
        o.addProperty("rand", "l:" + Det.state(ws.rand));
        o.addProperty("lcg", Snapshot.intField(ws, "updateLCG"));
        o.addProperty("time", "l:" + wi.getWorldTime());
        o.addProperty("total", "l:" + wi.getWorldTotalTime());
        o.addProperty("rain", wi.isRaining() ? 1 : 0);
        o.addProperty("thunder", wi.isThundering() ? 1 : 0);
        o.addProperty("rainTime", wi.getRainTime());
        o.addProperty("thunderTime", wi.getThunderTime());
        o.addProperty("rainS", fbits(Snapshot.floatField(ws, "rainingStrength")));
        o.addProperty("thunderS", fbits(Snapshot.floatField(ws, "thunderingStrength")));
        o.addProperty("prevRainS", fbits(Snapshot.floatField(ws, "prevRainingStrength")));
        o.addProperty("prevThunderS", fbits(Snapshot.floatField(ws, "prevThunderingStrength")));
        o.addProperty("sky", Snapshot.intField(ws, "skylightSubtracted"));
        o.addProperty("amb", Snapshot.intField(ws, "ambientTickCountdown"));
        o.addProperty("sleep", Snapshot.boolField(ws, "allPlayersSleeping") ? 1 : 0);
        o.addProperty("qT", Snapshot.intField(ws, "field_147489_T"));
        o.addProperty("sseed", "l:" + Det.seederState(Det.SERVER));
        o.addProperty("smath", "l:" + Det.mathState(Det.SERVER));
        o.addProperty("sstat", "l:" + Det.splitState(Det.SERVER));
        o.addProperty("sid", Det.nextId[Det.SERVER]);

        TreeSet pending = (TreeSet)Snapshot.objField(ws, "pendingTickListEntriesTreeSet");
        o.addProperty("pending", pending.size());
        o.addProperty("thisTick", ((List)Snapshot.objField(ws, "pendingTickListEntriesThisTick")).size());

        long h = 0xcbf29ce484222325L;
        int n = 0;
        JsonArray first = new JsonArray();

        for (Object x : pending)
        {
            NextTickListEntry e = (NextTickListEntry)x;

            if (n < SAMPLE)
            {
                h = fold(h, e.xCoord);
                h = fold(h, e.yCoord);
                h = fold(h, e.zCoord);
                h = fold(h, Block.getIdFromBlock(e.func_151351_a()));
                h = fold(h, e.scheduledTime);
                h = fold(h, e.priority);
                h = fold(h, Snapshot.longField(e, "tickEntryID"));
            }

            if (n == 0)
            {
                first.add(new JsonPrimitive(e.xCoord));
                first.add(new JsonPrimitive(e.yCoord));
                first.add(new JsonPrimitive(e.zCoord));
                first.add(new JsonPrimitive(Block.getIdFromBlock(e.func_151351_a())));
                first.add(new JsonPrimitive("l:" + e.scheduledTime));
                first.add(new JsonPrimitive(e.priority));
                first.add(new JsonPrimitive("l:" + Snapshot.longField(e, "tickEntryID")));
            }

            if (++n == SAMPLE) break;
        }

        o.addProperty("pdig", "l:" + fold(h, n));
        o.add("pfirst", first);
        return o;
    }

    static long fold(long h, long v)
    {
        return (h ^ v) * 0x100000001b3L;
    }

    static String fbits(float f)
    {
        return "f:" + Snapshot.hex(Float.floatToRawIntBits(f) & 4294967295L, 8);
    }

    // ---------------------------------------------------------------- spawns

    static void spawnLine(PrintWriter w, int t, int weather, Entity e) throws Exception
    {
        JsonObject o = new JsonObject();
        o.addProperty("t", t);
        o.addProperty("dim", e.dimension);
        o.addProperty("id", e.getEntityId());
        o.addProperty("class", e.getClass().getSimpleName());
        o.addProperty("weather", weather);
        o.addProperty("x", dbits(e.posX));
        o.addProperty("y", dbits(e.posY));
        o.addProperty("z", dbits(e.posZ));
        o.addProperty("mx", dbits(e.motionX));
        o.addProperty("my", dbits(e.motionY));
        o.addProperty("mz", dbits(e.motionZ));
        o.addProperty("yaw", fbits(e.rotationYaw));
        net.minecraft.nbt.NBTTagCompound tag = new net.minecraft.nbt.NBTTagCompound();
        e.writeToNBT(tag);
        o.add("nbt", StructuresProbe.canon(tag));

        if (e instanceof EntityItem)
        {
            EntityItem it = (EntityItem)e;
            ItemStack s = it.getEntityItem();
            o.addProperty("age", it.age);
            o.addProperty("dly", it.delayBeforeCanPickup);
            o.addProperty("hover", fbits(it.hoverStart));
            o.addProperty("item", s == null ? -1 : net.minecraft.item.Item.getIdFromItem(s.getItem()));
            o.addProperty("dmg", s == null ? -1 : s.getItemDamage());
            o.addProperty("cnt", s == null ? -1 : s.stackSize);
        }
        else if (e instanceof EntityLightningBolt)
        {
            EntityLightningBolt b = (EntityLightningBolt)e;
            o.addProperty("boltVertex", "l:" + b.boltVertex);
            o.addProperty("boltLivingTime", Snapshot.intField(b, "boltLivingTime"));
        }

        w.println(o.toString());
    }

    static String dbits(double d)
    {
        return "d:" + Snapshot.hex(Double.doubleToRawLongBits(d), 16);
    }

    // --------------------------------------------------------- block events

    /** The block event queues' contents as x, y, z, block id, event id, five per
     * entry; empty in a tick nothing queues into (no tile entity ticks here). */
    static int[] blockEvents(WorldServer ws)
    {
        Object[] lists = (Object[])Snapshot.objField(ws, "field_147490_S");
        int n = 0;

        for (Object l : lists) n += ((List)l).size();

        int[] out = new int[5 * n];
        int k = 0;

        for (Object l : lists)
        {
            for (Object o : (List)l)
            {
                BlockEventData d = (BlockEventData)o;
                out[k++] = d.func_151340_a();
                out[k++] = d.func_151342_b();
                out[k++] = d.func_151341_c();
                out[k++] = Block.getIdFromBlock(d.getBlock());
                out[k++] = d.getEventID();
            }
        }

        return out;
    }

    // ------------------------------------------------------------ the region

    /** The snapshot's chunk files, at the end of the run. */
    static int writeRegion(WorldServer ws, File dir) throws Exception
    {
        List chunks = ws.theChunkProviderServer.func_152380_a();
        byte[] buf = new byte[Probe.CHUNK_BYTES];
        byte[] nbuf = new byte[Probe.CHUNK_BYTES];
        DataOutputStream out = new DataOutputStream(new BufferedOutputStream(new GZIPOutputStream(new FileOutputStream(new File(dir, "end-chunks.bin.gz")), 1 << 16), 1 << 20));
        PrintWriter state = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "end-chunkstate.jsonl")), "UTF-8"));
        int n = 0;

        for (Object o : chunks)
        {
            Chunk c = (Chunk)o;
            Probe.fillChunkBytes(c, buf);
            Snapshot.le32(out, c.xPosition);
            Snapshot.le32(out, c.zPosition);
            Snapshot.le64(out, fnv(buf));
            Snapshot.le64(out, Snapshot.hashAround(ws, c.xPosition, c.zPosition, nbuf));
            out.write(buf);

            JsonObject s = new JsonObject();
            s.addProperty("cx", c.xPosition);
            s.addProperty("cz", c.zPosition);
            s.addProperty("terrainPopulated", c.isTerrainPopulated ? 1 : 0);
            s.addProperty("lightPopulated", c.isLightPopulated ? 1 : 0);
            s.addProperty("populated", c.field_150815_m ? 1 : 0);
            s.addProperty("modified", c.isModified ? 1 : 0);
            s.addProperty("queuedLightChecks", Snapshot.intField(c, "queuedLightChecks"));
            s.addProperty("skylightColumns", Snapshot.hexBools(c.updateSkylightColumns));
            s.addProperty("gap", Snapshot.boolField(c, "isGapLightingUpdated") ? 1 : 0);
            state.println(Snapshot.canonText(s));
            ++n;
        }

        state.close();
        out.close();
        return n;
    }

    static long fnv(byte[] b)
    {
        long h = Probe.FNV_OFFSET;
        for (int i = 0; i < b.length; ++i) h = (h ^ (b[i] & 255)) * Probe.FNV_PRIME;
        return h;
    }

    // ------------------------------------------------------------- the setup

    /** Every block id with getTickRandomly() that stands in a section that needs
     * random ticking, over the tick's chunk set. */
    static JsonArray randomTickIds(WorldServer ws)
    {
        Set<Integer> ids = new java.util.TreeSet<Integer>();

        for (Object o : (Iterable)Snapshot.objField(ws, "activeChunkSet"))
        {
            ChunkCoordIntPair p = (ChunkCoordIntPair)o;
            Chunk c = ws.getChunkFromChunkCoords(p.chunkXPos, p.chunkZPos);
            ExtendedBlockStorage[] a = c.getBlockStorageArray();

            for (ExtendedBlockStorage s : a)
            {
                if (s == null || !s.getNeedsRandomTick()) continue;

                for (int y = 0; y < 16; ++y)
                    for (int z = 0; z < 16; ++z)
                        for (int x = 0; x < 16; ++x)
                        {
                            Block b = s.func_150819_a(x, y, z);

                            if (b.getTickRandomly()) ids.add(Block.getIdFromBlock(b));
                        }
            }
        }

        JsonArray out = new JsonArray();
        for (Integer i : ids) out.add(new JsonPrimitive(i));
        return out;
    }

    // --------------------------------------------------------------- helpers

    static void flushWrites(DataOutputStream wr, byte[] buf, int[] len) throws Exception
    {
        if (len[0] == 0) return;
        wr.write(buf, 0, len[0]);
        len[0] = 0;
    }

    static void put32(byte[] b, int o, int v)
    {
        b[o] = (byte)v;
        b[o + 1] = (byte)(v >> 8);
        b[o + 2] = (byte)(v >> 16);
        b[o + 3] = (byte)(v >> 24);
    }

    /** The loaded chunk set, watched across the tick: with the preload in place
     * it must not move, and a tick that generates or unloads one is a probe
     * setting this lane cannot cover (worldgen lanes own chunk loading). */
    static final java.util.HashSet<Long> chunkSet = new java.util.HashSet<Long>();

    static void chunkSnapshot(WorldServer ws) throws Exception
    {
        chunkSet.clear();
        chunkSet.addAll(chunkCoords(ws));
    }

    static java.util.HashSet<Long> chunkCoords(WorldServer ws) throws Exception
    {
        java.util.HashSet<Long> now = new java.util.HashSet<Long>();

        for (Object o : (List)Snapshot.objField(ws.theChunkProviderServer, "loadedChunks"))
        {
            Chunk c = (Chunk)o;
            now.add(((long)c.xPosition << 32) | (c.zPosition & 0xffffffffL));
        }

        return now;
    }

    static void chunkMove(WorldServer ws, int t, boolean strict) throws Exception
    {
        Set q = (Set)Snapshot.objField(ws.theChunkProviderServer, "chunksToUnload");
        java.util.HashSet<Long> now = chunkCoords(ws);

        if (q.isEmpty() && now.equals(chunkSet)) return;

        StringBuilder b = new StringBuilder();
        for (Long k : now) if (!chunkSet.contains(k)) b.append(" +").append((int)(k >> 32)).append(",").append((int)(long)k);
        for (Long k : chunkSet) if (!now.contains(k)) b.append(" -").append((int)(k >> 32)).append(",").append((int)(long)k);
        System.out.println("CHUNKS t=" + t + " " + chunkSet.size() + " -> " + now.size() + b);
        chunkSet.clear();
        chunkSet.addAll(now);

        throw new IllegalStateException("ServerTickProbe: the tick moved a chunk at t=" + t + " (queue " + q.size() + ")" + b);
    }

    /** The pending block ticks still parked, the TreeSet's size. */
    static int pendingAtEnd(WorldServer ws)
    {
        return ((TreeSet)Snapshot.objField(ws, "pendingTickListEntriesTreeSet")).size();
    }

    /** ChunkProviderServer.loadedChunks, the set the tick must not change. */
    static int loadedChunks(WorldServer ws)
    {
        return ((List)Snapshot.objField(ws.theChunkProviderServer, "loadedChunks")).size();
    }

    static Iterable iter(WorldServer ws, String name)
    {
        return (Iterable)Snapshot.objField(ws, name);
    }

    static String readText(File f) throws Exception
    {
        java.io.BufferedReader r = new java.io.BufferedReader(new java.io.InputStreamReader(new java.io.FileInputStream(f), "UTF-8"));
        StringBuilder b = new StringBuilder();
        String line;

        while ((line = r.readLine()) != null) b.append(line).append('\n');
        r.close();
        return b.toString();
    }
}