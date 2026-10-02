package netherite.oracle;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import com.google.gson.JsonPrimitive;
import java.io.File;
import java.io.IOException;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.GuiScreen;
import net.minecraft.client.settings.GameSettings;
import net.minecraft.client.shader.Framebuffer;
import net.minecraft.world.WorldSettings;
import net.minecraft.world.WorldType;

/**
 * Oracle harness entry points. Every call from patched vanilla code lands
 * here. Modes:
 *   PLAY   a human plays at wall-clock pace; each client tick is paired with
 *          one server tick; input is recorded as post-input effects.
 *   AGENT  ticks advance only on command (script, tape replay, or TCP);
 *          input comes from the command, never from the keyboard or mouse.
 */
public final class Oracle
{
    public static final int OFF = 0, PLAY = 1, AGENT = 2;

    public static int mode = OFF;
    public static long seed;
    public static String worldName = "oracle";
    public static String tapePath, scriptPath, replayPath, framesDir, harness = "";
    public static String fromDir, checkpointRoot;
    public static long fromTotalTime;
    // the quiet join pin: server entities other than players do not tick until
    // the player has joined; on in every new tape, a replay takes it from the header
    public static boolean quietJoinPin = true;
    /** Det.PIN_SPAWNER: the spawner display mob's head pinned (a tape's start.spawnerPin; new tapes on) */
    public static boolean spawnerPin = true;
    public static int port, frameEvery;
    /** --frame-ticks: a replay also captures the frame after each of these rows */
    static java.util.Set<Long> frameTicks;
    public static boolean detail; // --detail: per-entity rows for divergence forensics
    public static java.util.Set<Integer> detailNbt = new java.util.HashSet<Integer>(); // --nbt ID,ID: their whole NBT in the detail rows
    public static boolean dev;
    // --toasts: render the achievement toast (the four cleanHud hooks in
    // GuiAchievement and Minecraft.renderWorld). Default off keeps every
    // existing gate byte-identical.
    public static boolean showToasts;
    // --chat: draw GuiNewChat's lines (the GuiIngame hook). Default off keeps
    // every existing gate byte-identical; a tape made with it says so in its
    // header ("chat":true) and its replay draws the chat too.
    public static boolean showChat;
    // --tooltips: draw the held item's name over the hotbar after a change
    // (GuiIngame's toolHighlight, the GuiIngame hook), as the live client
    // does; default off keeps every existing frame byte-identical
    public static boolean showTooltips;
    // --frame-pt: the partial tick an agent frame is drawn at (1.0, the
    // tick's end; the frame judge's 0.5 is the live client's in-between
    // frame, interpolated from the tick's start to its end)
    public static float framePt = 1.0F;
    // --frame-pts A,B,...: each marked tick draws a frame at every partial tick
    // in turn, the first by the game loop, the rest again by postRender
    // (f_TTTTTT.png at 1.0, f_TTTTTT.pNNN.png at 0.NNN), so the frame judge's
    // in-between and end-of-tick frames come from one replay
    public static float[] framePts;
    // --hide-gui: vanilla's F1 (GameSettings.hideGUI) held from the start, the
    // observation without the HUD, the hand and the block outline (a screen
    // still draws itself and the HUD); a tape recorded with it says so and its
    // replay hides them too
    public static boolean hideGui;

    static Minecraft mc;
    /** Tick pairs completed since the world launched. Row t describes pair t. */
    public static long tick;

    static Act cur;       // agent: input for the tick in progress
    static Act rec;       // play: input being recorded for the tick in progress
    static boolean inInput;
    static boolean done;

    // agent step state
    static Act stepAct;
    static int stepsLeft;
    static boolean firstTick, frameAtEnd, needFrame, replyPending, frameOnly;
    static String frameReq;

    static int[] lastOpts;

    /* --snap-at T,T,... with --snap-dir DIR on a replay: a Snapshot of the
     * state at the head of each listed tick, into DIR/t<T> (the any-tick
     * sweep, tests/anytick.sh), taken where a script's Snapshot command is */
    static java.util.TreeSet<Long> snapTicks;
    public static String snapDir;
    /* --snap-plain: each --snap-at snapshot in the single-snapshot form a
     * script's Snapshot writes (chunks.bin.gz, plain text files), not over
     * the shared blob directory: a self-contained recording of a played tape */
    static boolean snapPlain;
    /* --rerecord-client on a replay: the client's own row fields (d.cw, d.cseed,
     * d.cmath, d.cstat and d.px) are written, not compared; any other field
     * still stops the replay. Re-records a played tape whose inputs are
     * unchanged after a pin moved only the client (lane/clientgate) */
    static boolean rerecordClient;

    static void snapAt(String list)
    {
        snapTicks = new java.util.TreeSet<Long>();
        for (String t : list.split(",")) if (!t.trim().isEmpty()) snapTicks.add(Long.parseLong(t.trim()));
    }

    /* --detail-from T: the --detail --enbt rows from tick T on only (the
     * divergence report's window, csrc/tests/diverge.sh); --until T: a
     * replay ends at the head of tick T (ORACLE REPLAY UNTIL) */
    static long detailFrom = -1, untilTick = -1;

