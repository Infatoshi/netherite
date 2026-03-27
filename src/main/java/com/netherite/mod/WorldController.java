package com.netherite.mod;

import net.minecraft.client.MinecraftClient;
import net.minecraft.client.gui.screen.TitleScreen;
import net.minecraft.resource.DataConfiguration;
import net.minecraft.server.integrated.IntegratedServerLoader;
import net.minecraft.world.Difficulty;
import net.minecraft.world.GameMode;
import net.minecraft.world.GameRules;
import net.minecraft.world.gen.GeneratorOptions;
import net.minecraft.world.gen.WorldPresets;
import net.minecraft.world.level.LevelInfo;

/**
 * Auto-creates singleplayer world on title screen.
 * Handles reset via world deletion + recreation.
 */
public class WorldController {
    public static final WorldController INSTANCE = new WorldController();

    private int instanceId;
    private long seed;
    private boolean worldCreated = false;
    private int ticksSinceCreation = 0;

    public void init(int instanceId, long seed) {
        this.instanceId = instanceId;
        this.seed = seed;
        NetheriteMod.LOGGER.info("WorldController: instance={}, seed={}", instanceId, seed);
    }

    public void tick(MinecraftClient mc) {
        if (mc.world != null) {
            ticksSinceCreation++;
            return;
        }

        // On title screen: auto-create world
        if (!worldCreated && mc.currentScreen instanceof TitleScreen) {
            worldCreated = true;
            createWorld(mc);
        }
    }

    private void createWorld(MinecraftClient mc) {
        String worldName = "netherite_" + instanceId;
        NetheriteMod.LOGGER.info("WorldController: creating world '{}' with seed {}", worldName, seed);

        mc.execute(() -> {
            try {
                GameRules gameRules = new GameRules();
                gameRules.get(GameRules.DO_DAYLIGHT_CYCLE).set(false, null);
                gameRules.get(GameRules.DO_WEATHER_CYCLE).set(false, null);
                gameRules.get(GameRules.DO_MOB_SPAWNING).set(false, null);

                LevelInfo levelInfo = new LevelInfo(
                        worldName,
                        GameMode.SURVIVAL,
                        false,
                        Difficulty.NORMAL,
                        false,
                        gameRules,
                        DataConfiguration.SAFE_MODE
                );

                GeneratorOptions generatorOptions = new GeneratorOptions(seed, true, false);

                IntegratedServerLoader loader = mc.createIntegratedServerLoader();
                loader.createAndStart(worldName, levelInfo, generatorOptions,
                        WorldPresets::createDemoOptions);
            } catch (Exception e) {
                NetheriteMod.LOGGER.error("WorldController: failed to create world", e);
                worldCreated = false;
            }
        });
    }

    public void requestReset() {
        worldCreated = false;
        ticksSinceCreation = 0;
    }
}
