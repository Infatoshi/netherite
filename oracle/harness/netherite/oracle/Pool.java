package netherite.oracle;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.io.PrintStream;
import java.lang.reflect.Field;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import net.minecraft.client.Minecraft;
import net.minecraft.client.renderer.texture.ITextureObject;
import net.minecraft.client.renderer.texture.TextureAtlasSprite;
import net.minecraft.client.renderer.texture.TextureManager;
import net.minecraft.client.renderer.texture.TextureMap;
import net.minecraft.client.renderer.texture.TextureUtil;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.world.WorldServer;

/**
 * The warm pool: one long-lived oracle JVM that records or replays one tape
 * after another, each byte-identical to what a fresh JVM writes.
 *
 * A pool JVM starts like any agent run (JVM, class loading, the client's
 * startGame) and, at the point where a fresh run would launch its world,
 * takes the pristine snapshot instead: every class the oracle compiles is
 * initialized (BiomeGenBase first, where a fresh run's WorldConf.pruneSpawns
 * initializes it, so its generators' Randoms are born from the same seeder
 * draws), and Rewind records the graph under every static field. It then
 * serves jobs on a local TCP port: a job is the argument list a fresh JVM
 * would get. Between jobs the integrated server is stopped (no save; a fresh
 * run halts without one), the client world dropped, the textures and
 * harness threads a job made are released, and Rewind writes the pristine
 * graph back. Det.reset in the next launch reseeds every stream.
 *
 * Only one class initializer draws from a Det seeder after a fresh run's
 * Det.reset: GuiScreen's (its static RenderItem makes a Random) when
 * launchIntegratedServer shows the JoinScreen. The pool has initialized
 * GuiScreen already, so JoinScreen makes that draw itself (joinScreenDraw).
 * Any other class whose initializer draws is found while the pool
 * initializes it; a class a fresh run initializes only after its reset would
 * then draw at a point the pool cannot reproduce, so the pool refuses every
 * job (the caller runs a fresh JVM) when one appears that is not listed here.
 *
 * A job the pool cannot run exactly (another harness, other options, a mode
 * or flag outside script and replay) is refused before anything runs, and
 * the caller falls back to a fresh JVM. A job that leaves the pool unable to
 * stop its server or quiesce its connection ends the pool. A job that throws
 * (fault: its command or launch, any thread, the JVM exiting under it) ends
 * with Oracle.FAULT_RC and a POOL-FAULT line naming the error, and the member
 * ends with it; the caller (tests/pool.sh) runs the job in a fresh JVM.
 */
public final class Pool
{
    private Pool() {}

    /** --pool DIR: this JVM is a pool member; DIR holds its port, pid and log. */
    static String dir;
    static boolean active() { return dir != null; }

    /** A capture has finished (or none started yet): the next commands() call tears down and takes the next job. */
    static volatile boolean cyclePending;
    /**
     * --pool-skip-reset WHAT, the negative control: "rewind" skips the whole
     * rewind between jobs; "Class.field" leaves that one field (and what it
     * holds) as the last job left it; "gl" keeps the last job's GL state (PoolGl).
     */
    static String skipReset;

    static ServerSocket server;
    static Socket client;
    static PrintStream out0, err0;
    /** The running job's output (to the caller and this member's log); null between jobs. */
    static volatile PrintStream jobOut;
    static int jobs;
    static long jobStart;
    static int lastRc;
    static String poolOptions;
    static int poolWidth, poolHeight;
    static String refuseAll;
    /** --pool-options TAPE: this member pins the options of TAPE's header (a mipmapped play profile). */
    static String optionsTape;
    /** The directory a fresh JVM would have run this job in (pool.sh's CWD). */
    static String jobCwd;
    /** This run's first JoinScreen has made the GuiScreen initializer's draw (JoinScreen). */
    static boolean joinDrawn;

    /** Classes whose initializer draws from a Det stream, and why that is exact. */
    static final String[] CLINIT_DRAWS = {
        // pre-reset in a fresh run too (WorldConf.pruneSpawns): initialized first, at the same draw
        "net.minecraft.world.biome.BiomeGenBase",
        // bootstrap, before the pristine point in every JVM
        "net.minecraft.client.renderer.entity.RenderManager", "net.minecraft.client.gui.GuiIngame",
        // the one post-reset initializer draw: JoinScreen repeats it (joinDrawn)
        "net.minecraft.client.gui.GuiScreen",
        // never initialized by an agent run: the stats screen opens from a GUI button (agent input is
        // window clicks), the enchanting GUI never opens (config.yaml enchanting is unsupported, forced off)
        "net.minecraft.client.gui.achievement.GuiStats", "net.minecraft.util.EnchantmentNameParts",
        // reached only through a script's probe command, which the pool refuses (Main.configurePool)
        "netherite.oracle.FeatureProbeTrees",
    };

