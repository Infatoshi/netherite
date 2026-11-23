package netherite.oracle;

import java.io.DataOutputStream;
import java.io.File;
import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.WeakHashMap;
import net.minecraft.nbt.CompressedStreamTools;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.world.ChunkCoordIntPair;
import net.minecraft.world.chunk.storage.AnvilChunkLoader;
import net.minecraft.world.chunk.storage.RegionFileCache;
import net.minecraft.world.storage.IThreadedFileIO;

/**
 * The oracle's chunk writer. Vanilla's ThreadedFileIOBase writes region files
 * on a background thread, and a chunk that reloads while its write is in
 * flight (AnvilChunkLoader.writeNextIO drops it from the pending set before
 * the file has it) reads the stale file. The oracle keeps the background
 * write and makes it exactly what a synchronous write gives, without the
 * server ever waiting for it: AnvilChunkLoader.saveChunk hands every save to
 * this writer as its own deep copy (detach), and the writer writes them in
 * save order, each one, region file bytes included (a later save of a chunk
 * still pending does not replace the earlier one here, as it does in the
 * loader's list). The loader's pending list only serves reads:
 * AnvilChunkLoader.loadChunk finds a chunk with a save in flight there
 * (vanilla reads the pending NBT), and an entry leaves it (purge, on the
 * server thread) only once every save of its chunk is in the region file. A
 * reader of the save as files (ThreadedFileIOBase.waitForFinish, a command
 * that may snapshot or copy the save) settles first: settle returns only
 * when every queued write is on disk. Region timestamps are the save clock
 * pin.
 */
public final class ChunkIO
{
    private static final Object lock = new Object();
    /** Saves (Write) and other IThreadedFileIO, in queue order. */
    private static final ArrayDeque<Object> queue = new ArrayDeque<Object>();
    /** Per loader and chunk, the saves not yet in the region file. */
    private static final Map<Key, int[]> inFlight = new HashMap<Key, int[]>();
    /** Per loader, the chunks whose every save is in the region file, for
     * purge (weak: a world that ended keeps none of it). */
    private static final Map<AnvilChunkLoader, List<ChunkCoordIntPair>> done = new WeakHashMap<AnvilChunkLoader, List<ChunkCoordIntPair>>();
    private static boolean busy;
    private static Thread writer;

    private ChunkIO() {}

    private static final class Key
    {
        final AnvilChunkLoader loader;
        final ChunkCoordIntPair at;

        Key(AnvilChunkLoader loader, ChunkCoordIntPair at)
        {
            this.loader = loader;
            this.at = at;
        }

        public boolean equals(Object o)
        {
            return o instanceof Key && ((Key)o).loader == loader && ((Key)o).at.equals(at);
        }

        public int hashCode()
        {
            return System.identityHashCode(loader) * 31 + at.hashCode();
        }
    }

    private static final class Write
    {
        final Key key;
        final NBTTagCompound nbt;

        Write(Key key, NBTTagCompound nbt)
        {
            this.key = key;
            this.nbt = nbt;
        }
    }

    /** ThreadedFileIOBase.queueIO: true when the oracle writes it. A chunk
     * loader's saves are already queued (detach). */
    public static boolean queue(IThreadedFileIO io)
    {
        if (!Oracle.active()) return false;
        if (io instanceof AnvilChunkLoader) return true;
        enqueue(io);
        return true;
    }

    private static void enqueue(Object o)
    {
        synchronized (lock)
        {
            if (writer == null)
            {
                writer = new Thread(new Runnable() { public void run() { write(); } }, "Oracle Chunk IO");
                writer.setDaemon(true);
                writer.start();
            }
            queue.add(o);
            lock.notifyAll();
        }
    }

    private static void write()
    {
        while (true)
        {
            Object o;
            synchronized (lock)
            {
                busy = false;
                lock.notifyAll();
                while (queue.isEmpty())
                {
                    try { lock.wait(); }
                    catch (InterruptedException e) { return; }
                }
                o = queue.poll();
                busy = true;
            }
            try
            {
                if (o instanceof Write)
                {
                    Write w = (Write)o;
                    try
                    {
                        DataOutputStream out = RegionFileCache.getChunkOutputStream((File)DIR.get(w.key.loader), w.key.at.chunkXPos, w.key.at.chunkZPos);
                        CompressedStreamTools.write(w.nbt, out);
                        out.close();
                    }
                    catch (Exception e)
                    {
                        e.printStackTrace();
                    }
                    synchronized (lock)
                    {
                        int[] n = inFlight.get(w.key);
                        if (--n[0] == 0)
                        {
                            inFlight.remove(w.key);
                            List<ChunkCoordIntPair> d = done.get(w.key.loader);
                            if (d == null) done.put(w.key.loader, d = new ArrayList<ChunkCoordIntPair>());
                            d.add(w.key.at);
                        }
                    }
                }
                else
                {
                    IThreadedFileIO io = (IThreadedFileIO)o;
                    while (io.writeNextIO())
                    {
                        ;
                    }
                }
            }
            catch (Throwable t)
            {
                t.printStackTrace();
            }
        }
    }

