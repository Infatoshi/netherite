package net.minecraft.world.storage;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.world.GameRules;
import net.minecraft.world.WorldSettings;
import net.minecraft.world.WorldType;

public class DerivedWorldInfo extends WorldInfo
{
    /** Instance of WorldInfo. */
    private final WorldInfo theWorldInfo;
    private static final String __OBFID = "CL_00000584";

    public DerivedWorldInfo(WorldInfo p_i2145_1_)
    {
        this.theWorldInfo = p_i2145_1_;
    }

    /**
     * Gets the NBTTagCompound for the worldInfo
     */
    public NBTTagCompound getNBTTagCompound()
    {
        return this.theWorldInfo.getNBTTagCompound();
    }

    /**
     * Creates a new NBTTagCompound for the world, with the given NBTTag as the "Player"
     */
    public NBTTagCompound cloneNBTCompound(NBTTagCompound p_76082_1_)
    {
        return this.theWorldInfo.cloneNBTCompound(p_76082_1_);
    }

    /**
     * Returns the seed of current world.
     */
    public long getSeed()
    {
        return this.theWorldInfo.getSeed();
    }

    /**
     * Returns the x spawn position
     */
    public int getSpawnX()
    {
        return this.theWorldInfo.getSpawnX();
    }

    /**
     * Return the Y axis spawning point of the player.
     */
    public int getSpawnY()
    {
        return this.theWorldInfo.getSpawnY();
    }

    /**
     * Returns the z spawn position
     */
    public int getSpawnZ()
    {
        return this.theWorldInfo.getSpawnZ();
    }

    public long getWorldTotalTime()
    {
        return this.theWorldInfo.getWorldTotalTime();
    }

    /**
     * Get current world time
     */
    public long getWorldTime()
    {
        return this.theWorldInfo.getWorldTime();
    }

    @SideOnly(Side.CLIENT)
    public long getSizeOnDisk()
    {
        return this.theWorldInfo.getSizeOnDisk();
    }

    /**
     * Returns the player's NBTTagCompound to be loaded
     */
    public NBTTagCompound getPlayerNBTTagCompound()
    {
        return this.theWorldInfo.getPlayerNBTTagCompound();
    }

    /**
     * Returns vanilla MC dimension (-1,0,1). For custom dimension compatibility, always prefer
     * WorldProvider.dimensionID accessed from World.provider.dimensionID
     */
    public int getVanillaDimension()
    {
        return this.theWorldInfo.getVanillaDimension();
    }

    /**
     * Get current world name
     */
    public String getWorldName()
    {
        return this.theWorldInfo.getWorldName();
    }

    /**
     * Returns the save version of this world
     */
    public int getSaveVersion()
    {
        return this.theWorldInfo.getSaveVersion();
    }

    /**
     * Return the last time the player was in this world.
     */
    @SideOnly(Side.CLIENT)
    public long getLastTimePlayed()
    {
        return this.theWorldInfo.getLastTimePlayed();
    }

    /**
     * Returns true if it is thundering, false otherwise.
     */
    public boolean isThundering()
    {
        return this.theWorldInfo.isThundering();
    }

    /**
     * Returns the number of ticks until next thunderbolt.
     */
    public int getThunderTime()
    {
        return this.theWorldInfo.getThunderTime();
    }

    /**
     * Returns true if it is raining, false otherwise.
     */
    public boolean isRaining()
    {
        return this.theWorldInfo.isRaining();
    }

    /**
     * Return the number of ticks until rain.
     */
    public int getRainTime()
    {
        return this.theWorldInfo.getRainTime();
    }

    /**
     * Gets the GameType.
     */
    public WorldSettings.GameType getGameType()
    {
        return this.theWorldInfo.getGameType();
    }

    /**
     * Set the x spawn position to the passed in value
     */
    @SideOnly(Side.CLIENT)
    public void setSpawnX(int p_76058_1_) {}

    /**
     * Sets the y spawn position
     */
    @SideOnly(Side.CLIENT)
    public void setSpawnY(int p_76056_1_) {}

    public void incrementTotalWorldTime(long p_82572_1_) {}

    /**
     * Set the z spawn position to the passed in value
     */
    @SideOnly(Side.CLIENT)
    public void setSpawnZ(int p_76087_1_) {}

    /**
     * Set current world time
     */
    public void setWorldTime(long p_76068_1_) {}

    /**
     * Sets the spawn zone position. Args: x, y, z
     */
    public void setSpawnPosition(int p_76081_1_, int p_76081_2_, int p_76081_3_) {}

    public void setWorldName(String p_76062_1_) {}

    /**
     * Sets the save version of the world
     */
    public void setSaveVersion(int p_76078_1_) {}

    /**
     * Sets whether it is thundering or not.
     */
    public void setThundering(boolean p_76069_1_) {}

    /**
     * Defines the number of ticks until next thunderbolt.
     */
    public void setThunderTime(int p_76090_1_) {}

    /**
     * Sets whether it is raining or not.
     */
    public void setRaining(boolean p_76084_1_) {}

    /**
     * Sets the number of ticks until rain.
     */
    public void setRainTime(int p_76080_1_) {}

    /**
     * Get whether the map features (e.g. strongholds) generation is enabled or disabled.
     */
    public boolean isMapFeaturesEnabled()
    {
        return this.theWorldInfo.isMapFeaturesEnabled();
    }

    /**
     * Returns true if hardcore mode is enabled, otherwise false
     */
    public boolean isHardcoreModeEnabled()
    {
        return this.theWorldInfo.isHardcoreModeEnabled();
    }

    public WorldType getTerrainType()
    {
        return this.theWorldInfo.getTerrainType();
    }

    public void setTerrainType(WorldType p_76085_1_) {}

    /**
     * Returns true if commands are allowed on this World.
     */
    public boolean areCommandsAllowed()
    {
        return this.theWorldInfo.areCommandsAllowed();
    }

    /**
     * Returns true if the World is initialized.
     */
    public boolean isInitialized()
    {
        return this.theWorldInfo.isInitialized();
    }

    /**
     * Sets the initialization status of the World.
     */
    public void setServerInitialized(boolean p_76091_1_) {}

    /**
     * Gets the GameRules class Instance.
     */
    public GameRules getGameRulesInstance()
    {
        return this.theWorldInfo.getGameRulesInstance();
    }
}