    /* --save-at-end DIR on a replay: when the tape's rows run out, the
     * script's {"cmd":"save"} (a vanilla Save and Quit, then a copy of the
     * save) into DIR instead of a plain exit: a played session's end becomes
     * the next session's checkpoint (csrc/play/session.sh) */
    static String saveAtEnd;
    /* --save-no-pre: a checkpoint without its pre/ Snapshot (the roundtrip
     * gate's reference, Roundtrip.java): csrc/play/session.sh's saves,
     * whose next start is a Snapshot of its own (3 s of a 10 s replay) */
    static boolean saveNoPre;

    static void detailHere()
    {
        if (detailFrom < 0 || detail || tick < detailFrom) return;
        RowHash.awaitSerialized();
        detail = true;
        System.setProperty("netherite.enbt", "1");
    }

    static void snapshotHere(Minecraft m)
    {
        if (snapTicks == null || snapDir == null || !joined || !snapTicks.remove(tick)) return;
        RowHash.awaitSerialized();
        ChunkIO.settle();
        JsonObject c = new JsonObject();
        c.addProperty("out", new File(snapDir, "t" + tick).getPath());
        c.addProperty("overlay", true);
        if (!snapPlain) c.addProperty("blobs", new File(snapDir, "blobs").getPath());
        try
        {
            Snapshot.run(m.getIntegratedServer(), c);
        }
        catch (Exception e)
        {
            threw("snapshot at t=" + tick, e);
            e.printStackTrace();
            finish(2);
        }
    }
    static volatile boolean joined;

    private Oracle() {}

    public static boolean active() { return mode != OFF; }
    public static boolean agent() { return mode == AGENT; }
    public static boolean cleanHud() { return mode == PLAY || mode == AGENT; }
    public static boolean cleanHudToast() { return cleanHud() && !showToasts; }
    public static boolean cleanHudChat() { return cleanHud() && !showChat; }
    public static boolean cleanHudTooltip() { return cleanHud() && !showTooltips; }
    public static boolean quietJoin() { return active() && quietJoinPin && !joined; }

    /* The calendar pin: World.theCalendar (the date the bat spawn rule and
     * the zombie and skeleton Halloween pumpkins read, and the snapshot's
     * calMonth and calDay) is 2026-09-22 12:00 UTC while the oracle runs, not
     * the wall clock, so a recording made on another day (or across
     * midnight) is the same. Noon UTC is the 22nd in any time zone from
     * UTC-11 to UTC+11. None of 1.7.10's dated rules fire in September. */
    public static final long CALENDAR_MILLIS = 1790078400000L;

    public static long calendarMillis()
    {
        return active() ? CALENDAR_MILLIS : net.minecraft.server.MinecraftServer.getSystemTimeMillis();
    }

    /* The save clock pin: the wall clock vanilla writes into a save
     * (level.dat's LastPlayed, so level.dat_old's too; session.lock's
     * SaveHandler.initializationTime; each region file chunk's timestamp) is
     * CALENDAR_MILLIS while the oracle runs, so two runs that save the same
     * world (a checkpoint, SAVEEND) write the same bytes. None of it is game
     * state: the session lock only compares the file to the same field. */
    public static long saveClockMillis()
    {
        return calendarMillis();
    }

    public static java.util.Calendar calendar()
    {
        java.util.Calendar c = java.util.Calendar.getInstance();
        c.setTimeInMillis(calendarMillis());
        return c;
    }

    /* The tripwire pin: BlockTripWire.func_150140_e tests the entities in
     * the block singleton's bounds, which are whatever the last
     * setBlockBoundsBasedOnState on any tripwire left: the client's pick ray,
     * a mob's sight ray or an arrow on the server thread, a chunk render.
     * In vanilla that is a race between the two threads; while the oracle
     * runs the wire first takes the bounds of its own metadata (the native
     * engine's box). */
    public static void tripwireBounds(net.minecraft.block.Block wire, net.minecraft.world.World w, int x, int y, int z)
    {
        if (active()) wire.setBlockBoundsBasedOnState(w, x, y, z);
    }

    /* The anvil pin: BlockAnvil keeps Block's collision box, the block
     * singleton's bounds, which are whatever the last setBlockBoundsBasedOnState
     * on any anvil left: the client's pick ray, a chunk render, a ray on the
     * server thread. In vanilla that is a race between the two threads (the
     * server player's move against an anvil reads bounds the client thread
     * set); while the oracle runs the anvil's box is its own metadata's. */
    public static void anvilBounds(net.minecraft.block.Block b, net.minecraft.world.World w, int x, int y, int z)
    {
        if (b instanceof net.minecraft.block.BlockAnvil && active()) b.setBlockBoundsBasedOnState(w, x, y, z);
    }

    // ------------------------------------------------------------ startup

    /** End of Minecraft.startGame. Launches the pinned world instead of the main menu. */
    public static boolean onStartGame(Minecraft m)
    {
        if (!active()) return false;
        mc = m;
        Prof.tStarted = System.nanoTime();
        m.gameSettings.pauseOnLostFocus = false;
        if (hideGui) m.gameSettings.hideGUI = true;
        lastOpts = opts(m.gameSettings);
        if (mode == AGENT) faultUncaught();
        try
        {
            if (Pool.active()) Pool.init(m); // the pristine point: jobs launch from commands()
            else launch(m);
        }
        catch (IOException e)
        {
            throw new RuntimeException(e);
        }
        return true;
    }

