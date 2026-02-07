package net.minecraft.world.chunk.storage;

import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.File;
import java.io.IOException;
import java.util.HashMap;
import java.util.Iterator;
import java.util.Map;

public class RegionFileCache
{
    /** A map containing Files as keys and RegionFiles as values */
    private static final Map regionsByFilename = new HashMap();
    private static final String __OBFID = "CL_00000383";

    public static synchronized RegionFile createOrLoadRegionFile(File p_76550_0_, int p_76550_1_, int p_76550_2_)
    {
        File file2 = new File(p_76550_0_, "region");
        File file3 = new File(file2, "r." + (p_76550_1_ >> 5) + "." + (p_76550_2_ >> 5) + ".mca");
        RegionFile regionfile = (RegionFile)regionsByFilename.get(file3);

        if (regionfile != null)
        {
            return regionfile;
        }
        else
        {
            if (!file2.exists())
            {
                file2.mkdirs();
            }

            if (regionsByFilename.size() >= 256)
            {
                clearRegionFileReferences();
            }

            RegionFile regionfile1 = new RegionFile(file3);
            regionsByFilename.put(file3, regionfile1);
            return regionfile1;
        }
    }

    /**
     * Saves the current Chunk Map Cache
     */
    public static synchronized void clearRegionFileReferences()
    {
        Iterator iterator = regionsByFilename.values().iterator();

        while (iterator.hasNext())
        {
            RegionFile regionfile = (RegionFile)iterator.next();

            try
            {
                if (regionfile != null)
                {
                    regionfile.close();
                }
            }
            catch (IOException ioexception)
            {
                ioexception.printStackTrace();
            }
        }

        regionsByFilename.clear();
    }

    /**
     * Returns an input stream for the specified chunk. Args: worldDir, chunkX, chunkZ
     */
    public static DataInputStream getChunkInputStream(File p_76549_0_, int p_76549_1_, int p_76549_2_)
    {
        RegionFile regionfile = createOrLoadRegionFile(p_76549_0_, p_76549_1_, p_76549_2_);
        return regionfile.getChunkDataInputStream(p_76549_1_ & 31, p_76549_2_ & 31);
    }

    /**
     * Returns an output stream for the specified chunk. Args: worldDir, chunkX, chunkZ
     */
    public static DataOutputStream getChunkOutputStream(File p_76552_0_, int p_76552_1_, int p_76552_2_)
    {
        RegionFile regionfile = createOrLoadRegionFile(p_76552_0_, p_76552_1_, p_76552_2_);
        return regionfile.getChunkDataOutputStream(p_76552_1_ & 31, p_76552_2_ & 31);
    }
}