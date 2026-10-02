package netherite.oracle;

import com.google.gson.JsonObject;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;

/**
 * Oracle launcher. Pins options.txt, fixes the session identity, then runs the
 * vanilla client main on this thread.
 *
 *   --play | --agent        mode (required)
 *   --conf PATH             the world config (config.yaml at the repo root, or configs/NAME.yaml)
 *   --set key=value         override one config key (section.key, or a bare world key); repeatable
 *   --tape PATH             write the tape here
 *   --script PATH           agent: JSONL commands
 *   --replay PATH           agent: replay a tape and stop at the first difference
 *   --port N                agent: TCP control port on 127.0.0.1
 *   --frames DIR            agent: where frames go
 *   --frame-every N         agent: capture a frame every N ticks
 *   --frame-ticks T,T,...   replay: also capture the frame after each of these rows
 *   --save-at-end DIR       replay: after the last row, a Save and Quit into the checkpoint DIR
 *   --save-no-pre           that checkpoint without its pre/ Snapshot (csrc/play/session.sh)
 *   --rerecord-client       replay: write the client's own fields (d.cw, d.cseed, d.cmath,
 *                           d.cstat, d.px) without comparing them; any other field still stops it
 *   --trace PATH            write matched tracepoints here (Trace.t call sites)
 *   --from DIR              start on a checkpoint (DIR/manifest.json, DIR/save): Det
 *                           reset from (seed, TotalTime); a replay takes it from the header
 *   --game-dir DIR --assets DIR --width W --height H --harness ID --world NAME
 *   --hide-gui              F1 (hideGUI) from the start: the observation without the
 *                           HUD, the hand and the block outline (not in the pool)
 *   --pool DIR              a warm pool member (Pool): serves script and replay runs on the
 *                           port it writes to DIR/port (make pool-start, tests/pool.sh)
 *   --pool-cold             a pool member that does not prelaunch the last run's world
 *   --pool-options TAPE     a pool member whose options.txt is TAPE's (its header's options),
 *                           for runs that pin options read at client start (mipmaps)
 *   --pool-skip-reset WHAT  the negative control: never rewind WHAT ("rewind", or Class.field)
 *   --pool-max-mb N         a pool member ends after a job that leaves it above N MB
 *                           resident (default 2500); callers then start fresh JVMs
 *   --tick-profile          one ORACLE PROFILE line at the end: the tick loop's time per
 *                           phase (Prof)
 *   --rd N                  the render distance of a fresh recording (the config's
 *                           world.render_distance by default); the server's view distance
 *                           follows it, so it changes which chunks load and which
 *                           pending worldgen ticks fire. A replay takes rd from
 *                           the tape header regardless.
 *   --particles N           the particle setting of a fresh recording (0 all,
 *                           1 decreased, 2 minimal; the config's client.particles); the
 *                           tape header carries it and a replay takes it from there
 *   --option KEY=VALUE      one more option of a fresh recording, by its tape header
 *                           key: fancy, ao, mip, aniso, bob, clouds (0 or 1, the levels
 *                           for ao and mip); the render scenes made before the pinned
 *                           fast profile (fancy, smooth light, mipmaps) are recorded
 *                           again this way. A replay takes them from the header; with
 *                           --replay it is refused
 */
public final class Main
{
    /** The fresh-recording render distance (--rd; -1 the config's world.render_distance); writeOptions pins it when no replay header carries one. */
    static int renderDistance = -1;
    /** The fresh-recording particle setting (--particles; -1 the config's client.particles). */
    static int particles = -1;
    /** --option KEY=VALUE: a fresh recording's other options, by tape header key (null: the pinned profile). */
    static JsonObject optionSets;
    static final java.util.List<String> OPTION_KEYS = java.util.Arrays.asList("fancy", "ao", "mip", "aniso", "bob", "clouds");

    static String gameDir = "run", assets, conf, trace, renderstate;
    static int width = 854, height = 480;
    /** The options.txt this run pins (optionsFor); a pool job applies its own (Pool.applyOptions). */
    static String optionsText;

