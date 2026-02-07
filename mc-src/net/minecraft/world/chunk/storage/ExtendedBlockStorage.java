package net.minecraft.world.chunk.storage;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.block.Block;
import net.minecraft.init.Blocks;
import net.minecraft.world.chunk.NibbleArray;

public class ExtendedBlockStorage
{
    /** Contains the bottom-most Y block represented by this ExtendedBlockStorage. Typically a multiple of 16. */
    private int yBase;
    /** A total count of the number of non-air blocks in this block storage's Chunk. */
    private int blockRefCount;
    /**
     * Contains the number of blocks in this block storage's parent chunk that require random ticking. Used to cull the
     * Chunk from random tick updates for performance reasons.
     */
    private int tickRefCount;
    /** Contains the least significant 8 bits of each block ID belonging to this block storage's parent Chunk. */
    private byte[] blockLSBArray;
    /** Contains the most significant 4 bits of each block ID belonging to this block storage's parent Chunk. */
    private NibbleArray blockMSBArray;
    /** Stores the metadata associated with blocks in this ExtendedBlockStorage. */
    private NibbleArray blockMetadataArray;
    /** The NibbleArray containing a block of Block-light data. */
    private NibbleArray blocklightArray;
    /** The NibbleArray containing a block of Sky-light data. */
    private NibbleArray skylightArray;
    private static final String __OBFID = "CL_00000375";

    public ExtendedBlockStorage(int p_i1997_1_, boolean p_i1997_2_)
    {
        this.yBase = p_i1997_1_;
        this.blockLSBArray = new byte[4096];
        this.blockMetadataArray = new NibbleArray(this.blockLSBArray.length, 4);
        this.blocklightArray = new NibbleArray(this.blockLSBArray.length, 4);

        if (p_i1997_2_)
        {
            this.skylightArray = new NibbleArray(this.blockLSBArray.length, 4);
        }
    }

    /**
     * Returns the block for a location in a chunk, with the extended ID merged from a byte array and a NibbleArray to
     * form a full 12-bit block ID.
     */
    public Block getBlockByExtId(int p_150819_1_, int p_150819_2_, int p_150819_3_)
    {
        int l = this.blockLSBArray[p_150819_2_ << 8 | p_150819_3_ << 4 | p_150819_1_] & 255;

        if (this.blockMSBArray != null)
        {
            l |= this.blockMSBArray.get(p_150819_1_, p_150819_2_, p_150819_3_) << 8;
        }

        return Block.getBlockById(l);
    }

    public void func_150818_a(int p_150818_1_, int p_150818_2_, int p_150818_3_, Block p_150818_4_)
    {
        int l = this.blockLSBArray[p_150818_2_ << 8 | p_150818_3_ << 4 | p_150818_1_] & 255;

        if (this.blockMSBArray != null)
        {
            l |= this.blockMSBArray.get(p_150818_1_, p_150818_2_, p_150818_3_) << 8;
        }

        Block block1 = Block.getBlockById(l);

        if (block1 != Blocks.air)
        {
            --this.blockRefCount;

            if (block1.getTickRandomly())
            {
                --this.tickRefCount;
            }
        }

        if (p_150818_4_ != Blocks.air)
        {
            ++this.blockRefCount;

            if (p_150818_4_.getTickRandomly())
            {
                ++this.tickRefCount;
            }
        }

        int i1 = Block.getIdFromBlock(p_150818_4_);
        this.blockLSBArray[p_150818_2_ << 8 | p_150818_3_ << 4 | p_150818_1_] = (byte)(i1 & 255);

        if (i1 > 255)
        {
            if (this.blockMSBArray == null)
            {
                this.blockMSBArray = new NibbleArray(this.blockLSBArray.length, 4);
            }

            this.blockMSBArray.set(p_150818_1_, p_150818_2_, p_150818_3_, (i1 & 3840) >> 8);
        }
        else if (this.blockMSBArray != null)
        {
            this.blockMSBArray.set(p_150818_1_, p_150818_2_, p_150818_3_, 0);
        }
    }

    /**
     * Returns the metadata associated with the block at the given coordinates in this ExtendedBlockStorage.
     */
    public int getExtBlockMetadata(int p_76665_1_, int p_76665_2_, int p_76665_3_)
    {
        return this.blockMetadataArray.get(p_76665_1_, p_76665_2_, p_76665_3_);
    }

    /**
     * Sets the metadata of the Block at the given coordinates in this ExtendedBlockStorage to the given metadata.
     */
    public void setExtBlockMetadata(int p_76654_1_, int p_76654_2_, int p_76654_3_, int p_76654_4_)
    {
        this.blockMetadataArray.set(p_76654_1_, p_76654_2_, p_76654_3_, p_76654_4_);
    }