    /**
     * Snapshot.detState: leave this split out of det.nbt, as a fresh JVM would.
     * A fresh JVM registers a split when its owner class is initialized; a pool
     * member initialized every class at start. So a member lists a split only
     * if its owner was initialized before the pristine point (as in a fresh
     * JVM before its world launch), or the run has drawn from it or touched its
     * owner (PoolAgent: a constructor or static method of the owner ran since
     * the run's world launch). A member without the agent lists every split.
     */
    static boolean absentSplit(Det.SplitRandom s)
    {
        if (!active()) return false;
        for (boolean u : s.used) if (u) return false;
        // ./net/minecraft/enchantment/EnchantmentHelper.java:enchantmentRand: net.minecraft.enchantment.EnchantmentHelper
        int j = s.name.indexOf(".java:");
        if (!s.name.startsWith("./") || j < 0) return false;
        String owner = s.name.substring(2, j).replace('/', '.');
        if (early.contains(owner)) return false;
        Boolean t = PoolAgent.touched(owner);
        return t != null && !t;
    }

    /** The classes initialized before Pool.init initialized the rest: a fresh JVM's at its world launch. */
    static final java.util.Set<String> early = new java.util.HashSet<String>();

    // ------------------------------------------------------------ pool start

    /** Oracle.onStartGame in a pool JVM: initialize everything, snapshot, and wait for the first job at the next frame. */
    static void init(Minecraft m) throws IOException
    {
        long t0 = System.nanoTime();
        File d = new File(dir);
        d.mkdirs();
        // a coverage member: what the JVM start ran is every job's (Coverage), each forced initializer's is kept (CovInit)
        boolean cov = CovInit.installed && Coverage.present();
        if (cov) Coverage.startup = Coverage.data(true);
        out0 = System.out;
        err0 = System.err;
        poolOptions = Main.optionsText;
        poolWidth = m.displayWidth;
        poolHeight = m.displayHeight;

        List<String> names = Rewind.classNames();
        List<Class<?>> before = Rewind.initialized(names);
        for (Class<?> c : before) early.add(c.getName());
        ClassLoader cl = Pool.class.getClassLoader();
        Det.clinitWatch = new java.util.LinkedHashSet<String>();
        if (cov) CovInit.begin();
        try
        {
            // where a fresh run first touches it (WorldConf.pruneSpawns), before any other draw
            net.minecraft.world.biome.BiomeGenBase.getBiomeGenArray();
            for (String n : names)
            {
                if (n.startsWith("netherite.oracle.Pool") || n.startsWith("netherite.oracle.Rewind")) continue;
                try { Class.forName(n, true, cl); }
                catch (Throwable t) { System.out.println("ORACLE POOL init skipped " + n + ": " + t); }
            }
        }
        finally
        {
            if (cov) CovInit.end();
            java.util.Set<String> drew = Det.clinitWatch;
            Det.clinitWatch = null;
            for (String c : drew)
            {
                boolean known = false;
                for (String k : CLINIT_DRAWS) if (k.equals(c)) known = true;
                boolean early = false;
                for (Class<?> b : before) if (b.getName().equals(c)) early = true;
                if (!known && !early)
                {
                    refuseAll = "class initializer draws from Det: " + c;
                    System.out.println("ORACLE POOL WARN " + refuseAll + " (every job goes to a fresh JVM)");
                }
            }
        }

        List<Class<?>> roots = new ArrayList<Class<?>>();
        for (Class<?> c : Rewind.initialized(names))
        {
            String n = c.getName();
            if (n.startsWith("netherite.oracle.Pool") || n.startsWith("netherite.oracle.Rewind")) continue;
            if (n.startsWith("netherite.oracle.Coverage") || n.startsWith("netherite.oracle.CovInit")) continue;
            roots.add(c);
        }
        Rewind.keep("netherite.oracle.Det.splits");
        Rewind.keep("netherite.oracle.Det.clientThread");
        Rewind.keep("netherite.oracle.RowHash.pool");
        Rewind.keep("netherite.oracle.ChunkIO.writer");
        // compiled once per model part, like a fresh JVM; rewinding them would leak a display list per part per job
        Rewind.keep("net.minecraft.client.model.ModelRenderer.compiled");
        Rewind.keep("net.minecraft.client.model.ModelRenderer.displayList");
        Rewind.keep("net.minecraft.client.renderer.GLAllocation.mapDisplayLists");
        if (skipReset != null && !"rewind".equals(skipReset) && !"gl".equals(skipReset))
        {
            Rewind.keep(skipReset);
            System.out.println("ORACLE POOL negative control: " + skipReset + " is never rewound");
        }
        Rewind.capture(roots);
        PoolGl.capture();
        pristineTextures = new HashMap<Object, Object>(textureMap(m));
        try { PoolMouse.record(); }
        catch (Exception e) { throw new IllegalStateException("pool: the LWJGL mouse", e); }
        try { recordArgs(); }
        catch (Exception e) { throw new IllegalStateException(e); }

        // a job the JVM exits under (System.exit after a client or server crash report) is a fault, not a closed connection
        Runtime.getRuntime().addShutdownHook(new Thread(new Runnable()
        {
            public void run() { if (jobOut != null) fault("the JVM is exiting during the job", null); }
        }, "Oracle Pool exit"));
        server = new ServerSocket(0, 50, InetAddress.getByName("127.0.0.1"));
        writeFile(new File(d, "harness"), Oracle.harness);
        writeFile(new File(d, "pid"), pid());
        writeFile(new File(d, "port"), Integer.toString(server.getLocalPort()));
        System.out.println("ORACLE POOL READY port=" + server.getLocalPort() + " in " + (System.nanoTime() - t0) / 1000000 + " ms");
        if (PoolAgent.active) System.out.println("ORACLE POOL agent watches " + PoolAgent.owners + ", initialized before the pool: " + early.size() + " classes");
        PoolAgent.clear();
        if (Coverage.startup != null) CovInit.keepGsons(roots);
        covReset();
        cyclePending = true;
    }