    /** A run's world: the save, the Det streams, the command source and the tape, then the integrated server. */
    static void launch(Minecraft m) throws IOException
    {
        launchWorld(m);
        if (!attach(m)) return; // a replay header the oracle refuses: no tape, no world
        announce();
        startServer(m);
    }

    /** The save and the Det streams: everything the world depends on (a pool JVM prelaunches with these). */
    static void launchWorld(Minecraft m) throws IOException
    {
        Prof.tLaunch = System.nanoTime();
        File save = new File(new File(m.mcDataDir, "saves"), worldName);
        deleteTree(save);
        if (fromDir != null)
        {
            Checkpoint.install(new File(fromDir), save);
            Checkpoint.recordJoinWrites();
            BlockRands.restore(new File(new File(fromDir), "pre/det.nbt"));
        }
        WorldConf.pruneSpawns();
        Det.reset(fromDir == null ? seed : Det.mix(seed, fromTotalTime));
        Prof.tSave = System.nanoTime();
    }

    /** The run's command source and its tape. False when the run ended already. */
    static boolean attach(Minecraft m) throws IOException
    {
        if (mode == AGENT) Control.start();
        if (done) return false;
        Rows.open(header(m));
        return true;
    }

    static void announce()
    {
        System.out.println("ORACLE LAUNCH seed=" + seed + " mode=" + (mode == PLAY ? "play" : "agent"));
    }

    static void startServer(Minecraft m)
    {
        m.launchIntegratedServer(worldName, worldName, new WorldSettings(seed, WorldSettings.GameType.SURVIVAL, true, false, WorldType.DEFAULT));
        Prof.tServerUp = System.nanoTime();
    }

    /**
     * Minecraft.launchIntegratedServer's wait for the server's run loop, one
     * poll: vanilla sleeps 200 ms a poll, 0.1 s of every world launch on
     * average (0.13 to 0.27 s measured, lane/replaylat); the oracle polls every
     * 5 ms in agent mode. A poll changes nothing but the loading screen's
     * message, which agent mode never draws (its clock, Minecraft.getSystemTime,
     * is the tick's, 0 at a launch). A pool member's prelaunch gives way here
     * to a job for another world (Pool.launchWait).
     */
    public static void launchWait() throws InterruptedException
    {
        if (!agent())
        {
            Thread.sleep(200L);
            return;
        }
        if (Pool.active()) Pool.launchWait();
        Thread.sleep(5L);
    }

    static JsonObject header(Minecraft m)
    {
        JsonObject h = new JsonObject();
        h.addProperty("tape", "netherite-oracle");
        h.addProperty("v", dev ? 3 : 2);
        if (dev) h.addProperty("dev", true);
        if (showChat) h.addProperty("chat", true);
        // every row carries the S02 chat packets its server tick sent (Rows.s02)
        h.addProperty("s02", 1);
        if (showToasts) h.addProperty("toasts", true);
        if (showTooltips) h.addProperty("tooltips", true);
        if (hideGui) h.addProperty("hideGui", true);
        h.addProperty("mc", "1.7.10");
        h.addProperty("harness", harness);
        h.addProperty("seed", seed);
        h.add("world", WorldConf.json());
        h.addProperty("mode", mode == PLAY ? "play" : "agent");
        h.addProperty("save", worldName);
        JsonObject start = new JsonObject();
        start.addProperty("kind", fromDir == null ? "seed" : "checkpoint");
        start.addProperty("quietJoin", quietJoinPin);
        if (spawnerPin) start.addProperty("spawnerPin", true);
        if (fromDir != null)
        {
            start.addProperty("dir", new File(fromDir).getAbsolutePath());
            start.addProperty("manifestSha256", Checkpoint.sha256(new File(fromDir, "manifest.json")));
            start.addProperty("totalTime", fromTotalTime);
            start.addProperty("detReset", "mix(seed,totalTime)");
        }
        h.add("start", start);
        h.addProperty("w", m.displayWidth);
        h.addProperty("h", m.displayHeight);
        GameSettings g = m.gameSettings;
        JsonObject o = new JsonObject();
        o.addProperty("rd", g.renderDistanceChunks);
        o.addProperty("difficulty", g.difficulty.getDifficultyId());
        o.addProperty("gui", g.guiScale);
        o.addProperty("fancy", g.fancyGraphics ? 1 : 0);
        o.addProperty("particles", g.particleSetting);
        o.addProperty("ao", g.ambientOcclusion);
        o.addProperty("mip", g.mipmapLevels);
        o.addProperty("aniso", g.anisotropicFiltering);
        o.addProperty("fov", g.fovSetting);
        o.addProperty("gamma", g.gammaSetting);
        o.addProperty("bob", g.viewBobbing ? 1 : 0);
        o.addProperty("clouds", g.clouds ? 1 : 0);
        o.addProperty("sens", g.mouseSensitivity);
        h.add("options", o);
        if (replayPath != null && replaySetup != null) h.add("setup", replaySetup);
        return h;
    }

    static int[] opts(GameSettings g)
    {
        return new int[] {g.thirdPersonView, g.hideGUI ? 1 : 0, g.smoothCamera ? 1 : 0, g.renderDistanceChunks, g.showDebugInfo ? 1 : 0};
    }

    static void deleteTree(File f)
    {
        if (!f.exists()) return;
        File[] kids = f.listFiles();
        if (kids != null) for (File k : kids) deleteTree(k);
        f.delete();
    }

