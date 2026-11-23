package netherite.oracle;

import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.List;
import java.util.Random;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicLong;

/**
 * Deterministic replacements for every unseeded RNG source in the client and
 * integrated server, and for the shared entity-ID counter. Vanilla draws from the
 * same distributions; only the seed source changes. Each role owns its own seeder,
 * Math.random stream, static-RNG delegates and entity-ID range:
 *   CLIENT  client thread during ticks
 *   SERVER  integrated server thread
 *   OTHER   any other thread (netty, IO)
 *   RENDER  client thread while drawing a frame
 * so server state never depends on client-side object creation, and tick state
 * never depends on how many frames were drawn.
 */
public final class Det
{
    public static final int CLIENT = 0, SERVER = 1, OTHER = 2, RENDER = 3, ROLES = 4;

    static volatile Thread clientThread;
    static volatile Thread serverThread;

    static final Random[] seeder = { new Random(0x6e657468L), new Random(0x65726974L), new Random(0x6f726163L), new Random(0x72656e64L) };
    static final Random[] math = { new Random(0x6d617468L), new Random(0x6d617469L), new Random(0x6d61746aL), new Random(0x6d61746bL) };
    /** Entity IDs per role, in disjoint ranges. Only SERVER IDs ever reach the wire. */
    static final int[] nextId = { 1 << 24, 0, 3 << 24, 2 << 24 };
    static volatile boolean inRender;
    static final List<SplitRandom> splits = new ArrayList<SplitRandom>();
    static volatile long worldSeed;
    static final long[] otherDraws = new long[1];

    private Det() {}

    public static void onClientThread() { clientThread = Thread.currentThread(); }
    public static void onServerThread() { serverThread = Thread.currentThread(); }

    static int role()
    {
        Thread t = Thread.currentThread();
        if (t == serverThread) return SERVER;
        if (t == clientThread) return inRender ? RENDER : CLIENT;
        return OTHER;
    }

    static long mix(long s, long k)
    {
        long z = s + 0x9e3779b97f4a7c15L * (k + 1);
        z = (z ^ (z >>> 30)) * 0xbf58476d1ce4e5b9L;
        z = (z ^ (z >>> 27)) * 0x94d049bb133111ebL;
        return z ^ (z >>> 31);
    }

    /** Called once per world launch, before the integrated server thread starts. */
    public static synchronized void reset(long seed)
    {
        worldSeed = seed;
        installShuffle(mix(seed, 9));
        for (int r = 0; r < ROLES; ++r)
        {
            seeder[r].setSeed(mix(seed, 10 + r));
            math[r].setSeed(mix(seed, 20 + r));
        }
        nextId[CLIENT] = 1 << 24;
        nextId[SERVER] = 0;
        nextId[OTHER] = 3 << 24;
        nextId[RENDER] = 2 << 24;
        for (SplitRandom s : splits) s.reseed();
        for (TickRandom p : pins) p.at = Long.MIN_VALUE;
        pinRun = 0;
        spawnerRun = 0;
    }

    static Random seeder()
    {
        if (clinitWatch != null) watchClinit();
        int r = role();
        if (r == OTHER) ++otherDraws[0];
        return seeder[r];
    }

    /** Replaces {@code nextEntityID++} in the Entity constructor. */
    public static int nextEntityId()
    {
        if (clinitWatch != null) watchClinit();
        int r = role();
        synchronized (nextId) { return nextId[r]++; }
    }

    /**
     * While a pool JVM initializes every class (Pool.init): the classes whose
     * static initializer draws from a seeder, a Math.random stream or the
     * entity-ID counter. Null otherwise.
     */
    static volatile java.util.Set<String> clinitWatch;

    static void watchClinit()
    {
        java.util.Set<String> w = clinitWatch;
        if (w == null) return;
        for (StackTraceElement e : new Throwable().getStackTrace())
        {
            if ("<clinit>".equals(e.getMethodName())) { w.add(e.getClassName()); return; }
        }
    }

    public static void beginRender() { inRender = true; }
    public static void endRender() { inRender = false; }

    /** Replaces {@code new Random()} in vanilla code. */
    public static Random newRandom()
    {
        Random s = seeder();
        long v;
        synchronized (s) { v = s.nextLong(); }
        if (role() == CLIENT)
        {
            Random p = birthPin();
            if (p != null) return p;
        }
        return new Born(v);
    }

    /** Replaces {@code Math.random()} in vanilla code. */
    public static double mathRandom()
    {
        if (clinitWatch != null) watchClinit();
        int r = role();
        if (r == OTHER) ++otherDraws[0];
        Random m = math[r];
        double v;
        synchronized (m) { v = m.nextDouble(); }
        if (r == CLIENT)
        {
            Random p = mathPin();
            if (p != null) return p.nextDouble();
        }
        else if (r == RENDER && Oracle.spawnerPin)
        {
            Random p = spawnerPin();
            if (p != null) return p.nextDouble();
        }
        return v;
    }