    /** A coverage member: the probes and touches so far are nobody's job. */
    static void covReset()
    {
        if (Coverage.startup == null) return;
        Coverage.data(true);
        CovInit.clear();
    }

    static String pid()
    {
        String n = java.lang.management.ManagementFactory.getRuntimeMXBean().getName();
        return n.substring(0, n.indexOf('@'));
    }

    static void writeFile(File f, String s) throws IOException
    {
        File tmp = new File(f.getPath() + ".tmp");
        FileOutputStream o = new FileOutputStream(tmp);
        o.write((s + "\n").getBytes("UTF-8"));
        o.close();
        if (!tmp.renameTo(f)) throw new IOException("pool: cannot write " + f);
    }

    // ------------------------------------------------------------ between jobs

    /** Oracle.commands, when cyclePending: end the last job's world, rewind, and launch the next job. */
    static void cycle(Minecraft m)
    {
        cyclePending = false;
        if (world) clean(m);
        collect();
        // a run already on its way (a caller running one after another) gets a world launched for it:
        // a prelaunch for another key would make it wait for two
        Socket early = prelaunch && lastArgs != null && refuseAll == null ? connection(GRACE_MS) : null;
        String earlyLine = null;
        // a session check's replay (--save-at-end) is followed by a run from the checkpoint it saved:
        // another key, so its own world is not prelaunched
        if (early == null && prelaunch && lastArgs != null && refuseAll == null && !Arrays.asList(lastArgs).contains("--save-at-end"))
        {
            // the last run's world, launched now, for a next run with the same key (seed, world, start)
            resetArgs();
            if (Main.configurePool(lastArgs) == null)
            {
                long t0 = System.nanoTime();
                try
                {
                    prelaunchKey = key();
                    applyOptions(m);
                    joinDrawn = false;
                    world = true;
                    prelaunching = true;
                    Oracle.launchWorld(m);
                    Oracle.startServer(m);
                    out0.println("ORACLE POOL prelaunched " + prelaunchKey.substring(0, Math.min(40, prelaunchKey.length())) + " in " + (System.nanoTime() - t0) / 1000000 + " ms");
                }
                catch (GiveWay g)
                {
                    out0.println("ORACLE POOL prelaunch given up after " + (System.nanoTime() - t0) / 1000000 + " ms: " + g.getMessage());
                    clean(m);
                }
                catch (Throwable t)
                {
                    t.printStackTrace(out0);
                    clean(m);
                }
                finally
                {
                    prelaunching = false;
                }
                early = arrived;
                earlyLine = arrivedLine;
                arrived = null;
                arrivedLine = null;
            }
        }
        while (true)
        {
            String[] args = accept(early != null ? early : connection(0), earlyLine);
            early = null;
            earlyLine = null;
            if (args == null) continue;
            resetArgs();
            String why = refuseAll != null ? refuseAll : Main.configurePool(args);
            if (why == null) why = optionsDiffer(Main.optionsText);
            if (why == null && (Main.width != poolWidth || Main.height != poolHeight)) why = "window size differs from the pool's";
            if (why != null)
            {
                refuse(why);
                continue;
            }
            ++jobs;
            try
            {
                if (world && prelaunchKey != null && prelaunchKey.equals(key()))
                {
                    ++hits;
                    Prof.tJob = jobStart;
                    prelaunchKey = null;
                    lastArgs = args;
                    out0.println("ORACLE POOL job " + jobs + " takes the prelaunched world (" + hits + " of " + jobs + ")");
                    if (Oracle.attach(m)) Oracle.announce();
                    mark("attached");
                    return;
                }
                if (world)
                {
                    clean(m);
                    resetArgs();
                    Main.configurePool(args);
                }
                applyOptions(m);
                lastArgs = args;
                joinDrawn = false;
                world = true;
                Prof.tJob = jobStart; // the clean above rewound it
                Oracle.launch(m);
                return;
            }
            catch (Throwable t)
            {
                fault("launch", t);
            }
        }
    }