    // ------------------------------------------------------------ tick scheduling

    /**
     * True while ticksThisFrame runs its commands: a probe that ticks from a
     * command and reads Rows.lastRow gets each row written at once.
     */
    static boolean inFrameSetup;

    /** How many client ticks this frame runs. Agent mode blocks here for the next command. */
    public static int ticksThisFrame(Minecraft m, int elapsed)
    {
        if (mode != AGENT)
        {
            RawRec.frameStart(m); // play: the raw events the last Display.update polled
            return elapsed;
        }
        inFrameSetup = true;
        if (Prof.on) Prof.lap(Prof.FRAME);
        try
        {
            return commands(m);
        }
        finally
        {
            inFrameSetup = false;
            if (Prof.on) Prof.lap(Prof.CMD);
        }
    }

    static int commands(Minecraft m)
    {
        if (Pool.cyclePending)
        {
            Pool.cycle(m); // the last job ends here and the next one launches
            return 0;
        }
        if (done) return 0;
        if (replayPath != null)
        {
            if (!joined && ready(m)) joined = true;
            applyReplaySetup(m);
            snapshotHere(m);
            if (done) return 0;
            if (untilTick >= 0 && joined && tick >= untilTick)
            {
                System.out.println("ORACLE REPLAY UNTIL t=" + tick);
                finish(0);
                return 0;
            }
            detailHere();
            JsonObject row = Control.next();
            if (row == null)
            {
                System.out.println("ORACLE REPLAY OK ticks=" + tick);
                if (saveAtEnd != null)
                {
                    RowHash.awaitSerialized();
                    ChunkIO.settle();
                    try
                    {
                        File target = new File(saveAtEnd);
                        Checkpoint.save(m, target, target.getName());
                        finish(0);
                    }
                    catch (Exception e)
                    {
                        e.printStackTrace();
                        finish(2);
                    }
                    return 0;
                }
                finish(0);
                return 0;
            }
            Rows.Check.expect = replayInputsOnly ? null : row;
            Rows.Check.expectText = Control.lastLine;
            try { cur = Act.parseTape(row.has("act") ? row.getAsJsonObject("act") : null); }
            catch (IllegalArgumentException e)
            {
                Act.refuse("tape row: " + e.getMessage());
                finish(4);
                return 0;
            }
            // re-render every frame the reference captured, so pixels are checked too
            needFrame = (frameEvery > 0 && (tick + 1) % frameEvery == 0)
                || (row.has("d") && row.getAsJsonObject("d").has("px"))
                || (frameTicks != null && frameTicks.contains(tick));
            return 1;
        }
        if (stepsLeft == 0)
        {
            if (!joined && !ready(m))
            {
                cur = null;
                needFrame = false;
                return 1;
            }
            if (!joined)
            {
                joined = true;
                System.out.println("ORACLE READY t=" + tick);
                if (Pool.active()) Pool.mark("ready");
            }
            while (true)
            {
                if (done) return 0; // a run class ended the run (GoldBot)
                JsonObject c = Control.next();
                if (c == null)
                {
                    System.out.println("ORACLE SCRIPT DONE ticks=" + tick);
                    finish(0);
                    return 0;
                }
                String kind = c.has("cmd") ? c.get("cmd").getAsString() : "step";
                if (!readOnly(kind, c))
                {
                    // a command may change the server or read its save: the
                    // held row's entities are serialized, and every chunk
                    // write is on disk
                    RowHash.awaitSerialized();
                    ChunkIO.settle();
                }
                if ("quit".equals(kind))
                {
                    Control.reply(ok());
                    finish(0);
                    return 0;
                }
                if ("save".equals(kind))
                {
                    try
                    {
                        String name = c.has("name") ? c.get("name").getAsString() : "checkpoint";
                        File target = c.has("out") ? new File(c.get("out").getAsString())
                            : new File(new File(checkpointRoot, Long.toString(seed)), name);
                        Checkpoint.save(m, target, name);
                        finish(0);
                    }
                    catch (Exception e)
                    {
                        threw("save", e);
                        e.printStackTrace();
                        finish(2);
                    }
                    return 0;
                }
                if ("state".equals(kind))
                {
                    Rows.flush();
                    JsonObject r = ok();
                    r.add("row", Rows.lastRow);
                    Control.reply(r);
                    continue;
                }
                if ("run".equals(kind))
                {
                    // Any probe class: netherite.oracle.<class>.run(IntegratedServer, JsonObject)
                    // returns its result. It runs on this (client) thread between frames and
                    // starts its own thread if it must not; a new probe needs no edit here.
                    JsonObject r;
                    String cls = c.has("class") ? c.get("class").getAsString() : "?";
                    try
                    {
                        r = ok();
                        Class<?> klass = Class.forName("netherite.oracle." + cls);
                        boolean mutates = mutates(klass);
                        if (mutates && !dev && !legacySetup(cls))
                            throw new IllegalStateException(cls + " requires --dev");
                        java.lang.reflect.Method run = klass
                            .getDeclaredMethod("run", net.minecraft.server.integrated.IntegratedServer.class, JsonObject.class);
                        run.setAccessible(true);
                        Object out = run.invoke(null, m.getIntegratedServer(), c);
                        r.add("run", (JsonObject)out);
                        // a world-setup command changes the state the tape's rows
                        // describe; it goes in the header so a replay repeats it
                        if (mutates) Rows.noteSetup(cls, c, tick);
                        if (mutates) Lockstep.settleSetup();
                    }
                    catch (java.lang.reflect.InvocationTargetException e)
                    {
                        threw("run " + cls, e.getCause());
                        r = Control.error(cls + ": " + e.getCause());
                        e.getCause().printStackTrace();
                    }
                    catch (Exception e)
                    {
                        threw("run " + cls, e);
                        r = Control.error(cls + ": " + e);
                    }
                    System.out.println("ORACLE RUN " + r);
                    Control.reply(r);
                    continue;
                }
                if ("dev".equals(kind))
                {
                    if (!dev) { Control.reply(Control.error("dev requires --dev")); continue; }
                    try
                    {
                        Dev.validate(c);
                        Rows.noteSetup("Dev", c, tick);
                        Dev.queue(tick, c);
                        Control.reply(ok());
                    }
                    catch (Exception e) { Control.reply(Control.error("dev: " + e)); }
                    continue;
                }
                if ("chunks".equals(kind))
                {
                    JsonObject r;
                    try
                    {
                        r = ok();
                        r.add("dump", ChunkDump.run(m.getIntegratedServer(), c));
                    }
                    catch (Exception e)
                    {
                        r = Control.error("chunks: " + e);
                    }
                    System.out.println("ORACLE CHUNKS " + r);
                    Control.reply(r);
                    continue;
                }
                if ("probe".equals(kind))
                {
                    JsonObject r;
                    try
                    {
                        r = ok();
                        r.add("probe", Probe.run(m.getIntegratedServer(), c));
                    }
                    catch (Exception e)
                    {
                        r = Control.error("probe: " + e);
                    }
                    System.out.println("ORACLE PROBE " + r);
                    Control.reply(r);
                    continue;
                }
                if ("structures".equals(kind))
                {
                    JsonObject r;
                    try
                    {
                        r = ok();
                        r.add("dump", StructuresProbe.run(m.getIntegratedServer(), c));
                    }
                    catch (Exception e)
                    {
                        r = Control.error("structures: " + e);
                    }
                    System.out.println("ORACLE STRUCTURES " + r);
                    Control.reply(r);
                    continue;
                }
                if ("frame".equals(kind))
                {
                    frameReq = c.has("path") ? c.get("path").getAsString() : null;
                    frameOnly = true;
                    needFrame = true;
                    replyPending = true;
                    return 0;
                }
                if ("step".equals(kind))
                {
                    try { stepAct = Act.parseAgent(c.has("act") ? c.getAsJsonObject("act") : null); }
                    catch (IllegalArgumentException e)
                    {
                        /* a value the vanilla client cannot produce: the
                         * step is refused whole, nothing ticks */
                        Act.refuse("act: " + e.getMessage());
                        Control.reply(Control.error("act: " + e.getMessage()));
                        continue;
                    }
                    stepsLeft = c.has("n") ? Math.max(1, c.get("n").getAsInt()) : 1;
                    frameAtEnd = c.has("frame") && !(c.get("frame").isJsonPrimitive() && c.get("frame").getAsJsonPrimitive().isBoolean() && !c.get("frame").getAsBoolean());
                    frameReq = frameAtEnd && c.get("frame").getAsJsonPrimitive().isString() ? c.get("frame").getAsString() : null;
                    firstTick = true;
                    break;
                }
                Control.reply(Control.error("unknown cmd " + kind));
            }
        }
        cur = firstTick ? stepAct.copyAgent() : stepAct.holdOnly();
        firstTick = false;
        --stepsLeft;
        needFrame = (stepsLeft == 0 && frameAtEnd) || (frameEvery > 0 && (tick + 1) % frameEvery == 0);
        replyPending = stepsLeft == 0;
        return 1;
    }

