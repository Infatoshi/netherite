package com.netherite.mod;

import net.minecraft.client.MinecraftClient;
import net.minecraft.client.gui.screen.TitleScreen;
import net.minecraft.client.option.CloudRenderMode;
import net.minecraft.client.option.GraphicsMode;
import net.minecraft.client.option.ParticlesMode;
import net.minecraft.client.render.ChunkBuilderMode;
import net.minecraft.resource.DataConfiguration;
import net.minecraft.server.integrated.IntegratedServerLoader;
import net.minecraft.world.GameRules;
import net.minecraft.world.gen.GeneratorOptions;
import net.minecraft.world.gen.WorldPresets;
import net.minecraft.util.math.MathHelper;
import net.minecraft.world.level.LevelInfo;

import java.nio.MappedByteBuffer;
import java.util.HashMap;
import java.util.Map;

/**
 * Applies all config to MC options, auto-creates world, dismisses menus in RL mode.
 */
public class WorldController {
    public static final WorldController INSTANCE = new WorldController();

    private static final int CONTROL_MAGIC = 0x4E455443; // "NETC"
    private static final int CONTROL_SIZE = 4096;
    private static final int CTRL_STATUS_IDLE = 0;
    private static final int CTRL_STATUS_BUSY = 1;
    private static final int CTRL_STATUS_DONE = 2;
    private static final int CTRL_STATUS_ERROR = 3;
    private static final int CTRL_OP_RESET_WORLD = 1;
    private static final int CTRL_OP_SET_POSE = 2;
    private static final int CTRL_OP_RELEASE_START = 3;
    private static final int STARTUP_SETTLE_TICKS = 512;
    private static final int STARTUP_RENDER_READY_TICKS = 16;
    private static final int STARTUP_WORLD_READY_TICKS = 16;

    private enum ResetPhase {
        NONE,
        DISCONNECTING,
        DELETING,
        CREATING,
        WAITING_FOR_WORLD,
        TELEPORTING
    }

    private static final class StartPose {
        final double x;
        final double y;
        final double z;
        final float yaw;
        final float pitch;

        private StartPose(double x, double y, double z, float yaw, float pitch) {
            this.x = x;
            this.y = y;
            this.z = z;
            this.yaw = yaw;
            this.pitch = pitch;
        }
    }

    private NetheriteConfig cfg;
    private boolean worldCreated = false;
    private boolean optionsApplied = false;
    private int titleScreenTicks = 0;
    private int ticksSinceCreation = 0;
    private ShmemBuffer controlShmem;
    private long activeSeed;
    private int episodeId = 0;
    private int lastControlRequestId = 0;
    private int pendingResetRequestId = 0;
    private long pendingResetSeed;
    private int pendingTeleportRequestId = 0;
    private StartPose pendingTeleportPose;
    private boolean pendingTeleportIssued = false;
    private ResetPhase resetPhase = ResetPhase.NONE;
    private boolean resetDisconnectRequested = false;
    private boolean startLatchPending = false;
    private boolean startLatched = false;
    private boolean startRenderReloadIssued = false;
    private int startRenderReadyTicks = 0;
    private long startWorldFingerprint = Long.MIN_VALUE;
    private int startWorldChunkMask = -1;
    private int startWorldReadyTicks = 0;
    private final Map<Long, StartPose> startPoses = new HashMap<>();

    public void init(NetheriteConfig cfg) {
        this.cfg = cfg;
        this.activeSeed = cfg.seed;
        this.controlShmem = new ShmemBuffer("netherite_control_" + cfg.instanceId, CONTROL_SIZE);
        resetStartupLatchState();
        writeControlState(CTRL_STATUS_IDLE, 0);
    }

