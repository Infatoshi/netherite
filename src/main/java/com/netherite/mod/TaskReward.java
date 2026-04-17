package com.netherite.mod;

import net.fabricmc.fabric.api.event.player.PlayerBlockBreakEvents;
import net.minecraft.block.BlockState;
import net.minecraft.client.MinecraftClient;
import net.minecraft.registry.tag.BlockTags;
import net.minecraft.util.math.BlockPos;

import java.nio.MappedByteBuffer;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * Adds a reward signal + episode management on top of the base Netherite env.
 *
 * Behaviour:
 *   - Listens on Fabric's PlayerBlockBreakEvents.AFTER to award +1.0 reward
 *     whenever the player breaks any block in BlockTags.LOGS (treechop task).
 *   - On player death: sets done=1, requests respawn, teleports back to the
 *     cached start pose once respawn completes, and starts a new episode.
 *   - On step limit (cfg.maxEpisodeSteps): sets truncated=1, teleports back
 *     to start, starts a new episode. Does NOT tear down the world.
 *
 * Shmem protocol (32 bytes at offset REWARD_OFFSET inside StateExporter's
 * 64 KB state buffer):
 *     u32  reward_magic        = 0x4E455252  ("NERR")
 *     f32  reward_delta        reward accumulated during the current tick
 *     f32  reward_cumulative   reward accumulated during the current episode
 *     u32  done                1 on terminal (death) or truncation tick, else 0
 *     u32  truncated           1 only on step-limit truncation
 *     u32  logs_broken         cumulative log count for the current episode
 *     u32  episode_id          monotonic episode counter (>= 1)
 *     u32  steps_this_episode  ticks since last reset
 *
 * The reward block sits far past any state data written by StateExporter
 * (worst-case entity + world sample totals ~22 KB), so there is no aliasing.
 */
public class TaskReward {
    public static final TaskReward INSTANCE = new TaskReward();

    public static final int REWARD_OFFSET = 32768; // halfway into the 64 KB state shmem
    public static final int REWARD_SIZE = 32;
    public static final int REWARD_MAGIC = 0x4E455252; // "NERR"

    // Reward shaping coefficients.
    private static final float LOG_REWARD = 1.0f;
    private static final float DEATH_PENALTY = 0.0f;
    private static final float TICK_PENALTY = 0.0f;
    // Dense shaping: reward progress toward nearest log block. Each tick we
    // scan a bounded cube around the player and award
    //     SHAPING_COEFF * max(0, prev_min_dist - curr_min_dist)
    // so the agent gets a small signal for navigating toward trees even
    // before it hits a log. One-sided (no penalty for moving away) keeps
    // exploration reasonable.
    private static final int SHAPING_RADIUS = 12;   // blocks
    private static final float SHAPING_COEFF = 0.05f;
    private static final float NO_LOG_DISTANCE = SHAPING_RADIUS + 1f;

    // How long to wait for a requestRespawn() to complete before giving up.
    private static final int RESPAWN_WAIT_TICKS = 80;

    private enum Task { NONE, TREECHOP }

    private Task task = Task.NONE;
    private int maxEpisodeSteps = 1000;

    private int episodeId = 1;
    private int stepsThisEpisode = 0;
    private int logsBroken = 0;
    private float rewardDelta = 0f;
    private float rewardCumulative = 0f;
    private boolean done = false;
    private boolean truncated = false;

    // Death / respawn state machine.
    private boolean awaitingRespawn = false;
    private int awaitingRespawnTicks = 0;
    // After we clear `done`, we teleport on the next tick so the just-respawned
    // client player is fully initialised by the time we refresh its position.
    private boolean pendingEpisodeReset = false;

    // PlayerBlockBreakEvents.AFTER fires on the server thread. We drain this
    // counter from the client tick thread to keep per-episode accumulators
    // single-threaded.
    private final AtomicInteger pendingLogBreaks = new AtomicInteger(0);

    // Previous-tick distance to nearest log (blocks). Float.NaN means "no
    // reading yet", which disables shaping until the next tick.
    private float prevMinLogDist = Float.NaN;

