package netherite.oracle;

import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonPrimitive;
import java.io.BufferedOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Calendar;
import java.util.Collections;
import java.util.Comparator;
import java.util.IdentityHashMap;
import java.util.List;
import java.util.Set;
import java.util.zip.GZIPOutputStream;
import net.minecraft.client.Minecraft;
import net.minecraft.client.entity.EntityClientPlayerMP;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityLiving;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.entity.player.EntityPlayerMP;
import net.minecraft.item.ItemStack;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.nbt.NBTTagList;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.tileentity.TileEntity;
import net.minecraft.util.MovementInput;
import net.minecraft.world.NextTickListEntry;
import net.minecraft.world.World;
import net.minecraft.world.WorldServer;
import net.minecraft.world.chunk.Chunk;
import net.minecraft.world.chunk.storage.ExtendedBlockStorage;
/**
 * The full game state at the start of a tape, the frame the native tape replay
 * loads before its first tick. Run it as the first command of a script:
 *
 *   {"cmd":"run","class":"Snapshot"}
 *   {"n":20,"act":{"hold":["forward"]}}
 *   ...
 *
 * and the state written is the one right before the tick that writes the tape's
 * first row: row t is captured after tick pair t, so a snapshot taken when
 * Oracle.tick is T is the pre-state of row T. The manifest records that
 * alignment as "tick". The directory defaults to the tape's own directory (run
 * with --tape), or comes from the command's "out".
 *
 * Output DIR:
 *   manifest.json     seed, tick, the resolved world, the layouts, the counts
 *   chunks.bin.gz     every loaded overworld chunk, in load order (gzipped:
 *                     the chunks are mostly air, 173 MB in, 2 MB out)
 *   chunkstate.jsonl  the same chunks' biome arrays and non-block state, in the
 *                     same order, with each chunk's tile entities
 *   entities.jsonl    every server entity, loadedEntityList order per dimension
 *   ticks.jsonl.gz    the pending block ticks, in the order the world holds them
 *   worldinfo.nbt     level.dat NBT
 *   worldstate.nbt    the world Random, updateLCG, weather, the block-tick
 *                     counter and the other scalars that are not in level.dat
 *   det.nbt           every Det per-role stream, counter and split Random
 *   player_client.nbt the client player: NBT plus the fields that are not in
 *                     NBT but drive the next tick's movement
 *   player_server.nbt the server player: NBT plus its runtime fields
 *
 * chunks.bin.gz, per chunk, in chunkstate.jsonl order: cx int32 LE, cz int32 LE,
 * hash uint64 LE, near uint64 LE, then the Probe.CHUNK_BYTES chunk bytes. hash
 * is FNV-1a 64 over that one chunk's bytes; near is the probe's 3x3 hash around
 * it (a chunk that is not loaded contributes one zero byte).
 *
 * Everything else is canonical NBT as JSON text: no whitespace, keys sorted by
 * String.compareTo, scalars as "b:1" "s:1" "i:-3" "l:5" "f:"+8 hex digits of
 * the raw float bits "d:"+16 "str:text" "ba:1,2" "ia:1,2". A field the reader
 * does not know is still typed, so nothing is lost.
 *
 * Runs on its own thread (the OTHER role, like ChunkDump) while the server is
 * parked, so no CLIENT or SERVER RNG stream moves and no state can change under
 * it.
 */
final class Snapshot
{
    private Snapshot() {}

    /* the any-tick sweep's throwaway snapshots ("overlay") deflate at
     * BEST_SPEED: the default level is most of a snapshot's time, and the
     * native reader takes any level; a recorded snapshot keeps the default */
    static boolean fastGzip;

    static GZIPOutputStream gzip(File f) throws IOException
    {
        GZIPOutputStream g = new GZIPOutputStream(new FileOutputStream(f), 1 << 16);
        if (fastGzip)
        {
            java.util.zip.Deflater d = (java.util.zip.Deflater)objField(g, "def");
            d.setLevel(java.util.zip.Deflater.BEST_SPEED);
        }
        return g;
    }

    /* A run that takes several snapshots ("blobs": the any-tick sweep and the
     * keyframes) writes each chunk's bytes once, as a gzip file named by its
     * own hash under this directory, and per snapshot only chunks.idx and
     * saved-chunks.idx (one "cx cz hash near blob" line per chunk, the blob
     * path relative to the snapshot's directory) and its big text files
     * gzipped; the native loader reads both forms. null otherwise. */
    static File blobDir;
    static final Set<String> knownBlobs = new java.util.HashSet<String>();

    /** A text file of the snapshot: name.gz in blob mode. */
    static PrintWriter textOut(File dir, String name) throws IOException
    {
        java.io.OutputStream o = blobDir != null ? gzip(new File(dir, name + ".gz")) : new FileOutputStream(new File(dir, name));
        return new PrintWriter(new OutputStreamWriter(o, "UTF-8"));
    }

    static JsonObject run(final IntegratedServer server, final JsonObject cmd) throws Exception
    {
        fastGzip = cmd.has("overlay");
        // a trace file that starts at the snapshot: the native replay opens its
        // own trace at the join tick, so the two diff from line 1
        netherite.oracle.Trace.restart();

        final JsonObject[] result = new JsonObject[1];
        final Throwable[] error = new Throwable[1];
        Thread t = new Thread(new Runnable()
        {
            public void run()
            {
                try
                {
                    result[0] = dump(server, cmd);
                }
                catch (Throwable e)
                {
                    // an Error too (an OutOfMemoryError, a worker thread that could not start): a
                    // snapshot cut short is never answered as written (Oracle.threw)
                    error[0] = e;
                }
            }
        }, "Oracle Snapshot");
        t.start();
        t.join();
        if (error[0] instanceof Exception) throw (Exception)error[0];
        if (error[0] != null) throw new IllegalStateException("Snapshot: " + error[0], error[0]);
        return result[0];
    }

    /** The tape's own directory: a tape at DIR/tape.jsonl keeps its snapshot in DIR. */
    static String defaultOut()
    {
        if (Oracle.tapePath == null)
            throw new IllegalArgumentException("Snapshot: pass \"out\", or run with a tape so the snapshot lands beside it");
        File t = new File(Oracle.tapePath).getAbsoluteFile();
        File p = t.getParentFile();
        return (p == null ? new File(".") : p).getPath();
    }

    static JsonObject dump(IntegratedServer server, JsonObject cmd) throws Exception
    {
        // "sub": a directory under the tape's own (an end-state snapshot beside
        // the start one); it never takes the checkpoint overlay
        File dir = new File(cmd.has("out") ? cmd.get("out").getAsString()
            : cmd.has("sub") ? new File(defaultOut(), cmd.get("sub").getAsString()).getPath() : defaultOut());
        dir.mkdirs();
        if (!dir.isDirectory()) throw new IllegalStateException("Snapshot: " + dir + " is not a directory");

        Minecraft mc = Oracle.mc;
        if (mc == null || mc.thePlayer == null) throw new IllegalStateException("Snapshot: the join has not happened yet");
        WorldServer ws = server.worldServers[0];
        long tick = Oracle.tick;

        long t0 = System.nanoTime();
        fillCache = new java.util.HashMap<String, Filled>();
        blobDir = cmd.has("blobs") ? new File(cmd.get("blobs").getAsString()) : null;
        for (String old : new String[] {"chunks.bin.gz", "chunks.idx", "saved-chunks.bin.gz", "saved-chunks.idx",
            "chunkstate.jsonl", "chunkstate.jsonl.gz", "saved-chunkstate.jsonl", "saved-chunkstate.jsonl.gz",
            "entities.jsonl", "entities.jsonl.gz", "clientents.jsonl", "clientents.jsonl.gz"})
            new File(dir, old).delete();
        int nchunks, nsaved;
        long tChunks, tSaved, tEnts, tTicks, tSeed;
        int ntiles;
        int[] counts = new int[4];
        int nticks, nseedticks;
        ChunkOut.deferred = new ArrayList<ChunkOut>();
        try
        {
            nchunks = writeChunks(server, dir);
            tChunks = System.nanoTime();
            nsaved = writeSavedChunks(server, dir);
            tSaved = System.nanoTime();
            ntiles = tilesWritten;
            writeEntities(server, dir, counts);
            tEnts = System.nanoTime();
            nticks = writeTicks(server, dir, counts);
            tTicks = System.nanoTime();
            nseedticks = writeSeedTicks(server, dir);
            tSeed = System.nanoTime();
            writeClientEntities(mc, dir);
            ChunkOut.finishDeferred();
        }
        finally
        {
            try { ChunkOut.finishDeferred(); } // a phase failed: its streams end too
            catch (IOException e) {}
            fillCache = null;
            blobDir = null;
        }

        // LastPlayed is the wall clock, not tick state: pinned so two runs of the
        // same script are byte-identical
        JsonObject wi = (JsonObject)StructuresProbe.canon(ws.getWorldInfo().getNBTTagCompound());
        wi.add("LastPlayed", new JsonPrimitive("l:0"));
        writeFile(new File(dir, "worldinfo.nbt"), canonText(wi));
        writeFile(new File(dir, "worldstate.nbt"), worldState(ws));
        writeFile(new File(dir, "worlds.nbt"), worldsState(server));
        writeFile(new File(dir, "structures.json"), structures(server));
        writeFile(new File(dir, "villages.nbt"), villages(ws));
        writeFile(new File(dir, "clientworld.nbt"), clientWorld(mc, ws));
        writeFile(new File(dir, "det.nbt"), detState());
        writeFile(new File(dir, "player_client.nbt"), clientPlayer(mc));
        writeFile(new File(dir, "player_server.nbt"), serverPlayer(server));
        writeFile(new File(dir, "stats.json"), Stats.state(server).toString());
        // a checkpoint start's join ticks save chunks the replay may load later
        // and a mid-run snapshot ("overlay", the any-tick sweep) carries every
        // chunk the run saved since its start, from a seed start all of them
        long tFiles = System.nanoTime();
        int saved = (Oracle.fromDir != null && !cmd.has("out") && !cmd.has("sub")) || cmd.has("overlay") ? SaveOverlay.write(dir) : 0;
        long tOverlay = System.nanoTime();

        JsonObject m = new JsonObject();
        m.addProperty("kind", "netherite-snapshot");
        m.addProperty("dev", Oracle.dev);
        m.addProperty("v", 2);
        m.addProperty("align", "the state at the start of tick " + tick + ", before preTick. Rows 0.." + (tick - 1)
            + " of the tape are the join ticks (no input row: the command loop only runs once the client and server are ready); row " + tick
            + " is the first row written after this snapshot, and what this state plus that row's act produce");
        m.addProperty("seed", ws.getSeed());
        m.addProperty("tick", tick);
        m.add("world", WorldConf.json());
        m.addProperty("harness", Oracle.harness);
        JsonObject f = new JsonObject();
        f.addProperty("chunks.bin.gz", "gzip of: per chunk, cx int32 LE, cz int32 LE, hash uint64 LE, near uint64 LE, then the Probe chunk bytes");
        f.addProperty("chunkstate.jsonl", "one object per chunk, same order as chunks.bin.gz; tilesrt beside tiles: each tile entity's fields (EntityState), same order; chests (when the chunk has one) is Snapshot.chestState, the chests' state their NBT does not carry");
        f.addProperty("saved-chunks.bin.gz", "the same format as chunks.bin.gz, for every chunk the region files of the Nether and the End hold but the world does not have loaded, in region-file order (r.X.Z.mca files sorted, each header slot 0..1023 in order), read at snapshot time through AnvilChunkLoader.checkedReadChunkFromNBT");
        f.addProperty("saved-chunkstate.jsonl", "one object per saved-chunks.bin.gz chunk, same order, with saved:1, the region NBT's Entities (ents, canonical NBT) and TileTicks (ticks: x, y, z, id, t the saved delay, pri, eid)");
        f.addProperty("entities.jsonl", "one object per entity: dim, id, class, player, weather, rand (its own Random now), rand0 (the state it was born with), gauss (the cached nextGaussian, when there is one), age (EntityLivingBase.entityAge), lsf (EntityLiving.livingSoundTime), vil (a villager's villageObj as its villages.nbt index or -1, randomTickDivider, isLookingForHome; an iron golem's villageObj index and homeCheckTimer), size (an XP orb's width), dragon (EntityDragon's unsaved flight state: target, ring, anim, the healing crystal's id, counters, box, parts), crystal (innerRotation,health), throwable (ticksInAir,ticksInGround,thrower id or -1,ticksExisted), trk and trkd (the EntityTrackerEntry: ticks, ticksSinceLastForcedTeleport, lastScaledX/Y/ZPosition, lastYaw, lastPitch, lastHeadMotion, isDataInitialized; then motionX/Y/Z and posX/Y/Z as double bits), rt (EntityState: every field of the class chain, the data watcher, the AI task lists and helpers), trkrt (the EntityTrackerEntry's fields, watchedBy: the tracking player count), cidx (its index in its chunk slice's entity list), nbt");
        if (new File(dir, "ghosts.jsonl").exists() || new File(dir, "ghosts.jsonl.gz").exists())
            f.addProperty("ghosts.jsonl", "entities.jsonl's form for each living entity no world lists that a listed entity's rt still references (its chunk unloaded, or it died), in the order first referenced");
        f.addProperty("ticks.jsonl.gz", "gzip of one object per pending block tick: dim, list, x, y, z, id, t, pri, eid");
        f.addProperty("seedents.jsonl.gz", "gzip of one object per overworld chunk the region files hold that is not loaded, in seedticks.jsonl.gz's chunk order: cx, cz and ents, the region NBT's Entities (canonical NBT, in list order); a chunk that is not loaded re-adds exactly these at its next load");
        f.addProperty("seedticks.jsonl.gz", "gzip of one object per saved pending block tick of every chunk that is not loaded, read from the region files: dim, cx, cz, x, y, z, id, t (the saved delay, relative to the chunk's last save), pri; a chunk that is not loaded re-adds exactly these at its next load");
        f.addProperty("worldinfo.nbt", "level.dat NBT");
        f.addProperty("worldstate.nbt", "the world Random, updateLCG and the other non-level.dat scalars; bigTree: each biome id's BiomeGenBase.worldGeneratorBigTree.heightLimit (0 none); blockBounds: for the fences, the stairs, the brewing stand, the ladder, the end portal frame and the end portal, whose one object's bounds getCollisionBoundingBoxFromPool reports, id then minX minY minZ maxX maxY maxZ as float bits; leafDecay: BlockLeaves.field_150128_a's centre cell for Blocks.leaves then Blocks.leaves2 (0 before the first search)");
        f.addProperty("worlds.nbt", "every world server's own scalars, one compound per worldServers index (order, w0, w1, ...): the same keys worldstate.nbt carries plus ambientTickCountdown, difficulty, the view distance, the loaded chunk and pending tick counts, the active chunk set in its HashSet iteration order, the loaded tile entities in loadedTileEntityList order (teOrder, x,y,z each) and the pending ticks");
        f.addProperty("structures.json", "per world server with a ChunkProviderGenerate (w0, ...): each MapGenStructure's structureMap keys (Mineshaft, Village, Stronghold, Temple) as ia:cx,cz,... in the HashMap's iteration order; a start there is never offered again, so these decide which spawn tests reseed World.rand; <Type>.pieces lists, per start in the same order, its components now (minX minY minZ maxX maxY maxZ HPos VCount each), what data/<Type>.dat would give a reloaded start; <Type>.flags the same components' once-set generation flags (see Snapshot.pieceFlags); the Nether's (ChunkProviderHell) w entry holds its Fortress map's keys the same way when the map is not empty");
        f.addProperty("villages.nbt", "the overworld's live VillageCollection: Tick, the villager position queue (pos, x y z triples), and per village (list order) its writeVillageDataToNBT keys plus what that does not save: each door's opening restriction counter (DoorsRC, door order) and the aggressors as entity id and time pairs (Agg); the siege's village as its list index (siegeV, -1 none) and its three muster coordinates (siegeG, siegeH, siegeI)");
        f.addProperty("clientworld.nbt", "the client world's own chunk set and how it compares with the server's; prevActive: WorldClient.previousActiveChunkSet (cx, cz pairs), activeCap: its activeChunkSet's table length");
        f.addProperty("clientents.jsonl", "the client world's entities but the player, loadedEntityList order: id, class, f (EntityState fields), dw, body (an EntityLiving's EntityBodyHelper)");
        f.addProperty("det.nbt", "one entry per role and stream: CLIENT SERVER OTHER RENDER; worldSeed is Det.reset's argument, which seeds a split Random made later; furnace, chest, dispenser, hopper and brewing are the block Randoms (BlockRands) of furnace/lit_furnace, chest/trapped_chest, dispenser/dropper, hopper, brewing_stand (state, gauss when cached)");
        f.addProperty("player_client.nbt", "nbt plus the fields that are not in NBT, rt (EntityState), ctl (PlayerControllerMP's fields) and pending (the received packets not yet processed)");
        f.addProperty("player_server.nbt", "nbt plus the runtime fields, rt (EntityState), netrt (the NetHandlerPlayServer's fields), iiw (ItemInWorldManager's fields), mirror and openMirror (Container.inventoryItemStacks of the player's own container and of an open window, a {} per empty slot)");
        f.addProperty("stats.json", "the server player's StatisticsFile (file: the stats/<uuid>.json form with sorted keys and progress; dirty: field_150888_e; lastSend: field_150885_f; achDirty: field_150886_g; tickCounter: MinecraftServer.getTickCounter) and the client's StatFileWriter mirror (client.file, client.has: field_147308_k, client.hint)");
        if (saved > 0) f.addProperty("save", "the chunks the join ticks saved (" + saved + "), each whose stored NBT differs from the checkpoint's save, as Anvil region files (region, DIM-1/region, DIM1/region) with zero timestamps; a chunk found here is loaded from here, any other from the checkpoint");
        if (cmd.has("blobs"))
        {
            // blob mode: the chunk bytes are blobs under the run's blob
            // directory, listed by the .idx files; the big text files are gzipped
            JsonObject g = new JsonObject();
            for (java.util.Map.Entry<String, JsonElement> e : f.entrySet())
            {
                String k = e.getKey();
                if (k.equals("chunks.bin.gz") || k.equals("saved-chunks.bin.gz"))
                    g.addProperty(k.replace(".bin.gz", ".idx"), "one line per chunk, " + k + "'s order: cx cz hash near (hex) and the path of the gzip of its bytes (" + k + "'s chunk bytes), relative to this directory");
                else if (k.equals("chunkstate.jsonl") || k.equals("saved-chunkstate.jsonl") || k.equals("entities.jsonl") || k.equals("clientents.jsonl") || k.equals("ghosts.jsonl"))
                    g.add(k + ".gz", e.getValue());
                else g.add(k, e.getValue());
            }
            f = g;
        }
        m.add("files", f);
        m.addProperty("chunk_bytes", "ids uint16 LE (65536, index x << 12 | z << 8 | y), metas uint8 (65536), sky light uint8 (65536), block light uint8 (65536), heightMap 256 int32 LE, precipitationHeightMap 256 int32 LE (both Java index order z << 4 | x), heightMapMinimum int32 LE, section mask uint16 LE (bit s set when storageArrays[s] != null); cells in absent sections read as 0");
        m.addProperty("chunk_hash", "FNV-1a 64 (offset 0xcbf29ce484222325, prime 0x100000001b3) over one chunk's bytes as above");
        m.addProperty("chunk_near", "the same FNV-1a over the 3x3 chunks around it, dx outer -1..1, dz inner -1..1; a chunk that is not loaded contributes one zero byte");
        m.addProperty("chunk_order", "ChunkProviderServer.loadedChunks order: the order the chunks were loaded in, which is also the order they are saved in");
        m.addProperty("tile_order", "chunkTileEntityMap values sorted by x, then y, then z");
        m.addProperty("entity_order", "for dim in worldServers order: loadedEntityList order, then weatherEffects order, weatherEffects already in loadedEntityList skipped");
        m.addProperty("tick_order", "pendingTickListEntriesTreeSet iteration order (scheduled time, priority, entry id), then pendingTickListEntriesThisTick order");
        m.addProperty("wallclock", "worldinfo.nbt's LastPlayed is the wall clock, not tick state: it is written as l:0 so two runs of the same script are byte-identical; so are the rt and netrt fields EntityState.WALLCLOCK names (EntityPlayerMP.field_143005_bX and ping, NetHandlerPlayServer.field_147378_h and field_147379_i), written as 0; server.calMonth and calDay are the oracle's pinned calendar (Oracle.CALENDAR_MILLIS), not today");
        m.addProperty("canonical", "compound: object with String.compareTo key order; list: array; scalar strings b: s: i: l: f:<8 hex raw float bits> d:<16 hex raw double bits> str: ba: ia:");
        m.addProperty("roles", "Det.CLIENT SERVER OTHER RENDER, indices 0 1 2 3");
        m.addProperty("client_chunk_order", "clientworld.nbt: chunks is the size of ChunkProviderClient.chunkListing and hashElements is chunkMapping.getNumHashElements(); loaded is [[cx,cz], ...] in chunkListing order; sameAsServer and differ<Region> compare the chunk bytes of the chunks both worlds have, region by region in the chunk_bytes order");
        JsonObject c = new JsonObject();
        c.addProperty("chunks", nchunks);
        c.addProperty("savedChunks", nsaved);
        c.addProperty("tiles", ntiles);
        c.addProperty("entities", counts[0]);
        c.addProperty("players", counts[1]);
        c.addProperty("weatherEffects", counts[2]);
        c.addProperty("pendingTicks", nticks);
        c.addProperty("thisTick", counts[3]);
        c.addProperty("seedTicks", nseedticks);
        m.add("counts", c);
        m.add("server", serverManifest(server));
        writeFile(new File(dir, "manifest.json"), m.toString());
        System.out.println("ORACLE SNAPSHOT " + dir + " chunks=" + nchunks + " tiles=" + ntiles + " ents=" + counts[0]
            + " ticks=" + nticks + " seedticks=" + nseedticks + " t=" + tick);
        long tEnd = System.nanoTime();
        System.out.println("ORACLE SNAPSHOT TIME ms chunks=" + ms(tChunks - t0) + " saved=" + ms(tSaved - tChunks)
            + " ents=" + ms(tEnts - tSaved) + " ticks=" + ms(tTicks - tEnts) + " seedticks=" + ms(tSeed - tTicks)
            + " files=" + ms(tFiles - tSeed) + " overlay=" + ms(tOverlay - tFiles) + " total=" + ms(tEnd - t0));

        JsonObject r = new JsonObject();
        r.addProperty("dir", dir.getPath());
        r.addProperty("tick", tick);
        r.addProperty("chunks", nchunks);
        r.addProperty("tiles", ntiles);
        r.addProperty("entities", counts[0]);
        r.addProperty("pendingTicks", nticks);
        r.addProperty("bytes", bytesWritten);
        return r;
    }