    public void tick(MinecraftClient mc) {
        // Apply options once MC is initialized
        if (!optionsApplied) {
            applyOptions(mc);
            optionsApplied = true;
        }

        mc.options.pauseOnLostFocus = false;

        pollControlRequests();
        if (resetPhase != ResetPhase.NONE) {
            driveReset(mc);
            return;
        }

        if (mc.world != null) {
            titleScreenTicks = 0;
            StartPose startPose = getOrCreateStartPose(mc, activeSeed);
            if (mc.player == null) {
                return;
            }
            if (ticksSinceCreation == 0 && !isAtStartPose(mc, startPose)) {
                teleportPlayerToStart(mc, startPose);
                return;
            }
            if (startLatchPending) {
                if (!isAtStartPose(mc, startPose)) {
                    if (ticksSinceCreation % 40 == 0) {
                        NetheriteMod.LOGGER.info("WorldController: teleporting to start pose (x={}, y={}, z={}, yaw={}, pitch={})",
                                startPose.x, startPose.y, startPose.z, startPose.yaw, startPose.pitch);
                    }
                    teleportPlayerToStart(mc, startPose);
                    return;
                }
                if (!startLatched) {
                    NetheriteMod.LOGGER.info("WorldController: at start pose, arming latch");
                    armStartLatch();
                }
                if (cfg.rl && mc.currentScreen != null) {
                    mc.setScreen(null);
                }
                if (cfg.rl) {
                    mc.getToastManager().clear();
                }
                return;
            }
            if (ticksSinceCreation == 0 && episodeId == 0) {
                StateExporter.INSTANCE.resetTickCounter();
                episodeId = 1;
                writeControlState(CTRL_STATUS_IDLE, 0);
            }
            if (cfg.rl && mc.currentScreen != null) {
                mc.setScreen(null);
            }
            // Clear toasts (recipe unlocked, advancements, tutorials)
            if (cfg.rl) {
                mc.getToastManager().clear();
            }
            ticksSinceCreation++;
            return;
        }

        if (mc.currentScreen instanceof TitleScreen) {
            titleScreenTicks++;
            if (!worldCreated && titleScreenTicks >= 40) {
                worldCreated = true;
                createWorld(mc);
            }
        } else {
            if (cfg.rl && !worldCreated && mc.currentScreen != null) {
                mc.setScreen(new TitleScreen());
                return;
            }
            titleScreenTicks = 0;
        }
    }

    private void pollControlRequests() {
        if (controlShmem == null) {
            return;
        }

        MappedByteBuffer buf = controlShmem.getBuffer();
        if (buf.getInt(0) != CONTROL_MAGIC) {
            writeControlState(resetPhase == ResetPhase.NONE ? CTRL_STATUS_IDLE : CTRL_STATUS_BUSY, 0);
            return;
        }

        int requestId = buf.getInt(4);
        if (requestId == 0 || requestId == lastControlRequestId) {
            return;
        }

        lastControlRequestId = requestId;
        int opcode = buf.getInt(16);
        long requestedSeed = buf.getLong(24);
        if (opcode == CTRL_OP_RESET_WORLD) {
            startReset(requestId, requestedSeed);
            return;
        }

        if (opcode == CTRL_OP_SET_POSE) {
            startTeleport(
                    requestId,
                    new StartPose(
                            buf.getDouble(48),
                            buf.getDouble(56),
                            buf.getDouble(64),
                            buf.getFloat(72),
                            buf.getFloat(76)
                    )
            );
            return;
        }

        if (opcode == CTRL_OP_RELEASE_START) {
            releaseStartLatch(requestId);
            return;
        }

        NetheriteMod.LOGGER.error("WorldController: unsupported control opcode {}", opcode);
        writeControlState(CTRL_STATUS_ERROR, requestId);
    }

    private void startReset(int requestId, long seed) {
        pendingResetRequestId = requestId;
        pendingResetSeed = seed;
        resetPhase = ResetPhase.DISCONNECTING;
        resetDisconnectRequested = false;
        writeControlState(CTRL_STATUS_BUSY, 0);
        NetheriteMod.LOGGER.info("WorldController: reset requested for '{}' seed={}",
                worldName(), pendingResetSeed);
    }

    private void startTeleport(int requestId, StartPose pose) {
        pendingTeleportRequestId = requestId;
        pendingTeleportPose = pose;
        pendingTeleportIssued = false;
        resetPhase = ResetPhase.TELEPORTING;
        writeControlState(CTRL_STATUS_BUSY, 0);
        NetheriteMod.LOGGER.info(
                "WorldController: teleport requested to ({}, {}, {}) yaw={} pitch={}",
                pose.x,
                pose.y,
                pose.z,
                pose.yaw,
                pose.pitch
        );
    }