    public void init(NetheriteConfig cfg) {
        this.task = parseTask(cfg.task);
        this.maxEpisodeSteps = Math.max(1, cfg.maxEpisodeSteps);

        if (task == Task.NONE) {
            NetheriteMod.LOGGER.info("TaskReward: disabled (task=none)");
            return;
        }

        NetheriteMod.LOGGER.info("TaskReward: task={} maxEpisodeSteps={}", task, maxEpisodeSteps);

        PlayerBlockBreakEvents.AFTER.register((world, player, pos, state, blockEntity) -> {
            onBlockBroken(state);
        });
    }

    public void tick(MinecraftClient mc) {
        if (task == Task.NONE) {
            return;
        }
        if (mc.player == null || mc.world == null) {
            // Still publish a zero-valued reward block so Python sees the
            // magic as soon as shmem is alive. Keeps the env.reset() contract
            // (magic present immediately) independent of start-latch timing.
            writeRewardBlock();
            return;
        }
        // Let WorldController's startup latch finish before we start counting
        // reward deltas, but keep the block alive so callers always see NERR.
        if (WorldController.INSTANCE.isStartLatched()) {
            writeRewardBlock();
            return;
        }

        // Phase 1: complete any deferred reset from the previous tick.
        if (pendingEpisodeReset) {
            beginNewEpisode();
        }

        // Phase 2: if we are mid-respawn, wait for the client player to come
        // back alive, then teleport and schedule a fresh episode.
        if (awaitingRespawn) {
            awaitingRespawnTicks++;
            boolean alive = mc.player.getHealth() > 0f && !mc.player.isDead();
            if (alive) {
                WorldController.INSTANCE.teleportToStartPose(mc);
                pendingEpisodeReset = true;
                awaitingRespawn = false;
                awaitingRespawnTicks = 0;
            } else if (awaitingRespawnTicks > RESPAWN_WAIT_TICKS) {
                NetheriteMod.LOGGER.warn(
                        "TaskReward: respawn did not complete within {} ticks; retrying",
                        RESPAWN_WAIT_TICKS);
                try {
                    mc.player.requestRespawn();
                } catch (Exception e) {
                    NetheriteMod.LOGGER.warn("TaskReward: requestRespawn retry failed", e);
                }
                awaitingRespawnTicks = 0;
            }
            writeRewardBlock();
            rewardDelta = 0f;
            return;
        }

        // Phase 3: normal tick accounting.
        stepsThisEpisode++;
        if (TICK_PENALTY != 0f) {
            rewardDelta -= TICK_PENALTY;
            rewardCumulative -= TICK_PENALTY;
        }

        // Drain any log-break events from the server thread.
        int newLogs = pendingLogBreaks.getAndSet(0);
        if (newLogs > 0) {
            logsBroken += newLogs;
            float r = LOG_REWARD * newLogs;
            rewardDelta += r;
            rewardCumulative += r;
        }

        // Dense shaping: reward progress toward nearest log.
        if (task == Task.TREECHOP && SHAPING_COEFF != 0f) {
            float curr = nearestLogDistance(mc);
            if (!Float.isNaN(prevMinLogDist)) {
                float progress = prevMinLogDist - curr;
                if (progress > 0f) {
                    float r = SHAPING_COEFF * progress;
                    rewardDelta += r;
                    rewardCumulative += r;
                }
            }
            prevMinLogDist = curr;
        }

        // Phase 4: terminal detection. Death wins over truncation.
        boolean died = mc.player.getHealth() <= 0f || mc.player.isDead();
        if (died) {
            done = true;
            truncated = false;
            if (DEATH_PENALTY != 0f) {
                rewardDelta += DEATH_PENALTY;
                rewardCumulative += DEATH_PENALTY;
            }
            try {
                mc.player.requestRespawn();
            } catch (Exception e) {
                NetheriteMod.LOGGER.warn("TaskReward: requestRespawn failed", e);
            }
            awaitingRespawn = true;
            awaitingRespawnTicks = 0;
            prevMinLogDist = Float.NaN;
            NetheriteMod.LOGGER.info(
                    "TaskReward: episode {} died at step {} (logs_broken={}, reward_cum={})",
                    episodeId, stepsThisEpisode, logsBroken, rewardCumulative);
        } else if (stepsThisEpisode >= maxEpisodeSteps) {
            done = true;
            truncated = true;
            WorldController.INSTANCE.teleportToStartPose(mc);
            pendingEpisodeReset = true;
            NetheriteMod.LOGGER.info(
                    "TaskReward: episode {} truncated at step {} (logs_broken={}, reward_cum={})",
                    episodeId, stepsThisEpisode, logsBroken, rewardCumulative);
        }

        writeRewardBlock();
        rewardDelta = 0f;
    }

