package netherite.oracle;

import java.io.DataOutputStream;
import java.io.IOException;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.atomic.AtomicInteger;
import net.minecraft.entity.Entity;
import net.minecraft.entity.player.EntityPlayerMP;
import net.minecraft.nbt.CompressedStreamTools;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.server.MinecraftServer;
import net.minecraft.world.WorldServer;

/**
 * The digests of a row's server entities (Rows: d.ents and d.sp), computed
 * beside the tick loop instead of in it. A digest is the first 8 bytes of the
 * SHA-1 of the entity's uncompressed NBT (Rows.nbtLong), folded in list order.
 *
 * The server thread starts a job when its tick ends (Lockstep.serverDone),
 * before it lets the client go: the entity list is the server's own lists at
 * that moment. Workers serialize the entities (writeToNBT only reads them)
 * while the client drains packets, captures its row and runs its next tick,
 * which touch only the client world; Lockstep waits for every entity to be
 * serialized before it lets the server tick again. The SHA-1 of the bytes
 * then runs beside the server tick, and the row is completed and written at
 * the next capture (Rows.flush). An entity whose bytes equal its bytes of the
 * last job has the same digest, which is reused (most entities are idle).
 * Each digest lands at its list index, so the fold is the serial loop's.
 */
final class RowHash
{
    private RowHash() {}

    /** An unsynchronized growable byte buffer (ByteArrayOutputStream locks every byte). */
    static final class Buf extends java.io.OutputStream
    {
        byte[] b = new byte[2048];
        int n;
        final DataOutputStream data = new DataOutputStream(this);

        void room(int k)
        {
            if (n + k > b.length) b = Arrays.copyOf(b, Math.max(b.length * 2, n + k));
        }

        @Override public void write(int v)
        {
            room(1);
            b[n++] = (byte)v;
        }

        @Override public void write(byte[] src, int off, int len)
        {
            room(len);
            System.arraycopy(src, off, b, n, len);
            n += len;
        }

        boolean same(Buf o)
        {
            if (n != o.n) return false;
            byte[] x = b, y = o.b;
            for (int i = 0; i < n; ++i) if (x[i] != y[i]) return false;
            return true;
        }
    }

    /**
     * CompressedStreamTools.write(tag, DataOutput) byte for byte, written
     * straight into a Buf: the same tag ids, big-endian numbers (floats and
     * doubles by floatToIntBits / doubleToLongBits), modified UTF-8 strings
     * with their u16 length, a compound's entries in its map's iteration order
     * then 0, and a list's type as NBTTagList.write computes it (without
     * storing it). The first entity of each class, and one entity in 2^18
     * after that, is also written the vanilla way and compared.
     */
    static final class Nbt
    {
        private static final java.lang.reflect.Field MAP, LIST, STRING;

        static
        {
            try
            {
                MAP = net.minecraft.nbt.NBTTagCompound.class.getDeclaredField("tagMap");
                LIST = net.minecraft.nbt.NBTTagList.class.getDeclaredField("tagList");
                STRING = net.minecraft.nbt.NBTTagString.class.getDeclaredField("data");
                MAP.setAccessible(true);
                LIST.setAccessible(true);
                STRING.setAccessible(true);
            }
            catch (Exception e)
            {
                throw new ExceptionInInitializerError(e);
            }
        }

        private static Object get(java.lang.reflect.Field f, Object o)
        {
            try { return f.get(o); }
            catch (IllegalAccessException e) { throw new IllegalStateException(e); }
        }

        static void root(NBTTagCompound tag, Buf o)
        {
            o.room(3);
            o.b[o.n++] = 10;
            o.b[o.n++] = 0; // writeUTF("")
            o.b[o.n++] = 0;
            compound(tag, o);
        }

        private static void compound(NBTTagCompound c, Buf o)
        {
            java.util.Map m = (java.util.Map)get(MAP, c);
            for (Object x : m.entrySet())
            {
                java.util.Map.Entry en = (java.util.Map.Entry)x;
                net.minecraft.nbt.NBTBase t = (net.minecraft.nbt.NBTBase)en.getValue();
                byte id = t.getId();
                o.room(1);
                o.b[o.n++] = id;
                if (id != 0)
                {
                    utf((String)en.getKey(), o);
                    payload(t, id, o);
                }
            }
            o.room(1);
            o.b[o.n++] = 0;
        }