    private void driveReset(MinecraftClient mc) {
        switch (resetPhase) {
            case DISCONNECTING -> {
                if (!resetDisconnectRequested) {
                    var server = mc.getServer();
                    if (server != null && !server.isStopping()) {
                        server.stop(false);
                    }
                    if (mc.world != null || mc.player != null || mc.isIntegratedServerRunning()) {
                        mc.disconnect(new TitleScreen());
                    }
                    resetDisconnectRequested = true;
                    return;
                }

                if (mc.world != null || mc.player != null || mc.isIntegratedServerRunning()) {
                    return;
                }

                resetDisconnectRequested = false;
                resetPhase = ResetPhase.DELETING;
            }
            case DELETING -> {
                if (!(mc.currentScreen instanceof TitleScreen)) {
                    mc.setScreen(new TitleScreen());
                    return;
                }

                try (var session = mc.getLevelStorage().createSession(worldName())) {
                    session.deleteSessionLock();
                } catch (Exception e) {
                    NetheriteMod.LOGGER.warn("WorldController: failed to delete '{}', will retry",
                            worldName(), e);
                    return;
                }

                cfg.seed = pendingResetSeed;
                activeSeed = pendingResetSeed;
                worldCreated = false;
                titleScreenTicks = 0;
                ticksSinceCreation = 0;
                resetStartupLatchState();
                resetPhase = ResetPhase.CREATING;
            }
            case CREATING -> {
                if (!(mc.currentScreen instanceof TitleScreen)) {
                    mc.setScreen(new TitleScreen());
                    return;
                }

                if (!worldCreated) {
                    titleScreenTicks++;
                    if (titleScreenTicks < 40) {
                        return;
                    }
                    worldCreated = true;
                    titleScreenTicks = 0;
                    createWorld(mc);
                }
                resetPhase = ResetPhase.WAITING_FOR_WORLD;
            }
            case WAITING_FOR_WORLD -> {
                if (mc.world != null && mc.player != null) {
                    StartPose startPose = getOrCreateStartPose(mc, pendingResetSeed);
                    if (!isAtStartPose(mc, startPose)) {
                        teleportPlayerToStart(mc, startPose);
                        return;
                    }

                    ticksSinceCreation = 0;
                    resetStartupLatchState();
                    if (!cfg.rl) {
                        StateExporter.INSTANCE.resetTickCounter();
                        episodeId++;
                    }
                    writeControlState(CTRL_STATUS_DONE, pendingResetRequestId);
                    pendingResetRequestId = 0;
                    resetPhase = ResetPhase.NONE;
                    return;
                }

                if (mc.currentScreen instanceof TitleScreen && !worldCreated) {
                    resetPhase = ResetPhase.CREATING;
                }
            }
            case TELEPORTING -> {
                if (mc.world == null || mc.player == null || pendingTeleportPose == null) {
                    return;
                }

                if (!pendingTeleportIssued || !isAtStartPose(mc, pendingTeleportPose)) {
                    teleportPlayerToStart(mc, pendingTeleportPose);
                    pendingTeleportIssued = true;
                    return;
                }

                writeControlState(CTRL_STATUS_DONE, pendingTeleportRequestId);
                pendingTeleportRequestId = 0;
                pendingTeleportPose = null;
                pendingTeleportIssued = false;
                resetPhase = ResetPhase.NONE;
            }
            default -> {
            }
        }
    }

