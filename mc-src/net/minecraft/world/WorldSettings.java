package net.minecraft.world;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.entity.player.PlayerCapabilities;
import net.minecraft.world.storage.WorldInfo;

public final class WorldSettings
{
    /** The seed for the map. */
    private final long seed;
    /** The EnumGameType. */
    private final WorldSettings.GameType theGameType;
    /** Switch for the map features. 'true' for enabled, 'false' for disabled. */
    private final boolean mapFeaturesEnabled;
    /** True if hardcore mode is enabled */
    private final boolean hardcoreEnabled;
    private final WorldType terrainType;
    /** True if Commands (cheats) are allowed. */
    private boolean commandsAllowed;
    /** True if the Bonus Chest is enabled. */
    private boolean bonusChestEnabled;
    private String field_82751_h;
    private static final String __OBFID = "CL_00000147";

    public WorldSettings(long p_i1957_1_, WorldSettings.GameType p_i1957_3_, boolean p_i1957_4_, boolean p_i1957_5_, WorldType p_i1957_6_)
    {
        this.field_82751_h = "";
        this.seed = p_i1957_1_;
        this.theGameType = p_i1957_3_;
        this.mapFeaturesEnabled = p_i1957_4_;
        this.hardcoreEnabled = p_i1957_5_;
        this.terrainType = p_i1957_6_;
    }

    public WorldSettings(WorldInfo p_i1958_1_)
    {
        this(p_i1958_1_.getSeed(), p_i1958_1_.getGameType(), p_i1958_1_.isMapFeaturesEnabled(), p_i1958_1_.isHardcoreModeEnabled(), p_i1958_1_.getTerrainType());
    }

    /**
     * Enables the bonus chest.
     */
    public WorldSettings enableBonusChest()
    {
        this.bonusChestEnabled = true;
        return this;
    }

    public WorldSettings func_82750_a(String p_82750_1_)
    {
        this.field_82751_h = p_82750_1_;
        return this;
    }

    /**
     * Enables Commands (cheats).
     */
    @SideOnly(Side.CLIENT)
    public WorldSettings enableCommands()
    {
        this.commandsAllowed = true;
        return this;
    }

    /**
     * Returns true if the Bonus Chest is enabled.
     */
    public boolean isBonusChestEnabled()
    {
        return this.bonusChestEnabled;
    }

    /**
     * Returns the seed for the world.
     */
    public long getSeed()
    {
        return this.seed;
    }

    /**
     * Gets the game type.
     */
    public WorldSettings.GameType getGameType()
    {
        return this.theGameType;
    }

    /**
     * Returns true if hardcore mode is enabled, otherwise false
     */
    public boolean getHardcoreEnabled()
    {
        return this.hardcoreEnabled;
    }

    /**
     * Get whether the map features (e.g. strongholds) generation is enabled or disabled.
     */
    public boolean isMapFeaturesEnabled()
    {
        return this.mapFeaturesEnabled;
    }

    public WorldType getTerrainType()
    {
        return this.terrainType;
    }

    /**
     * Returns true if Commands (cheats) are allowed.
     */
    public boolean areCommandsAllowed()
    {
        return this.commandsAllowed;
    }

    /**
     * Gets the GameType by ID
     */
    public static WorldSettings.GameType getGameTypeById(int p_77161_0_)
    {
        return WorldSettings.GameType.getByID(p_77161_0_);
    }

    public String func_82749_j()
    {
        return this.field_82751_h;
    }

    public static enum GameType
    {
        NOT_SET(-1, ""),
        SURVIVAL(0, "survival"),
        CREATIVE(1, "creative"),
        ADVENTURE(2, "adventure");
        int id;
        String name;

        private static final String __OBFID = "CL_00000148";

        private GameType(int p_i1956_3_, String p_i1956_4_)
        {
            this.id = p_i1956_3_;
            this.name = p_i1956_4_;
        }

        /**
         * Returns the ID of this game type
         */
        public int getID()
        {
            return this.id;
        }

        /**
         * Returns the name of this game type
         */
        public String getName()
        {
            return this.name;
        }

        /**
         * Configures the player capabilities based on the game type
         */
        public void configurePlayerCapabilities(PlayerCapabilities p_77147_1_)
        {
            if (this == CREATIVE)
            {
                p_77147_1_.allowFlying = true;
                p_77147_1_.isCreativeMode = true;
                p_77147_1_.disableDamage = true;
            }
            else
            {
                p_77147_1_.allowFlying = false;
                p_77147_1_.isCreativeMode = false;
                p_77147_1_.disableDamage = false;
                p_77147_1_.isFlying = false;
            }

            p_77147_1_.allowEdit = !this.isAdventure();
        }

        /**
         * Returns true if this is the ADVENTURE game type
         */
        public boolean isAdventure()
        {
            return this == ADVENTURE;
        }

        /**
         * Returns true if this is the CREATIVE game type
         */
        public boolean isCreative()
        {
            return this == CREATIVE;
        }

        /**
         * Returns true if this is the SURVIVAL or ADVENTURE game type
         */
        @SideOnly(Side.CLIENT)
        public boolean isSurvivalOrAdventure()
        {
            return this == SURVIVAL || this == ADVENTURE;
        }

        /**
         * Returns the game type with the specified ID, or SURVIVAL if none found. Args: id
         */
        public static WorldSettings.GameType getByID(int p_77146_0_)
        {
            WorldSettings.GameType[] agametype = values();
            int j = agametype.length;

            for (int k = 0; k < j; ++k)
            {
                WorldSettings.GameType gametype = agametype[k];

                if (gametype.id == p_77146_0_)
                {
                    return gametype;
                }
            }

            return SURVIVAL;
        }

        /**
         * Returns the game type with the specified name, or SURVIVAL if none found. This is case sensitive. Args: name
         */
        @SideOnly(Side.CLIENT)
        public static WorldSettings.GameType getByName(String p_77142_0_)
        {
            WorldSettings.GameType[] agametype = values();
            int i = agametype.length;

            for (int j = 0; j < i; ++j)
            {
                WorldSettings.GameType gametype = agametype[j];

                if (gametype.name.equals(p_77142_0_))
                {
                    return gametype;
                }
            }

            return SURVIVAL;
        }
    }
}