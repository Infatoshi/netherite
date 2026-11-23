package netherite.oracle;

import com.google.gson.JsonObject;
import com.google.gson.JsonPrimitive;
import java.io.File;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.util.List;
import net.minecraft.client.Minecraft;
import net.minecraft.entity.Entity;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.util.ChunkCoordinates;
import net.minecraft.world.WorldServer;

/**
 * The Nether and End spawn areas: the 625 chunks MinecraftServer.
 * initialWorldChunkLoad would generate around the dimension's own spawn point,
 * loaded through the dimensions' own ChunkProviderServer, in the same order the
 * overworld's initial load uses (x outer, z inner, step 16). This is the
 * reference for the native dimension build (csrc/engine/seedworld.c,
 * seedworld_init_dim, checked by csrc/tests/test_seedworld.c).
 *
 * The Nether and the End are never loaded at world start (MinecraftServer.
 * loadAllWorlds only runs initialWorldChunkLoad for worldServers[0]); a player
 * reaching them makes the server generate the square around the dimension's
 * spawn point on the server thread, which is what this probe does here - on its
 * own thread, bound to Det's SERVER role, while the server is parked, so every
 * draw lands in the stream the real server would use.
 *
 * Each dimension's createSpawnPosition takes WorldServer.createSpawnPosition's
 * !canRespawnHere branch (both providers return false), so the spawn point is
 * (0, WorldProvider.getAverageGroundLevel(), 0): 64 for the Nether (the default
 * is 64), 50 for the End (WorldProviderEnd overrides it). The WorldInfo the
 * dimension eventually holds is a DerivedWorldInfo over the overworld's, whose
 * setSpawnPosition is a no-op, so ws.getSpawnPoint() reads the overworld's
 * spawn: the manifest carries both, and the native check compares "spawn".
 *
 * Run with {"cmd":"run","class":"SpawnDumpDim","dim":-1,"out":"DIR"}: the dump
 * happens at the top of tick 0, before the join, with the overworld's
 * initialWorldChunkLoad already done (its writes are in the run's block chain,
 * which this dump restarts so the file carries the dimension's own writes).
 *
 * DIR is a snapshot (Snapshot.java's format, so csrc/engine/snapshot.c loads it),
 * with animals.jsonl as the dimension's own world-generation entities.
 */
final class SpawnDumpDim
{
    private SpawnDumpDim() {}

    /** --spawndumpdim DIM:DIR; dim is 3 when the run does not dump. */
    static String dir;
    static int dim = 3;
    static boolean done;

    /** The "--spawndumpdim DIM:DIR" argument: the dimension and the dump's path. */
    static void parse(String arg)
    {
        int c = arg.indexOf(':');

        if (c <= 0) throw new IllegalArgumentException("SpawnDumpDim: pass DIM:DIR, not " + arg);

        try
        {
            dim = Integer.parseInt(arg.substring(0, c));
        }
        catch (NumberFormatException e)
        {
            throw new IllegalArgumentException("SpawnDumpDim: " + arg.substring(0, c) + " is not a dimension");
        }

        dir = arg.substring(c + 1);
    }

    /** Called from Oracle.preTick on the client thread, before tick 0's client tick. */
    static void early(Minecraft mc)
    {
        done = true;

        try
        {
            IntegratedServer server = mc.getIntegratedServer();
            if (server == null) throw new IllegalStateException("SpawnDumpDim: no integrated server at tick 0");
            JsonObject r = runDump(server, dim, new File(dir));
            System.out.println("ORACLE SPAWNDUMPDIM " + r);
        }
        catch (Exception e)
        {
            throw new RuntimeException("SpawnDumpDim: " + e, e);
        }
    }

    /** The generic `run` command's entry. It has to be issued at tick 0, or the
     * scheduled times the server tick moved would not be the dump's. */
    static JsonObject run(IntegratedServer server, JsonObject cmd) throws Exception
    {
        if (Oracle.tick != 0)
            throw new IllegalStateException("SpawnDumpDim: the run command landed at tick " + Oracle.tick
                + ", not tick 0 (use --spawndumpdim DIM:DIR)");

        return runDump(server, cmd.get("dim").getAsInt(),
                       new File(cmd.has("out") ? cmd.get("out").getAsString() : dir));
    }

