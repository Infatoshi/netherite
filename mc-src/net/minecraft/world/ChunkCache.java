package net.minecraft.world;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.block.Block;
import net.minecraft.block.material.Material;
import net.minecraft.init.Blocks;
import net.minecraft.tileentity.TileEntity;
import net.minecraft.world.biome.BiomeGenBase;
import net.minecraft.world.chunk.Chunk;
import net.minecraftforge.common.util.ForgeDirection;

public class ChunkCache implements IBlockAccess
{
    private int chunkX;
    private int chunkZ;
    private Chunk[][] chunkArray;
    /** True if the chunk cache is empty. */
    private boolean isEmpty;
    /** Reference to the World object. */
    private World worldObj;
    private static final String __OBFID = "CL_00000155";

    public ChunkCache(World p_i1964_1_, int p_i1964_2_, int p_i1964_3_, int p_i1964_4_, int p_i1964_5_, int p_i1964_6_, int p_i1964_7_, int p_i1964_8_)
    {
        this.worldObj = p_i1964_1_;
        this.chunkX = p_i1964_2_ - p_i1964_8_ >> 4;
        this.chunkZ = p_i1964_4_ - p_i1964_8_ >> 4;
        int l1 = p_i1964_5_ + p_i1964_8_ >> 4;
        int i2 = p_i1964_7_ + p_i1964_8_ >> 4;
        this.chunkArray = new Chunk[l1 - this.chunkX + 1][i2 - this.chunkZ + 1];
        this.isEmpty = true;
        int j2;
        int k2;
        Chunk chunk;

        for (j2 = this.chunkX; j2 <= l1; ++j2)
        {
            for (k2 = this.chunkZ; k2 <= i2; ++k2)
            {
                chunk = p_i1964_1_.getChunkFromChunkCoords(j2, k2);

                if (chunk != null)
                {
                    this.chunkArray[j2 - this.chunkX][k2 - this.chunkZ] = chunk;
                }
            }
        }

        for (j2 = p_i1964_2_ >> 4; j2 <= p_i1964_5_ >> 4; ++j2)
        {
            for (k2 = p_i1964_4_ >> 4; k2 <= p_i1964_7_ >> 4; ++k2)
            {
                chunk = this.chunkArray[j2 - this.chunkX][k2 - this.chunkZ];

                if (chunk != null && !chunk.getAreLevelsEmpty(p_i1964_3_, p_i1964_6_))
                {
                    this.isEmpty = false;
                }
            }
        }
    }

    /**
     * set by !chunk.getAreLevelsEmpty
     */
    @SideOnly(Side.CLIENT)
    public boolean extendedLevelsInChunkCache()
    {
        return this.isEmpty;
    }

    public Block getBlock(int p_147439_1_, int p_147439_2_, int p_147439_3_)
    {
        Block block = Blocks.air;

        if (p_147439_2_ >= 0 && p_147439_2_ < 256)
        {
            int l = (p_147439_1_ >> 4) - this.chunkX;
            int i1 = (p_147439_3_ >> 4) - this.chunkZ;

            if (l >= 0 && l < this.chunkArray.length && i1 >= 0 && i1 < this.chunkArray[l].length)
            {
                Chunk chunk = this.chunkArray[l][i1];

                if (chunk != null)
                {
                    block = chunk.getBlock(p_147439_1_ & 15, p_147439_2_, p_147439_3_ & 15);
                }
            }
        }

        return block;
    }

    public TileEntity getTileEntity(int p_147438_1_, int p_147438_2_, int p_147438_3_)
    {
        int l = (p_147438_1_ >> 4) - this.chunkX;
        int i1 = (p_147438_3_ >> 4) - this.chunkZ;
        if (l < 0 || l >= chunkArray.length || i1 < 0 || i1 >= chunkArray[l].length) return null;
        if (chunkArray[l][i1] == null) return null;
        return this.chunkArray[l][i1].func_150806_e(p_147438_1_ & 15, p_147438_2_, p_147438_3_ & 15);
    }

    /**
     * Any Light rendered on a 1.8 Block goes through here
     */
    @SideOnly(Side.CLIENT)
    public int getLightBrightnessForSkyBlocks(int p_72802_1_, int p_72802_2_, int p_72802_3_, int p_72802_4_)
    {
        int i1 = this.getSkyBlockTypeBrightness(EnumSkyBlock.Sky, p_72802_1_, p_72802_2_, p_72802_3_);
        int j1 = this.getSkyBlockTypeBrightness(EnumSkyBlock.Block, p_72802_1_, p_72802_2_, p_72802_3_);

        if (j1 < p_72802_4_)
        {
            j1 = p_72802_4_;
        }

        return i1 << 20 | j1 << 4;
    }

    /**
     * Returns the block metadata at coords x,y,z
     */
    public int getBlockMetadata(int p_72805_1_, int p_72805_2_, int p_72805_3_)
    {
        if (p_72805_2_ < 0)
        {
            return 0;
        }
        else if (p_72805_2_ >= 256)
        {
            return 0;
        }
        else
        {
            int l = (p_72805_1_ >> 4) - this.chunkX;
            int i1 = (p_72805_3_ >> 4) - this.chunkZ;
            if (l < 0 || l >= chunkArray.length || i1 < 0 || i1 >= chunkArray[l].length) return 0;
            if (chunkArray[l][i1] == null) return 0;
            return this.chunkArray[l][i1].getBlockMetadata(p_72805_1_ & 15, p_72805_2_, p_72805_3_ & 15);
        }
    }

