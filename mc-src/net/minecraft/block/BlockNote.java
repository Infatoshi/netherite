package net.minecraft.block;

import net.minecraft.block.material.Material;
import net.minecraft.creativetab.CreativeTabs;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.tileentity.TileEntity;
import net.minecraft.tileentity.TileEntityNote;
import net.minecraft.world.World;

public class BlockNote extends BlockContainer
{
    private static final String __OBFID = "CL_00000278";

    public BlockNote()
    {
        super(Material.wood);
        this.setCreativeTab(CreativeTabs.tabRedstone);
    }

    /**
     * Lets the block know when one of its neighbor changes. Doesn't know which neighbor changed (coordinates passed are
     * their own) Args: x, y, z, neighbor Block
     */
    public void onNeighborBlockChange(World p_149695_1_, int p_149695_2_, int p_149695_3_, int p_149695_4_, Block p_149695_5_)
    {
        boolean flag = p_149695_1_.isBlockIndirectlyGettingPowered(p_149695_2_, p_149695_3_, p_149695_4_);
        TileEntityNote tileentitynote = (TileEntityNote)p_149695_1_.getTileEntity(p_149695_2_, p_149695_3_, p_149695_4_);

        if (tileentitynote != null && tileentitynote.previousRedstoneState != flag)
        {
            if (flag)
            {
                tileentitynote.triggerNote(p_149695_1_, p_149695_2_, p_149695_3_, p_149695_4_);
            }

            tileentitynote.previousRedstoneState = flag;
        }
    }

    /**
     * Called upon block activation (right click on the block.)
     */
    public boolean onBlockActivated(World p_149727_1_, int p_149727_2_, int p_149727_3_, int p_149727_4_, EntityPlayer p_149727_5_, int p_149727_6_, float p_149727_7_, float p_149727_8_, float p_149727_9_)
    {
        if (p_149727_1_.isRemote)
        {
            return true;
        }
        else
        {
            TileEntityNote tileentitynote = (TileEntityNote)p_149727_1_.getTileEntity(p_149727_2_, p_149727_3_, p_149727_4_);

            if (tileentitynote != null)
            {
                int old = tileentitynote.note;
                tileentitynote.changePitch();
                if (old == tileentitynote.note) return false;
                tileentitynote.triggerNote(p_149727_1_, p_149727_2_, p_149727_3_, p_149727_4_);
            }

            return true;
        }
    }

    /**
     * Called when a player hits the block. Args: world, x, y, z, player
     */
    public void onBlockClicked(World p_149699_1_, int p_149699_2_, int p_149699_3_, int p_149699_4_, EntityPlayer p_149699_5_)
    {
        if (!p_149699_1_.isRemote)
        {
            TileEntityNote tileentitynote = (TileEntityNote)p_149699_1_.getTileEntity(p_149699_2_, p_149699_3_, p_149699_4_);

            if (tileentitynote != null)
            {
                tileentitynote.triggerNote(p_149699_1_, p_149699_2_, p_149699_3_, p_149699_4_);
            }
        }
    }

    /**
     * Returns a new instance of a block's tile entity class. Called on placing the block.
     */
    public TileEntity createNewTileEntity(World p_149915_1_, int p_149915_2_)
    {
        return new TileEntityNote();
    }

    public boolean onBlockEventReceived(World p_149696_1_, int p_149696_2_, int p_149696_3_, int p_149696_4_, int p_149696_5_, int p_149696_6_)
    {
        int meta = p_149696_1_.getBlockMetadata(p_149696_2_, p_149696_3_, p_149696_4_);
        net.minecraftforge.event.world.NoteBlockEvent.Play e = new net.minecraftforge.event.world.NoteBlockEvent.Play(p_149696_1_, p_149696_2_, p_149696_3_, p_149696_4_, meta, p_149696_6_, p_149696_5_);
        if (net.minecraftforge.common.MinecraftForge.EVENT_BUS.post(e)) return false;
        p_149696_5_ = e.instrument.ordinal();
        p_149696_6_ = e.getVanillaNoteId(); 
        float f = (float)Math.pow(2.0D, (double)(p_149696_6_ - 12) / 12.0D);
        String s = "harp";

        if (p_149696_5_ == 1)
        {
            s = "bd";
        }

        if (p_149696_5_ == 2)
        {
            s = "snare";
        }

        if (p_149696_5_ == 3)
        {
            s = "hat";
        }

        if (p_149696_5_ == 4)
        {
            s = "bassattack";
        }

        p_149696_1_.playSoundEffect((double)p_149696_2_ + 0.5D, (double)p_149696_3_ + 0.5D, (double)p_149696_4_ + 0.5D, "note." + s, 3.0F, f);
        p_149696_1_.spawnParticle("note", (double)p_149696_2_ + 0.5D, (double)p_149696_3_ + 1.2D, (double)p_149696_4_ + 0.5D, (double)p_149696_6_ / 24.0D, 0.0D, 0.0D);
        return true;
    }
}