        private static void payload(net.minecraft.nbt.NBTBase t, byte id, Buf o)
        {
            switch (id)
            {
                case 0: return;
                case 1: o.room(1); o.b[o.n++] = ((net.minecraft.nbt.NBTTagByte)t).func_150290_f(); return;
                case 2: u16(((net.minecraft.nbt.NBTTagShort)t).func_150289_e(), o); return;
                case 3: i32(((net.minecraft.nbt.NBTTagInt)t).func_150287_d(), o); return;
                case 4: i64(((net.minecraft.nbt.NBTTagLong)t).func_150291_c(), o); return;
                case 5: i32(Float.floatToIntBits(((net.minecraft.nbt.NBTTagFloat)t).func_150288_h()), o); return;
                case 6: i64(Double.doubleToLongBits(((net.minecraft.nbt.NBTTagDouble)t).func_150286_g()), o); return;
                case 7:
                {
                    byte[] a = ((net.minecraft.nbt.NBTTagByteArray)t).func_150292_c();
                    i32(a.length, o);
                    o.write(a, 0, a.length);
                    return;
                }
                case 8: utf((String)get(STRING, t), o); return;
                case 9:
                {
                    List l = (List)get(LIST, t);
                    int n = l.size();
                    byte type = n == 0 ? 0 : ((net.minecraft.nbt.NBTBase)l.get(0)).getId();
                    o.room(1);
                    o.b[o.n++] = type;
                    i32(n, o);
                    for (int i = 0; i < n; ++i)
                    {
                        net.minecraft.nbt.NBTBase e = (net.minecraft.nbt.NBTBase)l.get(i);
                        payload(e, e.getId(), o);
                    }
                    return;
                }
                case 10: compound((NBTTagCompound)t, o); return;
                case 11:
                {
                    int[] a = ((net.minecraft.nbt.NBTTagIntArray)t).func_150302_c();
                    i32(a.length, o);
                    for (int v : a) i32(v, o);
                    return;
                }
                default: throw new IllegalStateException("oracle: NBT tag id " + id);
            }
        }

        private static void u16(int v, Buf o)
        {
            o.room(2);
            byte[] b = o.b;
            int n = o.n;
            b[n] = (byte)(v >>> 8);
            b[n + 1] = (byte)v;
            o.n = n + 2;
        }

        private static void i32(int v, Buf o)
        {
            o.room(4);
            byte[] b = o.b;
            int n = o.n;
            b[n] = (byte)(v >>> 24);
            b[n + 1] = (byte)(v >>> 16);
            b[n + 2] = (byte)(v >>> 8);
            b[n + 3] = (byte)v;
            o.n = n + 4;
        }

        private static void i64(long v, Buf o)
        {
            i32((int)(v >>> 32), o);
            i32((int)v, o);
        }

        /** DataOutputStream.writeUTF: u16 length, then 1 byte for 0x01..0x7f, 3 above 0x7ff, else 2. */
        private static void utf(String s, Buf o)
        {
            int len = s.length();
            o.room(2 + 3 * len);
            byte[] b = o.b;
            int at = o.n, n = at + 2;
            for (int i = 0; i < len; ++i)
            {
                char c = s.charAt(i);
                if (c >= 0x0001 && c <= 0x007F) b[n++] = (byte)c;
                else if (c > 0x07FF)
                {
                    b[n++] = (byte)(0xE0 | ((c >> 12) & 0x0F));
                    b[n++] = (byte)(0x80 | ((c >> 6) & 0x3F));
                    b[n++] = (byte)(0x80 | (c & 0x3F));
                }
                else
                {
                    b[n++] = (byte)(0xC0 | ((c >> 6) & 0x1F));
                    b[n++] = (byte)(0x80 | (c & 0x3F));
                }
            }
            int utflen = n - at - 2;
            if (utflen > 65535) throw new RuntimeException(new java.io.UTFDataFormatException("encoded string too long: " + utflen + " bytes"));
            b[at] = (byte)(utflen >>> 8);
            b[at + 1] = (byte)utflen;
            o.n = n;
        }

        private static final java.util.Set<Class<?>> checked = java.util.Collections.newSetFromMap(new java.util.concurrent.ConcurrentHashMap<Class<?>, Boolean>());
        private static final AtomicInteger sample = new AtomicInteger();

        static boolean check(Entity e)
        {
            return !checked.contains(e.getClass()) || (sample.incrementAndGet() & 0x3ffff) == 0;
        }

        /** The vanilla writer gives the same bytes, or the run stops. */
        static void verify(NBTTagCompound tag, Buf got, Entity e)
        {
            Buf want = new Buf();
            try { CompressedStreamTools.write(tag, want.data); }
            catch (IOException ex) { throw new RuntimeException(ex); }
            if (!want.same(got)) throw new IllegalStateException("oracle: RowHash.Nbt differs from CompressedStreamTools for " + e.getClass().getName());
            checked.add(e.getClass());
        }
    }

    /** One thread's digest. */
    static final class Hasher
    {
        final MessageDigest md;
        final Buf scratch = new Buf();