    /** A prelaunch is waiting for its server (Oracle.launchWait polls here). */
    static boolean prelaunching;
    /** A job that arrived during the prelaunch: its connection and its line, taken by the cycle after it. */
    static Socket arrived;
    static String arrivedLine;

    /** Thrown out of the prelaunch's launchIntegratedServer: a job for another world arrived. */
    static final class GiveWay extends RuntimeException
    {
        GiveWay(String why) { super(why); }
    }

    /**
     * Oracle.launchWait during a prelaunch: a job that arrives now is read
     * at once. One that likely runs the prelaunched world (sameWorld) waits
     * for it as before; any other ends the prelaunch here (GiveWay), where
     * it used to wait for the whole spawn area of a world it then threw
     * away (1,226 of 1,540 prelaunches of master's members were not taken,
     * 1.9 s each on average, 2026-09-28). The server stops within a chunk
     * (initialWorldChunkLoad checks isServerRunning) and saves nothing
     * (teardown: levelSaving). The guess only decides whether to wait: the
     * job still takes the world only if its key() equals the prelaunch's.
     */
    static void launchWait()
    {
        if (!prelaunching || arrived != null || Det.serverThread == null) return;
        Socket s = connection(1);
        if (s == null) return;
        String line;
        try
        {
            s.setTcpNoDelay(true);
            line = new BufferedReader(new InputStreamReader(s.getInputStream(), "UTF-8")).readLine();
        }
        catch (IOException e) { line = null; }
        arrived = s;
        arrivedLine = line;
        String[] args = line == null ? null : argsOf(line);
        if (args == null || !sameWorld(args, lastArgs)) throw new GiveWay("a job for another world arrived");
    }

    static String[] argsOf(String line)
    {
        try
        {
            JsonArray a = Rows.parse(line).getAsJsonArray("args");
            String[] r = new String[a.size()];
            for (int i = 0; i < r.length; ++i) r[i] = a.get(i).getAsString();
            return r;
        }
        catch (Exception e) { return null; }
    }

    /**
     * Whether two runs' arguments likely launch the same world: the same
     * world-shaping flags (a config.yaml by its text), and for a replay the
     * same world fields in its tape header (HEADER_WORLD). A guess, and cheap: it reads a replay's first line and a
     * config file, never the statics the prelaunch's server is reading.
     */
    static boolean sameWorld(String[] a, String[] b)
    {
        return b != null && worldOf(a).equals(worldOf(b));
    }

    static final java.util.Set<String> WORLD_FLAGS = new java.util.HashSet<String>(Arrays.asList(
        "--play", "--agent", "--dev", "--from", "--conf", "--set", "--world", "--rd", "--particles", "--width", "--height", "--pool-options"));

    /** The replay header's fields a replay's world launch reads (Main.configure): not its harness, rows' format or setup. */
    static final String[] HEADER_WORLD = {"seed", "world", "save", "mode", "start", "options", "dev", "w", "h", "chat", "toasts", "tooltips"};

    static String worldOf(String[] args)
    {
        StringBuilder k = new StringBuilder();
        for (int i = 0; i < args.length; ++i)
        {
            String f = args[i];
            if ("--replay".equals(f) && i + 1 < args.length)
            {
                String h = null;
                try
                {
                    BufferedReader r = Rows.openRef(args[++i]);
                    try { h = r.readLine(); } finally { r.close(); }
                    JsonObject o = Rows.parse(h), w = new JsonObject();
                    for (String hf : HEADER_WORLD) if (o.has(hf)) w.add(hf, o.get(hf));
                    h = w.toString();
                }
                catch (Exception e) { h = "?" + args[i]; }
                k.append("replay ").append(h).append('\n');
                continue;
            }
            if (!WORLD_FLAGS.contains(f)) continue;
            boolean value = !"--play".equals(f) && !"--agent".equals(f) && !"--dev".equals(f);
            String v = value && i + 1 < args.length ? args[++i] : "";
            if ("--conf".equals(f))
            {
                try { v = new String(java.nio.file.Files.readAllBytes(java.nio.file.Paths.get(v)), "UTF-8"); }
                catch (Exception e) {}
            }
            k.append(f).append(' ').append(v).append('\n');
        }
        return k.toString();
    }