    /**
     * Returns whether or not this block storage's Chunk is fully empty, based on its internal reference count.
     */
    public boolean isEmpty()
    {
        return this.blockRefCount == 0;
    }

    /**
     * Returns whether or not this block storage's Chunk will require random ticking, used to avoid looping through
     * random block ticks when there are no blocks that would randomly tick.
     */
    public boolean getNeedsRandomTick()
    {
        return this.tickRefCount > 0;
    }

    /**
     * Returns the Y location of this ExtendedBlockStorage.
     */
    public int getYLocation()
    {
        return this.yBase;
    }

    /**
     * Sets the saved Sky-light value in the extended block storage structure.
     */
    public void setExtSkylightValue(int p_76657_1_, int p_76657_2_, int p_76657_3_, int p_76657_4_)
    {
        this.skylightArray.set(p_76657_1_, p_76657_2_, p_76657_3_, p_76657_4_);
    }

    /**
     * Gets the saved Sky-light value in the extended block storage structure.
     */
    public int getExtSkylightValue(int p_76670_1_, int p_76670_2_, int p_76670_3_)
    {
        return this.skylightArray.get(p_76670_1_, p_76670_2_, p_76670_3_);
    }

    /**
     * Sets the saved Block-light value in the extended block storage structure.
     */
    public void setExtBlocklightValue(int p_76677_1_, int p_76677_2_, int p_76677_3_, int p_76677_4_)
    {
        this.blocklightArray.set(p_76677_1_, p_76677_2_, p_76677_3_, p_76677_4_);
    }

    /**
     * Gets the saved Block-light value in the extended block storage structure.
     */
    public int getExtBlocklightValue(int p_76674_1_, int p_76674_2_, int p_76674_3_)
    {
        return this.blocklightArray.get(p_76674_1_, p_76674_2_, p_76674_3_);
    }

    public void removeInvalidBlocks()
    {
        this.blockRefCount = 0;
        this.tickRefCount = 0;

        for (int i = 0; i < 16; ++i)
        {
            for (int j = 0; j < 16; ++j)
            {
                for (int k = 0; k < 16; ++k)
                {
                    Block block = this.getBlockByExtId(i, j, k);

                    if (block != Blocks.air)
                    {
                        ++this.blockRefCount;

                        if (block.getTickRandomly())
                        {
                            ++this.tickRefCount;
                        }
                    }
                }
            }
        }
    }

    public byte[] getBlockLSBArray()
    {
        return this.blockLSBArray;
    }

    @SideOnly(Side.CLIENT)
    public void clearMSBArray()
    {
        this.blockMSBArray = null;
    }

    /**
     * Returns the block ID MSB (bits 11..8) array for this storage array's Chunk.
     */
    public NibbleArray getBlockMSBArray()
    {
        return this.blockMSBArray;
    }

    public NibbleArray getMetadataArray()
    {
        return this.blockMetadataArray;
    }

    /**
     * Returns the NibbleArray instance containing Block-light data.
     */
    public NibbleArray getBlocklightArray()
    {
        return this.blocklightArray;
    }

    /**
     * Returns the NibbleArray instance containing Sky-light data.
     */
    public NibbleArray getSkylightArray()
    {
        return this.skylightArray;
    }

    /**
     * Sets the array of block ID least significant bits for this ExtendedBlockStorage.
     */
    public void setBlockLSBArray(byte[] p_76664_1_)
    {
        this.blockLSBArray = p_76664_1_;
    }

    /**
     * Sets the array of blockID most significant bits (blockMSBArray) for this ExtendedBlockStorage.
     */
    public void setBlockMSBArray(NibbleArray p_76673_1_)
    {
        this.blockMSBArray = p_76673_1_;
    }

    /**
     * Sets the NibbleArray of block metadata (blockMetadataArray) for this ExtendedBlockStorage.
     */
    public void setBlockMetadataArray(NibbleArray p_76668_1_)
    {
        this.blockMetadataArray = p_76668_1_;
    }

    /**
     * Sets the NibbleArray instance used for Block-light values in this particular storage block.
     */
    public void setBlocklightArray(NibbleArray p_76659_1_)
    {
        this.blocklightArray = p_76659_1_;
    }

    /**
     * Sets the NibbleArray instance used for Sky-light values in this particular storage block.
     */
    public void setSkylightArray(NibbleArray p_76666_1_)
    {
        this.skylightArray = p_76666_1_;
    }

    /**
     * Called by a Chunk to initialize the MSB array if getBlockMSBArray returns null. Returns the newly-created
     * NibbleArray instance.
     */
    @SideOnly(Side.CLIENT)
    public NibbleArray createBlockMSBArray()
    {
        this.blockMSBArray = new NibbleArray(this.blockLSBArray.length, 4);
        return this.blockMSBArray;
    }
}