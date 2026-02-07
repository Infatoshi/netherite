package net.minecraft.dispenser;

import net.minecraft.block.BlockDispenser;
import net.minecraft.entity.item.EntityItem;
import net.minecraft.item.ItemStack;
import net.minecraft.util.EnumFacing;
import net.minecraft.world.World;

public class BehaviorDefaultDispenseItem implements IBehaviorDispenseItem
{
    private static final String __OBFID = "CL_00001195";

    /**
     * Dispenses the specified ItemStack from a dispenser.
     */
    public final ItemStack dispense(IBlockSource p_82482_1_, ItemStack p_82482_2_)
    {
        ItemStack itemstack1 = this.dispenseStack(p_82482_1_, p_82482_2_);
        this.playDispenseSound(p_82482_1_);
        this.spawnDispenseParticles(p_82482_1_, BlockDispenser.func_149937_b(p_82482_1_.getBlockMetadata()));
        return itemstack1;
    }

    /**
     * Dispense the specified stack, play the dispense sound and spawn particles.
     */
    protected ItemStack dispenseStack(IBlockSource p_82487_1_, ItemStack p_82487_2_)
    {
        EnumFacing enumfacing = BlockDispenser.func_149937_b(p_82487_1_.getBlockMetadata());
        IPosition iposition = BlockDispenser.func_149939_a(p_82487_1_);
        ItemStack itemstack1 = p_82487_2_.splitStack(1);
        doDispense(p_82487_1_.getWorld(), itemstack1, 6, enumfacing, iposition);
        return p_82487_2_;
    }

    public static void doDispense(World p_82486_0_, ItemStack p_82486_1_, int p_82486_2_, EnumFacing p_82486_3_, IPosition p_82486_4_)
    {
        double d0 = p_82486_4_.getX();
        double d1 = p_82486_4_.getY();
        double d2 = p_82486_4_.getZ();
        EntityItem entityitem = new EntityItem(p_82486_0_, d0, d1 - 0.3D, d2, p_82486_1_);
        double d3 = p_82486_0_.rand.nextDouble() * 0.1D + 0.2D;
        entityitem.motionX = (double)p_82486_3_.getFrontOffsetX() * d3;
        entityitem.motionY = 0.20000000298023224D;
        entityitem.motionZ = (double)p_82486_3_.getFrontOffsetZ() * d3;
        entityitem.motionX += p_82486_0_.rand.nextGaussian() * 0.007499999832361937D * (double)p_82486_2_;
        entityitem.motionY += p_82486_0_.rand.nextGaussian() * 0.007499999832361937D * (double)p_82486_2_;
        entityitem.motionZ += p_82486_0_.rand.nextGaussian() * 0.007499999832361937D * (double)p_82486_2_;
        p_82486_0_.spawnEntityInWorld(entityitem);
    }

    /**
     * Play the dispense sound from the specified block.
     */
    protected void playDispenseSound(IBlockSource p_82485_1_)
    {
        p_82485_1_.getWorld().playAuxSFX(1000, p_82485_1_.getXInt(), p_82485_1_.getYInt(), p_82485_1_.getZInt(), 0);
    }

    /**
     * Order clients to display dispense particles from the specified block and facing.
     */
    protected void spawnDispenseParticles(IBlockSource p_82489_1_, EnumFacing p_82489_2_)
    {
        p_82489_1_.getWorld().playAuxSFX(2000, p_82489_1_.getXInt(), p_82489_1_.getYInt(), p_82489_1_.getZInt(), this.func_82488_a(p_82489_2_));
    }

    private int func_82488_a(EnumFacing p_82488_1_)
    {
        return p_82488_1_.getFrontOffsetX() + 1 + (p_82488_1_.getFrontOffsetZ() + 1) * 3;
    }
}