    /**
     * The options a client reads while it starts (the texture atlases'
     * mipmaps and filtering, the language, vsync, the framebuffer) are the
     * pool's for good; the rest (render distance, fancy graphics, particles,
     * smooth lighting, bobbing, clouds) are only read at run time, so a run
     * that pins other values gets them the way a fresh JVM does, from its
     * options.txt through GameSettings.loadOptions, over the rewound settings.
     */
    static final String[] STARTUP_OPTIONS = { "mipmapLevels", "anisotropicFiltering", "lang", "enableVsync", "fboEnable", "fullscreen", "overrideWidth", "overrideHeight" };

    static Map<String, String> optionMap(String text)
    {
        Map<String, String> o = new HashMap<String, String>();
        for (String l : text.split("\\R")) { int c = l.indexOf(':'); if (c > 0) o.put(l.substring(0, c), l.substring(c + 1)); }
        return o;
    }

    static String optionsDiffer(String text)
    {
        Map<String, String> a = optionMap(poolOptions), b = optionMap(text);
        if (!a.keySet().equals(b.keySet())) return "options.txt keys differ from the pool's";
        for (String k : STARTUP_OPTIONS)
        {
            String x = a.get(k), y = b.get(k);
            if (x == null ? y != null : !x.equals(y)) return "option " + k + " is read at client start: " + y + " here, " + x + " in the pool";
        }
        return null;
    }

    static void applyOptions(Minecraft m) throws IOException
    {
        if (Main.optionsText.equals(poolOptions)) return;
        File f = new File(m.mcDataDir, "options.txt");
        java.io.Writer w = new java.io.OutputStreamWriter(new FileOutputStream(f), "UTF-8");
        w.write(Main.optionsText);
        w.close();
        m.gameSettings.loadOptions();
        out0.println("ORACLE POOL options " + optionMap(Main.optionsText).get("renderDistance") + " (rd) for job " + jobs);
    }

    /** A world exists (a run's, or a prelaunched one): the next cycle stops it. */
    static boolean world;
    /** How long a finished run waits for the next one before it prelaunches. */
    static final int GRACE_MS = 300;
    /** --pool-cold: no prelaunch, every run launches its own world. */
    static boolean prelaunch = true;
    static String[] lastArgs;
    static String prelaunchKey;
    static int hits;

    /**
     * Between jobs, the jobs' garbage collected once it fills a third of the
     * old generation. A member runs -XX:+DisableExplicitGC (vanilla calls
     * System.gc() at every world swap), so nothing collected the old
     * generation until it was full: each job's world was promoted, the old
     * generation grew into new pages instead, and a member's RSS climbed 60
     * to 130 MB a job to --pool-max-mb (2500), where it retired after 6 to
     * 20 jobs (2,160 jobs of master's harness, 2026-09-28: 128 members
     * started) and its callers ran fresh JVMs while the next one started. A
     * member that let vanilla's collections run stayed near 1.1 GB. The
     * collection is the DiagnosticCommand's heap histogram (a full
     * collection that DisableExplicitGC does not stop), after the rewind, so
     * the last job is all garbage; collections never change what the oracle
     * computes (fresh JVMs collect at whatever point their heap fills).
     */
    static void collect()
    {
        java.lang.management.MemoryPoolMXBean old = null;
        for (java.lang.management.MemoryPoolMXBean p : java.lang.management.ManagementFactory.getMemoryPoolMXBeans())
            if (p.getType() == java.lang.management.MemoryType.HEAP && p.getName().contains("Tenured")) old = p;
        if (old == null) return;
        java.lang.management.MemoryUsage u = old.getUsage(), after = old.getCollectionUsage();
        // what the last collection left is live (a member keeps some of its jobs' worlds, a follow-up):
        // collect only when the garbage since then is a third of the old generation, else it would
        // collect every job for little
        long live = after == null ? 0L : after.getUsed();
        if (u.getUsed() - live < u.getCommitted() / 3) return;
        long t0 = System.nanoTime();
        try
        {
            java.lang.management.ManagementFactory.getPlatformMBeanServer().invoke(
                new javax.management.ObjectName("com.sun.management:type=DiagnosticCommand"), "gcClassHistogram",
                new Object[] {new String[0]}, new String[] {String[].class.getName()});
        }
        catch (Exception e)
        {
            out0.println("ORACLE POOL gc failed: " + e);
            return;
        }
        out0.println("ORACLE POOL gc old " + u.getUsed() / 1048576 + " -> " + old.getUsage().getUsed() / 1048576 + " MB of "
            + old.getUsage().getCommitted() / 1048576 + " in " + (System.nanoTime() - t0) / 1000000 + " ms, rss " + rssMb() + " MB");
    }

