package net.minecraft.world;

import net.minecraft.nbt.NBTTagCompound;

public abstract class WorldSavedData
{
    /** The name of the map data nbt */
    public final String mapName;
    /** Whether this MapDataBase needs saving to disk. */
    private boolean dirty;
    private static final String __OBFID = "CL_00000580";

    public WorldSavedData(String p_i2141_1_)
    {
        this.mapName = p_i2141_1_;
    }

    /**
     * reads in data from the NBTTagCompound into this MapDataBase
     */
    public abstract void readFromNBT(NBTTagCompound p_76184_1_);

    /**
     * write data to NBTTagCompound from this MapDataBase, similar to Entities and TileEntities
     */
    public abstract void writeToNBT(NBTTagCompound p_76187_1_);

    /**
     * Marks this MapDataBase dirty, to be saved to disk when the level next saves.
     */
    public void markDirty()
    {
        this.setDirty(true);
    }

    /**
     * Sets the dirty state of this MapDataBase, whether it needs saving to disk.
     */
    public void setDirty(boolean p_76186_1_)
    {
        this.dirty = p_76186_1_;
    }

    /**
     * Whether this MapDataBase needs saving to disk.
     */
    public boolean isDirty()
    {
        return this.dirty;
    }
}