    /**
     * The spawner display pin (lane/idxclient). TileEntityMobSpawnerRenderer
     * makes the spinning mob (MobSpawnerBaseLogic.func_98281_h) on the first
     * frame that draws the spawner, and its EntityLivingBase constructor draws
     * three Math.random on the RENDER role's stream (the third is its
     * rotationYaw and rotationYawHead, the head's turn the renderer shows), a
     * stream every other frame-time draw moves: the frame it is made in and
     * everything drawn before decide the head. While the oracle runs those
     * three take their values from one stream reseeded at each construction
     * with pinSeed(seed, PIN_SPAWNER, 0), so every display mob of a world has
     * the same head; the shared stream is still drawn. On a tape whose start
     * block says spawnerPin (new tapes).
     */
    public static final int PIN_SPAWNER = 5;
    static final Random spawnerStream = new Random(0L);
    private static int spawnerRun;

    static Random spawnerPin()
    {
        if (spawnerRun > 0) { --spawnerRun; return spawnerStream; }
        Throwable t = new Throwable();
        // 0 spawnerPin, 1 mathRandom, 2 the draw's site
        StackTraceElement s = JLA.getStackTraceElement(t, 2);
        if (!"<init>".equals(s.getMethodName()) || !"net.minecraft.entity.EntityLivingBase".equals(s.getClassName())) return null;
        for (int i = 3, n = depth(t); i < n; ++i)
        {
            StackTraceElement e = JLA.getStackTraceElement(t, i);
            if ("func_98281_h".equals(e.getMethodName()) && "net.minecraft.tileentity.MobSpawnerBaseLogic".equals(e.getClassName()))
            {
                spawnerStream.setSeed(pinSeed(Oracle.seed, PIN_SPAWNER, 0L));
                spawnerRun = 2;
                return spawnerStream;
            }
        }
        return null;
    }

    /**
     * The client consumer pin (lane/clientrand). Vanilla's client thread draws
     * one Math.random stream and one seeder for everything in call order: the
     * torch flicker (EntityRenderer.updateTorchFlicker's eight draws a tick),
     * a spawned item's EntityItem constructor (hoverStart, the spin and bob
     * phase), every particle's Math.random and its own Random, the particle
     * spawner's EffectRenderer.rand, beside the mobs, orbs, pickups, clock
     * and compass. So a frame's lightmap, an item's spin and a particle's
     * flight depended on every other client draw of the session, which a
     * client that skips one construction can never reproduce. While the
     * oracle runs, those consumers take their values from their own streams
     * (PIN_*), each seeded at every client tick from the world seed, the
     * consumer and the tick (the row's t): a value depends only on the
     * consumer's own draws within that tick. The shared streams are still
     * drawn at every call, and the consumer receives the pinned value; since
     * those values steer some consumers (a rain splash's landing draw, a crit
     * emitter's count), cseed and cmath follow the pinned game, not vanilla's
     * (lane/clientgate); every other row field is unchanged.
     * The pickup effect (EntityPickupFX) keeps the shared stream. Found by
     * the draw's call site; the flicker's eight and the item's four are
     * consecutive, so one stack walk serves a run.
     */
    public static final int PIN_FLICKER = 0, PIN_ITEM = 1, PIN_FX_MATH = 2, PIN_FX_SEED = 3, PIN_EFFECT = 4, PINS = 5;
    static final TickRandom[] pins = { new TickRandom(PIN_FLICKER), new TickRandom(PIN_ITEM), new TickRandom(PIN_FX_MATH),
                                       new TickRandom(PIN_FX_SEED), new TickRandom(PIN_EFFECT) };
    private static int pinRun;
    private static Random pinRunStream;

    /** The consumer's stream for a client Math.random draw, or null for the shared one. */
    static Random mathPin()
    {
        if (pinRun > 0) { --pinRun; return pinRunStream; }
        Throwable t = new Throwable();
        int n = depth(t);
        // 0 mathPin, 1 mathRandom, 2 the draw's site
        StackTraceElement s = JLA.getStackTraceElement(t, 2);
        String c = s.getClassName();
        if ("net.minecraft.client.renderer.EntityRenderer".equals(c) && "updateTorchFlicker".equals(s.getMethodName()))
        {
            pinRun = 7;
            return pinRunStream = pins[PIN_FLICKER];
        }
        if ("net.minecraft.entity.item.EntityItem".equals(c) && n > 3 && "<init>".equals(s.getMethodName())
            && "handleSpawnObject".equals(JLA.getStackTraceElement(t, 3).getMethodName()))
        {
            pinRun = 3;
            return pinRunStream = pins[PIN_ITEM];
        }
        if (c.startsWith("net.minecraft.client.particle.") && !pickupFx(t, 2, n)) return pins[PIN_FX_MATH];
        return null;
    }