    private void applyOptions(MinecraftClient mc) {
        var opts = mc.options;

        // Display
        opts.getViewDistance().setValue(cfg.renderDistance);
        opts.getSimulationDistance().setValue(cfg.simulationDistance);
        // MC's "Unlimited" FPS option uses value 260 as special marker
        // Values above 260 actually make MC slower, so cap at 260
        opts.getMaxFps().setValue(Math.min(cfg.maxFps, 260));
        opts.getEnableVsync().setValue(cfg.vsync);
        opts.getFov().setValue(cfg.fov);
        opts.getGuiScale().setValue(cfg.guiScale);
        opts.getFullscreen().setValue(cfg.fullscreen);

        // Graphics
        opts.getGraphicsMode().setValue(switch (cfg.graphics) {
            case "fancy" -> GraphicsMode.FANCY;
            case "fabulous" -> GraphicsMode.FABULOUS;
            default -> GraphicsMode.FAST;
        });
        opts.getChunkBuilderMode().setValue(ChunkBuilderMode.NEARBY);
        opts.getParticles().setValue(switch (cfg.particles) {
            case "all" -> ParticlesMode.ALL;
            case "decreased" -> ParticlesMode.DECREASED;
            default -> ParticlesMode.MINIMAL;
        });
        opts.getCloudRenderMode().setValue(switch (cfg.clouds) {
            case "fast" -> CloudRenderMode.FAST;
            case "fancy" -> CloudRenderMode.FANCY;
            default -> CloudRenderMode.OFF;
        });
        opts.getEntityShadows().setValue(cfg.entityShadows);
        opts.getAo().setValue(cfg.smoothLighting);
        opts.getBiomeBlendRadius().setValue(cfg.biomeBlend);

        // Disable tutorial
        opts.tutorialStep = net.minecraft.client.tutorial.TutorialStep.NONE;

        // RL/headless: strip everything unnecessary
        if (cfg.rl || cfg.headless) {
            // Audio -- mute all channels
            opts.getSoundVolumeOption(net.minecraft.sound.SoundCategory.MASTER).setValue(0.0);
            opts.getSoundVolumeOption(net.minecraft.sound.SoundCategory.MUSIC).setValue(0.0);
            opts.getSoundVolumeOption(net.minecraft.sound.SoundCategory.WEATHER).setValue(0.0);
            opts.getSoundVolumeOption(net.minecraft.sound.SoundCategory.AMBIENT).setValue(0.0);

            // Input -- prevent interference with agent actions
            opts.getAutoJump().setValue(false);
            opts.getMouseSensitivity().setValue(0.5);

            // HUD/UI -- disable visual noise
            opts.getChatOpacity().setValue(0.0);
            opts.getTextBackgroundOpacity().setValue(0.0);
            opts.getNarrator().setValue(net.minecraft.client.option.NarratorMode.OFF);
            opts.getShowSubtitles().setValue(false);
            opts.getRealmsNotifications().setValue(false);
            opts.getNotificationDisplayTime().setValue(0.5);
            opts.getShowAutosaveIndicator().setValue(false);

            // Visual effects -- disable for consistency/performance
            opts.getDistortionEffectScale().setValue(0.0);
            opts.getFovEffectScale().setValue(0.0);
            opts.getDarknessEffectScale().setValue(0.0);
            opts.getDamageTiltStrength().setValue(0.0);
            opts.getGlintSpeed().setValue(0.0);
            opts.getGlintStrength().setValue(0.0);
            opts.getHideLightningFlashes().setValue(true);
            opts.getHighContrast().setValue(false);
            opts.getBobView().setValue(false);

            // Network/telemetry -- disable
            opts.getAllowServerListing().setValue(false);
            opts.getAutoSuggestions().setValue(false);

            // Accessibility
            opts.onboardAccessibility = false;
        }

        opts.write();

        NetheriteMod.LOGGER.info("WorldController: options applied (rd={}, fps={}, graphics={})",
                cfg.renderDistance, cfg.maxFps, cfg.graphics);
    }