    public static void main(String[] args) throws Exception
    {
        Prof.tMain = System.nanoTime();
        configure(args, false);
        File gd = new File(gameDir);
        writeOptions(new File(gd, "options.txt"));
        if (trace != null) Trace.open(trace);
        if (renderstate != null) RenderStateProbe.open(new File(renderstate).getAbsolutePath());
        Det.onClientThread();
        Runtime.getRuntime().addShutdownHook(new Thread(new Runnable()
        {
            public void run()
            {
                Oracle.onShutdown(); // SIGTERM or window close: the tape ends on a complete row
            }
        }, "Oracle Tape Close"));

        String uuid = UUID.nameUUIDFromBytes("OfflinePlayer:Player".getBytes("UTF-8")).toString().replace("-", "");
        net.minecraft.client.main.Main.main(new String[] {
            "--username", "Player", "--uuid", uuid, "--version", "1.7.10",
            "--gameDir", gd.getPath(), "--assetsDir", assets, "--assetIndex", "1.7.10",
            "--accessToken", "0", "--userProperties", "{}", "--userType", "legacy",
            "--width", Integer.toString(width), "--height", Integer.toString(height)
        });
    }

    /**
     * A warm pool's job: the same arguments a fresh JVM would get, applied to
     * the rewound statics. Null when the pool can run it exactly, else why not
     * (the caller then runs a fresh JVM).
     */
    static String configurePool(String[] args)
    {
        for (String a : args)
        {
            if ("--play".equals(a) || "--port".equals(a) || "--trace".equals(a) || "--chunklog".equals(a) || "--spawndump".equals(a)
                || "--spawndumpdim".equals(a) || "--renderstate".equals(a) || "--enbt".equals(a) || "--pool".equals(a)
                || "--raw-gui".equals(a)
                || "--raw-rec".equals(a) || "--raw-drive".equals(a) || "--cw-diff".equals(a) || "--cw-cells".equals(a)
                || "--rerecord-client".equals(a) || "--hide-gui".equals(a))
                return "the pool does not run " + a;
        }
        // a coverage run goes to a member that cuts its data per job (Coverage), and only such a member
        boolean cov = java.util.Arrays.asList(args).contains("--coverage");
        if (cov != (Coverage.startup != null))
            return cov ? "the pool member carries no coverage agent" : "a coverage member takes coverage runs only";
        try
        {
            configure(args, true);
            if (Oracle.mode != Oracle.AGENT) return "the pool runs agent jobs only";
            if (Oracle.hideGui) return "the pool does not run a hideGui tape";
            if (Oracle.scriptPath != null)
            {
                // a probe command reaches FeatureProbeTrees, whose initializer draws (Pool.CLINIT_DRAWS)
                BufferedReader r = Rows.openRef(Oracle.scriptPath);
                try
                {
                    String line;
                    while ((line = r.readLine()) != null)
                    {
                        line = line.trim();
                        if (line.isEmpty()) continue;
                        JsonObject c = Rows.parse(line);
                        String k = c.has("cmd") ? c.get("cmd").getAsString() : "step";
                        if ("probe".equals(k) || "chunks".equals(k) || "structures".equals(k)) return "the pool does not run " + k + " commands";
                    }
                }
                finally { r.close(); }
            }
            return null;
        }
        catch (Exception e)
        {
            return e.toString();
        }
    }

