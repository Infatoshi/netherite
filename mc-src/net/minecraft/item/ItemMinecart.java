package net.minecraft.item;

import net.minecraft.block.Block;
import net.minecraft.block.BlockDispenser;
import net.minecraft.block.BlockRailBase;
import net.minecraft.block.material.Material;
import net.minecraft.creativetab.CreativeTabs;
import net.minecraft.dispenser.BehaviorDefaultDispenseItem;
import net.minecraft.dispenser.IBehaviorDispenseItem;
import net.minecraft.dispenser.IBlockSource;
import net.minecraft.entity.item.EntityMinecart;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.util.EnumFacing;
import net.minecraft.world.World;

public class ItemMinecart extends Item
{
    private static final IBehaviorDispenseItem dispenserMinecartBehavior = new BehaviorDefaultDispenseItem()
    {
        private final BehaviorDefaultDispenseItem behaviourDefaultDispenseItem = new BehaviorDefaultDispenseItem();
        private static final String __OBFID = "CL_00000050";
        /**
         * Dispense the specified stack, play the dispense sound and spawn particles.
         */
        public ItemStack dispenseStack(IBlockSource p_82487_1_, ItemStack p_82487_2_)
        {
            EnumFacing enumfacing = BlockDispenser.func_149937_b(p_82487_1_.getBlockMetadata());
            World world = p_82487_1_.getWorld();
            double d0 = p_82487_1_.getX() + (double)((float)enumfacing.getFrontOffsetX() * 1.125F);
            double d1 = p_82487_1_.getY() + (double)((float)enumfacing.getFrontOffsetY() * 1.125F);
            double d2 = p_82487_1_.getZ() + (double)((float)enumfacing.getFrontOffsetZ() * 1.125F);
            int i = p_82487_1_.getXInt() + enumfacing.getFrontOffsetX();
            int j = p_82487_1_.getYInt() + enumfacing.getFrontOffsetY();
            int k = p_82487_1_.getZInt() + enumfacing.getFrontOffsetZ();
            Block block = world.getBlock(i, j, k);
            double d3;

            if (BlockRailBase.func_150051_a(block))
            {
                d3 = 0.0D;
            }
            else
            {
                if (block.getMaterial() != Material.air || !BlockRailBase.func_150051_a(world.getBlock(i, j - 1, k)))
                {
                    return this.behaviourDefaultDispenseItem.dispense(p_82487_1_, p_82487_2_);
                }

                d3 = -1.0D;
            }

            EntityMinecart entityminecart = EntityMinecart.createMinecart(world, d0, d1 + d3, d2, ((ItemMinecart)p_82487_2_.getItem()).minecartType);

            if (p_82487_2_.hasDisplayName())
            {
                entityminecart.setMinecartName(p_82487_2_.getDisplayName());
            }

            world.spawnEntityInWorld(entityminecart);
            p_82487_2_.splitStack(1);
            return p_82487_2_;
        }
        /**
         * Play the dispense sound from the specified block.
         */
        protected void playDispenseSound(IBlockSource p_82485_1_)
        {
            p_82485_1_.getWorld().playAuxSFX(1000, p_82485_1_.getXInt(), p_82485_1_.getYInt(), p_82485_1_.getZInt(), 0);
        }
    };
    public int minecartType;
    private static final String __OBFID = "CL_00000049";

    public ItemMinecart(int p_i45345_1_)
    {
        this.maxStackSize = 1;
        this.minecartType = p_i45345_1_;
        this.setCreativeTab(CreativeTabs.tabTransport);
        BlockDispenser.dispenseBehaviorRegistry.putObject(this, dispenserMinecartBehavior);
    }

    /**
     * Callback for item usage. If the item does something special on right clicking, he will have one of those. Return
     * True if something happen and false if it don't. This is for ITEMS, not BLOCKS
     */
    public boolean onItemUse(ItemStack p_77648_1_, EntityPlayer p_77648_2_, World p_77648_3_, int p_77648_4_, int p_77648_5_, int p_77648_6_, int p_77648_7_, float p_77648_8_, float p_77648_9_, float p_77648_10_)
    {
        if (BlockRailBase.func_150051_a(p_77648_3_.getBlock(p_77648_4_, p_77648_5_, p_77648_6_)))
        {
            if (!p_77648_3_.isRemote)
            {
                EntityMinecart entityminecart = EntityMinecart.createMinecart(p_77648_3_, (double)((float)p_77648_4_ + 0.5F), (double)((float)p_77648_5_ + 0.5F), (double)((float)p_77648_6_ + 0.5F), this.minecartType);

                if (p_77648_1_.hasDisplayName())
                {
                    entityminecart.setMinecartName(p_77648_1_.getDisplayName());
                }

                p_77648_3_.spawnEntityInWorld(entityminecart);
            }

            --p_77648_1_.stackSize;
            return true;
        }
        else
        {
            return false;
        }
    }
}