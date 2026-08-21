package netheritemod;

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
import net.minecraftforge.client.event.RenderPlayerEvent;
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
@Mod(modid = Recorder.MODID, name = "NetheriteMod", version = "1.0",
     clientSideOnly = true, acceptableRemoteVersions = "*")
public class Recorder {
    // Frozen legacy id: Forge writes MODID into level.dat; renaming breaks existing
    // worlds (e.g. qrl_0 play save) and tape-recorded worlds (missing-mods screen).
    public static final String MODID = "qrl";
    // Frozen legacy default port (TCP bridge wire protocol). Override: -Dqrl.port
    // (gradle -PqrlPort) > qrl_launch.json "port" > this default.
    static int PORT = 25575;
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

    // ---------------- server tick lockstep gate ----------------
    // The integrated server runs on its own thread, so an ordinary bridge command
    // straddles zero, one, or two authoritative ticks: back-to-back getblocks can
    // land on different server ticks and disagree, and there is no way to advance
    // the world by an exact count. server_step_lock parks the server thread inside
    // ServerTickEvent.START; step_server_locked permits exactly n ticks and returns
    // only once the server has BOTH executed them and re-parked at the next START,
    // so a following *_locked read observes a frozen world without being charged an
    // extra tick. Off by default; the ungated step/getblocks/setblocks paths are
    // untouched.
    //
    // Locked commands deliberately run on the SOCKET thread. While the server is
    // parked it never drains addScheduledTask, so routing them through the normal
    // server-task path would deadlock; and the client thread is free to keep
    // rendering, so it cannot serve them either. Reading a parked WorldServer from
    // another thread is safe precisely because the owner is blocked in wait().
    private final Object lockMon = new Object();
    private volatile boolean lockArmed = false;  // gate is on
    private boolean lockParked = false;          // server thread is waiting in START
    private boolean lockInFlight = false;        // a permitted tick is executing
    private long lockPermits = 0;                // ticks permitted but not yet started
    private long lockCompleted = 0;              // ticks executed under this gate

    // ---------------- RNG cursor capture ----------------
    // Vanilla consumes java.util.Random draws in a fixed order every server tick;
    // magma must consume the same number in the same order or it silently walks a
    // different random stream. The failure is invisible for hundreds of ticks and
    // then surfaces as a physics or pixel divergence with no obvious cause.
    //
    // java.util.Random IS its 48-bit LCG state, so a snapshot of that state at a
    // tick boundary is a cursor: two cursors bracketing a tick determine EXACTLY
    // how many next() calls the tick consumed (walk the LCG forward and count -
    // see verify/trace/rng_cursor.py). That turns "the streams drifted somewhere"
    // into "tick N consumed 7 world draws, expected 6".
    //
    // Four server-side streams are captured, matching the set bluecoconut's PR #5
    // state capsule marks exact (world.rng.{java,math,block}_random_seed48 and
    // world.rng.update_lcg):
    //   world  - World.rand, the big one: random ticks, weather, mob spawning, and
    //            every playSound draw (which is why the sound-EVENT ring exists).
    //   math   - java.lang.Math's process-global RNG. Shared with the JDK, so a
    //            stray Math.random() anywhere in the JVM moves it; captured to
    //            prove non-interference, not to model.
    //   block  - Block.RANDOM, the static fallback for blocks without a world.
    //   lcg    - World.updateLCG, an int32 LCG (not a java.util.Random) that picks
    //            random-tick coordinates. Advances independently of `world`.
    //
    // Capture is a bounded ring filled at ServerTickEvent.START, i.e. the same
    // boundary the lockstep gate parks at, so cursor[i] is the state the tick
    // about to run is about to consume. Ring, not a socket round-trip per tick:
    // a 6000-tick session costs one dump instead of 6000 request/response pairs.
    // Arm/dump do NOT require the gate - capture is just as valid free-running -
    // but a direct rng_cursor_locked read does, because reading a live server's
    // RNG is exactly the race the gate exists to remove.
    private final Object rngMon = new Object();
    private volatile boolean rngCaptureOn = false;
    private int rngCapN = 0;          // records held (<= capacity)
    private int rngCapHead = 0;       // ring write cursor
    private long rngCapDropped = 0;   // ticks lost to ring overflow
    private long rngCapSeq = 0;       // monotone capture sequence (survives wrap)
    private long[] rngCapWorldTime;   // World.getTotalWorldTime() at START
    private long[] rngCapSeqArr;      // capture sequence number
    private long[] rngCapCompletedArr;// lockstep gate `completed` at START (-1 unarmed)
    private long[] rngCapWorld;       // World.rand seed48
    private long[] rngCapMath;        // Math RNG seed48
    private long[] rngCapBlock;       // Block.RANDOM seed48
    private int[] rngCapLcg;          // World.updateLCG (int32)
    private byte[] rngCapGaussHave;   // World.rand haveNextNextGaussian
    private long[] rngCapGaussBits;   // World.rand nextNextGaussian, raw IEEE-754

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
    /** When true, skip auto-respawn so death-screen oracle captures can freeze. */
    private volatile boolean holdDeath = false;
    /** When >= 0, re-apply EntityRenderer.rendererUpdateCount before each frame{}
     * re-render so portal camera warp is A/B-stable under a live client tick. */
    private volatile int pinPortalPhase = -1;
    /** Sticky timeInPortal for oracle portal overlay. Free-running client ticks
     * decay EntityPlayer.timeInPortal when not in a portal block; without
     * re-applying here, A/B frame{} grabs sample different fourth-power alphas
     * and leave maxch=1 residuals under the purple fullscreen swirl. */
    private volatile boolean pinPortalTimeActive = false;
    private volatile float pinPortalTime = 0.0F;

    /**
     * Sticky first-person use-pose pin for oracle hand captures.
     *
     * Vanilla Minecraft.runTick processKeyBinds() calls onStoppedUsingItem whenever
     * isHandActive() && !keyBindUseItem.isKeyDown(). Free-running client ticks
     * between hud_pin and frame{} therefore wipe setActiveHand, so ItemRenderer
     * falls through to the idle/rest branch (tip only) despite pin_reply reporting
     * hand_active=true at pin time.
     *
     * Also: ItemBow.pulling / ItemShield.blocking model predicates require
     * getActiveItemStack() == rendered stack (reference equality). Pinning a
     * .copy() breaks bow_pulling_* / shield_blocking overrides even if the
     * BOW/EAT/BLOCK transform branch is taken.
     *
     * pinUseActive is re-applied at ClientTickEvent END and immediately before
     * frame{} re-render so processKeyBinds cannot leave an idle viewmodel.
     *
     * Scope: MAIN_HAND only (dual-wield / offhand active-use out of scope).
     * pinPoseActive freezes camera for capture A/B registration only.
     */
    private volatile boolean pinUseActive = false;
    private volatile int pinUseRemaining = 0;
    private volatile boolean pinUseKeyHeld = false;
    /** Optional sticky camera freeze (set by hud_pin x/y/z) so free-running
     * ticks between A/B cannot drift the wall registration under a large
     * viewmodel silhouette (drawn bow). */
    private volatile boolean pinPoseActive = false;
    private volatile double pinPoseX, pinPoseY, pinPoseZ;
    private volatile float pinPoseYaw, pinPosePitch;

    // Inventory player-preview capture pin: freeze ModelBiped ageInTicks so
    // GuiInventory.drawEntityOnScreen (partialTicks=1.0) is same-state for A/B
    // grabs. handleRotationFloat = ticksExisted + partialTicks; with partial=1
    // and ticksExisted=-1, ageInTicks=0 -> idle arm Z = ±0.10 exactly.
    private static volatile boolean pinPreviewAnim = false;
    private static volatile int pinPreviewTicksExisted = -1;
    private int pinSavedTicksExisted = 0;
    private float pinSavedLimbSwing = 0, pinSavedLimbSwingAmount = 0, pinSavedPrevLimb = 0;
    private boolean pinSavedValid = false;

    // ---- chain-RL protocol v2 state (semantic camera / craft / interact) ----
    // Mirrors magma/game/rl_mode.c semantics so the blaze-trained policy
    // transfers: container is MOD state (no GUI opens - headless keybind play
    // must keep running), validity-checked every tick like gm_runtime_tick.
    private final SemanticCamera semCam = new SemanticCamera();
    private boolean rlCamPending = false;   // last step action asked for "cam":1
    // Monotone proof that policy steps reached applyAction() on the client
    // thread.  The paper sim2real harness checks that seq advances exactly
    // once per socket step and records the digest in its result artifact.
    private long rlPolicyActionSeq = 0L;
    private long rlPolicyActionFnv = 0xcbf29ce484222325L;
    private int rlContainer = 0;            // 0 none/2x2, 1 table, 2 furnace
    private BlockPos rlContainerPos = null;
    /** True once a protocol-v2 action has been applied (any of cam/dyaw/
     * dpitch/craft/interact/smelt). While set, a real GuiContainer is closed
     * at the end of every client tick: magma has no window at all, so a
     * vanilla right-click that opens GuiCrafting/GuiFurnace/GuiChest would
     * park the client in a state the training env cannot represent (vanilla
     * skips processKeyBinds and zeroes movement input while a screen is up,
     * so attack/use/move all die and the episode never recovers). Cleared on
     * reset. */
    private boolean rlV2Active = false;
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
    /** SPacketExplosion events applied since the last recorded tick. Each
     * entry: [x,y,z,strength,mx,my,mz,[[bx,by,bz],...]]. Client thread only;
     * a tick can carry several blasts (e.g. chained TNT). */
    private static final java.util.List<String> recExplosions =
        new java.util.ArrayList<String>();
    /** Whitelisted client particle spawns since the last recorded tick
     * (MixinRecordParticles -> ParticleManager.spawnEffectParticle). Each
     * entry: [enumParticleId,x,y,z,vx,vy,vz]. Explosion classes only for
     * now: their placement RNG (Entity.rand seeded from nanoTime) is the
     * documented unrecoverable-from-tape class (SCOPE section 4); recording
     * the actual spawns makes creeper/TNT clouds and dragon-death puffs
     * deterministic for replay. Client thread only. */
    private static final java.util.List<String> recParticles =
        new java.util.ArrayList<String>();
    /** Ordered GuiContainer.handleMouseClick events since the last recorded
     * tick (MixinRecordGuiClick). Each entry is a compact JSON array
     * [guiSimpleName, slotNumber, mouseButton, clickTypeOrdinal, "NAME", wid].
     * slotNumber is Container.inventorySlots index (-999 = outside). Client
     * thread only; drained into tape field "gclk". gslots/gcur remain
     * post-tick render truth and must not be used to invent clicks. */
    private static final java.util.List<String> recGuiClicks =
        new java.util.ArrayList<String>();
    /** Container open edges this tick (edge-detected at ClientTick END).
     * Compact JSON via ContainerTapeFormat. Drained as tape field "gopen". */
    private static final java.util.List<String> recGuiOpens =
        new java.util.ArrayList<String>();
    /** Container close edges this tick. Drained as tape field "gclose". */
    private static final java.util.List<String> recGuiCloses =
        new java.util.ArrayList<String>();
    /** Last noted right-click block pos (MixinRecordBlockInteract). Used to
     * bind workbench/furnace/chest opens to a world position because
     * SPacketOpenWindow carries no BlockPos. */
    private static volatile boolean recLastUseValid = false;
    private static volatile int recLastUseX, recLastUseY, recLastUseZ;
    /** Open container identity tracked across ticks for edge detection. */
    private static int recOpenWid = -1;
    private static String recOpenGui = null;
    /** True only between successful recstart and recstop. Static so the
     * recorder mixins can gate without holding the Recorder instance. */
    private static volatile boolean recActive = false;
    /** Player entity flag-7 values delivered by SPacketEntityMetadata since
     * the last recorded tick. The mixin records the packet's base-flags data
     * entry itself, so these are observed integrated-server round trips rather
     * than states inferred from jump input or local EntityLivingBase updates. */
    private static final java.util.List<Integer> recFlag7Metadata =
        new java.util.ArrayList<Integer>();
    /** Successful WorldClient setBlockState finals since the last recorded
     * tick (MixinRecordBlockChanges). Each entry is [x,y,z,id,meta] in exact
     * call order. Client thread only while recActive. Also receives changed
     * cells from partial SPacketChunkData -> Chunk.fillChunk
     * (MixinRecordChunkFill), same channel / same set_block_post replay. */
    private static final java.util.List<int[]> recBlockChanges =
        new java.util.ArrayList<int[]>();
    /** In-flight partial fillChunk pre-image: per-section packed id/meta, or
     * null section = all air. Client thread only; one fill at a time. */
    private static boolean recFillCaptureActive = false;
    private static int recFillChunkX = 0;
    private static int recFillChunkZ = 0;
    private static int recFillMask = 0;
    /** Length 16; each entry null or length-4096 packed (id << 4 | meta). */
    private static final int[][] recFillPreSections = new int[16][];
    /** dig_trace only: ordered neighbor-caused block finals this tick
     * (setBlockState nested under World.notifyNeighbors*). Same [x,y,z,id,meta]
     * layout as bc entries. Empty / omitted when dig_trace is off so normal
     * tapes stay byte-identical. */
    private static final java.util.List<int[]> recNeighborBlockChanges =
        new java.util.ArrayList<int[]>();
    /** dig_trace only: successful onPlayerDestroyBlock this tick, as
     * [x,y,z] world coords. At most one progressive break per client tick in
     * practice; list preserves order if creative multi-fire occurs. */
    private static final java.util.List<int[]> recDigBreaks =
        new java.util.ArrayList<int[]>();
    /** Nesting depth of World.notifyNeighborsOfStateChange/Except. Used only
     * to tag neighbor-caused finals for dig_trace; 0 when dig_trace off is
     * fine (recordBlockChange ignores the flag then). */
    private static int recNeighborNotifyDepth = 0;
    /** Nesting depth of PlayerControllerMP.onPlayerDestroyBlock. */
    private static int recDigDestroyDepth = 0;
    /** When true, recstart opted into per-tick dig controller traces. Default
     * false: no dig field, no dig_trace header key, determinism unchanged. */
    private static volatile boolean recDigTrace = false;
    /** Opt-in recorder contract metadata. tape.py enables this for new tapes;
     * direct legacy recstart callers omit it and keep their old header shape. */
    private static volatile boolean recContract = false;
    /** Rotation sampled at ClientTickEvent.START, i.e. exactly what this
     * tick's travel() will use. The post-tick yaw/pitch in the row can differ
     * when the driver lands a turn between END and the next START (mouse look,
     * scenario turns), and WHICH side of travel a turn lands on varies per
     * recording - so replay must not infer the physics rotation from post-tick
     * values (measured: nether_elytra t=43 arm tick used the NEW pitch 6.8
     * while the fresh-world t371 walking turn used the OLD yaw). */
    private static float recPreYaw = 0.0F, recPrePitch = 0.0F;
    private static boolean recPreLookValid = false;
    private volatile String recSnapRoot = null;   // <tape>_world, live while recording
    private int recLastPlayerTicksExisted = Integer.MIN_VALUE;
    private int recLastDimension = Integer.MIN_VALUE;
    private boolean recPlayerLoading = false;
    private static java.lang.reflect.Field recPortalFrameField = null;
    private static java.lang.reflect.Field recRendererPhaseField = null;
    private static final int REC_ENT_RADIUS = 48;
    private static final int REC_ENT_MAX = 32;

    /**
     * Pending CLIENT render-entity pin for oracle A/B frames.
     * Server spawn alone cannot pin squishFactor/deathTicks (not reliably synced;
     * client onUpdate overwrites squish). frame{} applies this immediately before
     * renderWorld(partialTicks=1) and restores afterward so free-running ticks
     * cannot clobber the pin between settle and readback.
     */
    private volatile boolean renderPinActive = false;
    private volatile String renderPinKind = null;
    private volatile java.util.UUID renderPinUuid = null;
    private volatile int renderPinEid = -1;
    private volatile double renderPinX, renderPinY, renderPinZ;
    private volatile float renderPinSquish;
    private volatile int renderPinSize;
    private volatile int renderPinDeathTicks;
    private volatile boolean renderPinIsMagma;
    /** XP orb client render pin: color/age/value + frozen world pose. */
    private volatile int renderPinXpColor;
    private volatile int renderPinXpAge;
    private volatile int renderPinXpValue;
    private volatile boolean renderPinIsXp;

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

    /** Client-thread hook from MixinRecordExplosion. Serialized immediately:
     * the packet's block list is authoritative (Explosion.doExplosionB clears
     * exactly these positions client-side) and the motion components are the
     * knockback added to the local player. */
    public static void recordExplosionPacket(
            net.minecraft.network.play.server.SPacketExplosion packet) {
        StringBuilder e = new StringBuilder();
        e.append('[').append(packet.getX())
         .append(',').append(packet.getY())
         .append(',').append(packet.getZ())
         .append(',').append(packet.getStrength())
         .append(',').append(packet.getMotionX())
         .append(',').append(packet.getMotionY())
         .append(',').append(packet.getMotionZ())
         .append(",[");
        java.util.List<net.minecraft.util.math.BlockPos> blocks =
            packet.getAffectedBlockPositions();
        for (int i = 0; i < blocks.size(); i++) {
            net.minecraft.util.math.BlockPos bp = blocks.get(i);
            if (i > 0) e.append(',');
            e.append('[').append(bp.getX()).append(',')
             .append(bp.getY()).append(',').append(bp.getZ()).append(']');
        }
        e.append("]]");
        recExplosions.add(e.toString());
    }

    /** Client-thread hook from MixinRecordParticles. Only whitelisted ids
     * arrive here; serialized flat so recordTick can drain cheaply. */
    public static void recordParticleSpawn(int id, double x, double y,
            double z, double vx, double vy, double vz) {
        StringBuilder p = new StringBuilder(96);
        p.append('[').append(id)
         .append(',').append(x).append(',').append(y).append(',').append(z)
         .append(',').append(vx).append(',').append(vy).append(',').append(vz)
         .append(']');
        recParticles.add(p.toString());
    }

    /** Client-thread hook from MixinRecordGuiClick. Only while recording;
     * preserves vanilla call order within the post-tick drain window.
     * Timing note: clicks land between render/input and ClientTick END, so
     * they appear on the END-of-tick row that drains them. Replay applies
     * them before gm_runtime_tick of that same tape tick (one-tick
     * ambiguity if a click and its world effect straddle the END boundary). */
    public static void recordGuiContainerClick(String gui, int slotId,
            int mouseButton, net.minecraft.inventory.ClickType type,
            int windowId) {
        if (!recActive || type == null) return;
        recGuiClicks.add(ContainerTapeFormat.formatGclk(
                gui, slotId, mouseButton, type.ordinal(), type.name(), windowId));
    }

    /** Client-thread hook from MixinRecordBlockInteract. */
    public static void noteBlockInteract(net.minecraft.util.math.BlockPos pos) {
        if (!recActive || pos == null) return;
        recLastUseX = pos.getX();
        recLastUseY = pos.getY();
        recLastUseZ = pos.getZ();
        recLastUseValid = true;
    }

    private static void clearRecGuiClicks() {
        recGuiClicks.clear();
    }

    private static void clearRecGuiLifecycle() {
        recGuiOpens.clear();
        recGuiCloses.clear();
        recOpenWid = -1;
        recOpenGui = null;
        recLastUseValid = false;
    }

    /** Serialize one ItemStack as tape fragment: 0 or [id,meta,count]. */
    private static void appendStackJson(StringBuilder b,
            net.minecraft.item.ItemStack st) {
        if (st == null || st.isEmpty()) {
            b.append('0');
            return;
        }
        b.append('[').append(net.minecraft.item.Item.getIdFromItem(st.getItem()))
         .append(',').append(st.getMetadata())
         .append(',').append(st.getCount()).append(']');
    }

