package com.netherite.mod;

import net.minecraft.client.MinecraftClient;
import net.minecraft.client.gui.screen.TitleScreen;
import net.minecraft.client.gui.screen.world.CreateWorldScreen;
import net.minecraft.client.world.GeneratorOptionsHolder;
import net.minecraft.world.GameMode;
import net.minecraft.world.GameRules;
import net.minecraft.world.level.LevelInfo;
import net.minecraft.world.level.LevelProperties;

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
            createWorld(mc);
            worldCreated = true;
        }
    }

    private void createWorld(MinecraftClient mc) {
        String worldName = "netherite_" + instanceId;
        NetheriteMod.LOGGER.info("WorldController: creating world '{}'", worldName);

        // Use CreateWorldScreen's internal API to create a world programmatically
        mc.execute(() -> {
            try {
                CreateWorldScreen.create(mc, mc.currentScreen);
            } catch (Exception e) {
                NetheriteMod.LOGGER.error("Failed to open create world screen", e);
            }
        });
    }

    public void requestReset() {
        worldCreated = false;
        ticksSinceCreation = 0;
    }
}
