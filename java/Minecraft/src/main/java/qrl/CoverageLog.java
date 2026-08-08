package qrl;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.Map;
import java.util.TreeMap;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicLong;

/**
 * render-opt coverage hook: counts how many times each render-path method our C
 * kernels back fires during real play, keyed by (method, block/entity/particle/model
 * name, dimension, branch flags). Deduped with counts; flushed to a file periodically.
 *
 * This is the qrl-mod side of the hook. The actual sampling of MC render methods lives
 * in Recorder.onClientTick (it calls read-only MC methods directly and infers the
 * mutating ones from world state, so goldens captured from REAL MC stay valid). Control
 * cmds come over the qrl TCP socket: coverage_reset / coverage_dump / coverage_enable.
 *
 * Output line format (tab-separated):
 *   method <TAB> name <TAB> dim <TAB> flags <TAB> count
 */
public final class CoverageLog
{
    public static final String DEFAULT_FILE =
        System.getProperty("user.home") + "/dev/minecraft/mc-1.11.2-env/java/render-opt/coverage.log";

    // Branch-flag bits (orthogonal; call sites OR them; dim bits added by hit()).
    public static final int F_WATER        = 1 << 0;
    public static final int F_LAVA         = 1 << 1;
    public static final int F_SMOOTH_AO    = 1 << 2;   // renderQuadsSmooth (AO) path
    public static final int F_FLAT_AO      = 1 << 3;   // renderQuadsFlat (no AO) path
    public static final int F_OVERLAY      = 1 << 4;   // water-overlay side quad
    public static final int F_OVERWORLD    = 1 << 5;
    public static final int F_NETHER       = 1 << 6;
    public static final int F_END          = 1 << 7;
    public static final int F_SKY          = 1 << 8;
    public static final int F_BLOCK_LIGHT  = 1 << 9;
    public static final int F_HAS_SKYLIGHT = 1 << 10;
    public static final int F_NO_SKYLIGHT  = 1 << 11;
    public static final int F_NIGHTVISION  = 1 << 12;
    public static final int F_LIGHTNING    = 1 << 13;
    public static final int F_UVLOCK       = 1 << 14;
    public static final int F_PART_ROT     = 1 << 15;
    public static final int F_FLOW         = 1 << 16;
    public static final int F_STILL        = 1 << 17;
    public static final int F_UP_BACKFACE  = 1 << 18;
    public static final int F_DOWN         = 1 << 19;
    public static final int F_AO_CORNER    = 1 << 20;  // AO else-branch (translucent neighbor)
    public static final int F_TINT         = 1 << 21;
    public static final int F_GAMMA        = 1 << 22;
    public static final int F_BOSS         = 1 << 23;
    public static final int F_GRASS        = 1 << 24;
    public static final int F_FOLIAGE      = 1 << 25;
    public static final int F_WATER_BIOME  = 1 << 26;
    public static final int F_LIGHT_INC    = 1 << 27;
    public static final int F_LIGHT_DEC    = 1 << 28;
    public static final int F_FACE_RENDER  = 1 << 29;  // shouldSideBeRendered returned true
    public static final int F_FACE_CULL    = 1 << 30;  // shouldSideBeRendered returned false

    private static final ConcurrentHashMap<String, AtomicLong> COUNTS = new ConcurrentHashMap<String, AtomicLong>();
    private static volatile String file = DEFAULT_FILE;
    private static volatile boolean enabled = true;
    private static volatile int currentDim = 0;          // 0 overworld, -1 nether, 1 end
    private static volatile String currentDimName = "overworld";
    private static final AtomicLong totalHits = new AtomicLong();

    private CoverageLog() {}

    public static void setEnabled(boolean e) { enabled = e; }
    public static boolean isEnabled() { return enabled; }
    public static void setFile(String f) { if (f != null && !f.isEmpty()) file = f; }
    public static int getDimension() { return currentDim; }

    public static void setDimension(int id)
    {
        currentDim = id;
        currentDimName = dimName(id);
    }

    private static String dimName(int id)
    {
        if (id == 0) return "overworld";
        if (id == -1) return "nether";
        if (id == 1) return "end";
        return "dim" + id;
    }

    private static int dimFlag(int id)
    {
        if (id == 0) return F_OVERWORLD;
        if (id == -1) return F_NETHER;
        if (id == 1) return F_END;
        return 0;
    }