    static JsonObject runDump(final IntegratedServer server, final int dim, final File d) throws Exception
    {
        final JsonObject[] result = new JsonObject[1];
        final Exception[] error = new Exception[1];
        Thread t = new Thread(new Runnable()
        {
            public void run()
            {
                Thread self = Thread.currentThread();
                Thread oldClient = Det.clientThread, oldServer = Det.serverThread;
                boolean oldRender = Det.inRender;
                // the dimension's chunk load runs on the server thread in a real
                // run, so its entity draws (the End's dragon, the spikes'
                // crystals) belong to Det's SERVER stream
                Det.clientThread = null;
                Det.serverThread = self;
                Det.inRender = false;
                try
                {
                    result[0] = dump(server, dim, d);
                }
                catch (Exception e)
                {
                    error[0] = e;
                }
                finally
                {
                    Det.clientThread = oldClient;
                    Det.serverThread = oldServer;
                    Det.inRender = oldRender;
                }
            }
        }, "Oracle SpawnDumpDim");
        t.start();
        t.join();
        if (error[0] != null) throw error[0];
        return result[0];
    }

    static JsonObject dump(IntegratedServer server, int dim, File d) throws Exception
    {
        if (dim != -1 && dim != 1)
            throw new IllegalArgumentException("SpawnDumpDim: dim " + dim + " is neither the Nether nor the End");

        if (Probe.rawChunks)
            throw new IllegalStateException("SpawnDumpDim: Probe.rawChunks is set, so the load would not populate");

        d.mkdirs();
        if (!d.isDirectory()) throw new IllegalStateException("SpawnDumpDim: " + d + " is not a directory");

        WorldServer ws = server.worldServers[dim == -1 ? 1 : 2];

        if (ws == null || ws.provider.dimensionId != dim)
            throw new IllegalStateException("SpawnDumpDim: worldServers holds no dimension " + dim + " world");

        if (ws.provider.canRespawnHere())
            throw new IllegalStateException("SpawnDumpDim: dimension " + dim + " can respawn, so its spawn point is searched");

        /* WorldServer.createSpawnPosition's !canRespawnHere branch */
        int sy = ws.provider.getAverageGroundLevel();
        int sx = 0, sz = 0;
        ChunkCoordinates derived = ws.getSpawnPoint();

        /* The block chain the worldstate carries starts at the dimension's own
         * load: Rows hashes every changed write of the run, and the overworld's
         * initial load is already in it, so the chain is restarted and left
         * where the dimension's load put it. The global tick-entry counter is
         * noted instead, for the native side to add to its own zero-based ids. */
        long tickBase = Snapshot.longField(null, "nextTickEntryID", net.minecraft.world.NextTickListEntry.class);
        int idBase = Det.nextId[Det.SERVER];
        Rows.blkHash = 0xcbf29ce484222325L;
        Rows.blkCount = 0;

        /* MinecraftServer.initialWorldChunkLoad, for this dimension's spawn */
        for (int a = -192; a <= 192; a += 16)
            for (int b = -192; b <= 192; b += 16)
                ws.theChunkProviderServer.loadChunk((sx + a) >> 4, (sz + b) >> 4);

        long dimBlkHash = Rows.blkHash;
        int dimBlkCount = Rows.blkCount;

        int nchunks = Snapshot.writeChunks(ws, d);
        int ntiles = Snapshot.tilesWritten;
        int[] counts = new int[4];
        int animals = writeEntities(ws, d, counts);
        int nticks = Snapshot.writeTicks(ws, d, counts);

        JsonObject wi = (JsonObject)StructuresProbe.canon(ws.getWorldInfo().getNBTTagCompound());
        wi.add("LastPlayed", new JsonPrimitive("l:0"));
        Snapshot.writeFile(new File(d, "worldinfo.nbt"), Snapshot.canonText(wi));
        Snapshot.writeFile(new File(d, "worldstate.nbt"), Snapshot.worldState(ws));
        Snapshot.writeFile(new File(d, "worlds.nbt"), Snapshot.worldsState(server));
        Snapshot.writeFile(new File(d, "det.nbt"), Snapshot.detState());
        Snapshot.writeFile(new File(d, "clientworld.nbt"), "{}");
        Snapshot.writeFile(new File(d, "player_client.nbt"), "{}");
        Snapshot.writeFile(new File(d, "player_server.nbt"), "{}");

        JsonObject m = new JsonObject();
        m.addProperty("kind", "netherite-snapshot");
        m.addProperty("v", 1);
        m.addProperty("spawndump", 2);
        m.addProperty("dim", dim);
        m.addProperty("align", "the dimension's spawn area: WorldServer.createSpawnPosition's !canRespawnHere spawn point "
            + "and the 625 chunks MinecraftServer.initialWorldChunkLoad would load around it, at the top of tick 0, "
            + "with no player and no server tick yet; the dimension had no chunks when the probe started");
        m.addProperty("seed", ws.getSeed());
        m.addProperty("tick", 0);
        m.add("world", WorldConf.json());
        m.addProperty("harness", Oracle.harness);
        m.addProperty("spawn", "i:" + sx + ",i:" + sy + ",i:" + sz);
        m.addProperty("tickEntryBase", "l:" + tickBase);
        m.addProperty("nextIdBase", "i:" + idBase);
        m.addProperty("spawnDerived", "i:" + derived.posX + ",i:" + derived.posY + ",i:" + derived.posZ);
        m.addProperty("spawnNote", "spawn is the position the dimension's own createSpawnPosition set (the "
            + "!canRespawnHere branch: (0, getAverageGroundLevel(), 0)); WorldServerMulti replaces the worldInfo with a "
            + "DerivedWorldInfo over the overworld's, so worldinfo.nbt and spawnDerived read the overworld's spawn, not "
            + "this dimension's");
        JsonObject f = new JsonObject();
        f.addProperty("chunks.bin.gz", "gzip of: per chunk, cx int32 LE, cz int32 LE, hash uint64 LE, near uint64 LE, then the Probe chunk bytes");
        f.addProperty("chunkstate.jsonl", "one object per chunk, same order as chunks.bin.gz");
        f.addProperty("entities.jsonl", "one object per entity of this dimension: dim, id, class, player, weather, nbt");
        f.addProperty("animals.jsonl", "one object per world-generation entity of this dimension, in loadedEntityList order: "
            + "class, id, uuidMsb, uuidLsb, x, y, z, yaw, pitch, motionX, motionY, motionZ, rand (the entity's own Det Random state, decimal); "
            + "id and det.nbt's nextId[SERVER] count from the run's start, so the manifest's nextIdBase (the counter when this load began) "
            + "is what a build of the dimension alone adds to its own ids");
        f.addProperty("ticks.jsonl.gz", "gzip of one object per pending block tick: list, x, y, z, id, t, pri, eid; eid is the global NextTickListEntry.nextTickEntryID, whose value when this load started is the manifest's tickEntryBase");
        f.addProperty("worldinfo.nbt", "level.dat NBT; for a dimension this is the overworld's (DerivedWorldInfo), see spawnNote");
        f.addProperty("worldstate.nbt", "this dimension's world Random, updateLCG and the block-write chain of the dimension's own load");
        f.addProperty("worlds.nbt", "every world server's own scalars, one compound per worldServers index");
        f.addProperty("det.nbt", "one entry per role and stream: CLIENT SERVER OTHER RENDER");
        f.addProperty("clientworld.nbt", "empty here: no client world state is read");
        f.addProperty("player_client.nbt", "empty here: no player at this point");
        f.addProperty("player_server.nbt", "empty here: no player at this point");
        m.add("files", f);
        m.addProperty("chunk_bytes", "ids uint16 LE (65536, index x << 12 | z << 8 | y), metas uint8 (65536), sky light uint8 (65536), "
            + "block light uint8 (65536), heightMap 256 int32 LE, precipitationHeightMap 256 int32 LE (both Java index order z << 4 | x), "
            + "heightMapMinimum int32 LE, section mask uint16 LE (bit s set when storageArrays[s] != null); cells in absent sections read as 0");
        m.addProperty("chunk_hash", "FNV-1a 64 (offset 0xcbf29ce484222325, prime 0x100000001b3) over one chunk's bytes as above");
        m.addProperty("chunk_near", "the same FNV-1a over the 3x3 chunks around it, dx outer -1..1, dz inner -1..1; a chunk that is not loaded contributes one zero byte");
        m.addProperty("chunk_order", "ChunkProviderServer.loadedChunks order: the order the chunks were loaded in");
        m.addProperty("tile_order", "chunkTileEntityMap values sorted by x, then y, then z");
        m.addProperty("entity_order", "this dimension's loadedEntityList order, then its weatherEffects order");
        m.addProperty("tick_order", "pendingTickListEntriesTreeSet iteration order (scheduled time, priority, entry id), then pendingTickListEntriesThisTick order");
        m.addProperty("canonical", "compound: object with String.compareTo key order; list: array; scalar strings b: s: i: l: f:<8 hex raw float bits> d:<16 hex raw double bits> str: ba: ia:");
        m.addProperty("roles", "Det.CLIENT SERVER OTHER RENDER, indices 0 1 2 3");
        JsonObject c = new JsonObject();
        c.addProperty("chunks", nchunks);
        c.addProperty("tiles", ntiles);
        c.addProperty("entities", counts[0]);
        c.addProperty("animals", animals);
        c.addProperty("players", counts[1]);
        c.addProperty("weatherEffects", counts[2]);
        c.addProperty("pendingTicks", nticks);
        c.addProperty("thisTick", counts[3]);
        m.add("counts", c);
        m.add("server", Snapshot.serverManifest(server));
        Snapshot.writeFile(new File(d, "manifest.json"), m.toString());

        JsonObject r = new JsonObject();
        r.addProperty("dir", d.getPath());
        r.addProperty("dim", dim);
        r.addProperty("seed", ws.getSeed());
        r.addProperty("spawn", sx + "," + sy + "," + sz);
        r.addProperty("chunks", nchunks);
        r.addProperty("tiles", ntiles);
        r.addProperty("animals", animals);
        r.addProperty("entities", counts[0]);
        r.addProperty("pendingTicks", nticks);
        r.addProperty("blkHash", "l:" + dimBlkHash);
        r.addProperty("blkCount", "i:" + dimBlkCount);
        return r;
    }