    /** Stops the world and rewinds, or ends the pool when that fails. */
    static void clean(Minecraft m)
    {
        world = false;
        prelaunchKey = null;
        try
        {
            teardown(m);
        }
        catch (Throwable t)
        {
            // the pool cannot promise a clean state any more: end it, callers fall back to fresh JVMs
            t.printStackTrace(out0);
            out0.println("ORACLE POOL END teardown failed: " + t);
            out0.flush();
            Runtime.getRuntime().halt(70);
        }
        if ("rewind".equals(skipReset)) out0.println("ORACLE POOL negative control: no rewind before job " + (jobs + 1));
        else rewind(m);
        PoolAgent.clear();
        covReset();
    }

    /**
     * What a launched world depends on, besides the pool's own options: a run
     * whose key equals the prelaunched world's takes that world. Everything
     * Oracle.launchWorld and the server start read is here; the command source
     * and the tape (Oracle.attach) come after and read nothing of the world.
     */
    static String key() throws IOException
    {
        StringBuilder k = new StringBuilder();
        k.append(Oracle.seed).append('|').append(WorldConf.json()).append('|').append(Oracle.worldName).append('|').append(Oracle.dev)
            .append('|').append(Oracle.quietJoinPin).append('|').append(Oracle.mode).append('|').append(Main.optionsText);
        if (Oracle.fromDir != null)
        {
            File f = new File(Oracle.fromDir);
            k.append('|').append(f.getCanonicalPath()).append('|').append(Oracle.fromTotalTime).append('|').append(Checkpoint.sha256(new File(f, "manifest.json")));
        }
        return k.toString();
    }

    /**
     * The statics a run's arguments set, as they were at the pristine point,
     * so each run's argument parse starts where a fresh JVM's does. Main and
     * WorldConf hold nothing else; Oracle's are listed.
     */
    static final String[] ORACLE_ARGS = {
        "mode", "seed", "worldName", "tapePath", "scriptPath", "replayPath", "framesDir", "harness", "fromDir", "checkpointRoot",
        "fromTotalTime", "quietJoinPin", "spawnerPin", "port", "frameEvery", "frameTicks", "detail", "dev", "showToasts", "showChat", "showTooltips", "framePt", "framePts", "hideGui",
        // the replay flags: a prelaunch parses the last run's arguments, so a --save-at-end would outlive its run
        "saveAtEnd", "saveNoPre", "snapTicks", "snapDir", "snapPlain", "detailFrom", "untilTick", "rerecordClient",
    };
    static Map<Field, Object> argsPristine;

    static void recordArgs() throws Exception
    {
        argsPristine = new HashMap<Field, Object>();
        for (Class<?> c : new Class<?>[] {Main.class, WorldConf.class})
        {
            for (Field f : c.getDeclaredFields())
            {
                int mod = f.getModifiers();
                if (!java.lang.reflect.Modifier.isStatic(mod) || java.lang.reflect.Modifier.isFinal(mod)) continue;
                f.setAccessible(true);
                argsPristine.put(f, f.get(null));
            }
        }
        for (String n : ORACLE_ARGS)
        {
            Field f = Oracle.class.getDeclaredField(n);
            f.setAccessible(true);
            argsPristine.put(f, f.get(null));
        }
    }

    static void resetArgs()
    {
        try
        {
            for (Map.Entry<Field, Object> e : argsPristine.entrySet()) e.getKey().set(null, e.getValue());
        }
        catch (IllegalAccessException e) { throw new IllegalStateException(e); }
        Oracle.detailNbt.clear();
        System.clearProperty("netherite.enbt"); // --detail-from sets it mid-run (Oracle.detailHere); Rows reads it per row
        Coverage.out = null;
    }

    /** The next job's argument list, with the client's output as this JVM's System.out and System.err. Null: try again. */
    /** A connection within ms milliseconds (0: wait for one), or null. */
    static Socket connection(int ms)
    {
        try
        {
            server.setSoTimeout(ms);
            return server.accept();
        }
        catch (java.net.SocketTimeoutException e) { return null; }
        catch (IOException e) { throw new IllegalStateException("pool: accept", e); }
    }