    /**
     * A command that neither changes the server nor reads its save as files:
     * a step, and the golden bot's per-tick run, which reads loaded chunks and
     * entities only (GoldBot.chunk: nothing loads a chunk). It runs while the
     * RowHash workers serialize the held row's entities, which only read them
     * too, as the client's own tick does; Lockstep waits for them before the
     * server's next tick. Every other command may change the server or
     * snapshot or copy the save.
     */
    static boolean readOnly(String kind, JsonObject c)
    {
        return "step".equals(kind) || ("run".equals(kind) && c.has("class") && "GoldBot".equals(c.get("class").getAsString()));
    }

    /** Every mutating run class declares MUTATES; legacy setup remains playable. */
    static boolean mutates(Class<?> klass) throws Exception
    {
        try { return klass.getDeclaredField("MUTATES").getBoolean(null); }
        catch (NoSuchFieldException e) { return false; }
    }

    static boolean legacySetup(String cls)
    {
        // PreloadRegion prepares a save through the vanilla provider; the
        // later snapshot has no setup entry and is safe for the product pool.
        return "MobFree".equals(cls) || "MobMove".equals(cls) || "PreloadRegion".equals(cls);
    }

    /** The replay header's setup entries, applied in order at their recorded tick. */
    static JsonArray replaySetup;
    static boolean replayInputsOnly;
    static int replaySetupNext;

