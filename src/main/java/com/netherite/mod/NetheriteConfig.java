package com.netherite.mod;

import net.minecraft.world.Difficulty;
import net.minecraft.world.GameMode;

/**
 * Reads all netherite.* system properties into a config object.
 * Python sets these via -D flags when launching the JVM.
 */
public class NetheriteConfig {
    public static final NetheriteConfig INSTANCE = new NetheriteConfig();

    // Instance
    public int instanceId;
    public long seed;

    // Display
    public int width;
    public int height;

    // Game
    public GameMode gameMode;
    public Difficulty difficulty;
    public int renderDistance;
    public int simulationDistance;

    // Game rules
    public boolean doDaylightCycle;
    public boolean doWeatherCycle;
    public boolean doMobSpawning;
    public boolean doFireTick;
    public boolean doMobGriefing;
    public boolean doEntityDrops;
    public boolean doTileDrops;
    public boolean naturalRegeneration;
    public int randomTickSpeed;
    public boolean keepInventory;
    public boolean doInsomnia;
    public boolean doPatrolSpawning;
    public boolean doTraderSpawning;
    public boolean doWardenSpawning;

    // Graphics
    public int maxFps;
    public boolean vsync;
    public String graphics;
    public String particles;
    public String clouds;
    public boolean entityShadows;
    public boolean smoothLighting;
    public int biomeBlend;
    public int guiScale;
    public boolean fullscreen;
    public int fov;

    // RL mode
    public boolean rl;
    public boolean headless;

    // Observation mode: "pixels", "voxels", "both"
    public String obsMode;
    // Voxel grid dimensions (blocks in each direction from player)
    public int voxelForward;
    public int voxelBack;
    public int voxelLeft;
    public int voxelRight;
    public int voxelUp;
    public int voxelDown;
    // Skip rendering entirely (for max throughput testing)
    public boolean skipRender;

    public void load() {
        instanceId = getInt("netherite.instance_id", getInt("netherite.instance", 0));
        seed = getLong("netherite.seed", 12345L);

        width = getInt("netherite.width", 854);
        height = getInt("netherite.height", 480);

        gameMode = parseGameMode(getString("netherite.game_mode", "survival"));
        difficulty = parseDifficulty(getString("netherite.difficulty", "normal"));
        renderDistance = getInt("netherite.render_distance", 8);
        simulationDistance = getInt("netherite.simulation_distance", 5);

        doDaylightCycle = getBool("netherite.do_daylight_cycle", false);
        doWeatherCycle = getBool("netherite.do_weather_cycle", false);
        doMobSpawning = getBool("netherite.do_mob_spawning", false);
        doFireTick = getBool("netherite.do_fire_tick", false);
        doMobGriefing = getBool("netherite.do_mob_griefing", false);
        doEntityDrops = getBool("netherite.do_entity_drops", false);
        doTileDrops = getBool("netherite.do_tile_drops", true);
        naturalRegeneration = getBool("netherite.natural_regeneration", true);
        randomTickSpeed = getInt("netherite.random_tick_speed", 0);
        keepInventory = getBool("netherite.keep_inventory", true);
        doInsomnia = getBool("netherite.do_insomnia", false);
        doPatrolSpawning = getBool("netherite.do_patrol_spawning", false);
        doTraderSpawning = getBool("netherite.do_trader_spawning", false);
        doWardenSpawning = getBool("netherite.do_warden_spawning", false);

        maxFps = getInt("netherite.max_fps", 60);
        vsync = getBool("netherite.vsync", false);
        graphics = getString("netherite.graphics", "fast");
        particles = getString("netherite.particles", "minimal");
        clouds = getString("netherite.clouds", "off");
        entityShadows = getBool("netherite.entity_shadows", false);
        smoothLighting = getBool("netherite.smooth_lighting", false);
        biomeBlend = getInt("netherite.biome_blend", 0);
        guiScale = getInt("netherite.gui_scale", 0);
        fullscreen = getBool("netherite.fullscreen", false);
        fov = getInt("netherite.fov", 70);

        rl = getBool("netherite.rl", false);
        headless = getBool("netherite.headless", false);

        obsMode = getString("netherite.obs_mode", "both");  // "pixels", "voxels", "both"
        voxelForward = getInt("netherite.voxel_forward", 8);
        voxelBack = getInt("netherite.voxel_back", 8);
        voxelLeft = getInt("netherite.voxel_left", 8);
        voxelRight = getInt("netherite.voxel_right", 8);
        voxelUp = getInt("netherite.voxel_up", 6);
        voxelDown = getInt("netherite.voxel_down", 2);
        skipRender = getBool("netherite.skip_render", false);
    }

    public boolean needsPixels() {
        return "pixels".equalsIgnoreCase(obsMode) || "both".equalsIgnoreCase(obsMode);
    }

    public boolean needsVoxels() {
        return "voxels".equalsIgnoreCase(obsMode) || "both".equalsIgnoreCase(obsMode);
    }

    private static int getInt(String key, int def) {
        String v = System.getProperty(key);
        if (v == null) return def;
        try { return Integer.parseInt(v); } catch (NumberFormatException e) { return def; }
    }

    private static long getLong(String key, long def) {
        String v = System.getProperty(key);
        if (v == null) return def;
        try { return Long.parseLong(v); } catch (NumberFormatException e) { return def; }
    }

    private static boolean getBool(String key, boolean def) {
        String v = System.getProperty(key);
        if (v == null) return def;
        return "true".equalsIgnoreCase(v);
    }

    private static String getString(String key, String def) {
        return System.getProperty(key, def);
    }

    private static GameMode parseGameMode(String s) {
        return switch (s.toLowerCase()) {
            case "creative" -> GameMode.CREATIVE;
            case "adventure" -> GameMode.ADVENTURE;
            case "spectator" -> GameMode.SPECTATOR;
            default -> GameMode.SURVIVAL;
        };
    }

    private static Difficulty parseDifficulty(String s) {
        return switch (s.toLowerCase()) {
            case "peaceful" -> Difficulty.PEACEFUL;
            case "easy" -> Difficulty.EASY;
            case "hard" -> Difficulty.HARD;
            default -> Difficulty.NORMAL;
        };
    }
}