    static long ms(long ns) { return (ns + 500000) / 1000000; }

    // ------------------------------------------------------------------ chunks

    static int tilesWritten;
    static long bytesWritten;

    static int writeChunks(IntegratedServer server, File dir) throws Exception
    {
        return writeChunks(server.worldServers, dir);
    }

    static int writeChunks(WorldServer ws, File dir) throws Exception
    {
        return writeChunks(new WorldServer[] {ws}, dir);
    }

    static int writeChunks(WorldServer[] worlds, File dir) throws Exception
    {
        byte[] nbuf = new byte[Probe.CHUNK_BYTES]; // hashAround's scratch: it overwrites the bytes it is handed
        ChunkOut out = new ChunkOut(dir, "chunks");
        PrintWriter state = textOut(dir, "chunkstate.jsonl");
        tilesWritten = 0;
        int n = 0;

        for (WorldServer ws : worlds)
        {
            if (ws == null) continue;
            List chunks = ws.theChunkProviderServer.func_152380_a();
            prehash(ws, chunks);
            for (Object o : chunks)
            {
            Chunk c = (Chunk)o;
            Filled fc = cached(ws, c.xPosition, c.zPosition);
            long hash = fc.own();
            long near = hashAround(ws, c.xPosition, c.zPosition, nbuf);
            out.write(c.xPosition, c.zPosition, hash, near, fc.bytes);

            JsonObject s = new JsonObject();
            s.addProperty("dim", ws.provider.dimensionId);
            s.addProperty("cx", c.xPosition);
            s.addProperty("cz", c.zPosition);
            s.addProperty("biome", hexBytes(c.getBiomeArray()));
            s.addProperty("terrainPopulated", c.isTerrainPopulated ? 1 : 0);
            s.addProperty("lightPopulated", c.isLightPopulated ? 1 : 0);
            s.addProperty("populated", c.field_150815_m ? 1 : 0);
            s.addProperty("modified", c.isModified ? 1 : 0);
            s.addProperty("hasEntities", c.hasEntities ? 1 : 0);
            s.addProperty("sendUpdates", c.sendUpdates ? 1 : 0);
            s.addProperty("lastSaveTime", c.lastSaveTime);
            s.addProperty("inhabitedTime", c.inhabitedTime);
            s.addProperty("queuedLightChecks", intField(c, "queuedLightChecks"));
            s.addProperty("skylightColumns", hexBools(c.updateSkylightColumns));
            s.addProperty("gap", boolField(c, "isGapLightingUpdated") ? 1 : 0);

            List tes = new ArrayList(c.chunkTileEntityMap.values());
            // the chunk's map has no order of its own; the position is the key
            Collections.sort(tes, new Comparator()
            {
                public int compare(Object a, Object b)
                {
                    TileEntity x = (TileEntity)a, y = (TileEntity)b;
                    int c = cmp(x.field_145851_c, y.field_145851_c);
                    if (c != 0) return c;
                    c = cmp(x.field_145848_d, y.field_145848_d);
                    return c != 0 ? c : cmp(x.field_145849_e, y.field_145849_e);
                }

                int cmp(int a, int b) { return a < b ? -1 : (a > b ? 1 : 0); }
            });
            JsonArray ta = new JsonArray();
            JsonArray tr = new JsonArray();
            for (Object to : tes)
            {
                NBTTagCompound tag = new NBTTagCompound();
                ((TileEntity)to).writeToNBT(tag);
                ta.add(StructuresProbe.canon(tag));
                // the fields NBT does not carry (the furnace's
                // currentItemBurnTime, a chest's viewers and lid)
                JsonObject rt = EntityState.fields(to, Object.class);
                rt.addProperty("cls", "str:" + to.getClass().getSimpleName());
                tr.add(rt);
            }
            tilesWritten += tes.size();
            s.add("tiles", ta);
            s.add("tilesrt", tr);
            String chests = chestState(c);
            if (chests != null) s.addProperty("chests", chests);
            state.println(canonText(s));
            ++n;
            }
        }

        state.close();
        out.close();
        return n;
    }

    /**
     * The chunks the Nether's and the End's region files hold but the world
     * does not have loaded. A restore from a save rebuilds every provider with
     * loadChunkOnProvideRequest at its default (true), so the replay's first
     * walk over an absent chunk loads it from disk: populated bytes, no
     * populateChunk, no block writes, its Entities and TileTicks re-added
     * (AnvilChunkLoader.readChunkFromNBT). Read now, not from the checkpoint:
     * the join's unload drain saves chunks after they changed (fire ages).
     * The overworld's are left out: the native reads them from the
     * checkpoint save itself, with seedticks.jsonl.gz on top. Enumerated from
     * each region file's own header (slot order), loaded through the
     * loader's checkedReadChunkFromNBT (reflection: it is protected) so the
     * Chunk carries the saved light arrays, written with "saved":1.
     */
    static int writeSavedChunks(IntegratedServer server, File dir) throws Exception
    {
        byte[] nbuf = new byte[Probe.CHUNK_BYTES];
        ChunkOut out = new ChunkOut(dir, "saved-chunks");
        PrintWriter state = textOut(dir, "saved-chunkstate.jsonl");
        int n = 0;
        long eid = longField(null, "nextTickEntryID", NextTickListEntry.class);
        final java.lang.reflect.Method read = net.minecraft.world.chunk.storage.AnvilChunkLoader.class
            .getDeclaredMethod("checkedReadChunkFromNBT", World.class, Integer.TYPE, Integer.TYPE, NBTTagCompound.class);
        read.setAccessible(true);

        // the region chunks in the order they are written, each with the
        // payload stored for it now
        final List<Object[]> order = new ArrayList<Object[]>();
        for (WorldServer ws : server.worldServers)
        {
            if (ws == null || ws.provider.dimensionId == 0) continue;
            Object cps = objField(ws, "theChunkProviderServer");
            if (!boolField(cps, "loadChunkOnProvideRequest")) continue;
            Object loader = objField(cps, "currentChunkLoader");
            if (!(loader instanceof net.minecraft.world.chunk.storage.AnvilChunkLoader)) continue;
            File saveDir = (File)objField(loader, "chunkSaveLocation");
            File regionDir = new File(saveDir, "region");
            File[] files = regionDir.listFiles();
            if (files == null) continue;
            Arrays.sort(files);

            for (File f : files)
            {
                String name = f.getName();
                if (!name.startsWith("r.") || !name.endsWith(".mca")) continue;
                String[] parts = name.substring(2, name.length() - 4).split("\\.");
                if (parts.length != 2) continue;
                int rx, rz;
                try
                {
                    rx = Integer.parseInt(parts[0]);
                    rz = Integer.parseInt(parts[1]);
                }
                catch (NumberFormatException e)
                {
                    continue;
                }

                // the region file as the server's RegionFile wrote it (its
                // writes are unbuffered): each slot's stored payload
                byte[] file = SaveOverlay.readAll(f);
                for (int slot = 0; slot < 1024; ++slot)
                {
                    if (4 * slot + 3 >= file.length) break;
                    int off = (((file[4 * slot] & 255) << 16) | ((file[4 * slot + 1] & 255) << 8) | (file[4 * slot + 2] & 255)) * 4096;
                    if (off == 0) continue;
                    int cx = rx * 32 + (slot & 31), cz = rz * 32 + (slot >> 5);
                    if (ws.getChunkProvider().chunkExists(cx, cz)) continue;
                    byte[] payload = SaveOverlay.payload(file, off);
                    if (payload == null) continue;
                    order.add(new Object[] {ws, loader, cx, cz, payload, null});
                }
            }
        }

        // the chunks not seen with this payload before, read on several
        // threads (inflate, readChunkFromNBT on a bare copy, the bytes): the
        // game threads wait for the snapshot, and nothing here touches the
        // live world
        final List<Object[]> fresh = new ArrayList<Object[]>();
        for (Object[] e : order)
        {
            WorldServer ws = (WorldServer)e[0];
            String key = ws.provider.dimensionId + "," + e[2] + "," + e[3];
            SavedChunk sc = savedCache.get(key);
            if (sc != null && Arrays.equals(sc.payload, (byte[])e[4]) && (blobDir == null || ChunkOut.known(sc.hash))) e[5] = sc;
            else fresh.add(e);
        }
        parallel(fresh.size(), new Job()
        {
            public void run(int i)
            {
                Object[] e = fresh.get(i);
                try
                {
                    e[5] = readSaved((WorldServer)e[0], e[1], read, (Integer)e[2], (Integer)e[3], (byte[])e[4]);
                }
                catch (Exception x)
                {
                    throw new IllegalStateException("Snapshot: region chunk " + e[2] + "," + e[3] + ": " + x, x);
                }
            }
        });

        for (Object[] e : order)
        {
            SavedChunk sc = (SavedChunk)e[5];
            if (sc == null) continue; // not a chunk (checkedReadChunkFromNBT refused it)
            WorldServer ws = (WorldServer)e[0];
            int cx = (Integer)e[2], cz = (Integer)e[3];
            savedCache.put(ws.provider.dimensionId + "," + cx + "," + cz, sc);
            long near = hashAround(ws, cx, cz, nbuf);
            out.write(cx, cz, sc.hash, near, sc.bytes());

            /* the region chunk's own pending ticks: readChunkFromNBT
             * schedules each on load (WorldServer.func_147446_b) */
            JsonArray ta = new JsonArray();
            for (int ti = 0; ti < sc.ticks.tagCount(); ++ti)
            {
                NBTTagCompound t = sc.ticks.getCompoundTagAt(ti);
                JsonObject to = new JsonObject();
                to.addProperty("x", t.getInteger("x"));
                to.addProperty("y", t.getInteger("y"));
                to.addProperty("z", t.getInteger("z"));
                to.addProperty("id", t.getInteger("i"));
                to.addProperty("t", "l:" + t.getInteger("t"));
                to.addProperty("pri", t.getInteger("p"));
                to.addProperty("eid", "l:" + (eid + ti));
                ta.add(to);
            }
            sc.state.add("ticks", ta);
            state.println(canonText(sc.state));
            sc.state.remove("ticks");
            sc.drop();
            ++n;
        }

        state.close();
        out.close();
        return n;
    }

    /** One region chunk read from its stored payload: its bytes and state
     * line (all but the ticks), or null when the loader refuses it. */
    static SavedChunk readSaved(WorldServer ws, Object loader, java.lang.reflect.Method read, int cx, int cz, byte[] payload)
        throws Exception
    {
        NBTTagCompound root = net.minecraft.nbt.CompressedStreamTools.read(SaveOverlay.inflate(payload));
        /* readChunkFromNBT would construct the Entities (new ids,
         * new Randoms), create the TileEntities and schedule the
         * TileTicks into the live world: read a copy without them,
         * and write them below from the region NBT itself */
        NBTTagCompound level = root.getCompoundTag("Level");
        NBTTagCompound bare = (NBTTagCompound)level.copy();
        bare.removeTag("Entities");
        bare.removeTag("TileEntities");
        bare.removeTag("TileTicks");
        NBTTagCompound bareRoot = new NBTTagCompound();
        bareRoot.setTag("Level", bare);
        Chunk c = (Chunk)read.invoke(loader, ws, cx, cz, bareRoot);
        if (c == null) return null;

        SavedChunk sc = new SavedChunk();
        sc.payload = payload;
        sc.raw = new byte[Probe.CHUNK_BYTES];
        fillChunkBytes(c, sc.raw);
        sc.hash = fnv(sc.raw);
        if (blobDir != null) writeBlob(sc.hash, sc.raw);
        else sc.packed = deflate(sc.raw);

        JsonObject js = new JsonObject();
        js.addProperty("dim", ws.provider.dimensionId);
        js.addProperty("cx", c.xPosition);
        js.addProperty("cz", c.zPosition);
        js.addProperty("saved", 1);
        js.addProperty("biome", hexBytes(c.getBiomeArray()));
        js.addProperty("terrainPopulated", c.isTerrainPopulated ? 1 : 0);
        js.addProperty("lightPopulated", c.isLightPopulated ? 1 : 0);
        js.addProperty("populated", c.field_150815_m ? 1 : 0);
        js.addProperty("modified", c.isModified ? 1 : 0);
        js.addProperty("hasEntities", c.hasEntities ? 1 : 0);
        js.addProperty("sendUpdates", c.sendUpdates ? 1 : 0);
        js.addProperty("lastSaveTime", c.lastSaveTime);
        js.addProperty("inhabitedTime", c.inhabitedTime);
        js.addProperty("queuedLightChecks", intField(c, "queuedLightChecks"));
        js.addProperty("skylightColumns", hexBools(c.updateSkylightColumns));
        js.addProperty("gap", boolField(c, "isGapLightingUpdated") ? 1 : 0);
        /* the chunk's saved entities: readChunkFromNBT would add
         * each to the world on a real load (Chunk.onChunkLoad) */
        JsonArray ea = new JsonArray();
        NBTTagList el = level.getTagList("Entities", 10);
        for (int ei = 0; ei < el.tagCount(); ++ei)
            ea.add(StructuresProbe.canon(el.getCompoundTagAt(ei)));
        js.add("ents", ea);
        /* its TileEntities as the region wrote them (writeToNBT) */
        JsonArray tea = new JsonArray();
        NBTTagList tel = level.getTagList("TileEntities", 10);
        for (int ti = 0; ti < tel.tagCount(); ++ti)
            tea.add(StructuresProbe.canon(tel.getCompoundTagAt(ti)));
        js.add("tiles", tea);
        sc.state = js;
        sc.ticks = level.getTagList("TileTicks", 10);
        return sc;
    }