    static void onReplayHeader(JsonObject h)
    {
        replaySetup = h.has("setup") && h.get("setup").isJsonArray() ? h.getAsJsonArray("setup") : null;
        replaySetupNext = 0;
        dev = h.has("dev") && h.get("dev").getAsBoolean();
        Rows.Check.refS02 = h.has("s02");
        if (replaySetup != null) for (int i = 0; i < replaySetup.size(); ++i)
        {
            String cls = replaySetup.get(i).getAsJsonObject().get("class").getAsString();
            if ((!dev && !legacySetup(cls)) || ("Dev".equals(cls) && h.has("mode") && "play".equals(h.get("mode").getAsString())))
            {
                System.err.println("REFUSE dev entry in a non-dev tape");
                finish(4);
            }
        }
        // a tape played on the native client carries only its inputs: replay
        // them and record this client's own rows, with nothing to compare
        replayInputsOnly = h.has("inputs_only") && h.get("inputs_only").getAsBoolean();
    }

    /**
     * A setup command the recording ran between the join and the first replayed
     * row is re-run here, on the client thread between frames, at the tick it
     * was recorded with: the tick count at the head of row t is t, so the state
     * the row's tick sees is the one the recording's tick saw.
     */
    static void applyReplaySetup(Minecraft m)
    {
        if (replaySetup == null) return;

        while (replaySetupNext < replaySetup.size())
        {
            JsonObject e = replaySetup.get(replaySetupNext).getAsJsonObject();
            long at = e.has("tick") ? e.get("tick").getAsLong() : 0;

            if (tick < at) return;

            RowHash.awaitSerialized(); // the setup may change the entities the held row reads
            ChunkIO.settle();
            ++replaySetupNext;
            String cls = e.get("class").getAsString();
            JsonObject c = e.has("cmd") ? Rows.parse(e.get("cmd").toString()) : new JsonObject();
            if ("Dev".equals(cls))
            {
                Dev.queue(at, c);
                continue;
            }
            c.addProperty("cmd", "run");
            c.addProperty("class", cls);
            try
            {
                java.lang.reflect.Method run = Class.forName("netherite.oracle." + cls)
                    .getDeclaredMethod("run", net.minecraft.server.integrated.IntegratedServer.class, JsonObject.class);
                run.setAccessible(true);
                System.out.println("ORACLE REPLAY SETUP " + cls + " t=" + tick + " " + run.invoke(null, m.getIntegratedServer(), c));
                Lockstep.settleSetup();
            }
            catch (Exception ex)
            {
                threw("replay setup " + cls, ex);
                System.out.println("ORACLE REPLAY SETUP " + cls + " failed: " + ex);
                finish(3);
            }
        }
    }

    static boolean ready(Minecraft m)
    {
        return m.thePlayer != null && m.theWorld != null && m.currentScreen == null
            && Lockstep.server != null && !Lockstep.server.getConfigurationManager().playerEntityList.isEmpty();
    }

    public static void preTick(Minecraft m)
    {
        RenderStateProbe.tickStart();
        // right after MinecraftServer.initialWorldChunkLoad, before the first
        // server tick: the world the server has before its join (--spawndump)
        if (SpawnDump.dir != null && !SpawnDump.done && tick == 0 && Lockstep.server != null) SpawnDump.early(m);
        if (SpawnDumpDim.dir != null && !SpawnDumpDim.done && tick == 0 && Lockstep.server != null) SpawnDumpDim.early(m);

        if (mode == PLAY)
        {
            rec = Act.captureLook(m);
            rec.gui = new JsonArray();
            RawRec.tickStart();
        }
        else if (mode == AGENT && cur != null)
        {
            cur.applyLook(m);
        }
    }

    public static void postTick(Minecraft m)
    {
        if (mode == OFF) return;
        if (Prof.on) Prof.lap(Prof.CLIENT);
        if (poolUnwind()) return;
        Lockstep.pair(m);
        if (poolUnwind()) return; // the server thread may have ended the run (Dev)
        if (mode == PLAY && !joined && ready(m))
        {
            joined = true;
            System.out.println("ORACLE READY t=" + tick);
        }
        if (mode == AGENT) m.ingameGUI.oracleTickVignette();
        JsonObject row = Rows.capture(m, mode == PLAY ? rec : cur);
        CwDiff.maybe(m, tick);
        ++tick;
        rec = null;
        if (mode == AGENT && needFrame)
        {
            Rows.pending = row;
            if (Prof.on) Prof.lap(Prof.EMIT);
            return;
        }
        Rows.emit(row);
        if (mode == AGENT && replyPending && replayPath == null)
        {
            replyPending = false;
            // a script has no reader for the reply: its row stays held until
            // the next capture, and its digests are computed meanwhile
            if (Control.out != null)
            {
                Rows.flush();
                JsonObject r = ok();
                r.add("row", row);
                Control.reply(r);
            }
        }
        if (Prof.on) Prof.lap(Prof.EMIT);
    }

    // ------------------------------------------------------------ rendering

    public static boolean wantRender()
    {
        return (mode != AGENT || needFrame) && !done;
    }

    static File frameFile(long t, float pt)
    {
        if (pt >= 1.0F) return new File(framesDir, String.format("f_%06d.png", t));
        return new File(framesDir, String.format("f_%06d.p%03d.png", t, Math.round(pt * 1000.0F)));
    }

