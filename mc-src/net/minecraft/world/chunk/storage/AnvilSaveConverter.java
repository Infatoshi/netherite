package net.minecraft.world.chunk.storage;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.File;
import java.io.FilenameFilter;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.Iterator;
import java.util.List;
import net.minecraft.client.AnvilConverterException;
import net.minecraft.nbt.CompressedStreamTools;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.util.IProgressUpdate;
import net.minecraft.util.MathHelper;
import net.minecraft.world.WorldType;
import net.minecraft.world.biome.BiomeGenBase;
import net.minecraft.world.biome.WorldChunkManager;
import net.minecraft.world.biome.WorldChunkManagerHell;
import net.minecraft.world.storage.ISaveHandler;
import net.minecraft.world.storage.SaveFormatComparator;
import net.minecraft.world.storage.SaveFormatOld;
import net.minecraft.world.storage.WorldInfo;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

public class AnvilSaveConverter extends SaveFormatOld
{
    private static final Logger logger = LogManager.getLogger();
    private static final String __OBFID = "CL_00000582";

    public AnvilSaveConverter(File p_i2144_1_)
    {
        super(p_i2144_1_);
    }

    @SideOnly(Side.CLIENT)
    public String func_154333_a()
    {
        return "Anvil";
    }

    @SideOnly(Side.CLIENT)
    public List getSaveList() throws AnvilConverterException
    {
        if (this.savesDirectory != null && this.savesDirectory.exists() && this.savesDirectory.isDirectory())
        {
            ArrayList arraylist = new ArrayList();
            File[] afile = this.savesDirectory.listFiles();
            File[] afile1 = afile;
            int i = afile.length;

            for (int j = 0; j < i; ++j)
            {
                File file1 = afile1[j];

                if (file1.isDirectory())
                {
                    String s = file1.getName();
                    WorldInfo worldinfo = this.getWorldInfo(s);

                    if (worldinfo != null && (worldinfo.getSaveVersion() == 19132 || worldinfo.getSaveVersion() == 19133))
                    {
                        boolean flag = worldinfo.getSaveVersion() != this.getSaveVersion();
                        String s1 = worldinfo.getWorldName();

                        if (s1 == null || MathHelper.stringNullOrLengthZero(s1))
                        {
                            s1 = s;
                        }

                        long k = 0L;
                        arraylist.add(new SaveFormatComparator(s, s1, worldinfo.getLastTimePlayed(), k, worldinfo.getGameType(), flag, worldinfo.isHardcoreModeEnabled(), worldinfo.areCommandsAllowed()));
                    }
                }
            }

            return arraylist;
        }
        else
        {
            throw new AnvilConverterException("Unable to read or access folder where game worlds are saved!");
        }
    }

    protected int getSaveVersion()
    {
        return 19133;
    }

    public void flushCache()
    {
        RegionFileCache.clearRegionFileReferences();
    }

    /**
     * Returns back a loader for the specified save directory
     */
    public ISaveHandler getSaveLoader(String p_75804_1_, boolean p_75804_2_)
    {
        return new AnvilSaveHandler(this.savesDirectory, p_75804_1_, p_75804_2_);
    }

    @SideOnly(Side.CLIENT)
    public boolean func_154334_a(String p_154334_1_)
    {
        WorldInfo worldinfo = this.getWorldInfo(p_154334_1_);
        return worldinfo != null && worldinfo.getSaveVersion() == 19132;
    }

    /**
     * Checks if the save directory uses the old map format
     */
    public boolean isOldMapFormat(String p_75801_1_)
    {
        WorldInfo worldinfo = this.getWorldInfo(p_75801_1_);
        return worldinfo != null && worldinfo.getSaveVersion() != this.getSaveVersion();
    }

    /**
     * Converts the specified map to the new map format. Args: worldName, loadingScreen
     */
    public boolean convertMapFormat(String p_75805_1_, IProgressUpdate p_75805_2_)
    {
        p_75805_2_.setLoadingProgress(0);
        ArrayList arraylist = new ArrayList();
        ArrayList arraylist1 = new ArrayList();
        ArrayList arraylist2 = new ArrayList();
        File file1 = new File(this.savesDirectory, p_75805_1_);
        File file2 = new File(file1, "DIM-1");
        File file3 = new File(file1, "DIM1");
        logger.info("Scanning folders...");
        this.addRegionFilesToCollection(file1, arraylist);

        if (file2.exists())
        {
            this.addRegionFilesToCollection(file2, arraylist1);
        }

        if (file3.exists())
        {
            this.addRegionFilesToCollection(file3, arraylist2);
        }

        int i = arraylist.size() + arraylist1.size() + arraylist2.size();
        logger.info("Total conversion count is " + i);
        WorldInfo worldinfo = this.getWorldInfo(p_75805_1_);
        Object object = null;

        if (worldinfo.getTerrainType() == WorldType.FLAT)
        {
            object = new WorldChunkManagerHell(BiomeGenBase.plains, 0.5F);
        }
        else
        {
            object = new WorldChunkManager(worldinfo.getSeed(), worldinfo.getTerrainType());
        }

        this.convertFile(new File(file1, "region"), arraylist, (WorldChunkManager)object, 0, i, p_75805_2_);
        this.convertFile(new File(file2, "region"), arraylist1, new WorldChunkManagerHell(BiomeGenBase.hell, 0.0F), arraylist.size(), i, p_75805_2_);
        this.convertFile(new File(file3, "region"), arraylist2, new WorldChunkManagerHell(BiomeGenBase.sky, 0.0F), arraylist.size() + arraylist1.size(), i, p_75805_2_);
        worldinfo.setSaveVersion(19133);

        if (worldinfo.getTerrainType() == WorldType.DEFAULT_1_1)
        {
            worldinfo.setTerrainType(WorldType.DEFAULT);
        }

        this.createFile(p_75805_1_);
        ISaveHandler isavehandler = this.getSaveLoader(p_75805_1_, false);
        isavehandler.saveWorldInfo(worldinfo);
        return true;
    }