    /** Parses the arguments into the harness statics and checks the world and the checkpoint, as far as the client launch. */
    static void configure(String[] args, boolean poolJob) throws Exception
    {
        replayStartSha = null;
        List<String> sets = new ArrayList<String>();
        for (int i = 0; i < args.length; ++i)
        {
            String a = args[i];
            if ("--play".equals(a)) Oracle.mode = Oracle.PLAY;
            else if ("--rd".equals(a)) renderDistance = Integer.parseInt(args[++i]);
            else if ("--particles".equals(a)) particles = Integer.parseInt(args[++i]);
            else if ("--option".equals(a))
            {
                String[] kv = args[++i].split("=", 2);
                if (kv.length != 2 || !OPTION_KEYS.contains(kv[0])) throw new IllegalArgumentException("oracle: --option takes KEY=VALUE with KEY one of " + OPTION_KEYS);
                if (optionSets == null) optionSets = new JsonObject();
                optionSets.addProperty(kv[0], Integer.toString(Integer.parseInt(kv[1])));
            }
            else if ("--agent".equals(a)) Oracle.mode = Oracle.AGENT;
            else if ("--dev".equals(a)) Oracle.dev = true;
            else if ("--from".equals(a)) Oracle.fromDir = args[++i];
            else if ("--conf".equals(a)) conf = args[++i];
            else if ("--set".equals(a)) sets.add(args[++i]);
            else if ("--world".equals(a)) Oracle.worldName = args[++i];
            else if ("--tape".equals(a)) Oracle.tapePath = args[++i];
            else if ("--script".equals(a)) Oracle.scriptPath = args[++i];
            else if ("--raw-gui".equals(a)) { RawGui.path = args[++i]; RawGui.load(); }
            else if ("--raw-rec".equals(a)) RawRec.path = args[++i];
            else if ("--raw-drive".equals(a)) { RawDrive.path = args[++i]; RawDrive.load(); }
            else if ("--replay".equals(a)) Oracle.replayPath = args[++i];
            else if ("--snap-at".equals(a)) Oracle.snapAt(args[++i]);
            else if ("--snap-dir".equals(a)) Oracle.snapDir = args[++i];
            else if ("--snap-plain".equals(a)) Oracle.snapPlain = true;
            else if ("--rerecord-client".equals(a)) Oracle.rerecordClient = true;
            else if ("--detail-from".equals(a)) Oracle.detailFrom = Long.parseLong(args[++i]);
            else if ("--until".equals(a)) Oracle.untilTick = Long.parseLong(args[++i]);
            else if ("--save-at-end".equals(a)) Oracle.saveAtEnd = args[++i];
            else if ("--save-no-pre".equals(a)) Oracle.saveNoPre = true;
            else if ("--port".equals(a)) Oracle.port = Integer.parseInt(args[++i]);
            else if ("--frames".equals(a)) Oracle.framesDir = args[++i];
            else if ("--frame-every".equals(a)) Oracle.frameEvery = Integer.parseInt(args[++i]);
            else if ("--frame-ticks".equals(a))
            {
                Oracle.frameTicks = new java.util.HashSet<Long>();
                for (String v : args[++i].split(",")) Oracle.frameTicks.add(Long.parseLong(v));
            }
            else if ("--renderstate".equals(a)) renderstate = args[++i];
            else if ("--harness".equals(a)) Oracle.harness = args[++i];
            else if ("--detail".equals(a)) Oracle.detail = true;
            else if ("--cw-cells".equals(a))
            {
                for (String v : args[++i].split(","))
                {
                    String[] q = v.split(":");
                    CwDiff.cells.add(new long[] {Long.parseLong(q[0]), Long.parseLong(q[1]), Long.parseLong(q[2]), Long.parseLong(q[3])});
                }
            }
            else if ("--cw-diff".equals(a))
            {
                CwDiff.ticks = new java.util.HashSet<Long>();
                for (String v : args[++i].split(",")) CwDiff.ticks.add(Long.parseLong(v));
            }
            else if ("--tick-profile".equals(a)) Prof.on = true;
            else if ("--coverage".equals(a)) Coverage.out = args[++i];
            else if ("--enbt".equals(a)) System.setProperty("netherite.enbt", "1");
            else if ("--nbt".equals(a))
            {
                Oracle.detail = true;
                for (String id : args[++i].split(",")) Oracle.detailNbt.add(Integer.parseInt(id.trim()));
            }
            else if ("--toasts".equals(a)) Oracle.showToasts = true;
            else if ("--chat".equals(a)) Oracle.showChat = true;
            else if ("--tooltips".equals(a)) Oracle.showTooltips = true;
            else if ("--hide-gui".equals(a)) Oracle.hideGui = true;
            else if ("--frame-pt".equals(a)) Oracle.framePt = Float.parseFloat(args[++i]);
            else if ("--frame-pts".equals(a))
            {
                String[] v = args[++i].split(",");
                Oracle.framePts = new float[v.length];
                for (int k = 0; k < v.length; ++k) Oracle.framePts[k] = Float.parseFloat(v[k]);
                Oracle.framePt = Oracle.framePts[0];
            }
            else if ("--trace".equals(a)) trace = args[++i];
            else if ("--chunklog".equals(a)) netherite.oracle.ChunkLoadProbe.begin(args[++i]);
            else if ("--spawndump".equals(a)) SpawnDump.dir = args[++i];
            else if ("--spawndumpdim".equals(a)) SpawnDumpDim.parse(args[++i]);
            else if ("--game-dir".equals(a))
            {
                String d = args[++i];
                if (!poolJob) gameDir = d; // a pool job runs in the pool's own game directory
            }
            else if ("--assets".equals(a)) assets = args[++i];
            else if ("--width".equals(a)) width = Integer.parseInt(args[++i]);
            else if ("--height".equals(a)) height = Integer.parseInt(args[++i]);
            else if ("--pool".equals(a)) Pool.dir = args[++i];
            else if ("--pool-skip-reset".equals(a)) Pool.skipReset = args[++i];
            else if ("--pool-cold".equals(a)) Pool.prelaunch = false;
            else if ("--pool-options".equals(a)) Pool.optionsTape = args[++i];
            else if ("--pool-max-mb".equals(a)) Pool.maxMb = Integer.parseInt(args[++i]);
            else throw new IllegalArgumentException("oracle: unknown argument " + a);
        }
        if (Oracle.mode == Oracle.OFF) throw new IllegalArgumentException("oracle: pass --play or --agent");
        if (Oracle.mode == Oracle.PLAY && Oracle.dev) {
            System.err.println("REFUSE --play --dev");
            System.exit(2);
        }
        if (Oracle.mode == Oracle.AGENT && Oracle.replayPath == null && Oracle.scriptPath == null && Oracle.port == 0 && !Pool.active())
            throw new IllegalArgumentException("oracle: agent mode needs --replay, --script or --port");
        if (assets == null) throw new IllegalArgumentException("oracle: pass --assets DIR");
        if (Oracle.replayPath != null && !sets.isEmpty()) throw new IllegalArgumentException("oracle: a replay takes its world from the tape; drop --set");
        if (Pool.active() && !poolJob)
        {
            // the pool JVM itself: each job brings its world; the options read at client start are the pool's
            JsonObject o = null;
            if (Pool.optionsTape != null)
            {
                BufferedReader r = Rows.openRef(Pool.optionsTape);
                try { o = Rows.parse(r.readLine()).getAsJsonObject("options"); }
                finally { r.close(); }
            }
            optionsText = optionsFor(false, o);
            return;
        }
        WorldConf.load(conf, sets, Oracle.replayPath);
        Oracle.seed = WorldConf.seed;
        if (renderDistance < 0) renderDistance = WorldConf.renderDistance;
        if (particles < 0) particles = WorldConf.particles;
        // a fresh JVM runs in out/java/run/NAME; a pool job resolves the same relative root against that directory
        String up = poolJob ? Pool.jobCwd + "/../../../../" : "../../../../";
        Oracle.checkpointRoot = new File(conf == null ? new File(up) : WorldConf.root(conf), "out/java/checkpoints").getPath();

        File gd = new File(gameDir);
        gd.mkdirs();
        JsonObject replayOptions = null;
        if (Oracle.replayPath != null)
        {
            BufferedReader ref = Rows.openRef(Oracle.replayPath);
            try
            {
                String first = ref.readLine();
                JsonObject header = first == null ? null : Rows.parse(first);
                if (header != null && header.has("options")) replayOptions = header.getAsJsonObject("options");
                // a dev tape's checkpoint start needs --dev at verify, before Oracle reads the header again
                if (header != null && header.has("dev") && header.get("dev").getAsBoolean()) Oracle.dev = true;
                // a tape recorded with --chat or --toasts drew them into its frames
                if (header != null && header.has("chat") && header.get("chat").getAsBoolean()) Oracle.showChat = true;
                if (header != null && header.has("toasts") && header.get("toasts").getAsBoolean()) Oracle.showToasts = true;
                if (header != null && header.has("tooltips") && header.get("tooltips").getAsBoolean()) Oracle.showTooltips = true;
                if (header != null && header.has("hideGui") && header.get("hideGui").getAsBoolean()) Oracle.hideGui = true;
                // a tape from before the start block ticked every entity in its join ticks
                Oracle.quietJoinPin = false;
                Oracle.spawnerPin = false;
                if (header != null && header.has("start"))
                {
                    JsonObject start = header.getAsJsonObject("start");
                    Oracle.quietJoinPin = start.has("quietJoin") && start.get("quietJoin").getAsBoolean();
                    Oracle.spawnerPin = start.has("spawnerPin") && start.get("spawnerPin").getAsBoolean();
                    if ("checkpoint".equals(start.get("kind").getAsString()))
                    {
                        String recorded = start.get("dir").getAsString();
                        replayStartSha = start.has("manifestSha256") ? start.get("manifestSha256").getAsString() : null;
                        if (Oracle.fromDir != null && !new File(Oracle.fromDir).getCanonicalFile().equals(new File(recorded).getCanonicalFile()))
                            throw new IllegalArgumentException("oracle: --from conflicts with replay header");
                        Oracle.fromDir = recorded;
                    }
                }
            }
            finally { ref.close(); }
        }
        if (Oracle.fromDir != null)
        {
            // a tape header names the checkpoint by absolute path as it was
            // recorded; on another tree (a lane, master after a move) that path
            // can be stale even though the checkpoint itself exists here, so
            // fall back to the seed's directory under Oracle.checkpointRoot.
            // The tape header's manifestSha256 proves the copy is the same one.
            if (!Checkpoint.complete(new File(Oracle.fromDir)) && replayStartSha != null)
            {
                File local = new File(new File(Oracle.checkpointRoot), Long.toString(Oracle.seed) + "/" + new File(Oracle.fromDir).getName());
                if (Checkpoint.complete(local) && Checkpoint.sha256(new File(local, "manifest.json")).equals(replayStartSha))
                    Oracle.fromDir = local.getPath();
            }
            Oracle.fromTotalTime = Checkpoint.verify(new File(Oracle.fromDir), Oracle.seed, WorldConf.json(), Oracle.dev);
        }
        if (optionSets != null)
        {
            if (Oracle.replayPath != null) throw new IllegalArgumentException("oracle: a replay takes its options from the tape; drop --option");
            replayOptions = optionSets;
        }
        optionsText = optionsFor(Oracle.mode == Oracle.PLAY, replayOptions);
    }

