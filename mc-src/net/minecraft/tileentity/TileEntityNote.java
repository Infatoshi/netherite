package net.minecraft.tileentity;

import net.minecraft.block.material.Material;
import net.minecraft.init.Blocks;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.world.World;

public class TileEntityNote extends TileEntity
{
    /** Note to play */
    public byte note;
    /** stores the latest redstone state */
    public boolean previousRedstoneState;
    private static final String __OBFID = "CL_00000362";

    public void writeToNBT(NBTTagCompound p_145841_1_)
    {
        super.writeToNBT(p_145841_1_);
        p_145841_1_.setByte("note", this.note);
    }

    public void readFromNBT(NBTTagCompound p_145839_1_)
    {
        super.readFromNBT(p_145839_1_);
        this.note = p_145839_1_.getByte("note");

        if (this.note < 0)
        {
            this.note = 0;
        }

        if (this.note > 24)
        {
            this.note = 24;
        }
    }

    /**
     * change pitch by -> (currentPitch + 1) % 25
     */
    public void changePitch()
    {
        byte old = note;
        this.note = (byte)((this.note + 1) % 25);
        if (!net.minecraftforge.common.ForgeHooks.onNoteChange(this, old)) return;
        this.markDirty();
    }

    /**
     * plays the stored note
     */
    public void triggerNote(World p_145878_1_, int p_145878_2_, int p_145878_3_, int p_145878_4_)
    {
        if (p_145878_1_.getBlock(p_145878_2_, p_145878_3_ + 1, p_145878_4_).getMaterial() == Material.air)
        {
            Material material = p_145878_1_.getBlock(p_145878_2_, p_145878_3_ - 1, p_145878_4_).getMaterial();
            byte b0 = 0;

            if (material == Material.rock)
            {
                b0 = 1;
            }

            if (material == Material.sand)
            {
                b0 = 2;
            }

            if (material == Material.glass)
            {
                b0 = 3;
            }

            if (material == Material.wood)
            {
                b0 = 4;
            }

            p_145878_1_.addBlockEvent(p_145878_2_, p_145878_3_, p_145878_4_, Blocks.noteblock, b0, this.note);
        }
    }
}