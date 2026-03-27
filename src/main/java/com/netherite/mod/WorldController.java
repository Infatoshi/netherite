package com.netherite.mod;

import net.minecraft.client.MinecraftClient;
import net.minecraft.client.gui.screen.TitleScreen;
import net.minecraft.client.option.CloudRenderMode;
import net.minecraft.client.option.GraphicsMode;
import net.minecraft.client.option.ParticlesMode;
import net.minecraft.client.toast.ToastManager;
import net.minecraft.resource.DataConfiguration;
import net.minecraft.server.integrated.IntegratedServerLoader;
import net.minecraft.world.GameRules;
import net.minecraft.world.gen.GeneratorOptions;
import net.minecraft.world.gen.WorldPresets;
import net.minecraft.world.level.LevelInfo;

/**
 * Applies all config to MC options, auto-creates world, dismisses menus in RL mode.
 */
public class WorldController {
    public static final WorldController INSTANCE = new WorldController();

    private NetheriteConfig cfg;
    private boolean worldCreated = false;
    private boolean optionsApplied = false;
    private int ticksSinceCreation = 0;

    public void init(NetheriteConfig cfg) {
        this.cfg = cfg;
    }

    public void tick(MinecraftClient mc) {
        // Apply options once MC is initialized
        if (!optionsApplied) {
            applyOptions(mc);
            optionsApplied = true;
        }

        mc.options.pauseOnLostFocus = false;

        if (mc.world != null) {
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

        if (!worldCreated && mc.currentScreen instanceof TitleScreen) {
            worldCreated = true;
            createWorld(mc);
        }
    }

    private void applyOptions(MinecraftClient mc) {
        var opts = mc.options;

        // Display
        opts.getViewDistance().setValue(cfg.renderDistance);
        opts.getSimulationDistance().setValue(cfg.simulationDistance);
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
        String worldName = "netherite_" + cfg.instanceId;
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

    public void requestReset() {
        worldCreated = false;
        ticksSinceCreation = 0;
    }
}
