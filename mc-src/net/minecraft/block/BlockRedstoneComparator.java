package net.minecraft.block;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.Random;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.init.Blocks;
import net.minecraft.init.Items;
import net.minecraft.item.Item;
import net.minecraft.tileentity.TileEntity;
import net.minecraft.tileentity.TileEntityComparator;
import net.minecraft.util.Direction;
import net.minecraft.util.IIcon;
import net.minecraft.world.IBlockAccess;
import net.minecraft.world.World;

public class BlockRedstoneComparator extends BlockRedstoneDiode implements ITileEntityProvider
{
    private static final String __OBFID = "CL_00000220";

    public BlockRedstoneComparator(boolean p_i45399_1_)
    {
        super(p_i45399_1_);
        this.isBlockContainer = true;
    }

    public Item getItemDropped(int p_149650_1_, Random p_149650_2_, int p_149650_3_)
    {
        return Items.comparator;
    }

    /**
     * Gets an item for the block being called on. Args: world, x, y, z
     */
    @SideOnly(Side.CLIENT)
    public Item getItem(World p_149694_1_, int p_149694_2_, int p_149694_3_, int p_149694_4_)
    {
        return Items.comparator;
    }

    protected int func_149901_b(int p_149901_1_)
    {
        return 2;
    }

    protected BlockRedstoneDiode getBlockPowered()
    {
        return Blocks.powered_comparator;
    }

    protected BlockRedstoneDiode getBlockUnpowered()
    {
        return Blocks.unpowered_comparator;
    }

    /**
     * The type of render function that is called for this block
     */
    public int getRenderType()
    {
        return 37;
    }

    /**
     * Gets the block's texture. Args: side, meta
     */
    @SideOnly(Side.CLIENT)
    public IIcon getIcon(int p_149691_1_, int p_149691_2_)
    {
        boolean flag = this.isRepeaterPowered || (p_149691_2_ & 8) != 0;
        return p_149691_1_ == 0 ? (flag ? Blocks.redstone_torch.getBlockTextureFromSide(p_149691_1_) : Blocks.unlit_redstone_torch.getBlockTextureFromSide(p_149691_1_)) : (p_149691_1_ == 1 ? (flag ? Blocks.powered_comparator.blockIcon : this.blockIcon) : Blocks.double_stone_slab.getBlockTextureFromSide(1));
    }

    protected boolean func_149905_c(int p_149905_1_)
    {
        return this.isRepeaterPowered || (p_149905_1_ & 8) != 0;
    }

    protected int func_149904_f(IBlockAccess p_149904_1_, int p_149904_2_, int p_149904_3_, int p_149904_4_, int p_149904_5_)
    {
        return this.getTileEntityComparator(p_149904_1_, p_149904_2_, p_149904_3_, p_149904_4_).getOutputSignal();
    }

    private int getOutputStrength(World p_149970_1_, int p_149970_2_, int p_149970_3_, int p_149970_4_, int p_149970_5_)
    {
        return !this.func_149969_d(p_149970_5_) ? this.getInputStrength(p_149970_1_, p_149970_2_, p_149970_3_, p_149970_4_, p_149970_5_) : Math.max(this.getInputStrength(p_149970_1_, p_149970_2_, p_149970_3_, p_149970_4_, p_149970_5_) - this.func_149902_h(p_149970_1_, p_149970_2_, p_149970_3_, p_149970_4_, p_149970_5_), 0);
    }

    public boolean func_149969_d(int p_149969_1_)
    {
        return (p_149969_1_ & 4) == 4;
    }

    protected boolean isGettingInput(World p_149900_1_, int p_149900_2_, int p_149900_3_, int p_149900_4_, int p_149900_5_)
    {
        int i1 = this.getInputStrength(p_149900_1_, p_149900_2_, p_149900_3_, p_149900_4_, p_149900_5_);

        if (i1 >= 15)
        {
            return true;
        }
        else if (i1 == 0)
        {
            return false;
        }
        else
        {
            int j1 = this.func_149902_h(p_149900_1_, p_149900_2_, p_149900_3_, p_149900_4_, p_149900_5_);
            return j1 == 0 ? true : i1 >= j1;
        }
    }