    /** The game loop's frame once more at another partial tick (Minecraft.runGameLoop's render block). */
    static void renderAgain(Minecraft m, Framebuffer fb, float pt)
    {
        net.minecraft.util.Timer timer;
        try
        {
            java.lang.reflect.Field tf = Minecraft.class.getDeclaredField("timer");
            tf.setAccessible(true);
            timer = (net.minecraft.util.Timer)tf.get(m);
        }
        catch (Exception e)
        {
            throw new RuntimeException(e);
        }
        float was = timer.renderPartialTicks;
        timer.renderPartialTicks = pt;
        org.lwjgl.opengl.GL11.glPushMatrix();
        org.lwjgl.opengl.GL11.glClear(org.lwjgl.opengl.GL11.GL_COLOR_BUFFER_BIT | org.lwjgl.opengl.GL11.GL_DEPTH_BUFFER_BIT);
        fb.bindFramebuffer(true);
        org.lwjgl.opengl.GL11.glEnable(org.lwjgl.opengl.GL11.GL_TEXTURE_2D);
        if (!m.skipRenderWorld) m.entityRenderer.updateCameraAndRender(pt);
        org.lwjgl.opengl.GL11.glFlush();
        if (!cleanHud() || showToasts) m.guiAchievement.func_146254_a();
        fb.unbindFramebuffer();
        org.lwjgl.opengl.GL11.glPopMatrix();
        org.lwjgl.opengl.GL11.glPushMatrix();
        fb.framebufferRender(m.displayWidth, m.displayHeight);
        org.lwjgl.opengl.GL11.glPopMatrix();
        timer.renderPartialTicks = was;
    }

    /** Before Display.update, after the frame was drawn into the framebuffer. */
    public static void postRender(Minecraft m, Framebuffer fb)
    {
        if (mode == PLAY) RawRec.frameEnd(m);
        if (mode != AGENT || !needFrame) return;
        needFrame = false;
        File f = null;
        long ft = Math.max(0L, tick - 1);
        if (frameReq != null) f = new File(frameReq);
        else if (framesDir != null) f = frameFile(ft, framePt);
        // the later partial ticks are drawn even when no PNG is written: a draw
        // moves what the next frame shows (EntityFX.interpPos under a pickup's
        // flight), so a replay of goldens drawn at 1.0 then 0.5 draws both
        // (coverage.sh's cf- recipe for idx-pickcap-s1)
        boolean more = frameReq == null && framePts != null;
        frameReq = null;
        String px;
        try
        {
            px = Frames.capture(m, fb, f);
            for (int i = 1; more && i < framePts.length; ++i)
            {
                renderAgain(m, fb, framePts[i]);
                Frames.capture(m, fb, framesDir != null ? frameFile(ft, framePts[i]) : null);
            }
        }
        catch (IOException e)
        {
            throw new RuntimeException(e);
        }
        JsonObject row = Rows.pending;
        if (row != null)
        {
            Rows.pending = null;
            row.getAsJsonObject("d").addProperty("px", px);
            if (f != null) row.addProperty("frame", f.getPath());
            Rows.emit(row);
        }
        if (replyPending && replayPath == null)
        {
            replyPending = false;
            Rows.flush();
            JsonObject r = ok();
            if (row != null) r.add("row", row);
            r.addProperty("px", px);
            if (f != null) r.addProperty("frame", f.getPath());
            Control.reply(r);
        }
        frameOnly = false;
    }

    // ------------------------------------------------------------ input hooks

    /** Replaces currentScreen.handleInput() when it returns true. */
    public static boolean guiInput(Minecraft m)
    {
        if (mode == AGENT)
        {
            /* the GUI judge's raw events: vanilla handleInput reads them */
            if (RawGui.active() && RawGui.inject(m, tick)) return false;
            if (cur != null) cur.applyGui(m);
            return true;
        }
        if (mode == PLAY)
        {
            inInput = true;
            RawRec.input(m, true);
        }
        return false;
    }

    public static void endGuiInput()
    {
        inInput = false;
        if (mode == PLAY) RawRec.inputEnd();
        RawGui.recording = false;
    }

    /** Replaces the vanilla mouse loop, leftClickCounter decrement and keyboard loop when it returns true. */
    public static boolean gameInput(Minecraft m)
    {
        if (mode == AGENT)
        {
            if (cur == null || (replayPath != null && cur.keys == null))
            {
                if (replayPath != null)
                {
                    Rows.flush(); // an earlier row that differs is the first divergence
                    System.out.println("ORACLE DIVERGE t=" + tick + " input block ran but the tape has no input snapshot");
                    finish(3);
                }
                if (m.leftClickCounter > 0) --m.leftClickCounter;
                return true;
            }
            cur.applyInput(m);
            cur.applyOpts(m);
            if (hideGui) m.gameSettings.hideGUI = true;
            return true;
        }
        if (mode == PLAY)
        {
            inInput = true;
            RawRec.input(m, false);
        }
        return false;
    }

    /** Play mode: right after the vanilla keyboard loop. */
    public static void endGameInput(Minecraft m)
    {
        inInput = false;
        if (mode == PLAY) RawRec.inputEnd();
        if (mode != PLAY || rec == null) return;
        rec.captureSnapshot(m);
        int[] o = opts(m.gameSettings);
        if (!java.util.Arrays.equals(o, lastOpts))
        {
            JsonObject j = new JsonObject();
            j.addProperty("tpv", o[0]);
            j.addProperty("hide", o[1]);
            j.addProperty("smooth", o[2]);
            j.addProperty("rd", o[3]);
            j.addProperty("dbg", o[4]);
            rec.opts = j;
            lastOpts = o;
        }
    }

