package net.minecraft.world.storage;

import java.io.File;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.world.MinecraftException;
import net.minecraft.world.WorldProvider;
import net.minecraft.world.chunk.storage.IChunkLoader;

public interface ISaveHandler
{
    /**
     * Loads and returns the world info
     */
    WorldInfo loadWorldInfo();

    /**
     * Checks the session lock to prevent save collisions
     */
    void checkSessionLock() throws MinecraftException;

    /**
     * Returns the chunk loader with the provided world provider
     */
    IChunkLoader getChunkLoader(WorldProvider p_75763_1_);

    /**
     * Saves the given World Info with the given NBTTagCompound as the Player.
     */
    void saveWorldInfoWithPlayer(WorldInfo p_75755_1_, NBTTagCompound p_75755_2_);

    /**
     * Saves the passed in world info.
     */
    void saveWorldInfo(WorldInfo p_75761_1_);

    /**
     * returns null if no saveHandler is relevent (eg. SMP)
     */
    IPlayerFileData getSaveHandler();

    /**
     * Called to flush all changes to disk, waiting for them to complete.
     */
    void flush();

    /**
     * Gets the File object corresponding to the base directory of this world.
     */
    File getWorldDirectory();

    /**
     * Gets the file location of the given map
     */
    File getMapFileFromName(String p_75758_1_);

    /**
     * Returns the name of the directory where world information is saved.
     */
    String getWorldDirectoryName();
}