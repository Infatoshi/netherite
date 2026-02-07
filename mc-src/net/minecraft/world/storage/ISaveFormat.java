package net.minecraft.world.storage;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.List;
import net.minecraft.client.AnvilConverterException;
import net.minecraft.util.IProgressUpdate;

public interface ISaveFormat
{
    @SideOnly(Side.CLIENT)
    String func_154333_a();

    /**
     * Returns back a loader for the specified save directory
     */
    ISaveHandler getSaveLoader(String p_75804_1_, boolean p_75804_2_);

    @SideOnly(Side.CLIENT)
    List getSaveList() throws AnvilConverterException;

    void flushCache();

    /**
     * gets the world info
     */
    @SideOnly(Side.CLIENT)
    WorldInfo getWorldInfo(String p_75803_1_);

    @SideOnly(Side.CLIENT)
    boolean func_154335_d(String p_154335_1_);

    /**
     * @args: Takes one argument - the name of the directory of the world to delete. @desc: Delete the world by deleting
     * the associated directory recursively.
     */
    boolean deleteWorldDirectory(String p_75802_1_);

    /**
     * @args: Takes two arguments - first the name of the directory containing the world and second the new name for
     * that world. @desc: Renames the world by storing the new name in level.dat. It does *not* rename the directory
     * containing the world data.
     */
    @SideOnly(Side.CLIENT)
    void renameWorld(String p_75806_1_, String p_75806_2_);

    @SideOnly(Side.CLIENT)
    boolean func_154334_a(String p_154334_1_);

    /**
     * Checks if the save directory uses the old map format
     */
    boolean isOldMapFormat(String p_75801_1_);

    /**
     * Converts the specified map to the new map format. Args: worldName, loadingScreen
     */
    boolean convertMapFormat(String p_75805_1_, IProgressUpdate p_75805_2_);

    /**
     * Return whether the given world can be loaded.
     */
    @SideOnly(Side.CLIENT)
    boolean canLoadWorld(String p_90033_1_);
}