    static String replayStartSha;

    /** The pinned option set. Everything that can change world state or pixels is fixed here. */
    static String option(JsonObject o, String key, String fallback)
    {
        return o != null && o.has(key) ? o.get(key).getAsString() : fallback;
    }

    static String boolOption(JsonObject o, String key, boolean fallback)
    {
        return option(o, key, fallback ? "1" : "0").equals("1") ? "true" : "false";
    }

    static String optionsFor(boolean play, JsonObject replay)
    {
        String[] lines = {
            "renderDistance:" + option(replay, "rd", Integer.toString(renderDistance < 0 ? WorldConf.renderDistance : renderDistance)),
            "difficulty:" + option(replay, "difficulty", Integer.toString(WorldConf.difficulty)), "guiScale:2",
            "fancyGraphics:" + boolOption(replay, "fancy", WorldConf.fancy),
            "particles:" + option(replay, "particles", Integer.toString(particles < 0 ? WorldConf.particles : particles)),
            "ao:" + option(replay, "ao", "0"), "mipmapLevels:" + option(replay, "mip", "0"),
            "anisotropicFiltering:" + option(replay, "aniso", "1"), "fov:0.0", "gamma:" + option(replay, "gamma", WorldConf.gamma),
            "bobView:" + boolOption(replay, "bob", false), "clouds:" + boolOption(replay, "clouds", false),
            "mouseSensitivity:0.5", "invertYMouse:false",
            "advancedOpengl:false", "anaglyph3d:false", "fboEnable:true", "lang:en_US",
            "pauseOnLostFocus:false", "snooperEnabled:false", "showCape:true", "chatVisibility:0",
            // 260 is unlimited: an agent run's game loop runs one tick a pass, and
            // Display.sync at 120 held it to 120 ticks a second
            "enableVsync:" + (play ? "true" : "false"), "maxFps:" + (play ? "120" : "260"),
            "soundCategory_master:" + (play ? "1.0" : "0.0")
        };
        StringBuilder b = new StringBuilder();
        for (String l : lines) b.append(l).append(System.lineSeparator());
        return b.toString();
    }

    static void writeOptions(File f) throws IOException
    {
        PrintWriter w = new PrintWriter(new OutputStreamWriter(new FileOutputStream(f), "UTF-8"));
        w.print(optionsText);
        w.close();
    }
}