    private void createWorld(MinecraftClient mc) {
        String worldName = worldName();
        NetheriteMod.LOGGER.info("WorldController: creating world '{}' seed={}", worldName, cfg.seed);

        mc.execute(() -> {
            try {
                GameRules rules = new GameRules();
                rules.get(GameRules.DO_DAYLIGHT_CYCLE).set(cfg.doDaylightCycle, null);
                rules.get(GameRules.DO_WEATHER_CYCLE).set(cfg.doWeatherCycle, null);
                rules.get(GameRules.DO_MOB_SPAWNING).set(cfg.doMobSpawning, null);
                rules.get(GameRules.DO_FIRE_TICK).set(cfg.doFireTick, null);
                rules.get(GameRules.DO_MOB_GRIEFING).set(cfg.doMobGriefing, null);
                rules.get(GameRules.DO_ENTITY_DROPS).set(cfg.doEntityDrops, null);
                rules.get(GameRules.DO_TILE_DROPS).set(cfg.doTileDrops, null);
                rules.get(GameRules.NATURAL_REGENERATION).set(cfg.naturalRegeneration, null);
                rules.get(GameRules.RANDOM_TICK_SPEED).set(cfg.randomTickSpeed, null);
                rules.get(GameRules.KEEP_INVENTORY).set(cfg.keepInventory, null);
                rules.get(GameRules.DO_INSOMNIA).set(cfg.doInsomnia, null);
                rules.get(GameRules.DO_PATROL_SPAWNING).set(cfg.doPatrolSpawning, null);
                rules.get(GameRules.DO_TRADER_SPAWNING).set(cfg.doTraderSpawning, null);
                rules.get(GameRules.DO_WARDEN_SPAWNING).set(cfg.doWardenSpawning, null);
                rules.get(GameRules.ANNOUNCE_ADVANCEMENTS).set(false, null);

                LevelInfo levelInfo = new LevelInfo(
                        worldName,
                        cfg.gameMode,
                        false,
                        cfg.difficulty,
                        false,
                        rules,
                        DataConfiguration.SAFE_MODE
                );

                GeneratorOptions genOpts = new GeneratorOptions(cfg.seed, true, false);

                IntegratedServerLoader loader = mc.createIntegratedServerLoader();
                loader.createAndStart(worldName, levelInfo, genOpts,
                        WorldPresets::createDemoOptions);
            } catch (Exception e) {
                NetheriteMod.LOGGER.error("WorldController: failed to create world", e);
                worldCreated = false;
            }
        });
    }

    private String worldName() {
        return "netherite_" + cfg.instanceId;
    }

    private StartPose getOrCreateStartPose(MinecraftClient mc, long seed) {
        StartPose cached = startPoses.get(seed);
        if (cached != null) {
            return cached;
        }

        var spawnPos = mc.world.getSpawnPos();
        StartPose startPose = new StartPose(
                spawnPos.getX() + 0.5,
                spawnPos.getY(),
                spawnPos.getZ() + 0.5,
                0.0f,
                0.0f
        );
        startPoses.put(seed, startPose);
        return startPose;
    }

    private StartPose captureStartPose(MinecraftClient mc) {
        return new StartPose(
                mc.player.getX(),
                mc.player.getY(),
                mc.player.getZ(),
                mc.player.getYaw(),
                mc.player.getPitch()
        );
    }

    private boolean isAtStartPose(MinecraftClient mc, StartPose startPose) {
        if (mc.player == null) {
            return false;
        }

        return Math.abs(mc.player.getX() - startPose.x) < 0.01
                && Math.abs(mc.player.getY() - startPose.y) < 0.01
                && Math.abs(mc.player.getZ() - startPose.z) < 0.01
                && Math.abs(MathHelper.wrapDegrees(mc.player.getYaw() - startPose.yaw)) < 0.05f
                && Math.abs(mc.player.getPitch() - startPose.pitch) < 0.05f;
    }

    private void teleportPlayerToStart(MinecraftClient mc, StartPose startPose) {
        if (mc.player == null) {
            return;
        }

        mc.player.refreshPositionAndAngles(
                startPose.x,
                startPose.y,
                startPose.z,
                startPose.yaw,
                startPose.pitch
        );

        var server = mc.getServer();
        if (server == null) {
            return;
        }

        var playerUuid = mc.player.getUuid();
        server.execute(() -> {
            var serverPlayer = server.getPlayerManager().getPlayer(playerUuid);
            if (serverPlayer != null) {
                serverPlayer.teleport(
                        serverPlayer.getServerWorld(),
                        startPose.x,
                        startPose.y,
                        startPose.z,
                        startPose.yaw,
                        startPose.pitch
                );
            }
        });
    }