    /**
     * Edge-detect open/close of GuiContainer screens at ClientTick END.
     * Emits gopen with gui/wid/ctype/pos/container-side slots/cursor (and
     * furnace prop) and gclose with wid+gui. Position comes from the last
     * right-click block when the block id matches the ctype; player inventory
     * is wid=0 ctype=player with no pos.
     */
    private static void trackContainerLifecycle(Minecraft mc,
            net.minecraft.entity.player.EntityPlayer p) {
        if (mc == null || p == null) return;
        boolean isCont = mc.currentScreen instanceof
                net.minecraft.client.gui.inventory.GuiContainer;
        if (!isCont) {
            if (recOpenWid >= 0) {
                recGuiCloses.add(ContainerTapeFormat.formatGclose(
                        recOpenWid, recOpenGui != null ? recOpenGui : ""));
                recOpenWid = -1;
                recOpenGui = null;
            }
            return;
        }
        net.minecraft.client.gui.inventory.GuiContainer gc =
                (net.minecraft.client.gui.inventory.GuiContainer) mc.currentScreen;
        String gui = gc.getClass().getSimpleName();
        int wid = p.openContainer != null ? p.openContainer.windowId : 0;
        int nSlots = gc.inventorySlots != null
                ? gc.inventorySlots.inventorySlots.size() : 0;
        if (recOpenWid == wid && gui.equals(recOpenGui)) return;
        /* Switching A->B: only emit open for B. Replay treats a new gopen as
         * an implicit close of the previous identity. Explicit gclose is only
         * for returning to no-container (displayGuiScreen null / inventory
         * closed). */
        String ctype = ContainerTapeFormat.ctypeForGui(gui, nSlots);
        if (ContainerTapeFormat.CTYPE_PLAYER.equals(ctype)) {
            recGuiOpens.add(ContainerTapeFormat.formatGopenPlayer(gui, wid));
        } else {
            int x = 0, y = 0, z = 0;
            boolean havePos = false;
            /* Prefer TE pos from the first non-player inventory slot when the
             * open container still holds a TileEntity reference. */
            try {
                if (gc.inventorySlots != null && nSlots > 0) {
                    for (int i = 0; i < nSlots; ++i) {
                        net.minecraft.inventory.Slot sl =
                                gc.inventorySlots.inventorySlots.get(i);
                        if (sl == null || sl.inventory == null) continue;
                        if (sl.inventory instanceof net.minecraft.tileentity.TileEntity) {
                            net.minecraft.util.math.BlockPos bp =
                                    ((net.minecraft.tileentity.TileEntity) sl.inventory)
                                            .getPos();
                            if (bp != null) {
                                x = bp.getX(); y = bp.getY(); z = bp.getZ();
                                havePos = true;
                                break;
                            }
                        }
                    }
                }
            } catch (Throwable ig) {}
            if (!havePos && recLastUseValid && mc.world != null) {
                int bid = net.minecraft.block.Block.getIdFromBlock(
                        mc.world.getBlockState(
                                new net.minecraft.util.math.BlockPos(
                                        recLastUseX, recLastUseY, recLastUseZ))
                                .getBlock());
                int expect = ContainerTapeFormat.expectedBlockId(ctype);
                boolean ok = expect == bid
                        || (ContainerTapeFormat.CTYPE_FURNACE.equals(ctype)
                            && ContainerTapeFormat.furnaceBlockOk(bid));
                if (ok) {
                    x = recLastUseX; y = recLastUseY; z = recLastUseZ;
                    havePos = true;
                }
            }
            if (!havePos) {
                /* Fail-closed identity: pos sentinel forces replay reject if
                 * a block container cannot be bound. */
                x = Integer.MIN_VALUE;
                y = Integer.MIN_VALUE;
                z = Integer.MIN_VALUE;
            }
            /* Container-side slots only (vanilla prefix before player inv). */
            int contN = 0;
            if (ContainerTapeFormat.CTYPE_WORKBENCH.equals(ctype)) contN = 10;
            else if (ContainerTapeFormat.CTYPE_FURNACE.equals(ctype)) contN = 3;
            else if (ContainerTapeFormat.CTYPE_CHEST.equals(ctype)) contN = 27;
            else if (ContainerTapeFormat.CTYPE_DOUBLE_CHEST.equals(ctype)) contN = 54;
            StringBuilder slots = new StringBuilder(8 + contN * 16);
            slots.append('[');
            for (int i = 0; i < contN; ++i) {
                if (i > 0) slots.append(',');
                net.minecraft.item.ItemStack st =
                        (gc.inventorySlots != null && i < nSlots)
                        ? gc.inventorySlots.inventorySlots.get(i).getStack()
                        : net.minecraft.item.ItemStack.EMPTY;
                appendStackJson(slots, st);
            }
            slots.append(']');
            StringBuilder cur = new StringBuilder(24);
            appendStackJson(cur, p.inventory.getItemStack());
            String prop = null;
            if (ContainerTapeFormat.CTYPE_FURNACE.equals(ctype)
                    && gc.inventorySlots != null && nSlots > 0) {
                try {
                    net.minecraft.inventory.IInventory fi =
                            gc.inventorySlots.inventorySlots.get(0).inventory;
                    prop = "[" + fi.getField(0) + "," + fi.getField(1)
                            + "," + fi.getField(2) + "," + fi.getField(3) + "]";
                } catch (Throwable ig) {
                    prop = "[0,0,0,200]";
                }
            }
            recGuiOpens.add(ContainerTapeFormat.formatGopenBlock(
                    gui, wid, ctype, x, y, z,
                    slots.toString(), cur.toString(), prop));
        }
        recOpenWid = wid;
        recOpenGui = gui;
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

    /** Client-thread hook from MixinRecordPlayerMetadata. Capture every
     * occurrence because a tape can arm, clear, and arm again. Data parameter
     * 0 is Entity.FLAGS; bit 7 is fall-flying in MC 1.11.2. */
    public static void recordPlayerFlag7Metadata(
            net.minecraft.network.play.server.SPacketEntityMetadata packet) {
        Minecraft mc = Minecraft.getMinecraft();
        if (mc == null || mc.player == null ||
                packet.getEntityId() != mc.player.getEntityId()) return;
        java.util.List<net.minecraft.network.datasync.EntityDataManager.DataEntry<?>> entries =
            packet.getDataManagerEntries();
        if (entries == null) return;
        for (net.minecraft.network.datasync.EntityDataManager.DataEntry<?> entry : entries) {
            if (entry.getKey().getId() != 0 || !(entry.getValue() instanceof Byte))
                continue;
            int flags = ((Byte) entry.getValue()).byteValue() & 0xff;
            recFlag7Metadata.add(Integer.valueOf((flags & (1 << 7)) != 0 ? 1 : 0));
        }
    }

    /** Active human-tape session (recstart succeeded, recstop not yet). */
    public static boolean isRecording() { return recActive; }

    /** dig_trace session flag (recstart dig_trace:1). Mixins may gate extra
     * work on this so off-path cost stays zero. */
    public static boolean isDigTrace() { return recDigTrace; }

    /** Current World.notifyNeighbors* nesting depth (0 = top-level edit). */
    public static int neighborNotifyDepth() { return recNeighborNotifyDepth; }

    public static void neighborNotifyBegin() {
        if (recDigTrace) recNeighborNotifyDepth++;
    }

    public static void neighborNotifyEnd() {
        if (recDigTrace && recNeighborNotifyDepth > 0) recNeighborNotifyDepth--;
    }

    public static void digDestroyBegin() {
        if (recDigTrace) recDigDestroyDepth++;
    }

    /** Successful destroy end: records a dig break event when dig_trace on. */
    public static void digDestroyEnd(net.minecraft.util.math.BlockPos pos, boolean ok) {
        if (recDigTrace && recDigDestroyDepth > 0) recDigDestroyDepth--;
        if (!recActive || !recDigTrace || !ok || pos == null) return;
        int y = pos.getY();
        if (y < 0 || y > 255) return;
        recDigBreaks.add(new int[] { pos.getX(), y, pos.getZ() });
    }

    /** Client-thread hook from MixinRecordBlockChanges. Queue the FINAL
     * id/meta at pos after every successful WorldClient setBlockState.
     * Preserve repeated same-cell writes: adjacent callbacks can make their
     * intermediate order observable to fluids and falling blocks on replay. */
    public static void recordBlockChange(net.minecraft.util.math.BlockPos pos,
            net.minecraft.block.state.IBlockState state) {
        recordBlockChange(pos, state, false);
    }

    /** Same as {@link #recordBlockChange(BlockPos, IBlockState)} with an
     * explicit neighbor-cascade tag for dig_trace bc_ref. */
    public static void recordBlockChange(net.minecraft.util.math.BlockPos pos,
            net.minecraft.block.state.IBlockState state, boolean neighborCaused) {
        if (!recActive || pos == null || state == null) return;
        int x = pos.getX(), y = pos.getY(), z = pos.getZ();
        if (y < 0 || y > 255) return;
        net.minecraft.block.Block blk = state.getBlock();
        int id = net.minecraft.block.Block.getIdFromBlock(blk);
        int meta = blk.getMetaFromState(state) & 15;
        int[] entry = new int[] { x, y, z, id, meta };
        recBlockChanges.add(entry);
        if (recDigTrace && neighborCaused)
            recNeighborBlockChanges.add(entry);
    }

    /** Queue one final cell without going through BlockPos/IBlockState.
     * Used by fillChunk diffs so bulk path shares the bc channel. */
    private static void recordBlockChangePacked(int x, int y, int z, int id, int meta) {
        if (!recActive) return;
        if (y < 0 || y > 255) return;
        if (id < 0 || id > 4095) return;
        recBlockChanges.add(new int[] { x, y, z, id, meta & 15 });
    }

    private static void clearRecBlockChanges() {
        recBlockChanges.clear();
        abortChunkFillCapture();
        recNeighborBlockChanges.clear();
        recDigBreaks.clear();
    }

    /** Drop an in-flight fill pre-image (full load, world handoff, recstop). */
    public static void abortChunkFillCapture() {
        recFillCaptureActive = false;
        recFillMask = 0;
        for (int i = 0; i < recFillPreSections.length; ++i)
            recFillPreSections[i] = null;
    }

    /**
     * Client-thread HEAD of Chunk.fillChunk for a partial section packet.
     * Snapshots pre-image of every section bit set in {@code availableSections}.
     * Null / empty sections snapshot as all-air so a newly created section only
     * emits cells that become non-air (or any real id/meta change).
     */
    public static void beginChunkFillCapture(net.minecraft.world.chunk.Chunk chunk,
            int availableSections) {
        abortChunkFillCapture();
        if (!recActive || chunk == null) return;
        recFillChunkX = chunk.xPosition;
        recFillChunkZ = chunk.zPosition;
        recFillMask = availableSections;
        net.minecraft.world.chunk.storage.ExtendedBlockStorage[] storages =
            chunk.getBlockStorageArray();
        if (storages == null) return;
        for (int s = 0; s < 16 && s < storages.length; ++s) {
            if ((availableSections & (1 << s)) == 0) continue;
            net.minecraft.world.chunk.storage.ExtendedBlockStorage ebs = storages[s];
            int[] packed = new int[4096];
            if (ebs != null) {
                for (int i = 0; i < 4096; ++i) {
                    int lx = i & 15;
                    int lz = (i >> 4) & 15;
                    int ly = (i >> 8) & 15;
                    net.minecraft.block.state.IBlockState st = ebs.get(lx, ly, lz);
                    if (st == null) {
                        packed[i] = 0;
                        continue;
                    }
                    net.minecraft.block.Block blk = st.getBlock();
                    int id = net.minecraft.block.Block.getIdFromBlock(blk);
                    int meta = blk.getMetaFromState(st) & 15;
                    packed[i] = (id << 4) | meta;
                }
            }
            /* null ebs: packed stays all-zero (air). */
            recFillPreSections[s] = packed;
        }
        recFillCaptureActive = true;
    }

    /**
     * Client-thread RETURN of Chunk.fillChunk. Emits only cells whose packed
     * id/meta changed, in deterministic section then storage-index order, onto
     * the shared bc list. No-op if begin was aborted (full load / not recording).
     */
    public static void endChunkFillCapture(net.minecraft.world.chunk.Chunk chunk,
            int availableSections) {
        if (!recFillCaptureActive || !recActive || chunk == null) {
            abortChunkFillCapture();
            return;
        }
        if (chunk.xPosition != recFillChunkX || chunk.zPosition != recFillChunkZ
                || availableSections != recFillMask) {
            abortChunkFillCapture();
            return;
        }
        net.minecraft.world.chunk.storage.ExtendedBlockStorage[] storages =
            chunk.getBlockStorageArray();
        /* Scratch for one section post-image; reused across sections. */
        int[] postPacked = new int[4096];
        java.util.ArrayList<int[]> sectionDiffs = new java.util.ArrayList<int[]>();
        try {
            for (int s = 0; s < 16; ++s) {
                if ((availableSections & (1 << s)) == 0) continue;
                int[] pre = recFillPreSections[s];
                if (pre == null) continue;
                net.minecraft.world.chunk.storage.ExtendedBlockStorage ebs =
                    (storages != null && s < storages.length) ? storages[s] : null;
                java.util.Arrays.fill(postPacked, 0);
                if (ebs != null) {
                    for (int i = 0; i < 4096; ++i) {
                        int lx = i & 15;
                        int lz = (i >> 4) & 15;
                        int ly = (i >> 8) & 15;
                        net.minecraft.block.state.IBlockState st = ebs.get(lx, ly, lz);
                        if (st == null) continue;
                        net.minecraft.block.Block blk = st.getBlock();
                        int id = net.minecraft.block.Block.getIdFromBlock(blk);
                        int meta = blk.getMetaFromState(st) & 15;
                        postPacked[i] = ChunkFillDiff.pack(id, meta);
                    }
                }
                sectionDiffs.clear();
                ChunkFillDiff.appendSectionDiffs(
                    sectionDiffs, recFillChunkX, recFillChunkZ, s, pre, postPacked);
                for (int di = 0; di < sectionDiffs.size(); ++di) {
                    int[] e = sectionDiffs.get(di);
                    recordBlockChangePacked(e[0], e[1], e[2], e[3], e[4]);
                }
            }
        } finally {
            abortChunkFillCapture();
        }
    }

    private void applyPinnedPortalPhase(Minecraft mc) {
        if (pinPortalPhase < 0 || mc == null || mc.entityRenderer == null) return;
        try {
            java.lang.reflect.Field fPhase =
                net.minecraft.client.renderer.EntityRenderer.class
                    .getDeclaredField("rendererUpdateCount");
            fPhase.setAccessible(true);
            fPhase.setInt(mc.entityRenderer, pinPortalPhase);
        } catch (Throwable ig) { /* leave live phase */ }
    }

    /** Re-apply sticky timeInPortal so GuiIngame.renderPortal alpha is identical
     * on every frame{} re-render despite free-running client decay. */
    private void applyPinnedPortalTime(Minecraft mc) {
        if (!pinPortalTimeActive || mc == null || mc.player == null) return;
        EntityPlayerSP p = mc.player;
        p.timeInPortal = pinPortalTime;
        p.prevTimeInPortal = pinPortalTime;
    }

    /** Restore EntityRenderer fogColor1/2 so frame_pair pass B starts from the
     * same smoother state as pass A. updateFogColor / updateRenderer both write
     * these; two re-renders on one turn otherwise step the 0.1F lerp twice. */
    private void restoreFogColors(Minecraft mc, float c1, float c2) {
        if (mc == null || mc.entityRenderer == null) return;
        try {
            java.lang.reflect.Field f1 =
                net.minecraft.client.renderer.EntityRenderer.class
                    .getDeclaredField("fogColor1");
            f1.setAccessible(true);
            f1.setFloat(mc.entityRenderer, c1);
            java.lang.reflect.Field f2 =
                net.minecraft.client.renderer.EntityRenderer.class
                    .getDeclaredField("fogColor2");
            f2.setAccessible(true);
            f2.setFloat(mc.entityRenderer, c2);
        } catch (Throwable ig) { /* leave live fog */ }
    }

    /** Per-pass renderer fields that must not drift inside frame_pair. */
    private JsonObject framePassDiag(Minecraft mc) {
        JsonObject d = new JsonObject();
        try {
            if (mc.entityRenderer != null) {
                Class<?> erc = net.minecraft.client.renderer.EntityRenderer.class;
                String[] names = {
                    "fogColor1", "fogColor2",
                    "fogColorRed", "fogColorGreen", "fogColorBlue"
                };
                for (int i = 0; i < names.length; i++) {
                    java.lang.reflect.Field f = erc.getDeclaredField(names[i]);
                    f.setAccessible(true);
                    d.addProperty(names[i], f.getFloat(mc.entityRenderer));
                }
                java.lang.reflect.Field fPhase = erc.getDeclaredField("rendererUpdateCount");
                fPhase.setAccessible(true);
                d.addProperty("rendererUpdateCount", fPhase.getInt(mc.entityRenderer));
                try {
                    java.lang.reflect.Field fCol = erc.getDeclaredField("lightmapColors");
                    fCol.setAccessible(true);
                    int[] colors = (int[]) fCol.get(mc.entityRenderer);
                    int h = 1;
                    if (colors != null) {
                        for (int i = 0; i < colors.length; i++) h = 31 * h + colors[i];
                        d.addProperty("lightmap_n", colors.length);
                    }
                    d.addProperty("lightmap_hash", h);
                } catch (Throwable ig) {}
            }
            if (mc.player != null) {
                d.addProperty("timeInPortal", mc.player.timeInPortal);
                d.addProperty("prevTimeInPortal", mc.player.prevTimeInPortal);
                d.addProperty("air", mc.player.getAir());
                d.addProperty("x", mc.player.posX);
                d.addProperty("y", mc.player.posY);
                d.addProperty("z", mc.player.posZ);
                d.addProperty("yaw", mc.player.rotationYaw);
                d.addProperty("pitch", mc.player.rotationPitch);
            }
            try {
                net.minecraft.client.renderer.texture.TextureAtlasSprite sp =
                    mc.getBlockRendererDispatcher().getBlockModelShapes()
                        .getTexture(net.minecraft.init.Blocks.PORTAL.getDefaultState());
                if (sp != null) {
                    d.addProperty("portal_minU", sp.getMinU());
                    d.addProperty("portal_maxU", sp.getMaxU());
                    d.addProperty("portal_minV", sp.getMinV());
                    d.addProperty("portal_maxV", sp.getMaxV());
                    try {
                        java.lang.reflect.Field fc =
                            net.minecraft.client.renderer.texture.TextureAtlasSprite.class
                                .getDeclaredField("frameCounter");
                        fc.setAccessible(true);
                        d.addProperty("portal_frameCounter", fc.getInt(sp));
                    } catch (Throwable ig) {}
                    try {
                        java.lang.reflect.Field tc =
                            net.minecraft.client.renderer.texture.TextureAtlasSprite.class
                                .getDeclaredField("tickCounter");
                        tc.setAccessible(true);
                        d.addProperty("portal_tickCounter", tc.getInt(sp));
                    } catch (Throwable ig) {}
                }
            } catch (Throwable ig) {}
        } catch (Throwable t) {
            d.addProperty("diag_error", String.valueOf(t));
        }
        return d;
    }

    /**
     * Oracle translucent DRAW capture for the slime-rim contradiction.
     * Rebuilds every RenderChunk overlapping the requested block bbox (default:
     * 16-block radius around the player at the player's Y section), dumps:
     *   camera.json          pose + GL modelview/projection after setupCameraTransform
     *   model_census.json    per-slime baked quads (general vs face) + shouldSideBeRendered
     *   chunks.json          per-section origin, layer vertex counts, sort camera
     *   quads.jsonl          TRANSLUCENT layer post-sortVertexData quads in draw order
     * Draw order is vanilla's: descending squared centroid distance, stable TimSort.
     * Fragment visit order is derived offline from quads + MVP (Python driver).
     */
    private void captureTranslucentDraw(Minecraft mc, JsonObject action, Req r) {
        if (mc.world == null || mc.player == null || mc.renderGlobal == null
                || mc.entityRenderer == null) {
            r.resp.offer(err("no world"));
            return;
        }
        try {
            final String dir = action.has("dir") ? action.get("dir").getAsString()
                : "verify/fixtures/slime_translucent";
            int px = (int) Math.floor(mc.player.posX);
            int py = (int) Math.floor(mc.player.posY);
            int pz = (int) Math.floor(mc.player.posZ);
            int x0 = action.has("x0") ? action.get("x0").getAsInt() : px - 16;
            int y0 = action.has("y0") ? action.get("y0").getAsInt() : py - 2;
            int z0 = action.has("z0") ? action.get("z0").getAsInt() : pz - 16;
            int x1 = action.has("x1") ? action.get("x1").getAsInt() : px + 16;
            int y1 = action.has("y1") ? action.get("y1").getAsInt() : py + 2;
            int z1 = action.has("z1") ? action.get("z1").getAsInt() : pz + 16;
            java.io.File gd = new java.io.File(dir);
            gd.mkdirs();

            float camX = (float) mc.player.posX;
            float camY = (float) mc.player.posY + mc.player.getEyeHeight();
            float camZ = (float) mc.player.posZ;
            float yaw = mc.player.rotationYaw;
            float pitch = mc.player.rotationPitch;

            JsonObject cam = new JsonObject();
            cam.addProperty("feet_x", mc.player.posX);
            cam.addProperty("feet_y", mc.player.posY);
            cam.addProperty("feet_z", mc.player.posZ);
            cam.addProperty("eye_x", camX);
            cam.addProperty("eye_y", camY);
            cam.addProperty("eye_z", camZ);
            cam.addProperty("yaw", yaw);
            cam.addProperty("pitch", pitch);
            cam.addProperty("eye_height", mc.player.getEyeHeight());
            cam.addProperty("sneaking", mc.player.isSneaking());
            cam.addProperty("display_w", mc.displayWidth);
            cam.addProperty("display_h", mc.displayHeight);
            cam.addProperty("partial_ticks", 1.0F);
            try {
                mc.entityRenderer.setupCameraTransform(1.0F, 0);
                java.nio.FloatBuffer mv = org.lwjgl.BufferUtils.createFloatBuffer(16);
                java.nio.FloatBuffer pr = org.lwjgl.BufferUtils.createFloatBuffer(16);
                org.lwjgl.opengl.GL11.glGetFloat(
                    org.lwjgl.opengl.GL11.GL_MODELVIEW_MATRIX, mv);
                org.lwjgl.opengl.GL11.glGetFloat(
                    org.lwjgl.opengl.GL11.GL_PROJECTION_MATRIX, pr);
                JsonArray mvA = new JsonArray();
                JsonArray prA = new JsonArray();
                for (int i = 0; i < 16; i++) {
                    mvA.add(new com.google.gson.JsonPrimitive(mv.get(i)));
                    prA.add(new com.google.gson.JsonPrimitive(pr.get(i)));
                }
                cam.add("modelview", mvA);
                cam.add("projection", prA);
            } catch (Throwable ig) {
                cam.addProperty("matrix_error", String.valueOf(ig));
            }

            net.minecraft.client.renderer.RenderGlobal rg = mc.renderGlobal;
            java.lang.reflect.Field vfF = net.minecraft.client.renderer.RenderGlobal.class
                .getDeclaredField("viewFrustum");
            vfF.setAccessible(true);
            net.minecraft.client.renderer.ViewFrustum vf =
                (net.minecraft.client.renderer.ViewFrustum) vfF.get(rg);
            if (vf == null || vf.renderChunks == null) {
                r.resp.offer(err("no viewFrustum"));
                return;
            }

            java.lang.reflect.Field rib = net.minecraft.client.renderer.VertexBuffer.class
                .getDeclaredField("rawIntBuffer");
            rib.setAccessible(true);

            java.util.ArrayList<net.minecraft.client.renderer.chunk.RenderChunk> hit =
                new java.util.ArrayList<net.minecraft.client.renderer.chunk.RenderChunk>();
            for (net.minecraft.client.renderer.chunk.RenderChunk c : vf.renderChunks) {
                if (c == null) continue;
                BlockPos o = c.getPosition();
                int cx0 = o.getX(), cy0 = o.getY(), cz0 = o.getZ();
                int cx1 = cx0 + 15, cy1 = cy0 + 15, cz1 = cz0 + 15;
                if (cx1 < x0 || cx0 > x1 || cy1 < y0 || cy0 > y1 || cz1 < z0 || cz0 > z1)
                    continue;
                hit.add(c);
            }

            JsonArray chunks = new JsonArray();
            java.io.PrintWriter qw = new java.io.PrintWriter(new java.io.BufferedWriter(
                new java.io.FileWriter(new java.io.File(gd, "quads.jsonl"))));
            int totalTransVerts = 0, totalQuads = 0, nChunks = 0;
            for (int ci = 0; ci < hit.size(); ci++) {
                net.minecraft.client.renderer.chunk.RenderChunk rc = hit.get(ci);
                BlockPos origin = rc.getPosition();
                net.minecraft.client.renderer.chunk.ChunkCompileTaskGenerator gen =
                    rc.makeCompileTaskChunk();
                net.minecraft.client.renderer.RegionRenderCacheBuilder rb =
                    new net.minecraft.client.renderer.RegionRenderCacheBuilder();
                gen.setRegionRenderCacheBuilder(rb);
                gen.setStatus(net.minecraft.client.renderer.chunk.ChunkCompileTaskGenerator.Status.COMPILING);
                rc.rebuildChunk(camX, camY, camZ, gen);
                JsonObject ch = new JsonObject();
                ch.addProperty("ox", origin.getX());
                ch.addProperty("oy", origin.getY());
                ch.addProperty("oz", origin.getZ());
                JsonArray layers = new JsonArray();
                for (int layer = 0; layer < 4; layer++) {
                    net.minecraft.client.renderer.VertexBuffer buf =
                        rb.getWorldRendererByLayerId(layer);
                    int vc = buf.getVertexCount();
                    JsonObject ly = new JsonObject();
                    ly.addProperty("layer", layer);
                    ly.addProperty("vertexCount", vc);
                    layers.add(ly);
                    if (layer != 3) continue;
                    totalTransVerts += vc;
                    java.nio.IntBuffer ib = (java.nio.IntBuffer) rib.get(buf);
                    int nq = vc / 4;
                    for (int qi = 0; qi < nq; qi++) {
                        int base = qi * 28;
                        float[] xs = new float[4], ys = new float[4], zs = new float[4];
                        JsonArray verts = new JsonArray();
                        for (int v = 0; v < 4; v++) {
                            int off = base + v * 7;
                            xs[v] = Float.intBitsToFloat(ib.get(off + 0));
                            ys[v] = Float.intBitsToFloat(ib.get(off + 1));
                            zs[v] = Float.intBitsToFloat(ib.get(off + 2));
                            int packed = ib.get(off + 3);
                            float u = Float.intBitsToFloat(ib.get(off + 4));
                            float vv = Float.intBitsToFloat(ib.get(off + 5));
                            int lm = ib.get(off + 6);
                            JsonObject vt = new JsonObject();
                            vt.addProperty("x", xs[v]);
                            vt.addProperty("y", ys[v]);
                            vt.addProperty("z", zs[v]);
                            vt.addProperty("color", packed);
                            vt.addProperty("r", packed & 255);
                            vt.addProperty("g", (packed >> 8) & 255);
                            vt.addProperty("b", (packed >> 16) & 255);
                            vt.addProperty("a", (packed >> 24) & 255);
                            vt.addProperty("u", u);
                            vt.addProperty("v", vv);
                            vt.addProperty("light", lm);
                            verts.add(vt);
                        }
                        float cx = 0.25F * (xs[0] + xs[1] + xs[2] + xs[3]);
                        float cy = 0.25F * (ys[0] + ys[1] + ys[2] + ys[3]);
                        float cz = 0.25F * (zs[0] + zs[1] + zs[2] + zs[3]);
                        float dx = cx - camX, dy = cy - camY, dz = cz - camZ;
                        float dist2 = dx * dx + dy * dy + dz * dz;
                        int bx = (int) Math.floor(cx);
                        int by = (int) Math.floor(cy);
                        int bz = (int) Math.floor(cz);
                        int bid = 0;
                        try {
                            net.minecraft.block.state.IBlockState st =
                                mc.world.getBlockState(new BlockPos(bx, by, bz));
                            bid = net.minecraft.block.Block.getIdFromBlock(st.getBlock());
                        } catch (Throwable ig) {}
                        JsonObject q = new JsonObject();
                        q.addProperty("chunk", ci);
                        q.addProperty("ox", origin.getX());
                        q.addProperty("oy", origin.getY());
                        q.addProperty("oz", origin.getZ());
                        q.addProperty("draw_i", totalQuads);
                        q.addProperty("qi", qi);
                        q.addProperty("dist2", dist2);
                        q.addProperty("cx", cx);
                        q.addProperty("cy", cy);
                        q.addProperty("cz", cz);
                        q.addProperty("bx", bx);
                        q.addProperty("by", by);
                        q.addProperty("bz", bz);
                        q.addProperty("block_id", bid);
                        q.add("verts", verts);
                        qw.println(q.toString());
                        totalQuads++;
                    }
                }
                ch.add("layers", layers);
                chunks.add(ch);
                nChunks++;
            }
            qw.flush();
            qw.close();

            // Per-slime baked-model census: generalQuads vs face quads vs Breakable cull.
            net.minecraft.client.renderer.BlockRendererDispatcher brd =
                mc.getBlockRendererDispatcher();
            JsonArray census = new JsonArray();
            int nSlime = 0, nGeneral = 0, nFace = 0;
            net.minecraft.util.math.BlockPos.MutableBlockPos bp =
                new net.minecraft.util.math.BlockPos.MutableBlockPos();
            for (int x = x0; x <= x1; x++)
              for (int y = y0; y <= y1; y++)
                for (int z = z0; z <= z1; z++) {
                    bp.setPos(x, y, z);
                    net.minecraft.block.state.IBlockState st = mc.world.getBlockState(bp);
                    if (st.getBlock() != net.minecraft.init.Blocks.SLIME) continue;
                    nSlime++;
                    net.minecraft.client.renderer.block.model.IBakedModel model =
                        brd.getModelForState(st);
                    java.util.List<net.minecraft.client.renderer.block.model.BakedQuad> genq =
                        model.getQuads(st, null, 0L);
                    nGeneral += genq.size();
                    JsonObject row = new JsonObject();
                    row.addProperty("x", x);
                    row.addProperty("y", y);
                    row.addProperty("z", z);
                    row.addProperty("general_quads", genq.size());
                    JsonObject faces = new JsonObject();
                    JsonObject side = new JsonObject();
                    JsonObject nbr = new JsonObject();
                    int faceCount = 0;
                    for (net.minecraft.util.EnumFacing f : net.minecraft.util.EnumFacing.values()) {
                        java.util.List<net.minecraft.client.renderer.block.model.BakedQuad> fq =
                            model.getQuads(st, f, 0L);
                        faces.addProperty(f.getName(), fq.size());
                        faceCount += fq.size();
                        boolean ssr = st.shouldSideBeRendered(mc.world, bp, f);
                        side.addProperty(f.getName(), ssr);
                        BlockPos np = bp.offset(f);
                        int nid = net.minecraft.block.Block.getIdFromBlock(
                            mc.world.getBlockState(np).getBlock());
                        nbr.addProperty(f.getName(), nid);
                    }
                    nFace += faceCount;
                    row.add("face_quads", faces);
                    row.add("should_side", side);
                    row.add("neighbor_id", nbr);
                    row.addProperty("layer", st.getBlock().getBlockLayer().toString());
                    census.add(row);
                }

            java.io.FileWriter cw = new java.io.FileWriter(new java.io.File(gd, "camera.json"));
            cw.write(cam.toString());
            cw.close();
            JsonObject chunkWrap = new JsonObject();
            chunkWrap.addProperty("n", nChunks);
            chunkWrap.addProperty("sort_cam_x", camX);
            chunkWrap.addProperty("sort_cam_y", camY);
            chunkWrap.addProperty("sort_cam_z", camZ);
            chunkWrap.add("chunks", chunks);
            java.io.FileWriter chw = new java.io.FileWriter(new java.io.File(gd, "chunks.json"));
            chw.write(chunkWrap.toString());
            chw.close();
            JsonObject cen = new JsonObject();
            cen.addProperty("n_slime", nSlime);
            cen.addProperty("n_general_quads", nGeneral);
            cen.addProperty("n_face_quads", nFace);
            cen.addProperty("bbox",
                x0 + "," + y0 + "," + z0 + ".." + x1 + "," + y1 + "," + z1);
            cen.add("blocks", census);
            java.io.FileWriter nw = new java.io.FileWriter(new java.io.File(gd, "model_census.json"));
            nw.write(cen.toString());
            nw.close();

            JsonObject o = new JsonObject();
            o.addProperty("ok", true);
            o.addProperty("dir", gd.getAbsolutePath());
            o.addProperty("n_chunks", nChunks);
            o.addProperty("n_translucent_verts", totalTransVerts);
            o.addProperty("n_translucent_quads", totalQuads);
            o.addProperty("n_slime", nSlime);
            o.addProperty("n_general_quads", nGeneral);
            o.addProperty("n_face_quads", nFace);
            o.addProperty("sort_cam_x", camX);
            o.addProperty("sort_cam_y", camY);
            o.addProperty("sort_cam_z", camZ);
            System.out.println("[qrl] capture_translucent_draw chunks=" + nChunks
                + " transQuads=" + totalQuads + " slime=" + nSlime
                + " general=" + nGeneral + " face=" + nFace + " -> " + dir);
            r.resp.offer(o.toString());
        } catch (Throwable t) {
            System.out.println("[qrl] capture_translucent_draw failed: " + t);
            java.io.StringWriter swr = new java.io.StringWriter();
            t.printStackTrace(new java.io.PrintWriter(swr));
            System.out.println(swr.toString());
            r.resp.offer(err("capture_translucent_draw: " + t));
        }
    }

    /** Snapshot of client entity fields restored after frame{} re-render. */
    private static final class RenderPinSnap {
        net.minecraft.entity.Entity entity;
        boolean isSlime;
        boolean isDragon;
        boolean isXp;
        float squishAmount, squishFactor, prevSquishFactor;
        int deathTicks;
        float health;
        int xpColor, xpAge, xpValue, xpDelay;
        double posX, posY, posZ;
        double prevPosX, prevPosY, prevPosZ;
        double lastTickPosX, lastTickPosY, lastTickPosZ;
        double motionX, motionY, motionZ;
    }

    /** Match just-spawned server entity on the client world: UUID, then eid, then
     * position+type within 2 blocks. */
    private net.minecraft.entity.Entity findClientRenderPinEntity(Minecraft mc) {
        if (mc == null || mc.world == null || !renderPinActive) return null;
        java.util.UUID want = renderPinUuid;
        if (want != null) {
            for (net.minecraft.entity.Entity e : mc.world.loadedEntityList) {
                if (e != null && want.equals(e.getUniqueID()) && typeMatchesRenderPin(e))
                    return e;
            }
        }
        if (renderPinEid >= 0) {
            net.minecraft.entity.Entity byId = mc.world.getEntityByID(renderPinEid);
            if (byId != null && typeMatchesRenderPin(byId)) return byId;
        }
        net.minecraft.entity.Entity best = null;
        double bestD = 4.0 * 4.0; // 4-block radius
        for (net.minecraft.entity.Entity e : mc.world.loadedEntityList) {
            if (e == null || !typeMatchesRenderPin(e)) continue;
            double dx = e.posX - renderPinX;
            double dy = e.posY - renderPinY;
            double dz = e.posZ - renderPinZ;
            double d = dx * dx + dy * dy + dz * dz;
            if (d < bestD) { bestD = d; best = e; }
        }
        return best;
    }

    private boolean typeMatchesRenderPin(net.minecraft.entity.Entity e) {
        if (renderPinKind == null) return false;
        String k = renderPinKind.toLowerCase();
        if (k.contains("magma"))
            return e instanceof net.minecraft.entity.monster.EntityMagmaCube;
        if (k.equals("slime"))
            return e instanceof net.minecraft.entity.monster.EntitySlime
                && !(e instanceof net.minecraft.entity.monster.EntityMagmaCube);
        if (k.contains("dragon") && !k.contains("fireball"))
            return e instanceof net.minecraft.entity.boss.EntityDragon;
        if (k.contains("xp") || k.equals("xporb"))
            return e instanceof net.minecraft.entity.item.EntityXPOrb;
        return false;
    }

    /** Write squishFactor/prevSquishFactor/squishAmount, deathTicks, or XP orb
     * color/age/pose onto the CLIENT entity the renderer reads. For dragon,
     * keep health > 0 so onDeathUpdate does not advance deathTicks. */
    private void applyClientEntityRenderFields(net.minecraft.entity.Entity e) {
        if (e instanceof net.minecraft.entity.monster.EntitySlime) {
            net.minecraft.entity.monster.EntitySlime sl =
                (net.minecraft.entity.monster.EntitySlime) e;
            float sq = renderPinSquish;
            sl.squishAmount = sq;
            sl.squishFactor = sq;
            sl.prevSquishFactor = sq;
            // Size is data-manager synced from server; re-assert if pin carries size.
            if (renderPinSize > 0) {
                try {
                    java.lang.reflect.Method m =
                        net.minecraft.entity.monster.EntitySlime.class
                            .getDeclaredMethod("setSlimeSize", int.class, boolean.class);
                    m.setAccessible(true);
                    m.invoke(sl, Integer.valueOf(renderPinSize), Boolean.FALSE);
                } catch (Throwable ig) {}
            }
        } else if (e instanceof net.minecraft.entity.boss.EntityDragon) {
            net.minecraft.entity.boss.EntityDragon dr =
                (net.minecraft.entity.boss.EntityDragon) e;
            // Keep alive so client onDeathUpdate does not ++deathTicks / spin yaw.
            try {
                if (dr.getHealth() <= 0.0F) dr.setHealth(dr.getMaxHealth());
            } catch (Throwable ig) {}
            dr.deathTicks = renderPinDeathTicks;
            dr.motionX = dr.motionY = dr.motionZ = 0.0D;
        } else if (e instanceof net.minecraft.entity.item.EntityXPOrb) {
            net.minecraft.entity.item.EntityXPOrb orb =
                (net.minecraft.entity.item.EntityXPOrb) e;
            // Freeze render inputs: xpColor drives sin-phase tint, xpOrbAge is
            // lifetime, delay blocks attraction. Pose must not interpolate.
            orb.xpColor = renderPinXpColor;
            orb.xpOrbAge = renderPinXpAge;
            if (renderPinXpValue > 0) orb.xpValue = renderPinXpValue;
            orb.delayBeforeCanPickup = 32767;
            try { orb.setNoGravity(true); } catch (Throwable ig) {}
            orb.motionX = orb.motionY = orb.motionZ = 0.0D;
            orb.setPosition(renderPinX, renderPinY, renderPinZ);
            orb.prevPosX = renderPinX;
            orb.prevPosY = renderPinY;
            orb.prevPosZ = renderPinZ;
            orb.lastTickPosX = renderPinX;
            orb.lastTickPosY = renderPinY;
            orb.lastTickPosZ = renderPinZ;
            orb.isDead = false;
        }
    }

    /** Apply pending pin; return snapshot for restore, or null if no match. */
    private RenderPinSnap applyPendingRenderPin(Minecraft mc) {
        net.minecraft.entity.Entity e = findClientRenderPinEntity(mc);
        if (e == null) return null;
        RenderPinSnap snap = new RenderPinSnap();
        snap.entity = e;
        if (e instanceof net.minecraft.entity.monster.EntitySlime) {
            net.minecraft.entity.monster.EntitySlime sl =
                (net.minecraft.entity.monster.EntitySlime) e;
            snap.isSlime = true;
            snap.squishAmount = sl.squishAmount;
            snap.squishFactor = sl.squishFactor;
            snap.prevSquishFactor = sl.prevSquishFactor;
        } else if (e instanceof net.minecraft.entity.boss.EntityDragon) {
            net.minecraft.entity.boss.EntityDragon dr =
                (net.minecraft.entity.boss.EntityDragon) e;
            snap.isDragon = true;
            snap.deathTicks = dr.deathTicks;
            snap.health = dr.getHealth();
        } else if (e instanceof net.minecraft.entity.item.EntityXPOrb) {
            net.minecraft.entity.item.EntityXPOrb orb =
                (net.minecraft.entity.item.EntityXPOrb) e;
            snap.isXp = true;
            snap.xpColor = orb.xpColor;
            snap.xpAge = orb.xpOrbAge;
            snap.xpValue = orb.xpValue;
            snap.xpDelay = orb.delayBeforeCanPickup;
            snap.posX = orb.posX; snap.posY = orb.posY; snap.posZ = orb.posZ;
            snap.prevPosX = orb.prevPosX; snap.prevPosY = orb.prevPosY;
            snap.prevPosZ = orb.prevPosZ;
            snap.lastTickPosX = orb.lastTickPosX;
            snap.lastTickPosY = orb.lastTickPosY;
            snap.lastTickPosZ = orb.lastTickPosZ;
            snap.motionX = orb.motionX; snap.motionY = orb.motionY;
            snap.motionZ = orb.motionZ;
        } else {
            return null;
        }
        applyClientEntityRenderFields(e);
        return snap;
    }

    private void restoreRenderPin(RenderPinSnap snap) {
        if (snap == null || snap.entity == null) return;
        if (snap.isSlime && snap.entity instanceof net.minecraft.entity.monster.EntitySlime) {
            net.minecraft.entity.monster.EntitySlime sl =
                (net.minecraft.entity.monster.EntitySlime) snap.entity;
            sl.squishAmount = snap.squishAmount;
            sl.squishFactor = snap.squishFactor;
            sl.prevSquishFactor = snap.prevSquishFactor;
        } else if (snap.isDragon
                && snap.entity instanceof net.minecraft.entity.boss.EntityDragon) {
            net.minecraft.entity.boss.EntityDragon dr =
                (net.minecraft.entity.boss.EntityDragon) snap.entity;
            dr.deathTicks = snap.deathTicks;
            try { dr.setHealth(snap.health); } catch (Throwable ig) {}
        } else if (snap.isXp
                && snap.entity instanceof net.minecraft.entity.item.EntityXPOrb) {
            net.minecraft.entity.item.EntityXPOrb orb =
                (net.minecraft.entity.item.EntityXPOrb) snap.entity;
            orb.xpColor = snap.xpColor;
            orb.xpOrbAge = snap.xpAge;
            orb.xpValue = snap.xpValue;
            orb.delayBeforeCanPickup = snap.xpDelay;
            orb.motionX = snap.motionX;
            orb.motionY = snap.motionY;
            orb.motionZ = snap.motionZ;
            orb.setPosition(snap.posX, snap.posY, snap.posZ);
            orb.prevPosX = snap.prevPosX;
            orb.prevPosY = snap.prevPosY;
            orb.prevPosZ = snap.prevPosZ;
            orb.lastTickPosX = snap.lastTickPosX;
            orb.lastTickPosY = snap.lastTickPosY;
            orb.lastTickPosZ = snap.lastTickPosZ;
        }
    }

    /**
     * Flush every loaded dimension to disk and copy the save next to the tape.
     * Replay loads the ACTUAL world instead of trusting regenerated worldgen,
     * which kills the populate-order/provenance divergence class at the source.
     *
     * addOnly=false (recstart): full copy, overwriting.  addOnly=true
     * (recstop): copy only paths the snapshot does NOT already have, so the
     * recstart truth for the starting dimension, level.dat and playerdata is
     * never overwritten with end-of-session state (dug blocks, spent items),
     * while dimensions that came into existence during the recording - which
     * cannot be in the recstart copy, because they had no region files yet -
     * are added.  Returns the number of files added/copied.
     */
    private static int snapshotSaveDir(Minecraft mc, java.io.File snapRoot,
                                       final boolean addOnly) throws Exception {
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
        final java.nio.file.Path src = saveDir.toPath(), dst = snapRoot.toPath();
        final int[] copied = {0};
        java.nio.file.Files.walkFileTree(src, new java.nio.file.SimpleFileVisitor<java.nio.file.Path>() {
            @Override public java.nio.file.FileVisitResult preVisitDirectory(
                    java.nio.file.Path d, java.nio.file.attribute.BasicFileAttributes a) throws java.io.IOException {
                java.nio.file.Files.createDirectories(dst.resolve(src.relativize(d)));
                return java.nio.file.FileVisitResult.CONTINUE;
            }
            @Override public java.nio.file.FileVisitResult visitFile(
                    java.nio.file.Path f, java.nio.file.attribute.BasicFileAttributes a) throws java.io.IOException {
                java.nio.file.Path target = dst.resolve(src.relativize(f));
                if (addOnly && java.nio.file.Files.exists(target))
                    return java.nio.file.FileVisitResult.CONTINUE;
                java.nio.file.Files.copy(f, target,
                    java.nio.file.StandardCopyOption.REPLACE_EXISTING);
                ++copied[0];
                return java.nio.file.FileVisitResult.CONTINUE;
            }
            @Override public java.nio.file.FileVisitResult visitFileFailed(
                    java.nio.file.Path f, java.io.IOException e) {
                return java.nio.file.FileVisitResult.CONTINUE; // session.lock etc.
            }
        });
        return copied[0];
    }

    /** EntityRenderer.fogColor1 at recstart, or -1 when it cannot be read.
     * -1 (not 0) so a replay can tell "unknown, use the steady-state seed"
     * apart from a genuine "the client had just started and it really is 0". */
    private static float recFogColor1(Minecraft mc) {
        if (mc == null || mc.entityRenderer == null) return -1.0F;
        try {
            java.lang.reflect.Field f =
                net.minecraft.client.renderer.EntityRenderer.class
                    .getDeclaredField("fogColor1");
            f.setAccessible(true);
            return f.getFloat(mc.entityRenderer);
        } catch (Throwable ig) { return -1.0F; }
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
                "../../render-opt/dropin/ao/qao_mode.txt"))).trim();
            if ("native".equals(m) || "sabotage".equals(m) || "vanilla".equals(m)) {
                net.minecraftforge.common.ForgeModContainer.forgeLightPipelineEnabled = false;
                System.err.println("[qao] Forge light pipeline DISABLED (vanilla getAoBrightness live) for qao_mode=" + m);
            }
        } catch (Exception ex) { /* no sidecar: leave Forge pipeline as-is */ }

        launchCfg = loadLaunchCfg();
        try {
            // -Dqrl.port is the batch escape hatch (gradle -PqrlPort, set by mc_cli.py when
            // it fans out N instances that all share one qrl_launch.json); the json's "port"
            // is the normal single-instance source.
            // Frozen legacy system property name (gradle -PqrlPort -> -Dqrl.port).
            String pe = System.getProperty("qrl.port");
            if (pe != null && !pe.isEmpty()) PORT = Integer.parseInt(pe);
            else if (launchCfg != null && launchCfg.has("port")) PORT = launchCfg.get("port").getAsInt();
        } catch (Exception ex) { System.out.println("[qrl] bad port override: " + ex); }

        WorldGenProbe.install(launchCfg);
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
                } finally {
                    // A bridge that dies while the step lock is armed would leave
                    // the server thread parked forever. Always release it.
                    lockDisarm();
                }
            }
        } catch (Exception ex) {
            System.out.println("[qrl] server failed: " + ex.getMessage());
        }
    }

    // ================= server tick lockstep gate =================
    // State machine (see the field block near the top for why this exists).
    //   disarmed          : armed=0                    server free-runs
    //   parked            : armed=1 parked=1 permits=0 in_flight=0
    //   permitted         : armed=1 parked=0 permits>0
    //   executing a tick  : armed=1 in_flight=1
    // Only the "parked" state admits a *_locked command, and step_server_locked
    // returns only after the machine is back in "parked" with completed==target.

    /** ServerTickEvent.START: consume exactly one permit, or park until one arrives. */
    private void lockAwaitPermit() {
        synchronized (lockMon) {
            if (!lockArmed) { lockParked = false; lockInFlight = false; return; }
            lockParked = true;
            lockMon.notifyAll();
            while (lockArmed && lockPermits == 0) {
                try { lockMon.wait(); }
                catch (InterruptedException ie) {
                    Thread.currentThread().interrupt();
                    lockArmed = false; lockParked = false; lockInFlight = false;
                    lockMon.notifyAll();
                    return;
                }
            }
            lockParked = false;
            if (lockArmed) { lockPermits--; lockInFlight = true; }
            else lockInFlight = false;
        }
    }

    /** ServerTickEvent.END: the permitted tick has fully executed. */
    private void lockFinishTick() {
        synchronized (lockMon) {
            if (lockInFlight) { lockCompleted++; lockInFlight = false; }
            lockMon.notifyAll();
        }
    }

    /** Release the gate unconditionally (bridge disconnect / shutdown safety). */
    private void lockDisarm() {
        synchronized (lockMon) {
            if (!lockArmed) return;
            lockArmed = false; lockPermits = 0;
            lockMon.notifyAll();
            System.out.println("[qrl] server step lock released");
        }
    }

    /** Caller must hold lockMon. null = the gate is parked and a locked cmd may run. */
    private String lockRequireParked(String cmd) {
        if (!lockArmed) return err(cmd + " requires server_step_lock");
        if (!lockParked || lockInFlight || lockPermits != 0)
            return err(cmd + " requires a parked server (parked=" + lockParked
                + " in_flight=" + lockInFlight + " permits=" + lockPermits + ")");
        return null;
    }

    /** server_step_lock / server_step_unlock. Arming blocks until the server parks. */
    private String lockControl(String cmd, JsonObject action) {
        boolean on = !cmd.equals("server_step_unlock");
        if (on && action.has("on")) on = action.get("on").getAsInt() != 0;
        synchronized (lockMon) {
            if (!on) {
                lockArmed = false; lockPermits = 0;
                lockMon.notifyAll();
                JsonObject o = new JsonObject();
                o.addProperty("ok", true);
                o.addProperty("armed", false);
                o.addProperty("completed", lockCompleted);
                return o.toString();
            }
            if (lockInFlight || lockPermits != 0)
                return err("cannot arm the step lock with a permitted tick in flight");
            lockArmed = true;
            lockMon.notifyAll();
            long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(10);
            while (!lockParked) {
                long remaining = deadline - System.nanoTime();
                if (remaining <= 0) {
                    lockArmed = false;
                    lockMon.notifyAll();
                    return err("server did not reach a tick boundary within 10s");
                }
                try { TimeUnit.NANOSECONDS.timedWait(lockMon, remaining); }
                catch (InterruptedException ie) {
                    Thread.currentThread().interrupt();
                    lockArmed = false; lockMon.notifyAll();
                    return err("interrupted while arming the step lock");
                }
            }
            System.out.println("[qrl] server step lock armed at completed=" + lockCompleted);
            JsonObject o = new JsonObject();
            o.addProperty("ok", true);
            o.addProperty("armed", true);
            o.addProperty("parked", true);
            o.addProperty("completed", lockCompleted);
            lockAddPlayer(o);
            return o.toString();
        }
    }

    /**
     * step_server_locked: permit exactly action.n server ticks and return only
     * once the server has executed all of them AND re-parked at the following
     * START. n=0 is a legal no-op that just re-asserts the parked boundary.
     */
    private String lockStepServer(JsonObject action) {
        int n = action.has("n") ? action.get("n").getAsInt() : 1;
        if (n < 0 || n > 100000) return err("step_server_locked: n must be 0..100000");
        long timeoutMs = action.has("timeout_ms")
            ? action.get("timeout_ms").getAsLong() : Math.min(110000L, 10000L + 400L * n);
        synchronized (lockMon) {
            String bad = lockRequireParked("step_server_locked");
            if (bad != null) return bad;
            long target = lockCompleted + n;
            if (n > 0) { lockPermits = n; lockMon.notifyAll(); }
            long deadline = System.nanoTime() + TimeUnit.MILLISECONDS.toNanos(timeoutMs);
            while (lockArmed && (lockCompleted < target || !lockParked)) {
                long remaining = deadline - System.nanoTime();
                if (remaining <= 0)
                    return err("step_server_locked timed out at completed=" + lockCompleted
                        + " target=" + target + " parked=" + lockParked);
                try { TimeUnit.NANOSECONDS.timedWait(lockMon, remaining); }
                catch (InterruptedException ie) {
                    Thread.currentThread().interrupt();
                    return err("interrupted while stepping the server");
                }
            }
            if (!lockArmed) return err("step lock was released during step_server_locked");
            if (lockCompleted != target || !lockParked)
                return err("step lock overshot: completed=" + lockCompleted
                    + " target=" + target + " parked=" + lockParked);
            JsonObject o = new JsonObject();
            o.addProperty("ok", true);
            o.addProperty("stepped", n);
            o.addProperty("completed", lockCompleted);
            o.addProperty("parked", true);
            lockAddPlayer(o);
            return o.toString();
        }
    }

    // ---- IEEE-754 transport ----
    // Decimal on the JSON wire is not a lossless float channel: gson prints a
    // shortest-roundtrip decimal for double but parsing back through getAsFloat
    // re-rounds, and any consumer that reformats loses low bits. The locked
    // commands therefore carry raw bit patterns as fixed-width hex, so a gate
    // comparing two snapshots is comparing the actual IEEE-754 words.
    private static String f32bits(float v) {
        String h = Integer.toHexString(Float.floatToRawIntBits(v));
        return h.length() >= 8 ? h : "00000000".substring(h.length()) + h;
    }
    private static String f64bits(double v) {
        String h = Long.toHexString(Double.doubleToRawLongBits(v));
        return h.length() >= 16 ? h : "0000000000000000".substring(h.length()) + h;
    }
    /** True if key is present in either bit form or decimal form. */
    private static boolean hasNum(JsonObject a, String k) {
        return a.has(k + "_bits") || a.has(k);
    }
    /** "<k>_bits" (raw hex, exact) wins over "<k>" (decimal, lossy). */
    private static float f32of(JsonObject a, String k, float dflt) {
        if (a.has(k + "_bits"))
            return Float.intBitsToFloat(
                (int) Long.parseLong(a.get(k + "_bits").getAsString().trim(), 16));
        return a.has(k) ? a.get(k).getAsFloat() : dflt;
    }
    private static double f64of(JsonObject a, String k, double dflt) {
        if (a.has(k + "_bits"))
            return Double.longBitsToDouble(
                Long.parseUnsignedLong(a.get(k + "_bits").getAsString().trim(), 16));
        return a.has(k) ? a.get(k).getAsDouble() : dflt;
    }

    private static net.minecraft.entity.player.EntityPlayerMP lockPlayerOf(MinecraftServer s) {
        try {
            java.util.List<net.minecraft.entity.player.EntityPlayerMP> ps =
                s.getPlayerList().getPlayers();
            if (!ps.isEmpty()) return ps.get(0);
        } catch (Throwable ig) {}
        return null;
    }
    private static net.minecraft.world.WorldServer lockWorldOf(MinecraftServer s) {
        net.minecraft.entity.player.EntityPlayerMP p = lockPlayerOf(s);
        return p != null ? p.getServerWorld() : s.worlds[0];
    }

    /**
     * Authoritative server-player snapshot taken while the server is parked, so
     * it is atomic with respect to the tick. Every float/double also carries its
     * raw IEEE-754 word.
     */
    private static JsonObject lockPlayerJson(net.minecraft.entity.player.EntityPlayerMP p) {
        JsonObject o = new JsonObject();
        if (p == null) return o;
        o.addProperty("x", p.posX);          o.addProperty("x_bits", f64bits(p.posX));
        o.addProperty("y", p.posY);          o.addProperty("y_bits", f64bits(p.posY));
        o.addProperty("z", p.posZ);          o.addProperty("z_bits", f64bits(p.posZ));
        o.addProperty("vx", p.motionX);      o.addProperty("vx_bits", f64bits(p.motionX));
        o.addProperty("vy", p.motionY);      o.addProperty("vy_bits", f64bits(p.motionY));
        o.addProperty("vz", p.motionZ);      o.addProperty("vz_bits", f64bits(p.motionZ));
        o.addProperty("yaw", p.rotationYaw);
        o.addProperty("yaw_bits", f32bits(p.rotationYaw));
        o.addProperty("pitch", p.rotationPitch);
        o.addProperty("pitch_bits", f32bits(p.rotationPitch));
        o.addProperty("fall_distance", p.fallDistance);
        o.addProperty("fall_distance_bits", f32bits(p.fallDistance));
        o.addProperty("health", p.getHealth());
        o.addProperty("health_bits", f32bits(p.getHealth()));
        o.addProperty("on_ground", p.onGround);
        o.addProperty("food", p.getFoodStats().getFoodLevel());
        o.addProperty("fire", fireTicks(p));
        o.addProperty("ticks_existed", p.ticksExisted);
        o.addProperty("dim", p.dimension);
        return o;
    }

    /** Attach the parked authoritative player + world tick to a locked reply. */
    private void lockAddPlayer(JsonObject out) {
        try {
            MinecraftServer s = Minecraft.getMinecraft().getIntegratedServer();
            if (s == null) return;
            net.minecraft.entity.player.EntityPlayerMP p = lockPlayerOf(s);
            if (p != null) {
                out.add("player", lockPlayerJson(p));
                out.addProperty("world_time", p.getServerWorld().getTotalWorldTime());
            }
        } catch (Throwable ig) {}
        out.addProperty("client_num_ticks", TimeHelper.SyncManager.numTicks);
    }

    /**
     * getblocks_locked: identical cuboid encoding to getblocks (little-endian u16
     * id<<4|meta, y-major then z then x) but read straight off the parked
     * WorldServer from the socket thread. No scheduled task, so no tick is
     * consumed and two back-to-back reads at the same boundary are byte-identical.
     * "file" is optional; the FNV-1a-64 digest is always returned so a gate can
     * compare reads without touching the filesystem.
     */
    private String lockGetBlocks(JsonObject a) {
        synchronized (lockMon) {
            String bad = lockRequireParked("getblocks_locked");
            if (bad != null) return bad;
        }
        try {
            MinecraftServer s = Minecraft.getMinecraft().getIntegratedServer();
            if (s == null) return err("no server");
            net.minecraft.world.WorldServer w = lockWorldOf(s);
            int x0 = a.get("x0").getAsInt(), y0 = a.get("y0").getAsInt();
            int z0 = a.get("z0").getAsInt(), x1 = a.get("x1").getAsInt();
            int y1 = a.get("y1").getAsInt(), z1 = a.get("z1").getAsInt();
            if (x1 < x0 || y1 < y0 || z1 < z0) return err("invalid block box");
            if (y0 < 0 || y1 > 255) return err("block box y must be 0..255");
            long cells = (long)(x1 - x0 + 1) * (long)(y1 - y0 + 1) * (long)(z1 - z0 + 1);
            if (cells > (1 << 26)) return err("block box too large");
            byte[] buf = new byte[(int) cells * 2];
            net.minecraft.util.math.BlockPos.MutableBlockPos pos =
                new net.minecraft.util.math.BlockPos.MutableBlockPos();
            int k = 0;
            for (int y = y0; y <= y1; y++) for (int z = z0; z <= z1; z++) for (int x = x0; x <= x1; x++) {
                net.minecraft.block.state.IBlockState st = w.getBlockState(pos.setPos(x, y, z));
                net.minecraft.block.Block b = st.getBlock();
                int v = (net.minecraft.block.Block.getIdFromBlock(b) << 4) | b.getMetaFromState(st);
                buf[k++] = (byte) (v & 0xff); buf[k++] = (byte) ((v >> 8) & 0xff);
            }
            if (a.has("file")) {
                String file = a.get("file").getAsString();
                java.io.DataOutputStream o = new java.io.DataOutputStream(
                    new java.io.BufferedOutputStream(new java.io.FileOutputStream(file), 1 << 20));
                o.write(buf, 0, k); o.close();
            }
            JsonObject o = new JsonObject();
            o.addProperty("ok", true);
            if (a.has("file")) o.addProperty("file", a.get("file").getAsString());
            o.addProperty("nx", x1 - x0 + 1);
            o.addProperty("ny", y1 - y0 + 1);
            o.addProperty("nz", z1 - z0 + 1);
            o.addProperty("bytes", k);
            o.addProperty("hash", fnv1a64hex(buf, k));
            synchronized (lockMon) { o.addProperty("gate_completed", lockCompleted); }
            lockAddPlayer(o);
            return o.toString();
        } catch (Throwable t) { return err("getblocks_locked: " + t); }
    }

    /** FNV-1a 64, zero-padded hex. Content digest only; not a security hash. */
    private static String fnv1a64hex(byte[] b, int n) {
        long h = 0xcbf29ce484222325L;
        for (int i = 0; i < n; i++) { h ^= (b[i] & 0xffL); h *= 0x100000001b3L; }
        String s = Long.toHexString(h);
        return s.length() >= 16 ? s : "0000000000000000".substring(s.length()) + s;
    }

    /**
     * setblocks_locked: the setblocks write applied at the parked boundary, so no
     * server tick can observe the world between the write and the next
     * step_server_locked. Same flag-3 setBlockState as setblocks, so onBlockAdded
     * still schedules exactly as vanilla placement does.
     */
    private String lockSetBlocks(JsonObject a) {
        synchronized (lockMon) {
            String bad = lockRequireParked("setblocks_locked");
            if (bad != null) return bad;
        }
        try {
            MinecraftServer s = Minecraft.getMinecraft().getIntegratedServer();
            if (s == null) return err("no server");
            net.minecraft.world.WorldServer w = lockWorldOf(s);
            com.google.gson.JsonArray blocks = a.has("blocks")
                ? a.getAsJsonArray("blocks") : new com.google.gson.JsonArray();
            if (blocks.size() > 1000000) return err("too many locked fixture blocks");
            int n = 0;
            for (com.google.gson.JsonElement el : blocks) {
                com.google.gson.JsonArray q = el.getAsJsonArray();
                if (q.size() != 5) return err("locked fixture block needs [x,y,z,id,meta]");
                int x = q.get(0).getAsInt(), y = q.get(1).getAsInt(), z = q.get(2).getAsInt();
                int id = q.get(3).getAsInt(), meta = q.get(4).getAsInt();
                if (y < 0 || y > 255 || id < 0 || id > 4095 || meta < 0 || meta > 15)
                    return err("invalid locked fixture block at index " + n);
                net.minecraft.block.Block b = net.minecraft.block.Block.getBlockById(id);
                if (b == null) return err("unknown block id " + id);
                w.setBlockState(new BlockPos(x, y, z), b.getStateFromMeta(meta), 3);
                n++;
            }
            JsonObject o = new JsonObject();
            o.addProperty("ok", true);
            o.addProperty("set", n);
            synchronized (lockMon) { o.addProperty("gate_completed", lockCompleted); }
            lockAddPlayer(o);
            return o.toString();
        } catch (Throwable t) { return err("setblocks_locked: " + t); }
    }

    /**
     * setplayer_locked: write named player fields at the parked boundary, so no
     * player or world tick runs between the write and the returned snapshot and
     * short counters are not consumed during scenario setup. Floats arrive as
     * raw IEEE-754 words ("vy_bits": "bff0000000000000"), which is what makes a
     * fixture reproducible bit-for-bit; decimal keys are accepted as a fallback.
     * Server and client player are written together so client prediction does
     * not immediately undo the fixture.
     */
    private String lockSetPlayer(JsonObject a) {
        synchronized (lockMon) {
            String bad = lockRequireParked("setplayer_locked");
            if (bad != null) return bad;
            try {
                MinecraftServer s = Minecraft.getMinecraft().getIntegratedServer();
                if (s == null) return err("no server");
                net.minecraft.entity.player.EntityPlayerMP p = lockPlayerOf(s);
                if (p == null) return err("no server player");
                EntityPlayerSP cp = Minecraft.getMinecraft().player;

                boolean hasPos = hasNum(a, "x") || hasNum(a, "y") || hasNum(a, "z");
                if (hasPos) {
                    if (!(hasNum(a, "x") && hasNum(a, "y") && hasNum(a, "z")))
                        return err("locked player position requires x/y/z together");
                    double x = f64of(a, "x", p.posX);
                    double y = f64of(a, "y", p.posY);
                    double z = f64of(a, "z", p.posZ);
                    if (!isFinite(x) || !isFinite(y) || !isFinite(z))
                        return err("locked player position must be finite");
                    p.setPositionAndUpdate(x, y, z);
                    if (cp != null) cp.setPosition(x, y, z);
                }
                boolean hasMotion = hasNum(a, "vx") || hasNum(a, "vy") || hasNum(a, "vz");
                if (hasMotion) {
                    if (!(hasNum(a, "vx") && hasNum(a, "vy") && hasNum(a, "vz")))
                        return err("locked player motion requires vx/vy/vz together");
                    double vx = f64of(a, "vx", p.motionX);
                    double vy = f64of(a, "vy", p.motionY);
                    double vz = f64of(a, "vz", p.motionZ);
                    if (!isFinite(vx) || !isFinite(vy) || !isFinite(vz))
                        return err("locked player motion must be finite");
                    p.motionX = vx; p.motionY = vy; p.motionZ = vz;
                    if (cp != null) { cp.motionX = vx; cp.motionY = vy; cp.motionZ = vz; }
                }
                boolean hasRot = hasNum(a, "yaw") || hasNum(a, "pitch");
                if (hasRot) {
                    if (!(hasNum(a, "yaw") && hasNum(a, "pitch")))
                        return err("locked player rotation requires yaw/pitch together");
                    float yaw = f32of(a, "yaw", p.rotationYaw);
                    float pitch = f32of(a, "pitch", p.rotationPitch);
                    if (Float.isNaN(yaw) || Float.isInfinite(yaw)
                            || Float.isNaN(pitch) || Float.isInfinite(pitch))
                        return err("locked player rotation must be finite");
                    p.rotationYaw = yaw; p.prevRotationYaw = yaw;
                    p.rotationPitch = pitch; p.prevRotationPitch = pitch;
                    p.setRotationYawHead(yaw);
                    if (cp != null) {
                        cp.rotationYaw = yaw; cp.prevRotationYaw = yaw;
                        cp.rotationPitch = pitch; cp.prevRotationPitch = pitch;
                    }
                }
                if (hasNum(a, "fall_distance")) {
                    float fd = f32of(a, "fall_distance", p.fallDistance);
                    if (Float.isNaN(fd) || Float.isInfinite(fd) || fd < 0.0F)
                        return err("fall_distance must be finite and >= 0");
                    p.fallDistance = fd;
                    if (cp != null) cp.fallDistance = fd;
                }
                if (a.has("on_ground")) {
                    boolean og = a.get("on_ground").getAsInt() != 0;
                    p.onGround = og;
                    if (cp != null) cp.onGround = og;
                }
                if (a.has("food")) {
                    int food = a.get("food").getAsInt();
                    if (food < 0 || food > 20) return err("food must be 0..20");
                    p.getFoodStats().setFoodLevel(food);
                    if (cp != null) cp.getFoodStats().setFoodLevel(food);
                }
                if (hasNum(a, "health")) {
                    float hp = f32of(a, "health", p.getHealth());
                    if (Float.isNaN(hp) || hp < 0.0F || hp > p.getMaxHealth())
                        return err("health must be 0..maxHealth");
                    p.setHealth(hp);
                    if (cp != null) cp.setHealth(hp);
                }
                JsonObject o = new JsonObject();
                o.addProperty("ok", true);
                o.addProperty("gate_completed", lockCompleted);
                lockAddPlayer(o);
                return o.toString();
            } catch (Throwable t) { return err("setplayer_locked: " + t); }
        }
    }

    /** Double.isFinite is Java 8+ on the JDK but keep the bridge source JDK-6 clean. */
    private static boolean isFinite(double v) {
        return !Double.isNaN(v) && !Double.isInfinite(v);
    }

    // ================= RNG cursor capture =================
    // Reflection accessors for the four server streams. java.util.Random keeps its
    // 48-bit state in an AtomicLong `seed` (scrambled by the constructor, never by
    // next()), so `seed.get() & (2^48-1)` IS the cursor: next() is one
    // multiply-add away from it. Gaussian state is separate - nextGaussian()
    // computes two values and stashes one, so a cursor is only complete with
    // haveNextNextGaussian/nextNextGaussian alongside the seed.
    // Fields are resolved once and cached; every accessor is fail-soft (-1 /
    // false) because a JDK that hides these must degrade the gate to
    // "unavailable", never crash the bridge.
    private static java.lang.reflect.Field RNG_SEED_F;
    private static java.lang.reflect.Field RNG_HAVE_GAUSS_F;
    private static java.lang.reflect.Field RNG_NEXT_GAUSS_F;
    private static java.lang.reflect.Field RNG_MATH_HOLDER_F;
    private static java.lang.reflect.Field RNG_BLOCK_F;
    private static java.lang.reflect.Field RNG_UPDATE_LCG_F;

    private static long rngSeed48(java.util.Random r) {
        try {
            if (r == null) return -1L;
            if (RNG_SEED_F == null) {
                RNG_SEED_F = java.util.Random.class.getDeclaredField("seed");
                RNG_SEED_F.setAccessible(true);
            }
            java.util.concurrent.atomic.AtomicLong s =
                (java.util.concurrent.atomic.AtomicLong) RNG_SEED_F.get(r);
            return s.get() & ((1L << 48) - 1L);
        } catch (Throwable ig) { return -1L; }
    }
    private static boolean rngHaveGaussian(java.util.Random r) {
        try {
            if (r == null) return false;
            if (RNG_HAVE_GAUSS_F == null) {
                RNG_HAVE_GAUSS_F =
                    java.util.Random.class.getDeclaredField("haveNextNextGaussian");
                RNG_HAVE_GAUSS_F.setAccessible(true);
            }
            return RNG_HAVE_GAUSS_F.getBoolean(r);
        } catch (Throwable ig) { return false; }
    }
    private static double rngNextGaussian(java.util.Random r) {
        try {
            if (r == null) return 0.0D;
            if (RNG_NEXT_GAUSS_F == null) {
                RNG_NEXT_GAUSS_F =
                    java.util.Random.class.getDeclaredField("nextNextGaussian");
                RNG_NEXT_GAUSS_F.setAccessible(true);
            }
            return RNG_NEXT_GAUSS_F.getDouble(r);
        } catch (Throwable ig) { return 0.0D; }
    }
    /** java.lang.Math$RandomNumberGeneratorHolder.randomNumberGenerator. */
    private static java.util.Random rngMathRandom() {
        try {
            if (RNG_MATH_HOLDER_F == null) {
                Class<?> h =
                    Class.forName("java.lang.Math$RandomNumberGeneratorHolder");
                RNG_MATH_HOLDER_F = h.getDeclaredField("randomNumberGenerator");
                RNG_MATH_HOLDER_F.setAccessible(true);
            }
            return (java.util.Random) RNG_MATH_HOLDER_F.get(null);
        } catch (Throwable ig) { return null; }
    }
    /** Block.RANDOM, the static "no world available" generator. */
    private static java.util.Random rngBlockRandom() {
        try {
            if (RNG_BLOCK_F == null) {
                RNG_BLOCK_F =
                    net.minecraft.block.Block.class.getDeclaredField("RANDOM");
                RNG_BLOCK_F.setAccessible(true);
            }
            return (java.util.Random) RNG_BLOCK_F.get(null);
        } catch (Throwable ig) { return null; }
    }
    /** World.updateLCG: int32, `lcg = lcg * 3 + 1013904223`, NOT a java.util.Random. */
    private static int rngUpdateLCG(net.minecraft.world.World w) {
        try {
            if (w == null) return 0;
            if (RNG_UPDATE_LCG_F == null) {
                RNG_UPDATE_LCG_F =
                    net.minecraft.world.World.class.getDeclaredField("updateLCG");
                RNG_UPDATE_LCG_F.setAccessible(true);
            }
            return RNG_UPDATE_LCG_F.getInt(w);
        } catch (Throwable ig) { return 0; }
    }

    /** Named stream lookup for the fixture/negative-control burn command. */
    private static java.util.Random rngStreamByName(
            net.minecraft.world.World w, String name) {
        if ("world".equals(name)) return w == null ? null : w.rand;
        if ("math".equals(name)) return rngMathRandom();
        if ("block".equals(name)) return rngBlockRandom();
        return null;
    }

    /** Fill a JSON object with a full four-stream cursor snapshot. */
    private static void rngCursorJson(
            JsonObject o, net.minecraft.world.World w) {
        java.util.Random wr = (w == null) ? null : w.rand;
        o.addProperty("world_seed48", rngSeed48(wr));
        o.addProperty("world_have_gaussian", rngHaveGaussian(wr));
        o.addProperty("world_gaussian_bits", f64bits(rngNextGaussian(wr)));
        o.addProperty("math_seed48", rngSeed48(rngMathRandom()));
        o.addProperty("block_seed48", rngSeed48(rngBlockRandom()));
        o.addProperty("update_lcg", rngUpdateLCG(w));
    }

    /**
     * ServerTickEvent.START hook: append one cursor record. Runs on the server
     * thread after the lockstep gate has released the tick, so the values are the
     * exact state the tick is about to consume, and no other thread can be inside
     * a draw. Silent no-op when capture is off - this is on the hot tick path.
     */
    private void rngCaptureTick(net.minecraft.world.World w) {
        if (!rngCaptureOn) return;
        java.util.Random wr = (w == null) ? null : w.rand;
        long wt = (w == null) ? -1L : w.getTotalWorldTime();
        long seed = rngSeed48(wr);
        boolean gh = rngHaveGaussian(wr);
        long gb = Double.doubleToRawLongBits(rngNextGaussian(wr));
        long mseed = rngSeed48(rngMathRandom());
        long bseed = rngSeed48(rngBlockRandom());
        int lcg = rngUpdateLCG(w);
        long completed;
        synchronized (lockMon) { completed = lockArmed ? lockCompleted : -1L; }
        synchronized (rngMon) {
            if (!rngCaptureOn || rngCapWorldTime == null) return;
            int cap = rngCapWorldTime.length;
            if (rngCapN == cap) { rngCapDropped++; return; }  // stop at full: the
            // FIRST divergence is what matters, so keep the head of the run rather
            // than overwriting it with the tail.
            int i = rngCapHead;
            rngCapWorldTime[i] = wt;
            rngCapSeqArr[i] = rngCapSeq++;
            rngCapCompletedArr[i] = completed;
            rngCapWorld[i] = seed;
            rngCapMath[i] = mseed;
            rngCapBlock[i] = bseed;
            rngCapLcg[i] = lcg;
            rngCapGaussHave[i] = (byte) (gh ? 1 : 0);
            rngCapGaussBits[i] = gb;
            rngCapHead++;
            rngCapN++;
        }
    }

    /** rng_capture: arm/disarm the per-tick cursor ring. No gate required. */
    private String rngCaptureControl(JsonObject a) {
        boolean on = !a.has("on") || a.get("on").getAsInt() != 0;
        int cap = a.has("capacity") ? a.get("capacity").getAsInt() : 20000;
        if (cap < 1 || cap > 2000000)
            return err("rng_capture: capacity must be 1..2000000");
        synchronized (rngMon) {
            if (on) {
                rngCapWorldTime = new long[cap];
                rngCapSeqArr = new long[cap];
                rngCapCompletedArr = new long[cap];
                rngCapWorld = new long[cap];
                rngCapMath = new long[cap];
                rngCapBlock = new long[cap];
                rngCapLcg = new int[cap];
                rngCapGaussHave = new byte[cap];
                rngCapGaussBits = new long[cap];
                rngCapN = 0; rngCapHead = 0; rngCapDropped = 0; rngCapSeq = 0;
                rngCaptureOn = true;
            } else {
                rngCaptureOn = false;
            }
            JsonObject o = new JsonObject();
            o.addProperty("ok", true);
            o.addProperty("capturing", rngCaptureOn);
            o.addProperty("capacity", rngCapWorldTime == null ? 0 : rngCapWorldTime.length);
            o.addProperty("count", rngCapN);
            o.addProperty("dropped", rngCapDropped);
            return o.toString();
        }
    }

    /**
     * rng_dump: write the ring as JSON Lines and report a digest. One record per
     * captured server tick, in tick order. The digest covers the cursor columns
     * only (not world_time), so two runs that agree on RNG but not on wall tick
     * numbering still compare equal.
     */
    private String rngDump(JsonObject a) {
        long[] wt, sq, cp, wd, mt, bk, gb; int[] lc; byte[] gh; int n; long dropped;
        synchronized (rngMon) {
            if (rngCapWorldTime == null) return err("rng_dump: capture was never armed");
            n = rngCapN; dropped = rngCapDropped;
            wt = rngCapWorldTime.clone(); sq = rngCapSeqArr.clone();
            cp = rngCapCompletedArr.clone(); wd = rngCapWorld.clone();
            mt = rngCapMath.clone(); bk = rngCapBlock.clone();
            lc = rngCapLcg.clone(); gh = rngCapGaussHave.clone();
            gb = rngCapGaussBits.clone();
        }
        try {
            StringBuilder sb = new StringBuilder(n * 160);
            long h = 0xcbf29ce484222325L;
            for (int i = 0; i < n; i++) {
                sb.append("{\"seq\":").append(sq[i])
                  .append(",\"world_time\":").append(wt[i])
                  .append(",\"gate_completed\":").append(cp[i])
                  .append(",\"world_seed48\":").append(wd[i])
                  .append(",\"world_have_gaussian\":").append(gh[i] != 0)
                  .append(",\"world_gaussian_bits\":\"").append(hex64(gb[i]))
                  .append("\",\"math_seed48\":").append(mt[i])
                  .append(",\"block_seed48\":").append(bk[i])
                  .append(",\"update_lcg\":").append(lc[i])
                  .append("}\n");
                h = fnvMix(h, wd[i]); h = fnvMix(h, mt[i]); h = fnvMix(h, bk[i]);
                h = fnvMix(h, lc[i] & 0xffffffffL); h = fnvMix(h, gh[i]);
            }
            JsonObject o = new JsonObject();
            o.addProperty("ok", true);
            o.addProperty("count", n);
            o.addProperty("dropped", dropped);
            o.addProperty("digest", hex64(h));
            if (a.has("file")) {
                String f = a.get("file").getAsString();
                java.io.Writer wtr = new java.io.BufferedWriter(
                    new java.io.OutputStreamWriter(
                        new java.io.FileOutputStream(f), "UTF-8"), 1 << 20);
                wtr.write(sb.toString());
                wtr.close();
                o.addProperty("file", f);
            } else {
                o.addProperty("records", sb.toString());
            }
            return o.toString();
        } catch (Throwable t) { return err("rng_dump: " + t); }
    }

    private static long fnvMix(long h, long v) {
        for (int b = 0; b < 8; b++) {
            h ^= (v >>> (b * 8)) & 0xffL;
            h *= 0x100000001b3L;
        }
        return h;
    }
    private static String hex64(long v) {
        String s = Long.toHexString(v);
        return s.length() >= 16 ? s : "0000000000000000".substring(s.length()) + s;
    }

    /** rng_cursor_locked: one four-stream cursor read off the parked server. */
    private String rngCursorLocked() {
        synchronized (lockMon) {
            String bad = lockRequireParked("rng_cursor_locked");
            if (bad != null) return bad;
        }
        try {
            MinecraftServer s = Minecraft.getMinecraft().getIntegratedServer();
            if (s == null) return err("no server");
            net.minecraft.world.WorldServer w = lockWorldOf(s);
            JsonObject o = new JsonObject();
            o.addProperty("ok", true);
            rngCursorJson(o, w);
            synchronized (lockMon) { o.addProperty("gate_completed", lockCompleted); }
            lockAddPlayer(o);
            return o.toString();
        } catch (Throwable t) { return err("rng_cursor_locked: " + t); }
    }

    /**
     * rng_burn_locked: consume exactly n draws from a named stream at the parked
     * boundary. FIXTURE ONLY - this is the negative control for the cursor gate.
     * It injects precisely the fault the gate exists to catch (a real extra
     * java.util.Random draw on the live server, not a doctored sidecar), so a
     * green gate can be shown to actually be sensitive. Deliberately NOT a
     * seed-setting command: this repo does not restore Java RNG cursors, see
     * verify/trace/rng_cursor.py for why.
     */
    private String rngBurnLocked(JsonObject a) {
        synchronized (lockMon) {
            String bad = lockRequireParked("rng_burn_locked");
            if (bad != null) return bad;
        }
        String stream = a.has("stream") ? a.get("stream").getAsString() : "world";
        int n = a.has("n") ? a.get("n").getAsInt() : 1;
        if (n < 0 || n > 1000000) return err("rng_burn_locked: n must be 0..1000000");
        try {
            MinecraftServer s = Minecraft.getMinecraft().getIntegratedServer();
            if (s == null) return err("no server");
            net.minecraft.world.WorldServer w = lockWorldOf(s);
            JsonObject o = new JsonObject();
            o.addProperty("ok", true);
            o.addProperty("stream", stream);
            o.addProperty("n", n);
            if ("lcg".equals(stream)) {
                int v = rngUpdateLCG(w);
                for (int i = 0; i < n; i++) v = v * 3 + 1013904223;
                if (RNG_UPDATE_LCG_F == null) return err("rng_burn_locked: no updateLCG field");
                RNG_UPDATE_LCG_F.setInt(w, v);
            } else {
                java.util.Random r = rngStreamByName(w, stream);
                if (r == null) return err("rng_burn_locked: unknown stream " + stream);
                o.addProperty("before_seed48", rngSeed48(r));
                for (int i = 0; i < n; i++) r.nextInt();
                o.addProperty("after_seed48", rngSeed48(r));
            }
            rngCursorJson(o, w);
            synchronized (lockMon) { o.addProperty("gate_completed", lockCompleted); }
            lockAddPlayer(o);
            return o.toString();
        } catch (Throwable t) { return err("rng_burn_locked: " + t); }
    }

    // ---------------- state capsule capture ----------------
    // A capsule is a pre-tick snapshot of everything the oracle knows at a parked
    // lockstep boundary that magma would need to CONTINUE the run rather than
    // restart it. Design ported from bluecoconut's PR #5 magma/trace/state_capsule.py:
    // a versioned manifest plus a raw block sidecar, and - the part that makes it a
    // contract rather than a dump - a capability ledger tagging every field class
    // exact / captured_only / unavailable.
    //
    // Block id and metadata are NOT the world state. Three kinds of hidden state
    // decide what the next tick does and none of them are visible in a cuboid read:
    //
    //   scheduled ticks   WorldServer.pendingTickListEntriesTreeSet, ordered by
    //                     (scheduledTime, priority, tickEntryID). A block that
    //                     looks identical on both sides behaves differently if one
    //                     side has a pending callback queued and the other does not.
    //   tile entities     TileEntityFurnace's burn/cook counters are the flagship
    //                     magma can actually restore: a lit furnace mid-smelt and a
    //                     freshly-lit one are the same two block ids.
    //   torch burnout     BlockRedstoneTorch.toggles, a process-global
    //                     WeakHashMap<World,List<Toggle>> of (pos, totalWorldTime).
    //                     Eight toggles at one position inside 60 ticks burns the
    //                     torch out. It lives in neither the block, the metadata,
    //                     nor the chunk NBT - it is pure JVM heap, and it is the
    //                     cleanest example in 1.11.2 of state that a "load the save
    //                     file" checkpoint provably cannot carry.
    //
    // Everything here reads a PARKED WorldServer from the socket thread, the same
    // rule and the same boundary as the wave-1 locked set and the wave-2 RNG
    // cursors, so the capsule is atomic with respect to the tick and cursor[i] is
    // the state tick i is about to consume.

    /** BlockRedstoneTorch.toggles: static Map<World,List<Toggle>>, Forge MC-101233. */
    private static java.lang.reflect.Field TORCH_TOGGLES_F;
    private static java.lang.reflect.Field TORCH_TOGGLE_POS_F;
    private static java.lang.reflect.Field TORCH_TOGGLE_TIME_F;
    /** WorldServer.pendingTickListEntriesTreeSet + NextTickListEntry.tickEntryID. */
    private static java.lang.reflect.Field PENDING_TREE_F;
    private static java.lang.reflect.Field TICK_ENTRY_ID_F;

    /**
     * Redstone torch burnout history for this world, chronological.
     * Fail-soft like the wave-2 RNG accessors: a JDK or a Forge build that hides
     * the field degrades the capsule to "unavailable" (captured=false) instead of
     * killing the bridge, so the gate refuses rather than silently proving nothing.
     */
    private static boolean torchTogglesJson(JsonObject out, net.minecraft.world.World w) {
        try {
            if (TORCH_TOGGLES_F == null) {
                TORCH_TOGGLES_F = net.minecraft.block.BlockRedstoneTorch.class
                    .getDeclaredField("toggles");
                TORCH_TOGGLES_F.setAccessible(true);
            }
            java.util.Map<?, ?> byWorld = (java.util.Map<?, ?>) TORCH_TOGGLES_F.get(null);
            JsonArray arr = new JsonArray();
            if (byWorld != null && w != null) {
                Object list = byWorld.get(w);
                if (list instanceof java.util.List) {
                    for (Object t : (java.util.List<?>) list) {
                        if (t == null) continue;
                        if (TORCH_TOGGLE_POS_F == null) {
                            TORCH_TOGGLE_POS_F = t.getClass().getDeclaredField("pos");
                            TORCH_TOGGLE_POS_F.setAccessible(true);
                            TORCH_TOGGLE_TIME_F = t.getClass().getDeclaredField("time");
                            TORCH_TOGGLE_TIME_F.setAccessible(true);
                        }
                        BlockPos p = (BlockPos) TORCH_TOGGLE_POS_F.get(t);
                        long time = TORCH_TOGGLE_TIME_F.getLong(t);
                        JsonObject e = new JsonObject();
                        e.addProperty("x", p.getX());
                        e.addProperty("y", p.getY());
                        e.addProperty("z", p.getZ());
                        e.addProperty("time", time);
                        arr.add(e);
                    }
                }
            }
            out.add("redstone_torch_toggles", arr);
            out.addProperty("redstone_torch_toggles_complete", true);
            return true;
        } catch (Throwable ig) {
            out.add("redstone_torch_toggles", new JsonArray());
            out.addProperty("redstone_torch_toggles_complete", false);
            return false;
        }
    }

    /**
     * Pending block updates whose position falls inside the capsule box.
     * `time` is the ABSOLUTE due tick (World.getTotalWorldTime() + delay), matching
     * NextTickListEntry.scheduledTime, and (time, priority, order) is the exact
     * triple NextTickListEntry.compareTo sorts on - so replaying the array in
     * order reproduces vanilla's execution order, which a bare per-position delay
     * cannot. `order` is tickEntryID, the monotone allocation counter that breaks
     * ties between two callbacks due on the same tick at the same priority.
     */
    private static boolean scheduledTicksJson(JsonObject out,
            net.minecraft.world.WorldServer w,
            int x0, int y0, int z0, int x1, int y1, int z1) {
        try {
            if (PENDING_TREE_F == null) {
                PENDING_TREE_F = net.minecraft.world.WorldServer.class
                    .getDeclaredField("pendingTickListEntriesTreeSet");
                PENDING_TREE_F.setAccessible(true);
            }
            java.util.TreeSet<?> tree = (java.util.TreeSet<?>) PENDING_TREE_F.get(w);
            JsonArray arr = new JsonArray();
            if (tree != null) {
                for (Object o : tree) {
                    net.minecraft.world.NextTickListEntry e =
                        (net.minecraft.world.NextTickListEntry) o;
                    BlockPos p = e.position;
                    if (p.getX() < x0 || p.getX() > x1 || p.getY() < y0 || p.getY() > y1
                        || p.getZ() < z0 || p.getZ() > z1) continue;
                    if (TICK_ENTRY_ID_F == null) {
                        TICK_ENTRY_ID_F = net.minecraft.world.NextTickListEntry.class
                            .getDeclaredField("tickEntryID");
                        TICK_ENTRY_ID_F.setAccessible(true);
                    }
                    JsonObject j = new JsonObject();
                    j.addProperty("x", p.getX());
                    j.addProperty("y", p.getY());
                    j.addProperty("z", p.getZ());
                    j.addProperty("block",
                        net.minecraft.block.Block.getIdFromBlock(e.getBlock()));
                    j.addProperty("time", e.scheduledTime);
                    j.addProperty("priority", e.priority);
                    j.addProperty("order", TICK_ENTRY_ID_F.getLong(e));
                    arr.add(j);
                }
            }
            out.add("scheduled_ticks", arr);
            out.addProperty("scheduled_ticks_complete", true);
            return true;
        } catch (Throwable ig) {
            out.add("scheduled_ticks", new JsonArray());
            out.addProperty("scheduled_ticks_complete", false);
            return false;
        }
    }

    /**
     * Tile entities inside the box, restricted to the classes magma can actually
     * materialize. Furnace carries the four TileEntityFurnace counters
     * (furnaceBurnTime / currentItemBurnTime / cookTime / totalCookTime) plus its
     * three slots; those map one-for-one onto magma's FurnaceLive and its
     * container_furnace_prop script event, which is why furnace is the exact class
     * and everything else here is captured_only.
     */
    private static boolean tileEntitiesJson(JsonObject out,
            net.minecraft.world.WorldServer w,
            int x0, int y0, int z0, int x1, int y1, int z1) {
        try {
            JsonArray arr = new JsonArray();
            BlockPos.MutableBlockPos pos = new BlockPos.MutableBlockPos();
            for (int y = y0; y <= y1; y++) for (int z = z0; z <= z1; z++)
            for (int x = x0; x <= x1; x++) {
                net.minecraft.tileentity.TileEntity te = w.getTileEntity(pos.setPos(x, y, z));
                if (!(te instanceof net.minecraft.tileentity.TileEntityFurnace)) continue;
                net.minecraft.tileentity.TileEntityFurnace f =
                    (net.minecraft.tileentity.TileEntityFurnace) te;
                JsonObject j = new JsonObject();
                j.addProperty("type", "furnace");
                j.addProperty("x", x); j.addProperty("y", y); j.addProperty("z", z);
                // getField: 0 furnaceBurnTime, 1 currentItemBurnTime, 2 cookTime,
                // 3 totalCookTime (TileEntityFurnace.getField, oracle-src :443-455).
                j.addProperty("burn_time", f.getField(0));
                j.addProperty("current_burn_time", f.getField(1));
                j.addProperty("cook_time", f.getField(2));
                j.addProperty("total_cook_time", f.getField(3));
                JsonArray items = new JsonArray();
                for (int slot = 0; slot < f.getSizeInventory(); slot++) {
                    net.minecraft.item.ItemStack st = f.getStackInSlot(slot);
                    if (st == null || st.isEmpty()) continue;
                    JsonObject si = new JsonObject();
                    si.addProperty("slot", slot);
                    si.addProperty("id", net.minecraft.item.Item.getIdFromItem(st.getItem()));
                    si.addProperty("count", st.getCount());
                    si.addProperty("meta", st.getMetadata());
                    items.add(si);
                }
                j.add("items", items);
                arr.add(j);
            }
            out.add("tile_entities", arr);
            out.addProperty("tile_entities_complete", true);
            return true;
        } catch (Throwable ig) {
            out.add("tile_entities", new JsonArray());
            out.addProperty("tile_entities_complete", false);
            return false;
        }
    }

    /**
     * capsule_dump_locked: one pre-tick state capsule read off the parked
     * WorldServer. Block/meta go to "blocks_file" in the same little-endian
     * u16 id&lt;&lt;4|meta, y-major-then-z-then-x order getblocks_locked uses, so the
     * two are byte-comparable; everything else comes back inline as JSON for
     * verify/trace/state_capsule.py to turn into a validated capsule directory.
     *
     * Every captured collection carries an explicit *_complete boolean. An empty
     * array with complete=true means "provably nothing there"; complete=false means
     * "this class could not be read", and the ledger downgrades it to unavailable
     * so a strict gate refuses instead of proving a vacuous continuation.
     */
    private String capsuleDumpLocked(JsonObject a) {
        synchronized (lockMon) {
            String bad = lockRequireParked("capsule_dump_locked");
            if (bad != null) return bad;
        }
        try {
            MinecraftServer s = Minecraft.getMinecraft().getIntegratedServer();
            if (s == null) return err("no server");
            net.minecraft.world.WorldServer w = lockWorldOf(s);
            int x0 = a.get("x0").getAsInt(), y0 = a.get("y0").getAsInt();
            int z0 = a.get("z0").getAsInt(), x1 = a.get("x1").getAsInt();
            int y1 = a.get("y1").getAsInt(), z1 = a.get("z1").getAsInt();
            if (x1 < x0 || y1 < y0 || z1 < z0) return err("invalid block box");
            if (y0 < 0 || y1 > 255) return err("block box y must be 0..255");
            long cells = (long)(x1 - x0 + 1) * (long)(y1 - y0 + 1) * (long)(z1 - z0 + 1);
            if (cells > (1 << 22)) return err("capsule box too large");
            byte[] buf = new byte[(int) cells * 2];
            BlockPos.MutableBlockPos pos = new BlockPos.MutableBlockPos();
            int k = 0;
            for (int y = y0; y <= y1; y++) for (int z = z0; z <= z1; z++)
            for (int x = x0; x <= x1; x++) {
                net.minecraft.block.state.IBlockState st = w.getBlockState(pos.setPos(x, y, z));
                net.minecraft.block.Block b = st.getBlock();
                int v = (net.minecraft.block.Block.getIdFromBlock(b) << 4) | b.getMetaFromState(st);
                buf[k++] = (byte) (v & 0xff); buf[k++] = (byte) ((v >> 8) & 0xff);
            }
            JsonObject o = new JsonObject();
            o.addProperty("ok", true);
            o.addProperty("schema", "netherite.state_capsule");
            o.addProperty("phase", "pre_tick");
            JsonArray box = new JsonArray();
            for (int v : new int[]{x0, y0, z0, x1, y1, z1})
                box.add(new com.google.gson.JsonPrimitive(v));
            o.add("box", box);
            o.addProperty("nx", x1 - x0 + 1);
            o.addProperty("ny", y1 - y0 + 1);
            o.addProperty("nz", z1 - z0 + 1);
            o.addProperty("cells", cells);
            o.addProperty("bytes", k);
            o.addProperty("hash", fnv1a64hex(buf, k));
            if (a.has("blocks_file")) {
                String file = a.get("blocks_file").getAsString();
                java.io.DataOutputStream ds = new java.io.DataOutputStream(
                    new java.io.BufferedOutputStream(new java.io.FileOutputStream(file), 1 << 20));
                ds.write(buf, 0, k); ds.close();
                o.addProperty("blocks_file", file);
            }
            JsonObject time = new JsonObject();
            time.addProperty("world_time", w.getWorldTime());
            time.addProperty("total_time", w.getTotalWorldTime());
            time.addProperty("dimension", w.provider.getDimension());
            o.add("time", time);
            JsonObject rng = new JsonObject();
            rngCursorJson(rng, w);
            o.add("world_rng", rng);
            boolean schedOk = scheduledTicksJson(o, w, x0, y0, z0, x1, y1, z1);
            boolean teOk = tileEntitiesJson(o, w, x0, y0, z0, x1, y1, z1);
            boolean torchOk = torchTogglesJson(o, w);
            // Entity capture is deliberately coarse: it is captured_only in the
            // ledger (magma restores no general entity), so a summary count is the
            // honest payload rather than a per-field dump nothing consumes.
            o.addProperty("entity_count", w.loadedEntityList.size());
            o.addProperty("entities_complete", false);
            o.addProperty("captured_scheduled_ticks", schedOk);
            o.addProperty("captured_tile_entities", teOk);
            o.addProperty("captured_torch_toggles", torchOk);
            synchronized (lockMon) { o.addProperty("gate_completed", lockCompleted); }
            lockAddPlayer(o);
            return o.toString();
        } catch (Throwable t) { return err("capsule_dump_locked: " + t); }
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
            case "capture_chunkrebuild": case "capture_translucent_draw":
            case "runcmds": case "dim": case "kmode":
            case "reload_renderers":
            case "portal_touch": case "use_end_eye": case "set_pose": case "hud_pin":
            case "entity_pin":
            case "dumpblocks": case "frame": case "frame_pair": case "gldiag": case "focusdiag": case "camera": case "sample_light":
            case "recstart": case "recstop":
            case "getblocks": case "setblocks":
            case "summon": case "getentities": case "killentities":
            case "pin_preview_anim": break;
            case "coverage_reset": case "coverage_dump":
            case "coverage_setfile": case "coverage_enable": return handleCoverage(cmd, action);
            // Lockstep gate. These run HERE, on the socket thread, and never enter
            // the `incoming` queue: while the server is parked it drains neither
            // its scheduled-task queue nor anything else, so the ordinary
            // game-thread hop would deadlock against the very gate they serve.
            case "server_step_lock": case "server_step_unlock":
                return lockControl(cmd, action);
            case "step_server_locked": return lockStepServer(action);
            case "getblocks_locked":   return lockGetBlocks(action);
            case "setblocks_locked":   return lockSetBlocks(action);
            case "setplayer_locked":   return lockSetPlayer(action);
            // RNG cursor capture. Same socket-thread rule as the locked commands:
            // rng_cursor_locked / rng_burn_locked touch a parked WorldServer, and
            // rng_capture / rng_dump only touch the mod's own ring (rngMon), so
            // none of them may take the game-thread hop.
            case "rng_capture":        return rngCaptureControl(action);
            case "rng_dump":           return rngDump(action);
            case "rng_cursor_locked":  return rngCursorLocked();
            case "rng_burn_locked":    return rngBurnLocked(action);
            // State capsule. Reads a parked WorldServer (scheduled-tick tree, tile
            // entities, BlockRedstoneTorch.toggles), so same socket-thread rule.
            case "capsule_dump_locked": return capsuleDumpLocked(action);
            default: return err("unknown cmd");
        }
        // Tearing down or reloading the world means the client thread will wait
        // on the server thread to stop. If the gate still holds it parked, that
        // is a deadlock, so release it before handing the command over.
        if (cmd.equals("close") || cmd.equals("reset") || cmd.equals("dim")) lockDisarm();
        Req r = new Req(cmd, action, world);
        incoming.put(r);
        String result = r.resp.poll(120, TimeUnit.SECONDS); // world-gen can be slow
        return result == null ? err("timeout") : result;
    }

    // ---------------- coverage hook control (render-opt) ----------------
    // CoverageLog is a sibling class in the netheritemod package (committed) and is on the
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
        // Lockstep gate: park at START (before anything in the tick has run) and
        // publish completion at END (after everything has). Both edges must run
        // on every tick or the gate's counter drifts, so lockFinishTick() is
        // reached on every END return path below.
        if (e.phase == TickEvent.Phase.START) {
            lockAwaitPermit();
            // AFTER the gate releases the tick: the cursor recorded here is the
            // state this tick is about to consume, and under the gate nothing
            // else can be mid-draw. Cheap no-op when capture is off.
            if (rngCaptureOn) {
                net.minecraft.server.MinecraftServer sv =
                    net.minecraftforge.fml.common.FMLCommonHandler.instance()
                        .getMinecraftServerInstance();
                if (sv != null) {
                    try { rngCaptureTick(lockWorldOf(sv)); } catch (Throwable ig) {}
                }
            }
            return;
        }
        if (e.phase != TickEvent.Phase.END) return;
        net.minecraft.server.MinecraftServer srv =
            net.minecraftforge.fml.common.FMLCommonHandler.instance().getMinecraftServerInstance();
        if (srv == null) { lockFinishTick(); return; }
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
        lockFinishTick();
        // Heartbeat so a paused/stalled integrated server is visible in the log
        // (the End-entry GuiDownloadTerrain deadlock froze ServerTickEvent for
        // 10+ minutes and looked exactly like a selector bug).
        if (++srvTicks % 1200 == 0) {
            System.out.println("[qrl] server tick heartbeat " + srvTicks);
        }
    }
    private long srvTicks = 0;

    // ---------------- game thread ----------------
    /** Apply pin fields just before the player model renders (inventory preview). */
    @SubscribeEvent
    public void onRenderPlayerPre(RenderPlayerEvent.Pre e) {
        if (!pinPreviewAnim) return;
        net.minecraft.entity.player.EntityPlayer p = e.getEntityPlayer();
        if (p != Minecraft.getMinecraft().player) return;
        pinSavedTicksExisted = p.ticksExisted;
        pinSavedLimbSwing = p.limbSwing;
        pinSavedLimbSwingAmount = p.limbSwingAmount;
        pinSavedPrevLimb = p.prevLimbSwingAmount;
        pinSavedValid = true;
        p.ticksExisted = pinPreviewTicksExisted;
        p.limbSwing = 0.0F;
        p.limbSwingAmount = 0.0F;
        p.prevLimbSwingAmount = 0.0F;
    }

    @SubscribeEvent
    public void onRenderPlayerPost(RenderPlayerEvent.Post e) {
        if (!pinPreviewAnim || !pinSavedValid) return;
        net.minecraft.entity.player.EntityPlayer p = e.getEntityPlayer();
        if (p != Minecraft.getMinecraft().player) return;
        p.ticksExisted = pinSavedTicksExisted;
        p.limbSwing = pinSavedLimbSwing;
        p.limbSwingAmount = pinSavedLimbSwingAmount;
        p.prevLimbSwingAmount = pinSavedPrevLimb;
        pinSavedValid = false;
    }

    @SubscribeEvent
    public void onClientTick(TickEvent.ClientTickEvent e) {
        if (e.phase == TickEvent.Phase.START) {
            Minecraft mcs = Minecraft.getMinecraft();
            if (recWriter != null && mcs.player != null) {
                recPreYaw = mcs.player.rotationYaw;
                recPrePitch = mcs.player.rotationPitch;
                recPreLookValid = true;
            }
            return;
        }
        if (e.phase != TickEvent.Phase.END) return;
        Minecraft mc = Minecraft.getMinecraft();
        mc.gameSettings.pauseOnLostFocus = false; // headless window never has focus; keep the server ticking
        // Keep limb swing zeroed while pinned so any path that samples between
        // ticks (not only RenderPlayer) still sees a parked idle pose.
        if (pinPreviewAnim && mc.player != null) {
            mc.player.limbSwing = 0.0F;
            mc.player.limbSwingAmount = 0.0F;
            mc.player.prevLimbSwingAmount = 0.0F;
        }
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
        // holdDeath freezes the dead state for oracle HUD captures (hud_pin).
        if (mc.player != null && mc.player.getHealth() <= 0.0F) {
            if (!wasDead) { deaths++; wasDead = true; }
            if (!holdDeath) {
                mc.player.respawnPlayer();
                if (mc.currentScreen instanceof net.minecraft.client.gui.GuiGameOver) {
                    mc.displayGuiScreen(null);
                }
            }
        } else if (mc.player != null) {
            wasDead = false;
        }

        // Sticky camera freeze + use-pose: processKeyBinds (earlier this tick)
        // stops use when the physical key is up. Re-assert after that so
        // free-running ticks and the upcoming render still see BOW/EAT/BLOCK
        // at a fixed wall registration. frame{} also re-applies.
        if (mc.player != null && mc.world != null) {
            try { applyStickyPosePin(mc); } catch (Throwable ig) {}
            if (pinUseActive) {
                try { applyUsePosePin(mc); } catch (Throwable ig) {}
            }
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
        } else if (recActive && (mc.world == null || mc.player == null)) {
            // World unload / dimension handoff: drop in-flight event channels
            // so none can attach to the first tick of the next world.
            clearRecBlockChanges();
            clearRecGuiClicks();
            clearRecGuiLifecycle();
        }

        // chain-RL container keep-open rule (gm_runtime_tick parity).
        try { rlContainerTick(mc); } catch (Throwable ig) {}
        // chain-RL GUI parity: magma/blaze have no window system - "use" on a
        // crafting table (id 58 is not ib_is_interactable, player_ctl.c:137)
        // is a plain block-place attempt there, while vanilla activates the
        // block and opens GuiCrafting. A GuiContainer never pauses the game,
        // so the pause-menu guard above leaves it up forever and every later
        // keybind action is a no-op. Close it at the tick boundary so the
        // policy sees the env it was trained in. Human play (mcwindow viewer
        // / tape recording) keeps its real GUIs.
        if (rlV2Active && !recActive && !HumanStream.hasViewer()
            && mc.player != null
            && mc.currentScreen instanceof
               net.minecraft.client.gui.inventory.GuiContainer) {
            try { mc.player.closeScreen(); } catch (Throwable ig) {}
        }

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
                recFlag7Metadata.clear();
                clearRecBlockChanges();
                clearRecGuiClicks();
                clearRecGuiLifecycle();
                recNeighborNotifyDepth = 0;
                recDigDestroyDepth = 0;
                /* dig_trace: opt-in diagnostic. Default off keeps every normal
                 * recstart byte-identical to pre-dig-trace tapes. */
                recDigTrace = r.action.has("dig_trace")
                    && r.action.get("dig_trace").getAsInt() != 0;
                recContract = r.action.has("contract")
                    && r.action.get("contract").getAsInt() != 0;
                recActive = true;
                recPreLookValid = false;
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
                 // Presence of flag7_metadata opts replay into recorded
                 // metadata timing. Legacy tapes omit it and retain the
                 // historical one-tick prediction byte-for-byte.
                 .append(",\"flag7_metadata\":1,\"flag7_initial\":")
                 .append(mc.player.isElytraFlying() ? 1 : 0)
                 // Presence of look_phase opts replay into per-tick pre-travel
                 // rotation rows (ry/rp); legacy tapes keep the deferred
                 // post-tick set_look semantics unchanged.
                 .append(",\"look_phase\":1")
                 .append(",\"gamemode\":\"").append(mc.playerController.getCurrentGameType().getName())
                 .append("\",\"difficulty\":\"").append(mc.world.getDifficulty().name())
                 // "default" (steve arm) or "slim" (alex) - offline UUID hash
                 // picks one; magma's first-person arm must match (set_skin).
                 .append("\",\"skin\":\"").append(mc.player.getSkinType())
                 // EntityRenderer.fogColor1 is an exponential smoother
                 // (+= (f2 - it) * 0.1F per updateRenderer) that starts at 0
                 // on a fresh EntityRenderer. Recording starts before it has
                 // converged, so the first ~40 goldens ramp; magma cannot
                 // derive the starting value from anything else in the tape
                 // (it depends on the light the client saw while loading, not
                 // on total_time - measured 0.90 on suffocate_camera against
                 // 0.99 on cobweb_fall at the same tick count).
                 .append("\",\"fog_color1\":").append(recFogColor1(mc))
                 // Rain and thunder scale getSunBrightness (each by up to
                 // 5/16), which drives the whole lightmap. Nothing in a tape
                 // recorded these, so magma has always replayed rain frames
                 // as clear sky.
                 .append(",\"rain_strength\":").append(mc.world.getRainStrength(1.0F))
                 .append(",\"thunder_strength\":").append(mc.world.getThunderStrength(1.0F));
                // active gamerules: scenario setup applies them via commands
                // but nothing recorded the resulting truth (doMobSpawning /
                // doDaylightCycle change what replay must simulate).
                // Read the INTEGRATED SERVER world's rules: the client copy is
                // only synced at join, except doDaylightCycle (inferred from
                // SPacketTimeUpdate's negated worldTime), so /gamerule setup
                // commands never reach mc.world.getGameRules() (caught on
                // silverfish 172741Z: header said naturalRegeneration true
                // while server-side it was false).
                try {
                    net.minecraft.world.GameRules gr = null;
                    net.minecraft.server.MinecraftServer srv = mc.getIntegratedServer();
                    if (srv != null && srv.worlds != null && srv.worlds.length > 0
                            && srv.worlds[0] != null)
                        gr = srv.worlds[0].getGameRules();
                    if (gr == null) gr = mc.world.getGameRules();
                    h.append(",\"gamerules\":{");
                    String[] grKeys = gr.getRules();
                    for (int gi = 0; gi < grKeys.length; ++gi) {
                        if (gi > 0) h.append(",");
                        h.append("\"").append(grKeys[gi]).append("\":\"")
                         .append(gr.getString(grKeys[gi])).append("\"");
                    }
                    h.append("}");
                } catch (Throwable ig) {}
                h.append(",\"velocity_packets\":1,\"position_packets\":1");
                if (recContract) {
                    String osName = System.getProperty("os.name", "").toLowerCase();
                    String osKey = osName.contains("mac") ? "darwin"
                        : (osName.contains("win") ? "win32" : "linux");
                    String archName = System.getProperty("os.arch", "").toLowerCase();
                    String archKey = ("amd64".equals(archName)
                        || "x86_64".equals(archName)) ? "x86_64"
                        : (("aarch64".equals(archName) || "arm64".equals(archName))
                           ? "arm64" : archName);
                    h.append(",\"frame_w\":").append(mc.displayWidth)
                     .append(",\"frame_h\":").append(mc.displayHeight)
                     .append(",\"renderer_provenance\":{\"os\":\"")
                     .append(osKey).append("\",\"arch\":\"").append(archKey)
                     .append("\",\"api\":\"gl\",\"resolution\":\"")
                     .append(mc.displayWidth).append("x").append(mc.displayHeight)
                     .append("\",\"scaling\":").append(mc.gameSettings.guiScale)
                     .append(",\"hide_gui\":")
                     .append(mc.gameSettings.hideGUI ? "true" : "false")
                     .append("}")
                     .append(",\"capabilities\":[\"block_finals\",")
                     .append("\"container_identity\",\"gui_clicks\",")
                     .append("\"inventory_keyframes\",\"renderer_provenance\",")
                     .append("\"world_snapshot\"");
                    if (recDigTrace) h.append(",\"dig_trace\"");
                    h.append("]");
                }
                /* Presence of dig_trace:1 opts parsers/comparators into per-tick
                 * dig controller rows. Legacy tapes omit the key entirely. */
                if (recDigTrace) h.append(",\"dig_trace\":1");
                h.append("}");
                recWriter.println(h.toString());
                recWriter.flush();
                // world snapshot: flush all dirty chunks to disk and copy the
                // save's region files next to the tape. Replay can then load
                // the ACTUAL world instead of trusting regenerated worldgen -
                // kills the populate-order/provenance divergence class
                // (OPEN_DIVERGENCES #1/#8/#18-20) at the source.
                String snapErr = null;
                recSnapRoot = file.replaceAll("\\.jsonl$", "") + "_world";
                try {
                    // WHOLE save dir: level.dat + playerdata (initial inventory/
                    // xp/potions) + all dimension regions + data/ (maps,
                    // villages). Region-only proved insufficient (audit
                    // 2026-07-12): held-item/XP truth lives in playerdata.
                    snapshotSaveDir(mc, new java.io.File(recSnapRoot), false);
                } catch (Throwable t) { snapErr = String.valueOf(t); }
                reply(r, "{\"ok\":true,\"file\":\"" + file + "\""
                    + (snapErr != null ? ",\"snapshot_error\":\"" + snapErr.replace('"','\'') + "\"" : "")
                    + "}");
            } catch (Exception ex) {
                recActive = false;
                recDigTrace = false;
                recContract = false;
                clearRecBlockChanges();
                clearRecGuiClicks();
                clearRecGuiLifecycle();
                recWriter = null;
                reply(r, err("recstart: " + ex));
            }
            return;
        }
        if (r.cmd.equals("recstop")) {
            long n = recTick;
            recActive = false;
            recDigTrace = false;
            recContract = false;
            recNeighborNotifyDepth = 0;
            recDigDestroyDepth = 0;
            clearRecBlockChanges();
            clearRecGuiClicks();
            clearRecGuiLifecycle();
            if (recWriter != null) { recWriter.flush(); recWriter.close(); recWriter = null; }
            if (recGeomWriter != null) { recGeomWriter.flush(); recGeomWriter.close(); recGeomWriter = null; }
            // Second snapshot pass, ADD-ONLY (see snapshotSaveDir): a dimension
            // the player first enters DURING the recording does not exist on
            // disk at recstart, so its region files can never be in the
            // recstart copy. Without this pass the replay falls back to
            // magma's own generator for that dimension, and the Nether's
            // populate() decoration (fire, lava springs, glowstone, quartz -
            // ChunkProviderHell.populate, whose rand is NOT reseeded per chunk
            // and is therefore chunk-load-order dependent, i.e. not derivable
            // from the seed) is simply absent from the replayed world.
            String snapErr = null;
            int added = 0;
            if (recSnapRoot != null) {
                try { added = snapshotSaveDir(mc, new java.io.File(recSnapRoot), true); }
                catch (Throwable t) { snapErr = String.valueOf(t); }
                recSnapRoot = null;
            }
            reply(r, "{\"ok\":true,\"ticks\":" + n + ",\"snapshot_added\":" + added
                + (snapErr != null ? ",\"snapshot_error\":\"" + snapErr.replace('"','\'') + "\"" : "")
                + "}");
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
                : "../../render-opt/kernels/14_light_query/golden";
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
                : "../../render-opt/kernels/19_fluid_height/golden";
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
                : "../../render-opt/kernels/18_biome_color_blend/golden";
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
                : "../../render-opt/kernels/21_should_side_render/golden";
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
                : ("../../render-opt/kernels/"
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
                : "../../render-opt/kernels/20_fluid_quad_gen/golden";
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
                : "../../render-opt/kernels/12_ao_vertex_brightness/golden";
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
                : "../../render-opt/kernels/29_particle_update/golden";
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
                : "../../render-opt/kernels/17_skylight_gen/golden";
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
                : "../../render-opt/kernels/37_entity_limb_anim/golden";
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
                : "../../render-opt/kernels/16_light_propagation/golden";
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
                : "../../render-opt/kernels/11_lightmap/golden";
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
                : "../../render-opt/kernels/26_chunk_rebuild_loop/golden";
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
        if (r.cmd.equals("capture_translucent_draw")) {
            captureTranslucentDraw(mc, r.action, r);
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

        // pin_preview_anim: freeze the local player's living-anim clock for inventory
        // player-preview goldens. drawEntityOnScreen calls doRenderEntity(..., partial=1),
        // so ageInTicks = ticksExisted + 1. Default ticksExisted=-1 => age=0 (idle arm Z
        // ±0.10, no arm X bob). A/B captures under the pin share one model pose.
        if (r.cmd.equals("pin_preview_anim")) {
            try {
                boolean en = !r.action.has("enable") || r.action.get("enable").getAsBoolean();
                pinPreviewAnim = en;
                if (r.action.has("ticks_existed"))
                    pinPreviewTicksExisted = r.action.get("ticks_existed").getAsInt();
                else if (en)
                    pinPreviewTicksExisted = -1;
                if (!en) pinSavedValid = false;
                reply(r, "{\"ok\":true,\"enable\":" + pinPreviewAnim
                    + ",\"ticks_existed\":" + pinPreviewTicksExisted + "}");
            } catch (Throwable t) { reply(r, err("pin_preview_anim: " + t)); }
            return;
        }

        // frame: re-render world+HUD at partialTicks=1.0 into the FBO, then write PNG.
        // Tick-boundary capture matches tape keyframes (tb:1 / hud:1). Optional
        // "rerender":false skips the re-render and just dumps the current FBO.
        //
        // frame_pair: two re-renders + two PNG dumps on the SAME client-thread
        // turn with no intervening game tick. Oracle A/B goldens that previously
        // raced free-running ticks (portal underlay 1-LSB, underwater fog,
        // entity pose) use this so twins measure true same-state noise.
        //
        // Malmo ClientStateMachine sets hideGUI=true for the episode (F1 view).
        // EntityRenderer.renderHand skips ItemRenderer when hideGUI is set, so
        // forced GUI overlays still paint but the first-person viewmodel is
        // missing. Oracle A/B frames must clear hideGUI for the re-render pass.
        //
        // When a pending entity render pin is active, apply it on the client entity
        // immediately before renderWorld and restore afterward so no intervening
        // client tick can overwrite squishFactor/deathTicks.
        //
        // Sticky use-pose is re-applied HERE immediately before renderWorld:
        // processKeyBinds (earlier this tick) may have already onStoppedUsingItem
        // because the physical use key is not held. Without this, bow/eat/shield
        // goldens capture idle rest tips despite a successful hud_pin.
        if (r.cmd.equals("frame") || r.cmd.equals("frame_pair")) {
            try {
                final boolean isPair = r.cmd.equals("frame_pair");
                String fileA = isPair
                    ? r.action.get("file_a").getAsString()
                    : r.action.get("file").getAsString();
                String fileB = isPair ? r.action.get("file_b").getAsString() : null;
                int fw = mc.displayWidth, fh = mc.displayHeight;
                boolean rerender = !r.action.has("rerender")
                    || r.action.get("rerender").getAsBoolean();
                boolean hud = false, tb = false;
                int pinApplied = 0;
                int pinClientEid = -1;
                JsonObject useAtRender = null;
                boolean prevHide = mc.gameSettings.hideGUI;
                int prevTp = mc.gameSettings.thirdPersonView;
                // Fixed nanoTime for both A/B re-renders so any clock-derived
                // shader/path does not inject A/B residual inside one pair.
                // finishTimeNano is a FUTURE deadline for updateChunks; passing
                // pairNano itself (already in the past by renderWorld) skips
                // remaining chunk uploads and can make pass B miss geometry.
                final long pairNano = System.nanoTime();
                final long pairDeadline = pairNano + 10_000_000_000L;
                float snapFog1 = 0.0F, snapFog2 = 0.0F;
                boolean haveFogSnap = false;
                if (mc.entityRenderer != null) {
                    try {
                        java.lang.reflect.Field f1 =
                            net.minecraft.client.renderer.EntityRenderer.class
                                .getDeclaredField("fogColor1");
                        f1.setAccessible(true);
                        java.lang.reflect.Field f2 =
                            net.minecraft.client.renderer.EntityRenderer.class
                                .getDeclaredField("fogColor2");
                        f2.setAccessible(true);
                        snapFog1 = f1.getFloat(mc.entityRenderer);
                        snapFog2 = f2.getFloat(mc.entityRenderer);
                        haveFogSnap = true;
                    } catch (Throwable ig) {}
                }
                JsonObject passA = null, passB = null;
                int nPasses = isPair ? 2 : 1;
                for (int pass = 0; pass < nPasses; ++pass) {
                    String file = (pass == 0) ? fileA : fileB;
                    if (rerender && mc.world != null && mc.player != null
                            && mc.entityRenderer != null) {
                        RenderPinSnap pinSnap = null;
                        try {
                            // First-person viewmodel requires: hideGUI=false,
                            // thirdPersonView=0, not spectator, not sleeping.
                            mc.gameSettings.hideGUI = false;
                            mc.gameSettings.thirdPersonView = 0;
                            if (haveFogSnap) restoreFogColors(mc, snapFog1, snapFog2);
                            // Sticky portal warp phase: updateRenderer may have
                            // advanced rendererUpdateCount since the last hud_pin.
                            applyPinnedPortalPhase(mc);
                            // Sticky portal overlay alpha (timeInPortal decays every
                            // free-running client tick when not in a portal block).
                            applyPinnedPortalTime(mc);
                            // Re-freeze camera so A/B wall registration matches
                            // under large viewmodel silhouettes (drawn bow).
                            applyStickyPosePin(mc);
                            // Re-assert sticky use pose + equip + stack identity.
                            // processKeyBinds may have already onStoppedUsingItem.
                            if (pinUseActive) {
                                useAtRender = applyUsePosePin(mc);
                            }
                            if (renderPinActive) {
                                pinSnap = applyPendingRenderPin(mc);
                                if (pinSnap != null && pinSnap.entity != null) {
                                    pinApplied = 1;
                                    pinClientEid = pinSnap.entity.getEntityId();
                                }
                            }
                            net.minecraft.client.shader.Framebuffer fb = mc.getFramebuffer();
                            fb.bindFramebuffer(true);
                            mc.entityRenderer.renderWorld(1.0F, pairDeadline);
                            try {
                                mc.entityRenderer.setupOverlayRendering();
                                mc.ingameGUI.renderGameOverlay(1.0F);
                                hud = true;
                                if (mc.currentScreen != null) {
                                    net.minecraft.client.gui.ScaledResolution sr =
                                        new net.minecraft.client.gui.ScaledResolution(mc);
                                    int mx = org.lwjgl.input.Mouse.getX() * sr.getScaledWidth()
                                        / mc.displayWidth;
                                    int my = sr.getScaledHeight()
                                        - org.lwjgl.input.Mouse.getY() * sr.getScaledHeight()
                                        / mc.displayHeight - 1;
                                    mc.currentScreen.drawScreen(mx, my, 1.0F);
                                }
                            } catch (Throwable ig2) {}
                            JsonObject passDiag = framePassDiag(mc);
                            if (pass == 0) passA = passDiag;
                            else passB = passDiag;
                            fb.unbindFramebuffer();
                            tb = true;
                        } catch (Throwable ig) {
                            /* re-render failed; still restore gameSettings / pin below */
                        } finally {
                            if (pinSnap != null) {
                                try { restoreRenderPin(pinSnap); } catch (Throwable ig3) {}
                            }
                            mc.gameSettings.hideGUI = prevHide;
                            mc.gameSettings.thirdPersonView = prevTp;
                        }
                    }
                    java.awt.image.BufferedImage img =
                        net.minecraft.util.ScreenShotHelper.createScreenshot(
                            fw, fh, mc.getFramebuffer());
                    javax.imageio.ImageIO.write(img, "png", new java.io.File(file));
                }
                if (haveFogSnap) restoreFogColors(mc, snapFog1, snapFog2);
                StringBuilder resp = new StringBuilder(256);
                resp.append("{\"ok\":true");
                if (isPair) {
                    resp.append(",\"file_a\":\"").append(fileA).append("\"")
                        .append(",\"file_b\":\"").append(fileB).append("\"")
                        .append(",\"pair\":1")
                        .append(",\"fog_restored\":").append(haveFogSnap ? 1 : 0);
                    if (passA != null) resp.append(",\"pass_a\":").append(passA.toString());
                    if (passB != null) resp.append(",\"pass_b\":").append(passB.toString());
                } else {
                    resp.append(",\"file\":\"").append(fileA).append("\"");
                }
                resp.append(",\"w\":").append(fw)
                  .append(",\"h\":").append(fh)
                  .append(",\"tb\":").append(tb ? 1 : 0)
                  .append(",\"hud\":").append(hud ? 1 : 0)
                  .append(",\"render_pin\":").append(pinApplied)
                  .append(",\"render_pin_eid\":").append(pinClientEid);
                // Sticky portal_phase / timeInPortal diagnostic (pinned or live).
                {
                    int phase = pinPortalPhase;
                    if (phase < 0) {
                        try {
                            if (mc.entityRenderer != null) {
                                java.lang.reflect.Field fPhase =
                                    net.minecraft.client.renderer.EntityRenderer.class
                                        .getDeclaredField("rendererUpdateCount");
                                fPhase.setAccessible(true);
                                phase = fPhase.getInt(mc.entityRenderer);
                            }
                        } catch (Throwable ig) {}
                    }
                    resp.append(",\"portal_phase\":").append(phase);
                    if (pinPortalTimeActive) {
                        resp.append(",\"portal_time_pin\":").append(pinPortalTime);
                        if (mc.player != null)
                            resp.append(",\"portal_time_live\":")
                                .append(mc.player.timeInPortal);
                    }
                }
                resp.append(",\"pin_use_active\":").append(pinUseActive);
                if (pinUseActive)
                    resp.append(",\"pin_use_remaining\":").append(pinUseRemaining);
                if (useAtRender != null) {
                    if (useAtRender.has("branch"))
                        resp.append(",\"use_branch\":\"").append(
                            useAtRender.get("branch").getAsString()).append("\"");
                    if (useAtRender.has("hand_active"))
                        resp.append(",\"hand_active\":").append(
                            useAtRender.get("hand_active").getAsBoolean());
                    if (useAtRender.has("use_count"))
                        resp.append(",\"use_count\":").append(
                            useAtRender.get("use_count").getAsInt());
                    if (useAtRender.has("pulling"))
                        resp.append(",\"model_pulling\":").append(
                            useAtRender.get("pulling").getAsFloat());
                    if (useAtRender.has("pull"))
                        resp.append(",\"model_pull\":").append(
                            useAtRender.get("pull").getAsFloat());
                    if (useAtRender.has("blocking"))
                        resp.append(",\"model_blocking\":").append(
                            useAtRender.get("blocking").getAsFloat());
                    if (useAtRender.has("stack_id_eq"))
                        resp.append(",\"stack_id_eq\":").append(
                            useAtRender.get("stack_id_eq").getAsBoolean());
                    if (useAtRender.has("ir_id_eq"))
                        resp.append(",\"ir_id_eq\":").append(
                            useAtRender.get("ir_id_eq").getAsBoolean());
                }
                resp.append("}");
                r.resp.offer(resp.toString());
            } catch (Exception ex) { r.resp.offer(err("frame: " + ex)); }
            return;
        }

        // hud_pin: freeze deterministic player/HUD state for oracle A/B captures.
        // Pins camera, vitals, GuiIngame health bookkeeping, active item use,
        // fire/portal, optional boss bar, and optional death hold. Does NOT
        // advance the world; caller uses frame{} for the PNG dump.
        if (r.cmd.equals("hud_pin")) {
            try {
                if (mc.player == null || mc.world == null) {
                    r.resp.offer(err("no world")); return;
                }
                EntityPlayerSP p = mc.player;
                JsonObject a = r.action;

                if (a.has("hold_death")) holdDeath = a.get("hold_death").getAsBoolean();

                // Pose + gravity pin (same contract as set_pose).
                if (a.has("x") && a.has("y") && a.has("z")) {
                    float yaw = a.has("yaw") ? a.get("yaw").getAsFloat() : p.rotationYaw;
                    float pitch = a.has("pitch") ? a.get("pitch").getAsFloat() : p.rotationPitch;
                    boolean noG = !a.has("no_gravity") || a.get("no_gravity").getAsBoolean();
                    clearKeys(mc);
                    p.setNoGravity(noG);
                    p.motionX = p.motionY = p.motionZ = 0.0D;
                    p.fallDistance = 0.0F;
                    p.setPositionAndRotation(a.get("x").getAsDouble(), a.get("y").getAsDouble(),
                        a.get("z").getAsDouble(), yaw, pitch);
                    // Freeze interpolation sources so partialTicks=1 and any
                    // free-run tick cannot leave prev!=current camera state.
                    p.prevPosX = p.posX; p.prevPosY = p.posY; p.prevPosZ = p.posZ;
                    p.lastTickPosX = p.posX; p.lastTickPosY = p.posY; p.lastTickPosZ = p.posZ;
                    p.prevRotationYaw = yaw; p.prevRotationPitch = pitch;
                    p.renderYawOffset = yaw; p.prevRenderYawOffset = yaw;
                    p.rotationYawHead = yaw; p.prevRotationYawHead = yaw;
                    pinPoseActive = true;
                    pinPoseX = a.get("x").getAsDouble();
                    pinPoseY = a.get("y").getAsDouble();
                    pinPoseZ = a.get("z").getAsDouble();
                    pinPoseYaw = yaw;
                    pinPosePitch = pitch;
                    final double fx = pinPoseX, fy = pinPoseY, fz = pinPoseZ;
                    final float fyaw = yaw, fpitch = pitch;
                    final boolean fng = noG;
                    net.minecraft.server.MinecraftServer srv = mc.getIntegratedServer();
                    if (srv != null) {
                        srv.addScheduledTask(new Runnable() { public void run() {
                            try {
                                java.util.List<net.minecraft.entity.player.EntityPlayerMP> ps =
                                    srv.getPlayerList().getPlayers();
                                if (ps.isEmpty()) return;
                                net.minecraft.entity.player.EntityPlayerMP pl = ps.get(0);
                                pl.setNoGravity(fng);
                                pl.motionX = pl.motionY = pl.motionZ = 0.0D;
                                pl.connection.setPlayerLocation(fx, fy, fz, fyaw, fpitch);
                            } catch (Throwable ig) {}
                        }});
                    }
                }

                // Hotbar / armor via replaceitem-style stacks (id, count, meta).
                if (a.has("hotbar") && a.get("hotbar").isJsonArray()) {
                    com.google.gson.JsonArray hb = a.getAsJsonArray("hotbar");
                    for (int i = 0; i < 9 && i < hb.size(); i++) {
                        if (hb.get(i).isJsonNull()) {
                            p.inventory.mainInventory.set(i, net.minecraft.item.ItemStack.EMPTY);
                            continue;
                        }
                        com.google.gson.JsonArray slot = hb.get(i).getAsJsonArray();
                        int id = slot.get(0).getAsInt();
                        int count = slot.size() > 1 ? slot.get(1).getAsInt() : 1;
                        int meta = slot.size() > 2 ? slot.get(2).getAsInt() : 0;
                        if (id <= 0 || count <= 0) {
                            p.inventory.mainInventory.set(i, net.minecraft.item.ItemStack.EMPTY);
                        } else {
                            net.minecraft.item.Item it =
                                net.minecraft.item.Item.getItemById(id);
                            if (it != null)
                                p.inventory.mainInventory.set(i,
                                    new net.minecraft.item.ItemStack(it, count, meta));
                        }
                    }
                    p.inventory.currentItem = a.has("hotbar_sel")
                        ? a.get("hotbar_sel").getAsInt() : 0;
                }
                if (a.has("armor") && a.get("armor").isJsonArray()) {
                    com.google.gson.JsonArray ar = a.getAsJsonArray("armor");
                    // order: feet, legs, chest, head (armorInventory indices 0..3)
                    for (int i = 0; i < 4 && i < ar.size(); i++) {
                        if (ar.get(i).isJsonNull() || ar.get(i).getAsInt() <= 0) {
                            p.inventory.armorInventory.set(i, net.minecraft.item.ItemStack.EMPTY);
                            continue;
                        }
                        net.minecraft.item.Item it =
                            net.minecraft.item.Item.getItemById(ar.get(i).getAsInt());
                        if (it != null)
                            p.inventory.armorInventory.set(i,
                                new net.minecraft.item.ItemStack(it, 1, 0));
                    }
                    // InventoryPlayer.getTotalArmorValue reads the live list;
                    // force a container detect so client attributes refresh.
                    p.inventory.markDirty();
                }

                // Vitals (client entity; server copy follows for multiplayer-safe fields).
                if (a.has("health")) {
                    float h = a.get("health").getAsFloat();
                    p.setHealth(h);
                    if (h > 0.0F) holdDeath = false;
                }
                if (a.has("food")) {
                    p.getFoodStats().setFoodLevel(a.get("food").getAsInt());
                }
                if (a.has("air")) {
                    p.setAir(a.get("air").getAsInt());
                }
                if (a.has("absorption")) {
                    p.setAbsorptionAmount(a.get("absorption").getAsFloat());
                }
                if (a.has("xp_level") || a.has("xp_frac")) {
                    if (a.has("xp_level")) p.experienceLevel = a.get("xp_level").getAsInt();
                    if (a.has("xp_frac")) p.experience = a.get("xp_frac").getAsFloat();
                    p.experienceTotal = 0;
                }
                if (a.has("fire")) {
                    // Pin Entity.fire ticks directly so first-person fire overlay
                    // (EntityRenderer.renderFireInFirstPerson / isBurning) is on.
                    // Prefer direct field write; avoid setFire() which can enqueue
                    // client/server work that hangs headless llvmpipe under load.
                    final int ft = a.get("fire").getAsInt();
                    try {
                        java.lang.reflect.Field ff =
                            net.minecraft.entity.Entity.class.getDeclaredField("fire");
                        ff.setAccessible(true);
                        if (ft > 0) {
                            ff.setInt(p, ft);
                        } else {
                            p.extinguish();
                            ff.setInt(p, -1);
                        }
                    } catch (Throwable ig) {
                        if (ft > 0) p.setFire(Math.max(1, ft / 20));
                        else p.extinguish();
                    }
                }
                if (a.has("portal")) {
                    float pt = a.get("portal").getAsFloat();
                    p.timeInPortal = pt;
                    p.prevTimeInPortal = pt;
                    // Sticky: free-running ticks decay timeInPortal; frame{}
                    // re-applies so A/B share the same fourth-power alpha.
                    if (pt > 0.0F) {
                        pinPortalTimeActive = true;
                        pinPortalTime = pt;
                    } else {
                        pinPortalTimeActive = false;
                        pinPortalTime = 0.0F;
                    }
                }
                // Freeze EntityRenderer.rendererUpdateCount so the portal camera
                // warp (setupCameraTransform rotate/scale on axis (0,1,1)) is
                // identical on A and B frame{} re-renders. Client ticks still
                // advance updateRenderer between socket commands; sticky pin is
                // re-applied immediately before each frame{} re-render.
                if (a.has("portal_phase")) {
                    pinPortalPhase = a.get("portal_phase").getAsInt();
                    applyPinnedPortalPhase(mc);
                } else if (a.has("portal") && a.get("portal").getAsFloat() <= 0.0F) {
                    // Clear sticky phase when leaving portal pin states.
                    pinPortalPhase = -1;
                    pinPortalTimeActive = false;
                    pinPortalTime = 0.0F;
                }
                if (a.has("hurt_time")) {
                    p.hurtTime = a.get("hurt_time").getAsInt();
                    p.maxHurtTime = a.has("max_hurt_time")
                        ? a.get("max_hurt_time").getAsInt()
                        : Math.max(p.hurtTime, 10);
                    p.attackedAtYaw = a.has("hurt_yaw")
                        ? a.get("hurt_yaw").getAsFloat() : 0.0F;
                    p.hurtResistantTime = Math.max(p.hurtResistantTime, p.maxHurtTime);
                }

                // clear_effects MUST run before applying requested effects; previous
                // order wiped every pin that set both (hunger/absorption contamination).
                if (a.has("clear_effects") && a.get("clear_effects").getAsBoolean()) {
                    p.clearActivePotions();
                }
                // Potion effects (id + duration ticks + amplifier).
                if (a.has("effects") && a.get("effects").isJsonArray()) {
                    for (com.google.gson.JsonElement el : a.getAsJsonArray("effects")) {
                        com.google.gson.JsonObject pe = el.getAsJsonObject();
                        int id = pe.get("id").getAsInt();
                        int dur = pe.has("duration") ? pe.get("duration").getAsInt() : 200;
                        int amp = pe.has("amplifier") ? pe.get("amplifier").getAsInt() : 0;
                        net.minecraft.potion.Potion pot =
                            net.minecraft.potion.Potion.getPotionById(id);
                        if (pot != null)
                            p.addPotionEffect(new net.minecraft.potion.PotionEffect(
                                pot, dur, amp, false, false));
                    }
                }

                // First-person viewmodel visibility + equip pin.
                // Malmo missions force hideGUI=true (F1), which EntityRenderer
                // treats as "no hand". Always clear for oracle pins; frame{}
                // also clears for the re-render pass.
                mc.gameSettings.hideGUI = false;
                mc.gameSettings.thirdPersonView = 0;

                // Fully raise the main-hand viewmodel. ItemRenderer passes
                // equipArg = 1 - equippedProgressMainHand into transforms, so
                // equippedProgressMainHand=1 => equipArg=0 => raised (vanilla
                // fully-equipped). Hotbar swaps leave progress near 0 (hidden)
                // for several ticks and oracle hand frames capture empty walls.
                float pinnedEquip = 1.0F;
                int equipPinned = 0;
                try {
                    java.lang.reflect.Field fEq =
                        net.minecraft.client.renderer.ItemRenderer.class
                            .getDeclaredField("equippedProgressMainHand");
                    java.lang.reflect.Field fPrev =
                        net.minecraft.client.renderer.ItemRenderer.class
                            .getDeclaredField("prevEquippedProgressMainHand");
                    java.lang.reflect.Field fItem =
                        net.minecraft.client.renderer.ItemRenderer.class
                            .getDeclaredField("itemStackMainHand");
                    java.lang.reflect.Field fItemOff =
                        net.minecraft.client.renderer.ItemRenderer.class
                            .getDeclaredField("itemStackOffHand");
                    java.lang.reflect.Field fEqOff =
                        net.minecraft.client.renderer.ItemRenderer.class
                            .getDeclaredField("equippedProgressOffHand");
                    java.lang.reflect.Field fPrevOff =
                        net.minecraft.client.renderer.ItemRenderer.class
                            .getDeclaredField("prevEquippedProgressOffHand");
                    fEq.setAccessible(true); fPrev.setAccessible(true);
                    fItem.setAccessible(true); fItemOff.setAccessible(true);
                    fEqOff.setAccessible(true); fPrevOff.setAccessible(true);
                    net.minecraft.client.renderer.ItemRenderer ir =
                        mc.getItemRenderer();
                    // Default 1.0 = fully equipped / raised.
                    pinnedEquip = a.has("equip_progress")
                        ? a.get("equip_progress").getAsFloat() : 1.0F;
                    fEq.setFloat(ir, pinnedEquip);
                    fPrev.setFloat(ir, pinnedEquip);
                    fEqOff.setFloat(ir, 1.0F);
                    fPrevOff.setFloat(ir, 1.0F);
                    // Same inventory reference - not copy (see applyUsePosePin).
                    // ItemBow.pulling / ItemShield.blocking require == not equals.
                    net.minecraft.item.ItemStack held = p.getHeldItemMainhand();
                    if (!held.isEmpty())
                        fItem.set(ir, held);
                    else
                        fItem.set(ir, net.minecraft.item.ItemStack.EMPTY);
                    net.minecraft.item.ItemStack off = p.getHeldItemOffhand();
                    if (!off.isEmpty())
                        fItemOff.set(ir, off);
                    else
                        fItemOff.set(ir, net.minecraft.item.ItemStack.EMPTY);
                    equipPinned = 1;
                } catch (Throwable ig) { equipPinned = 0; }

                // Active hand use: action 0 none, 1 eat/drink, 2 block, 3 bow.
                // remaining = getItemInUseCount; bow_pull = max - remaining.
                // Sticky pin survives free-running ticks (processKeyBinds would
                // otherwise onStoppedUsingItem every tick while use key is up)
                // and is re-applied in frame{} immediately before re-render.
                JsonObject useDiag = null;
                if (a.has("use_action") || a.has("bow_pull")) {
                    int remaining = 0;
                    boolean wantUse = false;
                    net.minecraft.item.ItemStack stack = p.getHeldItemMainhand();
                    if (a.has("bow_pull")) {
                        int pull = a.get("bow_pull").getAsInt();
                        if (!stack.isEmpty() && pull > 0) {
                            remaining = stack.getMaxItemUseDuration() - pull;
                            if (remaining < 1) remaining = 1;
                            wantUse = true;
                        }
                    } else if (a.has("use_action")) {
                        int action = a.get("use_action").getAsInt();
                        if (action <= 0) {
                            clearUsePosePin(mc);
                            wantUse = false;
                        } else if (!stack.isEmpty()) {
                            remaining = a.has("use_remaining")
                                ? a.get("use_remaining").getAsInt()
                                : stack.getMaxItemUseDuration() / 2;
                            if (remaining < 1) remaining = 1;
                            wantUse = true;
                        }
                    }
                    if (wantUse) {
                        armUsePosePin(remaining);
                        useDiag = applyUsePosePin(mc);
                        // Prefer equip_progress from the use-pin path when set.
                        if (useDiag.has("equip_pinned"))
                            equipPinned = useDiag.get("equip_pinned").getAsInt();
                        if (useDiag.has("equip_progress"))
                            pinnedEquip = useDiag.get("equip_progress").getAsFloat();
                    }
                }

                // GuiIngame health bookkeeping: pin flash phase AND updateCounter.
                // flash=1 => healthUpdateCounter > updateCounter and (delta/3)%2 == 1
                // flash=0 with window still open => (delta/3)%2 == 0
                // Heart-row jitter uses rand(updateCounter*312871); freeze updateCounter
                // to a fixed value so A/B noise is not 20-80 from tick race.
                if (a.has("hud_flash") || a.has("hud_health") || a.has("hud_last_health")
                        || a.has("hud_update_counter")) {
                    try {
                        net.minecraft.client.gui.GuiIngame gui = mc.ingameGUI;
                        java.lang.reflect.Field fUC =
                            net.minecraft.client.gui.GuiIngame.class
                                .getDeclaredField("updateCounter");
                        java.lang.reflect.Field fHUC =
                            net.minecraft.client.gui.GuiIngame.class
                                .getDeclaredField("healthUpdateCounter");
                        java.lang.reflect.Field fPH =
                            net.minecraft.client.gui.GuiIngame.class
                                .getDeclaredField("playerHealth");
                        java.lang.reflect.Field fLPH =
                            net.minecraft.client.gui.GuiIngame.class
                                .getDeclaredField("lastPlayerHealth");
                        fUC.setAccessible(true); fHUC.setAccessible(true);
                        fPH.setAccessible(true); fLPH.setAccessible(true);
                        // Fixed counter for deterministic heart jitter across A/B.
                        int uc = a.has("hud_update_counter")
                            ? a.get("hud_update_counter").getAsInt() : 1000;
                        fUC.setInt(gui, uc);
                        int hNow = a.has("hud_health")
                            ? a.get("hud_health").getAsInt()
                            : (int)Math.ceil(p.getHealth());
                        int hLast = a.has("hud_last_health")
                            ? a.get("hud_last_health").getAsInt()
                            : hNow;
                        fPH.setInt(gui, hNow);
                        fLPH.setInt(gui, hLast);
                        if (a.has("hud_flash")) {
                            boolean flash = a.get("hud_flash").getAsBoolean();
                            // delta = huc - uc; need delta > 0 and (delta/3)%2 == flash?1:0
                            // use delta=3 for flash on, delta=6 for flash off (still in window)
                            long delta = flash ? 3L : 6L;
                            fHUC.setLong(gui, (long)uc + delta);
                        } else if (a.has("health_update_counter")) {
                            fHUC.setLong(gui, a.get("health_update_counter").getAsLong());
                        }
                    } catch (Throwable t) {
                        r.resp.offer(err("hud_pin flash: " + t)); return;
                    }
                }

                // Boss bar: inject PINK progress bar into GuiBossOverlay map.
                if (a.has("boss")) {
                    try {
                        JsonObject b = a.getAsJsonObject("boss");
                        boolean show = !b.has("show") || b.get("show").getAsBoolean();
                        net.minecraft.client.gui.GuiBossOverlay overlay =
                            mc.ingameGUI.getBossOverlay();
                        if (!show) {
                            overlay.clearBossInfos();
                        } else {
                            float frac = b.has("frac") ? b.get("frac").getAsFloat() : 0.5f;
                            String name = b.has("name") ? b.get("name").getAsString()
                                : "Ender Dragon";
                            net.minecraft.world.BossInfoServer bi =
                                new net.minecraft.world.BossInfoServer(
                                    new net.minecraft.util.text.TextComponentString(name),
                                    net.minecraft.world.BossInfo.Color.PINK,
                                    net.minecraft.world.BossInfo.Overlay.PROGRESS);
                            // set via super to avoid BossInfoServer player fan-out
                            try {
                                java.lang.reflect.Field fPct0 =
                                    net.minecraft.world.BossInfo.class
                                        .getDeclaredField("percent");
                                fPct0.setAccessible(true);
                                fPct0.setFloat(bi, frac);
                            } catch (Throwable ig) {
                                bi.setPercent(frac);
                            }
                            net.minecraft.network.play.server.SPacketUpdateBossInfo pkt =
                                new net.minecraft.network.play.server.SPacketUpdateBossInfo(
                                    net.minecraft.network.play.server.SPacketUpdateBossInfo
                                        .Operation.ADD, bi);
                            overlay.clearBossInfos();
                            overlay.read(pkt);
                            // Freeze lerp at exact frac (rawPercent == percent).
                            try {
                                java.lang.reflect.Field fmap =
                                    net.minecraft.client.gui.GuiBossOverlay.class
                                        .getDeclaredField("mapBossInfos");
                                fmap.setAccessible(true);
                                @SuppressWarnings("unchecked")
                                java.util.Map<java.util.UUID, net.minecraft.world.BossInfoLerping> map =
                                    (java.util.Map<java.util.UUID, net.minecraft.world.BossInfoLerping>)
                                        fmap.get(overlay);
                                if (!map.isEmpty()) {
                                    net.minecraft.world.BossInfoLerping bl =
                                        map.values().iterator().next();
                                    java.lang.reflect.Field fRaw =
                                        net.minecraft.world.BossInfoLerping.class
                                            .getDeclaredField("rawPercent");
                                    java.lang.reflect.Field fPct =
                                        net.minecraft.world.BossInfo.class
                                            .getDeclaredField("percent");
                                    fRaw.setAccessible(true); fPct.setAccessible(true);
                                    fRaw.setFloat(bl, frac);
                                    fPct.setFloat(bl, frac);
                                }
                            } catch (Throwable ig) {}
                        }
                    } catch (Throwable t) {
                        r.resp.offer(err("hud_pin boss: " + t)); return;
                    }
                }

                // Death hold: health 0 + optional GuiGameOver (needs strip.menus=false).
                if (a.has("dead") && a.get("dead").getAsBoolean()) {
                    holdDeath = true;
                    p.setHealth(0.0F);
                    if (!wasDead) { deaths++; wasDead = true; }
                    if (!QLaunch.STRIP_MENUS) {
                        try {
                            mc.displayGuiScreen(
                                new net.minecraft.client.gui.GuiGameOver(null));
                        } catch (Throwable ig) {}
                    }
                }

                // Real respawn path: release death hold, respawnPlayer(), close GuiGameOver.
                // Setting health alone leaves deathTime / GuiGameOver and contaminates later
                // captures. Call this after every death-screen golden.
                if (a.has("respawn") && a.get("respawn").getAsBoolean()) {
                    holdDeath = false;
                    try {
                        if (p.getHealth() <= 0.0F || p.deathTime > 0
                                || mc.currentScreen instanceof net.minecraft.client.gui.GuiGameOver) {
                            p.respawnPlayer();
                        }
                    } catch (Throwable ig) {}
                    try {
                        if (mc.currentScreen instanceof net.minecraft.client.gui.GuiGameOver
                                || mc.currentScreen != null
                                   && mc.currentScreen.getClass().getSimpleName()
                                       .contains("GameOver")) {
                            mc.displayGuiScreen(null);
                        }
                    } catch (Throwable ig) {
                        try { mc.displayGuiScreen(null); } catch (Throwable ig2) {}
                    }
                    float h = a.has("health") ? a.get("health").getAsFloat() : 20.0F;
                    if (h <= 0.0F) h = 20.0F;
                    p.setHealth(h);
                    p.deathTime = 0;
                    p.hurtTime = 0;
                    p.isDead = false;
                    wasDead = false;
                    try {
                        p.extinguish();
                        java.lang.reflect.Field ff =
                            net.minecraft.entity.Entity.class.getDeclaredField("fire");
                        ff.setAccessible(true);
                        ff.setInt(p, -1);
                    } catch (Throwable ig) {}
                }

                // Sync key vitals + inventory to server player so nothing reverts.
                final JsonObject fa = a;
                net.minecraft.server.MinecraftServer srv2 = mc.getIntegratedServer();
                if (srv2 != null) {
                    srv2.addScheduledTask(new Runnable() { public void run() {
                        try {
                            java.util.List<net.minecraft.entity.player.EntityPlayerMP> ps =
                                srv2.getPlayerList().getPlayers();
                            if (ps.isEmpty()) return;
                            net.minecraft.entity.player.EntityPlayerMP pl = ps.get(0);
                            if (fa.has("health")) pl.setHealth(fa.get("health").getAsFloat());
                            if (fa.has("food"))
                                pl.getFoodStats().setFoodLevel(fa.get("food").getAsInt());
                            if (fa.has("air")) pl.setAir(fa.get("air").getAsInt());
                            if (fa.has("absorption"))
                                pl.setAbsorptionAmount(fa.get("absorption").getAsFloat());
                            if (fa.has("dead") && fa.get("dead").getAsBoolean())
                                pl.setHealth(0.0F);
                            if (fa.has("fire")) {
                                int ft = fa.get("fire").getAsInt();
                                try {
                                    java.lang.reflect.Field ff =
                                        net.minecraft.entity.Entity.class
                                            .getDeclaredField("fire");
                                    ff.setAccessible(true);
                                    if (ft > 0) {
                                        ff.setInt(pl, ft);
                                    } else {
                                        pl.extinguish();
                                        ff.setInt(pl, -1);
                                    }
                                } catch (Throwable ig) {
                                    if (ft > 0) pl.setFire(Math.max(1, ft / 20));
                                    else pl.extinguish();
                                }
                            }
                            if (fa.has("clear_effects")
                                    && fa.get("clear_effects").getAsBoolean()) {
                                pl.clearActivePotions();
                            }
                            if (fa.has("effects") && fa.get("effects").isJsonArray()) {
                                for (com.google.gson.JsonElement el :
                                        fa.getAsJsonArray("effects")) {
                                    com.google.gson.JsonObject pe = el.getAsJsonObject();
                                    int id = pe.get("id").getAsInt();
                                    int dur = pe.has("duration")
                                        ? pe.get("duration").getAsInt() : 200;
                                    int amp = pe.has("amplifier")
                                        ? pe.get("amplifier").getAsInt() : 0;
                                    net.minecraft.potion.Potion pot =
                                        net.minecraft.potion.Potion.getPotionById(id);
                                    if (pot != null)
                                        pl.addPotionEffect(
                                            new net.minecraft.potion.PotionEffect(
                                                pot, dur, amp, false, false));
                                }
                            }
                            if (fa.has("hotbar") && fa.get("hotbar").isJsonArray()) {
                                com.google.gson.JsonArray hb = fa.getAsJsonArray("hotbar");
                                for (int i = 0; i < 9 && i < hb.size(); i++) {
                                    if (hb.get(i).isJsonNull()) {
                                        pl.inventory.mainInventory.set(i,
                                            net.minecraft.item.ItemStack.EMPTY);
                                        continue;
                                    }
                                    com.google.gson.JsonArray slot = hb.get(i).getAsJsonArray();
                                    int id = slot.get(0).getAsInt();
                                    int count = slot.size() > 1 ? slot.get(1).getAsInt() : 1;
                                    int meta = slot.size() > 2 ? slot.get(2).getAsInt() : 0;
                                    if (id <= 0 || count <= 0) {
                                        pl.inventory.mainInventory.set(i,
                                            net.minecraft.item.ItemStack.EMPTY);
                                    } else {
                                        net.minecraft.item.Item it =
                                            net.minecraft.item.Item.getItemById(id);
                                        if (it != null)
                                            pl.inventory.mainInventory.set(i,
                                                new net.minecraft.item.ItemStack(it, count, meta));
                                    }
                                }
                                if (fa.has("hotbar_sel"))
                                    pl.inventory.currentItem = fa.get("hotbar_sel").getAsInt();
                            }
                            if (fa.has("armor") && fa.get("armor").isJsonArray()) {
                                com.google.gson.JsonArray ar = fa.getAsJsonArray("armor");
                                for (int i = 0; i < 4 && i < ar.size(); i++) {
                                    if (ar.get(i).isJsonNull() || ar.get(i).getAsInt() <= 0) {
                                        pl.inventory.armorInventory.set(i,
                                            net.minecraft.item.ItemStack.EMPTY);
                                        continue;
                                    }
                                    net.minecraft.item.Item it =
                                        net.minecraft.item.Item.getItemById(ar.get(i).getAsInt());
                                    if (it != null)
                                        pl.inventory.armorInventory.set(i,
                                            new net.minecraft.item.ItemStack(it, 1, 0));
                                }
                                pl.inventory.markDirty();
                            }
                        } catch (Throwable ig) {}
                    }});
                }

                int fireTicks = -1;
                try {
                    java.lang.reflect.Field ff =
                        net.minecraft.entity.Entity.class.getDeclaredField("fire");
                    ff.setAccessible(true);
                    fireTicks = ff.getInt(p);
                } catch (Throwable ig) {}
                String screenName = mc.currentScreen == null ? "null"
                    : mc.currentScreen.getClass().getSimpleName();

                StringBuilder sb = new StringBuilder(256);
                sb.append("{\"ok\":true");
                sb.append(",\"health\":").append(p.getHealth());
                sb.append(",\"food\":").append(p.getFoodStats().getFoodLevel());
                sb.append(",\"air\":").append(p.getAir());
                sb.append(",\"absorption\":").append(p.getAbsorptionAmount());
                sb.append(",\"xp_level\":").append(p.experienceLevel);
                sb.append(",\"xp_frac\":").append(p.experience);
                sb.append(",\"armor\":").append(p.getTotalArmorValue());
                sb.append(",\"hold_death\":").append(holdDeath);
                // Re-assert sticky use pose after server inventory schedule so
                // pin_reply reflects the exact state frame{} will re-apply.
                if (pinUseActive) {
                    JsonObject re = applyUsePosePin(mc);
                    if (useDiag == null) useDiag = re;
                    else {
                        useDiag = re;
                    }
                    if (useDiag.has("equip_pinned"))
                        equipPinned = useDiag.get("equip_pinned").getAsInt();
                    if (useDiag.has("equip_progress"))
                        pinnedEquip = useDiag.get("equip_progress").getAsFloat();
                }
                sb.append(",\"hand_active\":").append(p.isHandActive());
                if (p.isHandActive())
                    sb.append(",\"use_count\":").append(p.getItemInUseCount())
                      .append(",\"use_max\":").append(p.getItemInUseMaxCount());
                sb.append(",\"burning\":").append(p.isBurning());
                sb.append(",\"fire_ticks\":").append(fireTicks);
                sb.append(",\"portal\":").append(p.timeInPortal);
                sb.append(",\"portal_time_pin_active\":").append(pinPortalTimeActive);
                if (pinPortalTimeActive)
                    sb.append(",\"portal_time_pin\":").append(pinPortalTime);
                {
                    int phase = -1;
                    try {
                        if (mc.entityRenderer != null) {
                            java.lang.reflect.Field fPhase =
                                net.minecraft.client.renderer.EntityRenderer.class
                                    .getDeclaredField("rendererUpdateCount");
                            fPhase.setAccessible(true);
                            phase = fPhase.getInt(mc.entityRenderer);
                        }
                    } catch (Throwable ig) {}
                    sb.append(",\"portal_phase\":").append(phase);
                    if (pinPortalPhase >= 0)
                        sb.append(",\"portal_phase_pin\":").append(pinPortalPhase);
                }
                sb.append(",\"dead\":").append(p.getHealth() <= 0.0F || p.isDead);
                sb.append(",\"screen\":\"").append(screenName).append("\"");
                sb.append(",\"potion_count\":").append(p.getActivePotionEffects().size());
                // Viewmodel pin diagnostics (equip + hideGUI + held id + use branch).
                sb.append(",\"hide_gui\":").append(mc.gameSettings.hideGUI);
                sb.append(",\"third_person\":").append(mc.gameSettings.thirdPersonView);
                sb.append(",\"equip_progress\":").append(pinnedEquip);
                sb.append(",\"equip_pinned\":").append(equipPinned);
                {
                    net.minecraft.item.ItemStack held0 = p.getHeldItemMainhand();
                    int hid = held0.isEmpty() ? 0
                        : net.minecraft.item.Item.getIdFromItem(held0.getItem());
                    sb.append(",\"held_id\":").append(hid);
                }
                sb.append(",\"pin_use_active\":").append(pinUseActive);
                sb.append(",\"pin_use_remaining\":").append(pinUseRemaining);
                if (useDiag != null) {
                    if (useDiag.has("branch"))
                        sb.append(",\"use_branch\":\"").append(
                            useDiag.get("branch").getAsString()).append("\"");
                    if (useDiag.has("use_action"))
                        sb.append(",\"use_action_name\":\"").append(
                            useDiag.get("use_action").getAsString()).append("\"");
                    if (useDiag.has("stack_id_eq"))
                        sb.append(",\"stack_id_eq\":").append(
                            useDiag.get("stack_id_eq").getAsBoolean());
                    if (useDiag.has("ir_id_eq"))
                        sb.append(",\"ir_id_eq\":").append(
                            useDiag.get("ir_id_eq").getAsBoolean());
                    if (useDiag.has("pulling"))
                        sb.append(",\"model_pulling\":").append(
                            useDiag.get("pulling").getAsFloat());
                    if (useDiag.has("pull"))
                        sb.append(",\"model_pull\":").append(
                            useDiag.get("pull").getAsFloat());
                    if (useDiag.has("blocking"))
                        sb.append(",\"model_blocking\":").append(
                            useDiag.get("blocking").getAsFloat());
                    if (useDiag.has("use_key_down"))
                        sb.append(",\"use_key_down\":").append(
                            useDiag.get("use_key_down").getAsBoolean());
                }
sb.append("}");
                r.resp.offer(sb.toString());
            } catch (Throwable t) {
                r.resp.offer(err("hud_pin: " + t));
            }
            return;
        }

        // entity_pin: freeze deterministic interactive-entity state for oracle A/B
        // captures. Covers fields existing summon/NBT/tape cannot pin for render:
        // slime/magma size+squishFactor, dragon deathTicks, fireball pose, XP orb
        // age/color/value, and dig hit particles via ParticleManager.addBlockHitEffects.
        // Size/fireball/xp/dig use server spawn or client particles. squishFactor and
        // deathTicks are CLIENT render fields: arm a pending pin matched by UUID/eid/
        // position/type; frame{} applies it on the client entity at render time.
        // Does not advance the world; caller uses frame{} for the PNG dump.
        if (r.cmd.equals("entity_pin")) {
            try {
                if (mc.player == null || mc.world == null) {
                    r.resp.offer(err("no world")); return;
                }
                final JsonObject a = r.action;
                final MinecraftServer s = mc.getIntegratedServer();
                if (s == null) { r.resp.offer(err("no server")); return; }
                final String kind = a.has("kind") ? a.get("kind").getAsString() : "";
                final boolean clear = !a.has("clear") || a.get("clear").getAsBoolean();
                final Req fr = r;

                // Client-only dig hit spray (ParticleDigging). Face 0..5 = DUNSWE.
                if ("dig_hit".equalsIgnoreCase(kind)) {
                    renderPinActive = false;
                    int bx = a.get("bx").getAsInt();
                    int by = a.get("by").getAsInt();
                    int bz = a.get("bz").getAsInt();
                    int face = a.has("face") ? a.get("face").getAsInt() : 1; // UP default
                    int n = a.has("count") ? a.get("count").getAsInt() : 4;
                    if (n < 1) n = 1;
                    if (n > 32) n = 32;
                    net.minecraft.util.math.BlockPos pos =
                        new net.minecraft.util.math.BlockPos(bx, by, bz);
                    net.minecraft.util.EnumFacing fac =
                        net.minecraft.util.EnumFacing.values()[face & 7];
                    // Clear existing FX so A/B is the spray alone.
                    try {
                        java.lang.reflect.Field fl =
                            net.minecraft.client.particle.ParticleManager.class
                                .getDeclaredField("fxLayers");
                        fl.setAccessible(true);
                        @SuppressWarnings("unchecked")
                        java.util.ArrayDeque<net.minecraft.client.particle.Particle>[][] layers =
                            (java.util.ArrayDeque<net.minecraft.client.particle.Particle>[][])
                                fl.get(mc.effectRenderer);
                        if (layers != null) {
                            for (int i = 0; i < layers.length; ++i)
                                for (int j = 0; j < layers[i].length; ++j)
                                    if (layers[i][j] != null) layers[i][j].clear();
                        }
                    } catch (Throwable ig) {}
                    for (int i = 0; i < n; ++i)
                        mc.effectRenderer.addBlockHitEffects(pos, fac);
                    JsonObject o = new JsonObject();
                    o.addProperty("ok", true);
                    o.addProperty("kind", "dig_hit");
                    o.addProperty("bx", bx); o.addProperty("by", by); o.addProperty("bz", bz);
                    o.addProperty("face", face); o.addProperty("count", n);
                    r.resp.offer(o.toString());
                    return;
                }

                s.addScheduledTask(new Runnable() { public void run() {
                    try {
                        net.minecraft.world.WorldServer w = s.worlds[0];
                        try {
                            java.util.List<net.minecraft.entity.player.EntityPlayerMP> qps =
                                s.getPlayerList().getPlayers();
                            if (!qps.isEmpty()) w = qps.get(0).getServerWorld();
                        } catch (Throwable ig) {}
                        // EntitySlime.onUpdate sets isDead when difficulty==PEACEFUL and
                        // size>0. Integrated flat worlds often start PEACEFUL; chat
                        // /difficulty can fail (counted in runcmds failed). Force EASY
                        // on the server before any IMob pin so slimes/magma stay alive.
                        try {
                            s.setDifficultyForAllWorlds(net.minecraft.world.EnumDifficulty.EASY);
                            w.getWorldInfo().setDifficulty(net.minecraft.world.EnumDifficulty.EASY);
                            try {
                                w.getWorldInfo().setDifficultyLocked(false);
                            } catch (Throwable ig2) {}
                        } catch (Throwable ig) {}
                        if (clear) {
                            for (net.minecraft.entity.Entity e :
                                    new java.util.ArrayList<net.minecraft.entity.Entity>(
                                        w.loadedEntityList)) {
                                if (e instanceof net.minecraft.entity.player.EntityPlayer) continue;
                                e.setDead();
                            }
                        }
                        double sx = a.has("x") ? a.get("x").getAsDouble() : 0.5;
                        double sy = a.has("y") ? a.get("y").getAsDouble() : 4.0;
                        double sz = a.has("z") ? a.get("z").getAsDouble() : 0.5;
                        float yaw = a.has("yaw") ? a.get("yaw").getAsFloat() : 0.0f;
                        float pitch = a.has("pitch") ? a.get("pitch").getAsFloat() : 0.0f;
                        net.minecraft.entity.Entity ent = null;
                        JsonObject o = new JsonObject();
                        o.addProperty("ok", true);
                        o.addProperty("kind", kind);
                        try {
                            o.addProperty("difficulty", w.getDifficulty().name());
                        } catch (Throwable ig) {}

                        // Default: no client render pin unless slime/magma squish,
                        // dragon death, or xp_orb pose/color freeze.
                        boolean armRenderPin = false;
                        float pinSq = 0.0f;
                        int pinSize = 0;
                        int pinDt = 0;
                        boolean pinMagma = false;
                        renderPinIsXp = false;

                        if ("slime".equalsIgnoreCase(kind) || "magma_cube".equalsIgnoreCase(kind)
                                || "magmacube".equalsIgnoreCase(kind)) {
                            boolean magma = kind.toLowerCase().contains("magma");
                            pinMagma = magma;
                            net.minecraft.entity.monster.EntitySlime sl = magma
                                ? new net.minecraft.entity.monster.EntityMagmaCube(w)
                                : new net.minecraft.entity.monster.EntitySlime(w);
                            int size = a.has("size") ? a.get("size").getAsInt() : 2;
                            if (size < 1) size = 1;
                            // setSlimeSize is protected; NBT Size is getSlimeSize()-1.
                            try {
                                java.lang.reflect.Method m =
                                    net.minecraft.entity.monster.EntitySlime.class
                                        .getDeclaredMethod("setSlimeSize", int.class, boolean.class);
                                m.setAccessible(true);
                                m.invoke(sl, Integer.valueOf(size), Boolean.TRUE);
                            } catch (Throwable t) {
                                net.minecraft.nbt.NBTTagCompound nbt =
                                    new net.minecraft.nbt.NBTTagCompound();
                                sl.writeToNBT(nbt);
                                nbt.setInteger("Size", size - 1);
                                sl.readFromNBT(nbt);
                            }
                            sl.setNoAI(true);
                            try { sl.enablePersistence(); } catch (Throwable ig) {}
                            // Prevent immediate peaceful cull if difficulty flip races a tick.
                            try { sl.isDead = false; } catch (Throwable ig) {}
                            sl.setLocationAndAngles(sx, sy, sz, yaw, pitch);
                            float sq = a.has("squish") ? a.get("squish").getAsFloat() : 0.0f;
                            // Server-side squish is not what RenderSlime reads; still set for
                            // completeness. Client pin is armed below for actual render.
                            sl.squishAmount = sq;
                            sl.squishFactor = sq;
                            sl.prevSquishFactor = sq;
                            ent = sl;
                            pinSq = sq;
                            pinSize = size;
                            // Always arm render pin for slime/magma so squish=0 and squish=1
                            // both freeze client prev/current factors at the requested value.
                            armRenderPin = true;
                            o.addProperty("size", size);
                            o.addProperty("squish", sq);
                        } else if ("dragon".equalsIgnoreCase(kind)
                                || "ender_dragon".equalsIgnoreCase(kind)) {
                            net.minecraft.entity.boss.EntityDragon dr =
                                new net.minecraft.entity.boss.EntityDragon(w);
                            dr.setLocationAndAngles(sx, sy, sz, yaw, pitch);
                            int dt = a.has("death_ticks") ? a.get("death_ticks").getAsInt() : 0;
                            if (dt < 0) dt = 0;
                            if (dt > 200) dt = 200;
                            // Keep the SERVER dragon alive below removal. health<=0 runs
                            // onDeathUpdate (deathTicks++ , setDead at 200). Client render
                            // alone needs deathTicks for LayerEnderDragonDeath + dissolve;
                            // do not zero server health or server deathTicks.
                            try {
                                dr.setHealth(dr.getMaxHealth());
                            } catch (Throwable ig) {}
                            dr.deathTicks = 0;
                            try {
                                // HOVER stays put; server remains alive (health full).
                                dr.getPhaseManager().setPhase(
                                    net.minecraft.entity.boss.dragon.phase.PhaseList.HOVER);
                            } catch (Throwable ig) {
                                try {
                                    dr.getPhaseManager().setPhase(
                                        net.minecraft.entity.boss.dragon.phase.PhaseList.HOLDING_PATTERN);
                                } catch (Throwable ig2) {}
                            }
                            ent = dr;
                            pinDt = dt;
                            armRenderPin = (dt > 0);
                            o.addProperty("death_ticks", dt);
                            o.addProperty("server_alive", true);
                        } else if ("small_fireball".equalsIgnoreCase(kind)
                                || "fireball_small".equalsIgnoreCase(kind)) {
                            renderPinActive = false;
                            net.minecraft.entity.projectile.EntitySmallFireball fb =
                                new net.minecraft.entity.projectile.EntitySmallFireball(w);
                            fb.setPosition(sx, sy, sz);
                            fb.rotationYaw = yaw; fb.rotationPitch = pitch;
                            fb.motionX = fb.motionY = fb.motionZ = 0.0D;
                            ent = fb;
                        } else if ("dragon_fireball".equalsIgnoreCase(kind)
                                || "fireball_dragon".equalsIgnoreCase(kind)) {
                            renderPinActive = false;
                            net.minecraft.entity.projectile.EntityDragonFireball fb =
                                new net.minecraft.entity.projectile.EntityDragonFireball(w);
                            fb.setPosition(sx, sy, sz);
                            fb.rotationYaw = yaw; fb.rotationPitch = pitch;
                            fb.motionX = fb.motionY = fb.motionZ = 0.0D;
                            ent = fb;
                        } else if ("xp_orb".equalsIgnoreCase(kind)
                                || "xporb".equalsIgnoreCase(kind)) {
                            int val = a.has("value") ? a.get("value").getAsInt() : 7;
                            net.minecraft.entity.item.EntityXPOrb orb =
                                new net.minecraft.entity.item.EntityXPOrb(w, sx, sy, sz, val);
                            orb.motionX = orb.motionY = orb.motionZ = 0.0D;
                            try { orb.setNoGravity(true); } catch (Throwable ig) {}
                            int age = a.has("age") ? a.get("age").getAsInt() : 0;
                            // Default color phase with visible green-gold tint
                            // (sin phase ~0 → red mid, green 255, blue low).
                            int col = a.has("color") ? a.get("color").getAsInt() : 0;
                            orb.xpOrbAge = age;
                            orb.xpColor = col;
                            orb.delayBeforeCanPickup = 32767; // never attract
                            // Arm CLIENT render pin so free-running onUpdate
                            // (gravity, attraction, xpColor++) cannot move or
                            // despawn the orb between settle and frame{}.
                            armRenderPin = true;
                            renderPinXpColor = col;
                            renderPinXpAge = age;
                            renderPinXpValue = val;
                            renderPinIsXp = true;
                            ent = orb;
                            o.addProperty("value", val);
                            o.addProperty("age", age);
                            o.addProperty("color", col);
                        } else {
                            fr.resp.offer(err("entity_pin: unknown kind " + kind));
                            return;
                        }
                        if (ent != null) {
                            ent.motionX = ent.motionY = ent.motionZ = 0.0D;
                            if (ent instanceof net.minecraft.entity.EntityLiving) {
                                ((net.minecraft.entity.EntityLiving) ent).setNoAI(true);
                            }
                            w.spawnEntity(ent);
                            o.addProperty("eid", ent.getEntityId());
                            o.addProperty("uuid", ent.getUniqueID().toString());
                            o.addProperty("x", sx); o.addProperty("y", sy); o.addProperty("z", sz);

                            if (armRenderPin) {
                                // Arm pending CLIENT render pin (matched later on client thread).
                                renderPinKind = kind;
                                renderPinUuid = ent.getUniqueID();
                                renderPinEid = ent.getEntityId();
                                renderPinX = sx;
                                renderPinY = sy;
                                renderPinZ = sz;
                                renderPinSquish = pinSq;
                                renderPinSize = pinSize;
                                renderPinDeathTicks = pinDt;
                                renderPinIsMagma = pinMagma;
                                renderPinActive = true;
                                o.addProperty("render_pin_armed", true);
                                o.addProperty("render_pin_squish", pinSq);
                                o.addProperty("render_pin_death_ticks", pinDt);
                                // Best-effort early client apply (entity may not exist yet;
                                // frame{} re-applies after packets land).
                                final java.util.UUID pinUuid = ent.getUniqueID();
                                final int pinEid = ent.getEntityId();
                                try {
                                    Minecraft.getMinecraft().addScheduledTask(new Runnable() {
                                        public void run() {
                                            try {
                                                Minecraft cmc = Minecraft.getMinecraft();
                                                if (cmc.world == null) return;
                                                net.minecraft.entity.Entity ce =
                                                    findClientRenderPinEntity(cmc);
                                                if (ce != null) {
                                                    applyClientEntityRenderFields(ce);
                                                    System.out.println("[qrl] entity_pin client match"
                                                        + " eid=" + ce.getEntityId()
                                                        + " uuid=" + pinUuid
                                                        + " server_eid=" + pinEid);
                                                }
                                            } catch (Throwable ig) {}
                                        }
                                    });
                                } catch (Throwable ig) {}
                            } else {
                                renderPinActive = false;
                                o.addProperty("render_pin_armed", false);
                            }
                        }
                        fr.resp.offer(o.toString());
                    } catch (Throwable t) {
                        fr.resp.offer(err("entity_pin: " + t));
                    }
                }});
                return;
            } catch (Throwable t) {
                r.resp.offer(err("entity_pin: " + t));
                return;
            }
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
                    rlPolicyActionSeq = 0L;
                    rlPolicyActionFnv = 0xcbf29ce484222325L;
                    rlV2Active = false;
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
                if (!launching) {
                    if (fresh) {
                        rlPolicyActionSeq = 0L;
                        rlPolicyActionFnv = 0xcbf29ce484222325L;
                        rlV2Active = false;
                    }
                    launchWorld(mc, r.world);
                    launching = true;
                }
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
        if (a.has("cam") || a.has("dyaw") || a.has("dpitch") || a.has("craft")
            || a.has("interact") || a.has("smelt")) rlV2Active = true;
        // craft/interact/smelt fire in rl_mode.c's order (rl_mode.c:1196-1199:
        // craft, THEN interact, then smelt), so a decision that carries both a
        // 3x3 craft and an interact crafts against the PREVIOUS container
        // state on both sides.
        // craft: discrete primitive 0..7, fires once, silent failure.
        if (a.has("craft")) rlCraft(mc, a.get("craft").getAsInt());
        // interact: open the nearest placed crafting table/furnace in reach
        // (fires once, silent failure - rl_do_interact semantics).
        if (a.has("interact") && a.get("interact").getAsInt() != 0) rlInteract(mc);
        // smelt: furnace load/collect primitive (rl_do_smelt semantics) -
        // needs the open furnace container, fires once, fails silently.
        if (a.has("smelt") && a.get("smelt").getAsInt() != 0) rlSmelt(mc);
        byte[] actionBytes = a.toString().getBytes(StandardCharsets.UTF_8);
        for (byte actionByte : actionBytes) {
            rlPolicyActionFnv ^= (long) (actionByte & 0xff);
            rlPolicyActionFnv *= 0x100000001b3L;
        }
        rlPolicyActionSeq++;
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
        // Keep use-key held while a sticky use-pose pin is live so processKeyBinds
        // does not call onStoppedUsingItem between hud_pin and frame{}.
        key(mc.gameSettings.keyBindUseItem, pinUseKeyHeld);
    }

    // 1.11.2 MCP: setKeyBindState takes a key CODE, not a KeyBinding
    private static void key(KeyBinding kb, boolean down) {
        KeyBinding.setKeyBindState(kb.getKeyCode(), down);
    }

    /** Clear sticky use-pose pin (idle/rest hand). */
    private void clearUsePosePin(Minecraft mc) {
        pinUseActive = false;
        pinUseRemaining = 0;
        pinUseKeyHeld = false;
        if (mc != null && mc.gameSettings != null) {
            key(mc.gameSettings.keyBindUseItem, false);
        }
        if (mc != null && mc.player != null) {
            try { mc.player.resetActiveHand(); } catch (Throwable ig) {}
        }
    }

    /** Re-apply sticky camera freeze (pos + yaw/pitch + prev* interpolants). */
    private void applyStickyPosePin(Minecraft mc) {
        if (!pinPoseActive || mc == null || mc.player == null) return;
        EntityPlayerSP p = mc.player;
        p.motionX = p.motionY = p.motionZ = 0.0D;
        p.fallDistance = 0.0F;
        p.setNoGravity(true);
        p.setPositionAndRotation(pinPoseX, pinPoseY, pinPoseZ, pinPoseYaw, pinPosePitch);
        p.prevPosX = p.posX; p.prevPosY = p.posY; p.prevPosZ = p.posZ;
        p.lastTickPosX = p.posX; p.lastTickPosY = p.posY; p.lastTickPosZ = p.posZ;
        p.prevRotationYaw = pinPoseYaw; p.prevRotationPitch = pinPosePitch;
        p.renderYawOffset = pinPoseYaw; p.prevRenderYawOffset = pinPoseYaw;
        p.rotationYawHead = pinPoseYaw; p.prevRotationYawHead = pinPoseYaw;
    }

    /**
     * Arm sticky first-person use pose. remaining = getItemInUseCount countdown
     * (bow max-pull, eat leftover, shield full duration).
     */
    private void armUsePosePin(int remaining) {
        if (remaining <= 0) {
            pinUseActive = false;
            pinUseRemaining = 0;
            pinUseKeyHeld = false;
            return;
        }
        pinUseActive = true;
        pinUseRemaining = remaining;
        pinUseKeyHeld = true;
    }

    /**
     * Pin EntityPlayerSP + ItemRenderer so ItemRenderer's use branch and the
     * bow/shield model predicates see a consistent, sticky full-use pose.
     * Returns diagnostics for pin_reply / frame{} (branch, model overrides,
     * stack identity).
     */
    private JsonObject applyUsePosePin(Minecraft mc) {
        JsonObject d = new JsonObject();
        d.addProperty("pin_use_active", pinUseActive);
        d.addProperty("pin_use_remaining", pinUseRemaining);
        if (mc == null || mc.player == null) {
            d.addProperty("branch", "no_player");
            return d;
        }
        EntityPlayerSP p = mc.player;
        if (!pinUseActive || pinUseRemaining <= 0) {
            d.addProperty("branch", "idle_unpinned");
            d.addProperty("hand_active", p.isHandActive());
            d.addProperty("use_count", p.getItemInUseCount());
            return d;
        }

        // Hold use key so processKeyBinds will not stopActiveHand this tick.
        pinUseKeyHeld = true;
        key(mc.gameSettings.keyBindUseItem, true);
        // Also force the pressed field: isKeyDown gates on KeyConflictContext,
        // but onStoppedUsingItem only checks isKeyDown(); keep both paths hot.
        try {
            java.lang.reflect.Field fPressed =
                KeyBinding.class.getDeclaredField("pressed");
            fPressed.setAccessible(true);
            fPressed.setBoolean(mc.gameSettings.keyBindUseItem, true);
        } catch (Throwable ig) {}

        net.minecraft.item.ItemStack held = p.getHeldItemMainhand();
        int heldId = held.isEmpty() ? 0
            : net.minecraft.item.Item.getIdFromItem(held.getItem());
        d.addProperty("held_id", heldId);
        if (held.isEmpty()) {
            d.addProperty("branch", "empty_hand");
            d.addProperty("hand_active", false);
            return d;
        }

        // (Re)activate main hand. reset+set when already active so re-pins
        // refresh activeItemStack to the current inventory reference.
        try {
            p.resetActiveHand();
            p.setActiveHand(net.minecraft.util.EnumHand.MAIN_HAND);
        } catch (Throwable t) {
            d.addProperty("set_active_err", String.valueOf(t));
        }

        // CRITICAL: activeItemStack and itemStackMainHand must be the SAME
        // reference as the hotbar stack. ItemBow.pulling / ItemShield.blocking
        // use == (not equals); updateActiveHand also resets on identity mismatch.
        int equipPinned = 0;
        boolean stackIdOk = false;
        boolean irIdOk = false;
        float equipProg = -1.0F;
        try {
            java.lang.reflect.Field fCount =
                net.minecraft.entity.EntityLivingBase.class
                    .getDeclaredField("activeItemStackUseCount");
            java.lang.reflect.Field fStack =
                net.minecraft.entity.EntityLivingBase.class
                    .getDeclaredField("activeItemStack");
            fCount.setAccessible(true);
            fStack.setAccessible(true);
            // Same inventory reference - not held.copy().
            fStack.set(p, held);
            fCount.setInt(p, pinUseRemaining);

            // EntityPlayerSP private handActive / activeHand (setActiveHand
            // already sets these; re-assert after any intervening reset).
            try {
                java.lang.reflect.Field fHa =
                    EntityPlayerSP.class.getDeclaredField("handActive");
                java.lang.reflect.Field fAh =
                    EntityPlayerSP.class.getDeclaredField("activeHand");
                fHa.setAccessible(true);
                fAh.setAccessible(true);
                fHa.setBoolean(p, true);
                fAh.set(p, net.minecraft.util.EnumHand.MAIN_HAND);
            } catch (Throwable ig) {}

            net.minecraft.client.renderer.ItemRenderer ir = mc.getItemRenderer();
            java.lang.reflect.Field fEq =
                net.minecraft.client.renderer.ItemRenderer.class
                    .getDeclaredField("equippedProgressMainHand");
            java.lang.reflect.Field fPrev =
                net.minecraft.client.renderer.ItemRenderer.class
                    .getDeclaredField("prevEquippedProgressMainHand");
            java.lang.reflect.Field fItem =
                net.minecraft.client.renderer.ItemRenderer.class
                    .getDeclaredField("itemStackMainHand");
            java.lang.reflect.Field fItemOff =
                net.minecraft.client.renderer.ItemRenderer.class
                    .getDeclaredField("itemStackOffHand");
            java.lang.reflect.Field fEqOff =
                net.minecraft.client.renderer.ItemRenderer.class
                    .getDeclaredField("equippedProgressOffHand");
            java.lang.reflect.Field fPrevOff =
                net.minecraft.client.renderer.ItemRenderer.class
                    .getDeclaredField("prevEquippedProgressOffHand");
            fEq.setAccessible(true); fPrev.setAccessible(true);
            fItem.setAccessible(true); fItemOff.setAccessible(true);
            fEqOff.setAccessible(true); fPrevOff.setAccessible(true);
            fEq.setFloat(ir, 1.0F);
            fPrev.setFloat(ir, 1.0F);
            fEqOff.setFloat(ir, 1.0F);
            fPrevOff.setFloat(ir, 1.0F);
            // Same inventory reference as activeItemStack / hotbar.
            fItem.set(ir, held);
            fItemOff.set(ir, net.minecraft.item.ItemStack.EMPTY);
            equipPinned = 1;
            equipProg = fEq.getFloat(ir);
            stackIdOk = (p.getActiveItemStack() == held);
            irIdOk = (fItem.get(ir) == held);
        } catch (Throwable t) {
            d.addProperty("reflect_err", String.valueOf(t));
            equipPinned = 0;
        }

        // What ItemRenderer.renderItemInFirstPerson would switch on.
        boolean handActive = p.isHandActive();
        int useCount = p.getItemInUseCount();
        net.minecraft.util.EnumHand activeHand = p.getActiveHand();
        net.minecraft.item.EnumAction action = held.getItemUseAction();
        String branch = "idle";
        if (handActive && useCount > 0
                && activeHand == net.minecraft.util.EnumHand.MAIN_HAND) {
            switch (action) {
                case EAT: case DRINK: branch = "eat"; break;
                case BLOCK: branch = "block"; break;
                case BOW: branch = "bow"; break;
                case NONE: default: branch = "use_none"; break;
            }
        }

        // Model property overrides (same predicates as ItemBow / ItemShield).
        float pulling = 0.0F;
        float pull = 0.0F;
        float blocking = 0.0F;
        try {
            net.minecraft.item.ItemStack active = p.getActiveItemStack();
            if (handActive && active == held) {
                if (held.getItem() == net.minecraft.init.Items.BOW) {
                    pulling = 1.0F;
                    pull = (float)(held.getMaxItemUseDuration() - useCount) / 20.0F;
                }
                if (held.getItem() == net.minecraft.init.Items.SHIELD) {
                    blocking = 1.0F;
                }
            } else if (handActive && !active.isEmpty()
                    && active.getItem() == net.minecraft.init.Items.BOW
                    && held.getItem() == net.minecraft.init.Items.BOW) {
                // pull amount uses getItem() check, not ==
                pull = (float)(held.getMaxItemUseDuration() - useCount) / 20.0F;
            }
        } catch (Throwable ig) {}

        boolean useKey = false;
        try { useKey = mc.gameSettings.keyBindUseItem.isKeyDown(); } catch (Throwable ig) {}

        d.addProperty("branch", branch);
        d.addProperty("hand_active", handActive);
        d.addProperty("use_count", useCount);
        d.addProperty("use_max_elapsed", p.getItemInUseMaxCount());
        d.addProperty("use_action", action.name());
        d.addProperty("active_hand", activeHand == null ? "null" : activeHand.name());
        d.addProperty("stack_id_eq", stackIdOk);
        d.addProperty("ir_id_eq", irIdOk);
        d.addProperty("active_eq_held", p.getActiveItemStack() == held);
        d.addProperty("pulling", pulling);
        d.addProperty("pull", pull);
        d.addProperty("blocking", blocking);
        d.addProperty("equip_progress", equipProg);
        d.addProperty("equip_pinned", equipPinned);
        d.addProperty("use_key_down", useKey);
        d.addProperty("hide_gui", mc.gameSettings.hideGUI);
        d.addProperty("third_person", mc.gameSettings.thirdPersonView);
        return d;
    }

    // headless world launch, settings from Python (folder per seed for reuse/determinism)
    private void launchWorld(Minecraft mc, JsonObject w) {
        try {
            long seed = w.has("seed") ? w.get("seed").getAsLong() : 0L;
            GameType gt = "creative".equalsIgnoreCase(opt(w, "mode")) ? GameType.CREATIVE : GameType.SURVIVAL;
            WorldType wt = "flat".equalsIgnoreCase(opt(w, "type")) ? WorldType.FLAT : WorldType.DEFAULT;
            boolean structures = !w.has("structures") || w.get("structures").getAsBoolean();
            WorldSettings ws = new WorldSettings(seed, gt, structures, false, wt).enableCommands();
            // Frozen legacy world folder prefix "qrl_" (tapes embed world:"qrl_0").
            String folder = "qrl_" + seed + ("flat".equalsIgnoreCase(opt(w, "type")) ? "_flat" : "");
            initStartNanos = System.nanoTime();
            mc.launchIntegratedServer(folder, folder, ws);
            System.out.println("[qrl] launching world " + folder + " seed=" + seed + " mode=" + gt + " type=" + wt);
        } catch (Throwable t) {
            System.out.println("[qrl] launchWorld failed: " + t);
        }
    }

    // qrl_launch.json: written by mc_cli.py (repo root) from fast.yaml (agent) or vanilla.yaml (human play). Resolution order:
    // ./qrl_launch.json (cwd = Minecraft/run under gradle) > absolute fallback.
    private static JsonObject loadLaunchCfg() {
        // Frozen legacy filename: qrl_launch.json (compat with tooling/tapes).
        String[] paths = { "qrl_launch.json" };
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
    // server's command manager (same path as the NetheriteMod "runcmds" op).
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
        /* RenderGlobal's F3 line, e.g. "C: 45/4356 D: 8, L: 0, ..." - rendered
         * vs loaded render-sections, for renderer submission comparisons. */
        if (mc.renderGlobal != null)
            o.addProperty("renders", mc.renderGlobal.getDebugInfoRenders());
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
        // Frozen legacy schema id (wire/obs contract).
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

    /**
     * Emit the gated dig object into a tape row. Schema (all fields always present
     * when dig_trace is on so comparators need no optional-key matrix):
     * <pre>
     * "dig":{
     *   "lcc":int,              // Minecraft.leftClickCounter
     *   "bhd":int,              // PlayerControllerMP.blockHitDelay
     *   "hit":0|1,              // isHittingBlock
     *   "cb":[x,y,z]|null,      // currentBlock (null when y&lt;0 sentinel)
     *   "face":int,             // objectMouseOver.sideHit ordinal or -1
     *   "ray":0|1|2,            // RayTraceResult.Type: MISS/BLOCK/ENTITY
     *   "rx","ry","rz":int?,    // block pos when ray==BLOCK
     *   "held":[id,meta,count]|0,
     *   "dmg":float,            // curBlockDamageMP
     *   "rel":float|null,       // getPlayerRelativeBlockHardness (read-only)
     *   "reach":float,          // getBlockReachDistance
     *   "brk":0|[x,y,z],        // first successful destroy this tick, else 0
     *   "bcn":int,              // ordered neighbor-caused final count
     *   "bc_ref":[[x,y,z,id,meta],...]  // those finals in call order
     * }
     * </pre>
     * Accessors: DigMinecraftAccess / DigControllerAccess mixins (no reflection).
     */
    private static void appendDigTrace(StringBuilder b, Minecraft mc,
            EntityPlayerSP p) {
        b.append(",\"dig\":{");
        int lcc = 0;
        if (mc instanceof DigMinecraftAccess)
            lcc = ((DigMinecraftAccess) mc).dig$leftClickCounter();
        b.append("\"lcc\":").append(lcc);
        int bhd = 0, hit = 0;
        float dmg = 0.0F, reach = 4.5F;
        net.minecraft.util.math.BlockPos cb = null;
        if (mc.playerController instanceof DigControllerAccess) {
            DigControllerAccess pc = (DigControllerAccess) mc.playerController;
            bhd = pc.dig$blockHitDelay();
            hit = pc.dig$isHittingBlock() ? 1 : 0;
            dmg = pc.dig$curBlockDamageMP();
            cb = pc.dig$currentBlock();
        }
        if (mc.playerController != null)
            reach = mc.playerController.getBlockReachDistance();
        b.append(",\"bhd\":").append(bhd)
         .append(",\"hit\":").append(hit);
        if (cb == null || cb.getY() < 0)
            b.append(",\"cb\":null");
        else
            b.append(",\"cb\":[").append(cb.getX()).append(',')
             .append(cb.getY()).append(',').append(cb.getZ()).append(']');
        int face = -1, rayType = 0;
        net.minecraft.util.math.RayTraceResult moy = mc.objectMouseOver;
        if (moy != null) {
            rayType = moy.typeOfHit.ordinal();
            if (moy.sideHit != null) face = moy.sideHit.getIndex();
            if (moy.typeOfHit == net.minecraft.util.math.RayTraceResult.Type.BLOCK
                    && moy.getBlockPos() != null) {
                net.minecraft.util.math.BlockPos rp = moy.getBlockPos();
                b.append(",\"face\":").append(face)
                 .append(",\"ray\":").append(rayType)
                 .append(",\"rx\":").append(rp.getX())
                 .append(",\"ry\":").append(rp.getY())
                 .append(",\"rz\":").append(rp.getZ());
            } else {
                b.append(",\"face\":").append(face)
                 .append(",\"ray\":").append(rayType);
            }
        } else {
            b.append(",\"face\":-1,\"ray\":0");
        }
        net.minecraft.item.ItemStack held = p.getHeldItemMainhand();
        if (held == null || held.isEmpty())
            b.append(",\"held\":0");
        else
            b.append(",\"held\":[")
             .append(net.minecraft.item.Item.getIdFromItem(held.getItem()))
             .append(',').append(held.getMetadata())
             .append(',').append(held.getCount()).append(']');
        b.append(",\"dmg\":").append(dmg);
        // Relative hardness: read-only call on the current ray-block (or
        // currentBlock when hitting). getPlayerRelativeBlockHardness does not
        // mutate world/controller state.
        Float rel = null;
        try {
            net.minecraft.util.math.BlockPos rpos = null;
            if (moy != null
                    && moy.typeOfHit == net.minecraft.util.math.RayTraceResult.Type.BLOCK)
                rpos = moy.getBlockPos();
            else if (cb != null && cb.getY() >= 0)
                rpos = cb;
            if (rpos != null && mc.world != null) {
                net.minecraft.block.state.IBlockState st = mc.world.getBlockState(rpos);
                if (st.getMaterial() != net.minecraft.block.material.Material.AIR)
                    rel = Float.valueOf(st.getPlayerRelativeBlockHardness(
                            p, mc.world, rpos));
            }
        } catch (Throwable ig) { rel = null; }
        if (rel == null || Float.isNaN(rel.floatValue())
                || Float.isInfinite(rel.floatValue()))
            b.append(",\"rel\":null");
        else
            b.append(",\"rel\":").append(rel.floatValue());
        b.append(",\"reach\":").append(reach);
        if (recDigBreaks.isEmpty())
            b.append(",\"brk\":0");
        else {
            int[] br = recDigBreaks.get(0);
            b.append(",\"brk\":[").append(br[0]).append(',')
             .append(br[1]).append(',').append(br[2]).append(']');
        }
        b.append(",\"bcn\":").append(recNeighborBlockChanges.size());
        b.append(",\"bc_ref\":[");
        for (int i = 0; i < recNeighborBlockChanges.size(); ++i) {
            int[] e = recNeighborBlockChanges.get(i);
            if (i > 0) b.append(',');
            b.append('[').append(e[0]).append(',').append(e[1]).append(',')
             .append(e[2]).append(',').append(e[3]).append(',')
             .append(e[4]).append(']');
        }
        b.append("]}");
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
         // pre-travel rotation for THIS tick's physics (see recPreYaw docs);
         // falls back to post-tick rotation on the first row if START never
         // fired between recstart and this record (same-tick recstart).
         .append(",\"ry\":").append(recPreLookValid ? recPreYaw : p.rotationYaw)
         .append(",\"rp\":").append(recPreLookValid ? recPrePitch : p.rotationPitch)
         .append(",\"vx\":").append(p.motionX).append(",\"vy\":").append(p.motionY)
         .append(",\"vz\":").append(p.motionZ)
         .append(",\"og\":").append(p.onGround ? 1 : 0)
         .append(",\"hp\":").append(p.getHealth())
         .append(",\"food\":").append(p.getFoodStats().getFoodLevel())
         .append(",\"fall\":").append(p.fallDistance)
         .append(",\"dim\":").append(p.dimension)
         .append(",\"wt\":").append(mc.world.getWorldTime());
        // Java-truth world digest: FNV-1a 64 over the 9x9x9 id/meta volume at
        // floor(feet pos) - a bit-equal mirror of magma's nearby_hash
        // (script.c: z outer, y, x inner; value id<<4|meta zero-extended).
        // The anchor is emitted too so replay only compares digests when both
        // sides agree on the volume; a shifted anchor is the physics gate's
        // problem, not a world mismatch.
        {
            int cx = (int) Math.floor(p.posX), cy = (int) Math.floor(p.posY),
                cz = (int) Math.floor(p.posZ);
            // NOT the standard FNV-1a offset basis: magma's nearby_hash
            // predates this mirror with the standard constant's last digit
            // dropped (1469598103934665603 vs ...6037). Bit-equality with
            // script.c matters, the constant itself does not.
            long wh = 1469598103934665603L;
            net.minecraft.util.math.BlockPos.MutableBlockPos wbp =
                new net.minecraft.util.math.BlockPos.MutableBlockPos();
            for (int z = cz - 4; z <= cz + 4; ++z)
                for (int y = cy - 4; y <= cy + 4; ++y)
                    for (int x = cx - 4; x <= cx + 4; ++x) {
                        net.minecraft.block.state.IBlockState st =
                            mc.world.getBlockState(wbp.setPos(x, y, z));
                        net.minecraft.block.Block blk = st.getBlock();
                        int id = net.minecraft.block.Block.getIdFromBlock(blk);
                        // BlockDoublePlant upper FACING is written into the
                        // Anvil nibble but getStateFromMeta drops it (always
                        // NORTH). Canonicalize only that measured lossy state;
                        // other block metadata remains in the live Java domain.
                        int meta = blk.getMetaFromState(st);
                        if (id == 175 && (meta & 8) != 0)
                            meta = 8 | 2; // UPPER + NORTH
                        wh ^= (long) ((id << 4) | meta) & 0xffffffffL;
                        wh *= 0x100000001b3L;
                    }
            b.append(",\"wfnv\":\"").append(String.format("%016x", wh))
             .append("\",\"wfa\":[").append(cx).append(",").append(cy)
             .append(",").append(cz).append("]");
        }
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
        if (!recExplosions.isEmpty()) {
            b.append(",\"expl\":[");
            for (int i = 0; i < recExplosions.size(); i++) {
                if (i > 0) b.append(',');
                b.append(recExplosions.get(i));
            }
            b.append(']');
            recExplosions.clear();
        }
        // whitelisted particle spawns this tick (MixinRecordParticles);
        // conditional like expl so quiet ticks stay byte-identical.
        if (!recParticles.isEmpty()) {
            b.append(",\"pcl\":[");
            for (int i = 0; i < recParticles.size(); i++) {
                if (i > 0) b.append(',');
                b.append(recParticles.get(i));
            }
            b.append(']');
            recParticles.clear();
        }
        // Drop clicks that landed across a dimension/loading boundary before
        // emit: the open container is gone and replaying them would hit a
        // closed or different screen. recLastDimension is still the previous
        // value here (updated below after the loading flag).
        boolean dimChanged = p.dimension != recLastDimension;
        boolean forcedLoading = dimChanged || !p.addedToChunk
            || mc.currentScreen instanceof net.minecraft.client.gui.GuiDownloadTerrain;
        if (forcedLoading) {
            clearRecGuiClicks();
            clearRecGuiLifecycle();
            clearRecBlockChanges();
        }
        // Container open/close edges + ordered GuiContainer clicks. Edge
        // detection runs first so gopen lands before gclk on the open tick;
        // gclose after clicks. Never invent clicks from gslots/gcur.
        try { trackContainerLifecycle(mc, p); } catch (Throwable ig) {}
        if (!recGuiOpens.isEmpty()) {
            b.append(",\"gopen\":[");
            for (int i = 0; i < recGuiOpens.size(); i++) {
                if (i > 0) b.append(',');
                b.append(recGuiOpens.get(i));
            }
            b.append(']');
            recGuiOpens.clear();
        }
        if (!recGuiClicks.isEmpty()) {
            b.append(",\"gclk\":[");
            for (int i = 0; i < recGuiClicks.size(); i++) {
                if (i > 0) b.append(',');
                b.append(recGuiClicks.get(i));
            }
            b.append(']');
            recGuiClicks.clear();
        }
        if (!recGuiCloses.isEmpty()) {
            b.append(",\"gclose\":[");
            for (int i = 0; i < recGuiCloses.size(); i++) {
                if (i > 0) b.append(',');
                b.append(recGuiCloses.get(i));
            }
            b.append(']');
            recGuiCloses.clear();
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
        if (!recFlag7Metadata.isEmpty()) {
            b.append(",\"f7\":[");
            for (int i = 0; i < recFlag7Metadata.size(); ++i) {
                if (i > 0) b.append(',');
                b.append(recFlag7Metadata.get(i).intValue());
            }
            b.append(']');
            recFlag7Metadata.clear();
        }
        // Client-world block finals this tick (MixinRecordBlockChanges +
        // MixinRecordChunkFill partial fillChunk diffs). Key "bc" avoids
        // collision with script event "snapshot_block". Conditional so quiet
        // ticks stay byte-identical to pre-recorder tapes.
        if (!recBlockChanges.isEmpty()) {
            b.append(",\"bc\":[");
            for (int i = 0; i < recBlockChanges.size(); ++i) {
                int[] e = recBlockChanges.get(i);
                if (i > 0) b.append(',');
                b.append('[').append(e[0]).append(',').append(e[1]).append(',')
                 .append(e[2]).append(',').append(e[3]).append(',')
                 .append(e[4]).append(']');
            }
            b.append(']');
            recBlockChanges.clear();
        }
        // dig_trace: post-tick PlayerControllerMP / leftClickCounter / ray /
        // held / relative hardness / break / neighbor-finals. Opt-in only
        // (header dig_trace:1); omitted entirely when recDigTrace is false so
        // default recordings remain byte-identical.
        if (recDigTrace) {
            appendDigTrace(b, mc, p);
            recNeighborBlockChanges.clear();
            recDigBreaks.clear();
        }
        /* During cross-dimension terrain download the client player exists but
         * is not yet in a loaded chunk, so WorldClient deliberately does not
         * tick it. Replay must preserve that interval rather than applying
         * gravity at the temporary spawn or newly received portal position.
         * forcedLoading was computed above (gclk drain) so the same boundary
         * both drops stale clicks and marks the row. */
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
        // active potions retint the whole frame (night vision/blindness/nausea).
        // 4th field = doesShowParticles: GuiIngame.renderPotionEffects draws the
        // top-right status icon ONLY when it is true, so a hideParticles effect
        // (`/effect <ent> <id> <sec> <amp> true`) must not make magma paint one.
        java.util.Collection<net.minecraft.potion.PotionEffect> pots = p.getActivePotionEffects();
        if (!pots.isEmpty()) {
            b.append(",\"pots\":[");
            int pn = 0;
            for (net.minecraft.potion.PotionEffect pe : pots) {
                if (pn++ > 0) b.append(",");
                b.append("[").append(net.minecraft.potion.Potion.getIdFromPotion(pe.getPotion()))
                 .append(",").append(pe.getAmplifier())
                 .append(",").append(pe.getDuration())
                 .append(",").append(pe.doesShowParticles() ? 1 : 0).append("]");
            }
            b.append("]");
        }
        // generic.armor total behind the HUD armor row. NOT derivable from the
        // inv dump: ItemStack.getAttributeModifiers returns ONLY the NBT
        // AttributeModifiers when that tag is present, so a leather chestplate
        // carrying just a knockbackResistance modifier is worth 0 armor.
        b.append(",\"armor\":").append(p.getTotalArmorValue());
        // full inventory on change OR keyframe cadence (slots 0-40: main 36,
        // armor 4, offhand). Change dumps catch real evolution; keyframes every
        // 20 ticks line up with collect_state_assertions sampling so a tape is
        // not "inventory PASS" on the tick-0 seed alone. Full 41-slot dump each
        // time (not sparse deltas) keeps old readers working.
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
            // recTick was already incremented for this row; same cadence as
            // sparse pixel goldens (recFrameEvery, default 20). When frames
            // are off (0), still keyframe every 20 so the state gate has rows.
            int invEvery = recFrameEvery > 0 ? recFrameEvery : 20;
            boolean invKeyframe = ((recTick - 1) % invEvery) == 0;
            if (!cur.equals(recLastInv) || invKeyframe) {
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
                     .append(",").append(stationary)
                     // Entity.ticksExisted: RenderDragon.renderCrystalBeams
                     // scrolls the healing beam by -ticksExisted*0.01 per tick.
                     // Client-side counter, not derivable from the tape.
                     .append(",").append(e.ticksExisted);
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
                 .append(",").append(bt == null ? -1 : bt.getZ())
                 // Entity.ticksExisted: RenderDragon.doRender pulses the
                 // healing beam's crystal end by sin(ticksExisted*0.2).
                 .append(",").append(ec.ticksExisted);
            } else if (e instanceof net.minecraft.entity.item.EntityFallingBlock) {
                // RenderFallingBlock: fallTile block id + meta. Appended so
                // old 7-field rows still parse (magma defaults to sand).
                net.minecraft.entity.item.EntityFallingBlock fb =
                    (net.minecraft.entity.item.EntityFallingBlock) e;
                net.minecraft.block.state.IBlockState st = fb.getBlock();
                int bid = 12, meta = 0; // Blocks.SAND default
                if (st != null) {
                    bid = net.minecraft.block.Block.getIdFromBlock(st.getBlock());
                    meta = st.getBlock().getMetaFromState(st);
                }
                b.append(",").append(bid).append(",").append(meta);
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
        o.addProperty("policy_action_seq", rlPolicyActionSeq);
        o.addProperty("policy_action_fnv64",
                      Long.toUnsignedString(rlPolicyActionFnv, 16));
        try {
            o.addProperty("world_seed", mc.getIntegratedServer().worlds[0].getSeed());
            o.addProperty("save_folder", mc.getIntegratedServer().getFolderName());
        } catch (Throwable ig) {}
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
