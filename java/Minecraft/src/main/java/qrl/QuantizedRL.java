package qrl;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import com.microsoft.Malmo.Utils.TimeHelper;
import net.minecraft.client.Minecraft;
import net.minecraft.client.entity.EntityPlayerSP;
import net.minecraft.client.settings.KeyBinding;
import net.minecraft.entity.Entity;
import net.minecraft.server.MinecraftServer;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.RayTraceResult;
import net.minecraft.world.GameType;
import net.minecraft.world.WorldSettings;
import net.minecraft.world.WorldType;
import net.minecraftforge.common.MinecraftForge;
import net.minecraftforge.fml.common.Mod;
import net.minecraftforge.fml.common.event.FMLInitializationEvent;
import net.minecraftforge.fml.common.eventhandler.SubscribeEvent;
import net.minecraftforge.fml.common.gameevent.TickEvent;

import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.concurrent.SynchronousQueue;
import java.util.concurrent.TimeUnit;

/**
 * Thin, fully-discrete RL bridge for the MC 1.11.2 from-source env.
 * Local TCP server (newline-delimited JSON). Tick-synced step():
 *   step(action) -> apply keybinds + 15deg quantized aim -> advance 1 client tick -> obs.
 * Observation = pose+motion, vitals, look target, nearby entities. No mouse, no continuous.
 * Requires being in a world (singleplayer). Sibling mod to qlook; touches no Malmo source.
 */
@Mod(modid = QuantizedRL.MODID, name = "Quantized RL Bridge", version = "1.0",
     clientSideOnly = true, acceptableRemoteVersions = "*")
public class QuantizedRL {
    public static final String MODID = "qrl";
    static int PORT = 25575; // overridable: env QRL_PORT > qrl_launch.json "port" > default
    static final float QUANTUM = 15.0f;
    static final int N_ENTITIES = 8;
    static final int N_ENTITIES_MAX = 64;  // tick-trace oracle: emit up to this many nearby entities

    private static final class Req {
        final String cmd; final JsonObject action; final JsonObject world;
        final SynchronousQueue<String> resp = new SynchronousQueue<String>();
        boolean applied = false;
        Req(String cmd, JsonObject action, JsonObject world) { this.cmd = cmd; this.action = action; this.world = world; }
    }

    // game thread <-> socket thread handoff (one in-flight request)
    private final SynchronousQueue<Req> incoming = new SynchronousQueue<Req>();
    private Req inFlight = null;
    private boolean launching = false;
    private long initStartNanos = 0;  // set when launchWorld fires
    private double lastInitMs = 0;     // measured world-gen/init duration

    // CLI-driven instance config (mc_cli.py writes Minecraft/run/qrl_launch.json from fast.yaml/vanilla.yaml):
    // skips the main menu straight into the configured world, hides chat, applies gamerules.
    private static JsonObject launchCfg = null;
    private boolean autoWorldFired = false;
    private boolean launchApplied = false;

    // death tracking for the observation space (replaces the death screen)
    private boolean wasDead = false;
    private int deaths = 0;

    // ---- chain-RL protocol v2 state (semantic camera / craft / interact) ----
    // Mirrors c/magma/game/rl_mode.c semantics so the blaze-trained policy
    // transfers: container is MOD state (no GUI opens - headless keybind play
    // must keep running), validity-checked every tick like gm_runtime_tick.
    private final SemanticCamera semCam = new SemanticCamera();
    private boolean rlCamPending = false;   // last step action asked for "cam":1
    private int rlContainer = 0;            // 0 none/2x2, 1 table, 2 furnace
    private BlockPos rlContainerPos = null;
    /** inv_counts item ids (rl_mode.c rl_inv_ids): log, planks, stick,
     * cobblestone, crafting table, wooden pick, stone pick, coal, torch. */
    private static final int[] RL_INV_IDS = {17, 5, 280, 4, 58, 270, 274, 263, 50};
    /** Discrete craft primitives (rl_mode.c rl_crafts): inputs as (id,count)
     * pairs, output (id,count), 3x3 recipes gated on an open table. */
    private static final int[][] RL_CRAFT_IN = {
        {17, 1}, {5, 2}, {5, 4}, {5, 3, 280, 2}, {4, 3, 280, 2}, {263, 1, 280, 1},
        {4, 8}, {265, 3, 280, 2}};
    private static final int[][] RL_CRAFT_OUT = {
        {5, 4}, {280, 4}, {58, 1}, {270, 1}, {274, 1}, {50, 4},
        {61, 1}, {257, 1}};
    private static final boolean[] RL_CRAFT_TABLE = {false, false, false, true, true, false,
        true, true};

    // human-play tape recorder (recstart/recstop): one JSONL line per client tick
    // with the tick's movement inputs, player physics state, and nearby entities.
    // Written on the client thread only; volatile so the socket thread can test it.
    private volatile java.io.PrintWriter recWriter = null;
    private volatile java.io.PrintWriter recGeomWriter = null;
    private long recTick = 0;
    private String recLastInv = null;   // last serialized inventory (delta dump)
    private int recFrameEvery = 20;      // sparse pixel goldens; 0 = off
    private String recFramesDir = null;
    private static boolean recVelocityPending = false;
    private static int recVelocityX, recVelocityY, recVelocityZ;
    private static boolean recPositionPending = false;
    private static double recPositionX, recPositionY, recPositionZ;
    private static double recPositionVx, recPositionVy, recPositionVz;
    private static float recPositionYaw, recPositionPitch;
    private int recLastPlayerTicksExisted = Integer.MIN_VALUE;
    private int recLastDimension = Integer.MIN_VALUE;
    private boolean recPlayerLoading = false;
    private static java.lang.reflect.Field recPortalFrameField = null;
    private static java.lang.reflect.Field recRendererPhaseField = null;
    private static final int REC_ENT_RADIUS = 48;
    private static final int REC_ENT_MAX = 32;

    /** Client-thread hook from MixinRecordPlayerVelocity. Raw packet shorts are
     * retained so replay reproduces SPacketEntityVelocity's 1/8000 quantization. */
    public static void recordPlayerVelocityPacket(
            net.minecraft.network.play.server.SPacketEntityVelocity packet) {
        Minecraft mc = Minecraft.getMinecraft();
        if (mc == null || mc.player == null ||
            packet.getEntityID() != mc.player.getEntityId()) return;
        recVelocityX = packet.getMotionX();
        recVelocityY = packet.getMotionY();
        recVelocityZ = packet.getMotionZ();
        recVelocityPending = true;
    }

    /** Client-thread tail hook from MixinRecordPlayerPosition. Store the
     * fully resolved pose, not the packet's potentially relative coordinates. */
    public static void recordPlayerPositionPacket() {
        Minecraft mc = Minecraft.getMinecraft();
        if (mc == null || mc.player == null) return;
        recPositionX = mc.player.posX;
        recPositionY = mc.player.posY;
        recPositionZ = mc.player.posZ;
        recPositionYaw = mc.player.rotationYaw;
        recPositionPitch = mc.player.rotationPitch;
        recPositionVx = mc.player.motionX;
        recPositionVy = mc.player.motionY;
        recPositionVz = mc.player.motionZ;
        recPositionPending = true;
    }

    private static int reflectedInt(Object owner, String name,
                                    java.lang.reflect.Field cached) throws Exception {
        java.lang.reflect.Field field = cached;
        if (field == null) {
            Class<?> cls = owner.getClass();
            while (true) {
                try { field = cls.getDeclaredField(name); break; }
                catch (NoSuchFieldException ex) {
                    cls = cls.getSuperclass();
                    if (cls == null) throw ex;
                }
            }
            field.setAccessible(true);
            if ("frameCounter".equals(name)) recPortalFrameField = field;
            else recRendererPhaseField = field;
        }
        return field.getInt(owner);
    }

    @Mod.EventHandler
    public void init(FMLInitializationEvent e) {
        // AO drop-in (render-opt kernel 13): when a qao AO mode is active, disable Forge's
        // smooth-lighting pipeline so the VANILLA AmbientOcclusionFace.getAoBrightness path (the
        // kernel we verified) becomes the live AO path that the coremod hook rewrites. Forge's
        // pipeline (VertexLighterSmoothAo) is on by default and otherwise bypasses getAoBrightness
        // entirely (the inner class never even loads). Modes vanilla/native/sabotage all force the
        // vanilla path; "off"/absent leaves normal Forge rendering untouched.
        try {
            String m = new String(java.nio.file.Files.readAllBytes(java.nio.file.Paths.get(
                "/home/infatoshi/dev/minecraft/mc-1.11.2-env/c/render-opt/dropin/ao/qao_mode.txt"))).trim();
            if ("native".equals(m) || "sabotage".equals(m) || "vanilla".equals(m)) {
                net.minecraftforge.common.ForgeModContainer.forgeLightPipelineEnabled = false;
                System.err.println("[qao] Forge light pipeline DISABLED (vanilla getAoBrightness live) for qao_mode=" + m);
            }
        } catch (Exception ex) { /* no sidecar: leave Forge pipeline as-is */ }

        launchCfg = loadLaunchCfg();
        try {
            String pe = System.getenv("QRL_PORT");
            if (pe != null && !pe.isEmpty()) PORT = Integer.parseInt(pe);
            else if (launchCfg != null && launchCfg.has("port")) PORT = launchCfg.get("port").getAsInt();
        } catch (Exception ex) { System.out.println("[qrl] bad port override: " + ex); }

        WorldGenProbe.install();
        HumanStream.install();
        MinecraftForge.EVENT_BUS.register(this);
        Thread t = new Thread(this::serve, "qrl-socket");
        t.setDaemon(true);
        t.start();
        // Headless keep-awake: the window has no focus, so MC opens the pause menu
        // -> isGamePaused -> the client tick (and our ClientTickEvent) stops firing,
        // and the game force-ticks only every ~120s. ClientTickEvent can't fix this
        // because it doesn't run while paused. But the game loop drains its
        // scheduledTasks queue EVERY iteration even when paused (Malmo mixin), so a
        // daemon that schedules a menu-close keeps the game running unattended.
        Thread guard = new Thread(() -> {
            while (true) {
                try { Thread.sleep(50); } catch (InterruptedException ie) { return; }
                final Minecraft mc = Minecraft.getMinecraft();
                if (mc == null) continue;
                // Human play via mcwindow: the pause menu and inventory are
                // real UI, not headless stalls - leave screens alone.
                if (HumanStream.hasViewer()) continue;
                try {
                    mc.addScheduledTask(() -> {
                        mc.gameSettings.pauseOnLostFocus = false;
                        // Close ANY screen that pauses the game (the headless window keeps
                        // re-opening the pause menu on focus loss). Leave non-pausing GUIs
                        // (chat, etc.) alone. This keeps the integrated server ticking so
                        // chat-command processing does not stall ~120s at a time.
                        if (mc.world != null && mc.currentScreen != null
                            && mc.currentScreen.doesGuiPauseGame()) {
                            mc.displayGuiScreen(null);
                        }
                        // runGameLoop computes this flag after scheduled tasks.
                        // Clear the stale value too, otherwise a command queued in
                        // the same frame can wait for Minecraft's ~120s force tick.
                        mc.isGamePaused = false;
                    });
                } catch (Throwable ig) { /* mc not ready yet */ }
            }
        }, "qrl-unpause");
        guard.setDaemon(true);
        guard.start();
    }

    // ---------------- socket thread ----------------
    private void serve() {
        try (ServerSocket server = new ServerSocket(PORT)) {
            System.out.println("[qrl] listening on 127.0.0.1:" + PORT);
            while (true) {
                try (Socket s = server.accept();
                     BufferedReader in = new BufferedReader(new InputStreamReader(s.getInputStream(), StandardCharsets.UTF_8));
                     BufferedWriter out = new BufferedWriter(new OutputStreamWriter(s.getOutputStream(), StandardCharsets.UTF_8))) {
                    System.out.println("[qrl] client connected");
                    String line;
                    while ((line = in.readLine()) != null) {
                        out.write(handle(line));
                        out.write("\n");
                        out.flush();
                    }
                } catch (Exception ex) {
                    System.out.println("[qrl] client loop ended: " + ex.getMessage());
                }
            }
        } catch (Exception ex) {
            System.out.println("[qrl] server failed: " + ex.getMessage());
        }
    }

    private String handle(String line) throws InterruptedException {
        JsonObject msg;
        try { msg = new JsonParser().parse(line).getAsJsonObject(); }
        catch (Exception e) { return err("bad json"); }
        String cmd = msg.has("cmd") ? msg.get("cmd").getAsString() : "";
        JsonObject action = msg.has("action") ? msg.getAsJsonObject("action") : new JsonObject();
        JsonObject world = msg.has("world") ? msg.getAsJsonObject("world") : new JsonObject();
        switch (cmd) {
            case "step": case "reset": case "obs": case "stats": case "close":
            case "overclock": case "cmd": case "spawn": case "fluid": case "capture_light":
            case "capture_fluidheight": case "capture_biome":
            case "capture_shouldsiderender": case "capture_quadsflat":
            case "capture_quadssmooth": case "capture_fluidquads":
            case "capture_ao": case "capture_particle": case "capture_skylight":
            case "capture_lightmap": case "capture_limbanim": case "capture_lightprop":
            case "capture_chunkrebuild": case "runcmds": case "dim": case "kmode":
            case "reload_renderers":
            case "portal_touch": case "use_end_eye": case "set_pose":
            case "dumpblocks": case "frame": case "gldiag": case "focusdiag": case "camera": case "sample_light":
            case "recstart": case "recstop":
            case "getblocks": case "setblocks":
            case "summon": case "getentities": case "killentities": break;
            case "coverage_reset": case "coverage_dump":
            case "coverage_setfile": case "coverage_enable": return handleCoverage(cmd, action);
            default: return err("unknown cmd");
        }
        Req r = new Req(cmd, action, world);
        incoming.put(r);
        String result = r.resp.poll(120, TimeUnit.SECONDS); // world-gen can be slow
        return result == null ? err("timeout") : result;
    }

    // ---------------- coverage hook control (render-opt) ----------------
    // CoverageLog is a sibling class in the qrl package (committed) and is on the
    // compile classpath, so we call it directly. It is pure-Java and thread-safe
    // (ConcurrentHashMap + synchronized flush/reset), so these run on the socket
    // thread with no game-thread hop. Per-tick sampling of the render paths lives
    // in coverageSample() on the client tick. Control cmds: coverage_reset /
    // coverage_dump / coverage_setfile / coverage_enable.
    private String handleCoverage(String cmd, JsonObject action) {
        try {
            if (cmd.equals("coverage_reset")) {
                int n = CoverageLog.reset();
                return "{\"ok\":true,\"cleared\":" + n + "}";
            }
            if (cmd.equals("coverage_dump")) {
                String file = action.has("file") ? action.get("file").getAsString()
                    : CoverageLog.DEFAULT_FILE;
                long keys = CoverageLog.flush();
                return "{\"ok\":true,\"unique_keys\":" + keys + ",\"file\":\"" + file + "\"}";
            }
            if (cmd.equals("coverage_setfile")) {
                String f = action.has("file") ? action.get("file").getAsString() : "";
                CoverageLog.setFile(f);
                return "{\"ok\":true,\"file\":\"" + f + "\"}";
            }
            if (cmd.equals("coverage_enable")) {
                boolean e = !action.has("enabled") || action.get("enabled").getAsBoolean();
                CoverageLog.setEnabled(e);
                return "{\"ok\":true,\"enabled\":" + e + "}";
            }
        } catch (Throwable t) {
            return err("coverage cmd failed: " + t);
        }
        return err("unknown coverage cmd");
    }

    // ---------------- coverage sampler (runs each client tick) ----------------
    // Records, deduped-with-counts, which render-path methods our C kernels back
    // fire during REAL play, keyed by (method, block/entity/particle name, dim,
    // branch flags). Cheap read-only MC calls are invoked directly on the client
    // world around the player; expensive/side-effecting ones (updateLightmap,
    // checkLightFor BFS, generateSkylightMap, FaceBakery) are inferred from world
    // state so goldens captured from REAL MC stay valid and FPS is unaffected.
    private static int covTick = 0;
    private static int covCursor = 0;
    private static boolean covBakeryDone = false;
    private static java.lang.reflect.Method GFH;        // BlockFluidRenderer.getFluidHeight
    private static Object FLUID_R;                      // BlockFluidRenderer instance
    private static java.lang.reflect.Field FX_LAYERS;   // ParticleManager.fxLayers
    private static final int COV_MAX = 96;              // blocks sampled per tick

    private static String regName(net.minecraft.block.Block b) {
        try {
            net.minecraft.util.ResourceLocation rl = b.getRegistryName();
            if (rl != null) return rl.toString();
        } catch (Throwable ig) {}
        try { return b.getUnlocalizedName(); } catch (Throwable ig) {}
        return b.getClass().getSimpleName();
    }

    private void coverageSample(Minecraft mc) {
        if (!CoverageLog.isEnabled()) return;
        net.minecraft.world.World w = mc.world;
        EntityPlayerSP p = mc.player;
        covTick++;
        int dim = w.provider.getDimensionType().getId();
        CoverageLog.setDimension(dim);
        boolean hasSky = !w.provider.hasNoSky();
        boolean isOverworld = dim == 0;
        boolean nightVision = false;
        try { nightVision = p.isPotionActive(net.minecraft.init.MobEffects.NIGHT_VISION); } catch (Throwable ig) {}
        int lightning = 0; try { lightning = w.getLastLightningBolt(); } catch (Throwable ig) {}
        boolean boss = false;
        try {
            for (net.minecraft.entity.Entity e : w.loadedEntityList) {
                String cn = e.getClass().getSimpleName();
                if (cn.contains("Wither") || cn.contains("Dragon")) { boss = true; break; }
            }
        } catch (Throwable ig) {}
        boolean day = (w.getWorldTime() % 24000L) < 12000L;

        // ---- inferred hits (every 10 ticks): expensive/side-effecting paths ----
        if (covTick % 10 == 0) {
            int skyFlag = hasSky ? CoverageLog.F_SKY : 0;
            int skyLight = hasSky ? CoverageLog.F_HAS_SKYLIGHT : CoverageLog.F_NO_SKYLIGHT;
            int lmFlags = skyLight | skyFlag | CoverageLog.F_BLOCK_LIGHT
                | (nightVision ? CoverageLog.F_NIGHTVISION : 0)
                | (boss ? CoverageLog.F_BOSS : 0)
                | (lightning > 0 ? CoverageLog.F_LIGHTNING : 0);
            CoverageLog.hit("updateLightmap", "lightmap", lmFlags);                 // 11
            CoverageLog.hit("checkLightFor", "world", skyLight
                | (lightning > 0 ? CoverageLog.F_LIGHTNING : 0));                   // 16
            CoverageLog.hit("getCombinedLight", "block", skyLight | skyFlag
                | CoverageLog.F_BLOCK_LIGHT);                                       // 15
            CoverageLog.hit("generateSkylightMap", "chunk", skyLight);              // 17
            CoverageLog.hit("getRawLight", "world", skyLight);                      // 16 helper
            if (isOverworld) CoverageLog.hit("generateSky", "sky",
                CoverageLog.F_SKY | (day ? 0 : CoverageLog.F_NIGHTVISION));         // 36
            if (mc.currentScreen != null) CoverageLog.hit("renderDefaultChar", "font", 0); // 39
        }

        // ---- one-shot: model baking ran at resource load (kernels 31-35) ----
        if (!covBakeryDone) {
            covBakeryDone = true;
            CoverageLog.hit("makeBakedQuad", "all_models", 0);                      // 31
            CoverageLog.hit("makeBakedQuad", "all_models", CoverageLog.F_PART_ROT);
            CoverageLog.hit("fillVertexData", "all_models", 0);                     // 32
            CoverageLog.hit("rotatePart", "all_models", CoverageLog.F_PART_ROT);    // 33
            CoverageLog.hit("rotateVertex", "all_models", CoverageLog.F_PART_ROT);
            CoverageLog.hit("getFacingFromVertexData", "all_models", 0);            // 34
            CoverageLog.hit("applyFacing", "all_models", 0);
            CoverageLog.hit("bakeModel", "all_models", 0);                          // 35
        }

        // ---- block sampling: rotating window over a box around the player ----
        int px = (int) Math.floor(p.posX), py = (int) Math.floor(p.posY), pz = (int) Math.floor(p.posZ);
        final int R = 6, YMIN = -3, YMAX = 4;
        final int W = 2 * R + 1, H = YMAX - YMIN + 1, TOTAL = W * W * H;
        int start = covCursor % TOTAL;
        net.minecraft.client.renderer.BlockRendererDispatcher brd = null;
        try { brd = mc.getBlockRendererDispatcher(); } catch (Throwable ig) {}
        for (int n = 0; n < COV_MAX; n++) {
            int idx = (start + n) % TOTAL;
            int dy = idx / (W * W), rem = idx % (W * W), dx = rem / W, dz = rem % W;
            BlockPos pos = new BlockPos(px + dx - R, py + dy + YMIN, pz + dz - R);
            if (pos.getY() < 0 || pos.getY() > 255) continue;
            if (!w.isValid(pos) || !w.isBlockLoaded(pos)) continue;
            net.minecraft.block.state.IBlockState st;
            try { st = w.getBlockState(pos); } catch (Throwable ig) { continue; }
            net.minecraft.block.Block b = st.getBlock();
            if (b == net.minecraft.init.Blocks.AIR) continue;
            String name = regName(b);
            sampleBlock(w, st, b, pos, name, brd, hasSky);
        }
        covCursor = (start + COV_MAX) % TOTAL;

        // ---- particles (29/30): reflect ParticleManager.fxLayers ----
        sampleParticles(mc);

        // ---- entity limb anim (37) + model box (38): nearby living entities ----
        try {
            int counted = 0;
            for (net.minecraft.entity.Entity e : w.loadedEntityList) {
                if (e == p) continue;
                if (!(e instanceof net.minecraft.entity.EntityLivingBase)) continue;
                String en = e.getClass().getSimpleName();
                CoverageLog.hit("setRotationAngles", en, 0);                       // 37
                CoverageLog.hit("TexturedQuad.draw", en, 0);                        // 38
                if (++counted >= 24) break;
            }
        } catch (Throwable ig) {}
    }

    private void sampleBlock(net.minecraft.world.World w, net.minecraft.block.state.IBlockState st,
            net.minecraft.block.Block b, BlockPos pos, String name,
            net.minecraft.client.renderer.BlockRendererDispatcher brd, boolean hasSky) {
        // 21 shouldSideBeRendered: real call, 6 faces, flag render vs cull.
        for (net.minecraft.util.EnumFacing side : net.minecraft.util.EnumFacing.values()) {
            try {
                boolean out = st.shouldSideBeRendered(w, pos, side);
                CoverageLog.hit("shouldSideBeRendered", name,
                    out ? CoverageLog.F_FACE_RENDER : CoverageLog.F_FACE_CULL);
            } catch (Throwable ig) {}
        }
        // 14 light_query: real getLightFor (block + sky).
        try {
            int bl = w.getLightFor(net.minecraft.world.EnumSkyBlock.BLOCK, pos);
            int sl = hasSky ? w.getLightFor(net.minecraft.world.EnumSkyBlock.SKY, pos) : 0;
            CoverageLog.hit("getLightFor", name,
                (bl > 0 ? CoverageLog.F_BLOCK_LIGHT : 0)
                | (hasSky ? CoverageLog.F_HAS_SKYLIGHT : CoverageLog.F_NO_SKYLIGHT)
                | (sl > 0 ? CoverageLog.F_SKY : 0));
        } catch (Throwable ig) {}

        net.minecraft.block.material.Material bm = st.getMaterial();
        if (bm == net.minecraft.block.material.Material.WATER
            || bm == net.minecraft.block.material.Material.LAVA) {
            sampleFluid(w, st, pos, name, bm);
            return;
        }
        // 18 biome_color_blend: grass/foliage/water biomes for tinted blocks.
        if (b == net.minecraft.init.Blocks.GRASS || b == net.minecraft.init.Blocks.LEAVES
            || b == net.minecraft.init.Blocks.TALLGRASS || b == net.minecraft.init.Blocks.VINE
            || b == net.minecraft.init.Blocks.WATERLILY) {
            try {
                net.minecraft.world.biome.BiomeColorHelper.getGrassColorAtPos(w, pos);
                CoverageLog.hit("biome_color_blend", name, CoverageLog.F_GRASS);
                net.minecraft.world.biome.BiomeColorHelper.getFoliageColorAtPos(w, pos);
                CoverageLog.hit("biome_color_blend", name, CoverageLog.F_FOLIAGE);
            } catch (Throwable ig) {}
        }
        // 23/24 render quads + 12 updateVertexBrightness + 22 fillQuadBounds + 13 ao helpers.
        // BlockModelRenderer.renderQuadsSmooth is the default AO path; renderQuadsFlat is the
        // flat-color path. Both kernels back every solid block; record per block-type + tint +
        // AO-corner (translucent neighbor) so we can compare against the staged goldens.
        if (brd == null) return;
        int qflags = CoverageLog.F_SMOOTH_AO;
        boolean tinted = false, aoCorner = false;
        try {
            net.minecraft.client.renderer.block.model.IBakedModel model = brd.getModelForState(st);
            java.util.ArrayList<net.minecraft.util.EnumFacing> faces =
                new java.util.ArrayList<net.minecraft.util.EnumFacing>();
            faces.add(null);
            for (net.minecraft.util.EnumFacing ef : net.minecraft.util.EnumFacing.values()) faces.add(ef);
            for (net.minecraft.util.EnumFacing f : faces) {
                java.util.List<net.minecraft.client.renderer.block.model.BakedQuad> qs =
                    model.getQuads(st, f, 0L);
                if (qs == null) continue;
                for (net.minecraft.client.renderer.block.model.BakedQuad q : qs) {
                    if (q.hasTintIndex()) tinted = true;
                }
            }
            for (net.minecraft.util.EnumFacing side : net.minecraft.util.EnumFacing.values()) {
                try {
                    if (w.getBlockState(pos.offset(side)).isTranslucent()) { aoCorner = true; break; }
                } catch (Throwable ig) {}
            }
        } catch (Throwable ig) {}
        int tf = tinted ? CoverageLog.F_TINT : 0;
        CoverageLog.hit("renderQuadsSmooth", name, qflags | tf);                    // 24
        CoverageLog.hit("renderQuadsFlat", name, CoverageLog.F_FLAT_AO | tf);       // 23
        CoverageLog.hit("updateVertexBrightness", name,
            CoverageLog.F_SMOOTH_AO | tf | (aoCorner ? CoverageLog.F_AO_CORNER : 0)); // 12
        CoverageLog.hit("fillQuadBounds", name, tf);                                // 22
        CoverageLog.hit("getAoBrightness", name, CoverageLog.F_SMOOTH_AO | tf);     // 13
    }

    private void sampleFluid(net.minecraft.world.World w, net.minecraft.block.state.IBlockState st,
            BlockPos pos, String name, net.minecraft.block.material.Material bm) {
        boolean lava = bm == net.minecraft.block.material.Material.LAVA;
        int base = lava ? CoverageLog.F_LAVA : CoverageLog.F_WATER;
        boolean flowing = false;
        try {
            int lv = ((Integer) st.getValue(net.minecraft.block.BlockLiquid.LEVEL)).intValue();
            flowing = lv > 0;
        } catch (Throwable ig) {}
        int ff = base | (flowing ? CoverageLog.F_FLOW : CoverageLog.F_STILL);
        // 19 getFluidHeight: real call (reflected private method).
        try {
            if (GFH == null || FLUID_R == null) {
                Object brd = Minecraft.getMinecraft().getBlockRendererDispatcher();
                java.lang.reflect.Field flf = brd.getClass().getDeclaredField("fluidRenderer");
                flf.setAccessible(true);
                FLUID_R = flf.get(brd);
                GFH = FLUID_R.getClass().getDeclaredMethod("getFluidHeight",
                    net.minecraft.world.IBlockAccess.class, net.minecraft.util.math.BlockPos.class,
                    net.minecraft.block.material.Material.class);
                GFH.setAccessible(true);
            }
            GFH.invoke(FLUID_R, w, pos, bm);
            CoverageLog.hit("getFluidHeight", lava ? "lava" : "water", ff);          // 19
        } catch (Throwable ig) {}
        // 20 renderFluid: branches inferred from state + neighbors (UP back-face,
        // DOWN, water-overlay when a glass neighbor is present).
        boolean up = false, down = false, overlay = false;
        try { up = st.shouldSideBeRendered(w, pos, net.minecraft.util.EnumFacing.UP); } catch (Throwable ig) {}
        try { down = st.shouldSideBeRendered(w, pos, net.minecraft.util.EnumFacing.DOWN); } catch (Throwable ig) {}
        if (!lava) {
            net.minecraft.util.EnumFacing[] horiz = {
                net.minecraft.util.EnumFacing.NORTH, net.minecraft.util.EnumFacing.SOUTH,
                net.minecraft.util.EnumFacing.WEST, net.minecraft.util.EnumFacing.EAST };
            for (net.minecraft.util.EnumFacing s : horiz) {
                try {
                    net.minecraft.block.Block nb = w.getBlockState(pos.offset(s)).getBlock();
                    if (nb == net.minecraft.init.Blocks.GLASS || nb == net.minecraft.init.Blocks.STAINED_GLASS) {
                        overlay = true; break;
                    }
                } catch (Throwable ig) {}
            }
        }
        CoverageLog.hit("renderFluid", lava ? "lava" : "water", ff
            | (up ? CoverageLog.F_UP_BACKFACE : 0)
            | (down ? CoverageLog.F_DOWN : 0)
            | (overlay ? CoverageLog.F_OVERLAY : 0));                              // 20
    }

    private void sampleParticles(Minecraft mc) {
        try {
            if (FX_LAYERS == null) {
                FX_LAYERS = net.minecraft.client.particle.ParticleManager.class
                    .getDeclaredField("fxLayers");
                FX_LAYERS.setAccessible(true);
            }
            java.util.ArrayDeque<net.minecraft.client.particle.Particle>[][] layers =
                (java.util.ArrayDeque<net.minecraft.client.particle.Particle>[][])
                    FX_LAYERS.get(mc.effectRenderer);
            int counted = 0;
            for (int a = 0; a < layers.length; a++) {
                for (int c = 0; c < layers[a].length; c++) {
                    java.util.ArrayDeque<net.minecraft.client.particle.Particle> dq = layers[a][c];
                    if (dq == null) continue;
                    for (net.minecraft.client.particle.Particle prt : dq) {
                        String pn = prt.getClass().getSimpleName();
                        CoverageLog.hit("particle_onUpdate", pn, 0);                // 29
                        CoverageLog.hit("particle_renderParticle", pn, 0);          // 30
                        if (++counted >= 128) return;
                    }
                }
            }
        } catch (Throwable ig) {}
    }