    /**
     * Returns the signal strength at one input of the block. Args: world, X, Y, Z, side
     */
    protected int getInputStrength(World p_149903_1_, int p_149903_2_, int p_149903_3_, int p_149903_4_, int p_149903_5_)
    {
        int i1 = super.getInputStrength(p_149903_1_, p_149903_2_, p_149903_3_, p_149903_4_, p_149903_5_);
        int j1 = getDirection(p_149903_5_);
        int k1 = p_149903_2_ + Direction.offsetX[j1];
        int l1 = p_149903_4_ + Direction.offsetZ[j1];
        Block block = p_149903_1_.getBlock(k1, p_149903_3_, l1);

        if (block.hasComparatorInputOverride())
        {
            i1 = block.getComparatorInputOverride(p_149903_1_, k1, p_149903_3_, l1, Direction.rotateOpposite[j1]);
        }
        else if (i1 < 15 && block.isNormalCube())
        {
            k1 += Direction.offsetX[j1];
            l1 += Direction.offsetZ[j1];
            block = p_149903_1_.getBlock(k1, p_149903_3_, l1);

            if (block.hasComparatorInputOverride())
            {
                i1 = block.getComparatorInputOverride(p_149903_1_, k1, p_149903_3_, l1, Direction.rotateOpposite[j1]);
            }
        }

        return i1;
    }

    /**
     * Returns the blockTileEntity at given coordinates.
     */
    public TileEntityComparator getTileEntityComparator(IBlockAccess p_149971_1_, int p_149971_2_, int p_149971_3_, int p_149971_4_)
    {
        return (TileEntityComparator)p_149971_1_.getTileEntity(p_149971_2_, p_149971_3_, p_149971_4_);
    }

    /**
     * Called upon block activation (right click on the block.)
     */
    public boolean onBlockActivated(World p_149727_1_, int p_149727_2_, int p_149727_3_, int p_149727_4_, EntityPlayer p_149727_5_, int p_149727_6_, float p_149727_7_, float p_149727_8_, float p_149727_9_)
    {
        int i1 = p_149727_1_.getBlockMetadata(p_149727_2_, p_149727_3_, p_149727_4_);
        boolean flag = this.isRepeaterPowered | (i1 & 8) != 0;
        boolean flag1 = !this.func_149969_d(i1);
        int j1 = flag1 ? 4 : 0;
        j1 |= flag ? 8 : 0;
        p_149727_1_.playSoundEffect((double)p_149727_2_ + 0.5D, (double)p_149727_3_ + 0.5D, (double)p_149727_4_ + 0.5D, "random.click", 0.3F, flag1 ? 0.55F : 0.5F);
        p_149727_1_.setBlockMetadataWithNotify(p_149727_2_, p_149727_3_, p_149727_4_, j1 | i1 & 3, 2);
        this.func_149972_c(p_149727_1_, p_149727_2_, p_149727_3_, p_149727_4_, p_149727_1_.rand);
        return true;
    }

    protected void func_149897_b(World p_149897_1_, int p_149897_2_, int p_149897_3_, int p_149897_4_, Block p_149897_5_)
    {
        if (!p_149897_1_.isBlockTickScheduledThisTick(p_149897_2_, p_149897_3_, p_149897_4_, this))
        {
            int l = p_149897_1_.getBlockMetadata(p_149897_2_, p_149897_3_, p_149897_4_);
            int i1 = this.getOutputStrength(p_149897_1_, p_149897_2_, p_149897_3_, p_149897_4_, l);
            int j1 = this.getTileEntityComparator(p_149897_1_, p_149897_2_, p_149897_3_, p_149897_4_).getOutputSignal();

            if (i1 != j1 || this.func_149905_c(l) != this.isGettingInput(p_149897_1_, p_149897_2_, p_149897_3_, p_149897_4_, l))
            {
                if (this.func_149912_i(p_149897_1_, p_149897_2_, p_149897_3_, p_149897_4_, l))
                {
                    p_149897_1_.scheduleBlockUpdateWithPriority(p_149897_2_, p_149897_3_, p_149897_4_, this, this.func_149901_b(0), -1);
                }
                else
                {
                    p_149897_1_.scheduleBlockUpdateWithPriority(p_149897_2_, p_149897_3_, p_149897_4_, this, this.func_149901_b(0), 0);
                }
            }
        }
    }

