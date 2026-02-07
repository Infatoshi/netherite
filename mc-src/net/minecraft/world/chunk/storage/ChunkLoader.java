package net.minecraft.world.chunk.storage;

import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.nbt.NBTTagList;
import net.minecraft.world.biome.WorldChunkManager;
import net.minecraft.world.chunk.NibbleArray;

public class ChunkLoader
{
    private static final String __OBFID = "CL_00000379";

    public static ChunkLoader.AnvilConverterData load(NBTTagCompound p_76691_0_)
    {
        int i = p_76691_0_.getInteger("xPos");
        int j = p_76691_0_.getInteger("zPos");
        ChunkLoader.AnvilConverterData anvilconverterdata = new ChunkLoader.AnvilConverterData(i, j);
        anvilconverterdata.blocks = p_76691_0_.getByteArray("Blocks");
        anvilconverterdata.data = new NibbleArrayReader(p_76691_0_.getByteArray("Data"), 7);
        anvilconverterdata.skyLight = new NibbleArrayReader(p_76691_0_.getByteArray("SkyLight"), 7);
        anvilconverterdata.blockLight = new NibbleArrayReader(p_76691_0_.getByteArray("BlockLight"), 7);
        anvilconverterdata.heightmap = p_76691_0_.getByteArray("HeightMap");
        anvilconverterdata.terrainPopulated = p_76691_0_.getBoolean("TerrainPopulated");
        anvilconverterdata.entities = p_76691_0_.getTagList("Entities", 10);
        anvilconverterdata.field_151564_i = p_76691_0_.getTagList("TileEntities", 10);
        anvilconverterdata.field_151563_j = p_76691_0_.getTagList("TileTicks", 10);

        try
        {
            anvilconverterdata.lastUpdated = p_76691_0_.getLong("LastUpdate");
        }
        catch (ClassCastException classcastexception)
        {
            anvilconverterdata.lastUpdated = (long)p_76691_0_.getInteger("LastUpdate");
        }

        return anvilconverterdata;
    }

    public static void convertToAnvilFormat(ChunkLoader.AnvilConverterData p_76690_0_, NBTTagCompound p_76690_1_, WorldChunkManager p_76690_2_)
    {
        p_76690_1_.setInteger("xPos", p_76690_0_.x);
        p_76690_1_.setInteger("zPos", p_76690_0_.z);
        p_76690_1_.setLong("LastUpdate", p_76690_0_.lastUpdated);
        int[] aint = new int[p_76690_0_.heightmap.length];

        for (int i = 0; i < p_76690_0_.heightmap.length; ++i)
        {
            aint[i] = p_76690_0_.heightmap[i];
        }

        p_76690_1_.setIntArray("HeightMap", aint);
        p_76690_1_.setBoolean("TerrainPopulated", p_76690_0_.terrainPopulated);
        NBTTagList nbttaglist = new NBTTagList();
        int k;

        for (int j = 0; j < 8; ++j)
        {
            boolean flag = true;

            for (k = 0; k < 16 && flag; ++k)
            {
                int l = 0;

                while (l < 16 && flag)
                {
                    int i1 = 0;

                    while (true)
                    {
                        if (i1 < 16)
                        {
                            int j1 = k << 11 | i1 << 7 | l + (j << 4);
                            byte b0 = p_76690_0_.blocks[j1];

                            if (b0 == 0)
                            {
                                ++i1;
                                continue;
                            }

                            flag = false;
                        }

                        ++l;
                        break;
                    }
                }
            }

            if (!flag)
            {
                byte[] abyte1 = new byte[4096];
                NibbleArray nibblearray = new NibbleArray(abyte1.length, 4);
                NibbleArray nibblearray1 = new NibbleArray(abyte1.length, 4);
                NibbleArray nibblearray2 = new NibbleArray(abyte1.length, 4);

                for (int k2 = 0; k2 < 16; ++k2)
                {
                    for (int k1 = 0; k1 < 16; ++k1)
                    {
                        for (int l1 = 0; l1 < 16; ++l1)
                        {
                            int i2 = k2 << 11 | l1 << 7 | k1 + (j << 4);
                            byte b1 = p_76690_0_.blocks[i2];
                            abyte1[k1 << 8 | l1 << 4 | k2] = (byte)(b1 & 255);
                            nibblearray.set(k2, k1, l1, p_76690_0_.data.get(k2, k1 + (j << 4), l1));
                            nibblearray1.set(k2, k1, l1, p_76690_0_.skyLight.get(k2, k1 + (j << 4), l1));
                            nibblearray2.set(k2, k1, l1, p_76690_0_.blockLight.get(k2, k1 + (j << 4), l1));
                        }
                    }
                }

                NBTTagCompound nbttagcompound1 = new NBTTagCompound();
                nbttagcompound1.setByte("Y", (byte)(j & 255));
                nbttagcompound1.setByteArray("Blocks", abyte1);
                nbttagcompound1.setByteArray("Data", nibblearray.data);
                nbttagcompound1.setByteArray("SkyLight", nibblearray1.data);
                nbttagcompound1.setByteArray("BlockLight", nibblearray2.data);
                nbttaglist.appendTag(nbttagcompound1);
            }
        }

        p_76690_1_.setTag("Sections", nbttaglist);
        byte[] abyte = new byte[256];

        for (int j2 = 0; j2 < 16; ++j2)
        {
            for (k = 0; k < 16; ++k)
            {
                abyte[k << 4 | j2] = (byte)(p_76690_2_.getBiomeGenAt(p_76690_0_.x << 4 | j2, p_76690_0_.z << 4 | k).biomeID & 255);
            }
        }

        p_76690_1_.setByteArray("Biomes", abyte);
        p_76690_1_.setTag("Entities", p_76690_0_.entities);
        p_76690_1_.setTag("TileEntities", p_76690_0_.field_151564_i);

        if (p_76690_0_.field_151563_j != null)
        {
            p_76690_1_.setTag("TileTicks", p_76690_0_.field_151563_j);
        }
    }

    public static class AnvilConverterData
        {
            public long lastUpdated;
            public boolean terrainPopulated;
            public byte[] heightmap;
            public NibbleArrayReader blockLight;
            public NibbleArrayReader skyLight;
            public NibbleArrayReader data;
            public byte[] blocks;
            public NBTTagList entities;
            public NBTTagList field_151564_i;
            public NBTTagList field_151563_j;
            public final int x;
            public final int z;
            private static final String __OBFID = "CL_00000380";

            public AnvilConverterData(int p_i1999_1_, int p_i1999_2_)
            {
                this.x = p_i1999_1_;
                this.z = p_i1999_2_;
            }
        }
}