    private void armStartLatch() {
        if (startLatched || !startLatchPending) {
            return;
        }
        startLatched = true;
        ticksSinceCreation = 0;
        StateExporter.INSTANCE.resetTickCounter();
        if (episodeId == 0) {
            episodeId = 1;
        } else if (cfg.rl) {
            episodeId++;
        }
        writeControlState(CTRL_STATUS_IDLE, 0);
        NetheriteMod.LOGGER.info("WorldController: startup latch armed for seed={}", activeSeed);
    }

    private void releaseStartLatch(int requestId) {
        startLatched = false;
        startLatchPending = false;
        ticksSinceCreation = 0;
        startRenderReloadIssued = false;
        startRenderReadyTicks = 0;
        writeControlState(CTRL_STATUS_DONE, requestId);
        NetheriteMod.LOGGER.info("WorldController: startup latch released for seed={}", activeSeed);
    }

    private void resetStartupLatchState() {
        startLatchPending = cfg.rl;
        startLatched = false;
        startRenderReloadIssued = false;
        startRenderReadyTicks = 0;
        startWorldFingerprint = Long.MIN_VALUE;
        startWorldChunkMask = -1;
        startWorldReadyTicks = 0;
    }

    private boolean isStartupRenderReady(MinecraftClient mc) {
        var chunkBuilder = mc.worldRenderer.getChunkBuilder();
        if (chunkBuilder == null) {
            return false;
        }
        boolean empty = chunkBuilder.isEmpty();
        int toBatch = chunkBuilder.getToBatchCount();
        int toUpload = chunkBuilder.getChunksToUpload();
        int completed = mc.worldRenderer.getCompletedChunkCount();
        boolean ready = empty && toBatch == 0 && toUpload == 0 && completed > 0;

        // Log every tick during startup render ready phase
        if (!ready && startRenderReloadIssued && startRenderReadyTicks == 0) {
            NetheriteMod.LOGGER.info("WorldController render ready: empty={}, toBatch={}, toUpload={}, completed={}",
                    empty, toBatch, toUpload, completed);
        }

        return ready;
    }

    private boolean isStartupWorldReady(MinecraftClient mc) {
        long fingerprint = StateExporter.INSTANCE.currentWorldFingerprint(mc);
        int chunkMask = StateExporter.INSTANCE.currentChunkMask(mc);
        int loadedChunks = Integer.bitCount(chunkMask);

        // Log progress during startup
        if (startWorldReadyTicks == 0 && loadedChunks >= 25) {
            NetheriteMod.LOGGER.info("WorldController startup: loadedChunks={}, waiting for stable fingerprint", loadedChunks);
        }
        if (startWorldReadyTicks > 0 && startWorldReadyTicks % 4 == 0) {
            NetheriteMod.LOGGER.info("WorldController startup: fingerprint stable for {}/{} ticks",
                    startWorldReadyTicks, STARTUP_WORLD_READY_TICKS);
        }

        if (loadedChunks < 25) {
            startWorldFingerprint = Long.MIN_VALUE;
            startWorldChunkMask = -1;
            startWorldReadyTicks = 0;
            return false;
        }
        if (fingerprint == startWorldFingerprint && chunkMask == startWorldChunkMask) {
            startWorldReadyTicks++;
        } else {
            startWorldFingerprint = fingerprint;
            startWorldChunkMask = chunkMask;
            startWorldReadyTicks = 1;
        }
        return startWorldReadyTicks >= STARTUP_WORLD_READY_TICKS;
    }

    private void writeControlState(int status, int ackRequestId) {
        if (controlShmem == null) {
            return;
        }

        MappedByteBuffer buf = controlShmem.getBuffer();
        buf.putInt(0, CONTROL_MAGIC);
        buf.putInt(8, ackRequestId);
        buf.putInt(12, status);
        buf.putLong(32, activeSeed);
        buf.putInt(40, episodeId);
        buf.putInt(44, startLatched ? 1 : 0);
        buf.force();
    }

    public void requestReset() {
        startReset(lastControlRequestId + 1, cfg.seed);
    }

    public boolean isStartLatched() {
        return startLatched;
    }
}
