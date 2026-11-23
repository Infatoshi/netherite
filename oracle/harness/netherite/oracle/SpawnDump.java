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
import net.minecraft.world.biome.BiomeGenBase;

/**
 * The world the integrated server holds right after MinecraftServer.
 * initialWorldChunkLoad, before its first tick: the reference for the native
 * seed-world build (csrc/engine/seedworld.c, checked by csrc/tests/
 * test_seedworld.c).
 *
 * Run with --spawndump DIR: the dump happens on the client thread at the top of
 * tick 0, when MinecraftServer.run has published Lockstep.server (so
 * loadAllWorlds and initialWorldChunkLoad are done) and the server thread is
 * parked awaiting its first tick permit, so no server state moves under it and
 * the player has not joined yet.
 *
 * DIR is a snapshot (Snapshot.java's format, so csrc/engine/snapshot.c loads it):
 * manifest.json, chunks.bin.gz, chunkstate.jsonl, entities.jsonl,
 * ticks.jsonl.gz, worldinfo.nbt (the spawn point), worldstate.nbt (World.rand
 * and the block-write chain), worlds.nbt, det.nbt, clientworld.nbt, and the two
 * player files, empty here (there is no player yet).
 *
 * animals.jsonl is this dump's own file: the entities world generation spawned,
 * one reduced record each (class, entity id, uuid, position, rotation, motion,
 * the entity's own Det Random state), the part of an entity a native build with
 * no entity framework can produce.
 */
public final class SpawnDump
{
    private SpawnDump() {}

    /** --spawndump DIR; null when the run does not dump. */
    public static String dir;
    public static boolean done;

    /** Called from Oracle.preTick on the client thread, before tick 0's client tick. */
    static void early(Minecraft mc)
    {
        done = true;
        try
        {
            IntegratedServer server = mc.getIntegratedServer();
            if (server == null) throw new IllegalStateException("SpawnDump: no integrated server at tick 0");
            JsonObject r = dump(server, new File(dir));
            // the state is captured: later draws (the join, the server tick)
            // are not part of the reference
            System.out.println("ORACLE SPAWNDUMP " + r);
        }
        catch (Exception e)
        {
            throw new RuntimeException("SpawnDump: " + e, e);
        }
    }

    /** The generic `run` command's entry (a dump at whatever tick it is issued). */
    static JsonObject run(IntegratedServer server, JsonObject cmd) throws Exception
    {
        File d = new File(cmd.has("out") ? cmd.get("out").getAsString() : dir);
        return dump(server, d);
    }

    static JsonObject dump(IntegratedServer server, File d) throws Exception
    {
        d.mkdirs();
        if (!d.isDirectory()) throw new IllegalStateException("SpawnDump: " + d + " is not a directory");

        WorldServer ws = server.worldServers[0];
        int nchunks = Snapshot.writeChunks(ws, d);
        int ntiles = Snapshot.tilesWritten;
        int[] counts = new int[4];
        Snapshot.writeEntities(server, d, counts);
        int animals = writeAnimals(ws, d);
        writeCreatures(d);
        int nticks = Snapshot.writeTicks(ws, d, counts);

        JsonObject wi = (JsonObject)StructuresProbe.canon(ws.getWorldInfo().getNBTTagCompound());
        wi.add("LastPlayed", new JsonPrimitive("l:0"));
        Snapshot.writeFile(new File(d, "worldinfo.nbt"), Snapshot.canonText(wi));
        Snapshot.writeFile(new File(d, "worldstate.nbt"), Snapshot.worldState(ws));
        Snapshot.writeFile(new File(d, "worlds.nbt"), Snapshot.worldsState(server));
        Snapshot.writeFile(new File(d, "det.nbt"), Snapshot.detState());
        Snapshot.writeFile(new File(d, "clientworld.nbt"), empty());
        Snapshot.writeFile(new File(d, "player_client.nbt"), empty());
        Snapshot.writeFile(new File(d, "player_server.nbt"), empty());

        ChunkCoordinates sp = ws.getSpawnPoint();
        JsonObject m = new JsonObject();
        m.addProperty("kind", "netherite-snapshot");
        m.addProperty("v", 1);
        m.addProperty("spawndump", 1);
        m.addProperty("align", "the overworld state right after MinecraftServer.initialWorldChunkLoad, at the top of tick 0, "
            + "with no player and no server tick yet");
        m.addProperty("seed", ws.getSeed());
        m.addProperty("tick", 0);
        m.add("world", WorldConf.json());
        m.addProperty("harness", Oracle.harness);
        m.addProperty("spawn", "i:" + sp.posX + ",i:" + sp.posY + ",i:" + sp.posZ);
        JsonObject f = new JsonObject();
        f.addProperty("chunks.bin.gz", "gzip of: per chunk, cx int32 LE, cz int32 LE, hash uint64 LE, near uint64 LE, then the Probe chunk bytes");
        f.addProperty("chunkstate.jsonl", "one object per chunk, same order as chunks.bin.gz");
        f.addProperty("entities.jsonl", "one object per entity: dim, id, class, player, weather, nbt");
        f.addProperty("animals.jsonl", "one object per world-generation creature: class, id, uuidMsb, uuidLsb, x, y, z, yaw, pitch, "
            + "motionX, motionY, motionZ, rand (the entity's own Det Random state, decimal)");
        f.addProperty("creatures.jsonl", "every registered biome's creature spawn list after pruneSpawns: id, name, chance, "
            + "and the entries (class, weight, min, max) in list order");
        f.addProperty("ticks.jsonl.gz", "gzip of one object per pending block tick: list, x, y, z, id, t, pri, eid");
        f.addProperty("worldinfo.nbt", "level.dat NBT, the spawn point in SpawnX/SpawnY/SpawnZ");
        f.addProperty("worldstate.nbt", "the world Random, updateLCG, the block-write chain and the other non-level.dat scalars");
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
        m.addProperty("entity_order", "for dim in worldServers order: loadedEntityList order, then weatherEffects order");
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
        r.addProperty("seed", ws.getSeed());
        r.addProperty("spawn", sp.posX + "," + sp.posY + "," + sp.posZ);
        r.addProperty("chunks", nchunks);
        r.addProperty("tiles", ntiles);
        r.addProperty("animals", animals);
        r.addProperty("entities", counts[0]);
        r.addProperty("pendingTicks", nticks);
        return r;
    }