    public static void hit(String method, String name, int flags)
    {
        if (!enabled) return;
        String n = (name == null) ? "" : name;
        int f = flags | dimFlag(currentDim);
        String key = method + "\t" + n + "\t" + currentDimName + "\t" + f;
        AtomicLong c = COUNTS.get(key);
        if (c == null)
        {
            AtomicLong prev = COUNTS.putIfAbsent(key, new AtomicLong(1L));
            if (prev != null) prev.incrementAndGet();
        }
        else
        {
            c.incrementAndGet();
        }
        totalHits.incrementAndGet();
    }

    public static void hit(String method, String name) { hit(method, name, 0); }

    public static synchronized TreeMap<String, Long> snapshot()
    {
        TreeMap<String, Long> m = new TreeMap<String, Long>();
        for (Map.Entry<String, AtomicLong> e : COUNTS.entrySet()) m.put(e.getKey(), Long.valueOf(e.getValue().get()));
        return m;
    }

    public static synchronized int reset()
    {
        int n = COUNTS.size();
        COUNTS.clear();
        totalHits.set(0L);
        return n;
    }

    // Load an existing log back into COUNTS so counts survive a game restart
    // (fresh JVM). Called once at class init. Parses data lines (5 tab columns:
    // method name dim flags count); the first 4 columns are the exact key hit()
    // builds, so accumulation stays consistent.
    private static synchronized void loadExisting()
    {
        java.io.BufferedReader r = null;
        try
        {
            File f = new File(file);
            if (!f.exists()) return;
            r = new java.io.BufferedReader(new java.io.FileReader(f));
            String line;
            long loaded = 0L;
            while ((line = r.readLine()) != null)
            {
                if (line.isEmpty() || line.charAt(0) == '#') continue;
                String[] p = line.split("\t", 5);
                if (p.length != 5) continue;
                long c;
                try { c = Long.parseLong(p[4].trim()); } catch (NumberFormatException nfe) { continue; }
                String key = p[0] + "\t" + p[1] + "\t" + p[2] + "\t" + p[3];
                COUNTS.put(key, new AtomicLong(c));
                loaded += c;
            }
            totalHits.addAndGet(loaded);
        }
        catch (Throwable t)
        {
            System.out.println("[cov] loadExisting failed: " + t);
        }
        finally
        {
            if (r != null) try { r.close(); } catch (Throwable ig) {}
        }
    }

    public static synchronized long flush()
    {
        TreeMap<String, Long> m = snapshot();
        // Never clobber a populated log with an empty one (e.g. an idle/title-screen
        // JVM whose 2s flusher would otherwise erase a previous session's data).
        if (m.isEmpty()) return 0L;
        PrintWriter w = null;
        try
        {
            File f = new File(file);
            if (f.getParentFile() != null) f.getParentFile().mkdirs();
            w = new PrintWriter(new BufferedWriter(new FileWriter(f)));
            w.println("# render-opt coverage log");
            w.println("# columns: method <TAB> name <TAB> dim <TAB> flags <TAB> count");
            w.println("# total_hits=" + totalHits.get() + " unique_keys=" + m.size()
                + " dim=" + currentDimName + "(" + currentDim + ")");
            for (Map.Entry<String, Long> e : m.entrySet())
            {
                String[] parts = e.getKey().split("\t", 4);
                w.println(parts[0] + "\t" + parts[1] + "\t" + parts[2] + "\t" + parts[3] + "\t" + e.getValue());
            }
            w.flush();
        }
        catch (Throwable t)
        {
            System.out.println("[cov] flush failed: " + t);
        }
        finally
        {
            if (w != null) try { w.close(); } catch (Throwable ig) {}
        }
        return m.size();
    }

    static
    {
        loadExisting();   // accumulate across game restarts
        Thread t = new Thread(new Runnable()
        {
            public void run()
            {
                try { Thread.sleep(2000L); } catch (InterruptedException ie) {}
                while (true)
                {
                    try { Thread.sleep(2000L); } catch (InterruptedException ie) {}
                    try { flush(); } catch (Throwable g) {}
                }
            }
        }, "cov-flusher");
        t.setDaemon(true);
        t.start();
        Runtime.getRuntime().addShutdownHook(new Thread(new Runnable()
        {
            public void run() { enabled = false; try { flush(); } catch (Throwable g) {} }
        }));
    }
}