    // Walking into a 3x3 end-portal pad fires BlockEndPortal.onEntityCollidedWithBlock
    // once PER overlapped portal block in the same entity tick. The first call
    // transfers 0->1; the second then matches changeDimension's dimension==1 &&
    // dimensionIn==1 CREDITS path, which removeEntity()s the just-arrived player and
    // waits for the credits screen's respawn packet - headless, that never comes, so
    // the player is dead-but-alive limbo (selectors empty, never ticked, hp intact).
    // Cancel the 1->1 travel while the first transfer is still unconfirmed; a real
    // End exit (credits) happens long after the arrival teleport was confirmed.
    @SubscribeEvent
    public void onTravelToDimension(net.minecraftforge.event.entity.EntityTravelToDimensionEvent e) {
        if (!(e.getEntity() instanceof net.minecraft.entity.player.EntityPlayerMP)) return;
        net.minecraft.entity.player.EntityPlayerMP p =
            (net.minecraft.entity.player.EntityPlayerMP) e.getEntity();
        if (p.dimension == 1 && e.getDimension() == 1 && p.isInvulnerableDimensionChange()) {
            e.setCanceled(true);
            System.out.println("[qrl] canceled double end-portal transfer (credits limbo guard)");
        }
    }

    // Vanilla 1.11.2 bug: PlayerList.transferEntityToWorld skips toWorldIn.spawnEntity
    // whenever lastDimension == 1 (it assumes every End exit rides the credits/respawn
    // path), so a changeDimension transfer OUT of the End leaves the player absent from
    // the destination world's playerEntities/loadedEntityList: selectors (@a/@p) find
    // nothing, the player is never ticked server-side, and PlayerChunkMap streaming
    // starves. Repair on every server tick, whatever path dropped the registration.
    @SubscribeEvent
    public void onServerTick(TickEvent.ServerTickEvent e) {
        if (e.phase != TickEvent.Phase.END) return;
        net.minecraft.server.MinecraftServer srv =
            net.minecraftforge.fml.common.FMLCommonHandler.instance().getMinecraftServerInstance();
        if (srv == null) return;
        try {
            for (net.minecraft.entity.player.EntityPlayerMP p : srv.getPlayerList().getPlayers()) {
                if (p.isDead) continue;
                // The canonical destination world is what selectors search
                // (server.worlds, rebuilt by DimensionManager). A transfer can
                // leave p.world pointing at a stale, dropped WorldServer
                // instance; containment checks against p.world then pass while
                // @a still finds nothing. Always compare against the canonical
                // instance for p.dimension.
                net.minecraft.world.WorldServer canon = srv.worldServerForDimension(p.dimension);
                net.minecraft.world.WorldServer cur =
                    (p.world instanceof net.minecraft.world.WorldServer)
                        ? (net.minecraft.world.WorldServer) p.world : null;
                boolean staleWorld = cur != canon;
                boolean missing = !canon.playerEntities.contains(p);
                if (!staleWorld && !missing) continue;
                if (staleWorld && cur != null && cur.playerEntities.contains(p)) {
                    cur.removeEntityDangerously(p);
                    p.isDead = false;
                    try { cur.getPlayerChunkMap().removePlayer(p); } catch (Throwable ig) {}
                }
                p.setWorld(canon);
                p.interactionManager.setWorld(canon);
                if (!canon.playerEntities.contains(p)) canon.spawnEntity(p);
                boolean inChunkMap = false;
                try {
                    java.lang.reflect.Field pf = net.minecraft.server.management
                        .PlayerChunkMap.class.getDeclaredField("players");
                    pf.setAccessible(true);
                    inChunkMap = ((java.util.List<?>) pf.get(canon.getPlayerChunkMap())).contains(p);
                } catch (Throwable ig) {}
                if (!inChunkMap) canon.getPlayerChunkMap().addPlayer(p);
                canon.getChunkProvider().provideChunk((int) p.posX >> 4, (int) p.posZ >> 4);
                // The dropped registration also loses the dragon boss bar: the
                // client's GuiBossOverlay map never gets a working ADD again
                // (OPEN_DIVERGENCES #50 - the 175629Z e2e goldens have no bar).
                // Force a remove+add on the End fight's BossInfoServer so the
                // ADD packet is re-sent to this connection.
                if (p.dimension == 1 && canon.provider
                        instanceof net.minecraft.world.WorldProviderEnd) {
                    try {
                        net.minecraft.world.end.DragonFightManager dfm =
                            ((net.minecraft.world.WorldProviderEnd) canon.provider)
                                .getDragonFightManager();
                        if (dfm != null) {
                            java.lang.reflect.Field bf = net.minecraft.world.end
                                .DragonFightManager.class.getDeclaredField("bossInfo");
                            bf.setAccessible(true);
                            net.minecraft.world.BossInfoServer bi =
                                (net.minecraft.world.BossInfoServer) bf.get(dfm);
                            bi.removePlayer(p);
                            bi.addPlayer(p);
                            System.out.println("[qrl] boss bar re-added for "
                                + p.getName());
                        }
                    } catch (Throwable ig) {
                        System.out.println("[qrl] boss bar re-add failed: " + ig);
                    }
                }
                System.out.println("[qrl] re-registered player in dim " + p.dimension
                    + " at " + (int) p.posX + "," + (int) p.posY + "," + (int) p.posZ
                    + (staleWorld ? " (stale world instance "
                        + (cur == null ? "null" : Integer.toHexString(System.identityHashCode(cur)))
                        + " -> " + Integer.toHexString(System.identityHashCode(canon)) + ")"
                        : " (missing from world lists)"));
            }
        } catch (Throwable t) {
            System.out.println("[qrl] registration watchdog failed: " + t);
        }
        // Heartbeat so a paused/stalled integrated server is visible in the log
        // (the End-entry GuiDownloadTerrain deadlock froze ServerTickEvent for
        // 10+ minutes and looked exactly like a selector bug).
        if (++srvTicks % 1200 == 0) {
            System.out.println("[qrl] server tick heartbeat " + srvTicks);
        }
    }
    private long srvTicks = 0;