    /** Replaces GuiScreen.isCtrlKeyDown() for the drop key, so drop-stack replays exactly. */
    public static boolean ctrlDown()
    {
        if (mode == AGENT) return cur != null && cur.ctrl != 0;
        if (mode == PLAY && rec != null && rec.snap) return rec.ctrl != 0;
        return GuiScreen.isCtrlKeyDown();
    }

    public static void onWindowClick(int window, int slot, int button, int clickMode)
    {
        if (mode == AGENT && RawGui.recording) RawGui.record(op("click", window, slot, button, clickMode));
        if (mode != PLAY) return;
        if (rec == null || !inInput)
        {
            System.out.println("ORACLE WARN windowClick outside the input phase t=" + tick);
            return;
        }
        rec.gui.add(op("click", window, slot, button, clickMode));
    }

    public static void onGuiOp(String kind)
    {
        if (mode == AGENT && RawGui.recording) RawGui.record(op(kind));
        if (mode != PLAY) return;
        if (rec == null || !inInput)
        {
            System.out.println("ORACLE WARN " + kind + " outside the input phase t=" + tick);
            return;
        }
        rec.gui.add(op(kind));
    }

    // ------------------------------------------------------------ helpers

    static JsonArray op(String kind, int... args)
    {
        JsonArray a = new JsonArray();
        a.add(new JsonPrimitive(kind));
        for (int v : args) a.add(new JsonPrimitive(v));
        return a;
    }

    static JsonObject ok()
    {
        JsonObject o = new JsonObject();
        o.addProperty("ok", true);
        o.addProperty("t", tick);
        return o;
    }

    static boolean shutdownDone;

    public static synchronized void onShutdown()
    {
        if (!active() || shutdownDone) return;
        shutdownDone = true;
        Rows.close();
        Prof.report();
        RawRec.close();
        System.out.println("ORACLE SHUTDOWN ticks=" + tick);
        // a pool member ending (its watchdog's SIGTERM) is no job's end: its last job's file stays as that job wrote it
        if (!Pool.active()) Coverage.dump();
    }

    /** The exit status of a run that threw what it cannot answer for (fault). */
    static final int FAULT_RC = 71;

    /**
     * A run's command threw: in a pool member any Throwable is a fault (a
     * fresh JVM might not have thrown it: the member's heap, threads or
     * files may be why); in a fresh JVM an Error is (an OutOfMemoryError in
     * a Snapshot worker, wrapped or not), and an Exception goes on as before
     * (the command's error reply).
     */
    static void threw(String where, Throwable t)
    {
        if (Pool.active() || hasError(t)) fault(where, t);
    }

    static boolean hasError(Throwable t)
    {
        for (int i = 0; t != null && i < 32; ++i, t = t.getCause()) if (t instanceof Error) return true;
        return false;
    }

    /**
     * The run cannot vouch for its outputs (a snapshot or save may be cut
     * short): it ends at once with FAULT_RC and the error named on its
     * output, never with rc 0. A pool member tells its caller, which runs
     * the job again in a fresh JVM, and retires (Pool.fault). Never returns.
     */
    static void fault(String where, Throwable t)
    {
        if (Pool.active()) Pool.fault(where, t);
        synchronized (Oracle.class)
        {
            try
            {
                System.out.println("ORACLE FAULT " + where + (t == null ? "" : ": " + t));
                if (t != null) t.printStackTrace(System.out);
                System.out.flush();
            }
            finally { Runtime.getRuntime().halt(FAULT_RC); }
        }
    }

    /** Agent runs: a Throwable no thread catches is a fault (a dead worker or snapshot thread would leave a run that hangs or answers without its output). */
    static void faultUncaught()
    {
        Thread.setDefaultUncaughtExceptionHandler(new Thread.UncaughtExceptionHandler()
        {
            public void uncaughtException(Thread th, Throwable e) { fault("uncaught in thread " + th.getName(), e); }
        });
    }

    /**
     * A pool member's job has reported its rc. Rows.close compares the last
     * rows, and a divergence there finishes the run with rc 3 from inside
     * finish(0): a fresh JVM halts in that inner call, so a member's outer
     * call stops here instead of reporting the job a second time (it did:
     * "job 8 rc=3" then "rc=0"). The rewind puts it back to false.
     */
    static boolean finished;

    static void finish(int rc)
    {
        done = true;
        Rows.close();
        if (finished) return;
        finished = true;
        Prof.report();
        RawRec.close();
        System.out.println("ORACLE DONE rc=" + rc + " ticks=" + tick);
        Coverage.dump();
        System.out.flush();
        if (Pool.active())
        {
            Pool.jobDone(rc);
            return;
        }
        Runtime.getRuntime().halt(rc);
    }

    /**
     * A pool JVM's finish returns instead of halting; the client thread
     * skips the rest of the tick's harness work here. An agent frame runs at
     * most one tick (commands), and a finished run draws no frame
     * (wantRender), so the next frame's commands() tears the world down.
     * It threw a MinecraftError until lane/poolleak: Minecraft.run catches
     * that outside its loop and shuts the client down, so a member ended
     * after every run that finished inside a tick (a replay's input block
     * without its snapshot, a Dev op that ends the run).
     */
    static boolean poolUnwind()
    {
        return done && Pool.active();
    }
}
