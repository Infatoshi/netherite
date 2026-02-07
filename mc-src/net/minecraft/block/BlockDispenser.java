package net.minecraft.block;

import net.minecraft.block.material.Material;
import net.minecraft.dispenser.BehaviorDefaultDispenseItem;
import net.minecraft.dispenser.IBlockSource;
import net.minecraft.dispenser.IPosition;
import net.minecraft.dispenser.PositionImpl;
import net.minecraft.tileentity.TileEntity;
import net.minecraft.util.EnumFacing;
import net.minecraft.util.IRegistry;
import net.minecraft.util.RegistryDefaulted;
import net.minecraft.world.World;

/**
 * Stripped BlockDispenser: only the static dispenseBehaviorRegistry and helper methods
 * needed by BehaviorDefaultDispenseItem / BehaviorProjectileDispense / Bootstrap.
 * The actual "dispenser" block in the registry is a plain Block(Material.rock).
 */
public class BlockDispenser extends BlockContainer
{
    public static final IRegistry dispenseBehaviorRegistry = new RegistryDefaulted(new BehaviorDefaultDispenseItem());

    protected BlockDispenser()
    {
        super(Material.rock);
    }

    public TileEntity createNewTileEntity(World p_149915_1_, int p_149915_2_)
    {
        return null; // stripped: was TileEntityDispenser
    }

    public static IPosition func_149939_a(IBlockSource p_149939_0_)
    {
        EnumFacing enumfacing = func_149937_b(p_149939_0_.getBlockMetadata());
        double d0 = p_149939_0_.getX() + 0.7D * (double)enumfacing.getFrontOffsetX();
        double d1 = p_149939_0_.getY() + 0.7D * (double)enumfacing.getFrontOffsetY();
        double d2 = p_149939_0_.getZ() + 0.7D * (double)enumfacing.getFrontOffsetZ();
        return new PositionImpl(d0, d1, d2);
    }

    public static EnumFacing func_149937_b(int p_149937_0_)
    {
        return EnumFacing.getFront(p_149937_0_ & 7);
    }
}