    static String[] accept(Socket s, String first)
    {
        try
        {
            s.setTcpNoDelay(true);
            String line = first != null ? first : new BufferedReader(new InputStreamReader(s.getInputStream(), "UTF-8")).readLine();
            if (line == null) { s.close(); return null; }
            JsonObject job = Rows.parse(line);
            jobCwd = job.has("cwd") ? job.get("cwd").getAsString() : dir;
            new File(jobCwd).mkdirs(); // a fresh run starts in it (make's mkdir -p), and resolves ../ through it
            client = s;
            jobStart = System.nanoTime();
            Prof.tJob = jobStart;
            final OutputStream so = s.getOutputStream();
            jobOut = new PrintStream(new OutputStream()
            {
                public void write(int b) throws IOException { write(new byte[] {(byte)b}, 0, 1); }
                public void write(byte[] b, int off, int len) throws IOException
                {
                    out0.write(b, off, len);
                    try { so.write(b, off, len); }
                    catch (IOException e) {} // the caller went away; the job still finishes
                }
                public void flush() { out0.flush(); try { so.flush(); } catch (IOException e) {} }
            }, true, "UTF-8");
            System.setOut(jobOut);
            System.setErr(jobOut);
            if (!job.has("harness") || !job.get("harness").getAsString().equals(Oracle.harness))
            {
                refuse("harness " + (job.has("harness") ? job.get("harness").getAsString() : "?") + " != pool " + Oracle.harness);
                return null;
            }
            JsonArray a = job.getAsJsonArray("args");
            String[] args = new String[a.size()];
            for (int i = 0; i < args.length; ++i) args[i] = a.get(i).getAsString();
            out0.println("ORACLE POOL JOB " + (jobs + 1) + " " + Arrays.toString(args));
            return args;
        }
        catch (Exception e)
        {
            out0.println("ORACLE POOL bad job: " + e);
            endJob(-1);
            return null;
        }
    }

    /** The pool log's timeline of a run: ms since its job arrived. */
    static void mark(String what)
    {
        out0.println("ORACLE POOL job " + jobs + " " + what + " at " + (System.nanoTime() - jobStart) / 1000000 + " ms");
    }

    static void refuse(String why)
    {
        out0.println("ORACLE POOL REFUSE " + why);
        if (jobOut != null) jobOut.println("POOL-REFUSE " + why);
        endJob(-1);
    }

    /**
     * --pool-max-mb N: a member that a job leaves above N MB resident ends
     * after that job (a member grows per job: one reached 4.6 GB against the
     * 3000 MB it reserved and was OOM-killed, 2026-09-26). Its callers then
     * start fresh JVMs (pool.sh: no live member).
     */
    static int maxMb = 2500;

    /** This JVM's resident set (VmRSS), MB; -1 where /proc has none (macOS). */
    static long rssMb()
    {
        try
        {
            for (String l : java.nio.file.Files.readAllLines(java.nio.file.Paths.get("/proc/self/status"), java.nio.charset.StandardCharsets.UTF_8))
                if (l.startsWith("VmRSS:")) return Long.parseLong(l.replaceAll("[^0-9]", "")) / 1024L;
        }
        catch (Exception e) {}
        return -1L;
    }

    /** Oracle.finish in a pool JVM: the tape is closed; tell the caller and wait for the teardown at the next frame. */
    static void jobDone(int rc)
    {
        lastRc = rc;
        long rss = rssMb();
        out0.println("ORACLE POOL job " + jobs + " rc=" + rc + " in " + (System.nanoTime() - jobStart) / 1000000 + " ms rss=" + rss + " MB");
        endJob(rc);
        if (rss > maxMb) retire(rss);
        cyclePending = true;
    }

    /**
     * The member ends between jobs: the last job's outputs are complete and
     * its caller has its rc. The port and pid files go first, so no caller
     * connects to a member that is going away.
     */
    static void retire(long rss)
    {
        new File(dir, "port").delete();
        new File(dir, "pid").delete();
        out0.println("ORACLE POOL END rss " + rss + " MB over --pool-max-mb " + maxMb + " after job " + jobs);
        out0.flush();
        Runtime.getRuntime().halt(0);
    }

    /**
     * Oracle.fault in a member: the job threw, so its outputs may be cut short
     * and the member's state is suspect. The job ends with FAULT_RC, the error
     * named on its output and a POOL-FAULT line (pool.sh then runs it in a
     * fresh JVM), and the member ends at once, its port and pid files first.
     * Between jobs (a prelaunched world's thread) the member only ends.
     */
    static synchronized void fault(String where, Throwable t)
    {
        try
        {
            new File(dir, "port").delete();
            new File(dir, "pid").delete();
            String what = t == null ? where : where + ": " + t;
            PrintStream o = jobOut;
            if (o != null)
            {
                o.println("ORACLE FAULT " + what);
                if (t != null) t.printStackTrace(o);
                o.println("POOL-FAULT " + what);
                endJob(Oracle.FAULT_RC);
            }
            else if (t != null) t.printStackTrace(out0);
            out0.println("ORACLE POOL END fault " + (o != null ? "in job " + jobs : "after job " + jobs) + ": " + what);
            out0.flush();
        }
        finally { Runtime.getRuntime().halt(Oracle.FAULT_RC); }
    }

    static void endJob(int rc)
    {
        if (jobOut != null)
        {
            jobOut.println("POOL-RC " + rc);
            jobOut.flush();
        }
        System.setOut(out0);
        System.setErr(err0);
        jobOut = null;
        if (client != null)
        {
            try { client.close(); }
            catch (IOException e) {}
            client = null;
        }
    }