    /* the region chunks this run's snapshots read, by dim,cx,cz: a stored
     * payload seen before is not inflated or read again. Between snapshots
     * its bytes are kept deflated (blob mode: not at all, the blob has them). */
    static final class SavedChunk
    {
        byte[] payload;
        byte[] raw;        // the chunk bytes, until drop
        byte[] packed;     // or deflated
        long hash;
        JsonObject state;
        NBTTagList ticks;

        byte[] bytes() throws IOException
        {
            if (raw != null) return raw;
            if (packed == null) return null;
            byte[] b = new byte[Probe.CHUNK_BYTES];
            java.util.zip.Inflater inf = new java.util.zip.Inflater();
            inf.setInput(packed);
            try
            {
                int got = 0;
                while (got < b.length && !inf.finished()) got += inf.inflate(b, got, b.length - got);
                if (got != b.length) throw new IOException("Snapshot: a kept region chunk inflated to " + got + " bytes");
            }
            catch (java.util.zip.DataFormatException e)
            {
                throw new IOException(e);
            }
            finally
            {
                inf.end();
            }
            return b;
        }

        void drop()
        {
            raw = null;
        }
    }

    static byte[] deflate(byte[] raw)
    {
        java.util.zip.Deflater d = new java.util.zip.Deflater(java.util.zip.Deflater.BEST_SPEED);
        d.setInput(raw);
        d.finish();
        java.io.ByteArrayOutputStream o = new java.io.ByteArrayOutputStream(raw.length / 32);
        byte[] buf = new byte[65536];
        while (!d.finished()) o.write(buf, 0, d.deflate(buf));
        d.end();
        return o.toByteArray();
    }

    /** The blob of one chunk's bytes, named by its own hash, unless the run
     * wrote it already or it exists; safe on several threads (each writes
     * its own temporary file and renames it over the same content). */
    static void writeBlob(long hash, byte[] b) throws IOException
    {
        String name = hex(hash, 16) + ".gz";
        synchronized (knownBlobs)
        {
            if (knownBlobs.contains(name)) return;
        }
        File f = new File(blobDir, name);
        if (!f.isFile())
        {
            File tmp = new File(blobDir, name + ".tmp" + Thread.currentThread().getId());
            java.io.OutputStream o = gzip(tmp);
            o.write(b);
            o.close();
            if (!tmp.renameTo(f)) throw new IOException("Snapshot: cannot rename " + tmp);
        }
        synchronized (knownBlobs)
        {
            knownBlobs.add(name);
        }
    }

    static final java.util.HashMap<String, SavedChunk> savedCache = new java.util.HashMap<String, SavedChunk>();

    /** Probe.hashAround, reused byte for byte through Probe.fillChunkBytes.
     * Inside a snapshot the nine chunks' own hashes are the key of a cache
     * that lives for the run: a 3x3 hash whose nine chunks (and which of them
     * are loaded) are unchanged since the last snapshot is not recomputed. */
    static long hashAround(WorldServer ws, int chx, int chz, byte[] buf)
    {
        long[] sig = null;
        String key = null;
        if (fillCache != null)
        {
            sig = nearSig(ws, chx, chz);
            key = ws.provider.dimensionId + "," + chx + "," + chz;
            long[] was = nearCache.get(key);
            if (sameSig(was, sig)) return was[0];
        }

        long h = Probe.FNV_OFFSET;

        for (int dx = -1; dx <= 1; ++dx)
        {
            for (int dz = -1; dz <= 1; ++dz)
            {
                int ax = chx + dx, az = chz + dz;

                if (!ws.getChunkProvider().chunkExists(ax, az))
                {
                    h *= Probe.FNV_PRIME;
                    continue;
                }

                if (fillCache == null)
                {
                    Probe.fillChunkBytes(ws.getChunkFromChunkCoords(ax, az), buf);
                    h = fnv(h, buf);
                }
                else h = cached(ws, ax, az).fnv(h);
            }
        }

        if (key != null)
        {
            long[] v = new long[11];
            v[0] = h;
            System.arraycopy(sig, 0, v, 1, 10);
            nearCache.put(key, v);
        }
        return h;
    }

    /** The 3x3 hash's key: the nine chunks' own hashes (0 where not loaded)
     * and the loaded mask. */
    static long[] nearSig(WorldServer ws, int chx, int chz)
    {
        long[] sig = new long[10];
        long mask = 0;
        int k = 0;
        for (int dx = -1; dx <= 1; ++dx)
            for (int dz = -1; dz <= 1; ++dz, ++k)
            {
                if (!ws.getChunkProvider().chunkExists(chx + dx, chz + dz)) continue;
                sig[k] = cached(ws, chx + dx, chz + dz).own();
                mask |= 1L << k;
            }
        sig[9] = mask;
        return sig;
    }

    static boolean sameSig(long[] was, long[] sig)
    {
        if (was == null) return false;
        for (int i = 0; i < 10; ++i) if (was[i + 1] != sig[i]) return false;
        return true;
    }

    /** The 3x3 hashes of a world's loaded chunks whose nine chunks changed
     * since this run's last snapshot, on several threads (each is a serial
     * multiply chain over nine chunks' bytes, and every other part of the
     * snapshot waits); hashAround then finds them in nearCache. Nothing here
     * draws or writes game state: the game threads wait for the snapshot. */
    static void prehash(final WorldServer ws, List chunks) throws Exception
    {
        final List<int[]> todo = new ArrayList<int[]>();
        final List<long[]> sigs = new ArrayList<long[]>();
        // every loaded chunk's bytes, own hash and segments, several at once
        final List<Chunk> fill = new ArrayList<Chunk>();
        for (Object o : chunks)
        {
            Chunk c = (Chunk)o;
            if (!fillCache.containsKey(ws.provider.dimensionId + "," + c.xPosition + "," + c.zPosition)) fill.add(c);
        }
        final Filled[] made = new Filled[fill.size()];
        parallel(made.length, new Job()
        {
            public void run(int i)
            {
                byte[] b = new byte[Probe.CHUNK_BYTES];
                fillChunkBytes(fill.get(i), b);
                Filled f = new Filled(b);
                f.own();
                f.cut();
                made[i] = f;
                if (blobDir != null)
                {
                    try
                    {
                        blobDir.mkdirs();
                        writeBlob(f.own, b);
                    }
                    catch (IOException e)
                    {
                        throw new IllegalStateException(e);
                    }
                }
            }
        });
        for (int i = 0; i < made.length; ++i)
            fillCache.put(ws.provider.dimensionId + "," + fill.get(i).xPosition + "," + fill.get(i).zPosition, made[i]);
        for (Object o : chunks)
        {
            Chunk c = (Chunk)o;
            long[] sig = nearSig(ws, c.xPosition, c.zPosition);
            if (sameSig(nearCache.get(ws.provider.dimensionId + "," + c.xPosition + "," + c.zPosition), sig)) continue;
            todo.add(new int[] {c.xPosition, c.zPosition});
            sigs.add(sig);
        }
        if (todo.isEmpty()) return;
        final long[] near = new long[todo.size()];
        parallel(near.length, new Job()
        {
            public void run(int i)
            {
                int chx = todo.get(i)[0], chz = todo.get(i)[1];
                long h = Probe.FNV_OFFSET;
                for (int dx = -1; dx <= 1; ++dx)
                    for (int dz = -1; dz <= 1; ++dz)
                    {
                        Filled f = fillCache.get(ws.provider.dimensionId + "," + (chx + dx) + "," + (chz + dz));
                        if (f == null || !ws.getChunkProvider().chunkExists(chx + dx, chz + dz)) h *= Probe.FNV_PRIME;
                        else h = f.fnv(h);
                    }
                near[i] = h;
            }
        });
        for (int i = 0; i < near.length; ++i)
        {
            long[] v = new long[11];
            v[0] = near[i];
            System.arraycopy(sigs.get(i), 0, v, 1, 10);
            nearCache.put(ws.provider.dimensionId + "," + todo.get(i)[0] + "," + todo.get(i)[1], v);
        }
    }

    interface Job
    {
        void run(int i);
    }

    /** job.run(i) for i in [0, n) on up to eight threads (one when n is
     * small); the first failure is rethrown. */
    static void parallel(final int n, final Job job) throws Exception
    {
        final int threads = Math.max(1, Math.min(8, Math.min(Runtime.getRuntime().availableProcessors(), n / 16)));
        if (threads == 1)
        {
            for (int i = 0; i < n; ++i) job.run(i);
            return;
        }
        Thread[] ts = new Thread[threads];
        final Throwable[] error = new Throwable[1];
        for (int t = 0; t < threads; ++t)
        {
            final int first = t;
            ts[t] = new Thread(new Runnable()
            {
                public void run()
                {
                    try
                    {
                        for (int i = first; i < n; i += threads) job.run(i);
                    }
                    catch (Throwable e)
                    {
                        error[0] = e;
                    }
                }
            }, "Oracle Snapshot worker " + t);
            ts[t].start();
        }
        for (Thread t : ts) t.join();
        if (error[0] != null) throw new IllegalStateException("Snapshot: " + error[0], error[0]);
    }

    /* the run's 3x3 hashes: dim,cx,cz to {near, the nine own hashes, the
     * loaded mask} */
    static final java.util.HashMap<String, long[]> nearCache = new java.util.HashMap<String, long[]>();

    /* One snapshot's chunk bytes, filled once per chunk (every chunk's 3x3
     * hash reads its eight neighbours again), with the bytes cut into
     * nonzero segments and zero runs when a 3x3 hash needs them: FNV-1a turns
     * a zero byte into one multiply by the prime, so a run of k zeros is one
     * multiply by prime^k. null outside a dump. */
    static java.util.HashMap<String, Filled> fillCache;

    static final class Filled
    {
        byte[] bytes;
        byte[] nz;     // the nonzero segments, back to back
        int[] seg;     // segment i's length in nz
        int[] zeros;   // then run i's zeros (prime^zeros[i] is Snapshot.primePow)
        int nseg;
        long own;
        boolean hasOwn;

        Filled(byte[] b)
        {
            bytes = b;
        }

        long own()
        {
            if (!hasOwn)
            {
                own = Snapshot.fnv(Probe.FNV_OFFSET, bytes);
                hasOwn = true;
            }
            return own;
        }

        /** One (nonzero segment, zero run) pair per pass, the first from byte 0 (its segment empty when byte 0 is zero); sized exactly. */
        void cut()
        {
            byte[] b = bytes;
            int n = b.length, count = 1, nnz = 0;
            for (int i = 0; i < n; ++i)
            {
                if (b[i] == 0) continue;
                ++nnz;
                if (i > 0 && b[i - 1] == 0) ++count;
            }
            nz = new byte[nnz];
            seg = new int[count];
            zeros = new int[count];
            count = 0;
            int k = 0;
            for (int i = 0; i < n; )
            {
                int start = k;
                while (i < n && b[i] != 0) nz[k++] = b[i++];
                int z = i;
                while (i < n && b[i] == 0) ++i;
                seg[count] = k - start;
                zeros[count] = i - z;
                ++count;
            }
            nseg = count;
        }

        long fnv(long h)
        {
            if (nz == null) cut();
            int k = 0;
            for (int s = 0; s < nseg; ++s)
            {
                for (int e = k + seg[s]; k < e; ++k) h = (h ^ (nz[k] & 255)) * Probe.FNV_PRIME;
                h *= primePow(zeros[s]);
            }
            return h;
        }
    }

    static Filled cached(WorldServer ws, int cx, int cz)
    {
        String key = ws.provider.dimensionId + "," + cx + "," + cz;
        Filled f = fillCache.get(key);
        if (f == null)
        {
            byte[] b = new byte[Probe.CHUNK_BYTES];
            fillChunkBytes(ws.getChunkFromChunkCoords(cx, cz), b);
            f = new Filled(b);
            fillCache.put(key, f);
        }
        return f;
    }

    /** Probe.fillChunkBytes's layout, byte for byte, read straight from the
     * section arrays (the probe's per-cell NibbleArray.get is most of its
     * time); the height maps and the mask through the probe's own code. */
    static void fillChunkBytes(Chunk c, byte[] out)
    {
        ExtendedBlockStorage[] sa = c.getBlockStorageArray();
        Arrays.fill(out, 0, 65536 * 5, (byte)0);
        for (int s = 0; s < 16; ++s)
        {
            ExtendedBlockStorage e = sa[s];
            if (e == null) continue;
            byte[] lsb = e.getBlockLSBArray();
            byte[] msb = e.getBlockMSBArray() == null ? null : e.getBlockMSBArray().data;
            byte[] meta = e.getMetadataArray().data;
            byte[] sky = e.getSkylightArray() == null ? null : e.getSkylightArray().data;
            byte[] blk = e.getBlocklightArray().data;
            int y0 = s << 4;
            for (int yy = 0; yy < 16; ++yy)
            {
                for (int z = 0; z < 16; ++z)
                {
                    for (int x = 0; x < 16; ++x)
                    {
                        int i = yy << 8 | z << 4 | x, half = i >> 1, sh = (i & 1) << 2;
                        int cell = x << 12 | z << 8 | (y0 + yy);
                        int id = lsb[i] & 255;
                        if (msb != null) id |= ((msb[half] >> sh) & 15) << 8;
                        out[2 * cell] = (byte)id;
                        out[2 * cell + 1] = (byte)(id >> 8);
                        out[131072 + cell] = (byte)((meta[half] >> sh) & 15);
                        if (sky != null) out[196608 + cell] = (byte)((sky[half] >> sh) & 15);
                        out[262144 + cell] = (byte)((blk[half] >> sh) & 15);
                    }
                }
            }
        }
        Probe.fillChunkTail(c, out);
    }

    /* chunks.bin.gz, or in blob mode (blobDir) chunks.idx over the blobs.
     * The .bin.gz stream is written by a thread of its own: the chunks go to
     * it in order, and it makes the same calls on the same stream a write
     * on the snapshot thread did (the same bytes), while the snapshot goes on
     * with the next chunk's state line; in a dump (deferred) it goes on
     * through the snapshot's later phases, and dump finishes it at the end. */
    static final class ChunkOut
    {
        DataOutputStream bin;
        PrintWriter idx;
        File file;
        String rel;
        long bytes;
        final boolean chunks;
        private java.util.concurrent.LinkedBlockingQueue<Object[]> queue;
        private Thread writer;
        private volatile Throwable failed;
        private static final Object[] END = new Object[0];

        /** dump's writers whose stream is still being written, finished at the end of the dump; null elsewhere. */
        static List<ChunkOut> deferred;

        ChunkOut(File dir, String base) throws IOException
        {
            chunks = base.equals("chunks");
            if (blobDir == null)
            {
                file = new File(dir, base + ".bin.gz");
                bin = new DataOutputStream(new BufferedOutputStream(gzip(file), 1 << 20));
                // the loaded chunks' bytes are held by fillCache for the whole dump anyway;
                // a region chunk's are not: at most 64 of them (21 MB) wait for the stream
                queue = new java.util.concurrent.LinkedBlockingQueue<Object[]>(chunks ? Integer.MAX_VALUE : 64);
                writer = new Thread(new Runnable() { public void run() { drain(); } }, "Oracle Snapshot " + base);
                writer.setDaemon(true);
                writer.start();
                return;
            }
            blobDir.mkdirs();
            file = new File(dir, base + ".idx");
            idx = new PrintWriter(new OutputStreamWriter(new FileOutputStream(file), "UTF-8"));
            rel = dir.getAbsoluteFile().toPath().relativize(blobDir.getAbsoluteFile().toPath()).toString();
        }

        private void drain()
        {
            try
            {
                while (true)
                {
                    Object[] e = queue.take();
                    if (e == END) break;
                    if (failed != null) continue;
                    le32(bin, (Integer)e[0]);
                    le32(bin, (Integer)e[1]);
                    le64(bin, (Long)e[2]);
                    le64(bin, (Long)e[3]);
                    bin.write((byte[])e[4]);
                }
            }
            catch (Throwable t)
            {
                failed = t;
            }
        }

        /** a chunk whose blob is written already (known(hash)) may pass null bytes; b is not changed afterwards */
        void write(int cx, int cz, long hash, long near, byte[] b) throws Exception
        {
            if (bin != null)
            {
                if (failed != null) throw new IOException("Snapshot: " + file, failed);
                queue.put(new Object[] {cx, cz, hash, near, b});
                return;
            }
            String name = hex(hash, 16) + ".gz";
            writeBlob(hash, b);
            idx.println(cx + " " + cz + " " + hex(hash, 16) + " " + hex(near, 16) + " " + rel + "/" + name);
        }

        static boolean known(long hash)
        {
            if (blobDir == null) return false;
            synchronized (knownBlobs)
            {
                return knownBlobs.contains(hex(hash, 16) + ".gz");
            }
        }

