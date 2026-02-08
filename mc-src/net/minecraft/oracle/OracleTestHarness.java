package net.minecraft.oracle;

import com.mojang.authlib.GameProfile;
import io.netty.buffer.Unpooled;
import java.io.File;
import java.util.UUID;
import net.minecraft.entity.player.EntityPlayerMP;
import net.minecraft.network.NetHandlerPlayServer;
import net.minecraft.network.NetworkManager;
import net.minecraft.network.PacketBuffer;
import net.minecraft.network.play.client.*;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.management.ItemInWorldManager;
import net.minecraft.world.WorldServer;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

/**
 * End-to-end test harness for oracle recording, replay, and validation.
 *
 * Controlled by system properties:
 *   -Doracle.test=record   Record a scripted bot session, export state, shutdown
 *   -Doracle.test=replay   Replay the recording, export state, shutdown
 *   -Doracle.test=validate Compare record vs replay snapshots, report result
 *   -Doracle.test.ticks=N  Number of ticks to run (default 200)
 *
 * Called from MinecraftServer.tick() each server tick.
 */
public class OracleTestHarness
{
    private static final Logger logger = LogManager.getLogger();
    private static OracleTestHarness INSTANCE;

    private final String mode;
    private final int testTicks;
    private boolean initialized;
    private boolean finished;

    // Recording mode state
    private EntityPlayerMP testBot;
    private NetHandlerPlayServer testHandler;
    private int botCreatedTick;
    private double startX, startY, startZ;

    private OracleTestHarness()
    {
        this.mode = System.getProperty("oracle.test", "");
        this.testTicks = Integer.parseInt(System.getProperty("oracle.test.ticks", "200"));
    }

    public static synchronized OracleTestHarness get()
    {
        if (INSTANCE == null)
        {
            INSTANCE = new OracleTestHarness();
        }
        return INSTANCE;
    }

    public boolean isActive()
    {
        return !this.mode.isEmpty() && !this.finished;
    }

    public String getMode()
    {
        return this.mode;
    }

    /**
     * Called each server tick from MinecraftServer.tick().
     */
    public void tick(MinecraftServer server, int currentTick)
    {
        if (this.finished || this.mode.isEmpty()) return;

        if ("record".equals(this.mode))
        {
            tickRecord(server, currentTick);
        }
        else if ("replay".equals(this.mode))
        {
            tickReplay(server, currentTick);
        }
        else if ("validate".equals(this.mode))
        {
            runValidation(server);
        }
    }

    // ========== RECORD MODE ==========

    private void tickRecord(MinecraftServer server, int currentTick)
    {
        if (!this.initialized)
        {
            if (server.worldServers == null || server.worldServers.length == 0) return;
            WorldServer world = server.worldServerForDimension(0);
            if (world == null) return;

            createTestBot(server, world);
            this.botCreatedTick = currentTick;
            this.initialized = true;
            logger.info("[Oracle Test] RECORD mode: bot created at tick {}, running {} ticks", currentTick, this.testTicks);

            // Start recording explicitly (auto-recording is disabled in test mode)
            long seed = world.getSeed();
            String path = new java.io.File(world.getSaveHandler().getWorldDirectory(), "oracle_recording.nrec").getAbsolutePath();
            OracleRecorder.get().startRecording(path, seed, currentTick);
            return;
        }

        int elapsed = currentTick - this.botCreatedTick;

        // Inject scripted actions (skip tick 0 to let recording start first)
        if (elapsed >= 2 && elapsed <= this.testTicks)
        {
            injectScriptedAction(currentTick, elapsed);
        }

        if (elapsed == this.testTicks)
        {
            exportAndShutdown(server, currentTick, "oracle_record");
        }
    }