    private void onBlockBroken(BlockState state) {
        if (task != Task.TREECHOP) {
            return;
        }
        if (state.isIn(BlockTags.LOGS)) {
            pendingLogBreaks.incrementAndGet();
        }
    }

    private void beginNewEpisode() {
        episodeId++;
        stepsThisEpisode = 0;
        logsBroken = 0;
        rewardCumulative = 0f;
        rewardDelta = 0f;
        done = false;
        truncated = false;
        pendingEpisodeReset = false;
        // Drop any stale log-break events that fired during the respawn window.
        pendingLogBreaks.set(0);
        // Force shaping to skip the first tick of the new episode so a
        // teleport does not register as negative "progress".
        prevMinLogDist = Float.NaN;
    }

    /**
     * Scan a (2*SHAPING_RADIUS+1)^3 cube around the player and return the
     * Euclidean distance to the nearest block in BlockTags.LOGS. Returns
     * NO_LOG_DISTANCE if no log is found within the radius.
     */
    private float nearestLogDistance(MinecraftClient mc) {
        int px = (int) Math.floor(mc.player.getX());
        int py = (int) Math.floor(mc.player.getY());
        int pz = (int) Math.floor(mc.player.getZ());
        int r = SHAPING_RADIUS;
        int bestD2 = Integer.MAX_VALUE;
        BlockPos.Mutable pos = new BlockPos.Mutable();
        for (int dy = -r; dy <= r; dy++) {
            int dy2 = dy * dy;
            if (dy2 >= bestD2) continue;
            for (int dx = -r; dx <= r; dx++) {
                int dxdy2 = dx * dx + dy2;
                if (dxdy2 >= bestD2) continue;
                for (int dz = -r; dz <= r; dz++) {
                    int d2 = dxdy2 + dz * dz;
                    if (d2 >= bestD2) continue;
                    pos.set(px + dx, py + dy, pz + dz);
                    BlockState s = mc.world.getBlockState(pos);
                    if (s.isIn(BlockTags.LOGS)) {
                        bestD2 = d2;
                    }
                }
            }
        }
        if (bestD2 == Integer.MAX_VALUE) {
            return NO_LOG_DISTANCE;
        }
        return (float) Math.sqrt(bestD2);
    }

    private void writeRewardBlock() {
        MappedByteBuffer buf = StateExporter.INSTANCE.getShmemBuffer();
        if (buf == null) {
            return;
        }
        buf.putInt(REWARD_OFFSET + 0, REWARD_MAGIC);
        buf.putFloat(REWARD_OFFSET + 4, rewardDelta);
        buf.putFloat(REWARD_OFFSET + 8, rewardCumulative);
        buf.putInt(REWARD_OFFSET + 12, done ? 1 : 0);
        buf.putInt(REWARD_OFFSET + 16, truncated ? 1 : 0);
        buf.putInt(REWARD_OFFSET + 20, logsBroken);
        buf.putInt(REWARD_OFFSET + 24, episodeId);
        buf.putInt(REWARD_OFFSET + 28, stepsThisEpisode);
    }

    private static Task parseTask(String s) {
        if (s == null) {
            return Task.NONE;
        }
        return switch (s.toLowerCase()) {
            case "treechop" -> Task.TREECHOP;
            default -> Task.NONE;
        };
    }
}