    /**
     * This dimension's world-generation entities, in loadedEntityList order:
     * SpawnDump.writeAnimals's record, over the dimension's own list. The End's
     * are the dragon and the ender crystals its spikes spawn; the Nether's are
     * whatever a fortress piece dropped.
     */
    static int writeEntities(WorldServer ws, File d, int[] counts) throws Exception
    {
        PrintWriter w = new PrintWriter(new OutputStreamWriter(new java.io.FileOutputStream(new File(d, "animals.jsonl")), "UTF-8"));
        PrintWriter e = new PrintWriter(new OutputStreamWriter(new java.io.FileOutputStream(new File(d, "entities.jsonl")), "UTF-8"));
        int n = 0;

        for (Object o : ws.loadedEntityList)
        {
            Entity x = (Entity)o;
            Snapshot.writeEntity(e, ws.provider.dimensionId, x, 0, counts);
            w.println(Snapshot.canonText(animal(x)));
            ++n;
        }

        for (Object o : ws.weatherEffects)
        {
            Entity x = (Entity)o;
            Snapshot.writeEntity(e, ws.provider.dimensionId, x, 1, counts);
            w.println(Snapshot.canonText(animal(x)));
            ++n;
            ++counts[2];
        }

        w.close();
        e.close();
        return n;
    }