        void close() throws IOException
        {
            if (bin != null && deferred != null)
            {
                deferred.add(this);
                return;
            }
            finish();
        }

        /** The stream written and closed, and its length counted. */
        void finish() throws IOException
        {
            if (bin != null)
            {
                try
                {
                    queue.put(END);
                    writer.join();
                }
                catch (InterruptedException e) { throw new IOException(e); }
                if (failed != null)
                {
                    try { bin.close(); }
                    catch (IOException e) {}
                    throw new IOException("Snapshot: " + file, failed);
                }
                bin.close();
            }
            if (idx != null) idx.close();
            bytes += file.length();
            if (chunks) bytesWritten = bytes;
        }

        /** dump's end: every deferred stream finished (all of them, the first failure rethrown). */
        static void finishDeferred() throws IOException
        {
            List<ChunkOut> d = deferred;
            deferred = null;
            if (d == null) return;
            IOException first = null;
            for (ChunkOut o : d)
            {
                try { o.finish(); }
                catch (IOException e) { if (first == null) first = e; }
            }
            if (first != null) throw first;
        }
    }

    static String dhex(double v) { return String.format("d:%016x", Double.doubleToRawLongBits(v)); }
    static String fhex(float v) { return String.format("f:%08x", Float.floatToRawIntBits(v)); }

    /* EntityDragon's flight and fight state that NBT does not carry: the
     * target, the ring buffer, the animation, the healing crystal, the
     * living counters, the box and the seven parts */
    static JsonObject dragonState(net.minecraft.entity.boss.EntityDragon d)
    {
        JsonObject o = new JsonObject();
        JsonArray t = new JsonArray();
        t.add(new JsonPrimitive(dhex(d.targetX)));
        t.add(new JsonPrimitive(dhex(d.targetY)));
        t.add(new JsonPrimitive(dhex(d.targetZ)));
        o.add("target", t);
        o.addProperty("ringi", d.ringBufferIndex);
        if (d.ringBufferIndex >= 0)
        {
            JsonArray r = new JsonArray();
            for (int i = 0; i < 64; ++i)
            {
                r.add(new JsonPrimitive(dhex(d.ringBuffer[i][0])));
                r.add(new JsonPrimitive(dhex(d.ringBuffer[i][1])));
            }
            o.add("ring", r);
        }
        o.addProperty("anim", fhex(d.animTime));
        o.addProperty("panim", fhex(d.prevAnimTime));
        o.addProperty("force", d.forceNewTarget ? 1 : 0);
        o.addProperty("slowed", d.slowed ? 1 : 0);
        Object target = objField(d, "target");
        o.addProperty("hunt", target == null ? -1 : ((Entity)target).getEntityId());
        o.addProperty("deathTicks", d.deathTicks);
        o.addProperty("heal", d.healingEnderCrystal == null ? -1 : d.healingEnderCrystal.getEntityId());
        o.addProperty("ticksExisted", d.ticksExisted);
        o.addProperty("hurtResistantTime", d.hurtResistantTime);
        o.addProperty("lastDamage", fhex(floatField(d, "lastDamage")));
        o.addProperty("prevHealth", fhex(d.prevHealth));
        o.addProperty("yawVelocity", fhex(floatField(d, "randomYawVelocity")));
        o.addProperty("renderYaw", fhex(d.renderYawOffset));
        o.addProperty("prevYaw", fhex(d.prevRotationYaw));
        JsonArray prev = new JsonArray();
        prev.add(new JsonPrimitive(dhex(d.prevPosX)));
        prev.add(new JsonPrimitive(dhex(d.prevPosY)));
        prev.add(new JsonPrimitive(dhex(d.prevPosZ)));
        o.add("prev", prev);
        JsonArray bb = new JsonArray();
        bb.add(new JsonPrimitive(dhex(d.boundingBox.minX)));
        bb.add(new JsonPrimitive(dhex(d.boundingBox.minY)));
        bb.add(new JsonPrimitive(dhex(d.boundingBox.minZ)));
        bb.add(new JsonPrimitive(dhex(d.boundingBox.maxX)));
        bb.add(new JsonPrimitive(dhex(d.boundingBox.maxZ)));
        o.add("bb", bb);
        JsonArray parts = new JsonArray();
        for (net.minecraft.entity.boss.EntityDragonPart p : d.dragonPartArray)
        {
            JsonArray a = new JsonArray();
            a.add(new JsonPrimitive(p.getEntityId()));
            a.add(new JsonPrimitive(dhex(p.posX)));
            a.add(new JsonPrimitive(dhex(p.posY)));
            a.add(new JsonPrimitive(dhex(p.posZ)));
            a.add(new JsonPrimitive(fhex(p.width)));
            a.add(new JsonPrimitive(fhex(p.height)));
            parts.add(a);
        }
        o.add("parts", parts);
        return o;
    }

    static long fnv(byte[] b)
    {
        return fnv(Probe.FNV_OFFSET, b);
    }

    /* FNV-1a 64 on from h: a zero byte is one multiply by the prime, so a
     * run of k zeros (most of a chunk) is one multiply by prime^k */
    static long fnv(long h, byte[] b)
    {
        int n = b.length;
        for (int i = 0; i < n; )
        {
            if (b[i] != 0)
            {
                h = (h ^ (b[i] & 255)) * Probe.FNV_PRIME;
                ++i;
                continue;
            }
            int j = i;
            while (j < n && b[j] == 0) ++j;
            h *= primePow(j - i);
            i = j;
        }
        return h;
    }

    /* prime^k as prime^(k mod 1024) * prime^(1024 * (k / 1024)): one multiply
     * for every run a chunk can hold (k < 2^20) */
    static final long[] POW_LO = new long[1024], POW_HI = new long[1024];
    static
    {
        long p = 1;
        for (int i = 0; i < 1024; ++i) { POW_LO[i] = p; p *= Probe.FNV_PRIME; }
        long q = 1;
        for (int i = 0; i < 1024; ++i) { POW_HI[i] = q; q *= p; }
    }

    static long primePow(int k)
    {
        if (k < (1 << 20)) return POW_LO[k & 1023] * POW_HI[k >>> 10];
        long r = 1, p = Probe.FNV_PRIME;
        while (k > 0)
        {
            if ((k & 1) != 0) r *= p;
            p *= p;
            k >>= 1;
        }
        return r;
    }

    // ---------------------------------------------------------------- entities

    static void writeEntities(IntegratedServer server, File dir, int[] counts) throws Exception
    {
        EntityState.collect(true);
        try
        {
            writeEntities(server, dir, counts, true);
        }
        finally
        {
            EntityState.collect(false);
        }
    }

    static void writeEntities(IntegratedServer server, File dir, int[] counts, boolean ghosts) throws Exception
    {
        PrintWriter w = textOut(dir, "entities.jsonl");
        Set<Entity> seen = Collections.newSetFromMap(new IdentityHashMap<Entity, Boolean>());

        for (int d = 0; d < server.worldServers.length; ++d)
        {
            WorldServer x = server.worldServers[d];
            if (x == null) continue;

            for (Object o : x.loadedEntityList)
            {
                Entity e = (Entity)o;
                seen.add(e);
                writeEntity(w, d, e, 0, counts);
            }

            // lightning and the like: spawned into weatherEffects, not loadedEntityList
            for (Object o : x.weatherEffects)
            {
                Entity e = (Entity)o;
                if (seen.contains(e)) continue;
                writeEntity(w, d, e, 1, counts);
                ++counts[2];
            }
        }

        w.close();

        // ghosts.jsonl: the living entities no world lists that a listed one
        // still holds (an AI target, a revenge target, a task's partner):
        // one whose chunk unloaded (World.updateEntities dropped it, alive,
        // where it stood) or a dead one. The holder keeps reading it (its
        // position, isEntityAlive, its box), so a resume needs it whole. The
        // same entry form as entities.jsonl, in the order first referenced,
        // their own references included; written only when there is one.
        new File(dir, "ghosts.jsonl").delete();
        new File(dir, "ghosts.jsonl.gz").delete();
        if (!ghosts) return;
        PrintWriter g = null;
        java.util.List<Entity> refs = EntityState.collected();
        for (int i = 0; i < refs.size(); ++i)
        {
            Entity e = refs.get(i);
            if (seen.contains(e) || !(e instanceof net.minecraft.entity.EntityLivingBase)
                || e instanceof net.minecraft.entity.player.EntityPlayer) continue;
            int d = -1;
            for (int k = 0; k < server.worldServers.length; ++k)
                if (server.worldServers[k] == e.worldObj) d = k;
            if (d < 0) continue;
            seen.add(e);
            if (g == null) g = textOut(dir, "ghosts.jsonl");
            writeEntity(g, d, e, 0, new int[4]);
            refs = EntityState.collected();
        }
        if (g != null) g.close();
    }

    /** The entity's EntityTrackerEntry, what the next updateTrackedEntities
     * reads: trk is ticks, ticksSinceLastForcedTeleport, the last scaled
     * position (x, y, z), the last yaw, pitch and head angle bytes and
     * isDataInitialized; trkd the last sent motion (x, y, z) and the
     * position of the last range check (posX, posY, posZ), as raw double
     * bits in hex. A checkpoint's entities were tracked at their load (the
     * spawn area or the login), so the counter is not the world's time. */
    static void trackerState(JsonObject o, Entity e)
    {
        if (!(e.worldObj instanceof WorldServer)) return;
        net.minecraft.util.IntHashMap ids = (net.minecraft.util.IntHashMap)objField(objField(e.worldObj, "theEntityTracker"), "trackedEntityIDs");
        net.minecraft.entity.EntityTrackerEntry te = (net.minecraft.entity.EntityTrackerEntry)ids.lookup(e.getEntityId());
        if (te == null) return;
        o.addProperty("trk", te.ticks + "," + intField(te, "ticksSinceLastForcedTeleport") + "," + te.lastScaledXPosition + ","
            + te.lastScaledYPosition + "," + te.lastScaledZPosition + "," + te.lastYaw + "," + te.lastPitch + "," + te.lastHeadMotion + ","
            + (boolField(te, "isDataInitialized") ? 1 : 0));
        double[] d = {te.motionX, te.motionY, te.motionZ, doubleField(te, "posX"), doubleField(te, "posY"), doubleField(te, "posZ")};
        StringBuilder b = new StringBuilder();
        for (int i = 0; i < d.length; ++i) b.append(i > 0 ? "," : "").append(String.format("%016x", Double.doubleToRawLongBits(d[i])));
        o.addProperty("trkd", b.toString());
    }

    static void writeEntity(PrintWriter w, int dim, Entity e, int weather, int[] counts)
    {
        JsonObject o = new JsonObject();
        o.addProperty("dim", dim);
        o.addProperty("id", e.getEntityId());
        o.addProperty("class", e.getClass().getSimpleName());
        o.addProperty("player", e instanceof EntityPlayerMP ? 1 : 0);
        o.addProperty("weather", weather);
        java.util.Random rand = (java.util.Random)objField(e, "rand");
        o.addProperty("rand", "l:" + Det.state(rand));
        if (e instanceof net.minecraft.entity.EntityLivingBase)
        {
            o.addProperty("age", intField(e, "entityAge"));
            if (e instanceof net.minecraft.entity.EntityLiving)
                o.addProperty("lsf", intField(e, "livingSoundTime"));
        }
        if (rand instanceof Det.Born) o.addProperty("rand0", "l:" + ((Det.Born)rand).born);
        trackerState(o, e);
        // EntityXPOrb's two constructors make different boxes (0.25 from NBT,
        // 0.5 when spawned), and so do EntityFallingBlock's (Entity's 0.6 by
        // 1.8 from NBT, 0.98 when it falls); NBT does not carry which one
        if (e instanceof net.minecraft.entity.item.EntityXPOrb || e instanceof net.minecraft.entity.item.EntityFallingBlock)
            o.addProperty("size", String.format("f:%08x", Float.floatToRawIntBits(e.width)));
        try
        {
            java.lang.reflect.Field have = findField(java.util.Random.class, "haveNextNextGaussian");
            java.lang.reflect.Field next = findField(java.util.Random.class, "nextNextGaussian");
            if (have.getBoolean(rand))
                o.addProperty("gauss", String.format("d:%016x", Double.doubleToRawLongBits(next.getDouble(rand))));
        }
        catch (Exception x) { throw new IllegalStateException(x); }
        if (e instanceof EntityLiving)
        {
            try
            {
                /* the navigator's active path: Java keeps walking it after
                 * the snapshot, so the replay must too */
                java.lang.reflect.Field nf = findField(EntityLiving.class, "navigator");
                net.minecraft.pathfinding.PathNavigate nav = (net.minecraft.pathfinding.PathNavigate)nf.get(e);
                java.lang.reflect.Field pf = findField(net.minecraft.pathfinding.PathNavigate.class, "currentPath");
                net.minecraft.pathfinding.PathEntity path = (net.minecraft.pathfinding.PathEntity)pf.get(nav);
                java.lang.reflect.Field sf = findField(net.minecraft.pathfinding.PathNavigate.class, "speed");
                double speed = sf.getDouble(nav);
                if (path != null)
                {
                    java.lang.reflect.Field ptf = findField(net.minecraft.pathfinding.PathEntity.class, "points");
                    net.minecraft.pathfinding.PathPoint[] pts = (net.minecraft.pathfinding.PathPoint[])ptf.get(path);
                    StringBuilder b = new StringBuilder("ia:");
                    for (net.minecraft.pathfinding.PathPoint pt : pts)
                        b.append(pt.xCoord).append(',').append(pt.yCoord).append(',').append(pt.zCoord).append(',');
                    o.addProperty("path", b.substring(0, b.length() - 1));
                    o.addProperty("pathi", path.getCurrentPathIndex());
                    o.addProperty("pathspeed", String.format("d:%016x", Double.doubleToRawLongBits(speed)));
                }
            }
            catch (Exception x) { throw new IllegalStateException(x); }
            try
            {
                /* a running EntityAIWatchClosest keeps lookTime ticks of
                 * looking (and the look helper drives the pitch): the replay
                 * needs it or the mob's head snaps level at the snapshot */
                java.lang.reflect.Field tf = findField(EntityLiving.class, "tasks");
                net.minecraft.entity.ai.EntityAITasks tasks = (net.minecraft.entity.ai.EntityAITasks)tf.get(e);
                java.lang.reflect.Field ef = findField(net.minecraft.entity.ai.EntityAITasks.class, "executingTaskEntries");
                for (Object entry : (java.util.List)ef.get(tasks))
                {
                    java.lang.reflect.Field af = entry.getClass().getField("action");
                    af.setAccessible(true);
                    Object action = af.get(entry);
                    if (!(action instanceof net.minecraft.entity.ai.EntityAIWatchClosest)) continue;
                    java.lang.reflect.Field lf = findField(net.minecraft.entity.ai.EntityAIWatchClosest.class, "lookTime");
                    int lookTime = lf.getInt(action);
                    if (lookTime <= 0) continue;
                    java.lang.reflect.Field cf = findField(net.minecraft.entity.ai.EntityAIWatchClosest.class, "closestEntity");
                    Object watched = cf.get(action);
                    o.addProperty("watch", (watched instanceof EntityPlayerMP ? 1 : 0) + "," + lookTime);
                    break;
                }
            }
            catch (Exception x) { throw new IllegalStateException(x); }
        }
        // the village a villager or golem last looked up and its countdown to
        // the next lookup: runtime only, so a replay cannot derive them
        if (e instanceof net.minecraft.entity.passive.EntityVillager)
            o.addProperty("vil", villageIndex(e.worldObj, objField(e, "villageObj")) + "," + intField(e, "randomTickDivider")
                + "," + (boolField(e, "isLookingForHome") ? 1 : 0));
        if (e instanceof net.minecraft.entity.monster.EntityIronGolem)
            o.addProperty("vil", villageIndex(e.worldObj, objField(e, "villageObj")) + "," + intField(e, "homeCheckTimer"));
        if (e instanceof net.minecraft.entity.boss.EntityDragon) o.add("dragon", dragonState((net.minecraft.entity.boss.EntityDragon)e));
        // innerRotation comes from the constructor's draw, not NBT
        if (e instanceof net.minecraft.entity.item.EntityEnderCrystal)
            o.addProperty("crystal", ((net.minecraft.entity.item.EntityEnderCrystal)e).innerRotation + "," + ((net.minecraft.entity.item.EntityEnderCrystal)e).health);
        // EntityThrowable's counters and its thrower, none of them saved
        if (e instanceof net.minecraft.entity.projectile.EntityThrowable)
        {
            Object thrower = objField(e, "thrower");
            o.addProperty("throwable", intField(e, "ticksInAir") + "," + intField(e, "ticksInGround") + ","
                + (thrower == null ? -1 : ((Entity)thrower).getEntityId()) + "," + e.ticksExisted);
        }
        // everything else the next tick reads: the class chain's fields, the
        // data watcher, the AI (EntityState)
        o.add("rt", EntityState.of(e));
        // its place in its chunk slice's list (Chunk.entityLists, in which
        // order the chunk's queries find entities)
        if (e.addedToChunk && e.worldObj.getChunkProvider().chunkExists(e.chunkCoordX, e.chunkCoordZ))
        {
            Chunk ch = e.worldObj.getChunkFromChunkCoords(e.chunkCoordX, e.chunkCoordZ);
            int cy = Math.max(0, Math.min(15, e.chunkCoordY));
            o.addProperty("cidx", ch.entityLists[cy].indexOf(e));
        }
        // its EntityTrackerEntry: the last positions, angles and motion sent,
        // the update counter, and whether the player is watching it
        if (e.worldObj instanceof WorldServer)
        {
            Object entry = ((net.minecraft.util.IntHashMap)objField(((WorldServer)e.worldObj).getEntityTracker(), "trackedEntityIDs")).lookup(e.getEntityId());
            if (entry != null)
            {
                JsonObject t = EntityState.fields(entry, Object.class);
                t.addProperty("watchedBy", "i:" + ((java.util.Set)objField(entry, "trackingPlayers")).size());
                o.add("trkrt", t);
            }
        }
        NBTTagCompound tag = new NBTTagCompound();
        e.writeToNBT(tag);
        o.add("nbt", StructuresProbe.canon(tag));
        w.println(canonText(o));
        ++counts[0];
        if (e instanceof EntityPlayerMP) ++counts[1];
    }

