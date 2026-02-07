package net.minecraft.world.gen;

import net.minecraft.block.Block;

public class FlatLayerInfo
{
    private Block field_151537_a;
    /** Amount of layers for this set of layers. */
    private int layerCount;
    /** Block metadata used on this set of laeyrs. */
    private int layerFillBlockMeta;
    private int layerMinimumY;
    private static final String __OBFID = "CL_00000441";

    public FlatLayerInfo(int p_i45467_1_, Block p_i45467_2_)
    {
        this.layerCount = 1;
        this.layerCount = p_i45467_1_;
        this.field_151537_a = p_i45467_2_;
    }

    public FlatLayerInfo(int p_i45468_1_, Block p_i45468_2_, int p_i45468_3_)
    {
        this(p_i45468_1_, p_i45468_2_);
        this.layerFillBlockMeta = p_i45468_3_;
    }

    /**
     * Return the amount of layers for this set of layers.
     */
    public int getLayerCount()
    {
        return this.layerCount;
    }

    public Block func_151536_b()
    {
        return this.field_151537_a;
    }

    /**
     * Return the block metadata used on this set of layers.
     */
    public int getFillBlockMeta()
    {
        return this.layerFillBlockMeta;
    }

    /**
     * Return the minimum Y coordinate for this layer, set during generation.
     */
    public int getMinY()
    {
        return this.layerMinimumY;
    }

    /**
     * Set the minimum Y coordinate for this layer.
     */
    public void setMinY(int p_82660_1_)
    {
        this.layerMinimumY = p_82660_1_;
    }

    public String toString()
    {
        String s = Integer.toString(Block.getIdFromBlock(this.field_151537_a));

        if (this.layerCount > 1)
        {
            s = this.layerCount + "x" + s;
        }

        if (this.layerFillBlockMeta > 0)
        {
            s = s + ":" + this.layerFillBlockMeta;
        }

        return s;
    }
}