        Hasher()
        {
            try { md = MessageDigest.getInstance("SHA-1"); }
            catch (Exception e) { throw new RuntimeException(e); }
        }

        /** CompressedStreamTools.write of the entity's writeToNBT: its NBT, uncompressed (Nbt.root). */
        static void serialize(Entity e, Buf to)
        {
            NBTTagCompound tag = new NBTTagCompound();
            e.writeToNBT(tag);
            to.n = 0;
            Nbt.root(tag, to);
            if (Nbt.check(e)) Nbt.verify(tag, to, e);
        }

        long digest(Buf b)
        {
            md.update(b.b, 0, b.n);
            byte[] d = md.digest();
            long v = 0;
            for (int i = 0; i < 8; ++i) v = (v << 8) | (d[i] & 0xff);
            return v;
        }

        long hash(Entity e)
        {
            serialize(e, scratch);
            return digest(scratch);
        }
    }

    static final ThreadLocal<Hasher> HASHER = new ThreadLocal<Hasher>()
    {
        @Override protected Hasher initialValue() { return new Hasher(); }
    };

    /** One entity's serialized NBT and its digest as of the last job that hashed it. */
    static final class Last
    {
        Buf buf = new Buf();
        long digest;
        boolean valid;
        Job job; // the job that took it: an entity listed twice gets a fresh one

        /** The digest of slot[i], the entity's NBT now; the two buffers trade places when it changed. */
        long digest(Hasher h, Buf[] slot, int i)
        {
            Buf b = slot[i];
            if (valid && b.same(buf)) return digest;
            digest = h.digest(b);
            valid = true;
            slot[i] = buf;
            buf = b;
            return digest;
        }
    }

    /** The client player's Last and buffer (client thread). */
    static final Last cpLast = new Last();
    static final Buf[] cpSlot = { new Buf() };

    static long clientPlayer(Entity p)
    {
        Hasher h = HASHER.get();
        Hasher.serialize(p, cpSlot[0]);
        return cpLast.digest(h, cpSlot, 0);
    }

    static final int WORKERS = Math.max(1, Math.min(8, Runtime.getRuntime().availableProcessors() - 2));
    private static Thread[] pool;
    /** Workers take jobs here; each one that takes a job posts it twice more until every worker has it. */
    private static final LinkedBlockingQueue<Job> queue = new LinkedBlockingQueue<Job>();
    /** Scratch buffers per list index, one set per job parity (two jobs overlap: one hashing, one serializing). */
    private static final Buf[][] slots = { new Buf[0], new Buf[0] };
    /** The last job started (server thread; read by the client after the done semaphore). */
    static volatile Job latest;
    /** The job of the server's last tick, null when it started none. */
    static volatile Job tickJob;
    /** The job the server started at the end of the tick the client just paired, until capture takes it. */
    static Job fresh;
    private static long jobs;

    /**
     * Lockstep.serverDone, on the server thread before it releases the client:
     * the job of the tick that just ended, which the server thread then helps
     * serialize while it waits for its next permit.
     */
    static Job serverTickEnd(MinecraftServer server)
    {
        tickJob = null;
        if (Oracle.detail || server.worldServers == null || server.worldServers.length == 0 || server.worldServers[0] == null) return null;
        ArrayList<Entity> es = new ArrayList<Entity>();
        for (WorldServer x : server.worldServers)
        {
            if (x == null) continue;
            for (Object o : x.loadedEntityList) es.add((Entity)o);
        }
        int nEnts = es.size(), spAt = -1;
        List players = server.getConfigurationManager().playerEntityList;
        if (!players.isEmpty())
        {
            EntityPlayerMP sp = (EntityPlayerMP)players.get(0);
            for (int i = 0; i < nEnts && spAt < 0; ++i) if (es.get(i) == sp) spAt = i;
            if (spAt < 0) { spAt = es.size(); es.add(sp); }
        }
        if (es.isEmpty()) return null;
        return start(es, nEnts, spAt);
    }

    private static Job start(List<Entity> es, int nEnts, int spAt)
    {
        if (pool == null)
        {
            pool = new Thread[WORKERS];
            for (int i = 0; i < WORKERS; ++i)
            {
                pool[i] = new Thread(new Runnable() { public void run() { work(); } }, "Oracle Row Hash " + i);
                pool[i].setDaemon(true);
                pool[i].start();
            }
        }
        int parity = (int)(jobs++ & 1);
        if (slots[parity].length < es.size())
        {
            Buf[] a = slots[parity], b = Arrays.copyOf(a, Math.max(es.size(), a.length * 2));
            for (int i = a.length; i < b.length; ++i) b[i] = new Buf();
            slots[parity] = b;
        }
        Job j = new Job(es, nEnts, spAt, slots[parity], latest);
        latest = j;
        tickJob = j;
        j.posted.set(1);
        queue.add(j);
        return j;
    }