    private void createTestBot(MinecraftServer server, WorldServer world)
    {
        GameProfile profile = new GameProfile(
            UUID.nameUUIDFromBytes("OracleTestBot".getBytes()), "OracleTestBot");
        ItemInWorldManager itemManager = new ItemInWorldManager(world);
        this.testBot = new EntityPlayerMP(server, world, profile, itemManager);

        NetworkManager netManager = new NetworkManager(false);
        this.testHandler = new NetHandlerPlayServer(server, netManager, this.testBot);

        // This registers the player in the world and player list
        server.getConfigurationManager().initializeConnectionToPlayer(
            netManager, this.testBot, this.testHandler);

        this.startX = this.testBot.posX;
        this.startY = this.testBot.posY;
        this.startZ = this.testBot.posZ;

        logger.info("[Oracle Test] Bot spawned at (" + this.startX + ", " + this.startY + ", " + this.startZ + ")");
    }

    /**
     * Inject a scripted action for the current tick.
     * Goes through the normal process* handler so recording hooks capture it.
     *
     * Script:
     *   ticks 2-150:   Walk forward slowly (position updates)
     *   ticks 40,80:   Change held item slot
     *   ticks 60,120:  Swing arm (animation)
     *   ticks 100:     Start sprinting
     *   ticks 160:     Stop sprinting
     */
    private void injectScriptedAction(int tick, int elapsed)
    {
        try
        {
            // Position update: walk forward along +X axis
            double newX = this.startX + (elapsed - 2) * 0.04; // slow walk
            double newY = this.startY;
            double newZ = this.startZ;
            double stance = newY + 1.6200000047683716D;
            float yaw = (elapsed * 1.8f) % 360.0f;
            float pitch = 0.0f;

            // Build C06 (pos+look) packet via PacketBuffer
            PacketBuffer buf = new PacketBuffer(Unpooled.buffer(41));
            buf.writeDouble(newX);
            buf.writeDouble(newY);
            buf.writeDouble(stance);
            buf.writeDouble(newZ);
            buf.writeFloat(yaw);
            buf.writeFloat(pitch);
            buf.writeByte(1); // onGround = true

            C03PacketPlayer.C06PacketPlayerPosLook posLookPkt = new C03PacketPlayer.C06PacketPlayerPosLook();
            posLookPkt.readPacketData(buf);
            buf.release();
            this.testHandler.processPlayer(posLookPkt);

            // Held item changes
            if (elapsed == 40 || elapsed == 80)
            {
                int slot = (elapsed == 40) ? 1 : 3;
                PacketBuffer hbuf = new PacketBuffer(Unpooled.buffer(2));
                hbuf.writeShort(slot);
                C09PacketHeldItemChange heldPkt = new C09PacketHeldItemChange();
                heldPkt.readPacketData(hbuf);
                hbuf.release();
                this.testHandler.processHeldItemChange(heldPkt);
            }

            // Arm swing
            if (elapsed == 60 || elapsed == 120)
            {
                PacketBuffer abuf = new PacketBuffer(Unpooled.buffer(5));
                abuf.writeInt(this.testBot.getEntityId());
                abuf.writeByte(1); // swing arm
                C0APacketAnimation animPkt = new C0APacketAnimation();
                animPkt.readPacketData(abuf);
                abuf.release();
                this.testHandler.processAnimation(animPkt);
            }

            // Sprint start/stop
            if (elapsed == 100 || elapsed == 160)
            {
                int actionId = (elapsed == 100) ? 4 : 5; // 4=start sprint, 5=stop sprint
                PacketBuffer ebuf = new PacketBuffer(Unpooled.buffer(9));
                ebuf.writeInt(this.testBot.getEntityId());
                ebuf.writeByte(actionId);
                ebuf.writeInt(0); // jumpBoost
                C0BPacketEntityAction entPkt = new C0BPacketEntityAction();
                entPkt.readPacketData(ebuf);
                ebuf.release();
                this.testHandler.processEntityAction(entPkt);
            }
        }
        catch (Exception e)
        {
            logger.error("[Oracle Test] Error injecting action at tick " + tick, e);
        }
    }

    // ========== REPLAY MODE ==========