    private static final Field TO_WRITE = field(AnvilChunkLoader.class, "chunksToRemove"), PENDING = field(AnvilChunkLoader.class, "pendingAnvilChunksCoordinates"),
        LOCK = field(AnvilChunkLoader.class, "syncLockObject"), DIR = field(AnvilChunkLoader.class, "chunkSaveLocation");
    private static final Class<?> PENDING_CHUNK = pendingChunk();
    private static final Field COORD = field(PENDING_CHUNK, "chunkCoordinate"), TAGS = field(PENDING_CHUNK, "nbtTags");
    private static final Constructor<?> NEW_PENDING = pendingCtor();

    private static Field field(Class<?> c, String name)
    {
        try
        {
            Field f = c.getDeclaredField(name);
            f.setAccessible(true);
            return f;
        }
        catch (NoSuchFieldException e)
        {
            throw new ExceptionInInitializerError(e);
        }
    }

    private static Class<?> pendingChunk()
    {
        for (Class<?> c : AnvilChunkLoader.class.getDeclaredClasses())
        {
            if (c.getSimpleName().equals("PendingChunk")) return c;
        }
        throw new ExceptionInInitializerError("AnvilChunkLoader.PendingChunk");
    }

    private static Constructor<?> pendingCtor()
    {
        try
        {
            Constructor<?> k = PENDING_CHUNK.getDeclaredConstructor(ChunkCoordIntPair.class, NBTTagCompound.class);
            k.setAccessible(true);
            return k;
        }
        catch (NoSuchMethodException e)
        {
            throw new ExceptionInInitializerError(e);
        }
    }

    /**
     * Server thread, under the loader's lock: every chunk whose saves are all
     * in the region file leaves the pending list of its loader l (a later
     * save or load of it reads the file, as vanilla after writeNextIO).
     */
    private static void purge(AnvilChunkLoader l) throws Exception
    {
        List<ChunkCoordIntPair> gone = new ArrayList<ChunkCoordIntPair>();
        synchronized (lock)
        {
            List<ChunkCoordIntPair> d = done.remove(l);
            if (d == null) return;
            for (ChunkCoordIntPair at : d)
            {
                if (!inFlight.containsKey(new Key(l, at))) gone.add(at);
            }
        }
        List list = (List)TO_WRITE.get(l);
        Set pending = (Set)PENDING.get(l);
        for (ChunkCoordIntPair at : gone)
        {
            pending.remove(at);
            for (int i = 0; i < list.size(); i++)
            {
                if (at.equals(COORD.get(list.get(i))))
                {
                    list.remove(i);
                    break;
                }
            }
        }
    }

    /** AnvilChunkLoader.saveChunk: what it queues, as of the save. Vanilla's
     * NBT holds the live chunk's arrays (Blocks, Add, Data, BlockLight,
     * SkyLight, HeightMap, Biomes), so a chunk that stays loaded and relights
     * or changes before the writer thread reaches it was written with the
     * later bytes: two runs of one script saved different light into the
     * snapshot's save/ overlay. A deep copy is what a synchronous write
     * would have written; the writer gets its own entry for it, so every
     * save is written in save order however far behind the writer is. */
    public static NBTTagCompound detach(AnvilChunkLoader l, ChunkCoordIntPair at, NBTTagCompound nbt)
    {
        if (!Oracle.active()) return nbt;
        NBTTagCompound copy = (NBTTagCompound)nbt.copy();
        Key k = new Key(l, at);
        try
        {
            synchronized (LOCK.get(l))
            {
                purge(l);
            }
        }
        catch (Exception e)
        {
            throw new RuntimeException(e);
        }
        synchronized (lock)
        {
            int[] n = inFlight.get(k);
            if (n == null) inFlight.put(k, n = new int[1]);
            n[0]++;
        }
        enqueue(new Write(k, copy));
        return copy;
    }

    /** AnvilChunkLoader.loadChunk: a chunk still pending is read from the
     * loader's list, and the chunk vanilla builds from that NBT shares its
     * arrays, while the writer may not have written them yet: the list gets
     * a copy to read instead. */
    public static void loading(AnvilChunkLoader l, ChunkCoordIntPair at)
    {
        if (!Oracle.active()) return;
        try
        {
            synchronized (LOCK.get(l))
            {
                purge(l);
                if (!((Set)PENDING.get(l)).contains(at)) return;
                List list = (List)TO_WRITE.get(l);
                for (int i = 0; i < list.size(); i++)
                {
                    Object p = list.get(i);
                    if (at.equals(COORD.get(p)))
                    {
                        list.set(i, NEW_PENDING.newInstance(at, ((NBTTagCompound)TAGS.get(p)).copy()));
                        return;
                    }
                }
            }
        }
        catch (Exception e)
        {
            throw new RuntimeException(e);
        }
    }

    /** Every queued chunk write is on disk. */
    public static void settle()
    {
        if (!Oracle.active()) return;
        synchronized (lock)
        {
            while (busy || !queue.isEmpty())
            {
                try { lock.wait(); }
                catch (InterruptedException e) { throw new RuntimeException(e); }
            }
        }
    }
}