    // ---------------------------------------------------------------- villages

    static int villageIndex(World w, Object village)
    {
        if (village == null || w.villageCollectionObj == null) return -1;
        return w.villageCollectionObj.getVillageList().indexOf(village);
    }

    /**
     * WorldServer.villageCollectionObj as it is now. A checkpoint start reads
     * villages.dat (VillageCollection.readFromNBT), but the join ticks have
     * already ticked it, so the live object is written: the NBT keys plus the
     * runtime state NBT does not carry.
     */
    static String villages(WorldServer ws)
    {
        JsonObject o = new JsonObject();
        net.minecraft.village.VillageCollection vc = ws.villageCollectionObj;
        o.addProperty("Tick", "i:" + intField(vc, "tickCounter"));
        StringBuilder pos = new StringBuilder("ia:");
        int n = 0;
        for (Object x : (List)objField(vc, "villagerPositionsList"))
        {
            net.minecraft.util.ChunkCoordinates c = (net.minecraft.util.ChunkCoordinates)x;
            if (n++ > 0) pos.append(',');
            pos.append(c.posX).append(',').append(c.posY).append(',').append(c.posZ);
        }
        o.addProperty("pos", pos.toString());
        if (!((List)objField(vc, "newDoors")).isEmpty()) throw new IllegalStateException("Snapshot: VillageCollection.newDoors is not empty between ticks");
        JsonArray list = new JsonArray();
        for (Object x : vc.getVillageList())
        {
            net.minecraft.village.Village v = (net.minecraft.village.Village)x;
            NBTTagCompound tag = new NBTTagCompound();
            v.writeVillageDataToNBT(tag);
            JsonObject vo = (JsonObject)StructuresProbe.canon(tag);
            StringBuilder rc = new StringBuilder("ia:");
            int k = 0;
            for (Object d : v.getVillageDoorInfoList())
            {
                if (k++ > 0) rc.append(',');
                rc.append(((net.minecraft.village.VillageDoorInfo)d).getDoorOpeningRestrictionCounter());
            }
            vo.addProperty("DoorsRC", rc.toString());
            StringBuilder agg = new StringBuilder("ia:");
            k = 0;
            for (Object a : (List)objField(v, "villageAgressors"))
            {
                if (k++ > 0) agg.append(',');
                agg.append(((Entity)objField(a, "agressor")).getEntityId()).append(',').append(intField(a, "agressionTime"));
            }
            vo.addProperty("Agg", agg.toString());
            list.add(vo);
        }
        o.add("Villages", list);
        Object siege = objField(ws, "villageSiegeObj");
        o.addProperty("siegeV", "i:" + villageIndex(ws, objField(siege, "theVillage")));
        o.addProperty("siegeG", "i:" + intField(siege, "field_75532_g"));
        o.addProperty("siegeH", "i:" + intField(siege, "field_75538_h"));
        o.addProperty("siegeI", "i:" + intField(siege, "field_75539_i"));
        return canonText(o);
    }

    /**
     * The client world's entities other than the player, in its
     * loadedEntityList order: each one's class, id, fields and data watcher
     * (EntityState). They are the client's own copies, moved by the tracker's
     * packets and interpolated (serverPos, newPos), which the pick, a rider
     * and the client player's collisions read.
     */
    static void writeClientEntities(Minecraft mc, File dir) throws Exception
    {
        PrintWriter w = textOut(dir, "clientents.jsonl");
        for (Object x : mc.theWorld.loadedEntityList)
        {
            Entity e = (Entity)x;
            if (e == mc.thePlayer) continue;
            JsonObject o = new JsonObject();
            o.addProperty("id", e.getEntityId());
            o.addProperty("class", e.getClass().getSimpleName());
            o.add("f", EntityState.fields(e, Object.class));
            o.add("dw", EntityState.watcher(e.getDataWatcher()));
            if (e instanceof EntityLiving)
                o.add("body", EntityState.fields(objField(e, "bodyHelper"), Object.class));
            // its place in its chunk slice's entity list (the pick's order)
            if (e.addedToChunk && mc.theWorld.getChunkProvider().chunkExists(e.chunkCoordX, e.chunkCoordZ))
            {
                Chunk c = mc.theWorld.getChunkFromChunkCoords(e.chunkCoordX, e.chunkCoordZ);
                int cy = Math.max(0, Math.min(15, e.chunkCoordY));
                o.addProperty("cidx", c.entityLists[cy].indexOf(e));
            }
            w.println(canonText(o));
        }
        w.close();
    }

    // -------------------------------------------------------------- structures

    static String structures(IntegratedServer server)
    {
        JsonObject o = new JsonObject();
        for (int i = 0; i < server.worldServers.length; ++i)
        {
            WorldServer ws = server.worldServers[i];
            if (ws == null) continue;
            Object gen = objField(ws.theChunkProviderServer, "currentChunkProvider");
            if (gen instanceof net.minecraft.world.gen.ChunkProviderHell)
            {
                /* the fortress map: Fortress.dat's starts in its NBT order
                 * (loaded on first use), then the offers; the spawn list's
                 * func_142038_b reads the first sizeable start */
                java.util.Map sm = (java.util.Map)objField(((net.minecraft.world.gen.ChunkProviderHell)gen).genNetherBridge, "structureMap");
                if (sm.isEmpty()) continue;
                StringBuilder b = new StringBuilder("ia:");
                int n = 0;
                for (Object key : sm.keySet())
                {
                    long k = ((Long)key).longValue();
                    if (n++ > 0) b.append(',');
                    b.append((int)k).append(',').append((int)(k >> 32));
                }
                JsonObject w = new JsonObject();
                w.addProperty("Fortress", b.toString());
                o.add("w" + i, w);
                continue;
            }
            if (!(gen instanceof net.minecraft.world.gen.ChunkProviderGenerate)) continue;
            JsonObject w = new JsonObject();
            String[] fields = {"mineshaftGenerator", "villageGenerator", "strongholdGenerator", "scatteredFeatureGenerator"};
            for (String fname : fields)
            {
                net.minecraft.world.gen.structure.MapGenStructure m = (net.minecraft.world.gen.structure.MapGenStructure)objField(gen, fname);
                StringBuilder b = new StringBuilder("ia:");
                int n = 0;
                for (Object key : ((java.util.Map)objField(m, "structureMap")).keySet())
                {
                    long k = ((Long)key).longValue();
                    if (n++ > 0) b.append(',');
                    b.append((int)k).append(',').append((int)(k >> 32));
                }
                w.addProperty(m.func_143025_a(), b.toString());
                w.add(m.func_143025_a() + ".pieces", pieces(m));
                w.add(m.func_143025_a() + ".flags", pieceFlags(m));
                if (m instanceof net.minecraft.world.gen.structure.MapGenStronghold) w.addProperty("Stronghold.portal", strongholdPortal(m));
            }
            o.add("w" + i, w);
        }
        return canonText(o);
    }

    /**
     * Per start (the structureMap's iteration order), its components as they
     * are now: what a start read back from data/<Type>.dat holds and a start
     * rebuilt from the seed does not. Population drops a component whose
     * addComponentParts fails, re-offsets a village piece or a temple once
     * (HPos) and counts a village piece's villagers (VCount). Per component:
     * minX minY minZ maxX maxY maxZ HPos VCount, -1 and 0 where the class has
     * neither.
     */
    static JsonArray pieces(net.minecraft.world.gen.structure.MapGenStructure m)
    {
        JsonArray a = new JsonArray();
        for (Object st : ((java.util.Map)objField(m, "structureMap")).values())
        {
            StringBuilder b = new StringBuilder("ia:");
            int n = 0;
            for (Object c : ((net.minecraft.world.gen.structure.StructureStart)st).getComponents())
            {
                net.minecraft.world.gen.structure.StructureBoundingBox bb = ((net.minecraft.world.gen.structure.StructureComponent)c).getBoundingBox();
                int hpos = -1, vcount = 0;
                // StructureVillagePieces.Village and ComponentScatteredFeaturePieces.Feature are package-private
                if (hasField(c, "villagersSpawned"))
                {
                    hpos = intField(c, "field_143015_k");
                    vcount = intField(c, "villagersSpawned");
                }
                else if (hasField(c, "field_74936_d"))
                    hpos = intField(c, "field_74936_d");
                if (n++ > 0) b.append(',');
                b.append(bb.minX).append(',').append(bb.minY).append(',').append(bb.minZ).append(',')
                    .append(bb.maxX).append(',').append(bb.maxY).append(',').append(bb.maxZ).append(',')
                    .append(hpos).append(',').append(vcount);
            }
            a.add(new JsonPrimitive(b.toString()));
        }
        return a;
    }

    /**
     * Per stronghold start (the structureMap's order), 1 when its Stairs2
     * links its portal room, 0 when not: a start read back from
     * data/Stronghold.dat (a world joined again) has no link, and
     * func_151545_a's target (an ender eye's) is then the staircase's
     * center, not the portal room's.
     */
    static String strongholdPortal(net.minecraft.world.gen.structure.MapGenStructure m)
    {
        StringBuilder b = new StringBuilder("ia:");
        int n = 0;
        for (Object st : ((java.util.Map)objField(m, "structureMap")).values())
        {
            List cs = ((net.minecraft.world.gen.structure.StructureStart)st).getComponents();
            Object c = cs.isEmpty() ? null : cs.get(0);
            boolean linked = c instanceof net.minecraft.world.gen.structure.StructureStrongholdPieces.Stairs2
                && ((net.minecraft.world.gen.structure.StructureStrongholdPieces.Stairs2)c).strongholdPortalRoom != null;
            if (n++ > 0) b.append(',');
            b.append(linked ? 1 : 0);
        }
        return b.toString();
    }

    /**
     * A chunk's chests' state that TileEntity NBT does not carry, as
     * x,y,z,checked,adj,ticks,players,lid,prevLid per chest in x, y, z
     * order (null when the chunk has none): field_145984_a (the neighbour scan
     * ran), which neighbour fields hold a chest (bit 0 z-1 field_145992_i,
     * bit 1 z+1 field_145988_l, bit 2 x+1 field_145990_j, bit 3 x-1
     * field_145991_k), field_145983_q, numPlayersUsing and the two lid angles
     * as raw float bits.
     */
    static String chestState(net.minecraft.world.chunk.Chunk c)
    {
        List chests = new ArrayList();
        for (Object to : c.chunkTileEntityMap.values())
            if (to instanceof net.minecraft.tileentity.TileEntityChest) chests.add(to);
        if (chests.isEmpty()) return null;
        Collections.sort(chests, new Comparator()
        {
            public int compare(Object a, Object b)
            {
                TileEntity x = (TileEntity)a, y = (TileEntity)b;
                if (x.field_145851_c != y.field_145851_c) return x.field_145851_c < y.field_145851_c ? -1 : 1;
                if (x.field_145848_d != y.field_145848_d) return x.field_145848_d < y.field_145848_d ? -1 : 1;
                return x.field_145849_e < y.field_145849_e ? -1 : (x.field_145849_e > y.field_145849_e ? 1 : 0);
            }
        });
        StringBuilder b = new StringBuilder();
        for (Object o : chests)
        {
            net.minecraft.tileentity.TileEntityChest t = (net.minecraft.tileentity.TileEntityChest)o;
            int adj = (t.field_145992_i != null ? 1 : 0) | (t.field_145988_l != null ? 2 : 0)
                | (t.field_145990_j != null ? 4 : 0) | (t.field_145991_k != null ? 8 : 0);
            if (b.length() > 0) b.append(',');
            b.append(t.field_145851_c).append(',').append(t.field_145848_d).append(',').append(t.field_145849_e)
                .append(',').append(t.field_145984_a ? 1 : 0).append(',').append(adj)
                .append(',').append(intField(t, "field_145983_q")).append(',').append(t.field_145987_o)
                .append(',').append(Float.floatToRawIntBits(t.field_145989_m))
                .append(',').append(Float.floatToRawIntBits(t.field_145986_n));
        }
        return b.toString();
    }

    /**
     * Per start (the same order as pieces), per component, the flags its
     * generation sets once and data/<Type>.dat keeps: bit 0 a mineshaft
     * corridor's spawnerPlaced, bit 1 hasMadeChest (a village House2, a
     * stronghold ChestCorridor), bit 2 a stronghold PortalRoom's hasSpawner,
     * bits 3-6 a desert pyramid's four chest flags, bits 7-10 a jungle
     * pyramid's main chest, hidden chest and two traps, bit 11 a witch hut's
     * hasWitch.
     */
    static JsonArray pieceFlags(net.minecraft.world.gen.structure.MapGenStructure m)
    {
        JsonArray a = new JsonArray();
        for (Object st : ((java.util.Map)objField(m, "structureMap")).values())
        {
            StringBuilder b = new StringBuilder("ia:");
            int n = 0;
            for (Object c : ((net.minecraft.world.gen.structure.StructureStart)st).getComponents())
            {
                int f = 0;
                if (hasField(c, "spawnerPlaced") && boolField(c, "spawnerPlaced")) f |= 1;
                if (hasField(c, "hasMadeChest") && boolField(c, "hasMadeChest")) f |= 2;
                if (hasField(c, "hasSpawner") && boolField(c, "hasSpawner")) f |= 4;
                if (hasField(c, "field_74940_h"))
                {
                    boolean[] chests = (boolean[])objField(c, "field_74940_h");
                    for (int i = 0; i < 4; ++i) if (chests[i]) f |= 8 << i;
                }
                String[] jungle = {"field_74947_h", "field_74948_i", "field_74945_j", "field_74946_k"};
                for (int i = 0; i < 4; ++i) if (hasField(c, jungle[i]) && boolField(c, jungle[i])) f |= 128 << i;
                if (hasField(c, "hasWitch") && boolField(c, "hasWitch")) f |= 2048;
                if (n++ > 0) b.append(',');
                b.append(f);
            }
            a.add(new JsonPrimitive(b.toString()));
        }
        return a;
    }

    // ------------------------------------------------------------------- ticks

    static int writeTicks(IntegratedServer server, File dir, int[] counts) throws Exception
    {
        return writeTicks(server.worldServers, dir, counts);
    }

    static int writeTicks(WorldServer ws, File dir, int[] counts) throws Exception
    {
        return writeTicks(new WorldServer[] {ws}, dir, counts);
    }

    static int writeTicks(WorldServer[] worlds, File dir, int[] counts) throws Exception
    {
        PrintWriter w = new PrintWriter(new OutputStreamWriter(gzip(new File(dir, "ticks.jsonl.gz")), "UTF-8"));
        int n = 0;

        for (WorldServer ws : worlds)
        {
            if (ws == null) continue;
            for (Object o : (Iterable)objField(ws, "pendingTickListEntriesTreeSet"))
            {
                w.println(tickLine((NextTickListEntry)o, "pending", ws.provider.dimensionId));
                ++n;
            }

            for (Object o : (Iterable)objField(ws, "pendingTickListEntriesThisTick"))
            {
                w.println(tickLine((NextTickListEntry)o, "thisTick", ws.provider.dimensionId));
                ++counts[3];
            }
        }

        w.close();
        return n;
    }

    static String tickLine(NextTickListEntry e, String list, int dim)
    {
        JsonObject o = new JsonObject();
        if (dim != Integer.MIN_VALUE) o.addProperty("dim", dim);
        o.addProperty("list", list);
        o.addProperty("x", e.xCoord);
        o.addProperty("y", e.yCoord);
        o.addProperty("z", e.zCoord);
        o.addProperty("id", net.minecraft.block.Block.getIdFromBlock(e.func_151351_a()));
        o.addProperty("t", "l:" + e.scheduledTime);
        o.addProperty("pri", e.priority);
        o.addProperty("eid", "l:" + longField(e, "tickEntryID"));
        return canonText(o);
    }

    static String tickLine(NextTickListEntry e, String list)
    {
        return tickLine(e, list, Integer.MIN_VALUE);
    }