    private void func_149972_c(World p_149972_1_, int p_149972_2_, int p_149972_3_, int p_149972_4_, Random p_149972_5_)
    {
        int l = p_149972_1_.getBlockMetadata(p_149972_2_, p_149972_3_, p_149972_4_);
        int i1 = this.getOutputStrength(p_149972_1_, p_149972_2_, p_149972_3_, p_149972_4_, l);
        int j1 = this.getTileEntityComparator(p_149972_1_, p_149972_2_, p_149972_3_, p_149972_4_).getOutputSignal();
        this.getTileEntityComparator(p_149972_1_, p_149972_2_, p_149972_3_, p_149972_4_).setOutputSignal(i1);

        if (j1 != i1 || !this.func_149969_d(l))
        {
            boolean flag = this.isGettingInput(p_149972_1_, p_149972_2_, p_149972_3_, p_149972_4_, l);
            boolean flag1 = this.isRepeaterPowered || (l & 8) != 0;

            if (flag1 && !flag)
            {
                p_149972_1_.setBlockMetadataWithNotify(p_149972_2_, p_149972_3_, p_149972_4_, l & -9, 2);
            }
            else if (!flag1 && flag)
            {
                p_149972_1_.setBlockMetadataWithNotify(p_149972_2_, p_149972_3_, p_149972_4_, l | 8, 2);
            }

            this.func_149911_e(p_149972_1_, p_149972_2_, p_149972_3_, p_149972_4_);
        }
    }

    /**
     * Ticks the block if it's been scheduled
     */
    public void updateTick(World p_149674_1_, int p_149674_2_, int p_149674_3_, int p_149674_4_, Random p_149674_5_)
    {
        if (this.isRepeaterPowered)
        {
            int l = p_149674_1_.getBlockMetadata(p_149674_2_, p_149674_3_, p_149674_4_);
            p_149674_1_.setBlock(p_149674_2_, p_149674_3_, p_149674_4_, this.getBlockUnpowered(), l | 8, 4);
        }

        this.func_149972_c(p_149674_1_, p_149674_2_, p_149674_3_, p_149674_4_, p_149674_5_);
    }

    /**
     * Called whenever the block is added into the world. Args: world, x, y, z
     */
    public void onBlockAdded(World p_149726_1_, int p_149726_2_, int p_149726_3_, int p_149726_4_)
    {
        super.onBlockAdded(p_149726_1_, p_149726_2_, p_149726_3_, p_149726_4_);
        p_149726_1_.setTileEntity(p_149726_2_, p_149726_3_, p_149726_4_, this.createNewTileEntity(p_149726_1_, 0));
    }

    public void breakBlock(World p_149749_1_, int p_149749_2_, int p_149749_3_, int p_149749_4_, Block p_149749_5_, int p_149749_6_)
    {
        super.breakBlock(p_149749_1_, p_149749_2_, p_149749_3_, p_149749_4_, p_149749_5_, p_149749_6_);
        p_149749_1_.removeTileEntity(p_149749_2_, p_149749_3_, p_149749_4_);
        this.func_149911_e(p_149749_1_, p_149749_2_, p_149749_3_, p_149749_4_);
    }

    public boolean onBlockEventReceived(World p_149696_1_, int p_149696_2_, int p_149696_3_, int p_149696_4_, int p_149696_5_, int p_149696_6_)
    {
        super.onBlockEventReceived(p_149696_1_, p_149696_2_, p_149696_3_, p_149696_4_, p_149696_5_, p_149696_6_);
        TileEntity tileentity = p_149696_1_.getTileEntity(p_149696_2_, p_149696_3_, p_149696_4_);
        return tileentity != null ? tileentity.receiveClientEvent(p_149696_5_, p_149696_6_) : false;
    }

    /**
     * Returns a new instance of a block's tile entity class. Called on placing the block.
     */
    public TileEntity createNewTileEntity(World p_149915_1_, int p_149915_2_)
    {
        return new TileEntityComparator();
    }

    @Override
    public void onNeighborChange(IBlockAccess world, int x, int y, int z, int tileX, int tileY, int tileZ)
    {
        if (y == tileY && world instanceof World)
        {
            onNeighborBlockChange((World)world, x, y, z, world.getBlock(tileX, tileY, tileZ));
        }   
    }
    
    @Override
    public boolean getWeakChanges(IBlockAccess world, int x, int y, int z)
    {
        return true;
    }
}