    /** SpawnDump.writeAnimals's per-entity record. */
    static JsonObject animal(Entity e)
    {
        JsonObject a = new JsonObject();
        a.addProperty("class", e.getClass().getSimpleName());
        a.addProperty("id", e.getEntityId());
        a.addProperty("uuidMsb", "l:" + e.getUniqueID().getMostSignificantBits());
        a.addProperty("uuidLsb", "l:" + e.getUniqueID().getLeastSignificantBits());
        a.addProperty("x", "d:" + Snapshot.hex(Double.doubleToRawLongBits(e.posX), 16));
        a.addProperty("y", "d:" + Snapshot.hex(Double.doubleToRawLongBits(e.posY), 16));
        a.addProperty("z", "d:" + Snapshot.hex(Double.doubleToRawLongBits(e.posZ), 16));
        a.addProperty("yaw", "f:" + Snapshot.hex(Float.floatToRawIntBits(e.rotationYaw) & 4294967295L, 8));
        a.addProperty("pitch", "f:" + Snapshot.hex(Float.floatToRawIntBits(e.rotationPitch) & 4294967295L, 8));
        a.addProperty("motionX", "d:" + Snapshot.hex(Double.doubleToRawLongBits(e.motionX), 16));
        a.addProperty("motionY", "d:" + Snapshot.hex(Double.doubleToRawLongBits(e.motionY), 16));
        a.addProperty("motionZ", "d:" + Snapshot.hex(Double.doubleToRawLongBits(e.motionZ), 16));
        a.addProperty("rand", "l:" + Det.state((java.util.Random)Snapshot.objField(e, "rand")));
        NBTTagCompound tag = new NBTTagCompound();
        e.writeToNBT(tag);
        a.add("nbt", StructuresProbe.canon(tag));
        return a;
    }
}