    /**
     * The saved pending ticks of every chunk that is not loaded, copied out of
     * the region files. When one of those chunks loads again the server merges
     * exactly these entries back into the pending set (AnvilChunkLoader's
     * readChunkFromNBT TileTicks pass), so a replay that starts from this
     * snapshot needs them verbatim: their delays are relative to the chunk's
     * own last save, which no live state carries. The chunks: first those the
     * pending set points at (each entry sits in at most two chunk boxes on
     * each axis, the save box being [cx*16-2, cx*16+16)), then every other
     * chunk the region headers hold, region files in name order, slots in
     * order.
     */
    static int writeSeedTicks(IntegratedServer server, File dir) throws Exception
    {
        PrintWriter w = new PrintWriter(new OutputStreamWriter(gzip(new File(dir, "seedticks.jsonl.gz")), "UTF-8"));
        PrintWriter we = new PrintWriter(new OutputStreamWriter(gzip(new File(dir, "seedents.jsonl.gz")), "UTF-8"));
        int n = 0;

        for (WorldServer ws : server.worldServers)
        {
            if (ws == null) continue;

            Object loader = objField(ws.theChunkProviderServer, "currentChunkLoader");
            if (loader == null) continue;
            File saveLocation = (File)objField(loader, "chunkSaveLocation");

            Set loaded = new java.util.HashSet();
            for (Object o : ws.theChunkProviderServer.func_152380_a())
            {
                Chunk c = (Chunk)o;
                loaded.add(c.xPosition + "," + c.zPosition);
            }

            // the candidate chunks: every chunk box the pending entries can
            // fall into, loaded ones excluded
            Set candidates = new java.util.LinkedHashSet();
            for (Object o : (Iterable)objField(ws, "pendingTickListEntriesTreeSet"))
            {
                NextTickListEntry e = (NextTickListEntry)o;
                for (int cx = floorDiv(e.xCoord - 15, 16); cx <= floorDiv(e.xCoord + 2, 16); ++cx)
                {
                    for (int cz = floorDiv(e.zCoord - 15, 16); cz <= floorDiv(e.zCoord + 2, 16); ++cz)
                    {
                        if (!loaded.contains(cx + "," + cz)) candidates.add(cx + "," + cz);
                    }
                }
            }

            // and every other chunk the region files hold: a due entry whose
            // chunk is unloaded leaves the pending set for good (tickUpdates'
            // delay-0 reschedule fails its own chunk check), so a chunk saved
            // with ticks (the spawn area's, saved at the join) can have no
            // entry near it left and still re-add its TileTicks at its load
            File[] regions = new File(saveLocation, "region").listFiles();
            if (regions != null)
            {
                Arrays.sort(regions);
                for (File f : regions)
                {
                    String name = f.getName();
                    if (!name.startsWith("r.") || !name.endsWith(".mca")) continue;
                    String[] parts = name.substring(2, name.length() - 4).split("\\.");
                    if (parts.length != 2) continue;
                    int rx, rz;
                    try
                    {
                        rx = Integer.parseInt(parts[0]);
                        rz = Integer.parseInt(parts[1]);
                    }
                    catch (NumberFormatException ex)
                    {
                        continue;
                    }
                    byte[] header = new byte[4096];
                    java.io.RandomAccessFile raf = new java.io.RandomAccessFile(f, "r");
                    int got;
                    try
                    {
                        got = raf.read(header);
                    }
                    finally
                    {
                        raf.close();
                    }
                    for (int slot = 0; slot < 1024 && 4 * slot + 3 < got; ++slot)
                    {
                        if (header[4 * slot] == 0 && header[4 * slot + 1] == 0 && header[4 * slot + 2] == 0) continue;
                        int cx = rx * 32 + (slot & 31), cz = rz * 32 + (slot >> 5);
                        if (!loaded.contains(cx + "," + cz)) candidates.add(cx + "," + cz);
                    }
                }
            }

            for (Object o : candidates)
            {
                String[] parts = ((String)o).split(",");
                int cx = Integer.parseInt(parts[0]);
                int cz = Integer.parseInt(parts[1]);
                DataInputStream in = net.minecraft.world.chunk.storage.RegionFileCache.getChunkInputStream(saveLocation, cx, cz);
                if (in == null) continue;
                NBTTagCompound root = net.minecraft.nbt.CompressedStreamTools.read(in);
                in.close();
                if (!root.hasKey("Level")) continue;
                NBTTagCompound level = root.getCompoundTag("Level");
                if (ws.provider.dimensionId == 0)
                {
                    // the overworld chunk's saved Entities, read back at its
                    // next load (readChunkFromNBT): the native rebuilds the
                    // seed world's own creatures, not what the first ticks
                    // put in a chunk the join's unload drain saved (squids,
                    // items; lane/seedgen)
                    JsonObject ce = new JsonObject();
                    ce.addProperty("cx", cx);
                    ce.addProperty("cz", cz);
                    JsonArray ea = new JsonArray();
                    NBTTagList el = level.getTagList("Entities", 10);
                    for (int ei = 0; ei < el.tagCount(); ++ei)
                        ea.add(StructuresProbe.canon(el.getCompoundTagAt(ei)));
                    ce.add("ents", ea);
                    we.println(canonText(ce));
                }
                if (!level.hasKey("TileTicks")) continue;
                NBTTagList ticks = level.getTagList("TileTicks", 10);
                for (int i = 0; i < ticks.tagCount(); ++i)
                {
                    NBTTagCompound t = ticks.getCompoundTagAt(i);
                    JsonObject line = new JsonObject();
                    line.addProperty("dim", ws.provider.dimensionId);
                    line.addProperty("cx", cx);
                    line.addProperty("cz", cz);
                    line.addProperty("x", t.getInteger("x"));
                    line.addProperty("y", t.getInteger("y"));
                    line.addProperty("z", t.getInteger("z"));
                    line.addProperty("id", t.getInteger("i"));
                    line.addProperty("t", "l:" + t.getInteger("t"));
                    line.addProperty("pri", t.getInteger("p"));
                    w.println(canonText(line));
                    ++n;
                }
            }
        }

        w.close();
        we.close();
        return n;
    }

    static int floorDiv(int a, int b)
    {
        return a < 0 ? -((-a + b - 1) / b) : a / b;
    }

    // ------------------------------------------------------------------ world

    /**
     * The client world's own chunk set, which is not the server's: the client
     * player's physics runs against it, so the native replay needs to know which
     * chunks the client has and whether their bytes match the server's. Nothing
     * here loads a chunk: the server side is read through chunkExists.
     */
    static String clientWorld(Minecraft mc, WorldServer ws)
    {
        JsonObject o = new JsonObject();
        Object prov = mc.theWorld.getChunkProvider();
        List listing = (List)objField(prov, "chunkListing");
        // the listing is the map's own values in load order; the count is the
        // authority, so both are recorded
        int hashElements = (int)(Integer)invoke(objField(prov, "chunkMapping"), "getNumHashElements");
        JsonArray coords = new JsonArray();
        int same = 0, differ = 0, clientOnly = 0, blank = 0;
        byte[] a = new byte[Probe.CHUNK_BYTES], b = new byte[Probe.CHUNK_BYTES];
        int[] region = new int[REGION_NAMES.length];

        for (Object x : listing)
        {
            Chunk c = (Chunk)x;
            JsonArray p = new JsonArray();
            p.add(new JsonPrimitive("i:" + c.xPosition));
            p.add(new JsonPrimitive("i:" + c.zPosition));
            coords.add(p);

            if (c instanceof net.minecraft.world.chunk.EmptyChunk) ++blank;
            else if (!ws.getChunkProvider().chunkExists(c.xPosition, c.zPosition)) ++clientOnly;
            else
            {
                Probe.fillChunkBytes(c, a);
                Probe.fillChunkBytes(ws.getChunkFromChunkCoords(c.xPosition, c.zPosition), b);
                if (Arrays.equals(a, b)) ++same;
                else ++differ;

                // which part of the layout differs: the packet does not carry
                // every array, so this is where the two worlds part ways
                for (int r = 0; r < REGION_NAMES.length; ++r)
                    if (!same(a, b, REGION_OFF[r], REGION_LEN[r])) ++region[r];
            }
        }

        o.addProperty("chunks", "i:" + listing.size());
        o.addProperty("hashElements", "i:" + hashElements);
        // WorldClient.func_147456_g's state: previousActiveChunkSet (the
        // active chunks whose relight checks ran since the set last moved)
        // and activeChunkSet's table length (clear() keeps the largest)
        StringBuilder prev = new StringBuilder("ia:");
        int np = 0;
        for (Object x : (Iterable)objField(mc.theWorld, "previousActiveChunkSet"))
        {
            net.minecraft.world.ChunkCoordIntPair p = (net.minecraft.world.ChunkCoordIntPair)x;
            if (np++ > 0) prev.append(',');
            prev.append(p.chunkXPos).append(',').append(p.chunkZPos);
        }
        o.addProperty("prevActive", prev.toString());
        Object[] activeTable = (Object[])objField(objField(objField(mc.theWorld, "activeChunkSet"), "map"), "table");
        o.addProperty("activeCap", "i:" + (activeTable == null ? 0 : activeTable.length));
        o.addProperty("empty", "i:" + blank);
        o.addProperty("sameAsServer", "i:" + same);
        o.addProperty("differFromServer", "i:" + differ);
        o.addProperty("clientOnly", "i:" + clientOnly);

        for (int r = 0; r < REGION_NAMES.length; ++r)
            o.addProperty("differ" + REGION_NAMES[r], "i:" + region[r]);
        o.add("loaded", coords);
        return canonText(o);
    }

    /** The eight regions of the Probe chunk byte layout, in their order. */
    static final String[] REGION_NAMES = {"Ids", "Metas", "Sky", "Blocklight", "HeightMap", "Precip", "HeightMin", "Mask"};
    static final int[] REGION_OFF = {0, 2 * 65536, 3 * 65536, 4 * 65536, 5 * 65536, 5 * 65536 + 1024, 5 * 65536 + 2048, 5 * 65536 + 2052};
    static final int[] REGION_LEN = {2 * 65536, 65536, 65536, 65536, 1024, 1024, 4, 2};

    static boolean same(byte[] a, byte[] b, int off, int len)
    {
        for (int i = 0; i < len; ++i) if (a[off + i] != b[off + i]) return false;
        return true;
    }

    static String worldState(WorldServer ws)
    {
        JsonObject o = new JsonObject();
        o.addProperty("dim", "i:" + ws.provider.dimensionId);
        o.addProperty("rand", "l:" + Det.state(ws.rand));
        o.addProperty("updateLCG", "i:" + intField(ws, "updateLCG"));
        o.addProperty("blkHash", "l:" + Rows.blkHash);
        o.addProperty("skylightSubtracted", "i:" + ws.skylightSubtracted);
        o.addProperty("prevRainingStrength", "f:" + hex(Float.floatToRawIntBits(floatField(ws, "prevRainingStrength")) & 4294967295L, 8));
        o.addProperty("rainingStrength", "f:" + hex(Float.floatToRawIntBits(floatField(ws, "rainingStrength")) & 4294967295L, 8));
        o.addProperty("prevThunderingStrength", "f:" + hex(Float.floatToRawIntBits(floatField(ws, "prevThunderingStrength")) & 4294967295L, 8));
        o.addProperty("thunderingStrength", "f:" + hex(Float.floatToRawIntBits(floatField(ws, "thunderingStrength")) & 4294967295L, 8));
        o.addProperty("updateEntityTick", "i:" + intField(ws, "updateEntityTick"));
        o.addProperty("allPlayersSleeping", "b:" + (boolField(ws, "allPlayersSleeping") ? 1 : 0));
        o.addProperty("blockEventTick", "i:" + intField(ws, "field_147489_T"));
        o.addProperty("nextTickEntryID", "l:" + longField(null, "nextTickEntryID", NextTickListEntry.class));
        // each biome object's WorldGenBigTree keeps the heightLimit its first
        // generate drew (a failed validTreeLocation shortens it) for the
        // JVM's life, so the next big tree of that biome starts from it
        StringBuilder bt = new StringBuilder("ia:");
        net.minecraft.world.biome.BiomeGenBase[] biomes = net.minecraft.world.biome.BiomeGenBase.getBiomeGenArray();
        for (int i = 0; i < biomes.length; ++i)
        {
            Object gen = biomes[i] == null ? null : objField(biomes[i], "worldGeneratorBigTree");
            bt.append(i > 0 ? "," : "").append(gen == null ? 0 : intField(gen, "heightLimit"));
        }
        o.addProperty("bigTree", bt.toString());
        // the block singletons whose shared bounds getCollisionBoundingBoxFromPool
        // reports: the fences (the last fence a collision or a ray trace
        // saw) and the stairs (full, or an octant after a ray trace): per
        // block its id and minX minY minZ maxX maxY maxZ as float bits; the
        // brewing stand (full, or its base after a collision query); the
        // ladder (the last ladder a meta 2..5 query saw); the end portal
        // frame (full, or its 13/16 base after a collision query) and the
        // end portal (full, or 1/16 after a ray trace)
        StringBuilder bb = new StringBuilder("ia:");
        int[] shared = {85, 113, 53, 67, 108, 109, 114, 128, 134, 135, 136, 156, 163, 164, 117, 65, 120, 119};
        for (int i = 0; i < shared.length; ++i)
        {
            net.minecraft.block.Block b = net.minecraft.block.Block.getBlockById(shared[i]);
            double[] v = {b.getBlockBoundsMinX(), b.getBlockBoundsMinY(), b.getBlockBoundsMinZ(),
                b.getBlockBoundsMaxX(), b.getBlockBoundsMaxY(), b.getBlockBoundsMaxZ()};
            bb.append(i > 0 ? "," : "").append(shared[i]);
            for (double d : v) bb.append(',').append(Float.floatToRawIntBits((float)d));
        }
        o.addProperty("blockBounds", bb.toString());
        // BlockLeaves.field_150128_a, the decay search array each leaf block
        // object (Blocks.leaves, Blocks.leaves2) keeps: its centre cell, which
        // updateTick reads without a search when a chunk within 5 is missing
        // (the rest is rewritten before it is read); 0 before the first search
        StringBuilder ld = new StringBuilder("ia:");
        net.minecraft.block.Block[] leaves = {net.minecraft.init.Blocks.leaves, net.minecraft.init.Blocks.leaves2};
        for (int i = 0; i < leaves.length; ++i)
        {
            int[] a = (int[])objField(leaves[i], "field_150128_a");
            ld.append(i > 0 ? "," : "").append(a == null ? 0 : a[16 * 1024 + 16 * 32 + 16]);
        }
        o.addProperty("leafDecay", ld.toString());
        return canonText(o);
    }