    /**
     * Is this block powering in the specified direction Args: x, y, z, direction
     */
    public int isBlockProvidingPowerTo(int p_72879_1_, int p_72879_2_, int p_72879_3_, int p_72879_4_)
    {
        return this.getBlock(p_72879_1_, p_72879_2_, p_72879_3_).isProvidingStrongPower(this, p_72879_1_, p_72879_2_, p_72879_3_, p_72879_4_);
    }

    /**
     * Gets the biome for a given set of x/z coordinates
     */
    @SideOnly(Side.CLIENT)
    public BiomeGenBase getBiomeGenForCoords(int p_72807_1_, int p_72807_2_)
    {
        return this.worldObj.getBiomeGenForCoords(p_72807_1_, p_72807_2_);
    }

    /**
     * Returns true if the block at the specified coordinates is empty
     */
    public boolean isAirBlock(int p_147437_1_, int p_147437_2_, int p_147437_3_)
    {
        return this.getBlock(p_147437_1_, p_147437_2_, p_147437_3_).isAir(this, p_147437_1_, p_147437_2_, p_147437_3_);
    }

    /**
     * Brightness for SkyBlock.Sky is clear white and (through color computing it is assumed) DEPENDENT ON DAYTIME.
     * Brightness for SkyBlock.Block is yellowish and independent.
     */
    @SideOnly(Side.CLIENT)
    public int getSkyBlockTypeBrightness(EnumSkyBlock p_72810_1_, int p_72810_2_, int p_72810_3_, int p_72810_4_)
    {
        if (p_72810_3_ < 0)
        {
            p_72810_3_ = 0;
        }

        if (p_72810_3_ >= 256)
        {
            p_72810_3_ = 255;
        }

        if (p_72810_3_ >= 0 && p_72810_3_ < 256 && p_72810_2_ >= -30000000 && p_72810_4_ >= -30000000 && p_72810_2_ < 30000000 && p_72810_4_ <= 30000000)
        {
            if (p_72810_1_ == EnumSkyBlock.Sky && this.worldObj.provider.hasNoSky)
            {
                return 0;
            }
            else
            {
                int l;
                int i1;

                if (this.getBlock(p_72810_2_, p_72810_3_, p_72810_4_).getUseNeighborBrightness())
                {
                    l = this.getSpecialBlockBrightness(p_72810_1_, p_72810_2_, p_72810_3_ + 1, p_72810_4_);
                    i1 = this.getSpecialBlockBrightness(p_72810_1_, p_72810_2_ + 1, p_72810_3_, p_72810_4_);
                    int j1 = this.getSpecialBlockBrightness(p_72810_1_, p_72810_2_ - 1, p_72810_3_, p_72810_4_);
                    int k1 = this.getSpecialBlockBrightness(p_72810_1_, p_72810_2_, p_72810_3_, p_72810_4_ + 1);
                    int l1 = this.getSpecialBlockBrightness(p_72810_1_, p_72810_2_, p_72810_3_, p_72810_4_ - 1);

                    if (i1 > l)
                    {
                        l = i1;
                    }

                    if (j1 > l)
                    {
                        l = j1;
                    }

                    if (k1 > l)
                    {
                        l = k1;
                    }

                    if (l1 > l)
                    {
                        l = l1;
                    }

                    return l;
                }
                else
                {
                    l = (p_72810_2_ >> 4) - this.chunkX;
                    i1 = (p_72810_4_ >> 4) - this.chunkZ;
                    return this.chunkArray[l][i1].getSavedLightValue(p_72810_1_, p_72810_2_ & 15, p_72810_3_, p_72810_4_ & 15);
                }
            }
        }
        else
        {
            return p_72810_1_.defaultLightValue;
        }
    }

    /**
     * is only used on stairs and tilled fields
     */
    @SideOnly(Side.CLIENT)
    public int getSpecialBlockBrightness(EnumSkyBlock p_72812_1_, int p_72812_2_, int p_72812_3_, int p_72812_4_)
    {
        if (p_72812_3_ < 0)
        {
            p_72812_3_ = 0;
        }

        if (p_72812_3_ >= 256)
        {
            p_72812_3_ = 255;
        }

        if (p_72812_3_ >= 0 && p_72812_3_ < 256 && p_72812_2_ >= -30000000 && p_72812_4_ >= -30000000 && p_72812_2_ < 30000000 && p_72812_4_ <= 30000000)
        {
            int l = (p_72812_2_ >> 4) - this.chunkX;
            int i1 = (p_72812_4_ >> 4) - this.chunkZ;
            return this.chunkArray[l][i1].getSavedLightValue(p_72812_1_, p_72812_2_ & 15, p_72812_3_, p_72812_4_ & 15);
        }
        else
        {
            return p_72812_1_.defaultLightValue;
        }
    }

    /**
     * Returns current world height.
     */
    @SideOnly(Side.CLIENT)
    public int getHeight()
    {
        return 256;
    }

    @Override
    public boolean isSideSolid(int x, int y, int z, ForgeDirection side, boolean _default)
    {
        if (x < -30000000 || z < -30000000 || x >= 30000000 || z >= 30000000)
        {
            return _default;
        }

        return getBlock(x, y, z).isSideSolid(this, x, y, z, side);
    }
}