    private void tickReplay(MinecraftServer server, int currentTick)
    {
        if (!this.initialized)
        {
            if (server.worldServers == null || server.worldServers.length == 0) return;
            WorldServer world = server.worldServerForDimension(0);
            if (world == null) return;

            String worldDir = world.getSaveHandler().getWorldDirectory().getAbsolutePath();
            String recordingPath = worldDir + "/oracle_recording.nrec";

            File recordingFile = new File(recordingPath);
            if (!recordingFile.exists())
            {
                logger.error("[Oracle Test] Recording file not found: {}", recordingPath);
                this.finished = true;
                server.initiateShutdown();
                return;
            }

            if (!OracleReplay.get().loadRecording(recordingPath))
            {
                logger.error("[Oracle Test] Failed to load recording");
                this.finished = true;
                server.initiateShutdown();
                return;
            }

            OracleReplay.get().startReplay(server);
            this.botCreatedTick = currentTick;
            this.initialized = true;
            logger.info("[Oracle Test] REPLAY mode: started at tick {}, running {} ticks", currentTick, this.testTicks);
            return;
        }

        // OracleReplay.tickReplay() is called from MinecraftServer.tick() already

        int elapsed = currentTick - this.botCreatedTick;

        if (elapsed == this.testTicks)
        {
            exportAndShutdown(server, currentTick, "oracle_replay");
        }
    }

    // ========== VALIDATE MODE ==========

    private void runValidation(MinecraftServer server)
    {
        if (server.worldServers == null || server.worldServers.length == 0) return;

        String worldDir = server.worldServers[0].getSaveHandler().getWorldDirectory().getAbsolutePath();

        // Find the snapshot files
        File dir = new File(worldDir);
        String recordFile = null;
        String replayFile = null;

        File[] files = dir.listFiles();
        if (files != null)
        {
            for (File f : files)
            {
                String name = f.getName();
                if (name.startsWith("oracle_record_dim0_") && name.endsWith(".nsta"))
                {
                    recordFile = f.getAbsolutePath();
                }
                if (name.startsWith("oracle_replay_dim0_") && name.endsWith(".nsta"))
                {
                    replayFile = f.getAbsolutePath();
                }
            }
        }

        if (recordFile == null || replayFile == null)
        {
            logger.error("[Oracle Test] VALIDATE: snapshot files not found in " + worldDir + ". record=" + recordFile + " replay=" + replayFile);
            this.finished = true;
            server.initiateShutdown();
            return;
        }

        logger.info("[Oracle Test] VALIDATE: comparing " + recordFile + " vs " + replayFile);
        OracleValidator.ValidationResult result = OracleValidator.compare(recordFile, replayFile);

        if (result.passed)
        {
            logger.info("========================================");
            logger.info("[Oracle Test] VALIDATION PASSED");
            logger.info("========================================");
        }
        else
        {
            logger.error("========================================");
            logger.error("[Oracle Test] VALIDATION FAILED: {} divergence(s)", result.divergenceCount);
            logger.error("[Oracle Test] First: {}", result.firstDivergence);
            logger.error("========================================");
        }

        this.finished = true;
        server.initiateShutdown();
    }

    // ========== COMMON ==========

    private void exportAndShutdown(MinecraftServer server, int currentTick, String prefix)
    {
        String worldDir = server.worldServers[0].getSaveHandler().getWorldDirectory().getAbsolutePath();

        // Export overworld (dim 0) state
        WorldServer overworld = server.worldServerForDimension(0);
        if (overworld != null)
        {
            String path = worldDir + "/" + prefix + "_dim0_tick" + currentTick + ".nsta";
            OracleStateExporter.exportState(overworld, path, currentTick);
            logger.info("[Oracle Test] State exported: {}", path);
        }

        // Stop recording if active
        if (OracleRecorder.get().isRecording())
        {
            OracleRecorder.get().stopRecording();
        }

        logger.info("[Oracle Test] {} phase complete. Shutting down.", prefix.toUpperCase());
        this.finished = true;
        server.initiateShutdown();
    }
}
