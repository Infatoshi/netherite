package net.minecraft.oracle;

/**
 * Enumeration of oracle test checkpoints.
 * Each checkpoint spawns the player in a preconfigured scenario for
 * isolated mechanic validation.
 *
 * Usage: -Doracle.checkpoint=<name> (env: ORACLE_CHECKPOINT=<name>)
 */
public enum TestCheckpoint
{
    WATER_BUCKET("water_bucket", "Fluid placement and cobblestone generation", 6000),
    NETHER_PORTAL("nether_portal", "Portal lighting and dimension transfer", 6000),
    NETHER_FORTRESS("nether_fortress", "Blaze spawning, AI, and drops", 12000),
    ENDERMAN_HUNT("enderman_hunt", "Enderman AI, aggro, and pearl drops", 12000),
    STRONGHOLD("stronghold", "Eye placement, portal activation, end entry", 6000),
    DRAGON_FULL("dragon_full", "Full dragon fight with crystals", 24000),
    DRAGON_1HP("dragon_1hp", "Dragon death, portal, egg, credits", 6000),
    FALL_DAMAGE("fall_damage", "Fall damage calc and water bucket save", 6000),
    MOB_SPAWNING("mob_spawning", "Spawn conditions and light level checks", 6000),
    CRAFTING("crafting", "Full crafting chain validation", 6000);

    public final String name;
    public final String description;
    public final int defaultTimeout; // ticks before autotest gives up

    TestCheckpoint(String name, String description, int defaultTimeout)
    {
        this.name = name;
        this.description = description;
        this.defaultTimeout = defaultTimeout;
    }

    /**
     * Look up checkpoint by name string (case-insensitive).
     * Returns null if not found.
     */
    public static TestCheckpoint fromName(String name)
    {
        if (name == null || name.isEmpty()) return null;
        for (TestCheckpoint cp : values())
        {
            if (cp.name.equalsIgnoreCase(name)) return cp;
        }
        return null;
    }
}