    // ---------------- game thread ----------------
    @SubscribeEvent
    public void onClientTick(TickEvent.ClientTickEvent e) {
        if (e.phase != TickEvent.Phase.END) return;
        Minecraft mc = Minecraft.getMinecraft();
        mc.gameSettings.pauseOnLostFocus = false; // headless window never has focus; keep the server ticking
        // The headless window auto-opens the pause menu (no focus), which freezes the
        // integrated server -> ticks only every ~120s. Close it so the game keeps running.
        // Only the pause menu; leave inventory/chest/chat GUIs alone. During human play
        // (mcwindow viewer attached) Esc must work, so leave the pause menu alone too.
        if (!HumanStream.hasViewer()
            && mc.world != null && mc.currentScreen instanceof net.minecraft.client.gui.GuiIngameMenu) {
            mc.displayGuiScreen(null);
        }
        // Headless can't click "Respawn"; auto-respawn so a dead player never wedges the
        // session. With strip.menus the GuiGameOver screen never even opens (mixin blocks
        // it), so trigger purely off health; death is surfaced in obs as dead/deaths.
        if (mc.player != null && mc.player.getHealth() <= 0.0F) {
            if (!wasDead) { deaths++; wasDead = true; }
            mc.player.respawnPlayer();
            if (mc.currentScreen instanceof net.minecraft.client.gui.GuiGameOver) {
                mc.displayGuiScreen(null);
            }
        } else if (mc.player != null) {
            wasDead = false;
        }

        // CLI-driven launch: skip the main menu entirely, go straight into the configured world.
        if (launchCfg != null && !autoWorldFired && launchCfg.has("world") && mc.world == null
            && !launching && mc.currentScreen instanceof net.minecraft.client.gui.GuiMainMenu) {
            autoWorldFired = true;
            System.out.println("[qrl] auto-launching world from qrl_launch.json (menu skipped)");
            launchWorld(mc, launchCfg.getAsJsonObject("world"));
        }
        // One-time in-world config application (chat off, gamerules, time, weather).
        if (launchCfg != null && !launchApplied && mc.player != null && mc.getIntegratedServer() != null) {
            launchApplied = true;
            applyLaunchSettings(mc);
        }

        // render-opt coverage hook: keep dim current + sample render paths each tick.
        if (mc.world != null && mc.player != null) {
            try { CoverageLog.setDimension(mc.world.provider.getDimensionType().getId()); } catch (Throwable ig) {}
            try { coverageSample(mc); } catch (Throwable t) { /* never let sampling crash the tick */ }
        }

        // human-play tape: record EVERY client tick while active, bridge or no bridge.
        if (recWriter != null && mc.world != null && mc.player != null) {
            try { recordTick(mc); } catch (Throwable t) { /* never let recording crash the tick */ }
        }

        // chain-RL container keep-open rule (gm_runtime_tick parity).
        try { rlContainerTick(mc); } catch (Throwable ig) {}

        // finalize a step that was applied last tick.
        // NB: we do NOT blanket-release keys here. applyAction() sets every movement key to its
        // exact bit each step, so a held key stays held across consecutive identical actions (no
        // release/re-press edge). The old clearKeys() here turned "hold W for N steps" into "tap W
        // N times", which tripped EntityPlayerSP.sprintToggleTimer into spurious auto-sprint -- a
        // harness input artifact absent from real vanilla (verified vs the state-vector oracle).
        boolean justFinalized = false;
        if (inFlight != null && inFlight.applied) {
            String obs = (mc.player == null) ? err("no player") : obs(mc, rlCamPending);
            inFlight.resp.offer(obs);
            inFlight = null;
            justFinalized = true;
        }
        if (inFlight != null) return; // shouldn't happen

        // LOCKSTEP WINDOW: right after finalizing a step, wait briefly for the client's
        // next command so it applies THIS tick. Without this, the response->next-step
        // round trip always misses this handler's poll and lands one tick later, so the
        // tick in between free-runs with keys still held -- the game advances TWO physics
        // ticks per step and drifts off any external tick-for-tick trace. A fast client
        // (localhost RL loop, oracle tracer) answers in <1ms; a slow/absent one just eats
        // the timeout once and we fall back to free-running.
        Req polled = null;
        try {
            polled = justFinalized
                ? incoming.poll(20, java.util.concurrent.TimeUnit.MILLISECONDS)
                : incoming.poll();
        } catch (InterruptedException ie) {
            Thread.currentThread().interrupt();
        }
        final Req r = polled;  // effectively final: captured by inner classes below
        if (r == null) return;

        try { // a malformed command (missing param, bad type) must never kill the client thread
        if (r.cmd.equals("stats")) { reply(r, stats(mc)); return; }
        if (r.cmd.equals("recstart")) {
            if (mc.player == null || mc.world == null) { reply(r, err("no world")); return; }
            try {
                if (recWriter != null) { recWriter.close(); }
                String file = r.action.has("file") ? r.action.get("file").getAsString()
                                                   : "/tmp/qrl_tape.jsonl";
                recWriter = new java.io.PrintWriter(new java.io.BufferedWriter(
                    new java.io.FileWriter(file)), false);
                recTick = 0;
                recLastInv = null;  // force a full inventory dump on tick 0
                recVelocityPending = false;
                recPositionPending = false;
                recLastPlayerTicksExisted = mc.player.ticksExisted;
                recLastDimension = mc.player.dimension;
                recPlayerLoading = false;
                recFrameEvery = r.action.has("frames_every")
                    ? r.action.get("frames_every").getAsInt() : 20;
                recFramesDir = file.replaceAll("\\.jsonl$", "") + "_frames";
                if (recFrameEvery > 0) new java.io.File(recFramesDir).mkdirs();
                // geometry-oracle sidecar: post-render model part poses on
                // golden-frame ticks (<tape>.geom.jsonl). See dump code at
                // the frame-write site.
                if (recGeomWriter != null) { recGeomWriter.close(); }
                recGeomWriter = new java.io.PrintWriter(new java.io.BufferedWriter(
                    new java.io.FileWriter(file.replaceAll("\\.jsonl$", "")
                                           + ".geom.jsonl")), false);
                long seed = 0; String wname = "";
                try {
                    seed = mc.getIntegratedServer().worlds[0].getSeed();
                    wname = mc.getIntegratedServer().getFolderName();
                } catch (Throwable ig) {}
                StringBuilder h = new StringBuilder(256);
                h.append("{\"header\":1,\"seed\":").append(seed)
                 .append(",\"world\":\"").append(wname)
                 .append("\",\"world_time\":").append(mc.world.getWorldTime())
                 .append(",\"total_time\":").append(mc.world.getTotalWorldTime())
                 .append(",\"x\":").append(mc.player.posX)
                 .append(",\"y\":").append(mc.player.posY)
                 .append(",\"z\":").append(mc.player.posZ)
                 .append(",\"yaw\":").append(mc.player.rotationYaw)
                 .append(",\"pitch\":").append(mc.player.rotationPitch)
                 .append(",\"vx\":").append(mc.player.motionX)
                 .append(",\"vy\":").append(mc.player.motionY)
                 .append(",\"vz\":").append(mc.player.motionZ)
                 .append(",\"og\":").append(mc.player.onGround ? 1 : 0)
                 .append(",\"hp\":").append(mc.player.getHealth())
                 .append(",\"food\":").append(mc.player.getFoodStats().getFoodLevel())
                 .append(",\"dim\":").append(mc.player.dimension)
                 .append(",\"gamemode\":\"").append(mc.playerController.getCurrentGameType().getName())
                 .append("\",\"difficulty\":\"").append(mc.world.getDifficulty().name())
                 // "default" (steve arm) or "slim" (alex) - offline UUID hash
                 // picks one; magma's first-person arm must match (set_skin).
                 .append("\",\"skin\":\"").append(mc.player.getSkinType())
                 .append("\",\"velocity_packets\":1,\"position_packets\":1}");
                recWriter.println(h.toString());
                recWriter.flush();
                // world snapshot: flush all dirty chunks to disk and copy the
                // save's region files next to the tape. Replay can then load
                // the ACTUAL world instead of trusting regenerated worldgen -
                // kills the populate-order/provenance divergence class
                // (OPEN_DIVERGENCES #1/#8/#18-20) at the source.
                String snapErr = null;
                try {
                    net.minecraft.server.integrated.IntegratedServer srv = mc.getIntegratedServer();
                    for (net.minecraft.world.WorldServer ws : srv.worlds)
                        if (ws != null) ws.saveAllChunks(true, null);
                    // saveAllChunks only queues AnvilChunkLoader writes. Copying
                    // immediately raced the File IO Thread and produced a valid
                    // region file missing the just-loaded course/player chunks.
                    net.minecraft.world.storage.ThreadedFileIOBase
                        .getThreadedIOInstance().waitForFinish();
                    java.io.File saveDir = new java.io.File(srv.getDataDirectory(),
                        "saves/" + srv.getFolderName());
                    // WHOLE save dir: level.dat + playerdata (initial inventory/
                    // xp/potions) + all dimension regions + data/ (maps,
                    // villages). Region-only proved insufficient (audit
                    // 2026-07-12): held-item/XP truth lives in playerdata.
                    java.io.File snapRoot = new java.io.File(
                        file.replaceAll("\\.jsonl$", "") + "_world");
                    final java.nio.file.Path src = saveDir.toPath(), dst = snapRoot.toPath();
                    java.nio.file.Files.walkFileTree(src, new java.nio.file.SimpleFileVisitor<java.nio.file.Path>() {
                        @Override public java.nio.file.FileVisitResult preVisitDirectory(
                                java.nio.file.Path d, java.nio.file.attribute.BasicFileAttributes a) throws java.io.IOException {
                            java.nio.file.Files.createDirectories(dst.resolve(src.relativize(d)));
                            return java.nio.file.FileVisitResult.CONTINUE;
                        }
                        @Override public java.nio.file.FileVisitResult visitFile(
                                java.nio.file.Path f, java.nio.file.attribute.BasicFileAttributes a) throws java.io.IOException {
                            java.nio.file.Files.copy(f, dst.resolve(src.relativize(f)),
                                java.nio.file.StandardCopyOption.REPLACE_EXISTING);
                            return java.nio.file.FileVisitResult.CONTINUE;
                        }
                        @Override public java.nio.file.FileVisitResult visitFileFailed(
                                java.nio.file.Path f, java.io.IOException e) {
                            return java.nio.file.FileVisitResult.CONTINUE; // session.lock etc.
                        }
                    });
                } catch (Throwable t) { snapErr = String.valueOf(t); }
                reply(r, "{\"ok\":true,\"file\":\"" + file + "\""
                    + (snapErr != null ? ",\"snapshot_error\":\"" + snapErr.replace('"','\'') + "\"" : "")
                    + "}");
            } catch (Exception ex) {
                recWriter = null;
                reply(r, err("recstart: " + ex));
            }
            return;
        }
        if (r.cmd.equals("recstop")) {
            long n = recTick;
            if (recWriter != null) { recWriter.flush(); recWriter.close(); recWriter = null; }
            if (recGeomWriter != null) { recGeomWriter.flush(); recGeomWriter.close(); recGeomWriter = null; }
            reply(r, "{\"ok\":true,\"ticks\":" + n + "}");
            return;
        }
        if (r.cmd.equals("overclock")) {
            long ms = r.action.has("ms") ? r.action.get("ms").getAsLong() : 1L;
            TimeHelper.serverTickLength = Math.max(1L, ms);
            r.resp.offer("{\"ok\":true,\"server_tick_length\":" + TimeHelper.serverTickLength + "}");
            return;
        }
        if (r.cmd.equals("dim")) {
            // Cross-dimension teleport (vanilla 1.11.2 has no command for this).
            // Runs on the server thread; player obj is recreated, so coverage picks
            // up the new dim on the next client tick once chunks render.
            final net.minecraft.server.MinecraftServer srv = mc.getIntegratedServer();
            if (srv == null) { r.resp.offer(err("no server")); return; }
            final int target = r.action.has("id") ? r.action.get("id").getAsInt() : 0;
            srv.addScheduledTask(new Runnable() { public void run() {
                try {
                    // Forge lazily loads dimension worlds; on a FRESH save the target dim
                    // is not initialized yet and changeDimension NPEs. Force-init first.
                    if (net.minecraftforge.common.DimensionManager.getWorld(target) == null) {
                        net.minecraftforge.common.DimensionManager.initDimension(target);
                    }
                    java.util.List<net.minecraft.entity.player.EntityPlayerMP> ps = srv.getPlayerList().getPlayers();
                    if (!ps.isEmpty()) {
                        net.minecraft.entity.player.EntityPlayerMP pl = ps.get(0);
                        // A failed/restarted harness can reconnect between setPortal()
                        // and the following entity tick.  A scripted setup transfer
                        // must not carry that pending vanilla portal contact into the
                        // destination and immediately bounce back on its next tick.
                        try {
                            java.lang.reflect.Field inPortal =
                                net.minecraft.entity.Entity.class.getDeclaredField("inPortal");
                            inPortal.setAccessible(true);
                            inPortal.setBoolean(pl, false);
                            java.lang.reflect.Field portalCounter =
                                net.minecraft.entity.Entity.class.getDeclaredField("portalCounter");
                            portalCounter.setAccessible(true);
                            portalCounter.setInt(pl, 0);
                            pl.timeUntilPortal = pl.getPortalCooldown();
                        } catch (Throwable rt) {
                            System.out.println("[qrl] portal-transit reset failed: " + rt);
                        }
                        // Scripted dim change has no portal context: Teleporter.
                        // placeInExistingPortal dereferences getLastPortalVec() (null
                        // unless the entity walked through a real portal) -> NPE that
                        // aborts placement mid-transfer (the long-standing "stuck on
                        // GuiDownloadTerrain" bug). Seed neutral portal fields first.
                        try {
                            java.lang.reflect.Field vf = net.minecraft.entity.Entity.class.getDeclaredField("lastPortalVec");
                            vf.setAccessible(true);
                            if (vf.get(pl) == null) vf.set(pl, new net.minecraft.util.math.Vec3d(0.5D, 0.0D, 0.0D));
                            java.lang.reflect.Field df = net.minecraft.entity.Entity.class.getDeclaredField("teleportDirection");
                            df.setAccessible(true);
                            if (df.get(pl) == null) df.set(pl, net.minecraft.util.EnumFacing.NORTH);
                        } catch (Throwable rt) { System.out.println("[qrl] portal-field seed failed: " + rt); }
                        pl.changeDimension(target);
                        // transferPlayerToDimension mutates this same player object.
                        // Keep arrival-portal collision from re-arming the transfer
                        // before the setup harness can move the player away.
                        pl.timeUntilPortal = pl.getPortalCooldown();
                    }
                } catch (Throwable t) {
                    System.out.println("[qrl] dim change failed: " + t);
                    t.printStackTrace();
                }
            }});
            r.resp.offer("{\"ok\":true,\"dim\":" + target + "}");
            return;
        }
        if (r.cmd.equals("portal_touch")) {
            // Pulse Entity.setPortal at the player's feet so inPortal is true for
            // the next server entity tick. Creative players need only 1 tick of
            // contact (getMaxInPortalTime==1). Headless lacks block-collision
            // reliable enough to light inPortal without this pulse.
            final net.minecraft.server.MinecraftServer srv = mc.getIntegratedServer();
            if (srv == null) { r.resp.offer(err("no server")); return; }
            final Req fr = r;
            srv.addScheduledTask(new Runnable() { public void run() {
                try {
                    java.util.List<net.minecraft.entity.player.EntityPlayerMP> ps =
                        srv.getPlayerList().getPlayers();
                    if (ps.isEmpty()) { fr.resp.offer(err("no player")); return; }
                    net.minecraft.entity.player.EntityPlayerMP pl = ps.get(0);
                    // Clear portal cooldown so a scripted re-enter works immediately.
                    try {
                        java.lang.reflect.Field tf =
                            net.minecraft.entity.Entity.class.getDeclaredField("timeUntilPortal");
                        tf.setAccessible(true);
                        tf.setInt(pl, 0);
                    } catch (Throwable ig) {}
                    BlockPos bp = new BlockPos(pl.posX, pl.posY, pl.posZ);
                    net.minecraft.block.Block at = pl.world.getBlockState(bp).getBlock();
                    net.minecraft.block.Block bel = pl.world.getBlockState(bp.down()).getBlock();
                    // End portal: invoke the real BlockEndPortal collision handler.
                    // This preserves its entity/bounding-box/remote-side guards instead
                    // of labelling a direct changeDimension call as a natural entry.
                    if (at == net.minecraft.init.Blocks.END_PORTAL
                        || bel == net.minecraft.init.Blocks.END_PORTAL) {
                        if (at != net.minecraft.init.Blocks.END_PORTAL) bp = bp.down();
                        net.minecraft.block.state.IBlockState state = pl.world.getBlockState(bp);
                        int preDim = pl.dimension;
                        boolean intersects = pl.getEntityBoundingBox().intersectsWith(
                            state.getBoundingBox(pl.world, bp).offset(bp));
                        state.getBlock().onEntityCollidedWithBlock(pl.world, bp, state, pl);
                        fr.resp.offer("{\"ok\":true,\"portal_touch\":true,"
                            + "\"kind\":\"end_collision_handler\",\"assisted\":true,"
                            + "\"intersects\":" + intersects + ",\"pre_dim\":" + preDim
                            + ",\"post_dim\":" + pl.dimension + ",\"x\":" + bp.getX()
                            + ",\"y\":" + bp.getY() + ",\"z\":" + bp.getZ() + "}");
                        return;
                    }
                    // Nether portal: invoke the real BlockPortal collision handler.
                    // The world normally calls this after an AABB overlap; headless
                    // lockstep can miss that callback, so prove the same precondition
                    // before dispatching it explicitly.
                    if (at != net.minecraft.init.Blocks.PORTAL
                        && bel == net.minecraft.init.Blocks.PORTAL)
                        bp = bp.down();
                    net.minecraft.block.state.IBlockState state = pl.world.getBlockState(bp);
                    if (state.getBlock() != net.minecraft.init.Blocks.PORTAL) {
                        fr.resp.offer(err("player is not inside a Nether portal"));
                        return;
                    }
                    int preDim = pl.dimension;
                    boolean intersects = pl.getEntityBoundingBox().intersectsWith(
                        state.getBoundingBox(pl.world, bp).offset(bp));
                    if (!intersects) {
                        fr.resp.offer(err("player AABB does not intersect Nether portal"));
                        return;
                    }
                    state.getBlock().onEntityCollidedWithBlock(pl.world, bp, state, pl);
                    fr.resp.offer("{\"ok\":true,\"portal_touch\":true,"
                        + "\"kind\":\"nether_collision_handler\",\"assisted\":true,"
                        + "\"intersects\":true,\"pre_dim\":" + preDim
                        + ",\"post_dim\":" + pl.dimension + ",\"x\":" + bp.getX()
                        + ",\"y\":" + bp.getY() + ",\"z\":" + bp.getZ() + "}");
                } catch (Throwable t) {
                    fr.resp.offer(err("portal_touch: " + t));
                }
            }});
            return;
        }
        if (r.cmd.equals("use_end_eye")) {
            /* Exercise the real interaction path for the final eye. setblocks is
             * intentionally insufficient here: BlockEndPortalFrame only runs the
             * completed-ring pattern match from ItemEnderEye.onItemUse. */
            final net.minecraft.server.MinecraftServer srv = mc.getIntegratedServer();
            if (srv == null) { r.resp.offer(err("no server")); return; }
            final int x = r.action.get("x").getAsInt();
            final int y = r.action.get("y").getAsInt();
            final int z = r.action.get("z").getAsInt();
            final Req fr = r;
            srv.addScheduledTask(new Runnable() { public void run() {
                try {
                    java.util.List<net.minecraft.entity.player.EntityPlayerMP> ps =
                        srv.getPlayerList().getPlayers();
                    if (ps.isEmpty()) { fr.resp.offer(err("no player")); return; }
                    net.minecraft.entity.player.EntityPlayerMP pl = ps.get(0);
                    net.minecraft.world.WorldServer w = pl.getServerWorld();
                    BlockPos pos = new BlockPos(x, y, z);
                    net.minecraft.block.state.IBlockState before = w.getBlockState(pos);
                    if (before.getBlock() != net.minecraft.init.Blocks.END_PORTAL_FRAME) {
                        fr.resp.offer(err("target is not an end portal frame"));
                        return;
                    }
                    boolean beforeEye = ((Boolean)before.getValue(
                        net.minecraft.block.BlockEndPortalFrame.EYE)).booleanValue();
                    if (beforeEye) {
                        fr.resp.offer(err("target frame already has an eye"));
                        return;
                    }
                    net.minecraft.item.ItemStack previous =
                        pl.getHeldItem(net.minecraft.util.EnumHand.MAIN_HAND).copy();
                    net.minecraft.item.ItemStack eye = new net.minecraft.item.ItemStack(
                        net.minecraft.init.Items.ENDER_EYE, 1);
                    net.minecraft.util.EnumActionResult used;
                    pl.setHeldItem(net.minecraft.util.EnumHand.MAIN_HAND, eye);
                    try {
                        used = pl.interactionManager.processRightClickBlock(
                            pl, w, eye, net.minecraft.util.EnumHand.MAIN_HAND, pos,
                            net.minecraft.util.EnumFacing.UP, 0.5F, 0.8125F, 0.5F);
                    } finally {
                        pl.setHeldItem(net.minecraft.util.EnumHand.MAIN_HAND, previous);
                        pl.inventoryContainer.detectAndSendChanges();
                    }
                    net.minecraft.block.state.IBlockState after = w.getBlockState(pos);
                    boolean afterEye = after.getBlock() == net.minecraft.init.Blocks.END_PORTAL_FRAME
                        && ((Boolean)after.getValue(
                            net.minecraft.block.BlockEndPortalFrame.EYE)).booleanValue();
                    com.google.gson.JsonObject out = new com.google.gson.JsonObject();
                    out.addProperty("ok", true);
                    out.addProperty("result", used.name());
                    out.addProperty("before_eye", beforeEye);
                    out.addProperty("after_eye", afterEye);
                    out.addProperty("num_ticks", TimeHelper.SyncManager.numTicks);
                    out.addProperty("x", x); out.addProperty("y", y); out.addProperty("z", z);
                    fr.resp.offer(out.toString());
                } catch (Throwable t) {
                    fr.resp.offer(err("use_end_eye: " + t));
                }
            }});
            return;
        }
        if (r.cmd.equals("set_pose")) {
            /* Deterministic capture pose on the authoritative server entity.
             * Repeated /tp followed by a wall-clock sleep let gravity move the
             * camera several blocks before ffmpeg sampled it. NO_GRAVITY keeps
             * the requested creative pose fixed without changing flying FOV. */
            final net.minecraft.server.MinecraftServer srv = mc.getIntegratedServer();
            if (srv == null) { r.resp.offer(err("no server")); return; }
            final double x = r.action.get("x").getAsDouble();
            final double y = r.action.get("y").getAsDouble();
            final double z = r.action.get("z").getAsDouble();
            final float yaw = r.action.get("yaw").getAsFloat();
            final float pitch = r.action.get("pitch").getAsFloat();
            final boolean noGravity = !r.action.has("no_gravity")
                || r.action.get("no_gravity").getAsBoolean();
            final Req fr = r;
            if (mc.player != null) {
                // Special commands do not pass through applyAction(). Clear both
                // keybind state and the already-sampled MovementInput so this
                // same client tick cannot reapply stale strafe after the reset.
                clearKeys(mc);
                key(mc.gameSettings.keyBindSprint, false);
                if (mc.player.movementInput != null) {
                    mc.player.movementInput.moveForward = 0.0F;
                    mc.player.movementInput.moveStrafe = 0.0F;
                    mc.player.movementInput.jump = false;
                    mc.player.movementInput.sneak = false;
                }
                mc.player.moveForward = 0.0F;
                mc.player.moveStrafing = 0.0F;
                mc.player.setSprinting(false);
                mc.player.setNoGravity(noGravity);
                mc.player.motionX = mc.player.motionY = mc.player.motionZ = 0.0D;
                mc.player.fallDistance = 0.0F;
                mc.player.setPositionAndRotation(x, y, z, yaw, pitch);
            }
            srv.addScheduledTask(new Runnable() { public void run() {
                try {
                    java.util.List<net.minecraft.entity.player.EntityPlayerMP> ps =
                        srv.getPlayerList().getPlayers();
                    if (ps.isEmpty()) { fr.resp.offer(err("no player")); return; }
                    net.minecraft.entity.player.EntityPlayerMP pl = ps.get(0);
                    pl.setNoGravity(noGravity);
                    pl.motionX = pl.motionY = pl.motionZ = 0.0D;
                    pl.fallDistance = 0.0F;
                    pl.connection.setPlayerLocation(x, y, z, yaw, pitch);
                    fr.resp.offer("{\"ok\":true,\"x\":" + x + ",\"y\":" + y
                        + ",\"z\":" + z + ",\"yaw\":" + yaw + ",\"pitch\":"
                        + pitch + ",\"no_gravity\":" + noGravity + "}");
                } catch (Throwable t) {
                    fr.resp.offer(err("set_pose: " + t));
                }
            }});
            return;
        }
        if (r.cmd.equals("kmode")) {
            // Runtime kernel A/B switch: flip any of the four drop-in kernels between
            // off/vanilla | native | sabotage WITHOUT restarting the client, then rebuild
            // all chunk VBOs (biome tint + AO are baked into vertices at mesh time; sin +
            // lightmap are per-frame and need no rebuild). Enables in-session off-vs-native
            // pixel diffs on the SAME world state - seconds per comparison instead of two
            // launches. NOTE: the AO hook needs the vanilla AO path live, so launch with
            // qao_mode "vanilla" (or any AO mode); kmode only flips the C-vs-Java choice.
            try {
                if (r.action.has("sin"))   QSinNative.setMode(opt(r.action, "sin"));
                if (r.action.has("lm"))    QLightmapNative.setMode(opt(r.action, "lm"));
                if (r.action.has("biome")) QBiomeNative.setMode(opt(r.action, "biome"));
                if (r.action.has("ao"))    QAOHook.setMode(opt(r.action, "ao"));
                if (mc.renderGlobal != null) mc.renderGlobal.loadRenderers();
                r.resp.offer("{\"ok\":true,\"sin\":" + QSinNative.MODE + ",\"lm\":" + QLightmapNative.MODE
                    + ",\"biome\":" + QBiomeNative.MODE + ",\"ao\":" + QAOHook.MODE + "}");
            } catch (Throwable t) { r.resp.offer(err("kmode: " + t)); }
            return;
        }
        if (r.cmd.equals("reload_renderers")) {
            try {
                if (mc.renderGlobal == null) {
                    r.resp.offer(err("no render global"));
                    return;
                }
                mc.renderGlobal.loadRenderers();
                r.resp.offer("{\"ok\":true}");
            } catch (Throwable t) {
                r.resp.offer(err("reload_renderers: " + t));
            }
            return;
        }
        if (r.cmd.equals("runcmds")) {
            // Execute a whole list of server commands in ONE tick via the integrated
            // server's command manager. One round-trip lands the entire course even
            // though the headless game idle-pauses between socket requests.
            net.minecraft.server.MinecraftServer srv = mc.getIntegratedServer();
            if (srv == null) { r.resp.offer(err("no server")); return; }
            // Run AS THE PLAYER when one exists: the server sender's world is worlds[0]
            // (overworld), so from another dimension every fill/tp/@p silently targets
            // the wrong world. The player sender scopes commands to their dimension.
            net.minecraft.command.ICommandSender sender = srv;
            try {
                java.util.List<net.minecraft.entity.player.EntityPlayerMP> ps2 = srv.getPlayerList().getPlayers();
                if (!ps2.isEmpty()) sender = ps2.get(0);
            } catch (Throwable ig) {}
            int n = 0, fail = 0;
            if (r.action.has("cmds")) {
                for (com.google.gson.JsonElement el : r.action.getAsJsonArray("cmds")) {
                    // executeCommand returns 0 on command FAILURE (e.g. fill into an
                    // unloaded chunk) without throwing - count that as failed too
                    try {
                        int ret = srv.getCommandManager().executeCommand(sender, el.getAsString());
                        if (ret > 0) n++; else fail++;
                    } catch (Throwable t) { fail++; }
                }
            }
            r.resp.offer("{\"ok\":true,\"ran\":" + n + ",\"failed\":" + fail + "}");
            return;
        }
        if (r.cmd.equals("cmd")) {
            if (mc.player == null) { reply(r, err("no world")); return; }
            mc.player.sendChatMessage(r.action.has("text") ? r.action.get("text").getAsString() : "");
            reply(r, "{\"ok\":true}");
            return;
        }
        if (r.cmd.equals("spawn") || r.cmd.equals("fluid")) {
            final MinecraftServer s = mc.getIntegratedServer();
            if (s == null || mc.player == null) { r.resp.offer(err("no world")); return; }
            final double px = mc.player.posX, py = mc.player.posY, pz = mc.player.posZ;
            final boolean isFluid = r.cmd.equals("fluid");
            final boolean lava = "lava".equalsIgnoreCase(opt(r.action, "type"));
            final int count = r.action.has("count") ? r.action.get("count").getAsInt() : 50;
            final int rad = r.action.has("radius") ? r.action.get("radius").getAsInt() : 8;
            s.addScheduledTask(new Runnable() { public void run() {   // runs on the server thread (safe world mutation)
                net.minecraft.world.WorldServer w = s.worlds[0];  // overworld fallback
                try {   // player's world: ops must follow the player across dimensions
                    java.util.List<net.minecraft.entity.player.EntityPlayerMP> qps = s.getPlayerList().getPlayers();
                    if (!qps.isEmpty()) w = qps.get(0).getServerWorld();
                } catch (Throwable ig) {}
                if (isFluid) {
                    net.minecraft.block.state.IBlockState bs =
                        (lava ? net.minecraft.init.Blocks.FLOWING_LAVA : net.minecraft.init.Blocks.FLOWING_WATER).getDefaultState();
                    BlockPos b = new BlockPos(px, py, pz);
                    for (int dx = -rad; dx <= rad; dx++)
                        for (int dy = 0; dy <= 4; dy++)
                            for (int dz = -rad; dz <= rad; dz++)
                                w.setBlockState(b.add(dx, dy, dz), bs);
                } else {
                    java.util.Random rnd = new java.util.Random(7);
                    for (int k = 0; k < count; k++) {
                        net.minecraft.entity.passive.EntityCow c = new net.minecraft.entity.passive.EntityCow(w);
                        c.setLocationAndAngles(px + (rnd.nextDouble() - 0.5) * rad * 2, py + 1,
                                               pz + (rnd.nextDouble() - 0.5) * rad * 2, 0f, 0f);
                        w.spawnEntity(c);
                    }
                }
            }});
            r.resp.offer("{\"ok\":true}");
            return;
        }
        if (r.cmd.equals("capture_light")) {
            final MinecraftServer s = mc.getIntegratedServer();
            if (s == null || mc.player == null) { r.resp.offer(err("no world")); return; }
            final int px = (int) Math.floor(mc.player.posX);
            final int py = (int) Math.floor(mc.player.posY);
            final int pz = (int) Math.floor(mc.player.posZ);
            final int target = r.action.has("count") ? r.action.get("count").getAsInt() : 5000;
            final int rad = r.action.has("radius") ? r.action.get("radius").getAsInt() : 16;
            final String dir = r.action.has("dir") ? r.action.get("dir").getAsString()
                : "/home/infatoshi/dev/minecraft/mc-1.11.2-env/c/render-opt/kernels/14_light_query/golden";
            final Req fr = r;
            s.addScheduledTask(new Runnable() { public void run() {
                try {
                    net.minecraft.world.WorldServer w = s.worlds[0];  // overworld fallback
                    try {   // player's world: ops must follow the player across dimensions
                        java.util.List<net.minecraft.entity.player.EntityPlayerMP> qps = s.getPlayerList().getPlayers();
                        if (!qps.isEmpty()) w = qps.get(0).getServerWorld();
                    } catch (Throwable ig) {}
                    // Drop a glass cluster (lightOpacity==0 -> useNeighborBrightness==true) so the
                    // nb=true branch (max-of-5-neighbors) is exercised, not just the own-light branch.
                    net.minecraft.block.state.IBlockState glass = net.minecraft.init.Blocks.GLASS.getDefaultState();
                    net.minecraft.block.state.IBlockState glow = net.minecraft.init.Blocks.GLOWSTONE.getDefaultState();
                    java.util.ArrayList<BlockPos> glassPos = new java.util.ArrayList<BlockPos>();
                    java.util.ArrayList<BlockPos> clusterPos = new java.util.ArrayList<BlockPos>();
                    for (int dx = -2; dx <= 2; dx++)
                        for (int dy = 0; dy <= 4; dy++)
                            for (int dz = -2; dz <= 2; dz++) {
                                BlockPos gp = new BlockPos(px + 6 + dx, py + dy, pz + dz);
                                // center column = glowstone (light source, nb=false); rest = glass (nb=true).
                                // Gives glass cells a brighter neighbor than their own light -> max-of-5 != own,
                                // so the candidate's neighbor-max branch is genuinely discriminated.
                                if (dx == 0 && dz == 0) {
                                    w.setBlockState(gp, glow);
                                } else {
                                    w.setBlockState(gp, glass);
                                    glassPos.add(gp);
                                }
                                clusterPos.add(gp);
                            }
                    // Force immediate BLOCK/SKY light propagation so neighbor lights are settled this tick.
                    for (BlockPos cp : clusterPos) {
                        w.checkLightFor(net.minecraft.world.EnumSkyBlock.BLOCK, cp);
                        w.checkLightFor(net.minecraft.world.EnumSkyBlock.SKY, cp);
                    }

                    java.io.File gd = new java.io.File(dir);
                    gd.mkdirs();
                    java.io.PrintWriter inW = new java.io.PrintWriter(new java.io.BufferedWriter(
                        new java.io.FileWriter(new java.io.File(gd, "inputs.txt"))));
                    java.io.PrintWriter goW = new java.io.PrintWriter(new java.io.BufferedWriter(
                        new java.io.FileWriter(new java.io.File(gd, "golden.txt"))));
                    net.minecraft.world.EnumSkyBlock[] types = {
                        net.minecraft.world.EnumSkyBlock.BLOCK, net.minecraft.world.EnumSkyBlock.SKY };
                    // Sample list: glass positions first (guarantee nb=true coverage), then a sweep.
                    java.util.ArrayList<BlockPos> sample = new java.util.ArrayList<BlockPos>(glassPos);
                    for (int dx = -rad; dx <= rad; dx++)
                        for (int dy = -rad; dy <= rad; dy++)
                            for (int dz = -rad; dz <= rad; dz++)
                                sample.add(new BlockPos(px + dx, py + dy, pz + dz));
                    int written = 0, nbTrue = 0;
                    for (BlockPos pos : sample) {
                        if (written >= target) break;
                        if (pos.getY() < 0 || pos.getY() > 255) continue;
                        if (!w.isValid(pos) || !w.isBlockLoaded(pos)) continue;
                        int nb = w.getBlockState(pos).useNeighborBrightness() ? 1 : 0;
                        if (nb == 1) nbTrue++;
                        for (net.minecraft.world.EnumSkyBlock t : types) {
                            int up    = w.getLightFor(t, pos.up());
                            int east  = w.getLightFor(t, pos.east());
                            int west  = w.getLightFor(t, pos.west());
                            int south = w.getLightFor(t, pos.south());
                            int north = w.getLightFor(t, pos.north());
                            int own   = w.getLightFor(t, pos);
                            int out   = w.getLightFromNeighborsFor(t, pos);
                            // record: type nb up east west south north own
                            inW.println((t == net.minecraft.world.EnumSkyBlock.SKY ? 1 : 0)
                                + " " + nb + " " + up + " " + east + " " + west
                                + " " + south + " " + north + " " + own);
                            goW.println(out);
                            written++;
                        }
                    }
                    inW.flush(); inW.close(); goW.flush(); goW.close();
                    System.out.println("[qrl] capture_light wrote " + written + " records ("
                        + nbTrue + " nb=true positions) to " + dir);
                    JsonObject o = new JsonObject();
                    o.addProperty("ok", true); o.addProperty("written", written);
                    o.addProperty("nb_true_positions", nbTrue); o.addProperty("dir", dir);
                    fr.resp.offer(o.toString());
                } catch (Throwable t) {
                    System.out.println("[qrl] capture_light failed: " + t);
                    fr.resp.offer(err("capture failed: " + t));
                }
            }});
            return;
        }
        if (r.cmd.equals("capture_fluidheight")) {
            final MinecraftServer s = mc.getIntegratedServer();
            if (s == null || mc.player == null) { r.resp.offer(err("no world")); return; }
            final int px = (int) Math.floor(mc.player.posX);
            final int py = (int) Math.floor(mc.player.posY);
            final int pz = (int) Math.floor(mc.player.posZ);
            final int target = r.action.has("count") ? r.action.get("count").getAsInt() : 2000;
            final int rad = r.action.has("radius") ? r.action.get("radius").getAsInt() : 10;
            final String dir = r.action.has("dir") ? r.action.get("dir").getAsString()
                : "/home/infatoshi/dev/minecraft/mc-1.11.2-env/c/render-opt/kernels/19_fluid_height/golden";
            final Req fr = r;
            s.addScheduledTask(new Runnable() { public void run() {
                try {
                    net.minecraft.world.WorldServer w = s.worlds[0];  // overworld fallback
                    try {   // player's world: ops must follow the player across dimensions
                        java.util.List<net.minecraft.entity.player.EntityPlayerMP> qps = s.getPlayerList().getPlayers();
                        if (!qps.isEmpty()) w = qps.get(0).getServerWorld();
                    } catch (Throwable ig) {}
                    // Reflect the private client renderer method getFluidHeight(IBlockAccess,BlockPos,Material)
                    Object brd = Minecraft.getMinecraft().getBlockRendererDispatcher();
                    java.lang.reflect.Field ff = brd.getClass().getDeclaredField("fluidRenderer");
                    ff.setAccessible(true);
                    Object fluidR = ff.get(brd);
                    java.lang.reflect.Method gfh = fluidR.getClass().getDeclaredMethod("getFluidHeight",
                        net.minecraft.world.IBlockAccess.class, net.minecraft.util.math.BlockPos.class,
                        net.minecraft.block.material.Material.class);
                    gfh.setAccessible(true);

                    java.io.File gd = new java.io.File(dir);
                    gd.mkdirs();
                    java.io.PrintWriter inW = new java.io.PrintWriter(new java.io.BufferedWriter(
                        new java.io.FileWriter(new java.io.File(gd, "inputs.txt"))));
                    java.io.PrintWriter goW = new java.io.PrintWriter(new java.io.BufferedWriter(
                        new java.io.FileWriter(new java.io.File(gd, "golden.txt"))));
                    int written = 0;
                    for (int dx = -rad; dx <= rad && written < target; dx++)
                      for (int dy = -3; dy <= 6 && written < target; dy++)
                        for (int dz = -rad; dz <= rad && written < target; dz++) {
                            BlockPos pos = new BlockPos(px + dx, py + dy, pz + dz);
                            if (pos.getY() < 0 || pos.getY() > 255) continue;
                            if (!w.isValid(pos) || !w.isBlockLoaded(pos)) continue;
                            net.minecraft.block.material.Material bm = w.getBlockState(pos).getMaterial();
                            if (bm != net.minecraft.block.material.Material.WATER
                                && bm != net.minecraft.block.material.Material.LAVA) continue;
                            // Snapshot exactly what getFluidHeight reads, per the 4 j-iterations.
                            StringBuilder sb = new StringBuilder();
                            for (int j = 0; j < 4; ++j) {
                                BlockPos bp = pos.add(-(j & 1), 0, -(j >> 1 & 1));
                                int upSame = (w.getBlockState(bp.up()).getMaterial() == bm) ? 1 : 0;
                                net.minecraft.block.state.IBlockState ibs = w.getBlockState(bp);
                                net.minecraft.block.material.Material m = ibs.getMaterial();
                                int kind, k = 0;
                                if (m == bm) {
                                    kind = 0;
                                    k = ((Integer) ibs.getValue(net.minecraft.block.BlockLiquid.LEVEL)).intValue();
                                } else if (!m.isSolid()) {
                                    kind = 1;
                                } else {
                                    kind = 2;
                                }
                                if (j > 0) sb.append(' ');
                                sb.append(upSame).append(' ').append(kind).append(' ').append(k);
                            }
                            float out = (Float) gfh.invoke(fluidR, w, pos, bm);
                            inW.println(sb.toString());
                            goW.println(Float.floatToRawIntBits(out));
                            written++;
                        }
                    inW.flush(); inW.close(); goW.flush(); goW.close();
                    System.out.println("[qrl] capture_fluidheight wrote " + written + " records to " + dir);
                    JsonObject o = new JsonObject();
                    o.addProperty("ok", true); o.addProperty("written", written); o.addProperty("dir", dir);
                    fr.resp.offer(o.toString());
                } catch (Throwable t) {
                    System.out.println("[qrl] capture_fluidheight failed: " + t);
                    fr.resp.offer(err("capture failed: " + t));
                }
            }});
            return;
        }
        if (r.cmd.equals("capture_biome")) {
            final MinecraftServer s = mc.getIntegratedServer();
            if (s == null || mc.player == null) { r.resp.offer(err("no world")); return; }
            final int px = (int) Math.floor(mc.player.posX);
            final int py = (int) Math.floor(mc.player.posY);
            final int pz = (int) Math.floor(mc.player.posZ);
            final int target = r.action.has("count") ? r.action.get("count").getAsInt() : 1000;
            final int rad = r.action.has("radius") ? r.action.get("radius").getAsInt() : 48;
            final String dir = r.action.has("dir") ? r.action.get("dir").getAsString()
                : "/home/infatoshi/dev/minecraft/mc-1.11.2-env/c/render-opt/kernels/18_biome_color_blend/golden";
            final Req fr = r;
            s.addScheduledTask(new Runnable() { public void run() {
                try {
                    net.minecraft.world.WorldServer w = s.worlds[0];  // overworld fallback
                    try {   // player's world: ops must follow the player across dimensions
                        java.util.List<net.minecraft.entity.player.EntityPlayerMP> qps = s.getPlayerList().getPlayers();
                        if (!qps.isEmpty()) w = qps.get(0).getServerWorld();
                    } catch (Throwable ig) {}
                    java.io.File gd = new java.io.File(dir);
                    gd.mkdirs();
                    java.io.PrintWriter inW = new java.io.PrintWriter(new java.io.BufferedWriter(
                        new java.io.FileWriter(new java.io.File(gd, "inputs.txt"))));
                    java.io.PrintWriter goW = new java.io.PrintWriter(new java.io.BufferedWriter(
                        new java.io.FileWriter(new java.io.File(gd, "golden.txt"))));
                    int written = 0;
                    for (int dx = -rad; dx <= rad && written < target; dx += 3)
                      for (int dz = -rad; dz <= rad && written < target; dz += 3) {
                            BlockPos pos = new BlockPos(px + dx, py, pz + dz);
                            if (!w.isValid(pos) || !w.isBlockLoaded(pos)) continue;
                            // Snapshot the 3x3 grass colors the blend reads (same order as the method).
                            StringBuilder sb = new StringBuilder();
                            int n = 0;
                            for (BlockPos.MutableBlockPos bp : BlockPos.getAllInBoxMutable(
                                    pos.add(-1, 0, -1), pos.add(1, 0, 1))) {
                                int l = w.getBiome(bp).getGrassColorAtPos(bp);
                                if (n++ > 0) sb.append(' ');
                                sb.append(l);
                            }
                            int out = net.minecraft.world.biome.BiomeColorHelper.getGrassColorAtPos(w, pos);
                            inW.println(sb.toString());
                            goW.println(out);
                            written++;
                        }
                    inW.flush(); inW.close(); goW.flush(); goW.close();
                    System.out.println("[qrl] capture_biome wrote " + written + " records to " + dir);
                    JsonObject o = new JsonObject();
                    o.addProperty("ok", true); o.addProperty("written", written); o.addProperty("dir", dir);
                    fr.resp.offer(o.toString());
                } catch (Throwable t) {
                    System.out.println("[qrl] capture_biome failed: " + t);
                    fr.resp.offer(err("capture failed: " + t));
                }
            }});
            return;
        }
        // ---- 21_should_side_render: Block.shouldSideBeRendered (base impl) ----
        if (r.cmd.equals("capture_shouldsiderender")) {
            final MinecraftServer s = mc.getIntegratedServer();
            if (s == null || mc.player == null) { r.resp.offer(err("no world")); return; }
            final int px = (int) Math.floor(mc.player.posX);
            final int py = (int) Math.floor(mc.player.posY);
            final int pz = (int) Math.floor(mc.player.posZ);
            final int target = r.action.has("count") ? r.action.get("count").getAsInt() : 5000;
            final int rad = r.action.has("radius") ? r.action.get("radius").getAsInt() : 12;
            final String dir = r.action.has("dir") ? r.action.get("dir").getAsString()
                : "/home/infatoshi/dev/minecraft/mc-1.11.2-env/c/render-opt/kernels/21_should_side_render/golden";
            final Req fr = r;
            s.addScheduledTask(new Runnable() { public void run() {
                try {
                    net.minecraft.world.WorldServer w = s.worlds[0];  // overworld fallback
                    try {   // player's world: ops must follow the player across dimensions
                        java.util.List<net.minecraft.entity.player.EntityPlayerMP> qps = s.getPlayerList().getPlayers();
                        if (!qps.isEmpty()) w = qps.get(0).getServerWorld();
                    } catch (Throwable ig) {}
                    // Stage partial-bounding-box blocks that use the BASE Block.shouldSideBeRendered
                    // (soul sand -> maxY<1 UP branch; cactus -> 4 horizontal early-return branches),
                    // then capture immediately before any block update can pop them.
                    java.util.ArrayList<BlockPos> staged = new java.util.ArrayList<BlockPos>();
                    int si = 0;
                    net.minecraft.block.state.IBlockState[] partials = {
                        net.minecraft.init.Blocks.SOUL_SAND.getDefaultState(),
                        net.minecraft.init.Blocks.CACTUS.getDefaultState() };
                    for (net.minecraft.block.state.IBlockState ps : partials)
                        for (int dz = -2; dz <= 2; dz++) {
                            BlockPos b = new BlockPos(px + 5 + si, py, pz + dz);
                            w.setBlockState(b, ps); staged.add(b);
                        }
                    si++;
                    java.io.File gd = new java.io.File(dir); gd.mkdirs();
                    java.io.PrintWriter inW = new java.io.PrintWriter(new java.io.BufferedWriter(
                        new java.io.FileWriter(new java.io.File(gd, "inputs.txt"))));
                    java.io.PrintWriter goW = new java.io.PrintWriter(new java.io.BufferedWriter(
                        new java.io.FileWriter(new java.io.File(gd, "golden.txt"))));
                    java.util.ArrayList<BlockPos> sample = new java.util.ArrayList<BlockPos>(staged);
                    for (int dx = -rad; dx <= rad; dx++)
                      for (int dy = -rad; dy <= rad; dy++)
                        for (int dz = -rad; dz <= rad; dz++)
                            sample.add(new BlockPos(px + dx, py + dy, pz + dz));
                    int written = 0, tcount = 0;
                    for (BlockPos pos : sample) {
                        if (written >= target) break;
                        if (pos.getY() < 0 || pos.getY() > 255) continue;
                        if (!w.isValid(pos) || !w.isBlockLoaded(pos)) continue;
                        net.minecraft.block.state.IBlockState st = w.getBlockState(pos);
                        if (st.getBlock() == net.minecraft.init.Blocks.AIR) continue;
                        // only blocks using the BASE Block.shouldSideBeRendered (no override) match the C
                        // port. Walk the whole hierarchy up to (excluding) Block -- an override may live in a
                        // superclass (e.g. BlockLiquid for water, whose leaf is BlockDynamicLiquid).
                        boolean overrides = false;
                        for (Class<?> bc = st.getBlock().getClass();
                             bc != null && bc != net.minecraft.block.Block.class; bc = bc.getSuperclass()) {
                            try { bc.getDeclaredMethod("shouldSideBeRendered",
                                    net.minecraft.block.state.IBlockState.class, net.minecraft.world.IBlockAccess.class,
                                    net.minecraft.util.math.BlockPos.class, net.minecraft.util.EnumFacing.class);
                                  overrides = true; break; }
                            catch (NoSuchMethodException nsme) { /* keep walking */ }
                        }
                        if (overrides) continue;
                        net.minecraft.util.math.AxisAlignedBB bb;
                        try { bb = st.getBoundingBox(w, pos); } catch (Throwable ex) { continue; }
                        for (net.minecraft.util.EnumFacing side : net.minecraft.util.EnumFacing.values()) {
                            if (written >= target) break;
                            BlockPos np = pos.offset(side);
                            int nbr = w.getBlockState(np).doesSideBlockRendering(w, np, side.getOpposite()) ? 1 : 0;
                            boolean out = st.shouldSideBeRendered(w, pos, side);
                            if (out) tcount++;
                            inW.println(side.getIndex()
                                + " " + Double.doubleToRawLongBits(bb.minX)
                                + " " + Double.doubleToRawLongBits(bb.minY)
                                + " " + Double.doubleToRawLongBits(bb.minZ)
                                + " " + Double.doubleToRawLongBits(bb.maxX)
                                + " " + Double.doubleToRawLongBits(bb.maxY)
                                + " " + Double.doubleToRawLongBits(bb.maxZ)
                                + " " + nbr);
                            goW.println(out ? 1 : 0);
                            written++;
                        }
                    }
                    inW.flush(); inW.close(); goW.flush(); goW.close();
                    System.out.println("[qrl] capture_shouldsiderender wrote " + written + " (" + tcount + " true) to " + dir);
                    JsonObject o = new JsonObject(); o.addProperty("ok", true);
                    o.addProperty("written", written); o.addProperty("true_count", tcount);
                    o.addProperty("dir", dir); fr.resp.offer(o.toString());
                } catch (Throwable t) {
                    System.out.println("[qrl] capture_shouldsiderender failed: " + t);
                    fr.resp.offer(err("capture failed: " + t));
                }
            }});
            return;
        }
        // ---- 23_render_quads_flat / 24_render_quads_smooth: BlockModelRenderer vertex emit ----
        if (r.cmd.equals("capture_quadsflat") || r.cmd.equals("capture_quadssmooth")) {
            final MinecraftServer s = mc.getIntegratedServer();
            if (s == null || mc.player == null) { r.resp.offer(err("no world")); return; }
            final boolean smooth = r.cmd.equals("capture_quadssmooth");
            final int px = (int) Math.floor(mc.player.posX);
            final int py = (int) Math.floor(mc.player.posY);
            final int pz = (int) Math.floor(mc.player.posZ);
            final String dir = r.action.has("dir") ? r.action.get("dir").getAsString()
                : ("/home/infatoshi/dev/minecraft/mc-1.11.2-env/c/render-opt/kernels/"
                   + (smooth ? "24_render_quads_smooth" : "23_render_quads_flat") + "/golden");
            final Req fr = r;
            s.addScheduledTask(new Runnable() { public void run() {
                try {
                    net.minecraft.world.WorldServer w = s.worlds[0];  // overworld fallback
                    try {   // player's world: ops must follow the player across dimensions
                        java.util.List<net.minecraft.entity.player.EntityPlayerMP> qps = s.getPlayerList().getPlayers();
                        if (!qps.isEmpty()) w = qps.get(0).getServerWorld();
                    } catch (Throwable ig) {}
                    net.minecraft.client.renderer.BlockRendererDispatcher brd =
                        Minecraft.getMinecraft().getBlockRendererDispatcher();
                    net.minecraft.client.renderer.BlockModelRenderer bmr = brd.getBlockModelRenderer();
                    net.minecraft.client.renderer.color.BlockColors bcolors = Minecraft.getMinecraft().getBlockColors();
                    java.lang.reflect.Field rib = net.minecraft.client.renderer.VertexBuffer.class
                        .getDeclaredField("rawIntBuffer"); rib.setAccessible(true);
                    java.lang.reflect.Method rqf = null, rqs = null;
                    java.lang.reflect.Constructor<?> aoCtor = null;
                    java.lang.reflect.Field fvb = null, fvcm = null;
                    if (!smooth) {
                        rqf = net.minecraft.client.renderer.BlockModelRenderer.class.getDeclaredMethod(
                            "renderQuadsFlat", net.minecraft.world.IBlockAccess.class,
                            net.minecraft.block.state.IBlockState.class, net.minecraft.util.math.BlockPos.class,
                            int.class, boolean.class, net.minecraft.client.renderer.VertexBuffer.class,
                            java.util.List.class, java.util.BitSet.class);
                        rqf.setAccessible(true);
                    } else {
                        Class<?> aoCls = Class.forName("net.minecraft.client.renderer.BlockModelRenderer$AmbientOcclusionFace");
                        rqs = net.minecraft.client.renderer.BlockModelRenderer.class.getDeclaredMethod(
                            "renderQuadsSmooth", net.minecraft.world.IBlockAccess.class,
                            net.minecraft.block.state.IBlockState.class, net.minecraft.util.math.BlockPos.class,
                            net.minecraft.client.renderer.VertexBuffer.class, java.util.List.class,
                            float[].class, java.util.BitSet.class, aoCls);
                        rqs.setAccessible(true);
                        aoCtor = aoCls.getDeclaredConstructor(net.minecraft.client.renderer.BlockModelRenderer.class);
                        aoCtor.setAccessible(true);
                        fvb = aoCls.getDeclaredField("vertexBrightness"); fvb.setAccessible(true);
                        fvcm = aoCls.getDeclaredField("vertexColorMultiplier"); fvcm.setAccessible(true);
                    }
                    // Stage representative blocks: grass/leaves (tint), stairs (multi-quad + AO variation), solids.
                    net.minecraft.block.state.IBlockState[] tests = {
                        net.minecraft.init.Blocks.GRASS.getDefaultState(),
                        net.minecraft.init.Blocks.LEAVES.getDefaultState(),
                        net.minecraft.init.Blocks.OAK_STAIRS.getDefaultState(),
                        net.minecraft.init.Blocks.STONE.getDefaultState(),
                        net.minecraft.init.Blocks.DIRT.getDefaultState(),
                        net.minecraft.init.Blocks.COBBLESTONE.getDefaultState(),
                        net.minecraft.init.Blocks.LOG.getDefaultState() };
                    java.util.ArrayList<BlockPos> tpos = new java.util.ArrayList<BlockPos>();
                    int i = 0;
                    for (net.minecraft.block.state.IBlockState ts : tests) {
                        BlockPos b = new BlockPos(px + 5 + i, py + 1, pz);
                        w.setBlockState(b, ts);
                        w.checkLightFor(net.minecraft.world.EnumSkyBlock.BLOCK, b);
                        w.checkLightFor(net.minecraft.world.EnumSkyBlock.SKY, b);
                        tpos.add(b); i++;
                    }
                    java.util.ArrayList<net.minecraft.util.EnumFacing> faces =
                        new java.util.ArrayList<net.minecraft.util.EnumFacing>();
                    faces.add(null);
                    for (net.minecraft.util.EnumFacing ef : net.minecraft.util.EnumFacing.values()) faces.add(ef);

                    java.io.File gd = new java.io.File(dir); gd.mkdirs();
                    java.io.PrintWriter inW = new java.io.PrintWriter(new java.io.BufferedWriter(
                        new java.io.FileWriter(new java.io.File(gd, "inputs.txt"))));
                    java.io.PrintWriter goW = new java.io.PrintWriter(new java.io.BufferedWriter(
                        new java.io.FileWriter(new java.io.File(gd, "golden.txt"))));
                    int written = 0, tinted = 0;
                    for (BlockPos pos : tpos) {
                        net.minecraft.block.state.IBlockState st = w.getBlockState(pos);
                        net.minecraft.client.renderer.block.model.IBakedModel model = brd.getModelForState(st);
                        net.minecraft.util.math.Vec3d off = st.getOffset(w, pos);
                        double d0 = pos.getX() + off.xCoord, d1 = pos.getY() + off.yCoord, d2 = pos.getZ() + off.zCoord;
                        int brightnessIn = st.getPackedLightmapCoords(w, pos);
                        for (net.minecraft.util.EnumFacing f : faces) {
                            java.util.List<net.minecraft.client.renderer.block.model.BakedQuad> quads =
                                model.getQuads(st, f, 0L);
                            for (net.minecraft.client.renderer.block.model.BakedQuad q : quads) {
                                int[] vd = q.getVertexData();
                                if (vd.length != 28) continue;
                                int hasTint = q.hasTintIndex() ? 1 : 0;
                                int k = q.hasTintIndex() ? bcolors.colorMultiplier(st, w, pos, q.getTintIndex()) : 0;
                                if (hasTint == 1) tinted++;
                                net.minecraft.client.renderer.VertexBuffer buf =
                                    new net.minecraft.client.renderer.VertexBuffer(0x200000);
                                buf.begin(7, net.minecraft.client.renderer.vertex.DefaultVertexFormats.BLOCK);
                                StringBuilder sb = new StringBuilder();
                                for (int z = 0; z < 28; z++) sb.append(vd[z]).append(' ');
                                if (!smooth) {
                                    int applyDiff = q.shouldApplyDiffuseLighting() ? 1 : 0;
                                    float diffuse = applyDiff == 1
                                        ? net.minecraftforge.client.model.pipeline.LightUtil.diffuseLight(q.getFace())
                                        : 1.0f;
                                    rqf.invoke(bmr, w, st, pos, brightnessIn, false, buf,
                                        java.util.Collections.singletonList(q), new java.util.BitSet(3));
                                    if (buf.getVertexCount() != 4) continue;
                                    sb.append(brightnessIn).append(' ').append(hasTint).append(' ').append(k)
                                      .append(' ').append(applyDiff).append(' ').append(Float.floatToRawIntBits(diffuse))
                                      .append(' ').append(Double.doubleToRawLongBits(d0))
                                      .append(' ').append(Double.doubleToRawLongBits(d1))
                                      .append(' ').append(Double.doubleToRawLongBits(d2));
                                } else {
                                    Object ao = aoCtor.newInstance(bmr);
                                    float[] afloat = new float[net.minecraft.util.EnumFacing.values().length * 2];
                                    rqs.invoke(bmr, w, st, pos, buf, java.util.Collections.singletonList(q),
                                        afloat, new java.util.BitSet(3), ao);
                                    if (buf.getVertexCount() != 4) continue;
                                    int[] vb = (int[]) fvb.get(ao);
                                    float[] vcm = (float[]) fvcm.get(ao);
                                    sb.append(vb[0]).append(' ').append(vb[1]).append(' ').append(vb[2]).append(' ').append(vb[3])
                                      .append(' ').append(Float.floatToRawIntBits(vcm[0]))
                                      .append(' ').append(Float.floatToRawIntBits(vcm[1]))
                                      .append(' ').append(Float.floatToRawIntBits(vcm[2]))
                                      .append(' ').append(Float.floatToRawIntBits(vcm[3]))
                                      .append(' ').append(hasTint).append(' ').append(k)
                                      .append(' ').append(Double.doubleToRawLongBits(d0))
                                      .append(' ').append(Double.doubleToRawLongBits(d1))
                                      .append(' ').append(Double.doubleToRawLongBits(d2));
                                }
                                java.nio.IntBuffer ib = (java.nio.IntBuffer) rib.get(buf);
                                inW.println(sb.toString());
                                for (int z = 0; z < 28; z++) goW.println(ib.get(z));
                                written++;
                            }
                        }
                    }
                    inW.flush(); inW.close(); goW.flush(); goW.close();
                    System.out.println("[qrl] " + r.cmd + " wrote " + written + " quads (" + tinted + " tinted) to " + dir);
                    JsonObject o = new JsonObject(); o.addProperty("ok", true);
                    o.addProperty("quads", written); o.addProperty("tinted", tinted);
                    o.addProperty("dir", dir); fr.resp.offer(o.toString());
                } catch (Throwable t) {
                    System.out.println("[qrl] " + r.cmd + " failed: " + t);
                    java.io.StringWriter swr = new java.io.StringWriter();
                    t.printStackTrace(new java.io.PrintWriter(swr));
                    System.out.println(swr.toString());
                    fr.resp.offer(err("capture failed: " + t));
                }
            }});
            return;
        }
        // ---- 20_fluid_quad_gen: BlockFluidRenderer.renderFluid vertex emit ----
        if (r.cmd.equals("capture_fluidquads")) {
            final MinecraftServer s = mc.getIntegratedServer();
            if (s == null || mc.player == null) { r.resp.offer(err("no world")); return; }
            final int px = (int) Math.floor(mc.player.posX);
            final int py = (int) Math.floor(mc.player.posY);
            final int pz = (int) Math.floor(mc.player.posZ);
            final int target = r.action.has("count") ? r.action.get("count").getAsInt() : 200;
            final int rad = r.action.has("radius") ? r.action.get("radius").getAsInt() : 10;
            final String dir = r.action.has("dir") ? r.action.get("dir").getAsString()
                : "/home/infatoshi/dev/minecraft/mc-1.11.2-env/c/render-opt/kernels/20_fluid_quad_gen/golden";
            final Req fr = r;
            s.addScheduledTask(new Runnable() { public void run() {
                try {
                    net.minecraft.world.WorldServer w = s.worlds[0];  // overworld fallback
                    try {   // player's world: ops must follow the player across dimensions
                        java.util.List<net.minecraft.entity.player.EntityPlayerMP> qps = s.getPlayerList().getPlayers();
                        if (!qps.isEmpty()) w = qps.get(0).getServerWorld();
                    } catch (Throwable ig) {}
                    net.minecraft.client.renderer.BlockRendererDispatcher brd =
                        Minecraft.getMinecraft().getBlockRendererDispatcher();
                    java.lang.reflect.Field ff = brd.getClass().getDeclaredField("fluidRenderer");
                    ff.setAccessible(true);
                    Object fluidR = ff.get(brd);
                    java.lang.reflect.Method rf = fluidR.getClass().getDeclaredMethod("renderFluid",
                        net.minecraft.world.IBlockAccess.class, net.minecraft.block.state.IBlockState.class,
                        net.minecraft.util.math.BlockPos.class, net.minecraft.client.renderer.VertexBuffer.class);
                    rf.setAccessible(true);
                    java.lang.reflect.Field rib = net.minecraft.client.renderer.VertexBuffer.class
                        .getDeclaredField("rawIntBuffer"); rib.setAccessible(true);
                    // 20 UV-FIX: dump the runtime atlas sprite UV tables so a C port can reproduce UVs.
                    java.lang.reflect.Field fW2 = fluidR.getClass().getDeclaredField("atlasSpritesWater");
                    fW2.setAccessible(true);
                    java.lang.reflect.Field fL2 = fluidR.getClass().getDeclaredField("atlasSpritesLava");
                    fL2.setAccessible(true);
                    net.minecraft.client.renderer.texture.TextureAtlasSprite[] spW =
                        (net.minecraft.client.renderer.texture.TextureAtlasSprite[]) fW2.get(fluidR);
                    net.minecraft.client.renderer.texture.TextureAtlasSprite[] spL =
                        (net.minecraft.client.renderer.texture.TextureAtlasSprite[]) fL2.get(fluidR);
                    java.lang.reflect.Method gSlope = net.minecraft.block.BlockLiquid.class.getDeclaredMethod(
                        "getSlopeAngle", net.minecraft.world.IBlockAccess.class,
                        net.minecraft.util.math.BlockPos.class, net.minecraft.block.material.Material.class,
                        net.minecraft.block.state.IBlockState.class); gSlope.setAccessible(true);

                    // 20 ENRICHED: also dump the water_overlay sprite + per-block leaf inputs
                    // (color, render flags, corner heights, lightmaps, overlay selection) so a
                    // standalone C port of renderFluid can be bit-exact. See kernel README.
                    java.lang.reflect.Field fOv = fluidR.getClass().getDeclaredField("atlasSpriteWaterOverlay");
                    fOv.setAccessible(true);
                    net.minecraft.client.renderer.texture.TextureAtlasSprite spOv =
                        (net.minecraft.client.renderer.texture.TextureAtlasSprite) fOv.get(fluidR);
                    java.lang.reflect.Field fBC = fluidR.getClass().getDeclaredField("blockColors");
                    fBC.setAccessible(true);
                    net.minecraft.client.renderer.color.BlockColors blockColors =
                        (net.minecraft.client.renderer.color.BlockColors) fBC.get(fluidR);
                    java.lang.reflect.Method mFH = fluidR.getClass().getDeclaredMethod("getFluidHeight",
                        net.minecraft.world.IBlockAccess.class, net.minecraft.util.math.BlockPos.class,
                        net.minecraft.block.material.Material.class);
                    mFH.setAccessible(true);

                    java.io.File gd = new java.io.File(dir); gd.mkdirs();
                    // sprite_uv.txt: water[0] water[1] lava[0] lava[1] water_overlay, each minU maxU minV maxV (float bits)
                    java.io.PrintWriter uvW = new java.io.PrintWriter(new java.io.BufferedWriter(
                        new java.io.FileWriter(new java.io.File(gd, "sprite_uv.txt"))));
                    net.minecraft.client.renderer.texture.TextureAtlasSprite[] allSp =
                        { spW[0], spW[1], spL[0], spL[1], spOv };
                    String[] spNames = { "water_still", "water_flow", "lava_still", "lava_flow", "water_overlay" };
                    for (int z = 0; z < 5; z++) {
                        net.minecraft.client.renderer.texture.TextureAtlasSprite sp = allSp[z];
                        uvW.println(spNames[z] + " " + Float.floatToRawIntBits(sp.getMinU())
                            + " " + Float.floatToRawIntBits(sp.getMaxU())
                            + " " + Float.floatToRawIntBits(sp.getMinV())
                            + " " + Float.floatToRawIntBits(sp.getMaxV()));
                    }
                    uvW.flush(); uvW.close();
                    java.io.PrintWriter inW = new java.io.PrintWriter(new java.io.BufferedWriter(
                        new java.io.FileWriter(new java.io.File(gd, "inputs.txt"))));
                    java.io.PrintWriter goW = new java.io.PrintWriter(new java.io.BufferedWriter(
                        new java.io.FileWriter(new java.io.File(gd, "golden.txt"))));
                    // inputs.txt line 1 = sprite UV header (20 ints): wstill wflow lstill lflow woverlay,
                    // each minU maxU minV maxV (float bits). Lets candidate.c reproduce getInterpolatedU/V.
                    StringBuilder hdr = new StringBuilder();
                    for (int z = 0; z < 5; z++) {
                        net.minecraft.client.renderer.texture.TextureAtlasSprite sp = allSp[z];
                        if (z > 0) hdr.append(' ');
                        hdr.append(Float.floatToRawIntBits(sp.getMinU())).append(' ')
                           .append(Float.floatToRawIntBits(sp.getMaxU())).append(' ')
                           .append(Float.floatToRawIntBits(sp.getMinV())).append(' ')
                           .append(Float.floatToRawIntBits(sp.getMaxV()));
                    }
                    inW.println(hdr.toString());
                    net.minecraft.util.EnumFacing[] SIDE_FACE = {
                        net.minecraft.util.EnumFacing.NORTH, net.minecraft.util.EnumFacing.SOUTH,
                        net.minecraft.util.EnumFacing.WEST, net.minecraft.util.EnumFacing.EAST };
                    int written = 0;
                    for (int dx = -rad; dx <= rad && written < target; dx++)
                      for (int dy = -3; dy <= 6 && written < target; dy++)
                        for (int dz = -rad; dz <= rad && written < target; dz++) {
                            BlockPos pos = new BlockPos(px + dx, py + dy, pz + dz);
                            if (pos.getY() < 0 || pos.getY() > 255) continue;
                            if (!w.isValid(pos) || !w.isBlockLoaded(pos)) continue;
                            net.minecraft.block.state.IBlockState st = w.getBlockState(pos);
                            net.minecraft.block.material.Material bm = st.getMaterial();
                            if (bm != net.minecraft.block.material.Material.WATER
                                && bm != net.minecraft.block.material.Material.LAVA) continue;
                            float slope = (Float) gSlope.invoke(null, w, pos, bm, st);
                            net.minecraft.client.renderer.VertexBuffer buf =
                                new net.minecraft.client.renderer.VertexBuffer(0x200000);
                            buf.begin(7, net.minecraft.client.renderer.vertex.DefaultVertexFormats.BLOCK);
                            boolean ret = (Boolean) rf.invoke(fluidR, w, st, pos, buf);
                            int vc = buf.getVertexCount();
                            // ENRICHED input snapshot (27 fields): everything renderFluid reads, so a
                            // standalone C port can be bit-exact. Order matches candidate.c:
                            //   x y z isLava colorInt flag1 flag2 a0 a1 a2 a3 renderSidesUp slopeBits
                            //   f7b f8b f9b f10b lmUp lmDown lmS0 lmS1 lmS2 lmS3 ov0 ov1 ov2 ov3
                            boolean isLava = bm == net.minecraft.block.material.Material.LAVA;
                            int colorInt = blockColors.colorMultiplier(st, w, pos, 0);
                            int flag1 = st.shouldSideBeRendered(w, pos, net.minecraft.util.EnumFacing.UP) ? 1 : 0;
                            int flag2 = st.shouldSideBeRendered(w, pos, net.minecraft.util.EnumFacing.DOWN) ? 1 : 0;
                            int[] ab = new int[4];
                            for (int s = 0; s < 4; s++)
                                ab[s] = st.shouldSideBeRendered(w, pos, SIDE_FACE[s]) ? 1 : 0;
                            net.minecraft.block.BlockLiquid bl = (net.minecraft.block.BlockLiquid) st.getBlock();
                            int rsUp = bl.shouldRenderSides(w, pos.up()) ? 1 : 0;
                            float f7  = (Float) mFH.invoke(fluidR, w, pos, bm);
                            float f8  = (Float) mFH.invoke(fluidR, w, pos.south(), bm);
                            float f9  = (Float) mFH.invoke(fluidR, w, pos.east().south(), bm);
                            float f10 = (Float) mFH.invoke(fluidR, w, pos.east(), bm);
                            int lmUp   = st.getPackedLightmapCoords(w, pos);
                            int lmDown = st.getPackedLightmapCoords(w, pos.down());
                            // per-side neighbor pos (i1=0 north k1--, 1 south k1++, 2 west j1--, 3 east j1++),
                            // lightmap there, and water_overlay selection (water + glass/stained-glass neighbor).
                            int[] dj = { 0, 0, -1, 1 };  // j1 (x) for i1=0..3
                            int[] dk = { -1, 1, 0, 0 };  // k1 (z) for i1=0..3
                            int[] lmS = new int[4];
                            int[] ov = new int[4];
                            for (int i1 = 0; i1 < 4; i1++) {
                                BlockPos np = pos.add(dj[i1], 0, dk[i1]);
                                lmS[i1] = st.getPackedLightmapCoords(w, np);
                                ov[i1] = 0;
                                if (!isLava) {
                                    net.minecraft.block.Block nb = w.getBlockState(np).getBlock();
                                    if (nb == net.minecraft.init.Blocks.GLASS
                                        || nb == net.minecraft.init.Blocks.STAINED_GLASS) ov[i1] = 1;
                                }
                            }
                            inW.println(pos.getX() + " " + pos.getY() + " " + pos.getZ()
                                + " " + (isLava ? 1 : 0) + " " + colorInt
                                + " " + flag1 + " " + flag2
                                + " " + ab[0] + " " + ab[1] + " " + ab[2] + " " + ab[3]
                                + " " + rsUp + " " + Float.floatToRawIntBits(slope)
                                + " " + Float.floatToRawIntBits(f7) + " " + Float.floatToRawIntBits(f8)
                                + " " + Float.floatToRawIntBits(f9) + " " + Float.floatToRawIntBits(f10)
                                + " " + lmUp + " " + lmDown
                                + " " + lmS[0] + " " + lmS[1] + " " + lmS[2] + " " + lmS[3]
                                + " " + ov[0] + " " + ov[1] + " " + ov[2] + " " + ov[3]);
                            java.nio.IntBuffer ib = (java.nio.IntBuffer) rib.get(buf);
                            for (int z = 0; z < vc * 7; z++) goW.println(ib.get(z));
                            written++;
                        }
                    inW.flush(); inW.close(); goW.flush(); goW.close();
                    System.out.println("[qrl] capture_fluidquads wrote " + written + " fluid blocks to " + dir);
                    JsonObject o = new JsonObject(); o.addProperty("ok", true);
                    o.addProperty("blocks", written); o.addProperty("dir", dir); fr.resp.offer(o.toString());
                } catch (Throwable t) {
                    System.out.println("[qrl] capture_fluidquads failed: " + t);
                    java.io.StringWriter swr = new java.io.StringWriter();
                    t.printStackTrace(new java.io.PrintWriter(swr));
                    System.out.println(swr.toString());
                    fr.resp.offer(err("capture failed: " + t));
                }
            }});
            return;
        }
        // ---- 12_ao_vertex_brightness: AmbientOcclusionFace.updateVertexBrightness ----
        if (r.cmd.equals("capture_ao")) {
            final MinecraftServer s = mc.getIntegratedServer();
            if (s == null || mc.player == null) { r.resp.offer(err("no world")); return; }
            final int px = (int) Math.floor(mc.player.posX);
            final int py = (int) Math.floor(mc.player.posY);
            final int pz = (int) Math.floor(mc.player.posZ);
            final String dir = r.action.has("dir") ? r.action.get("dir").getAsString()
                : "/home/infatoshi/dev/minecraft/mc-1.11.2-env/c/render-opt/kernels/12_ao_vertex_brightness/golden";
            final Req fr = r;
            s.addScheduledTask(new Runnable() { public void run() {
                try {
                    net.minecraft.world.WorldServer w = s.worlds[0];  // overworld fallback
                    try {   // player's world: ops must follow the player across dimensions
                        java.util.List<net.minecraft.entity.player.EntityPlayerMP> qps = s.getPlayerList().getPlayers();
                        if (!qps.isEmpty()) w = qps.get(0).getServerWorld();
                    } catch (Throwable ig) {}
                    net.minecraft.client.renderer.BlockRendererDispatcher brd =
                        Minecraft.getMinecraft().getBlockRendererDispatcher();
                    net.minecraft.client.renderer.BlockModelRenderer bmr = brd.getBlockModelRenderer();
                    Class<?> aoCls = Class.forName("net.minecraft.client.renderer.BlockModelRenderer$AmbientOcclusionFace");
                    java.lang.reflect.Constructor<?> aoCtor = aoCls.getDeclaredConstructor(
                        net.minecraft.client.renderer.BlockModelRenderer.class); aoCtor.setAccessible(true);
                    java.lang.reflect.Field fvb = aoCls.getDeclaredField("vertexBrightness"); fvb.setAccessible(true);
                    java.lang.reflect.Field fvcm = aoCls.getDeclaredField("vertexColorMultiplier"); fvcm.setAccessible(true);
                    java.lang.reflect.Method updVB = aoCls.getDeclaredMethod("updateVertexBrightness",
                        net.minecraft.world.IBlockAccess.class, net.minecraft.block.state.IBlockState.class,
                        net.minecraft.util.math.BlockPos.class, net.minecraft.util.EnumFacing.class,
                        float[].class, java.util.BitSet.class); updVB.setAccessible(true);
                    java.lang.reflect.Method fqb = net.minecraft.client.renderer.BlockModelRenderer.class
                        .getDeclaredMethod("fillQuadBounds", net.minecraft.block.state.IBlockState.class,
                        int[].class, net.minecraft.util.EnumFacing.class, float[].class, java.util.BitSet.class);
                    fqb.setAccessible(true);
                    Class<?> eniCls = Class.forName("net.minecraft.client.renderer.BlockModelRenderer$EnumNeighborInfo");
                    java.lang.reflect.Method getNI = eniCls.getDeclaredMethod("getNeighbourInfo",
                        net.minecraft.util.EnumFacing.class); getNI.setAccessible(true);
                    java.lang.reflect.Field cornersF = eniCls.getDeclaredField("corners"); cornersF.setAccessible(true);

                    net.minecraft.block.state.IBlockState[] tests = {
                        net.minecraft.init.Blocks.OAK_STAIRS.getDefaultState(),
                        net.minecraft.init.Blocks.GRASS.getDefaultState(),
                        net.minecraft.init.Blocks.LEAVES.getDefaultState(),
                        net.minecraft.init.Blocks.STONE.getDefaultState(),
                        net.minecraft.init.Blocks.DIRT.getDefaultState(),
                        net.minecraft.init.Blocks.COBBLESTONE.getDefaultState(),
                        net.minecraft.init.Blocks.LOG.getDefaultState() };
                    java.util.ArrayList<BlockPos> tpos = new java.util.ArrayList<BlockPos>();
                    int ii = 0;
                    for (net.minecraft.block.state.IBlockState ts : tests) {
                        BlockPos b = new BlockPos(px + 5 + ii, py + 1, pz);
                        w.setBlockState(b, ts);
                        w.checkLightFor(net.minecraft.world.EnumSkyBlock.BLOCK, b);
                        w.checkLightFor(net.minecraft.world.EnumSkyBlock.SKY, b);
                        tpos.add(b); ii++;
                    }
                    java.io.File gd = new java.io.File(dir); gd.mkdirs();
                    java.io.PrintWriter inW = new java.io.PrintWriter(new java.io.BufferedWriter(
                        new java.io.FileWriter(new java.io.File(gd, "inputs.txt"))));
                    java.io.PrintWriter goW = new java.io.PrintWriter(new java.io.BufferedWriter(
                        new java.io.FileWriter(new java.io.File(gd, "golden.txt"))));
                    int written = 0, nonCubic = 0;
                    int nf = net.minecraft.util.EnumFacing.values().length;
                    java.util.ArrayList<net.minecraft.util.EnumFacing> faces =
                        new java.util.ArrayList<net.minecraft.util.EnumFacing>();
                    faces.add(null);
                    for (net.minecraft.util.EnumFacing ef : net.minecraft.util.EnumFacing.values()) faces.add(ef);
                    for (BlockPos pos : tpos) {
                        net.minecraft.block.state.IBlockState st = w.getBlockState(pos);
                        net.minecraft.client.renderer.block.model.IBakedModel model = brd.getModelForState(st);
                        for (net.minecraft.util.EnumFacing fc : faces) {
                            java.util.List<net.minecraft.client.renderer.block.model.BakedQuad> quads =
                                model.getQuads(st, fc, 0L);
                            for (net.minecraft.client.renderer.block.model.BakedQuad q : quads) {
                                int[] vd = q.getVertexData();
                                if (vd.length != 28) continue;
                                net.minecraft.util.EnumFacing face = q.getFace();
                                float[] qb = new float[nf * 2];
                                java.util.BitSet bs = new java.util.BitSet(3);
                                fqb.invoke(bmr, st, vd, face, qb, bs);
                                boolean s0 = bs.get(0), s1 = bs.get(1);
                                Object eni = getNI.invoke(null, face);
                                net.minecraft.util.EnumFacing[] cn =
                                    (net.minecraft.util.EnumFacing[]) cornersF.get(eni);
                                BlockPos bp0 = s0 ? pos.offset(face) : pos;
                                BlockPos p1 = bp0.offset(cn[0]), p2 = bp0.offset(cn[1]),
                                         p3 = bp0.offset(cn[2]), p4 = bp0.offset(cn[3]);
                                int li = st.getPackedLightmapCoords(w, p1);
                                int lj = st.getPackedLightmapCoords(w, p2);
                                int lk = st.getPackedLightmapCoords(w, p3);
                                int ll = st.getPackedLightmapCoords(w, p4);
                                float f  = w.getBlockState(p1).getAmbientOcclusionLightValue();
                                float f1 = w.getBlockState(p2).getAmbientOcclusionLightValue();
                                float f2 = w.getBlockState(p3).getAmbientOcclusionLightValue();
                                float f3 = w.getBlockState(p4).getAmbientOcclusionLightValue();
                                boolean flag  = w.getBlockState(p1.offset(face)).isTranslucent();
                                boolean flag1 = w.getBlockState(p2.offset(face)).isTranslucent();
                                boolean flag2 = w.getBlockState(p3.offset(face)).isTranslucent();
                                boolean flag3 = w.getBlockState(p4.offset(face)).isTranslucent();
                                float f4; int i1;
                                if (!flag2 && !flag) { f4 = f; i1 = li; }
                                else { BlockPos b = p1.offset(cn[2]);
                                    f4 = w.getBlockState(b).getAmbientOcclusionLightValue();
                                    i1 = st.getPackedLightmapCoords(w, b); }
                                float f5; int j1;
                                if (!flag3 && !flag) { f5 = f; j1 = li; }
                                else { BlockPos b = p1.offset(cn[3]);
                                    f5 = w.getBlockState(b).getAmbientOcclusionLightValue();
                                    j1 = st.getPackedLightmapCoords(w, b); }
                                float f6; int k1;
                                if (!flag2 && !flag1) { f6 = f1; k1 = lj; }
                                else { BlockPos b = p2.offset(cn[2]);
                                    f6 = w.getBlockState(b).getAmbientOcclusionLightValue();
                                    k1 = st.getPackedLightmapCoords(w, b); }
                                float f7; int l1;
                                if (!flag3 && !flag1) { f7 = f1; l1 = lj; }
                                else { BlockPos b = p2.offset(cn[3]);
                                    f7 = w.getBlockState(b).getAmbientOcclusionLightValue();
                                    l1 = st.getPackedLightmapCoords(w, b); }
                                int i3 = st.getPackedLightmapCoords(w, pos);
                                if (s0 || !w.getBlockState(pos.offset(face)).isOpaqueCube())
                                    i3 = st.getPackedLightmapCoords(w, pos.offset(face));
                                float f8 = s0 ? w.getBlockState(bp0).getAmbientOcclusionLightValue()
                                              : w.getBlockState(pos).getAmbientOcclusionLightValue();
                                // golden
                                Object ao = aoCtor.newInstance(bmr);
                                updVB.invoke(ao, w, st, pos, face, qb, bs);
                                int[] vb = (int[]) fvb.get(ao);
                                float[] vcm = (float[]) fvcm.get(ao);
                                StringBuilder sb = new StringBuilder();
                                sb.append(face.getIndex()).append(' ').append(s0 ? 1 : 0).append(' ').append(s1 ? 1 : 0);
                                for (int z = 0; z < nf * 2; z++) sb.append(' ').append(Float.floatToRawIntBits(qb[z]));
                                sb.append(' ').append(li).append(' ').append(lj).append(' ').append(lk).append(' ').append(ll);
                                sb.append(' ').append(i1).append(' ').append(j1).append(' ').append(k1).append(' ').append(l1);
                                sb.append(' ').append(i3);
                                sb.append(' ').append(Float.floatToRawIntBits(f)).append(' ').append(Float.floatToRawIntBits(f1))
                                  .append(' ').append(Float.floatToRawIntBits(f2)).append(' ').append(Float.floatToRawIntBits(f3))
                                  .append(' ').append(Float.floatToRawIntBits(f4)).append(' ').append(Float.floatToRawIntBits(f5))
                                  .append(' ').append(Float.floatToRawIntBits(f6)).append(' ').append(Float.floatToRawIntBits(f7))
                                  .append(' ').append(Float.floatToRawIntBits(f8));
                                inW.println(sb.toString());
                                goW.println(vb[0]); goW.println(vb[1]); goW.println(vb[2]); goW.println(vb[3]);
                                goW.println(Float.floatToRawIntBits(vcm[0])); goW.println(Float.floatToRawIntBits(vcm[1]));
                                goW.println(Float.floatToRawIntBits(vcm[2])); goW.println(Float.floatToRawIntBits(vcm[3]));
                                if (s1) nonCubic++;
                                written++;
                            }
                        }
                    }
                    inW.flush(); inW.close(); goW.flush(); goW.close();
                    System.out.println("[qrl] capture_ao wrote " + written + " quads (" + nonCubic + " s1=true) to " + dir);
                    JsonObject o = new JsonObject(); o.addProperty("ok", true);
                    o.addProperty("quads", written); o.addProperty("non_cubic", nonCubic);
                    o.addProperty("dir", dir); fr.resp.offer(o.toString());
                } catch (Throwable t) {
                    System.out.println("[qrl] capture_ao failed: " + t);
                    java.io.StringWriter swr = new java.io.StringWriter();
                    t.printStackTrace(new java.io.PrintWriter(swr)); System.out.println(swr.toString());
                    fr.resp.offer(err("capture failed: " + t));
                }
            }});
            return;
        }
        // ---- 29_particle_update: Particle.onUpdate (open-air free-fall integration) ----
        if (r.cmd.equals("capture_particle")) {
            final MinecraftServer s = mc.getIntegratedServer();
            if (s == null || mc.player == null) { r.resp.offer(err("no world")); return; }
            final double px = mc.player.posX, pz = mc.player.posZ;
            final String dir = r.action.has("dir") ? r.action.get("dir").getAsString()
                : "/home/infatoshi/dev/minecraft/mc-1.11.2-env/c/render-opt/kernels/29_particle_update/golden";
            final Req fr = r;
            s.addScheduledTask(new Runnable() { public void run() {
                try {
                    net.minecraft.world.WorldServer w = s.worlds[0];  // overworld fallback
                    try {   // player's world: ops must follow the player across dimensions
                        java.util.List<net.minecraft.entity.player.EntityPlayerMP> qps = s.getPlayerList().getPlayers();
                        if (!qps.isEmpty()) w = qps.get(0).getServerWorld();
                    } catch (Throwable ig) {}
                    java.lang.reflect.Constructor<net.minecraft.client.particle.Particle> ct =
                        net.minecraft.client.particle.Particle.class.getDeclaredConstructor(
                            net.minecraft.world.World.class, double.class, double.class, double.class);
                    ct.setAccessible(true);
                    Class<?> pc = net.minecraft.client.particle.Particle.class;
                    java.lang.reflect.Field fMX = pc.getDeclaredField("motionX"); fMX.setAccessible(true);
                    java.lang.reflect.Field fMY = pc.getDeclaredField("motionY"); fMY.setAccessible(true);
                    java.lang.reflect.Field fMZ = pc.getDeclaredField("motionZ"); fMZ.setAccessible(true);
                    java.lang.reflect.Field fG  = pc.getDeclaredField("particleGravity"); fG.setAccessible(true);
                    java.lang.reflect.Field fAge = pc.getDeclaredField("particleAge"); fAge.setAccessible(true);
                    java.lang.reflect.Field fMax = pc.getDeclaredField("particleMaxAge"); fMax.setAccessible(true);
                    java.lang.reflect.Field fCol = pc.getDeclaredField("canCollide"); fCol.setAccessible(true);
                    java.lang.reflect.Field fW = pc.getDeclaredField("width"); fW.setAccessible(true);
                    java.lang.reflect.Field fH = pc.getDeclaredField("height"); fH.setAccessible(true);
                    java.lang.reflect.Field fPX = pc.getDeclaredField("posX"); fPX.setAccessible(true);
                    java.lang.reflect.Field fPY = pc.getDeclaredField("posY"); fPY.setAccessible(true);
                    java.lang.reflect.Field fPZ = pc.getDeclaredField("posZ"); fPZ.setAccessible(true);
                    java.lang.reflect.Field fGround = pc.getDeclaredField("onGround"); fGround.setAccessible(true);

                    java.io.File gd = new java.io.File(dir); gd.mkdirs();
                    java.io.PrintWriter inW = new java.io.PrintWriter(new java.io.BufferedWriter(
                        new java.io.FileWriter(new java.io.File(gd, "inputs.txt"))));
                    java.io.PrintWriter goW = new java.io.PrintWriter(new java.io.BufferedWriter(
                        new java.io.FileWriter(new java.io.File(gd, "golden.txt"))));
                    java.util.Random rnd = new java.util.Random(0);
                    double baseY = 200.0;  // open air, no collision boxes
                    int written = 0;
                    for (int n = 0; n < 400; n++) {
                        double sx = px + (rnd.nextDouble() - 0.5) * 4.0;
                        double sy = baseY + rnd.nextDouble() * 10.0;
                        double sz = pz + (rnd.nextDouble() - 0.5) * 4.0;
                        net.minecraft.client.particle.Particle p = ct.newInstance(w, sx, sy, sz);
                        double mx = (rnd.nextDouble() - 0.5) * 0.4;
                        double my = (rnd.nextDouble() - 0.5) * 0.4;
                        double mz = (rnd.nextDouble() - 0.5) * 0.4;
                        float grav = (float) (rnd.nextDouble() * 1.5);
                        int age = rnd.nextInt(20);
                        int maxAge = 60 + rnd.nextInt(40);
                        fMX.setDouble(p, mx); fMY.setDouble(p, my); fMZ.setDouble(p, mz);
                        fG.setFloat(p, grav); fAge.setInt(p, age); fMax.setInt(p, maxAge);
                        fCol.setBoolean(p, false);
                        float pw = fW.getFloat(p), ph = fH.getFloat(p);
                        double ipx = fPX.getDouble(p), ipy = fPY.getDouble(p), ipz = fPZ.getDouble(p);
                        // input: posX posY posZ mX mY mZ (double bits) age maxAge grav(float bits) width height(float bits)
                        inW.println(Double.doubleToRawLongBits(ipx) + " " + Double.doubleToRawLongBits(ipy)
                            + " " + Double.doubleToRawLongBits(ipz) + " " + Double.doubleToRawLongBits(mx)
                            + " " + Double.doubleToRawLongBits(my) + " " + Double.doubleToRawLongBits(mz)
                            + " " + age + " " + maxAge + " " + Float.floatToRawIntBits(grav)
                            + " " + Float.floatToRawIntBits(pw) + " " + Float.floatToRawIntBits(ph));
                        p.onUpdate();
                        double oqx = fPX.getDouble(p), oqy = fPY.getDouble(p), oqz = fPZ.getDouble(p);
                        double omx = fMX.getDouble(p), omy = fMY.getDouble(p), omz = fMZ.getDouble(p);
                        int oage = fAge.getInt(p); boolean og = fGround.getBoolean(p);
                        goW.println(Double.doubleToRawLongBits(oqx)); goW.println(Double.doubleToRawLongBits(oqy));
                        goW.println(Double.doubleToRawLongBits(oqz)); goW.println(Double.doubleToRawLongBits(omx));
                        goW.println(Double.doubleToRawLongBits(omy)); goW.println(Double.doubleToRawLongBits(omz));
                        goW.println(oage); goW.println(og ? 1 : 0);
                        written++;
                    }
                    inW.flush(); inW.close(); goW.flush(); goW.close();
                    System.out.println("[qrl] capture_particle wrote " + written + " to " + dir);
                    JsonObject o = new JsonObject(); o.addProperty("ok", true);
                    o.addProperty("count", written); o.addProperty("dir", dir); fr.resp.offer(o.toString());
                } catch (Throwable t) {
                    System.out.println("[qrl] capture_particle failed: " + t);
                    java.io.StringWriter swr = new java.io.StringWriter();
                    t.printStackTrace(new java.io.PrintWriter(swr)); System.out.println(swr.toString());
                    fr.resp.offer(err("capture failed: " + t));
                }
            }});
            return;
        }
        // ---- 17_skylight_gen: Chunk.generateSkylightMap (per-column) ----
        if (r.cmd.equals("capture_skylight")) {
            final MinecraftServer s = mc.getIntegratedServer();
            if (s == null || mc.player == null) { r.resp.offer(err("no world")); return; }
            final int px = (int) Math.floor(mc.player.posX);
            final int pz = (int) Math.floor(mc.player.posZ);
            final String dir = r.action.has("dir") ? r.action.get("dir").getAsString()
                : "/home/infatoshi/dev/minecraft/mc-1.11.2-env/c/render-opt/kernels/17_skylight_gen/golden";
            final Req fr = r;
            s.addScheduledTask(new Runnable() { public void run() {
                try {
                    net.minecraft.world.WorldServer w = s.worlds[0];  // overworld fallback
                    try {   // player's world: ops must follow the player across dimensions
                        java.util.List<net.minecraft.entity.player.EntityPlayerMP> qps = s.getPlayerList().getPlayers();
                        if (!qps.isEmpty()) w = qps.get(0).getServerWorld();
                    } catch (Throwable ig) {}
                    net.minecraft.world.chunk.Chunk ch = w.getChunkFromBlockCoords(new BlockPos(px, 0, pz));
                    Class<?> cc = net.minecraft.world.chunk.Chunk.class;
                    java.lang.reflect.Method gOpac = cc.getDeclaredMethod("getBlockLightOpacity",
                        int.class, int.class, int.class); gOpac.setAccessible(true);
                    java.lang.reflect.Field fHM = cc.getDeclaredField("heightMap"); fHM.setAccessible(true);
                    java.lang.reflect.Field fSA = cc.getDeclaredField("storageArrays"); fSA.setAccessible(true);
                    boolean hasSky = w.provider.hasSkyLight();
                    int i = ch.getTopFilledSegment();
                    int nY = i + 16;
                    net.minecraft.world.chunk.storage.ExtendedBlockStorage[] sa =
                        (net.minecraft.world.chunk.storage.ExtendedBlockStorage[]) fSA.get(ch);
                    net.minecraft.world.chunk.storage.ExtendedBlockStorage NULL =
                        net.minecraft.world.chunk.Chunk.NULL_BLOCK_STORAGE;
                    // zero skylight in all non-null storages so the golden is reproducible from inputs alone
                    if (hasSky) {
                        for (int seg = 0; seg < sa.length; seg++) {
                            if (sa[seg] == NULL) continue;
                            for (int yy = 0; yy < 16; yy++)
                                for (int xx = 0; xx < 16; xx++)
                                    for (int zz = 0; zz < 16; zz++)
                                        sa[seg].setExtSkylightValue(xx, yy, zz, 0);
                        }
                    }
                    ch.generateSkylightMap();
                    int[] hm = (int[]) fHM.get(ch);
                    java.io.File gd = new java.io.File(dir); gd.mkdirs();
                    java.io.PrintWriter inW = new java.io.PrintWriter(new java.io.BufferedWriter(
                        new java.io.FileWriter(new java.io.File(gd, "inputs.txt"))));
                    java.io.PrintWriter goW = new java.io.PrintWriter(new java.io.BufferedWriter(
                        new java.io.FileWriter(new java.io.File(gd, "golden.txt"))));
                    for (int j = 0; j < 16; j++) {
                        for (int k = 0; k < 16; k++) {
                            StringBuilder sb = new StringBuilder();
                            sb.append(i).append(' ').append(hasSky ? 1 : 0);
                            for (int y = 0; y < nY; y++) {
                                int op = ((Integer) gOpac.invoke(ch, j, y, k)).intValue();
                                int seg = y >> 4;
                                int nn = (seg < sa.length && sa[seg] != NULL) ? 1 : 0;
                                sb.append(' ').append(op).append(' ').append(nn);
                            }
                            inW.println(sb.toString());
                            goW.println(hm[k << 4 | j]);
                            for (int y = 0; y < nY; y++) {
                                int seg = y >> 4;
                                int sky = (hasSky && seg < sa.length && sa[seg] != NULL)
                                    ? sa[seg].getExtSkylightValue(j, y & 15, k) : 0;
                                goW.println(sky);
                            }
                        }
                    }
                    inW.flush(); inW.close(); goW.flush(); goW.close();
                    System.out.println("[qrl] capture_skylight wrote 256 columns (i=" + i + ", nY=" + nY + ") to " + dir);
                    JsonObject o = new JsonObject(); o.addProperty("ok", true);
                    o.addProperty("columns", 256); o.addProperty("topSeg", i); o.addProperty("nY", nY);
                    o.addProperty("dir", dir); fr.resp.offer(o.toString());
                } catch (Throwable t) {
                    System.out.println("[qrl] capture_skylight failed: " + t);
                    java.io.StringWriter swr = new java.io.StringWriter();
                    t.printStackTrace(new java.io.PrintWriter(swr)); System.out.println(swr.toString());
                    fr.resp.offer(err("capture failed: " + t));
                }
            }});
            return;
        }
        // ---- 37_entity_limb_anim: ModelQuadruped(ModelCow).setRotationAngles (per-limb trig) ----
        if (r.cmd.equals("capture_limbanim")) {
            final String dir = r.action.has("dir") ? r.action.get("dir").getAsString()
                : "/home/infatoshi/dev/minecraft/mc-1.11.2-env/c/render-opt/kernels/37_entity_limb_anim/golden";
            final Req fr = r;
            final MinecraftServer s = mc.getIntegratedServer();
            if (s == null) { r.resp.offer(err("no world")); return; }
            s.addScheduledTask(new Runnable() { public void run() {
                try {
                    net.minecraft.client.model.ModelCow model = new net.minecraft.client.model.ModelCow();
                    Class<?> mq = net.minecraft.client.model.ModelQuadruped.class;
                    String[] partNames = { "head", "body", "leg1", "leg2", "leg3", "leg4" };
                    java.lang.reflect.Field[] pf = new java.lang.reflect.Field[6];
                    for (int z = 0; z < 6; z++) { pf[z] = mq.getDeclaredField(partNames[z]); pf[z].setAccessible(true); }
                    java.io.File gd = new java.io.File(dir); gd.mkdirs();
                    java.io.PrintWriter inW = new java.io.PrintWriter(new java.io.BufferedWriter(
                        new java.io.FileWriter(new java.io.File(gd, "inputs.txt"))));
                    java.io.PrintWriter goW = new java.io.PrintWriter(new java.io.BufferedWriter(
                        new java.io.FileWriter(new java.io.File(gd, "golden.txt"))));
                    java.util.Random rnd = new java.util.Random(0);
                    int written = 0;
                    for (int n = 0; n < 500; n++) {
                        float limbSwing = (float) (rnd.nextDouble() * 20.0 - 10.0);
                        float limbSwingAmount = (float) (rnd.nextDouble());
                        float ageInTicks = (float) (rnd.nextDouble() * 200.0);
                        float netHeadYaw = (float) (rnd.nextDouble() * 180.0 - 90.0);
                        float headPitch = (float) (rnd.nextDouble() * 90.0 - 45.0);
                        model.setRotationAngles(limbSwing, limbSwingAmount, ageInTicks, netHeadYaw, headPitch, 1.0F, null);
                        inW.println(Float.floatToRawIntBits(limbSwing) + " " + Float.floatToRawIntBits(limbSwingAmount)
                            + " " + Float.floatToRawIntBits(ageInTicks) + " " + Float.floatToRawIntBits(netHeadYaw)
                            + " " + Float.floatToRawIntBits(headPitch));
                        for (int z = 0; z < 6; z++) {
                            net.minecraft.client.model.ModelRenderer mr =
                                (net.minecraft.client.model.ModelRenderer) pf[z].get(model);
                            goW.println(Float.floatToRawIntBits(mr.rotateAngleX));
                            goW.println(Float.floatToRawIntBits(mr.rotateAngleY));
                            goW.println(Float.floatToRawIntBits(mr.rotateAngleZ));
                        }
                        written++;
                    }
                    inW.flush(); inW.close(); goW.flush(); goW.close();
                    System.out.println("[qrl] capture_limbanim wrote " + written + " to " + dir);
                    JsonObject o = new JsonObject(); o.addProperty("ok", true);
                    o.addProperty("count", written); o.addProperty("dir", dir); fr.resp.offer(o.toString());
                } catch (Throwable t) {
                    System.out.println("[qrl] capture_limbanim failed: " + t);
                    java.io.StringWriter swr = new java.io.StringWriter();
                    t.printStackTrace(new java.io.PrintWriter(swr)); System.out.println(swr.toString());
                    fr.resp.offer(err("capture failed: " + t));
                }
            }});
            return;
        }
        // ---- 16_light_propagation: World.checkLightFor BFS (before/after a placed light source) ----
        if (r.cmd.equals("capture_lightprop")) {
            final MinecraftServer s = mc.getIntegratedServer();
            if (s == null || mc.player == null) { r.resp.offer(err("no world")); return; }
            final int px = (int) Math.floor(mc.player.posX);
            final int py = (int) Math.floor(mc.player.posY);
            final int pz = (int) Math.floor(mc.player.posZ);
            final int rad = r.action.has("radius") ? r.action.get("radius").getAsInt() : 14;
            final String dir = r.action.has("dir") ? r.action.get("dir").getAsString()
                : "/home/infatoshi/dev/minecraft/mc-1.11.2-env/c/render-opt/kernels/16_light_propagation/golden";
            final Req fr = r;
            s.addScheduledTask(new Runnable() { public void run() {
                try {
                    net.minecraft.world.WorldServer w = s.worlds[0];  // overworld fallback
                    try {   // player's world: ops must follow the player across dimensions
                        java.util.List<net.minecraft.entity.player.EntityPlayerMP> qps = s.getPlayerList().getPlayers();
                        if (!qps.isEmpty()) w = qps.get(0).getServerWorld();
                    } catch (Throwable ig) {}
                    BlockPos src = new BlockPos(px - 24, py + 2, pz);
                    // clear an air pocket around the source so propagation is clean & reproducible
                    net.minecraft.block.state.IBlockState air = net.minecraft.init.Blocks.AIR.getDefaultState();
                    for (int dx = -rad; dx <= rad; dx++)
                      for (int dy = -rad; dy <= rad; dy++)
                        for (int dz = -rad; dz <= rad; dz++) {
                            BlockPos b = src.add(dx, dy, dz);
                            if (b.getY() < 1 || b.getY() > 255) continue;
                            w.setBlockState(b, air, 2);
                        }
                    // settle: recompute light over the cleared region (no source yet)
                    for (int dx = -rad; dx <= rad; dx += 2)
                      for (int dy = -rad; dy <= rad; dy += 2)
                        for (int dz = -rad; dz <= rad; dz += 2) {
                            BlockPos b = src.add(dx, dy, dz);
                            if (b.getY() < 1 || b.getY() > 255) continue;
                            w.checkLightFor(net.minecraft.world.EnumSkyBlock.BLOCK, b);
                        }
                    java.io.File gd = new java.io.File(dir); gd.mkdirs();
                    // BEFORE snapshot (block light + opacity of every cell)
                    java.io.PrintWriter inW = new java.io.PrintWriter(new java.io.BufferedWriter(
                        new java.io.FileWriter(new java.io.File(gd, "inputs.txt"))));
                    java.io.PrintWriter goW = new java.io.PrintWriter(new java.io.BufferedWriter(
                        new java.io.FileWriter(new java.io.File(gd, "golden.txt"))));
                    // header: relSrcX relSrcY relSrcZ sourceLuminance radius
                    inW.println("# rel_to_first_cell src=(0,0,0) luminance=15 radius=" + rad);
                    int[][] cells = new int[(2*rad+1)*(2*rad+1)*(2*rad+1)][];
                    int nc = 0;
                    for (int dx = -rad; dx <= rad; dx++)
                      for (int dy = -rad; dy <= rad; dy++)
                        for (int dz = -rad; dz <= rad; dz++) {
                            BlockPos b = src.add(dx, dy, dz);
                            if (b.getY() < 1 || b.getY() > 255) continue;
                            int op = w.getBlockState(b).getLightOpacity(w, b);
                            int before = w.getLightFor(net.minecraft.world.EnumSkyBlock.BLOCK, b);
                            cells[nc++] = new int[]{ dx, dy, dz, op, before };
                        }
                    // place glowstone source and propagate
                    w.setBlockState(src, net.minecraft.init.Blocks.GLOWSTONE.getDefaultState(), 2);
                    w.checkLightFor(net.minecraft.world.EnumSkyBlock.BLOCK, src);
                    for (int c = 0; c < nc; c++) {
                        int dx = cells[c][0], dy = cells[c][1], dz = cells[c][2];
                        BlockPos b = src.add(dx, dy, dz);
                        int after = w.getLightFor(net.minecraft.world.EnumSkyBlock.BLOCK, b);
                        // input: dx dy dz opacity beforeLight ; golden: afterLight
                        inW.println(dx + " " + dy + " " + dz + " " + cells[c][3] + " " + cells[c][4]);
                        goW.println(after);
                    }
                    inW.flush(); inW.close(); goW.flush(); goW.close();
                    System.out.println("[qrl] capture_lightprop wrote " + nc + " cells to " + dir);
                    JsonObject o = new JsonObject(); o.addProperty("ok", true);
                    o.addProperty("cells", nc); o.addProperty("dir", dir); fr.resp.offer(o.toString());
                } catch (Throwable t) {
                    System.out.println("[qrl] capture_lightprop failed: " + t);
                    java.io.StringWriter swr = new java.io.StringWriter();
                    t.printStackTrace(new java.io.PrintWriter(swr)); System.out.println(swr.toString());
                    fr.resp.offer(err("capture failed: " + t));
                }
            }});
            return;
        }
        // ---- 11_lightmap: EntityRenderer.updateLightmap (CLIENT thread, run inline) ----
        if (r.cmd.equals("capture_lightmap")) {
            if (mc.world == null || mc.player == null) { r.resp.offer(err("no world")); return; }
            final String dir = r.action.has("dir") ? r.action.get("dir").getAsString()
                : "/home/infatoshi/dev/minecraft/mc-1.11.2-env/c/render-opt/kernels/11_lightmap/golden";
            try {
                net.minecraft.client.renderer.EntityRenderer er = mc.entityRenderer;
                Class<?> ec = net.minecraft.client.renderer.EntityRenderer.class;
                java.lang.reflect.Method upd = ec.getDeclaredMethod("updateLightmap", float.class);
                upd.setAccessible(true);
                java.lang.reflect.Field fNeed = ec.getDeclaredField("lightmapUpdateNeeded"); fNeed.setAccessible(true);
                java.lang.reflect.Field fCol = ec.getDeclaredField("lightmapColors"); fCol.setAccessible(true);
                java.lang.reflect.Field fFlick = ec.getDeclaredField("torchFlickerX"); fFlick.setAccessible(true);
                java.lang.reflect.Field fBoss = ec.getDeclaredField("bossColorModifier"); fBoss.setAccessible(true);
                float partial = 1.0F;
                net.minecraft.world.World wld = mc.world;
                float sun = wld.getSunBrightness(partial);
                float rain = wld.getRainStrength(partial);
                float thunder = wld.getThunderStrength(partial);
                float gamma = mc.gameSettings.gammaSetting;
                int nv = mc.player.isPotionActive(net.minecraft.init.MobEffects.NIGHT_VISION) ? 1 : 0;
                float torchFlickerX = fFlick.getFloat(er);   // per-tick random-walk field the method reads
                float bossColor = fBoss.getFloat(er);         // 0 with no boss
                int lastLightning = wld.getLastLightningBolt();
                float[] tbl = wld.provider.getLightBrightnessTable();
                fNeed.setBoolean(er, true);
                upd.invoke(er, partial);
                int[] colors = (int[]) fCol.get(er);
                java.io.File gd = new java.io.File(dir); gd.mkdirs();
                java.io.PrintWriter inW = new java.io.PrintWriter(new java.io.BufferedWriter(
                    new java.io.FileWriter(new java.io.File(gd, "inputs.txt"))));
                java.io.PrintWriter goW = new java.io.PrintWriter(new java.io.BufferedWriter(
                    new java.io.FileWriter(new java.io.File(gd, "golden.txt"))));
                StringBuilder sb = new StringBuilder();
                sb.append("partialTicks ").append(Float.floatToRawIntBits(partial)).append('\n');
                sb.append("sunBrightness ").append(Float.floatToRawIntBits(sun)).append('\n');
                sb.append("rainStrength ").append(Float.floatToRawIntBits(rain)).append('\n');
                sb.append("thunderStrength ").append(Float.floatToRawIntBits(thunder)).append('\n');
                sb.append("gamma ").append(Float.floatToRawIntBits(gamma)).append('\n');
                sb.append("nightVision ").append(nv).append('\n');
                sb.append("dimId ").append(wld.provider.getDimensionType().getId()).append('\n');
                sb.append("torchFlickerX ").append(Float.floatToRawIntBits(torchFlickerX)).append('\n');
                sb.append("bossColorModifier ").append(Float.floatToRawIntBits(bossColor)).append('\n');
                sb.append("lastLightningBolt ").append(lastLightning).append('\n');
                sb.append("brightnessTable");
                for (int z = 0; z < tbl.length; z++) sb.append(' ').append(Float.floatToRawIntBits(tbl[z]));
                inW.println(sb.toString());
                for (int z = 0; z < colors.length; z++) goW.println(colors[z]);
                inW.flush(); inW.close(); goW.flush(); goW.close();
                System.out.println("[qrl] capture_lightmap wrote " + colors.length + " texels to " + dir);
                JsonObject o = new JsonObject(); o.addProperty("ok", true);
                o.addProperty("texels", colors.length); o.addProperty("dir", dir); r.resp.offer(o.toString());
            } catch (Throwable t) {
                System.out.println("[qrl] capture_lightmap failed: " + t);
                java.io.StringWriter swr = new java.io.StringWriter();
                t.printStackTrace(new java.io.PrintWriter(swr)); System.out.println(swr.toString());
                r.resp.offer(err("capture failed: " + t));
            }
            return;
        }
        // ---- 26_chunk_rebuild_loop: RenderChunk.rebuildChunk (CLIENT thread, run inline) ----
        if (r.cmd.equals("capture_chunkrebuild")) {
            if (mc.world == null || mc.player == null) { r.resp.offer(err("no world")); return; }
            final String dir = r.action.has("dir") ? r.action.get("dir").getAsString()
                : "/home/infatoshi/dev/minecraft/mc-1.11.2-env/c/render-opt/kernels/26_chunk_rebuild_loop/golden";
            try {
                int px = (int) Math.floor(mc.player.posX);
                int py = (int) Math.floor(mc.player.posY);
                int pz = (int) Math.floor(mc.player.posZ);
                net.minecraft.client.renderer.RenderGlobal rg = mc.renderGlobal;
                java.lang.reflect.Field vfF = net.minecraft.client.renderer.RenderGlobal.class
                    .getDeclaredField("viewFrustum"); vfF.setAccessible(true);
                net.minecraft.client.renderer.ViewFrustum vf =
                    (net.minecraft.client.renderer.ViewFrustum) vfF.get(rg);
                if (vf == null || vf.renderChunks == null) { r.resp.offer(err("no viewFrustum")); return; }
                // pick the section containing the ground beneath the player
                int ox = (px >> 4) << 4, oy = ((py - 2) >> 4) << 4, oz = (pz >> 4) << 4;
                net.minecraft.client.renderer.chunk.RenderChunk rc = null;
                for (net.minecraft.client.renderer.chunk.RenderChunk c : vf.renderChunks) {
                    if (c == null) continue;
                    BlockPos p = c.getPosition();
                    if (p.getX() == ox && p.getY() == oy && p.getZ() == oz) { rc = c; break; }
                }
                if (rc == null) { // fallback: any non-empty section
                    for (net.minecraft.client.renderer.chunk.RenderChunk c : vf.renderChunks) {
                        if (c == null) continue;
                        BlockPos p = c.getPosition();
                        if (mc.world.getChunkFromBlockCoords(p).getBlockState(p).getBlock()
                                != net.minecraft.init.Blocks.AIR) { rc = c; break; }
                    }
                }
                if (rc == null) { r.resp.offer(err("no candidate RenderChunk")); return; }
                BlockPos origin = rc.getPosition();
                net.minecraft.client.renderer.chunk.ChunkCompileTaskGenerator gen = rc.makeCompileTaskChunk();
                // vanilla's chunk-render worker assigns these before calling rebuildChunk; do it ourselves.
                net.minecraft.client.renderer.RegionRenderCacheBuilder rb =
                    new net.minecraft.client.renderer.RegionRenderCacheBuilder();
                gen.setRegionRenderCacheBuilder(rb);
                gen.setStatus(net.minecraft.client.renderer.chunk.ChunkCompileTaskGenerator.Status.COMPILING);
                rc.rebuildChunk((float) mc.player.posX, (float) mc.player.posY, (float) mc.player.posZ, gen);

                java.lang.reflect.Field rib = net.minecraft.client.renderer.VertexBuffer.class
                    .getDeclaredField("rawIntBuffer"); rib.setAccessible(true);
                java.io.File gd = new java.io.File(dir); gd.mkdirs();
                java.io.PrintWriter inW = new java.io.PrintWriter(new java.io.BufferedWriter(
                    new java.io.FileWriter(new java.io.File(gd, "inputs.txt"))));
                java.io.PrintWriter goW = new java.io.PrintWriter(new java.io.BufferedWriter(
                    new java.io.FileWriter(new java.io.File(gd, "golden.txt"))));
                // input: 18^3 block-state IDs of the neighborhood (origin-1 .. origin+16)
                StringBuilder hdr = new StringBuilder();
                hdr.append("origin ").append(origin.getX()).append(' ').append(origin.getY())
                   .append(' ').append(origin.getZ()).append(" dim 18");
                inW.println(hdr.toString());
                for (int dx = -1; dx <= 16; dx++)
                  for (int dy = -1; dy <= 16; dy++)
                    for (int dz = -1; dz <= 16; dz++) {
                        BlockPos b = origin.add(dx, dy, dz);
                        int id = net.minecraft.block.Block.getStateId(mc.world.getBlockState(b));
                        inW.println(dx + " " + dy + " " + dz + " " + id);
                    }
                // golden: per layer (0..3 = SOLID, CUTOUT, CUTOUT_MIPPED, TRANSLUCENT) vertex ints
                int totalVerts = 0;
                for (int layer = 0; layer < 4; layer++) {
                    net.minecraft.client.renderer.VertexBuffer buf = rb.getWorldRendererByLayerId(layer);
                    int vc = buf.getVertexCount();
                    goW.println("layer " + layer + " vertexCount " + vc);
                    java.nio.IntBuffer ib = (java.nio.IntBuffer) rib.get(buf);
                    for (int z = 0; z < vc * 7; z++) goW.println(ib.get(z));
                    totalVerts += vc;
                }
                inW.flush(); inW.close(); goW.flush(); goW.close();
                System.out.println("[qrl] capture_chunkrebuild origin=" + origin + " totalVerts=" + totalVerts + " -> " + dir);
                JsonObject o = new JsonObject(); o.addProperty("ok", true);
                o.addProperty("origin", origin.toString()); o.addProperty("total_verts", totalVerts);
                o.addProperty("dir", dir); r.resp.offer(o.toString());
            } catch (Throwable t) {
                System.out.println("[qrl] capture_chunkrebuild failed: " + t);
                java.io.StringWriter swr = new java.io.StringWriter();
                t.printStackTrace(new java.io.PrintWriter(swr)); System.out.println(swr.toString());
                r.resp.offer(err("capture failed: " + t));
            }
            return;
        }
        if (r.cmd.equals("close")) {
            if (mc.world != null) { mc.loadWorld(null); }
            launching = false;
            r.resp.offer("{\"ok\":true}");
            return;
        }

        // dumpblocks: write a chunk window as raw little-endian u16 (id<<4|meta), the .mcbd
        // body layout ((y*16+z)*16+x per chunk, chunks cz-major) - the tape recorder wraps
        // the header. Runs on the game thread so block reads are tick-consistent.
        if (r.cmd.equals("dumpblocks")) {
            try {
                final net.minecraft.world.World w = mc.world;
                if (w == null) { r.resp.offer(err("no world")); return; }
                int cx0, cz0, cx1, cz1;
                if (r.action.has("radius")) {
                    int rad = r.action.get("radius").getAsInt();
                    int pcx = ((int) Math.floor(mc.player.posX)) >> 4;
                    int pcz = ((int) Math.floor(mc.player.posZ)) >> 4;
                    cx0 = pcx - rad; cx1 = pcx + rad; cz0 = pcz - rad; cz1 = pcz + rad;
                } else {
                    cx0 = r.action.get("cx0").getAsInt(); cz0 = r.action.get("cz0").getAsInt();
                    cx1 = r.action.get("cx1").getAsInt(); cz1 = r.action.get("cz1").getAsInt();
                }
                String file = r.action.get("file").getAsString();
                java.io.DataOutputStream o = new java.io.DataOutputStream(
                    new java.io.BufferedOutputStream(new java.io.FileOutputStream(file), 1 << 20));
                byte[] buf = new byte[16 * 16 * 256 * 2];
                net.minecraft.util.math.BlockPos.MutableBlockPos pos =
                    new net.minecraft.util.math.BlockPos.MutableBlockPos();
                for (int cz = cz0; cz <= cz1; cz++) for (int cx = cx0; cx <= cx1; cx++) {
                    int k = 0;
                    for (int y = 0; y < 256; y++) for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
                        net.minecraft.block.state.IBlockState st =
                            w.getBlockState(pos.setPos(cx * 16 + x, y, cz * 16 + z));
                        net.minecraft.block.Block b = st.getBlock();
                        int v = (net.minecraft.block.Block.getIdFromBlock(b) << 4) | b.getMetaFromState(st);
                        buf[k++] = (byte) (v & 0xff); buf[k++] = (byte) ((v >> 8) & 0xff);
                    }
                    o.write(buf, 0, k);
                }
                o.close();
                r.resp.offer("{\"ok\":true,\"file\":\"" + file + "\",\"cx0\":" + cx0 + ",\"cz0\":" + cz0
                    + ",\"cx1\":" + cx1 + ",\"cz1\":" + cz1 + "}");
            } catch (Exception ex) { r.resp.offer(err("dumpblocks: " + ex)); }
            return;
        }

        // setblocks: batch-place blocks by NUMERIC id+meta on the SERVER world, on the
        // server thread (safe mutation), with setBlockState flag 3 so onBlockAdded fires
        // (fire/flowing-liquid schedule their first updateTick exactly as vanilla placement
        // does). Input: action.blocks = [[x,y,z,id,meta], ...]. Tick-trace scenario setup.
        if (r.cmd.equals("setblocks")) {
            final MinecraftServer s = mc.getIntegratedServer();
            if (s == null) { r.resp.offer(err("no server")); return; }
            final com.google.gson.JsonArray blocks = r.action.has("blocks")
                ? r.action.getAsJsonArray("blocks") : new com.google.gson.JsonArray();
            final Req fr = r;
            s.addScheduledTask(new Runnable() { public void run() {
                try {
                    net.minecraft.world.WorldServer w = s.worlds[0];  // overworld fallback
                    try {
                        java.util.List<net.minecraft.entity.player.EntityPlayerMP> qps = s.getPlayerList().getPlayers();
                        if (!qps.isEmpty()) w = qps.get(0).getServerWorld();
                    } catch (Throwable ig) {}
                    int n = 0;
                    for (com.google.gson.JsonElement el : blocks) {
                        com.google.gson.JsonArray a = el.getAsJsonArray();
                        int x = a.get(0).getAsInt(), y = a.get(1).getAsInt(), z = a.get(2).getAsInt();
                        int id = a.get(3).getAsInt(), meta = a.get(4).getAsInt();
                        net.minecraft.block.Block b = net.minecraft.block.Block.getBlockById(id);
                        net.minecraft.block.state.IBlockState bs = b.getStateFromMeta(meta);
                        w.setBlockState(new BlockPos(x, y, z), bs, 3);
                        n++;
                    }
                    // placement tick captured ATOMICALLY with the writes: the trace base
                    // tick, so frame "t" offsets are exact relative to onBlockAdded.
                    fr.resp.offer("{\"ok\":true,\"set\":" + n
                        + ",\"num_ticks\":" + TimeHelper.SyncManager.numTicks + "}");
                } catch (Throwable t) { fr.resp.offer(err("setblocks: " + t)); }
            }});
            return;
        }

        // getblocks: dump an ARBITRARY cuboid [x0,y0,z0]..[x1,y1,z1] inclusive from the
        // SERVER world (tick-consistent, unlike client-side dumpblocks) as raw little-endian
        // u16 (id<<4|meta), iteration order y-major then z then x. Runs on the server thread
        // and offers the response only after the file is written, so the client's blocking
        // read is a per-tick barrier. Tick-trace scenario capture.
        if (r.cmd.equals("getblocks")) {
            final MinecraftServer s = mc.getIntegratedServer();
            if (s == null) { r.resp.offer(err("no server")); return; }
            final int x0 = r.action.get("x0").getAsInt(), y0 = r.action.get("y0").getAsInt();
            final int z0 = r.action.get("z0").getAsInt(), x1 = r.action.get("x1").getAsInt();
            final int y1 = r.action.get("y1").getAsInt(), z1 = r.action.get("z1").getAsInt();
            final String file = r.action.get("file").getAsString();
            final Req fr = r;
            s.addScheduledTask(new Runnable() { public void run() {
                try {
                    net.minecraft.world.WorldServer w = s.worlds[0];  // overworld fallback
                    try {
                        java.util.List<net.minecraft.entity.player.EntityPlayerMP> qps = s.getPlayerList().getPlayers();
                        if (!qps.isEmpty()) w = qps.get(0).getServerWorld();
                    } catch (Throwable ig) {}
                    int nx = x1 - x0 + 1, ny = y1 - y0 + 1, nz = z1 - z0 + 1;
                    byte[] buf = new byte[nx * ny * nz * 2];
                    net.minecraft.util.math.BlockPos.MutableBlockPos pos =
                        new net.minecraft.util.math.BlockPos.MutableBlockPos();
                    int k = 0;
                    for (int y = y0; y <= y1; y++) for (int z = z0; z <= z1; z++) for (int x = x0; x <= x1; x++) {
                        net.minecraft.block.state.IBlockState st = w.getBlockState(pos.setPos(x, y, z));
                        net.minecraft.block.Block b = st.getBlock();
                        int v = (net.minecraft.block.Block.getIdFromBlock(b) << 4) | b.getMetaFromState(st);
                        buf[k++] = (byte) (v & 0xff); buf[k++] = (byte) ((v >> 8) & 0xff);
                    }
                    java.io.DataOutputStream o = new java.io.DataOutputStream(
                        new java.io.BufferedOutputStream(new java.io.FileOutputStream(file), 1 << 20));
                    o.write(buf, 0, k); o.close();
                    // authoritative tick captured ATOMICALLY with the dump (same server task),
                    // so the trace label matches the exact tick the blocks were read at.
                    fr.resp.offer("{\"ok\":true,\"file\":\"" + file + "\",\"nx\":" + nx
                        + ",\"ny\":" + ny + ",\"nz\":" + nz
                        + ",\"num_ticks\":" + TimeHelper.SyncManager.numTicks + "}");
                } catch (Throwable t) { fr.resp.offer(err("getblocks: " + t)); }
            }});
            return;
        }

        // summon: spawn ONE non-player entity by type at an exact position with an exact
        // initial motion, on the SERVER world/thread (safe mutation). Bypasses bow/AI RNG:
        // motion is written DIRECTLY so the C entity_trace verifier loads a byte-identical
        // start. type "arrow" -> EntityTippedArrow (no shooter, no inaccuracy draw);
        // type "zombie" -> EntityZombie with optional NoAI (pure-physics golden). Returns the
        // entity id + num_ticks ATOMICALLY (same server task) so the trace base tick is exact.
        if (r.cmd.equals("summon")) {
            final MinecraftServer s = mc.getIntegratedServer();
            if (s == null) { r.resp.offer(err("no server")); return; }
            final String type = opt(r.action, "type");
            final double sx = r.action.get("x").getAsDouble();
            final double sy = r.action.get("y").getAsDouble();
            final double sz = r.action.get("z").getAsDouble();
            final boolean hasMotion = r.action.has("mx");
            final double mx = hasMotion ? r.action.get("mx").getAsDouble() : 0.0;
            final double my = hasMotion ? r.action.get("my").getAsDouble() : 0.0;
            final double mz = hasMotion ? r.action.get("mz").getAsDouble() : 0.0;
            final boolean noai = r.action.has("noai") && r.action.get("noai").getAsBoolean();
            final Req fr = r;
            s.addScheduledTask(new Runnable() { public void run() {
                try {
                    net.minecraft.world.WorldServer w = s.worlds[0];
                    try {
                        java.util.List<net.minecraft.entity.player.EntityPlayerMP> qps = s.getPlayerList().getPlayers();
                        if (!qps.isEmpty()) w = qps.get(0).getServerWorld();
                    } catch (Throwable ig) {}
                    net.minecraft.entity.Entity ent;
                    if ("zombie".equalsIgnoreCase(type)) {
                        net.minecraft.entity.monster.EntityZombie z = new net.minecraft.entity.monster.EntityZombie(w);
                        z.setLocationAndAngles(sx, sy, sz, 0.0F, 0.0F);
                        if (noai) z.setNoAI(true);
                        ent = z;
                    } else if ("item".equalsIgnoreCase(type)) {
                        // EntityItem with an EXACT ItemStack + pickup delay. The 5-arg ctor sets
                        // random initial motion + a random rotationYaw; we overwrite motion below
                        // (hasMotion is always sent by the capture) so the C side is deterministic.
                        String itemName = r.action.has("item") ? r.action.get("item").getAsString() : "minecraft:stone";
                        int count = r.action.has("count") ? r.action.get("count").getAsInt() : 1;
                        int meta = r.action.has("meta") ? r.action.get("meta").getAsInt() : 0;
                        net.minecraft.item.Item it = net.minecraft.item.Item.getByNameOrId(itemName);
                        net.minecraft.item.ItemStack stk = new net.minecraft.item.ItemStack(it, count, meta);
                        net.minecraft.entity.item.EntityItem ei =
                            new net.minecraft.entity.item.EntityItem(w, sx, sy, sz, stk);
                        int pd = r.action.has("pickupdelay") ? r.action.get("pickupdelay").getAsInt() : 0;
                        ei.setPickupDelay(pd);
                        ent = ei;
                    } else if ("xporb".equalsIgnoreCase(type)) {
                        int val = r.action.has("value") ? r.action.get("value").getAsInt() : 1;
                        net.minecraft.entity.item.EntityXPOrb orb =
                            new net.minecraft.entity.item.EntityXPOrb(w, sx, sy, sz, val);
                        ent = orb;
                    } else {   // default: arrow (EntityTippedArrow, no shooter)
                        net.minecraft.entity.projectile.EntityTippedArrow ar =
                            new net.minecraft.entity.projectile.EntityTippedArrow(w, sx, sy, sz);
                        ent = ar;
                    }
                    if (hasMotion) { ent.motionX = mx; ent.motionY = my; ent.motionZ = mz; }
                    w.spawnEntity(ent);
                    JsonObject o = new JsonObject();
                    o.addProperty("ok", true);
                    o.addProperty("eid", ent.getEntityId());
                    o.addProperty("num_ticks", TimeHelper.SyncManager.numTicks);
                    fr.resp.offer(o.toString());
                } catch (Throwable t) { fr.resp.offer(err("summon: " + t)); }
            }});
            return;
        }

        // killentities: setDead every non-player entity on the SERVER world (clean scenario
        // start), server-thread. Returns num_ticks atomically.
        if (r.cmd.equals("killentities")) {
            final MinecraftServer s = mc.getIntegratedServer();
            if (s == null) { r.resp.offer(err("no server")); return; }
            final Req fr = r;
            s.addScheduledTask(new Runnable() { public void run() {
                try {
                    net.minecraft.world.WorldServer w = s.worlds[0];
                    try {
                        java.util.List<net.minecraft.entity.player.EntityPlayerMP> qps = s.getPlayerList().getPlayers();
                        if (!qps.isEmpty()) w = qps.get(0).getServerWorld();
                    } catch (Throwable ig) {}
                    int n = 0;
                    for (net.minecraft.entity.Entity e : new java.util.ArrayList<net.minecraft.entity.Entity>(w.loadedEntityList)) {
                        if (e instanceof net.minecraft.entity.player.EntityPlayer) continue;
                        e.setDead();
                        n++;
                    }
                    fr.resp.offer("{\"ok\":true,\"killed\":" + n
                        + ",\"num_ticks\":" + TimeHelper.SyncManager.numTicks + "}");
                } catch (Throwable t) { fr.resp.offer(err("killentities: " + t)); }
            }});
            return;
        }

        // getentities: dump EVERY non-player entity on the SERVER world (tick-consistent) as
        // RAW bits so doubles/floats survive JSON with no rounding: pos/motion via
        // Double.doubleToRawLongBits, yaw/pitch/health via Float.floatToRawIntBits. Also
        // onGround, fallDistance, air, and (arrows only, reflected) the protected inGround /
        // ticksInAir stick state. num_ticks captured ATOMICALLY (same server task) is the
        // authoritative per-tick trace label. Fields the C side compares bit-exact.
        if (r.cmd.equals("getentities")) {
            final MinecraftServer s = mc.getIntegratedServer();
            if (s == null) { r.resp.offer(err("no server")); return; }
            final Req fr = r;
            s.addScheduledTask(new Runnable() { public void run() {
                try {
                    net.minecraft.world.WorldServer w = s.worlds[0];
                    try {
                        java.util.List<net.minecraft.entity.player.EntityPlayerMP> qps = s.getPlayerList().getPlayers();
                        if (!qps.isEmpty()) w = qps.get(0).getServerWorld();
                    } catch (Throwable ig) {}
                    JsonArray arr = new JsonArray();
                    for (net.minecraft.entity.Entity e : new java.util.ArrayList<net.minecraft.entity.Entity>(w.loadedEntityList)) {
                        if (e instanceof net.minecraft.entity.player.EntityPlayer) continue;
                        JsonObject je = new JsonObject();
                        je.addProperty("eid", e.getEntityId());
                        je.addProperty("type", e.getClass().getSimpleName());
                        je.addProperty("x", Double.doubleToRawLongBits(e.posX));
                        je.addProperty("y", Double.doubleToRawLongBits(e.posY));
                        je.addProperty("z", Double.doubleToRawLongBits(e.posZ));
                        je.addProperty("mx", Double.doubleToRawLongBits(e.motionX));
                        je.addProperty("my", Double.doubleToRawLongBits(e.motionY));
                        je.addProperty("mz", Double.doubleToRawLongBits(e.motionZ));
                        je.addProperty("yaw", Float.floatToRawIntBits(e.rotationYaw));
                        je.addProperty("pitch", Float.floatToRawIntBits(e.rotationPitch));
                        je.addProperty("onGround", e.onGround ? 1 : 0);
                        je.addProperty("fall", Float.floatToRawIntBits(e.fallDistance));
                        je.addProperty("air", getAir(e));
                        int inGround = -1, ticksAir = -1;
                        if (e instanceof net.minecraft.entity.projectile.EntityArrow) {
                            inGround = arrowInGround((net.minecraft.entity.projectile.EntityArrow) e) ? 1 : 0;
                            ticksAir = arrowTicksInAir((net.minecraft.entity.projectile.EntityArrow) e);
                        }
                        je.addProperty("inGround", inGround);
                        je.addProperty("ticksInAir", ticksAir);
                        if (e instanceof net.minecraft.entity.EntityLivingBase) {
                            je.addProperty("health", Float.floatToRawIntBits(((net.minecraft.entity.EntityLivingBase) e).getHealth()));
                        } else {
                            je.addProperty("health", Float.floatToRawIntBits(-1.0F));
                        }
                        // EntityItem / EntityXPOrb bit-exact port fields (PORT_MATRIX P2). Defaults
                        // -1 for entities that don't carry the field; age/pickupdelay of EntityItem
                        // are private -> reflected. Orb fields (xpValue/delay/xpColor/age) public.
                        int itemid = -1, count = -1, meta = -1, age = -1, pickupdelay = -1, lifespan = -1;
                        int xpvalue = -1, xpcolor = -1;
                        if (e instanceof net.minecraft.entity.item.EntityItem) {
                            net.minecraft.entity.item.EntityItem ei = (net.minecraft.entity.item.EntityItem) e;
                            net.minecraft.item.ItemStack stk = ei.getEntityItem();
                            itemid = net.minecraft.item.Item.getIdFromItem(stk.getItem());
                            count = stk.getCount();
                            meta = stk.getMetadata();
                            age = itemField(ei, "age");
                            pickupdelay = itemField(ei, "delayBeforeCanPickup");
                            lifespan = ei.lifespan;
                        } else if (e instanceof net.minecraft.entity.item.EntityXPOrb) {
                            net.minecraft.entity.item.EntityXPOrb orb = (net.minecraft.entity.item.EntityXPOrb) e;
                            xpvalue = orb.xpValue;
                            pickupdelay = orb.delayBeforeCanPickup;
                            xpcolor = orb.xpColor;
                            age = orb.xpOrbAge;
                        }
                        je.addProperty("itemid", itemid);
                        je.addProperty("count", count);
                        je.addProperty("meta", meta);
                        je.addProperty("age", age);
                        je.addProperty("pickupdelay", pickupdelay);
                        je.addProperty("lifespan", lifespan);
                        je.addProperty("xpvalue", xpvalue);
                        je.addProperty("xpcolor", xpcolor);
                        arr.add(je);
                    }
                    JsonObject o = new JsonObject();
                    o.addProperty("ok", true);
                    o.addProperty("num_ticks", TimeHelper.SyncManager.numTicks);
                    o.add("ents", arr);
                    // player pos + eye height (constant while parked) for the XP-orb attraction port:
                    // orb pull reads closestPlayer.posX/Y/Z + getEyeHeight()/2. Captured atomically.
                    try {
                        java.util.List<net.minecraft.entity.player.EntityPlayerMP> pl = s.getPlayerList().getPlayers();
                        if (!pl.isEmpty()) {
                            net.minecraft.entity.player.EntityPlayerMP p = pl.get(0);
                            JsonObject pj = new JsonObject();
                            pj.addProperty("x", Double.doubleToRawLongBits(p.posX));
                            pj.addProperty("y", Double.doubleToRawLongBits(p.posY));
                            pj.addProperty("z", Double.doubleToRawLongBits(p.posZ));
                            pj.addProperty("eye", Float.floatToRawIntBits(p.getEyeHeight()));
                            pj.addProperty("spectator", p.isSpectator() ? 1 : 0);
                            o.add("player", pj);
                        }
                    } catch (Throwable ig) {}
                    fr.resp.offer(o.toString());
                } catch (Throwable t) { fr.resp.offer(err("getentities: " + t)); }
            }});
            return;
        }

        // frame: read the rendered framebuffer into a PNG at the given path. Runs on the
        // client thread (GL context). Caller pauses inputs and lets a frame render first;
        // at a quiescent tick every rendered frame shows the same world state (fixed time,
        // clouds off), so this is tick-locked for tape keyframes.
        if (r.cmd.equals("frame")) {
            try {
                String file = r.action.get("file").getAsString();
                int fw = mc.displayWidth, fh = mc.displayHeight;
                // Use the vanilla screenshot path (glGetTexImage from the FBO's color
                // texture when FBOs are on, glReadPixels otherwise): on macOS 26's
                // GL-on-Metal layer a raw glReadPixels of the bound FBO / window
                // surface returns all black, but the F2 texture-readback path works.
                java.awt.image.BufferedImage img =
                    net.minecraft.util.ScreenShotHelper.createScreenshot(fw, fh, mc.getFramebuffer());
                javax.imageio.ImageIO.write(img, "png", new java.io.File(file));
                r.resp.offer("{\"ok\":true,\"file\":\"" + file + "\",\"w\":" + fw + ",\"h\":" + fh + "}");
            } catch (Exception ex) { r.resp.offer(err("frame: " + ex)); }
            return;
        }

        // gldiag: interrogate the GL context from the client thread. Distinguishes
        // "no real GL" (null strings) / "FBO never rendered to" / "readback broken"
        // (clear-to-red test: clear the MC FBO red, read its texture back).
        // mouse-look debugging for the mcwindow human-play route
        if (r.cmd.equals("focusdiag")) {
            try {
                boolean act = org.lwjgl.opengl.Display.isActive();
                boolean grabbed = org.lwjgl.input.Mouse.isGrabbed();
                // reply(), not resp.offer(): this handler is fast enough to lose
                // the SynchronousQueue race (see reply() comment) and wedge serve().
                reply(r, "{\"ok\":true"
                    + ",\"display_active\":" + act
                    + ",\"ingame_focus\":" + mc.inGameHasFocus
                    + ",\"mouse_grabbed\":" + grabbed
                    + ",\"screen\":" + (mc.currentScreen == null ? "null"
                        : "\"" + mc.currentScreen.getClass().getSimpleName() + "\"") + "}");
            } catch (Throwable t) { reply(r, err("focusdiag: " + t)); }
            return;
        }

        if (r.cmd.equals("gldiag")) {
            try {
                org.lwjgl.opengl.GL11.glGetError(); // clear stale error
                String ver = org.lwjgl.opengl.GL11.glGetString(org.lwjgl.opengl.GL11.GL_VERSION);
                String ren = org.lwjgl.opengl.GL11.glGetString(org.lwjgl.opengl.GL11.GL_RENDERER);
                int errAfterGetString = org.lwjgl.opengl.GL11.glGetError();
                boolean fboEnabled = net.minecraft.client.renderer.OpenGlHelper.isFramebufferEnabled();
                net.minecraft.client.shader.Framebuffer fb = mc.getFramebuffer();
                int fboId = fb.framebufferObject, texId = fb.framebufferTexture;
                // clear-to-red test on the MC FBO, then read back via both paths
                fb.bindFramebuffer(true);
                org.lwjgl.opengl.GL11.glClearColor(1f, 0f, 0f, 1f);
                org.lwjgl.opengl.GL11.glClear(org.lwjgl.opengl.GL11.GL_COLOR_BUFFER_BIT);
                java.nio.ByteBuffer px = org.lwjgl.BufferUtils.createByteBuffer(4);
                org.lwjgl.opengl.GL11.glReadPixels(0, 0, 1, 1,
                    org.lwjgl.opengl.GL11.GL_RGBA, org.lwjgl.opengl.GL11.GL_UNSIGNED_BYTE, px);
                int readR = px.get(0) & 0xff;
                fb.unbindFramebuffer();
                org.lwjgl.opengl.GL11.glBindTexture(org.lwjgl.opengl.GL11.GL_TEXTURE_2D, texId);
                java.nio.ByteBuffer tex = org.lwjgl.BufferUtils.createByteBuffer(
                    fb.framebufferTextureWidth * fb.framebufferTextureHeight * 4);
                org.lwjgl.opengl.GL11.glGetTexImage(org.lwjgl.opengl.GL11.GL_TEXTURE_2D, 0,
                    org.lwjgl.opengl.GL11.GL_RGBA, org.lwjgl.opengl.GL11.GL_UNSIGNED_BYTE, tex);
                int texR = tex.get(0) & 0xff;
                int errEnd = org.lwjgl.opengl.GL11.glGetError();
                r.resp.offer("{\"ok\":true"
                    + ",\"gl_version\":" + (ver == null ? "null" : "\"" + ver.replace("\"", "'") + "\"")
                    + ",\"gl_renderer\":" + (ren == null ? "null" : "\"" + ren.replace("\"", "'") + "\"")
                    + ",\"err_after_getstring\":" + errAfterGetString
                    + ",\"fbo_enabled\":" + fboEnabled
                    + ",\"fbo_id\":" + fboId + ",\"tex_id\":" + texId
                    + ",\"clear_red_readpixels_r\":" + readR
                    + ",\"clear_red_gettex_r\":" + texR
                    + ",\"err_end\":" + errEnd + "}");
            } catch (Throwable t) { r.resp.offer(err("gldiag: " + t)); }
            return;
        }

        // reset handles the no-world case by launching a world headlessly (async)
        if (r.cmd.equals("reset")) {
            // "fresh": true forces a brand-new world at the requested seed: tear down the
            // loaded world, wipe its save folder (folders are qrl_<seed>, so a stale save
            // from a prior run would otherwise be silently re-joined), relaunch. Without it
            // reset re-uses whatever world is loaded regardless of seed (RL fast-reset path).
            boolean fresh = r.world != null && r.world.has("fresh")
                && r.world.get("fresh").getAsBoolean();
            // The save folder this reset is asking for (null = don't care).
            // ok may only be returned FROM that world: a fresh-reset poll can
            // otherwise race the async relaunch and get an "ok" obs from the
            // OLD still-loaded world (observed: seed sweeps returning
            // byte-identical camera counts for different seeds).
            String wantFolder = null;
            if (fresh || (r.world != null && r.world.has("seed")))
                wantFolder = "qrl_" + (r.world.has("seed") ? r.world.get("seed").getAsLong() : 0L)
                    + ("flat".equalsIgnoreCase(opt(r.world, "type")) ? "_flat" : "");
            String curFolder = null;
            try { curFolder = mc.getIntegratedServer().getFolderName(); }
            catch (Throwable ig) {}
            if (mc.player != null && mc.world != null && fresh && !launching) {
                try {
                    // Do NOT sendQuittingDisconnectingPacket() first: on an unpaused
                    // headless server it starts a concurrent server-side shutdown, and
                    // IntegratedServer.initiateShutdown()'s Futures.getUnchecked then
                    // waits forever on a task queued to the already-exiting server
                    // thread (observed hard hang, jstack: Client thread parked in
                    // initiateShutdown). loadWorld(null) already does a clean
                    // nethandler.cleanup() + blocking shutdown of a live server.
                    mc.loadWorld((net.minecraft.client.multiplayer.WorldClient) null);
                    mc.getSaveLoader().flushCache();
                    mc.getSaveLoader().deleteWorldDirectory(wantFolder);
                    launchWorld(mc, r.world);
                    launching = true;
                } catch (Throwable ex) {
                    System.out.println("[qrl] fresh reset failed: " + ex);
                }
                reply(r, loading());
                return;
            }
            if (mc.player != null && mc.world != null
                && (fresh || launching) && wantFolder != null
                && !wantFolder.equals(curFolder)) {
                reply(r, loading());   // wrong/stale world still loaded
                return;
            }
            if (mc.player != null && mc.world != null) {
                if (launching && initStartNanos > 0) {
                    lastInitMs = (System.nanoTime() - initStartNanos) / 1.0e6;
                    initStartNanos = 0;
                }
                launching = false;
                BlockPos sp = mc.world.getSpawnPoint();
                mc.player.setPositionAndUpdate(sp.getX() + 0.5, sp.getY(), sp.getZ() + 0.5);
                mc.player.motionX = mc.player.motionY = mc.player.motionZ = 0;
                reply(r, obs(mc));
            } else {
                if (!launching) { launchWorld(mc, r.world); launching = true; }
                reply(r, loading());
            }
            return;
        }

        if (mc.player == null) { reply(r, err("no world loaded")); return; }
        if (r.cmd.equals("obs")) {
            reply(r, obs(mc, r.action.has("cam") && r.action.get("cam").getAsInt() != 0));
            return;
        }
        // camera: self-describing golden provenance (eye pos, effective FOV, options, world).
        // Fable audit: every frame must ship machine-readable scene state so verify never
        // uses a hand-maintained pose file.
        if (r.cmd.equals("camera")) {
            try { r.resp.offer(camera(mc, r.action)); }
            catch (Throwable t) { r.resp.offer(err("camera: " + t)); }
            return;
        }
        /* Non-destructive sky/block light sample over an AABB (no glass/glow placement).
         * action: x0,y0,z0,x1,y1,z1 inclusive OR cx,cz,radius_chunks + y0,y1.
         * Optional file: write CSV "wx wy wz sky blk". Response includes path + n. */
        if (r.cmd.equals("sample_light")) {
            /* Prefer integrated server WorldServer light (authoritative). Client
             * WorldClient often reports full skylight=15 until chunks re-light. */
            final MinecraftServer s = mc.getIntegratedServer();
            if (s == null || mc.world == null) { r.resp.offer(err("no world")); return; }
            final int x0, y0, z0, x1, y1, z1;
            if (r.action.has("cx") && r.action.has("cz")) {
                int cx = r.action.get("cx").getAsInt();
                int cz = r.action.get("cz").getAsInt();
                int rad = r.action.has("radius") ? r.action.get("radius").getAsInt() : 2;
                x0 = (cx - rad) * 16; x1 = (cx + rad) * 16 + 15;
                z0 = (cz - rad) * 16; z1 = (cz + rad) * 16 + 15;
                y0 = r.action.has("y0") ? r.action.get("y0").getAsInt() : 50;
                y1 = r.action.has("y1") ? r.action.get("y1").getAsInt() : 120;
            } else {
                x0 = r.action.get("x0").getAsInt(); y0 = r.action.get("y0").getAsInt();
                z0 = r.action.get("z0").getAsInt();
                x1 = r.action.get("x1").getAsInt(); y1 = r.action.get("y1").getAsInt();
                z1 = r.action.get("z1").getAsInt();
            }
            final String path = r.action.has("file") ? r.action.get("file").getAsString()
                : "/tmp/hard_scene_agents/java_light.csv";
            final Req fr = r;
            s.addScheduledTask(new Runnable() { public void run() {
                try {
                    net.minecraft.world.WorldServer w = s.worlds[0];
                    try {
                        java.util.List<net.minecraft.entity.player.EntityPlayerMP> qps =
                            s.getPlayerList().getPlayers();
                        if (!qps.isEmpty()) w = qps.get(0).getServerWorld();
                    } catch (Throwable ig) {}
                    java.io.PrintWriter pw = new java.io.PrintWriter(
                        new java.io.BufferedWriter(new java.io.FileWriter(path)));
                    pw.println("wx wy wz sky blk");
                    net.minecraft.util.math.BlockPos.MutableBlockPos pos =
                        new net.minecraft.util.math.BlockPos.MutableBlockPos();
                    int n = 0;
                    for (int y = y0; y <= y1; y++)
                        for (int z = z0; z <= z1; z++)
                            for (int x = x0; x <= x1; x++) {
                                pos.setPos(x, y, z);
                                if (!w.isBlockLoaded(pos)) continue;
                                int sky = w.getLightFor(
                                    net.minecraft.world.EnumSkyBlock.SKY, pos);
                                int blk = w.getLightFor(
                                    net.minecraft.world.EnumSkyBlock.BLOCK, pos);
                                pw.println(x + " " + y + " " + z + " " + sky + " " + blk);
                                n++;
                            }
                    pw.close();
                    JsonObject o = new JsonObject();
                    o.addProperty("ok", true); o.addProperty("n", n);
                    o.addProperty("file", path); o.addProperty("source", "WorldServer");
                    o.addProperty("x0", x0); o.addProperty("y0", y0); o.addProperty("z0", z0);
                    o.addProperty("x1", x1); o.addProperty("y1", y1); o.addProperty("z1", z1);
                    fr.resp.offer(o.toString());
                } catch (Throwable t) {
                    fr.resp.offer(err("sample_light: " + t));
                }
            }});
            return;
        }
        // step: apply now, read obs next tick
        applyAction(mc, r.action);
        r.applied = true;
        inFlight = r;
        } catch (Throwable t) {
            r.resp.offer(err("command failed: " + t));
        }
    }

    private void applyAction(Minecraft mc, JsonObject a) {
        EntityPlayerSP p = mc.player;
        key(mc.gameSettings.keyBindForward, bit(a, "forward"));
        key(mc.gameSettings.keyBindBack,    bit(a, "back"));
        key(mc.gameSettings.keyBindLeft,    bit(a, "left"));
        key(mc.gameSettings.keyBindRight,   bit(a, "right"));
        key(mc.gameSettings.keyBindJump,    bit(a, "jump"));
        key(mc.gameSettings.keyBindSneak,   bit(a, "sneak"));
        key(mc.gameSettings.keyBindAttack,  bit(a, "attack"));
        key(mc.gameSettings.keyBindUseItem, bit(a, "use"));
        // sprint is INPUT, not state: press the sprint keybind and let the vanilla
        // EntityPlayerSP rules (sprint key hold, double-tap-W toggle timer, stop on
        // collide/slow/hunger) resolve the actual sprint flag. Poking setSprinting()
        // directly fought those rules every tick and desynced from real-input play.
        key(mc.gameSettings.keyBindSprint, bit(a, "sprint"));
        if (a.has("hotbar")) {
            int h = a.get("hotbar").getAsInt();
            if (h >= 0 && h <= 8) p.inventory.currentItem = h;
        }
        int dy = a.has("yaw") ? a.get("yaw").getAsInt() : 0;
        int dp = a.has("pitch") ? a.get("pitch").getAsInt() : 0;
        if (dy != 0) p.rotationYaw = (Math.round(p.rotationYaw / QUANTUM) + dy) * QUANTUM;
        if (dp != 0) p.rotationPitch = clamp((Math.round(p.rotationPitch / QUANTUM) + dp) * QUANTUM, -90, 90);
        // ---- protocol v2 (chain policy) extensions ----
        // Continuous look deltas in degrees: the blaze-trained heads emit
        // dyaw in {-15,0,15} and dpitch in {-10,0,10}; the legacy quantized
        // yaw/pitch keys cannot express 10-deg pitch steps.
        if (a.has("dyaw")) p.rotationYaw += a.get("dyaw").getAsFloat();
        if (a.has("dpitch"))
            p.rotationPitch = clamp(p.rotationPitch + a.get("dpitch").getAsFloat(), -90, 90);
        // Semantic camera request: obs after THIS step includes cam/depth/edge
        // + the coal list. Off by default (the raycast is the obs cost).
        rlCamPending = a.has("cam") && a.get("cam").getAsInt() != 0;
        // interact: open the nearest placed crafting table/furnace in reach
        // (fires once, silent failure - rl_do_interact semantics).
        if (a.has("interact") && a.get("interact").getAsInt() != 0) rlInteract(mc);
        // craft: discrete primitive 0..7, fires once, silent failure.
        if (a.has("craft")) rlCraft(mc, a.get("craft").getAsInt());
        // smelt: furnace load/collect primitive (rl_do_smelt semantics) -
        // needs the open furnace container, fires once, fails silently.
        if (a.has("smelt") && a.get("smelt").getAsInt() != 0) rlSmelt(mc);
    }

    /** interact: nearest crafting table (58) / furnace (61/62) whose center is
     * within 6.0 of the eye (gm_runtime_use_block's 36.0 squared reach) opens
     * as the active container. Mod state only - no GUI (a real GuiCrafting
     * pauses the headless client and the unpause guard would close it). */
    private void rlInteract(Minecraft mc) {
        EntityPlayerSP p = mc.player;
        if (p == null || mc.world == null) return;
        double ex = p.posX, ey = p.posY + SemanticCamera.EYE, ez = p.posZ;
        int pwx = (int) Math.floor(p.posX), pwy = (int) Math.floor(p.posY),
            pwz = (int) Math.floor(p.posZ);
        double bd = 36.0;
        BlockPos best = null;
        int bestId = 0;
        BlockPos.MutableBlockPos mp = new BlockPos.MutableBlockPos();
        for (int x = pwx - 6; x <= pwx + 6; ++x)
            for (int y = Math.max(0, pwy - 6); y <= Math.min(255, pwy + 6); ++y)
                for (int z = pwz - 6; z <= pwz + 6; ++z) {
                    mp.setPos(x, y, z);
                    int id = net.minecraft.block.Block.getIdFromBlock(
                        mc.world.getBlockState(mp).getBlock());
                    if (id != 58 && id != 61 && id != 62) continue;
                    double dx = x + 0.5 - ex, dy2 = y + 0.5 - ey, dz = z + 0.5 - ez;
                    double d2 = dx * dx + dy2 * dy2 + dz * dz;
                    if (d2 < bd) { bd = d2; best = new BlockPos(x, y, z); bestId = id; }
                }
        if (best != null) {
            rlContainer = bestId == 58 ? 1 : 2;
            rlContainerPos = best;
        }
    }

    /** Keep-open rule from gm_runtime_tick: the container closes when its
     * block is gone/changed or its center leaves the 6.0 eye reach. */
    private void rlContainerTick(Minecraft mc) {
        if (rlContainer == 0) return;
        EntityPlayerSP p = mc.player;
        if (p == null || mc.world == null || rlContainerPos == null) { rlContainer = 0; return; }
        int id = net.minecraft.block.Block.getIdFromBlock(
            mc.world.getBlockState(rlContainerPos).getBlock());
        boolean valid = rlContainer == 1 ? id == 58 : (id == 61 || id == 62);
        double dx = rlContainerPos.getX() + 0.5 - p.posX;
        double dy = rlContainerPos.getY() + 0.5 - (p.posY + SemanticCamera.EYE);
        double dz = rlContainerPos.getZ() + 0.5 - p.posZ;
        if (!valid || dx * dx + dy * dy + dz * dz > 36.0) {
            rlContainer = 0;
            rlContainerPos = null;
        }
    }

    private static int rlCountItem(net.minecraft.entity.player.InventoryPlayer inv, int id) {
        int n = 0;
        for (int i = 0; i < inv.mainInventory.size(); i++) {
            net.minecraft.item.ItemStack s = inv.mainInventory.get(i);
            if (!s.isEmpty() && stackId(s) == id) n += s.getCount();
        }
        return n;
    }

    private static void rlConsumeItem(net.minecraft.entity.player.InventoryPlayer inv,
                                      int id, int count) {
        for (int i = 0; i < inv.mainInventory.size() && count > 0; i++) {
            net.minecraft.item.ItemStack s = inv.mainInventory.get(i);
            if (s.isEmpty() || stackId(s) != id) continue;
            int take = Math.min(count, s.getCount());
            s.shrink(take);
            count -= take;
        }
    }

    /** Discrete craft primitive: verify counts, consume inputs, add the
     * output - rl_do_craft/gm_runtime_craft semantics (3x3 recipes need the
     * open table). Runs on the SERVER player (authoritative inventory; slot
     * changes sync to the client within a tick), fires once, fails silently. */
    private void rlCraft(Minecraft mc, int which) {
        if (which < 0 || which >= RL_CRAFT_IN.length) return;
        if (RL_CRAFT_TABLE[which] && rlContainer != 1) return;
        final MinecraftServer srv = mc.getIntegratedServer();
        if (srv == null) return;
        final int wi = which;
        srv.addScheduledTask(new Runnable() { public void run() {
            try {
                java.util.List<net.minecraft.entity.player.EntityPlayerMP> ps =
                    srv.getPlayerList().getPlayers();
                if (ps.isEmpty()) return;
                net.minecraft.entity.player.EntityPlayerMP pl = ps.get(0);
                int[] need = RL_CRAFT_IN[wi];
                for (int j = 0; j < need.length; j += 2)
                    if (rlCountItem(pl.inventory, need[j]) < need[j + 1]) return;
                for (int j = 0; j < need.length; j += 2)
                    rlConsumeItem(pl.inventory, need[j], need[j + 1]);
                net.minecraft.item.ItemStack out = new net.minecraft.item.ItemStack(
                    net.minecraft.item.Item.getItemById(RL_CRAFT_OUT[wi][0]),
                    RL_CRAFT_OUT[wi][1]);
                if (!pl.inventory.addItemStackToInventory(out) && !out.isEmpty())
                    pl.dropItem(out, false);
                pl.inventoryContainer.detectAndSendChanges();
            } catch (Throwable t) {
                System.out.println("[qrl] craft " + wi + " failed: " + t);
            }
        }});
    }

    /** Smelt primitive on the REAL TileEntityFurnace at rlContainerPos
     * (rl_do_smelt semantics, same order): 1) extract the whole output slot
     * into the inventory (partial move - only what the inventory accepts
     * leaves the furnace), 2) move the first inventory stack of iron ore
     * (15) into the input slot (merge-capped at 64), 3) if the fuel slot is
     * empty, insert exactly ONE coal (263). Server thread, fires once,
     * silent failure. The vanilla furnace handles cook progress and the
     * 61<->62 lit block flips itself. */
    private void rlSmelt(Minecraft mc) {
        if (rlContainer != 2 || rlContainerPos == null) return;
        final MinecraftServer srv = mc.getIntegratedServer();
        if (srv == null) return;
        final BlockPos fp = rlContainerPos;
        srv.addScheduledTask(new Runnable() { public void run() {
            try {
                java.util.List<net.minecraft.entity.player.EntityPlayerMP> ps =
                    srv.getPlayerList().getPlayers();
                if (ps.isEmpty()) return;
                net.minecraft.entity.player.EntityPlayerMP pl = ps.get(0);
                net.minecraft.tileentity.TileEntity te = pl.world.getTileEntity(fp);
                if (!(te instanceof net.minecraft.tileentity.TileEntityFurnace)) return;
                net.minecraft.tileentity.TileEntityFurnace fu =
                    (net.minecraft.tileentity.TileEntityFurnace) te;
                // 1. collect output (slot 2), partial-move
                net.minecraft.item.ItemStack out = fu.getStackInSlot(2);
                if (!out.isEmpty()) {
                    net.minecraft.item.ItemStack take = out.copy();
                    pl.inventory.addItemStackToInventory(take);
                    int moved = out.getCount() - take.getCount();
                    if (moved > 0) out.shrink(moved);
                }
                // 2. load iron ore: first main-inventory stack of id 15
                for (int i = 0; i < pl.inventory.mainInventory.size(); i++) {
                    net.minecraft.item.ItemStack s = pl.inventory.mainInventory.get(i);
                    if (s.isEmpty() || stackId(s) != 15) continue;
                    net.minecraft.item.ItemStack in = fu.getStackInSlot(0);
                    int accept;
                    if (in.isEmpty()) accept = Math.min(s.getCount(), 64);
                    else if (stackId(in) == 15 && in.getMetadata() == s.getMetadata())
                        accept = Math.min(s.getCount(), 64 - in.getCount());
                    else accept = 0;
                    if (accept > 0) {
                        if (in.isEmpty())
                            fu.setInventorySlotContents(0, new net.minecraft.item.ItemStack(
                                net.minecraft.item.Item.getItemById(15), accept, s.getMetadata()));
                        else in.grow(accept);
                        s.shrink(accept);
                    }
                    break;
                }
                // 3. one coal into the empty fuel slot (coal-frugal)
                if (fu.getStackInSlot(1).isEmpty()
                        && rlCountItem(pl.inventory, 263) >= 1) {
                    rlConsumeItem(pl.inventory, 263, 1);
                    fu.setInventorySlotContents(1, new net.minecraft.item.ItemStack(
                        net.minecraft.item.Item.getItemById(263), 1));
                }
                fu.markDirty();
                pl.inventoryContainer.detectAndSendChanges();
            } catch (Throwable t) {
                System.out.println("[qrl] smelt failed: " + t);
            }
        }});
    }

    /** Nearest-coal-ore list, rl_emit_obs geometry: horizontal radius 16
     * around the player block, y band [feet-24, feet+40], nearest 32 by
     * squared distance from the exact feet position to block centers. */
    private JsonArray rlCoalScan(Minecraft mc) {
        JsonArray arr = new JsonArray();
        EntityPlayerSP p = mc.player;
        int pwx = (int) Math.floor(p.posX), pwy = (int) Math.floor(p.posY),
            pwz = (int) Math.floor(p.posZ);
        int y0 = Math.max(0, pwy - 24), y1 = Math.min(255, pwy + 40);
        final int R = 16;
        java.util.ArrayList<int[]> found = new java.util.ArrayList<int[]>();
        BlockPos.MutableBlockPos mp = new BlockPos.MutableBlockPos();
        for (int dx = -R; dx <= R; ++dx)
            for (int dz = -R; dz <= R; ++dz)
                for (int y = y0; y <= y1; ++y) {
                    mp.setPos(pwx + dx, y, pwz + dz);
                    if (mc.world.getBlockState(mp).getBlock()
                        != net.minecraft.init.Blocks.COAL_ORE) continue;
                    found.add(new int[]{pwx + dx, y, pwz + dz});
                }
        final double px = p.posX, py = p.posY, pz = p.posZ;
        found.sort((a, b) -> {
            double da = sq(a[0] + 0.5 - px) + sq(a[1] + 0.5 - py) + sq(a[2] + 0.5 - pz);
            double db = sq(b[0] + 0.5 - px) + sq(b[1] + 0.5 - py) + sq(b[2] + 0.5 - pz);
            return Double.compare(da, db);
        });
        for (int i = 0; i < Math.min(32, found.size()); i++) {
            JsonArray c = new JsonArray();
            c.add(new com.google.gson.JsonPrimitive(found.get(i)[0]));
            c.add(new com.google.gson.JsonPrimitive(found.get(i)[1]));
            c.add(new com.google.gson.JsonPrimitive(found.get(i)[2]));
            arr.add(c);
        }
        return arr;
    }

    private static double sq(double v) { return v * v; }

    private void clearKeys(Minecraft mc) {
        key(mc.gameSettings.keyBindForward, false);
        key(mc.gameSettings.keyBindBack, false);
        key(mc.gameSettings.keyBindLeft, false);
        key(mc.gameSettings.keyBindRight, false);
        key(mc.gameSettings.keyBindJump, false);
        key(mc.gameSettings.keyBindSneak, false);
        key(mc.gameSettings.keyBindAttack, false);
        key(mc.gameSettings.keyBindUseItem, false);
    }

    // 1.11.2 MCP: setKeyBindState takes a key CODE, not a KeyBinding
    private static void key(KeyBinding kb, boolean down) {
        KeyBinding.setKeyBindState(kb.getKeyCode(), down);
    }

    // headless world launch, settings from Python (folder per seed for reuse/determinism)
    private void launchWorld(Minecraft mc, JsonObject w) {
        try {
            long seed = w.has("seed") ? w.get("seed").getAsLong() : 0L;
            GameType gt = "creative".equalsIgnoreCase(opt(w, "mode")) ? GameType.CREATIVE : GameType.SURVIVAL;
            WorldType wt = "flat".equalsIgnoreCase(opt(w, "type")) ? WorldType.FLAT : WorldType.DEFAULT;
            boolean structures = !w.has("structures") || w.get("structures").getAsBoolean();
            WorldSettings ws = new WorldSettings(seed, gt, structures, false, wt).enableCommands();
            String folder = "qrl_" + seed + ("flat".equalsIgnoreCase(opt(w, "type")) ? "_flat" : "");
            initStartNanos = System.nanoTime();
            mc.launchIntegratedServer(folder, folder, ws);
            System.out.println("[qrl] launching world " + folder + " seed=" + seed + " mode=" + gt + " type=" + wt);
        } catch (Throwable t) {
            System.out.println("[qrl] launchWorld failed: " + t);
        }
    }

    // qrl_launch.json: written by mc_cli.py (repo root) from fast.yaml (agent) or vanilla.yaml (human play). Resolution order:
    // env QRL_LAUNCH_JSON > ./qrl_launch.json (cwd = Minecraft/run under gradle) > absolute fallback.
    private static JsonObject loadLaunchCfg() {
        String[] paths = { System.getenv("QRL_LAUNCH_JSON"), "qrl_launch.json",
            "/home/infatoshi/dev/minecraft/mc-1.11.2-env/java/Minecraft/run/qrl_launch.json" };
        for (String p : paths) {
            if (p == null || p.isEmpty()) continue;
            try {
                byte[] b = java.nio.file.Files.readAllBytes(java.nio.file.Paths.get(p));
                JsonObject o = new JsonParser().parse(new String(b, StandardCharsets.UTF_8)).getAsJsonObject();
                System.out.println("[qrl] launch config " + p + ": " + o);
                return o;
            } catch (Exception ig) { /* try next */ }
        }
        return null;
    }

    // Apply the non-world half of qrl_launch.json once the player exists: chat visibility,
    // hide-gui, gamerules, time, weather. Gamerules/time/weather go through the integrated
    // server's command manager (same path as the qrl "runcmds" op).
    private void applyLaunchSettings(Minecraft mc) {
        JsonObject c = launchCfg;
        try {
            if (c.has("chat") && !c.get("chat").getAsBoolean())
                mc.gameSettings.chatVisibility = net.minecraft.entity.player.EntityPlayer.EnumChatVisibility.HIDDEN;
            if (c.has("hide_gui") && c.get("hide_gui").getAsBoolean())
                mc.gameSettings.hideGUI = true;
            MinecraftServer srv = mc.getIntegratedServer();
            ArrayList<String> cmds = new ArrayList<String>();
            JsonObject w = c.has("world") ? c.getAsJsonObject("world") : new JsonObject();
            if (w.has("gamerules")) {
                for (java.util.Map.Entry<String, com.google.gson.JsonElement> en
                        : w.getAsJsonObject("gamerules").entrySet())
                    cmds.add("gamerule " + en.getKey() + " " + en.getValue().getAsString());
            }
            if (w.has("time") && w.get("time").getAsLong() >= 0)
                cmds.add("time set " + w.get("time").getAsLong());
            if ("clear".equalsIgnoreCase(opt(w, "weather")))
                cmds.add("weather clear");
            int fail = 0;
            for (String cmd : cmds) {
                try { srv.getCommandManager().executeCommand(srv, cmd); }
                catch (Throwable t) { fail++; }
            }
            System.out.println("[qrl] launch settings applied: chat="
                + (!c.has("chat") || c.get("chat").getAsBoolean()) + " cmds=" + cmds
                + (fail > 0 ? " (" + fail + " failed)" : ""));
        } catch (Throwable t) {
            System.out.println("[qrl] applyLaunchSettings failed: " + t);
        }
    }

    private String stats(Minecraft mc) {
        JsonObject o = new JsonObject();
        o.addProperty("ok", true);
        o.addProperty("fps", Minecraft.getDebugFPS());
        o.addProperty("init_ms", lastInitMs);
        net.minecraft.server.MinecraftServer s = mc.getIntegratedServer();
        if (s != null && s.tickTimeArray != null) {
            double sum = 0, max = 0; int n = 0;
            for (long v : s.tickTimeArray) { if (v > 0) { sum += v; if (v > max) max = v; n++; } }
            double meanMs = n > 0 ? (sum / n) / 1.0e6 : 0;
            o.addProperty("tick_ms_mean", meanMs);
            o.addProperty("tick_ms_max", max / 1.0e6);
            o.addProperty("tick_budget_ms", 50.0);
            o.addProperty("tick_headroom_ms", 50.0 - meanMs);
            o.addProperty("max_tps_uncapped", meanMs > 0 ? 1000.0 / meanMs : 0);
        }
        o.addProperty("entities", mc.world != null ? mc.world.loadedEntityList.size() : 0);
        o.addProperty("server_tick_length", TimeHelper.serverTickLength);
        o.addProperty("num_ticks", TimeHelper.SyncManager.numTicks);
        return o.toString();
    }

    private static String opt(JsonObject w, String k) { return w.has(k) ? w.get(k).getAsString() : ""; }

    private static String loading() {
        JsonObject o = new JsonObject(); o.addProperty("ok", false); o.addProperty("loading", true);
        return o.toString();
    }

    /**
     * Dump exact render camera + graphics settings for instrumented goldens.
     * Optional action.file: also write the JSON to that path (atomic-ish overwrite).
     * Effective FOV mirrors EntityRenderer.getFOVModifier(useFOVSetting=true) at noon
     * overworld with no night-vision/death/water (spectator flying multiplies *1.1).
     */
    private String camera(Minecraft mc, JsonObject action) {
        EntityPlayerSP p = mc.player;
        Entity view = mc.getRenderViewEntity() != null ? mc.getRenderViewEntity() : p;
        JsonObject o = new JsonObject();
        o.addProperty("ok", true);
        o.addProperty("schema", "qrl.camera.v1");
        o.addProperty("ts_ms", System.currentTimeMillis());

        // Feet (entity pos) vs eye (camera). Hard-scene bug class: pose.json used feet.
        double feetX = view.posX, feetY = view.posY, feetZ = view.posZ;
        float eyeH = view.getEyeHeight();
        double eyeX = feetX, eyeY = feetY + eyeH, eyeZ = feetZ;
        float yaw = view.rotationYaw, pitch = view.rotationPitch;
        o.addProperty("feet_x", feetX); o.addProperty("feet_y", feetY); o.addProperty("feet_z", feetZ);
        o.addProperty("eye_height", eyeH);
        o.addProperty("eye_x", eyeX); o.addProperty("eye_y", eyeY); o.addProperty("eye_z", eyeZ);
        o.addProperty("yaw", yaw); o.addProperty("pitch", pitch);
        // Magma convention: magma_yaw = 180 - mc_yaw, magma_pitch = -mc_pitch
        // Normalize yaw to (-180, 180] so -180 MC maps to 0 not 360.
        double crYaw = 180.0 - yaw;
        while (crYaw > 180.0) crYaw -= 360.0;
        while (crYaw <= -180.0) crYaw += 360.0;
        o.addProperty("magma_yaw_deg", crYaw);
        o.addProperty("magma_pitch_deg", -pitch);

        // FOV chain: GameSettings.fovSetting * AbstractClientPlayer.getFovModifier()
        float fovSetting = mc.gameSettings.fovSetting;
        float fovMod = 1.0F;
        boolean flying = false;
        try {
            if (view instanceof net.minecraft.client.entity.AbstractClientPlayer) {
                net.minecraft.client.entity.AbstractClientPlayer acp =
                    (net.minecraft.client.entity.AbstractClientPlayer) view;
                flying = acp.capabilities.isFlying;
                fovMod = acp.getFovModifier();
            } else if (p != null) {
                flying = p.capabilities.isFlying;
                if (flying) fovMod *= 1.1F;
            }
        } catch (Throwable ig) {
            if (p != null && p.capabilities.isFlying) { flying = true; fovMod = 1.1F; }
        }
        float effectiveFov = fovSetting * fovMod;
        // Water FOV shrink (EntityRenderer.getFOVModifier)
        try {
            net.minecraft.block.state.IBlockState bs =
                net.minecraft.client.renderer.ActiveRenderInfo.getBlockStateAtEntityViewpoint(
                    mc.world, view, 1.0F);
            if (bs != null && bs.getMaterial() == net.minecraft.block.material.Material.WATER)
                effectiveFov = effectiveFov * 60.0F / 70.0F;
        } catch (Throwable ig) {}
        o.addProperty("fov_setting", fovSetting);
        o.addProperty("fov_modifier", fovMod);
        o.addProperty("fov_effective", effectiveFov);
        o.addProperty("is_flying", flying);
        o.addProperty("no_gravity", view.hasNoGravity());
        o.addProperty("gamemode", mc.playerController != null
            ? mc.playerController.getCurrentGameType().getName() : "?");

        o.addProperty("texture_animations_pinned", QLaunch.PIN_TEXTURE_ANIMATIONS);
        if (QLaunch.PIN_TEXTURE_ANIMATIONS) {
            o.addProperty("fire_layer_0_physical_frame", 0);
            o.addProperty("fire_layer_1_physical_frame", 0);
        }
        boolean bossFog = false;
        try { bossFog = mc.ingameGUI.getBossOverlay().shouldCreateFog(); }
        catch (Throwable ig) {}
        o.addProperty("boss_fog", bossFog);
        try {
            Class<?> erc = net.minecraft.client.renderer.EntityRenderer.class;
            String[] names = {"fogColorRed", "fogColorGreen", "fogColorBlue"};
            JsonArray fog = new JsonArray();
            JsonArray fog8 = new JsonArray();
            for (String name : names) {
                java.lang.reflect.Field field = erc.getDeclaredField(name);
                field.setAccessible(true);
                float value = field.getFloat(mc.entityRenderer);
                fog.add(new com.google.gson.JsonPrimitive(value));
                fog8.add(new com.google.gson.JsonPrimitive(Math.max(0, Math.min(255,
                    Math.round(value * 255.0F)))));
            }
            o.add("fog_rgb", fog);
            o.add("fog_rgb8", fog8);
            for (String name : new String[] {"fogColor1", "fogColor2"}) {
                java.lang.reflect.Field field = erc.getDeclaredField(name);
                field.setAccessible(true);
                o.addProperty(name, field.getFloat(mc.entityRenderer));
            }
            // LWJGL 2's glGetFloat wrapper requires room for the largest
            // supported return even when this pname produces one scalar.
            java.nio.FloatBuffer fogParam = org.lwjgl.BufferUtils.createFloatBuffer(16);
            org.lwjgl.opengl.GL11.glGetFloat(org.lwjgl.opengl.GL11.GL_FOG_START, fogParam);
            o.addProperty("fog_start", fogParam.get(0));
            fogParam.clear();
            org.lwjgl.opengl.GL11.glGetFloat(org.lwjgl.opengl.GL11.GL_FOG_END, fogParam);
            o.addProperty("fog_end", fogParam.get(0));
            o.addProperty("fog_mode", org.lwjgl.opengl.GL11.glGetInteger(
                org.lwjgl.opengl.GL11.GL_FOG_MODE));
            boolean nvFogDistance = org.lwjgl.opengl.GLContext.getCapabilities()
                .GL_NV_fog_distance;
            o.addProperty("nv_fog_distance", nvFogDistance);
            if (nvFogDistance) {
                // GL_FOG_DISTANCE_MODE_NV / GL_EYE_RADIAL_NV. Minecraft sets
                // this in EntityRenderer.setupFog for the terrain pass.
                o.addProperty("fog_distance_mode_nv",
                    org.lwjgl.opengl.GL11.glGetInteger(34138));
            }
        } catch (Throwable t) {
            o.addProperty("fog_error", String.valueOf(t));
        }

        // Projection params (EntityRenderer.setupCameraTransform terrain path)
        int rd = mc.gameSettings.renderDistanceChunks;
        float farPlane = (float)(rd * 16);
        float znear = 0.05F;
        float zfar = farPlane * (float)Math.sqrt(2.0); // farPlaneDistance * SQRT_2
        int dw = mc.displayWidth, dh = mc.displayHeight;
        o.addProperty("display_w", dw); o.addProperty("display_h", dh);
        o.addProperty("aspect", dh > 0 ? (float)dw / (float)dh : 0f);
        o.addProperty("render_distance_chunks", rd);
        int pcx = ((int)Math.floor(view.posX)) >> 4;
        int pcz = ((int)Math.floor(view.posZ)) >> 4;
        int clientChunks = 0;
        try {
            net.minecraft.world.chunk.IChunkProvider clientProvider =
                mc.world.getChunkProvider();
            for (int cz = pcz - rd; cz <= pcz + rd; ++cz) {
                for (int cx = pcx - rd; cx <= pcx + rd; ++cx) {
                    if (clientProvider.getLoadedChunk(cx, cz) != null) {
                        ++clientChunks;
                    }
                }
            }
        } catch (Throwable ig) {
            clientChunks = -1;
        }
        o.addProperty("client_chunks_loaded_radius", clientChunks);
        o.addProperty("client_chunk_window_slots", (2 * rd + 1) * (2 * rd + 1));
        boolean renderChunksReady = false;
        try {
            renderChunksReady = mc.renderGlobal != null
                && mc.renderGlobal.hasNoChunkUpdates();
            o.addProperty("render_debug", mc.renderGlobal != null
                ? mc.renderGlobal.getDebugInfoRenders() : "null");
        } catch (Throwable ig) {}
        o.addProperty("render_chunks_ready", renderChunksReady);
        o.addProperty("far_plane", farPlane);
        o.addProperty("znear", znear);
        o.addProperty("zfar", zfar);

        // Graphics options snapshot (the knobs that change goldens)
        JsonObject opt = new JsonObject();
        opt.addProperty("fov", fovSetting);
        opt.addProperty("gamma", mc.gameSettings.gammaSetting);
        opt.addProperty("renderDistance", rd);
        opt.addProperty("fancyGraphics", mc.gameSettings.fancyGraphics);
        opt.addProperty("ao", mc.gameSettings.ambientOcclusion);
        opt.addProperty("mipmapLevels", mc.gameSettings.mipmapLevels);
        opt.addProperty("renderClouds", mc.gameSettings.clouds);
        opt.addProperty("particles", mc.gameSettings.particleSetting);
        opt.addProperty("anaglyph", mc.gameSettings.anaglyph);
        opt.addProperty("entityShadows", mc.gameSettings.entityShadows);
        o.add("options", opt);

        // World identity
        JsonObject world = new JsonObject();
        try {
            int dim = mc.world.provider.getDimensionType().getId();
            net.minecraft.world.WorldServer serverWorld = null;
            try {
                if (mc.getIntegratedServer() != null)
                    serverWorld = mc.getIntegratedServer().worldServerForDimension(dim);
            } catch (Throwable ig) {}
            // WorldClient#getSeed() is zero in an integrated client even when the
            // authoritative WorldServer uses another seed.  Capture provenance must
            // identify the actual generator seed, not the client placeholder.
            long worldSeed = serverWorld != null ? serverWorld.getSeed() : mc.world.getSeed();
            world.addProperty("seed", worldSeed);
            world.addProperty("dim", dim);
            world.addProperty("world_time", mc.world.getWorldTime());
            world.addProperty("total_time", mc.world.getTotalWorldTime());
            world.addProperty("raining", mc.world.isRaining());
            BlockPos sp = mc.world.getSpawnPoint();
            world.addProperty("spawn_x", sp.getX());
            world.addProperty("spawn_y", sp.getY());
            world.addProperty("spawn_z", sp.getZ());
            world.addProperty("spawn_cx", sp.getX() >> 4);
            world.addProperty("spawn_cz", sp.getZ() >> 4);
            // Save folder name if integrated
            try {
                if (mc.isSingleplayer() && mc.getIntegratedServer() != null)
                    world.addProperty("save_folder",
                        mc.getIntegratedServer().getFolderName());
            } catch (Throwable ig) {}
            // Cheap world fingerprint: seed + spawn + time (client chunk count is opaque)
            int nChunks = -1;
            try {
                if (serverWorld != null)
                    nChunks = serverWorld.getChunkProvider().getLoadedChunkCount();
            } catch (Throwable ig) {}
            world.addProperty("loaded_chunks", nChunks);
            world.addProperty("fingerprint",
                String.format("seed=%d;spawn=%d,%d,%d;chunks=%d;time=%d",
                    worldSeed, sp.getX(), sp.getY(), sp.getZ(),
                    nChunks, mc.world.getWorldTime()));
        } catch (Throwable t) {
            world.addProperty("error", String.valueOf(t));
        }
        o.add("world", world);

        // Optional path write for atomic golden sidecar
        if (action != null && action.has("file")) {
            String path = action.get("file").getAsString();
            try {
                java.nio.file.Path pth = java.nio.file.Paths.get(path);
                if (pth.getParent() != null)
                    java.nio.file.Files.createDirectories(pth.getParent());
                java.nio.file.Files.write(pth, o.toString().getBytes(StandardCharsets.UTF_8));
                o.addProperty("wrote", path);
            } catch (Throwable t) {
                o.addProperty("write_error", String.valueOf(t));
            }
        }
        return o.toString();
    }

    // Timed response handoff. The socket thread does incoming.put(r) then
    // r.resp.poll(120s); put() returns the instant the tick thread TAKES the
    // request, so a handler that answers in sub-microseconds can offer() before
    // the socket thread parks in poll() - a plain offer drops the response and
    // the client eats the full 120s timeout. The timed offer waits for the
    // receiver (recstop lost this race reliably; slower handlers never did).
    private static void reply(Req r, String s) {
        try { r.resp.offer(s, 5, java.util.concurrent.TimeUnit.SECONDS); }
        catch (InterruptedException ie) { Thread.currentThread().interrupt(); }
    }

    // One human-play tape line: the tick's inputs (as MC consumed them), the resulting
    // player physics state, and nearby entities. Runs at ClientTick Phase.END so the
    // state is post-tick; movementInput still holds the values this tick used.
    private void recordTick(Minecraft mc) {
        EntityPlayerSP p = mc.player;
        StringBuilder b = new StringBuilder(512);
        b.append("{\"t\":").append(recTick++);
        // inputs
        float mf = 0.0F, ms = 0.0F; boolean ji = false, sn = false;
        if (p.movementInput != null) {
            mf = p.movementInput.moveForward; ms = p.movementInput.moveStrafe;
            ji = p.movementInput.jump; sn = p.movementInput.sneak;
        }
        b.append(",\"in\":{\"f\":").append(mf).append(",\"s\":").append(ms)
         .append(",\"jump\":").append(ji ? 1 : 0)
         .append(",\"sneak\":").append(sn ? 1 : 0)
         .append(",\"sprint\":").append(p.isSprinting() ? 1 : 0)
         .append(",\"atk\":").append(mc.gameSettings.keyBindAttack.isKeyDown() ? 1 : 0)
         .append(",\"use\":").append(mc.gameSettings.keyBindUseItem.isKeyDown() ? 1 : 0)
         .append(",\"hb\":").append(p.inventory.currentItem).append("}");
        // player physics state (post-tick); doubles round-trip via Double.toString
        b.append(",\"x\":").append(p.posX).append(",\"y\":").append(p.posY)
         .append(",\"z\":").append(p.posZ)
         .append(",\"yaw\":").append(p.rotationYaw).append(",\"pitch\":").append(p.rotationPitch)
         .append(",\"vx\":").append(p.motionX).append(",\"vy\":").append(p.motionY)
         .append(",\"vz\":").append(p.motionZ)
         .append(",\"og\":").append(p.onGround ? 1 : 0)
         .append(",\"hp\":").append(p.getHealth())
         .append(",\"food\":").append(p.getFoodStats().getFoodLevel())
         .append(",\"fall\":").append(p.fallDistance)
         .append(",\"dim\":").append(p.dimension)
         .append(",\"wt\":").append(mc.world.getWorldTime());
        // weather: rain/thunder strength drive vanilla sky/fog/light darkening
        // (World.getSkyColor 1-rain*5/16 etc.). Only emitted when active so
        // clear-weather tapes stay byte-identical to the old format.
        float rain = mc.world.getRainStrength(1.0F);
        float thun = mc.world.getThunderStrength(1.0F);
        if (rain > 0.0F) b.append(",\"rain\":").append(rain);
        if (thun > 0.0F) b.append(",\"thunder\":").append(thun);
        // player visual state the HUD/first-person overlays consume:
        // air -> bubble row (GuiIngameGUI), xp -> bar+level, hurtTime ->
        // red vignette + hurt camera tilt (attackedAtYaw = tilt direction),
        // fire -> first-person flame overlay. Conditional like rain so
        // steady-state ticks stay compact.
        if (p.getAir() < 300) b.append(",\"air\":").append(p.getAir());
        b.append(",\"xpl\":").append(p.experienceLevel)
         .append(",\"xpp\":").append(p.experience);
        if (p.hurtTime > 0) b.append(",\"hurt\":").append(p.hurtTime)
                             .append(",\"maxhurt\":").append(p.maxHurtTime)
                             .append(",\"hurtyaw\":").append(p.attackedAtYaw);
        if (p.isBurning()) b.append(",\"fire\":1");
        if (recVelocityPending) {
            b.append(",\"pvel\":[").append(recVelocityX)
             .append(",").append(recVelocityY)
             .append(",").append(recVelocityZ).append("]");
            recVelocityPending = false;
        }
        if (recPositionPending) {
            b.append(",\"ppos\":[").append(recPositionX)
             .append(",").append(recPositionY)
             .append(",").append(recPositionZ)
             .append(",").append(recPositionYaw)
             .append(",").append(recPositionPitch)
             .append(",").append(recPositionVx)
             .append(",").append(recPositionVy)
             .append(",").append(recPositionVz).append("]");
            recPositionPending = false;
        }
        /* During cross-dimension terrain download the client player exists but
         * is not yet in a loaded chunk, so WorldClient deliberately does not
         * tick it. Replay must preserve that interval rather than applying
         * gravity at the temporary spawn or newly received portal position. */
        boolean forcedLoading = p.dimension != recLastDimension || !p.addedToChunk
            || mc.currentScreen instanceof net.minecraft.client.gui.GuiDownloadTerrain;
        if (forcedLoading) recPlayerLoading = true;
        else if (recPlayerLoading && p.ticksExisted != recLastPlayerTicksExisted)
            recPlayerLoading = false;
        if (recPlayerLoading)
            b.append(",\"loading\":1");
        recLastPlayerTicksExisted = p.ticksExisted;
        recLastDimension = p.dimension;
        if (p.getAbsorptionAmount() > 0.0F)
            b.append(",\"absorb\":").append(p.getAbsorptionAmount());
        float sat = p.getFoodStats().getSaturationLevel();
        if (sat > 0.0F) b.append(",\"sat\":").append(sat);
        // portal screen-warp + overlay ramp (0.0125/tick, full-screen effect)
        if (p.timeInPortal > 0.0F) {
            b.append(",\"portal\":").append(p.timeInPortal);
        }
        // world portal-pane animation phase: the pane is visible whenever a
        // portal is in VIEW, not only while standing in one, so tape the
        // sprite frame every tick (frameCounter advances 1/tick, 32 frames)
        try {
            net.minecraft.client.renderer.texture.TextureAtlasSprite sprite =
                mc.getBlockRendererDispatcher().getBlockModelShapes().getTexture(
                    net.minecraft.init.Blocks.PORTAL.getDefaultState());
            b.append(",\"portal_frame\":").append(
                reflectedInt(sprite,"frameCounter",recPortalFrameField));
            b.append(",\"portal_phase\":").append(
                reflectedInt(mc.entityRenderer,"rendererUpdateCount",recRendererPhaseField));
        } catch (Throwable ig) {}
        // crosshair attack-indicator meter (resets on swings/hb switches the
        // tape can't always derive)
        float cd = p.getCooledAttackStrength(1.0F);
        if (cd < 1.0F) b.append(",\"cd\":").append(cd);
        // active potions retint the whole frame (night vision/blindness/nausea)
        java.util.Collection<net.minecraft.potion.PotionEffect> pots = p.getActivePotionEffects();
        if (!pots.isEmpty()) {
            b.append(",\"pots\":[");
            int pn = 0;
            for (net.minecraft.potion.PotionEffect pe : pots) {
                if (pn++ > 0) b.append(",");
                b.append("[").append(net.minecraft.potion.Potion.getIdFromPotion(pe.getPotion()))
                 .append(",").append(pe.getAmplifier())
                 .append(",").append(pe.getDuration()).append("]");
            }
            b.append("]");
        }
        // full inventory whenever it changed (slots 0-40: main 36, armor 4,
        // offhand): the ground truth that retires worldpatch set_inventory
        // and feeds hotbar icons/counts + GUI slot rendering. Delta-encoded
        // as a full dump on change; absent when unchanged.
        try {
            StringBuilder inv = new StringBuilder(128);
            inv.append("[");
            for (int i = 0; i < 41; ++i) {
                net.minecraft.item.ItemStack st =
                    i < 36 ? p.inventory.mainInventory.get(i)
                  : i < 40 ? p.inventory.armorInventory.get(i - 36)
                           : p.inventory.offHandInventory.get(0);
                if (i > 0) inv.append(",");
                if (st.isEmpty()) inv.append("0");
                else inv.append("[").append(net.minecraft.item.Item.getIdFromItem(st.getItem()))
                        .append(",").append(st.getMetadata())
                        .append(",").append(st.getCount()).append("]");
            }
            inv.append("]");
            String cur = inv.toString();
            if (!cur.equals(recLastInv)) {
                b.append(",\"inv\":").append(cur);
                recLastInv = cur;
            }
        } catch (Throwable ig) {}
        // open GUI screen: replay renders magma's own container screens from
        // this (screen.c). Only emitted while a screen is open.
        if (mc.currentScreen != null) {
            String g = mc.currentScreen.getClass().getSimpleName();
            b.append(",\"gui\":\"").append(g).append("\"");
            try {
                net.minecraft.client.gui.ScaledResolution sr =
                    new net.minecraft.client.gui.ScaledResolution(mc);
                int mx = org.lwjgl.input.Mouse.getX() * sr.getScaledWidth() / mc.displayWidth;
                int my = sr.getScaledHeight() - org.lwjgl.input.Mouse.getY() * sr.getScaledHeight() / mc.displayHeight - 1;
                b.append(",\"gmx\":").append(mx).append(",\"gmy\":").append(my);
            } catch (Throwable ig) {}
            // Exact post-tick contents for the visible container slots and
            // carried cursor stack. Player inventory has its own delta dump
            // above; these rows also cover craft/result/furnace slots that do
            // not live in InventoryPlayer. Emit every open-GUI tick so replay
            // state cannot survive a server slot update or screen transition.
            if (mc.currentScreen instanceof net.minecraft.client.gui.inventory.GuiContainer) {
                try {
                    net.minecraft.client.gui.inventory.GuiContainer gc =
                        (net.minecraft.client.gui.inventory.GuiContainer)mc.currentScreen;
                    b.append(",\"gslots\":[");
                    for (int i = 0; i < gc.inventorySlots.inventorySlots.size(); ++i) {
                        if (i > 0) b.append(",");
                        net.minecraft.item.ItemStack st =
                            gc.inventorySlots.inventorySlots.get(i).getStack();
                        if (st.isEmpty()) b.append("0");
                        else b.append("[").append(net.minecraft.item.Item.getIdFromItem(st.getItem()))
                                .append(",").append(st.getMetadata())
                                .append(",").append(st.getCount()).append("]");
                    }
                    b.append("]");
                    net.minecraft.item.ItemStack cur = p.inventory.getItemStack();
                    if (cur.isEmpty()) b.append(",\"gcur\":0");
                    else b.append(",\"gcur\":[")
                            .append(net.minecraft.item.Item.getIdFromItem(cur.getItem()))
                            .append(",").append(cur.getMetadata())
                            .append(",").append(cur.getCount()).append("]");
                    if ("GuiFurnace".equals(g) && !gc.inventorySlots.inventorySlots.isEmpty()) {
                        net.minecraft.inventory.IInventory fi =
                            gc.inventorySlots.inventorySlots.get(0).inventory;
                        b.append(",\"gprop\":[").append(fi.getField(0))
                         .append(",").append(fi.getField(1))
                         .append(",").append(fi.getField(2))
                         .append(",").append(fi.getField(3)).append("]");
                    }
                } catch (Throwable ig) {}
            }
        }
        // sparse pixel golden: real framebuffer every recFrameEvery ticks. The grab
        // happens at tick END; the framebuffer's last content belongs to the previous
        // tick with partialTicks interpolation, so we first force a fresh world render
        // at partialTicks=1.0 (exactly this tick's post-tick state, zero interpolation)
        // into the FBO. That makes the pixel goldens tick-locked even while moving
        // (OPEN_DIVERGENCES #7); "tb":1 marks rows whose frame is tick-boundary.
        if (recFrameEvery > 0 && (recTick - 1) % recFrameEvery == 0 && recFramesDir != null) {
            try {
                int fw = mc.displayWidth, fh = mc.displayHeight;
                String fp = String.format("%s/f_%06d.png", recFramesDir, recTick - 1);
                boolean tb = false, hud = false;
                try {
                    net.minecraft.client.shader.Framebuffer fb = mc.getFramebuffer();
                    fb.bindFramebuffer(true);
                    mc.entityRenderer.renderWorld(1.0F, System.nanoTime());
                    // the world-only re-render wipes the GUI layer from the FBO;
                    // redraw the HUD so the golden shows what the player sees
                    try {
                        mc.entityRenderer.setupOverlayRendering();
                        mc.ingameGUI.renderGameOverlay(1.0F);
                        hud = true;
                        // an open GUI screen (inventory/crafting/furnace) is part
                        // of what the player sees; draw it over the HUD like
                        // EntityRenderer.updateCameraAndRender does, at the real
                        // scaled-resolution mouse position.
                        if (mc.currentScreen != null) {
                            net.minecraft.client.gui.ScaledResolution sr =
                                new net.minecraft.client.gui.ScaledResolution(mc);
                            int mx = org.lwjgl.input.Mouse.getX() * sr.getScaledWidth() / mc.displayWidth;
                            int my = sr.getScaledHeight() - org.lwjgl.input.Mouse.getY() * sr.getScaledHeight() / mc.displayHeight - 1;
                            mc.currentScreen.drawScreen(mx, my, 1.0F);
                        }
                    } catch (Throwable ig2) {}
                    fb.unbindFramebuffer();
                    tb = true;
                } catch (Throwable ig) {} // fall back to the last interpolated frame
                java.awt.image.BufferedImage img =
                    net.minecraft.util.ScreenShotHelper.createScreenshot(fw, fh, mc.getFramebuffer());
                javax.imageio.ImageIO.write(img, "png", new java.io.File(fp));
                b.append(",\"frame\":\"").append(fp).append("\"");
                if (tb) b.append(",\"tb\":1");
                if (hud) b.append(",\"hud\":1");
                // geometry oracle: the golden re-render above just ran every
                // Render's setRotationAngles, so each mainModel's ModelRenderer
                // fields hold THIS frame's post-animation part poses. Dump
                // (rotationPoint, rotateAngle) per named part for boss/mob
                // entities. Caveat: a part rendered N times per frame with
                // mutated state (ModelDragon "neck" spine: 5 neck + 12 tail
                // segments) keeps only its LAST segment's pose.
                if (recGeomWriter != null && tb) {
                    for (net.minecraft.entity.Entity ge : mc.world.loadedEntityList) {
                        if (!(ge instanceof net.minecraft.entity.EntityLivingBase)
                                || ge instanceof net.minecraft.entity.player.EntityPlayer) continue;
                        double gdx = ge.posX - p.posX, gdy = ge.posY - p.posY, gdz = ge.posZ - p.posZ;
                        boolean gfar = ge instanceof net.minecraft.entity.boss.EntityDragon;
                        if (!gfar && gdx*gdx + gdy*gdy + gdz*gdz > 48.0*48.0) continue;
                        try {
                            net.minecraft.client.renderer.entity.Render<?> gr =
                                mc.getRenderManager().getEntityRenderObject(ge);
                            if (!(gr instanceof net.minecraft.client.renderer.entity.RenderLivingBase)) continue;
                            java.lang.reflect.Field gmf = net.minecraft.client.renderer.entity
                                .RenderLivingBase.class.getDeclaredField("mainModel");
                            gmf.setAccessible(true);
                            Object gmodel = gmf.get(gr);
                            StringBuilder gb = new StringBuilder(512);
                            gb.append("{\"t\":").append(recTick - 1)
                              .append(",\"eid\":").append(ge.getEntityId())
                              .append(",\"cls\":\"").append(ge.getClass().getSimpleName())
                              .append("\",\"parts\":{");
                            boolean gfirst = true;
                            for (java.lang.reflect.Field gf : gmodel.getClass().getDeclaredFields()) {
                                if (!net.minecraft.client.model.ModelRenderer.class
                                        .isAssignableFrom(gf.getType())) continue;
                                gf.setAccessible(true);
                                net.minecraft.client.model.ModelRenderer gp =
                                    (net.minecraft.client.model.ModelRenderer) gf.get(gmodel);
                                if (gp == null) continue;
                                if (!gfirst) gb.append(",");
                                gfirst = false;
                                gb.append("\"").append(gf.getName()).append("\":[")
                                  .append(gp.rotationPointX).append(",").append(gp.rotationPointY)
                                  .append(",").append(gp.rotationPointZ)
                                  .append(",").append(gp.rotateAngleX).append(",")
                                  .append(gp.rotateAngleY).append(",").append(gp.rotateAngleZ)
                                  .append("]");
                            }
                            gb.append("}}");
                            recGeomWriter.println(gb.toString());
                        } catch (Throwable ig3) {}
                    }
                    recGeomWriter.flush();
                }
            } catch (Throwable ig) {}
        }
        // nearby entities (client view = what renders), capped
        b.append(",\"ents\":[");
        int n = 0;
        double r2 = (double) REC_ENT_RADIUS * REC_ENT_RADIUS;
        for (net.minecraft.entity.Entity e : mc.world.loadedEntityList) {
            if (e == p || e instanceof net.minecraft.entity.player.EntityPlayer) continue;
            double dx = e.posX - p.posX, dy = e.posY - p.posY, dz = e.posZ - p.posZ;
            // route-critical End entities ignore the radius (crystals sit on
            // 70+ block pillars; the dragon orbits far outside 48 blocks).
            boolean farOk = e instanceof net.minecraft.entity.boss.EntityDragon
                         || e instanceof net.minecraft.entity.item.EntityEnderCrystal;
            if (!farOk && dx * dx + dy * dy + dz * dz > r2) continue;
            if (n > 0) b.append(",");
            float hp = -1.0F;
            if (e instanceof net.minecraft.entity.EntityLivingBase)
                hp = ((net.minecraft.entity.EntityLivingBase) e).getHealth();
            b.append("[").append(e.getEntityId()).append(",\"")
             .append(e.getClass().getSimpleName()).append("\",")
             .append(e.posX).append(",").append(e.posY).append(",").append(e.posZ)
             .append(",").append(e.rotationYaw).append(",").append(hp);
            // visual state the renderer consumes beyond pose (appended, so old
            // parsers reading fixed indices 0-6 keep working): head yaw, pitch,
            // swing/hurt/death timers; then per-type extras.
            if (e instanceof net.minecraft.entity.EntityLivingBase) {
                net.minecraft.entity.EntityLivingBase el = (net.minecraft.entity.EntityLivingBase) e;
                // flags bitfield: 1=burning 2=sneaking 4=invisible 8=child
                int fl = (el.isBurning() ? 1 : 0) | (el.isSneaking() ? 2 : 0)
                       | (el.isInvisible() ? 4 : 0) | (el.isChild() ? 8 : 0);
                b.append(",").append(el.rotationYawHead)
                 .append(",").append(el.rotationPitch)
                 .append(",").append(el.swingProgress)
                 .append(",").append(el.hurtTime)
                 .append(",").append(el.deathTime)
                 .append(",").append(el.renderYawOffset)
                 .append(",").append(fl);
                if (e instanceof net.minecraft.entity.passive.EntitySheep) {
                    net.minecraft.entity.passive.EntitySheep sh = (net.minecraft.entity.passive.EntitySheep) e;
                    // wool present + dye color + graze head-down timer
                    b.append(",").append(sh.getSheared() ? 1 : 0)
                     .append(",").append(sh.getFleeceColor().getMetadata())
                     .append(",").append(sh.getHeadRotationPointY(1.0F))
                     .append(",").append(sh.getHeadRotationAngleX(1.0F));
                } else if (e instanceof net.minecraft.entity.boss.EntityDragon) {
                    // wing-flap phase + death-animation ticks (0..200; drives
                    // the collapse/beam render, distinct from deathTime), then
                    // AI phase id + stationary flag (ModelDragon head-Y branch).
                    net.minecraft.entity.boss.EntityDragon dr =
                        (net.minecraft.entity.boss.EntityDragon) e;
                    int phaseId = -1, stationary = 0;
                    try {
                        net.minecraft.entity.boss.dragon.phase.IPhase ph =
                            dr.getPhaseManager().getCurrentPhase();
                        phaseId = ph.getPhaseList().getId();
                        stationary = ph.getIsStationary() ? 1 : 0;
                    } catch (Throwable ig) {}
                    b.append(",").append(dr.animTime)
                     .append(",").append(dr.deathTicks)
                     .append(",").append(phaseId)
                     .append(",").append(stationary);
                }
            } else if (e instanceof net.minecraft.entity.projectile.EntityArrow) {
                // render pitch (RenderArrow Rz): stuck arrows keep the impact
                // angle; yaw alone renders non-flat shots at the wrong tilt.
                b.append(",").append(e.rotationPitch);
            } else if (e instanceof net.minecraft.entity.item.EntityItem) {
                net.minecraft.entity.item.EntityItem ei = (net.minecraft.entity.item.EntityItem) e;
                net.minecraft.item.ItemStack st = ei.getEntityItem();
                b.append(",").append(net.minecraft.item.Item.getIdFromItem(st.getItem()))
                 .append(",").append(st.getMetadata())
                 .append(",").append(st.getCount())
                 .append(",").append(ei.getAge())
                 .append(",").append(ei.hoverStart); // unseeded random bob/spin phase
            } else if (e instanceof net.minecraft.entity.item.EntityXPOrb) {
                // RenderXPOrb: xpValue (texture tier), xpColor (green/yellow
                // phase), xpOrbAge. Appended so old 7-field rows still parse.
                net.minecraft.entity.item.EntityXPOrb orb =
                    (net.minecraft.entity.item.EntityXPOrb) e;
                b.append(",").append(orb.xpValue)
                 .append(",").append(orb.xpColor)
                 .append(",").append(orb.xpOrbAge);
            } else if (e instanceof net.minecraft.entity.item.EntityEnderCrystal) {
                // RenderEnderCrystal state: random-init spin/bob phase, base
                // plate flag, heal-beam target (-1,-1,-1 = no beam).
                net.minecraft.entity.item.EntityEnderCrystal ec =
                    (net.minecraft.entity.item.EntityEnderCrystal) e;
                net.minecraft.util.math.BlockPos bt = ec.getBeamTarget();
                b.append(",").append(ec.innerRotation)
                 .append(",").append(ec.shouldShowBottom() ? 1 : 0)
                 .append(",").append(bt == null ? -1 : bt.getX())
                 .append(",").append(bt == null ? -1 : bt.getY())
                 .append(",").append(bt == null ? -1 : bt.getZ());
            }
            b.append("]");
            if (++n >= REC_ENT_MAX) break;
        }
        b.append("]}");
        recWriter.println(b.toString());
        recWriter.flush();
    }

    private String obs(Minecraft mc) { return obs(mc, false); }

    private String obs(Minecraft mc, boolean wantCam) {
        EntityPlayerSP p = mc.player;
        JsonObject o = new JsonObject();
        o.addProperty("ok", true);
        // ---- chain-RL protocol v2 fields (rl_mode.c obs parity) ----
        o.addProperty("container", rlContainer);
        JsonArray invc = new JsonArray();
        for (int i = 0; i < RL_INV_IDS.length; i++)
            invc.add(new com.google.gson.JsonPrimitive(rlCountItem(p.inventory, RL_INV_IDS[i])));
        o.add("inv_counts", invc);
        // additive iron-chain counts (blaze_fill_status cols 13..16):
        // furnace item, iron ore, iron ingot, iron pickaxe.
        JsonArray invIron = new JsonArray();
        for (int id : new int[]{61, 15, 265, 257})
            invIron.add(new com.google.gson.JsonPrimitive(rlCountItem(p.inventory, id)));
        o.add("inv_iron", invIron);
        o.addProperty("hotbar_sel", p.inventory.currentItem);
        if (wantCam && mc.world != null) {
            try {
                semCam.render(mc.world, p.posX, p.posY + SemanticCamera.EYE,
                              p.posZ, p.rotationYaw, p.rotationPitch);
                o.add("cam", intArray(semCam.cam));
                o.add("depth", intArray(semCam.depth));
                o.add("edge", intArray(semCam.edge));
                o.add("coal", rlCoalScan(mc));
            } catch (Throwable t) {
                o.addProperty("cam_error", String.valueOf(t));
            }
        }
        // pose + motion
        o.addProperty("x", p.posX); o.addProperty("y", p.posY); o.addProperty("z", p.posZ);
        o.addProperty("yaw", p.rotationYaw); o.addProperty("pitch", p.rotationPitch);
        // Camera eye (for hard-scene / capture; feet = y, eye = y + eyeHeight)
        o.addProperty("eye_height", p.getEyeHeight());
        o.addProperty("eye_y", p.posY + p.getEyeHeight());
        o.addProperty("vx", p.motionX); o.addProperty("vy", p.motionY); o.addProperty("vz", p.motionZ);
        o.addProperty("on_ground", p.onGround);
        // vitals
        o.addProperty("health", p.getHealth());
        o.addProperty("food", p.getFoodStats().getFoodLevel());
        o.addProperty("saturation", p.getFoodStats().getSaturationLevel());
        o.addProperty("air", p.getAir());
        o.addProperty("xp", p.experienceLevel);
        o.addProperty("xp_frac", p.experience);   // 0..1 bar fill
        o.addProperty("dead", p.getHealth() <= 0.0F);
        o.addProperty("deaths", deaths);
        try { o.addProperty("dim", mc.world.provider.getDimensionType().getId()); }
        catch (Throwable ig) { o.addProperty("dim", 0); }
        // ---- extended per-tick STATE VECTOR (tick-trace oracle; trace/trace_java.py) ----
        // fire ticks (Entity.fire is protected -> reflect once; -1 if unavailable)
        o.addProperty("fire", fireTicks(p));
        o.addProperty("fall_distance", p.fallDistance);
        o.addProperty("sprinting", p.isSprinting());
        o.addProperty("sneaking", p.isSneaking());
        o.addProperty("jumping", livingJumping(p));   // EntityLivingBase.isJumping (reflected)
        o.addProperty("hurt_time", p.hurtTime);
        o.addProperty("death_time", p.deathTime);
        o.addProperty("attack_cooldown", p.getCooledAttackStrength(0.0F));  // 0..1
        // held item
        int held = p.inventory.currentItem;
        o.addProperty("held_slot", held);
        net.minecraft.item.ItemStack hs = p.inventory.getStackInSlot(held);
        o.addProperty("held_id", stackId(hs));
        o.addProperty("held_count", stackCount(hs));
        o.addProperty("held_meta", stackMeta(hs));
        // active potion effects
        JsonArray pots = new JsonArray();
        try {
            for (net.minecraft.potion.PotionEffect pe : p.getActivePotionEffects()) {
                JsonObject jp = new JsonObject();
                jp.addProperty("id", net.minecraft.potion.Potion.getIdFromPotion(pe.getPotion()));
                jp.addProperty("amp", pe.getAmplifier());
                jp.addProperty("dur", pe.getDuration());
                pots.add(jp);
            }
        } catch (Throwable ig) {}
        o.add("potions", pots);
        // full main inventory (36 slots), non-empty only, vanilla registry ids
        JsonArray inv = new JsonArray();
        try {
            for (int i = 0; i < p.inventory.mainInventory.size(); i++) {
                net.minecraft.item.ItemStack s = p.inventory.mainInventory.get(i);
                if (s == null || s.isEmpty()) continue;
                JsonObject js = new JsonObject();
                js.addProperty("slot", i);
                js.addProperty("id", stackId(s));
                js.addProperty("count", s.getCount());
                js.addProperty("meta", s.getMetadata());
                inv.add(js);
            }
        } catch (Throwable ig) {}
        o.add("inventory", inv);
        // time + weather
        JsonObject tw = new JsonObject();
        try {
            tw.addProperty("world_time", mc.world.getWorldTime());
            tw.addProperty("total_time", mc.world.getTotalWorldTime());
            tw.addProperty("moon_phase", mc.world.getMoonPhase());
            tw.addProperty("raining", mc.world.isRaining());
            tw.addProperty("thundering", mc.world.isThundering());
        } catch (Throwable ig) {}
        o.add("time", tw);
        // look target
        JsonObject look = new JsonObject();
        RayTraceResult r = mc.objectMouseOver;
        if (r == null || r.typeOfHit == RayTraceResult.Type.MISS) {
            look.addProperty("type", "miss");
        } else if (r.typeOfHit == RayTraceResult.Type.BLOCK) {
            look.addProperty("type", "block");
            BlockPos bp = r.getBlockPos();
            look.addProperty("bx", bp.getX()); look.addProperty("by", bp.getY()); look.addProperty("bz", bp.getZ());
            look.addProperty("block", mc.world.getBlockState(bp).getBlock().getUnlocalizedName());
            look.addProperty("dist", p.getDistanceSq(bp.getX(), bp.getY(), bp.getZ()));
        } else {
            look.addProperty("type", "entity");
            if (r.entityHit != null) {
                look.addProperty("entity", r.entityHit.getClass().getSimpleName());
                look.addProperty("dist", p.getDistanceToEntity(r.entityHit));
            }
        }
        o.add("look", look);
        // nearby entities: ALL loaded (capped at N_ENTITIES_MAX), sorted by squared distance.
        // Emits absolute pos + motion + rotation + health + a stable entityId so the diff tool
        // can track spawn/despawn (the entity SET changing) tick-to-tick.
        ArrayList<Entity> ents = new ArrayList<Entity>(mc.world.loadedEntityList);
        ents.remove(p);
        ents.sort((a2, b2) -> Double.compare(p.getDistanceSqToEntity(a2), p.getDistanceSqToEntity(b2)));
        JsonArray arr = new JsonArray();
        int emax = Math.min(N_ENTITIES_MAX, ents.size());
        for (int i = 0; i < emax; i++) {
            Entity en = ents.get(i);
            JsonObject je = new JsonObject();
            je.addProperty("eid", en.getEntityId());
            je.addProperty("type", en.getClass().getSimpleName());
            je.addProperty("x", en.posX); je.addProperty("y", en.posY); je.addProperty("z", en.posZ);
            je.addProperty("dx", en.posX - p.posX);
            je.addProperty("dy", en.posY - p.posY);
            je.addProperty("dz", en.posZ - p.posZ);
            je.addProperty("vx", en.motionX); je.addProperty("vy", en.motionY); je.addProperty("vz", en.motionZ);
            je.addProperty("yaw", en.rotationYaw); je.addProperty("pitch", en.rotationPitch);
            if (en instanceof net.minecraft.entity.EntityLivingBase) {
                net.minecraft.entity.EntityLivingBase lb = (net.minecraft.entity.EntityLivingBase) en;
                je.addProperty("health", lb.getHealth());
            } else {
                je.addProperty("health", -1.0f);
            }
            arr.add(je);
        }
        o.addProperty("entity_count", ents.size());
        o.add("entities", arr);
        return o.toString();
    }

    // ---- obs helpers for the extended state vector ----
    private static java.lang.reflect.Field FIRE_F, JUMP_F;
    private static int fireTicks(Entity e) {
        try {
            if (FIRE_F == null) {
                FIRE_F = Entity.class.getDeclaredField("fire");
                FIRE_F.setAccessible(true);
            }
            return FIRE_F.getInt(e);
        } catch (Throwable ig) { return -1; }
    }
    private static boolean livingJumping(net.minecraft.entity.EntityLivingBase e) {
        try {
            if (JUMP_F == null) {
                JUMP_F = net.minecraft.entity.EntityLivingBase.class.getDeclaredField("isJumping");
                JUMP_F.setAccessible(true);
            }
            return JUMP_F.getBoolean(e);
        } catch (Throwable ig) { return false; }
    }
    private static int stackId(net.minecraft.item.ItemStack s) {
        if (s == null || s.isEmpty()) return 0;
        return net.minecraft.item.Item.getIdFromItem(s.getItem());
    }
    private static int stackCount(net.minecraft.item.ItemStack s) {
        return (s == null || s.isEmpty()) ? 0 : s.getCount();
    }
    private static int stackMeta(net.minecraft.item.ItemStack s) {
        return (s == null || s.isEmpty()) ? 0 : s.getMetadata();
    }

    // ---- entity_trace reflection: arrow protected/private stick state, entity air ----
    private static java.lang.reflect.Field ARROW_INGROUND, ARROW_TICKSINAIR, ENT_AIR;
    private static boolean arrowInGround(net.minecraft.entity.projectile.EntityArrow a) {
        try {
            if (ARROW_INGROUND == null) {
                ARROW_INGROUND = net.minecraft.entity.projectile.EntityArrow.class.getDeclaredField("inGround");
                ARROW_INGROUND.setAccessible(true);
            }
            return ARROW_INGROUND.getBoolean(a);
        } catch (Throwable ig) { return false; }
    }
    private static int arrowTicksInAir(net.minecraft.entity.projectile.EntityArrow a) {
        try {
            if (ARROW_TICKSINAIR == null) {
                ARROW_TICKSINAIR = net.minecraft.entity.projectile.EntityArrow.class.getDeclaredField("ticksInAir");
                ARROW_TICKSINAIR.setAccessible(true);
            }
            return ARROW_TICKSINAIR.getInt(a);
        } catch (Throwable ig) { return -1; }
    }
    private static int getAir(net.minecraft.entity.Entity e) {
        try { return e.getAir(); } catch (Throwable ig) { return -1; }
    }
    // EntityItem.age / delayBeforeCanPickup are private int; reflect them for the P2 item trace.
    private static java.util.HashMap<String, java.lang.reflect.Field> ITEM_FIELDS =
        new java.util.HashMap<String, java.lang.reflect.Field>();
    private static int itemField(net.minecraft.entity.item.EntityItem ei, String name) {
        try {
            java.lang.reflect.Field f = ITEM_FIELDS.get(name);
            if (f == null) {
                f = net.minecraft.entity.item.EntityItem.class.getDeclaredField(name);
                f.setAccessible(true);
                ITEM_FIELDS.put(name, f);
            }
            return f.getInt(ei);
        } catch (Throwable ig) { return -1; }
    }

    private static JsonArray intArray(int[] v) {
        JsonArray a = new JsonArray();
        for (int i = 0; i < v.length; i++) a.add(new com.google.gson.JsonPrimitive(v[i]));
        return a;
    }

    private static boolean bit(JsonObject a, String k) { return a.has(k) && a.get(k).getAsInt() != 0; }
    private static float clamp(float v, float lo, float hi) { return Math.max(lo, Math.min(hi, v)); }
    private static String err(String m) { JsonObject o = new JsonObject(); o.addProperty("ok", false); o.addProperty("error", m); return o.toString(); }
}