    static String empty()
    {
        return "{}";
    }

    /**
     * Every registered biome's creature spawn list as it stands after
     * WorldConf.pruneSpawns, the table csrc/engine/seedworld_creatures.h was
     * written from. One plain JSON line per biome id (not canonical NBT: it is
     * read by hand, not by the native reader).
     */
    static void writeCreatures(File d) throws Exception
    {
        PrintWriter w = new PrintWriter(new OutputStreamWriter(new java.io.FileOutputStream(new File(d, "creatures.jsonl")), "UTF-8"));
        BiomeGenBase[] all = BiomeGenBase.getBiomeGenArray();

        for (int id = 0; id < all.length; ++id)
        {
            if (all[id] == null) continue;
            StringBuilder b = new StringBuilder();
            b.append("{\"id\":").append(id);
            b.append(",\"name\":\"").append(all[id].biomeName).append('"');
            b.append(",\"chance\":").append(Float.toString(all[id].getSpawningChance()));
            b.append(",\"creatures\":[");
            List l = all[id].getSpawnableList(net.minecraft.entity.EnumCreatureType.creature);

            for (int i = 0; i < l.size(); ++i)
            {
                net.minecraft.world.biome.BiomeGenBase.SpawnListEntry e =
                    (net.minecraft.world.biome.BiomeGenBase.SpawnListEntry)l.get(i);
                if (i > 0) b.append(',');
                b.append("{\"class\":\"").append(e.entityClass.getSimpleName()).append("\",\"weight\":").append(Snapshot.intField(e, "itemWeight"))
                 .append(",\"min\":").append(Snapshot.intField(e, "minGroupCount")).append(",\"max\":").append(Snapshot.intField(e, "maxGroupCount")).append('}');
            }

            b.append("]}");
            w.println(b);
        }

        w.close();
    }

    /** Every world-generation creature, in loadedEntityList order per dimension. */
    static int writeAnimals(WorldServer ws, File d) throws Exception
    {
        PrintWriter w = new PrintWriter(new OutputStreamWriter(new java.io.FileOutputStream(new File(d, "animals.jsonl")), "UTF-8"));
        int n = 0;

        for (Object o : ws.loadedEntityList)
        {
            Entity e = (Entity)o;
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
            a.add("nbt", StructuresProbe.canon(nbt(e)));
            w.println(Snapshot.canonText(a));
            ++n;
        }

        w.close();
        return n;
    }

    static NBTTagCompound nbt(Entity e)
    {
        NBTTagCompound tag = new NBTTagCompound();
        e.writeToNBT(tag);
        return tag;
    }
}