    /**
     * Stops the job's integrated server and client world. The server exits
     * its lockstep loop at once (Lockstep.stopping) and skips the world save
     * (levelSaving), as a fresh run that halts never saves; the players' data
     * it still writes goes to the pool's own save, which the next launch
     * deletes.
     */
    static void teardown(Minecraft m) throws Exception
    {
        long t0 = System.nanoTime();
        IntegratedServer s = m.getIntegratedServer();
        Thread st = Det.serverThread;
        if (s != null && Lockstep.server == s)
        {
            RowHash.awaitSerialized();
            if (s.worldServers != null) for (WorldServer w : s.worldServers) if (w != null) w.levelSaving = true;
        }
        else if (s != null && s.worldServers != null)
        {
            // a prelaunch given up in its spawn area (launchWait): its server saves nothing either
            for (WorldServer w : s.worldServers) if (w != null) w.levelSaving = true;
        }
        Lockstep.stopping = true;
        m.loadWorld(null);
        if (s != null && st != null)
        {
            Lockstep.permit.release();
            st.join(60000L);
            if (st.isAlive()) throw new IllegalStateException("the integrated server did not stop");
        }
        else if (s != null) throw new IllegalStateException("an integrated server without its thread");
        ChunkIO.settle();
        quiesce(Lockstep.c2sSent, Lockstep.c2sRecv);
        quiesce(Lockstep.s2cSent, Lockstep.s2cRecv);
        Control.stop();
        Lockstep.permit.drainPermits();
        Lockstep.done.drainPermits();
        Lockstep.stopping = false;
        RowHash.quiesce();
        PoolMouse.restore();
        out0.println("ORACLE POOL teardown " + (System.nanoTime() - t0) / 1000000 + " ms");
    }

    /** Every packet sent on the old connection has been counted as received (netty delivers the last ones on its own threads). */
    static void quiesce(java.util.concurrent.atomic.AtomicLong sent, java.util.concurrent.atomic.AtomicLong recv) throws InterruptedException
    {
        long until = System.nanoTime() + 10000000000L;
        while (recv.get() < sent.get())
        {
            if (System.nanoTime() > until) throw new IllegalStateException("packets in flight after the server stopped: " + recv.get() + "/" + sent.get());
            Thread.sleep(1L);
        }
    }

    static Map<Object, Object> pristineTextures;

    @SuppressWarnings("unchecked")
    static Map<Object, Object> textureMap(Minecraft m)
    {
        try
        {
            Field f = TextureManager.class.getDeclaredField("mapTextureObjects");
            f.setAccessible(true);
            return (Map<Object, Object>)f.get(m.getTextureManager());
        }
        catch (Exception e) { throw new IllegalStateException(e); }
    }

    /** The heap rewind and the GL state that mirrors it (textures, animations, PoolGl's fixed-function state). */
    static void rewind(Minecraft m)
    {
        long t0 = System.nanoTime();
        // a texture the job loaded is dropped from the map by the rewind; free its GL name first
        for (Map.Entry<Object, Object> e : textureMap(m).entrySet())
        {
            if (pristineTextures.get(e.getKey()) != e.getValue()) TextureUtil.deleteTexture(((ITextureObject)e.getValue()).getGlTextureId());
        }
        Rewind.restore();
        resyncAnimations(m);
        String gl = PoolGl.restore();
        if (gl != null)
        {
            // the next job cannot start from a fresh JVM's GL state: end the pool, callers fall back to fresh JVMs
            out0.println("ORACLE POOL END " + gl);
            out0.flush();
            Runtime.getRuntime().halt(70);
        }
        out0.println("ORACLE POOL rewind " + (System.nanoTime() - t0) / 1000000 + " ms");
    }

    /**
     * An animated sprite's texels live in GL: the rewind puts its frame
     * counter back to 0, so frame 0 goes back into the atlas, as the stitch
     * uploaded it at startup.
     */
    static void resyncAnimations(Minecraft m)
    {
        try
        {
            Field uploaded = TextureMap.class.getDeclaredField("mapUploadedSprites");
            uploaded.setAccessible(true);
            for (Object o : textureMap(m).values())
            {
                if (!(o instanceof TextureMap)) continue;
                TextureMap tm = (TextureMap)o;
                org.lwjgl.opengl.GL11.glBindTexture(org.lwjgl.opengl.GL11.GL_TEXTURE_2D, tm.getGlTextureId());
                for (Object v : ((Map<?, ?>)uploaded.get(tm)).values())
                {
                    TextureAtlasSprite sp = (TextureAtlasSprite)v;
                    if (sp.getFrameCount() > 1)
                        TextureUtil.func_147955_a(sp.func_147965_a(0), sp.getIconWidth(), sp.getIconHeight(), sp.getOriginX(), sp.getOriginY(), false, false);
                }
            }
        }
        catch (Exception e) { throw new IllegalStateException(e); }
    }
}