    /**
     * par: filename for the level.dat_mcr backup
     */
    private void createFile(String p_75809_1_)
    {
        File file1 = new File(this.savesDirectory, p_75809_1_);

        if (!file1.exists())
        {
            logger.warn("Unable to create level.dat_mcr backup");
        }
        else
        {
            File file2 = new File(file1, "level.dat");

            if (!file2.exists())
            {
                logger.warn("Unable to create level.dat_mcr backup");
            }
            else
            {
                File file3 = new File(file1, "level.dat_mcr");

                if (!file2.renameTo(file3))
                {
                    logger.warn("Unable to create level.dat_mcr backup");
                }
            }
        }
    }

    private void convertFile(File p_75813_1_, Iterable p_75813_2_, WorldChunkManager p_75813_3_, int p_75813_4_, int p_75813_5_, IProgressUpdate p_75813_6_)
    {
        Iterator iterator = p_75813_2_.iterator();

        while (iterator.hasNext())
        {
            File file2 = (File)iterator.next();
            this.convertChunks(p_75813_1_, file2, p_75813_3_, p_75813_4_, p_75813_5_, p_75813_6_);
            ++p_75813_4_;
            int k = (int)Math.round(100.0D * (double)p_75813_4_ / (double)p_75813_5_);
            p_75813_6_.setLoadingProgress(k);
        }
    }

    /**
     * copies a 32x32 chunk set from par2File to par1File, via AnvilConverterData
     */
    private void convertChunks(File p_75811_1_, File p_75811_2_, WorldChunkManager p_75811_3_, int p_75811_4_, int p_75811_5_, IProgressUpdate p_75811_6_)
    {
        try
        {
            String s = p_75811_2_.getName();
            RegionFile regionfile = new RegionFile(p_75811_2_);
            RegionFile regionfile1 = new RegionFile(new File(p_75811_1_, s.substring(0, s.length() - ".mcr".length()) + ".mca"));

            for (int k = 0; k < 32; ++k)
            {
                int l;

                for (l = 0; l < 32; ++l)
                {
                    if (regionfile.isChunkSaved(k, l) && !regionfile1.isChunkSaved(k, l))
                    {
                        DataInputStream datainputstream = regionfile.getChunkDataInputStream(k, l);

                        if (datainputstream == null)
                        {
                            logger.warn("Failed to fetch input stream");
                        }
                        else
                        {
                            NBTTagCompound nbttagcompound = CompressedStreamTools.read(datainputstream);
                            datainputstream.close();
                            NBTTagCompound nbttagcompound1 = nbttagcompound.getCompoundTag("Level");
                            ChunkLoader.AnvilConverterData anvilconverterdata = ChunkLoader.load(nbttagcompound1);
                            NBTTagCompound nbttagcompound2 = new NBTTagCompound();
                            NBTTagCompound nbttagcompound3 = new NBTTagCompound();
                            nbttagcompound2.setTag("Level", nbttagcompound3);
                            ChunkLoader.convertToAnvilFormat(anvilconverterdata, nbttagcompound3, p_75811_3_);
                            DataOutputStream dataoutputstream = regionfile1.getChunkDataOutputStream(k, l);
                            CompressedStreamTools.write(nbttagcompound2, dataoutputstream);
                            dataoutputstream.close();
                        }
                    }
                }

                l = (int)Math.round(100.0D * (double)(p_75811_4_ * 1024) / (double)(p_75811_5_ * 1024));
                int i1 = (int)Math.round(100.0D * (double)((k + 1) * 32 + p_75811_4_ * 1024) / (double)(p_75811_5_ * 1024));

                if (i1 > l)
                {
                    p_75811_6_.setLoadingProgress(i1);
                }
            }

            regionfile.close();
            regionfile1.close();
        }
        catch (IOException ioexception)
        {
            ioexception.printStackTrace();
        }
    }

    /**
     * filters the files in the par1 directory, and adds them to the par2 collections
     */
    private void addRegionFilesToCollection(File p_75810_1_, Collection p_75810_2_)
    {
        File file2 = new File(p_75810_1_, "region");
        File[] afile = file2.listFiles(new FilenameFilter()
        {
            private static final String __OBFID = "CL_00000583";
            public boolean accept(File p_accept_1_, String p_accept_2_)
            {
                return p_accept_2_.endsWith(".mcr");
            }
        });

        if (afile != null)
        {
            Collections.addAll(p_75810_2_, afile);
        }
    }
}