    /** The Random a client new Random() gets instead of a seeder-born one, or null. */
    static Random birthPin()
    {
        Throwable t = new Throwable();
        int n = depth(t);
        // 0 birthPin, 1 newRandom, 2 the constructor that makes it
        String c = JLA.getStackTraceElement(t, 2).getClassName();
        if ("net.minecraft.client.particle.EffectRenderer".equals(c)) return pins[PIN_EFFECT];
        if ("net.minecraft.entity.Entity".equals(c) && n > 3
            && "net.minecraft.client.particle.EntityFX".equals(JLA.getStackTraceElement(t, 3).getClassName())
            && !pickupFx(t, 3, n))
        {
            Random p = pins[PIN_FX_SEED];
            return new Born(p.nextLong());
        }
        return null;
    }

    /** An EntityPickupFX constructor among frames from.. of t's constructor chain. */
    private static boolean pickupFx(Throwable t, int from, int n)
    {
        for (int i = from; i < n; ++i)
        {
            StackTraceElement e = JLA.getStackTraceElement(t, i);
            if (!"<init>".equals(e.getMethodName())) return false;
            if ("net.minecraft.client.particle.EntityPickupFX".equals(e.getClassName())) return true;
        }
        return false;
    }

    /** A consumer's stream: reseeded at the first draw of each client tick. */
    static final class TickRandom extends Random
    {
        final int k;
        long at = Long.MIN_VALUE;

        TickRandom(int k) { super(0L); this.k = k; }

        @Override protected int next(int bits)
        {
            long t = Oracle.tick;
            if (t != at)
            {
                at = t;
                setSeed(pinSeed(Oracle.seed, k, t));
            }
            return super.next(bits);
        }
    }

    /** The seed of consumer k's stream at tick t. */
    public static long pinSeed(long worldSeed, int k, long t) { return mix(mix(worldSeed, 40 + k), t); }

    /**
     * java.util.Collections.shuffle(List)'s shared static Random, installed in
     * reset and recorded in the snapshot's det state so the replay seeds it
     * back. Vanilla creates it lazily with a nanoTime seed (it lives in
     * java.util, which the determinize pass does not own), which would make
     * every villager offer rebuild nondeterministic. Found by type on this
     * JDK, as the probes do.
     */
    static Random shuf;
    private static Field shufField;

    static void installShuffle(long seed)
    {
        try
        {
            if (shufField == null)
            {
                for (Field f : java.util.Collections.class.getDeclaredFields())
                {
                    if (f.getType() == Random.class) shufField = f;
                }
                if (shufField == null) throw new IllegalStateException("no shared shuffle Random in java.util.Collections");
                shufField.setAccessible(true);
            }
            shuf = new Random(seed);
            shufField.set(null, shuf);
        }
        catch (Exception e)
        {
            throw new RuntimeException(e);
        }
    }

    public static long shufState() { return shuf == null ? -1L : state(shuf); }

    /** --detail forensics: call sites of client-thread Entity constructions this tick. */
    static final List<String> clientNews = new ArrayList<String>();

    /** Replaces {@code UUID.randomUUID()} in Entity. Version-4 layout, deterministic bits. */
    public static UUID uuid()
    {
        if (Oracle.detail && role() == CLIENT)
        {
            // seven frames above this one, without the JVM's reflection
            // frames (sun.reflect: NativeConstructorAccessorImpl until a
            // constructor inflates, then GeneratedConstructorAccessorN, N in
            // the JVM's order of inflation across threads): two runs of one
            // tape differed there, not in the game's own frames
            Throwable t = new Throwable();
            StringBuilder b = new StringBuilder();
            for (int i = 2, n = depth(t), k = 0; i < n && k < 7; ++i)
            {
                if (JLA.getStackTraceElement(t, i).getClassName().startsWith("sun.reflect.")) continue;
                b.append(k++ > 0 ? "<" : "").append(frame(t, i));
            }
            clientNews.add(b.toString());
        }
        Random s = seeder();
        long a, b;
        synchronized (s) { a = s.nextLong(); b = s.nextLong(); }
        a = (a & ~0xF000L) | 0x4000L;
        b = (b & 0x3FFFFFFFFFFFFFFFL) | 0x8000000000000000L;
        return new UUID(a, b);
    }