    /**
     * Every world server's own scalar state, the whole-server tape replay's
     * start: worldstate.nbt keys, plus the state WorldServer.tick reads that
     * worldstate.nbt (the overworld's file) does not carry for the other
     * dimensions: the ambient countdown, the difficulty, the view distance, the
     * loaded chunk and pending tick counts, the active chunk set in the
     * HashSet's own iteration order, and the pending ticks themselves.
     */
    static String worldsState(IntegratedServer server) throws Exception
    {
        JsonObject o = new JsonObject();
        JsonArray order = new JsonArray();

        for (int i = 0; i < server.worldServers.length; ++i)
        {
            WorldServer ws = server.worldServers[i];
            if (ws == null) continue;
            order.add(new JsonPrimitive("i:" + ws.provider.dimensionId));
        }

        o.add("order", order);

        for (int i = 0; i < server.worldServers.length; ++i)
        {
            WorldServer ws = server.worldServers[i];
            if (ws == null) continue;
            JsonObject w = new JsonObject();
            w.addProperty("dim", "i:" + ws.provider.dimensionId);
            w.addProperty("rand", "l:" + Det.state(ws.rand));
            w.addProperty("updateLCG", "i:" + intField(ws, "updateLCG"));
            w.addProperty("blkHash", "l:" + Rows.blkHash);
            w.addProperty("skylightSubtracted", "i:" + ws.skylightSubtracted);
            w.addProperty("prevRainingStrength", fbits(floatField(ws, "prevRainingStrength")));
            w.addProperty("rainingStrength", fbits(floatField(ws, "rainingStrength")));
            w.addProperty("prevThunderingStrength", fbits(floatField(ws, "prevThunderingStrength")));
            w.addProperty("thunderingStrength", fbits(floatField(ws, "thunderingStrength")));
            w.addProperty("updateEntityTick", "i:" + intField(ws, "updateEntityTick"));
            w.addProperty("allPlayersSleeping", "b:" + (boolField(ws, "allPlayersSleeping") ? 1 : 0));
            w.addProperty("blockEventTick", "i:" + intField(ws, "field_147489_T"));
            w.addProperty("nextTickEntryID", "l:" + longField(null, "nextTickEntryID", NextTickListEntry.class));
            w.addProperty("ambientTickCountdown", "i:" + intField(ws, "ambientTickCountdown"));
            /* WorldServer.worldTeleporter: its Random and the portal
             * positions it cached (destinationCoordinateKeys order), which a
             * trip back through the same portal reuses */
            Object tp = objField(ws, "worldTeleporter");
            w.addProperty("tpRand", "l:" + Det.state((java.util.Random)objField(tp, "random")));
            JsonArray tc = new JsonArray();
            net.minecraft.util.LongHashMap cache = (net.minecraft.util.LongHashMap)objField(tp, "destinationCoordinateCache");
            for (Object k : (java.util.List)objField(tp, "destinationCoordinateKeys"))
            {
                long key = ((Long)k).longValue();
                net.minecraft.world.Teleporter.PortalPosition pp = (net.minecraft.world.Teleporter.PortalPosition)cache.getValueByKey(key);
                if (pp == null) continue;
                JsonObject e = new JsonObject();
                e.addProperty("k", "l:" + key);
                e.addProperty("x", "i:" + pp.posX);
                e.addProperty("y", "i:" + pp.posY);
                e.addProperty("z", "i:" + pp.posZ);
                e.addProperty("t", "l:" + pp.lastUpdateTime);
                tc.add(e);
            }
            w.add("tpCache", tc);
            /* WorldServer.villageSiegeObj (VillageSiege): its four fields are
             * runtime-only, so a snapshot's replay cannot derive them. The
             * dusk window's one World.rand draw and the village lookup hang
             * off field_75536_c. */
            Object siege = objField(ws, "villageSiegeObj");
            w.addProperty("siegeC", "i:" + intField(siege, "field_75536_c"));
            w.addProperty("siegeB", "b:" + (boolField(siege, "field_75535_b") ? 1 : 0));
            w.addProperty("siegeD", "i:" + intField(siege, "field_75533_d"));
            w.addProperty("siegeE", "i:" + intField(siege, "field_75534_e"));
            w.addProperty("difficulty", "i:" + ws.difficultySetting.getDifficultyId());
            w.addProperty("viewDistance", "i:" + (Integer)invoke(server.getConfigurationManager(), "getViewDistance"));
            w.addProperty("time", "l:" + ws.getWorldInfo().getWorldTime());
            w.addProperty("total", "l:" + ws.getWorldInfo().getWorldTotalTime());
            w.addProperty("raining", "b:" + (ws.getWorldInfo().isRaining() ? 1 : 0));
            w.addProperty("thundering", "b:" + (ws.getWorldInfo().isThundering() ? 1 : 0));
            w.addProperty("rainTime", "i:" + ws.getWorldInfo().getRainTime());
            w.addProperty("thunderTime", "i:" + ws.getWorldInfo().getThunderTime());
            w.addProperty("noChunkGeneration", "b:" + (Snapshot.boolField(Snapshot.objField(ws, "theChunkProviderServer"), "loadChunkOnProvideRequest") ? 0 : 1));
            w.addProperty("chunks", "i:" + chunkCount(ws));
            StringBuilder ai = new StringBuilder("ia:");
            int n = 0;

            for (Object x : (Iterable)objField(ws, "activeChunkSet"))
            {
                net.minecraft.world.ChunkCoordIntPair p = (net.minecraft.world.ChunkCoordIntPair)x;
                if (n++ > 0) ai.append(',');
                ai.append(p.chunkXPos).append(',').append(p.chunkZPos);
            }

            w.addProperty("active", n == 0 ? "ia:" : ai.toString());
            // World.loadedTileEntityList (field_147482_g) in its order, as
            // x,y,z per tile entity: the chunk loads' HashMap order the tick
            // walks
            StringBuilder to = new StringBuilder("ia:");
            int tn = 0;
            for (Object x : ws.field_147482_g)
            {
                net.minecraft.tileentity.TileEntity te = (net.minecraft.tileentity.TileEntity)x;
                if (tn++ > 0) to.append(',');
                to.append(te.field_145851_c).append(',').append(te.field_145848_d).append(',').append(te.field_145849_e);
            }
            w.addProperty("teOrder", to.toString());
            StringBuilder unload = new StringBuilder("ia:");
            int nu = 0;
            for (Object key : (Iterable)objField(objField(ws, "theChunkProviderServer"), "chunksToUnload"))
            {
                long packed = ((Long)key).longValue();
                if (nu++ > 0) unload.append(',');
                unload.append((int)packed).append(',').append((int)(packed >>> 32));
            }
            w.addProperty("unload", unload.toString());
            // the ConcurrentHashMap's table length under chunksToUnload (0 before
            // its first put): the table only grows, and the bins it gives the
            // keys, not their count, are the set's iteration order
            Object[] table = (Object[])objField(objField(objField(objField(ws, "theChunkProviderServer"), "chunksToUnload"), "m"), "table");
            w.addProperty("unloadBins", "i:" + (table == null ? 0 : table.length));
            // null when no overworld chunk has unloaded yet (a join in the
            // Nether): the key is left out, as the native reader allows
            String unloadClock = netherite.oracle.UnloadClock.dump();
            if (unloadClock != null) w.addProperty("unloadClock", unloadClock);
            /* PlayerManager's inhabitedTime clock: previousTotalWorldTime (the
             * 8,000-tick processChunk sweep's mark) and, per instance in
             * playerInstanceList order, cx, cz and the ticks since its
             * previousWorldTime (increaseInhabitedTime's mark) */
            Object pm = ws.getPlayerManager();
            long total = ws.getTotalWorldTime();
            w.addProperty("pmSweep", "l:" + longField(pm, "previousTotalWorldTime"));
            StringBuilder inst = new StringBuilder("ia:");
            int ni = 0;
            for (Object pi : (List)objField(pm, "playerInstanceList"))
            {
                net.minecraft.world.ChunkCoordIntPair at = (net.minecraft.world.ChunkCoordIntPair)objField(pi, "chunkLocation");
                if (ni++ > 0) inst.append(',');
                inst.append(at.chunkXPos).append(',').append(at.chunkZPos).append(',')
                    .append((int)(total - longField(pi, "previousWorldTime")));
            }
            w.addProperty("pmInst", inst.toString());

            int npend = 0, nthis = 0;

            for (Object x : (Iterable)objField(ws, "pendingTickListEntriesTreeSet")) ++npend;

            for (Object x : (Iterable)objField(ws, "pendingTickListEntriesThisTick")) ++nthis;

            w.addProperty("pending", "i:" + npend);
            w.addProperty("thisTick", "i:" + nthis);
            o.add("w" + i, w);
        }

        return canonText(o);
    }

    static int chunkCount(WorldServer ws) throws Exception
    {
        return ((List)objField(objField(ws, "theChunkProviderServer"), "loadedChunks")).size();
    }

    static String fbits(float f)
    {
        return "f:" + hex(Float.floatToRawIntBits(f) & 4294967295L, 8);
    }

    /**
     * What a whole-server tape replay needs that the files imply but the reader
     * should not have to infer: the per-world flags, the parked state tick 0
     * starts from, and whether the snapshot holds only players (a mob-free tape,
     * where the native entity digest over the entities native has can be exact).
     */
    static JsonObject serverManifest(IntegratedServer server) throws Exception
    {
        JsonObject s = new JsonObject();
        JsonArray dims = new JsonArray();
        JsonArray chunks = new JsonArray();
        JsonArray pending = new JsonArray();
        JsonArray noGen = new JsonArray();
        boolean onlyPlayers = true;

        for (WorldServer ws : server.worldServers)
        {
            if (ws == null) continue;
            dims.add(new JsonPrimitive("i:" + ws.provider.dimensionId));
            chunks.add(new JsonPrimitive("i:" + chunkCount(ws)));
            pending.add(new JsonPrimitive("i:" + (((java.util.TreeSet)objField(ws, "pendingTickListEntriesTreeSet")).size() + ((List)objField(ws, "pendingTickListEntriesThisTick")).size())));
            noGen.add(new JsonPrimitive("b:" + (boolField(objField(ws, "theChunkProviderServer"), "loadChunkOnProvideRequest") ? 0 : 1)));

            for (Object o : ws.loadedEntityList)
                if (!((Entity)o instanceof EntityPlayerMP)) onlyPlayers = false;
        }

        s.add("dims", dims);
        s.add("chunksPerWorld", chunks);
        s.add("pendingPerWorld", pending);
        s.add("noChunkGeneration", noGen);
        s.addProperty("onlyPlayers", onlyPlayers ? 1 : 0);
        WorldServer over = server.worldServers[0];
        Calendar calendar = over.getCurrentDate();
        s.addProperty("calMonth", calendar.get(Calendar.MONTH) + 1);
        s.addProperty("calDay", calendar.get(Calendar.DAY_OF_MONTH));
        EntityPlayerMP p = over.playerEntities.isEmpty() ? null : (EntityPlayerMP)over.playerEntities.get(0);
        s.addProperty("playerX", p == null ? "d:0000000000000000" : "d:" + hex(Double.doubleToRawLongBits(p.posX), 16));
        s.addProperty("playerY", p == null ? "d:0000000000000000" : "d:" + hex(Double.doubleToRawLongBits(p.posY), 16));
        s.addProperty("playerZ", p == null ? "d:0000000000000000" : "d:" + hex(Double.doubleToRawLongBits(p.posZ), 16));
        // PlayerManager's square is centred on managedPos, which only follows the
        // player once it has moved 8 blocks: a short teleport leaves it behind
        s.addProperty("managedX", p == null ? "d:0000000000000000" : "d:" + hex(Double.doubleToRawLongBits(p.managedPosX), 16));
        s.addProperty("managedZ", p == null ? "d:0000000000000000" : "d:" + hex(Double.doubleToRawLongBits(p.managedPosZ), 16));
        return s;
    }

    /** Every Det stream and counter, one entry per role. Splits are sorted by name. */
    static String detState()
    {
        JsonObject o = new JsonObject();
        o.addProperty("otherDraws", "l:" + Det.otherDraws[0]);
        o.addProperty("worldSeed", "l:" + Det.worldSeed);
        o.addProperty("shuf", "l:" + Det.shufState());
        JsonArray ids = new JsonArray();
        for (int r = 0; r < Det.ROLES; ++r) ids.add(new JsonPrimitive("i:" + Det.nextId[r]));
        o.add("nextId", ids);
        o.add("seeder", states(Det.seeder));
        o.add("math", states(Det.math));

        List<Det.SplitRandom> splits;
        synchronized (Det.class)
        {
            splits = new ArrayList<Det.SplitRandom>(Det.splits);
        }
        Collections.sort(splits, new Comparator<Det.SplitRandom>()
        {
            public int compare(Det.SplitRandom a, Det.SplitRandom b) { return a.name.compareTo(b.name); }
        });

        JsonArray sa = new JsonArray();
        for (Det.SplitRandom s : splits)
        {
            if (Pool.absentSplit(s)) continue;
            JsonObject e = new JsonObject();
            e.addProperty("name", "str:" + s.name);
            JsonArray used = new JsonArray();
            JsonArray st = new JsonArray();
            for (int r = 0; r < Det.ROLES; ++r)
            {
                used.add(new JsonPrimitive("b:" + (s.used[r] ? 1 : 0)));
                st.add(new JsonPrimitive("l:" + Det.state(s.d[r])));
            }
            e.add("used", used);
            e.add("state", st);
            sa.add(e);
        }
        o.add("splits", sa);

        // the block singletons' own Randoms (furnace, chest, dispenser,
        // hopper, brewing stand), born at bootstrap: breakBlock's spill draws them
        BlockRands.write(o);
        return canonText(o);
    }

    static JsonArray states(java.util.Random[] a)
    {
        JsonArray out = new JsonArray();
        for (int r = 0; r < Det.ROLES; ++r) out.add(new JsonPrimitive("l:" + Det.state(a[r])));
        return out;
    }

    // ----------------------------------------------------------------- players

    /**
     * The client player: its NBT (pos, motion, rotation, health, food, the
     * inventory, experience) plus the fields the next tick reads that NBT does
     * not carry: prevPos, prevRotation, onGround, the sprint toggle timer, the
     * jump ticks, fall distance, ySize, the movement input, and
     * EntityClientPlayerMP's move-packet bookkeeping.
     */
    static String clientPlayer(Minecraft mc)
    {
        EntityClientPlayerMP p = mc.thePlayer;
        JsonObject o = new JsonObject();
        NBTTagCompound tag = new NBTTagCompound();
        p.writeToNBT(tag);
        o.add("nbt", StructuresProbe.canon(tag));

        JsonObject f = new JsonObject();
        f.addProperty("dimension", "i:" + p.dimension);
        f.addProperty("ticksExisted", "i:" + p.ticksExisted);
        fields(f, p, "prevPosX", "prevPosY", "prevPosZ", "posX", "posY", "posZ",
            "motionX", "motionY", "motionZ", "prevRotationYaw", "prevRotationPitch",
            "rotationYaw", "rotationPitch", "onGround", "fallDistance", "ySize", "stepHeight");
        fields(f, p, "sprintToggleTimer", "sprintingTicksLeft", "timeInPortal", "prevTimeInPortal");
        fields(f, p, "oldPosX", "oldMinY", "oldPosY", "oldPosZ", "oldRotationYaw", "oldRotationPitch",
            "wasOnGround", "shouldStopSneaking", "wasSneaking", "ticksSinceMovePacket", "hasSetHealth");
        fields(f, p, "flyToggleTimer");
        // S06s the server's last tick sent that the client's next pump reads
        // (a from-run whose player is hurt: the join's first update)
        JsonArray s06 = new JsonArray();
        for (Object pk : (java.util.Queue)objField(mc.getNetHandler().getNetworkManager(), "receivedPacketsQueue"))
        {
            if (!(pk instanceof net.minecraft.network.play.server.S06PacketUpdateHealth)) continue;
            net.minecraft.network.play.server.S06PacketUpdateHealth h = (net.minecraft.network.play.server.S06PacketUpdateHealth)pk;
            JsonArray a = new JsonArray();
            a.add(new JsonPrimitive(fhex(h.func_149332_c())));
            a.add(new JsonPrimitive("i:" + h.func_149330_d()));
            a.add(new JsonPrimitive(fhex(h.func_149331_e())));
            s06.add(a);
        }
        if (s06.size() > 0) f.add("pendingS06", s06);
        // every packet the server's last tick sent that the client's next
        // pump reads, in queue order: the class and its fields (EntityState)
        JsonArray pend = new JsonArray();
        for (Object pk : (java.util.Queue)objField(mc.getNetHandler().getNetworkManager(), "receivedPacketsQueue"))
        {
            JsonObject po = EntityState.fields(pk, Object.class);
            po.addProperty("cls", "str:" + pk.getClass().getSimpleName());
            packetStacks(po, pk);
            pend.add(po);
        }
        o.add("pending", pend);
        f.addProperty("currentItem", "i:" + p.inventory.currentItem);
        // the client player's own Random (ItemStack.damageItem's Unbreaking
        // roll on the client draws it)
        f.addProperty("entityRand", "l:" + Det.state((java.util.Random)objField(p, "rand")));
        itemInUse(f, p);
        o.add("fields", f);

        JsonObject in = new JsonObject();
        MovementInput in0 = p.movementInput;
        fields(in, in0, "moveStrafe", "moveForward", "jump", "sneak");
        o.add("input", in);

        JsonObject ab = new JsonObject();
        fields(ab, p.capabilities, "isFlying", "allowFlying", "isCreativeMode", "disableDamage", "flySpeed", "walkSpeed");
        o.add("abilities", ab);
        // the whole runtime (EntityState): the data watcher's flags, the
        // attribute values, the jump and swing counters
        o.add("rt", EntityState.of(p));
        // PlayerControllerMP: the dig in progress and the hotbar slot last
        // sent (currentPlayerItem)
        o.add("ctl", EntityState.fields(mc.playerController, Object.class));
        // the open screen and the client's own copy of its window
        o.add("gui", guiState(p.openContainer, p.inventory.getItemStack(), mc));
        return canonText(o);
    }

    static String serverPlayer(IntegratedServer server)
    {
        List players = server.getConfigurationManager().playerEntityList;
        if (players.isEmpty()) throw new IllegalStateException("Snapshot: no server player");
        EntityPlayerMP p = (EntityPlayerMP)players.get(0);
        JsonObject o = new JsonObject();
        NBTTagCompound tag = new NBTTagCompound();
        p.writeToNBT(tag);
        o.add("nbt", StructuresProbe.canon(tag));

        JsonObject f = new JsonObject();
        f.addProperty("dimension", "i:" + p.dimension);
        f.addProperty("ticksExisted", "i:" + p.ticksExisted);
        fields(f, p, "prevPosX", "prevPosY", "prevPosZ", "posX", "posY", "posZ", "motionX", "motionY", "motionZ",
            "rotationYaw", "rotationPitch", "onGround", "fallDistance", "ySize",
            "lastExperience", "experienceLevel", "experienceTotal", "flyToggleTimer");
        fields(f, p, "hurtResistantTime", "lastDamage", "recentlyHit", "xpCooldown", "field_147101_bU");
        fields(f, p, "lastHealth", "lastFoodLevel", "wasHungry");
        fields(f, p, "nextStepDistance", "distanceWalkedOnStepModified");
        f.addProperty("currentItem", "i:" + p.inventory.currentItem);
        f.addProperty("entityRand", "l:" + Det.state((java.util.Random)objField(p, "rand")));
        itemInUse(f, p);
        // EntityPlayerMP.loadedChunks: the chunks queued to send, in order
        StringBuilder sq = new StringBuilder("ia:");
        int nq = 0;
        for (Object q : p.loadedChunks)
        {
            net.minecraft.world.ChunkCoordIntPair c = (net.minecraft.world.ChunkCoordIntPair)q;
            if (nq++ > 0) sq.append(',');
            sq.append(c.chunkXPos).append(',').append(c.chunkZPos);
        }
        f.addProperty("sendQueue", sq.toString());
        o.add("fields", f);

        // the handler's keep-alive counter: processPlayer's % 20 correction runs
        // while hasMoved is false (the respawn window), so its phase is state
        JsonObject net = new JsonObject();
        fields(net, p.playerNetServerHandler, "networkTickCount", "hasMoved", "floatingTickCount");
        o.add("net", net);
        o.add("rt", EntityState.of(p));
        o.add("netrt", EntityState.fields(p.playerNetServerHandler, Object.class));
        // Container.inventoryItemStacks, the last stacks detectAndSendChanges
        // sent, of the player's own container and of an open window
        // ItemInWorldManager: the server's dig in progress
        o.add("iiw", EntityState.fields(p.theItemInWorldManager, Object.class));
        o.add("mirror", containerMirror(p.inventoryContainer));
        // the open window: its id, kind, tiles, slots, cursor, progress
        o.add("gui", guiState(p.openContainer, p.inventory.getItemStack(), null));
        if (p.openContainer != null && p.openContainer != p.inventoryContainer)
            o.add("openMirror", containerMirror(p.openContainer));
        return canonText(o);
    }

