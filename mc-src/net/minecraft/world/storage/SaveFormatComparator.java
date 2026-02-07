package net.minecraft.world.storage;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.world.WorldSettings;

@SideOnly(Side.CLIENT)
public class SaveFormatComparator implements Comparable
{
    /** the file name of this save */
    private final String fileName;
    /** the displayed name of this save file */
    private final String displayName;
    private final long lastTimePlayed;
    private final long sizeOnDisk;
    private final boolean requiresConversion;
    /** Instance of EnumGameType. */
    private final WorldSettings.GameType theEnumGameType;
    private final boolean hardcore;
    private final boolean cheatsEnabled;
    private static final String __OBFID = "CL_00000601";

    public SaveFormatComparator(String p_i2161_1_, String p_i2161_2_, long p_i2161_3_, long p_i2161_5_, WorldSettings.GameType p_i2161_7_, boolean p_i2161_8_, boolean p_i2161_9_, boolean p_i2161_10_)
    {
        this.fileName = p_i2161_1_;
        this.displayName = p_i2161_2_;
        this.lastTimePlayed = p_i2161_3_;
        this.sizeOnDisk = p_i2161_5_;
        this.theEnumGameType = p_i2161_7_;
        this.requiresConversion = p_i2161_8_;
        this.hardcore = p_i2161_9_;
        this.cheatsEnabled = p_i2161_10_;
    }

    /**
     * return the file name
     */
    public String getFileName()
    {
        return this.fileName;
    }

    /**
     * return the display name of the save
     */
    public String getDisplayName()
    {
        return this.displayName;
    }

    public long func_154336_c()
    {
        return this.sizeOnDisk;
    }

    public boolean requiresConversion()
    {
        return this.requiresConversion;
    }

    public long getLastTimePlayed()
    {
        return this.lastTimePlayed;
    }

    public int compareTo(SaveFormatComparator p_compareTo_1_)
    {
        return this.lastTimePlayed < p_compareTo_1_.lastTimePlayed ? 1 : (this.lastTimePlayed > p_compareTo_1_.lastTimePlayed ? -1 : this.fileName.compareTo(p_compareTo_1_.fileName));
    }

    /**
     * Gets the EnumGameType.
     */
    public WorldSettings.GameType getEnumGameType()
    {
        return this.theEnumGameType;
    }

    public boolean isHardcoreModeEnabled()
    {
        return this.hardcore;
    }

    /**
     * @return {@code true} if cheats are enabled for this world
     */
    public boolean getCheatsEnabled()
    {
        return this.cheatsEnabled;
    }

    public int compareTo(Object p_compareTo_1_)
    {
        return this.compareTo((SaveFormatComparator)p_compareTo_1_);
    }
}