    /**
     * Replaces static shared Random fields (Item.itemRand and friends). Seeded by
     * name, not by creation order: class initialization can race between the
     * client and server threads during startup.
     */
    public static Random splitRandom(String name)
    {
        SplitRandom s = new SplitRandom(name);
        synchronized (Det.class) { splits.add(s); }
        s.reseed();
        return s;
    }

    private static Field seedField;

    /** The 48-bit state of a java.util.Random (or a SplitRandom's server stream). */
    public static long state(Random r)
    {
        if (r instanceof SplitRandom) r = ((SplitRandom)r).d[SERVER];
        try
        {
            if (seedField == null)
            {
                seedField = Random.class.getDeclaredField("seed");
                seedField.setAccessible(true);
            }
            return ((AtomicLong)seedField.get(r)).get();
        }
        catch (Exception e)
        {
            throw new RuntimeException(e);
        }
    }

    static long seederState(int role) { return state(seeder[role]); }
    static long mathState(int role) { return state(math[role]); }

    /**
     * The shared static RNGs of one role that have been drawn from since the last
     * reset, folded order-free (creation order races at startup). Untouched streams
     * carry no information, and leaving them out means deleting a class that owns
     * one does not change the digest.
     */
    static long splitState(int role)
    {
        long h = 0;
        synchronized (Det.class)
        {
            for (SplitRandom s : splits) if (s.used[role]) h += mix(state(s.d[role]), s.name.hashCode());
        }
        return h;
    }

    /** NW_JRAND set: every Born draw prints its call site (read once; the environment does not change). */
    static final boolean JRAND = System.getenv("NW_JRAND") != null;

    /** Where the JRAND lines go: stderr through a buffer (a draw is far
     * cheaper than an unbuffered write), flushed at each JRANDMARK and when
     * the rows close. */
    static final java.io.PrintStream JERR = JRAND
        ? new java.io.PrintStream(new java.io.BufferedOutputStream(new java.io.FileOutputStream(java.io.FileDescriptor.err), 1 << 16), false)
        : null;

    private static final sun.misc.JavaLangAccess JLA = sun.misc.SharedSecrets.getJavaLangAccess();

    /** t.getStackTrace().length, without building the elements. */
    static int depth(Throwable t) { return JLA.getStackTraceDepth(t); }

    /** Frame i of t.getStackTrace() as "SimpleClass.method" (the class name
     * past its last dot), building only that element. */
    static String frame(Throwable t, int i)
    {
        StackTraceElement e = JLA.getStackTraceElement(t, i);
        String c = e.getClassName();
        return c.substring(c.lastIndexOf('.') + 1) + "." + e.getMethodName();
    }

    /**
     * A {@code new Random()}: the same generator, remembering the state it was
     * born with. A Snapshot records it for every entity, because a constructor's
     * draws on the entity's own Random (EntitySquid.rotationVelocity,
     * EntityChicken.timeUntilNextEgg) set fields no NBT carries.
     */
    static final class Born extends Random
    {
        final long born;
        Born(long seed) { super(seed); born = (seed ^ 0x5DEECE66DL) & ((1L << 48) - 1); }

        @Override protected int next(int bits)
        {
            if (JRAND)
            {
                Throwable t = new Throwable();
                StringBuilder b = new StringBuilder("JRAND ");
                for (int i = 2, n = Math.min(depth(t), 6); i < n; ++i) b.append(frame(t, i)).append(' ');
                JERR.println(b);
            }

            return super.next(bits);
        }
    }

    static final class Rng extends Random
    {
        Rng() { super(0L); }
        int bits(int n) { return next(n); }
    }

    static final class SplitRandom extends Random
    {
        final Rng[] d = { new Rng(), new Rng(), new Rng(), new Rng() };

        final String name;
        final boolean[] used = new boolean[ROLES];

        SplitRandom(String name) { super(0L); this.name = name; }

        void reseed()
        {
            long k = name.hashCode() & 0xffffffffL;
            for (int r = 0; r < ROLES; ++r)
            {
                d[r].setSeed(mix(worldSeed ^ (k << 16), 30 + r));
                used[r] = false;
            }
        }

        private Rng pick()
        {
            int r = role();
            if (r == OTHER) ++otherDraws[0];
            used[r] = true;
            return d[r];
        }

        @Override protected int next(int bits)
        {
            Rng p = pick();
            synchronized (p) { return p.bits(bits); }
        }

        @Override public synchronized void setSeed(long seed)
        {
            if (d != null) pick().setSeed(seed);
        }

        @Override public double nextGaussian()
        {
            Rng p = pick();
            synchronized (p) { return p.nextGaussian(); }
        }
    }
}