    /* A pending packet's item stacks as canonical NBT ("<field>.nbt": a
     * compound, or a list with {} for an empty slot), and an S20's sprint
     * boost: whether its movementSpeed snapshot carries the sprinting
     * modifier ("sprintMod"), and its speed and slowness potions'
     * amplifiers ("speedAmp", "slowAmp", only when it carries them) */
    static void packetStacks(JsonObject po, Object pk)
    {
        for (Class<?> k = pk.getClass(); k != null && k != Object.class; k = k.getSuperclass())
        {
            for (Field f : k.getDeclaredFields())
            {
                if (java.lang.reflect.Modifier.isStatic(f.getModifiers())) continue;
                f.setAccessible(true);
                Object v;
                try { v = f.get(pk); } catch (Exception e) { throw new IllegalStateException(e); }
                if (f.getType() == ItemStack.class && v != null)
                {
                    NBTTagCompound t = new NBTTagCompound();
                    ((ItemStack)v).writeToNBT(t);
                    po.add(f.getName() + ".nbt", StructuresProbe.canon(t));
                }
                else if (f.getType() == ItemStack[].class && v != null)
                {
                    JsonArray a = new JsonArray();
                    for (ItemStack st : (ItemStack[])v)
                    {
                        NBTTagCompound t = new NBTTagCompound();
                        if (st != null) st.writeToNBT(t);
                        a.add(StructuresProbe.canon(t));
                    }
                    po.add(f.getName() + ".nbt", a);
                }
            }
        }
        // the data watcher's byte 16 a spawn or a metadata packet carries
        Object dwl = null;
        if (pk instanceof net.minecraft.network.play.server.S1CPacketEntityMetadata) dwl = objField(pk, "field_149378_b");
        if (pk instanceof net.minecraft.network.play.server.S0FPacketSpawnMob)
        {
            dwl = objField(pk, "field_149044_m");
            if (dwl == null)
            {
                net.minecraft.entity.DataWatcher dw = (net.minecraft.entity.DataWatcher)objField(pk, "field_149043_l");
                if (dw != null) dwl = dw.getAllWatched();
            }
        }
        if (dwl != null)
            for (Object wo : (List)dwl)
            {
                net.minecraft.entity.DataWatcher.WatchableObject w = (net.minecraft.entity.DataWatcher.WatchableObject)wo;
                if (w.getDataValueId() == 16 && w.getObject() instanceof Byte) po.addProperty("dw16", "i:" + ((Byte)w.getObject()).intValue());
            }
        if (pk instanceof net.minecraft.network.play.server.S20PacketEntityProperties)
        {
            boolean mod = false;
            for (Object sn : (List)objField(pk, "field_149444_b"))
            {
                if (!"generic.movementSpeed".equals(objField(sn, "field_151412_b"))) continue;
                for (Object m : (java.util.Collection)objField(sn, "field_151411_d"))
                {
                    net.minecraft.entity.ai.attributes.AttributeModifier am = (net.minecraft.entity.ai.attributes.AttributeModifier)m;
                    String id = am.getID().toString();
                    if (id.equalsIgnoreCase("662A6B8D-DA3E-4C1C-8813-96EA6097278D"))
                        mod = true;
                    // the potions' modifiers (Potion.moveSpeed and
                    // moveSlowdown), as their amplifiers, when present
                    else if (id.equalsIgnoreCase("91AEAA56-376B-4498-935B-2F7F68070635"))
                        po.addProperty("speedAmp", "i:" + (Math.round(am.getAmount() / 0.20000000298023224D) - 1));
                    else if (id.equalsIgnoreCase("7107DE5E-7CE8-4030-940E-514C1F160890"))
                        po.addProperty("slowAmp", "i:" + (Math.round(am.getAmount() / -0.15000000596046448D) - 1));
                }
            }
            po.addProperty("sprintMod", "b:" + (mod ? 1 : 0));
        }
    }

    /**
     * A player's open window: the window id and the Container's class, every
     * slot's stack in slot order ({} for an empty one), the cursor (the
     * InventoryPlayer's item stack), a furnace's progress values (the
     * server's last sent, the client's tile fields), the tile entities it
     * holds (x,y,z; a double chest's upper then lower) and, on the client,
     * the screen's class.
     */
    static JsonObject guiState(net.minecraft.inventory.Container c, ItemStack cursor, Minecraft mc)
    {
        JsonObject g = new JsonObject();
        if (mc != null && mc.currentScreen != null) g.addProperty("screen", "str:" + mc.currentScreen.getClass().getSimpleName());
        g.addProperty("window", "i:" + c.windowId);
        g.addProperty("kind", "str:" + c.getClass().getSimpleName());
        JsonArray sl = new JsonArray();
        for (int i = 0; i < c.inventorySlots.size(); ++i)
        {
            ItemStack st = c.getSlot(i).getStack();
            NBTTagCompound t = new NBTTagCompound();
            if (st != null) st.writeToNBT(t);
            sl.add(StructuresProbe.canon(t));
        }
        g.add("slots", sl);
        NBTTagCompound ct = new NBTTagCompound();
        if (cursor != null) cursor.writeToNBT(ct);
        g.add("cursor", StructuresProbe.canon(ct));
        StringBuilder tiles = new StringBuilder("ia:");
        Object inv = null;
        if (c instanceof net.minecraft.inventory.ContainerFurnace)
        {
            inv = objField(c, "furnace");
            if (mc == null)
                g.addProperty("fur", "ia:" + intField(c, "lastCookTime") + "," + intField(c, "lastBurnTime") + "," + intField(c, "lastItemBurnTime"));
            else
            {
                net.minecraft.tileentity.TileEntityFurnace f = (net.minecraft.tileentity.TileEntityFurnace)inv;
                g.addProperty("fur", "ia:" + f.field_145961_j + "," + f.field_145956_a + "," + f.field_145963_i);
            }
        }
        else if (c instanceof net.minecraft.inventory.ContainerChest) inv = objField(c, "lowerChestInventory");
        else if (c instanceof net.minecraft.inventory.ContainerHopper) inv = objField(c, "field_94538_a");
        else if (c instanceof net.minecraft.inventory.ContainerDispenser) inv = objField(c, "tileEntityDispenser");
        else if (c instanceof net.minecraft.inventory.ContainerWorkbench)
            tiles.append(intField(c, "posX")).append(',').append(intField(c, "posY")).append(',').append(intField(c, "posZ"));
        else if (c instanceof net.minecraft.inventory.ContainerMerchant)
        {
            // the merchant (the server's villager by id, the client's
            // NpcMerchant by the recipe list its S3F gave it) and
            // InventoryMerchant's currentRecipeIndex and currentRecipe (its
            // index in that list, -1 for none)
            Object mer = objField(c, "theMerchant");
            Object mi = objField(c, "merchantInventory");
            net.minecraft.village.MerchantRecipeList list = (net.minecraft.village.MerchantRecipeList)
                objField(mer, mer instanceof net.minecraft.entity.passive.EntityVillager ? "buyingList" : "recipeList");
            Object cur = objField(mi, "currentRecipe");
            int ci = -1;
            for (int i = 0; list != null && i < list.size(); ++i) if (list.get(i) == cur) ci = i;
            g.addProperty("trIdx", "i:" + intField(mi, "currentRecipeIndex"));
            g.addProperty("trCur", "i:" + ci);
            if (mer instanceof Entity) g.addProperty("merchant", "i:" + ((Entity)mer).getEntityId());
            else if (list != null) g.add("trList", StructuresProbe.canon(list.getRecipiesAsTags()));
        }
        if (inv instanceof net.minecraft.inventory.InventoryLargeChest)
        {
            TileEntity up = (TileEntity)objField(inv, "upperChest"), low = (TileEntity)objField(inv, "lowerChest");
            tiles.append(up.field_145851_c).append(',').append(up.field_145848_d).append(',').append(up.field_145849_e).append(',')
                .append(low.field_145851_c).append(',').append(low.field_145848_d).append(',').append(low.field_145849_e);
        }
        else if (inv instanceof TileEntity)
        {
            TileEntity te = (TileEntity)inv;
            tiles.append(te.field_145851_c).append(',').append(te.field_145848_d).append(',').append(te.field_145849_e);
        }
        else if (inv instanceof net.minecraft.inventory.InventoryEnderChest)
        {
            g.addProperty("ender", "b:1");
            TileEntity te = (TileEntity)objField(inv, "associatedChest");
            if (te != null) tiles.append(te.field_145851_c).append(',').append(te.field_145848_d).append(',').append(te.field_145849_e);
        }
        g.addProperty("tiles", tiles.toString());
        return g;
    }

    static JsonArray containerMirror(net.minecraft.inventory.Container c)
    {
        JsonArray a = new JsonArray();
        for (Object st : c.inventoryItemStacks)
        {
            NBTTagCompound t = new NBTTagCompound();
            if (st != null) ((ItemStack)st).writeToNBT(t);
            a.add(StructuresProbe.canon(t));
        }
        return a;
    }

    static void fields(JsonObject out, Object target, String... names)
    {
        for (String n : names) put(out, target, n);
    }

    /**
     * The itemInUse bookkeeping: which inventory slot the held stack object
     * sits in and the remaining use count. The slot comes from an identity
     * scan, because itemInUse is the stack object itself, not a copy.
     */
    static void itemInUse(JsonObject out, Object player)
    {
        Object use = null;
        Object count = null;
        Object inv = null;
        ItemStack[] main = null;
        ItemStack[] armor = null;

        try
        {
            use = field(player, "itemInUse").get(player);
            count = field(player, "itemInUseCount").get(player);
            inv = field(player, "inventory").get(player);
            main = (ItemStack[])field(inv, "mainInventory").get(inv);
            armor = (ItemStack[])field(inv, "armorInventory").get(inv);
        }
        catch (Exception e)
        {
            throw new IllegalStateException(e);
        }

        if (use == null)
        {
            out.addProperty("itemInUseSlot", "i:-1");
            out.addProperty("itemInUseCount", "i:0");
            return;
        }

        for (int i = 0; i < main.length; ++i)
        {
            if (main[i] == use)
            {
                out.addProperty("itemInUseSlot", "i:" + i);
                out.addProperty("itemInUseCount", "i:" + count);
                return;
            }
        }

        for (int i = 0; i < armor.length; ++i)
        {
            if (armor[i] == use)
            {
                out.addProperty("itemInUseSlot", "i:" + (i + 36));
                out.addProperty("itemInUseCount", "i:" + count);
                return;
            }
        }

        throw new IllegalStateException("Snapshot: itemInUse is not in the inventory");
    }

    /** One field as a canonical scalar; floats and doubles carry raw bits. */
    static void put(JsonObject out, Object target, String name)
    {
        try
        {
            Field f = findField(target.getClass(), name);
            Object v = f.get(target);

            if (v instanceof Double) out.addProperty(name, "d:" + hex(Double.doubleToRawLongBits(((Double)v).doubleValue()), 16));
            else if (v instanceof Float) out.addProperty(name, "f:" + hex(Float.floatToRawIntBits(((Float)v).floatValue()) & 4294967295L, 8));
            else if (v instanceof Integer) out.addProperty(name, "i:" + v);
            else if (v instanceof Long) out.addProperty(name, "l:" + v);
            else if (v instanceof Short) out.addProperty(name, "s:" + v);
            else if (v instanceof Byte) out.addProperty(name, "b:" + v);
            else if (v instanceof Boolean) out.addProperty(name, "b:" + (((Boolean)v).booleanValue() ? 1 : 0));
            else throw new IllegalStateException("unsupported field type " + (v == null ? "null" : v.getClass().getName()));
        }
        catch (RuntimeException e)
        {
            throw e;
        }
        catch (Exception e)
        {
            throw new IllegalStateException("Snapshot: field " + name + " of " + target.getClass().getName() + ": " + e);
        }
    }

    /* class to field name to the accessible Field: getDeclaredField copies the
     * field and walks the hierarchy on every call, and a snapshot asks for the
     * same few hundred fields tens of thousands of times */
    static final java.util.concurrent.ConcurrentHashMap<Class<?>, java.util.concurrent.ConcurrentHashMap<String, Field>> fieldCache =
        new java.util.concurrent.ConcurrentHashMap<Class<?>, java.util.concurrent.ConcurrentHashMap<String, Field>>();

    static Field findField(Class<?> c, String name)
    {
        java.util.concurrent.ConcurrentHashMap<String, Field> m = fieldCache.get(c);
        if (m == null)
        {
            m = new java.util.concurrent.ConcurrentHashMap<String, Field>();
            java.util.concurrent.ConcurrentHashMap<String, Field> had = fieldCache.putIfAbsent(c, m);
            if (had != null) m = had;
        }
        Field f = m.get(name);
        if (f != null) return f;
        for (Class<?> k = c; k != null; k = k.getSuperclass())
        {
            try
            {
                f = k.getDeclaredField(name);
                f.setAccessible(true);
                m.put(name, f);
                return f;
            }
            catch (NoSuchFieldException e)
            {
                // keep walking up
            }
        }
        throw new IllegalStateException("no field " + name + " in " + c.getName());
    }

    static boolean hasField(Object target, String name)
    {
        for (Class<?> k = target.getClass(); k != null; k = k.getSuperclass())
        {
            try { k.getDeclaredField(name); return true; }
            catch (NoSuchFieldException e) { }
        }
        return false;
    }

    static Field field(Object target, String name)
    {
        try
        {
            return findField(target.getClass(), name);
        }
        catch (Exception e)
        {
            throw new IllegalStateException(e);
        }
    }

    static Object invoke(Object target, String method)
    {
        try
        {
            java.lang.reflect.Method m = target.getClass().getMethod(method);
            m.setAccessible(true);
            return m.invoke(target);
        }
        catch (Exception e)
        {
            throw new IllegalStateException(target.getClass().getName() + "." + method + ": " + e);
        }
    }

    static Object objField(Object target, String name)
    {
        try
        {
            return field(target, name).get(target);
        }
        catch (Exception e)
        {
            throw new IllegalStateException(e);
        }
    }

    static int intField(Object target, String name)
    {
        try
        {
            return field(target, name).getInt(target);
        }
        catch (Exception e)
        {
            throw new IllegalStateException(e);
        }
    }

    static boolean boolField(Object target, String name)
    {
        try
        {
            return field(target, name).getBoolean(target);
        }
        catch (Exception e)
        {
            throw new IllegalStateException(e);
        }
    }

    static double doubleField(Object target, String name)
    {
        try
        {
            return field(target, name).getDouble(target);
        }
        catch (Exception e)
        {
            throw new IllegalStateException(e);
        }
    }

    static float floatField(Object target, String name)
    {
        try
        {
            return field(target, name).getFloat(target);
        }
        catch (Exception e)
        {
            throw new IllegalStateException(e);
        }
    }

    static long longField(Object target, String name)
    {
        return longField(target, name, null);
    }

    static long longField(Object target, String name, Class<?> cls)
    {
        try
        {
            Field f = findField(cls == null ? target.getClass() : cls, name);
            return f.getLong(target);
        }
        catch (Exception e)
        {
            throw new IllegalStateException(e);
        }
    }

    // ------------------------------------------------------------- text output

    /**
     * Canonical text for a JSON tree built here: the same form as the oracle's
     * NBT dumps, nested objects included. A string that canonical NBT cannot
     * carry (a quote, a backslash or a control character) is refused here, so a
     * file the native reader cannot parse is never written.
     */
    static String canonText(JsonElement e)
    {
        StringBuilder b = new StringBuilder();
        canon(b, e);
        return b.toString();
    }

    static void canon(StringBuilder b, JsonElement e)
    {
        if (e.isJsonObject())
        {
            JsonObject o = e.getAsJsonObject();
            List<String> kl = new ArrayList<String>();
            for (Object en : o.entrySet()) kl.add(((java.util.Map.Entry)en).getKey().toString());
            String[] keys = kl.toArray(new String[0]);
            Arrays.sort(keys);
            b.append('{');

            for (int i = 0; i < keys.length; ++i)
            {
                if (i != 0) b.append(',');
                b.append('"').append(keys[i]).append("\":");
                canon(b, o.get(keys[i]));
            }

            b.append('}');
        }
        else if (e.isJsonArray())
        {
            JsonArray a = e.getAsJsonArray();
            b.append('[');

            for (int i = 0; i < a.size(); ++i)
            {
                if (i != 0) b.append(',');
                canon(b, a.get(i));
            }

            b.append(']');
        }
        else
        {
            JsonPrimitive p = e.getAsJsonPrimitive();

            if (p.isString())
            {
                // a canonical NBT scalar or a plain string: quoted, and it must
                // not need an escape the native canonical parser does not know
                String s = p.getAsString();
                validate(s);
                b.append('"').append(s).append('"');
            }
            else b.append(p.toString()); // numbers and booleans stay bare
        }
    }

    /** No escape the native canonical parser does not know. */
    static void validate(String s)
    {
        for (int i = 0; i < s.length(); ++i)
        {
            char c = s.charAt(i);
            if (c < 32 || c == '"' || c == '\\')
                throw new IllegalStateException("Snapshot: a value needs a JSON escape the canonical form does not carry: "
                    + (s.length() > 60 ? s.substring(0, 60) + "..." : s));
        }
    }

    static void writeFile(File f, String text) throws Exception
    {
        PrintWriter w = new PrintWriter(new OutputStreamWriter(new FileOutputStream(f), "UTF-8"));
        w.println(text);
        w.close();
    }

    // ------------------------------------------------------------------ bytes

    static void le32(DataOutputStream o, int v) throws Exception
    {
        o.write(v & 255);
        o.write(v >> 8 & 255);
        o.write(v >> 16 & 255);
        o.write(v >> 24 & 255);
    }

    static void le64(DataOutputStream o, long v) throws Exception
    {
        for (int i = 0; i < 8; ++i) o.write((int)(v >> (8 * i)) & 255);
    }

    static String hexBytes(byte[] a)
    {
        StringBuilder b = new StringBuilder(a.length * 2);
        for (int i = 0; i < a.length; ++i) b.append(hex(a[i] & 255, 2));
        return b.toString();
    }

    static String hexBools(boolean[] a)
    {
        StringBuilder b = new StringBuilder(a.length * 2);
        for (int i = 0; i < a.length; ++i) b.append(a[i] ? "01" : "00");
        return b.toString();
    }

    static String hex(long bits, int digits)
    {
        StringBuilder b = new StringBuilder(digits);
        for (int i = digits - 1; i >= 0; --i) b.append("0123456789abcdef".charAt((int)((bits >>> (i * 4)) & 15L)));
        return b.toString();
    }
}