    /** Lockstep.pair, after the server's tick: that tick's job, for the capture that follows. */
    static void onPaired()
    {
        fresh = tickJob;
    }

    /** Capture: the job for the server state being captured, or null (capture hashes it itself). */
    static Job take()
    {
        Job j = fresh;
        fresh = null;
        return j;
    }

    /** Lockstep, before it lets the server tick: no job reads the server any more. */
    static void awaitSerialized()
    {
        Job j = latest;
        if (j != null) j.awaitSerialized();
    }

    /** A pool JVM between jobs: the last job is hashed, so no worker reads it while the statics rewind. */
    static void quiesce()
    {
        Job j = latest;
        if (j != null) j.awaitHashed();
    }

    private static void work()
    {
        while (true)
        {
            Job j;
            try { j = queue.take(); }
            catch (InterruptedException e) { return; }
            // wake two more, so every worker has the job after log2(WORKERS) wake-ups
            for (int k = 0; k < 2 && j.serNext.get() < j.n; ++k) if (j.posted.getAndIncrement() < WORKERS) queue.add(j);
            j.serialize();
            while (j.serLeft.get() > 0) Thread.yield();
            j.digest();
        }
    }

    /**
     * Each entity's Last from the previous job. Entities keep their order in
     * the lists (one leaves, one joins at the end), so the previous list is
     * walked alongside; an entity not found ahead starts a fresh Last. A Last
     * only saves work: equal bytes have the same digest whichever entity it held.
     */
    private static Last[] lasts(Job j)
    {
        Job p = j.prev;
        List<Entity> old = p == null ? new ArrayList<Entity>() : p.es;
        Last[] prevLast = p == null ? new Last[0] : p.last;
        Last[] out = new Last[j.n];
        int m = old.size(), at = 0;
        for (int i = 0; i < out.length; ++i)
        {
            Entity e = j.es.get(i);
            Last l = null;
            for (int k = at; k < m; ++k)
            {
                if (old.get(k) == e)
                {
                    l = prevLast[k];
                    at = k + 1;
                    break;
                }
            }
            if (l == null || l.job == j) l = new Last();
            l.job = j;
            out[i] = l;
        }
        return out;
    }

    static final class Job
    {
        final List<Entity> es;
        final int n, nEnts, spAt;
        final Buf[] slot;
        final long[] out;
        /** The previous job: its digests (and so its Lasts) finish before this job's start. */
        Job prev;
        volatile Last[] last;
        final AtomicInteger posted = new AtomicInteger();
        final AtomicInteger serNext = new AtomicInteger(), shaNext = new AtomicInteger();
        final AtomicInteger serLeft, shaLeft;
        volatile Throwable failed;

        Job(List<Entity> es, int nEnts, int spAt, Buf[] slot, Job prev)
        {
            this.es = es;
            n = es.size();
            this.nEnts = nEnts;
            this.spAt = spAt;
            this.slot = slot;
            this.prev = prev;
            out = new long[n];
            serLeft = new AtomicInteger(n);
            shaLeft = new AtomicInteger(n);
        }

        void serialize()
        {
            int i;
            while ((i = serNext.getAndIncrement()) < n)
            {
                try { Hasher.serialize(es.get(i), slot[i]); }
                catch (Throwable t) { failed = t; }
                serLeft.decrementAndGet();
            }
        }

        void digest()
        {
            if (shaNext.get() >= n) return;
            Last[] ls = last;
            if (ls == null)
            {
                Job p = prev;
                if (p != null) p.awaitHashed();
                synchronized (this)
                {
                    if (last == null)
                    {
                        last = lasts(this);
                        prev = null; // the chain ends here
                    }
                    ls = last;
                }
            }
            Hasher h = HASHER.get();
            int i;
            while ((i = shaNext.getAndIncrement()) < n)
            {
                try { out[i] = ls[i].digest(h, slot, i); }
                catch (Throwable t) { failed = t; }
                shaLeft.decrementAndGet();
            }
        }

        /** Every entity is serialized. Each decrement follows its write. */
        void awaitSerialized()
        {
            serialize();
            while (serLeft.get() > 0) Thread.yield();
            if (failed != null) throw new RuntimeException("row hash", failed);
        }

        long[] awaitHashed()
        {
            awaitSerialized();
            digest();
            while (shaLeft.get() > 0) Thread.yield();
            if (failed != null) throw new RuntimeException("row hash", failed);
            return out;
        }
    }
}
