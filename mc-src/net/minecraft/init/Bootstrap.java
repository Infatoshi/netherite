package net.minecraft.init;

import java.util.Random;
import net.minecraft.block.Block;
import net.minecraft.block.BlockDispenser;
import net.minecraft.block.BlockFire;
import net.minecraft.block.material.Material;
import net.minecraft.dispenser.BehaviorDefaultDispenseItem;
import net.minecraft.dispenser.BehaviorProjectileDispense;
import net.minecraft.dispenser.IBehaviorDispenseItem;
import net.minecraft.dispenser.IBlockSource;
import net.minecraft.dispenser.IPosition;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityLiving;
import net.minecraft.entity.EntityLivingBase;
import net.minecraft.entity.IProjectile;
import net.minecraft.entity.item.EntityBoat;
import net.minecraft.entity.item.EntityExpBottle;
import net.minecraft.entity.item.EntityFireworkRocket;
import net.minecraft.entity.item.EntityTNTPrimed;
import net.minecraft.entity.projectile.EntityArrow;
import net.minecraft.entity.projectile.EntityEgg;
import net.minecraft.entity.projectile.EntityPotion;
import net.minecraft.entity.projectile.EntitySmallFireball;
import net.minecraft.entity.projectile.EntitySnowball;
import net.minecraft.item.Item;
import net.minecraft.item.ItemBucket;
import net.minecraft.item.ItemDye;
import net.minecraft.item.ItemMonsterPlacer;
import net.minecraft.item.ItemPotion;
import net.minecraft.item.ItemStack;
import net.minecraft.stats.StatList;
import net.minecraft.tileentity.TileEntityDispenser;
import net.minecraft.util.EnumFacing;
import net.minecraft.world.World;

public class Bootstrap
{
    private static boolean field_151355_a = false;
    private static final String __OBFID = "CL_00001397";

    static void func_151353_a()
    {
        BlockDispenser.dispenseBehaviorRegistry.putObject(Items.arrow, new BehaviorProjectileDispense()
        {
            private static final String __OBFID = "CL_00001398";
            /**
             * Return the projectile entity spawned by this dispense behavior.
             */
            protected IProjectile getProjectileEntity(World p_82499_1_, IPosition p_82499_2_)
            {
                EntityArrow entityarrow = new EntityArrow(p_82499_1_, p_82499_2_.getX(), p_82499_2_.getY(), p_82499_2_.getZ());
                entityarrow.canBePickedUp = 1;
                return entityarrow;
            }
        });
        BlockDispenser.dispenseBehaviorRegistry.putObject(Items.egg, new BehaviorProjectileDispense()
        {
            private static final String __OBFID = "CL_00001404";
            /**
             * Return the projectile entity spawned by this dispense behavior.
             */
            protected IProjectile getProjectileEntity(World p_82499_1_, IPosition p_82499_2_)
            {
                return new EntityEgg(p_82499_1_, p_82499_2_.getX(), p_82499_2_.getY(), p_82499_2_.getZ());
            }
        });
        BlockDispenser.dispenseBehaviorRegistry.putObject(Items.snowball, new BehaviorProjectileDispense()
        {
            private static final String __OBFID = "CL_00001405";
            /**
             * Return the projectile entity spawned by this dispense behavior.
             */
            protected IProjectile getProjectileEntity(World p_82499_1_, IPosition p_82499_2_)
            {
                return new EntitySnowball(p_82499_1_, p_82499_2_.getX(), p_82499_2_.getY(), p_82499_2_.getZ());
            }
        });
        BlockDispenser.dispenseBehaviorRegistry.putObject(Items.experience_bottle, new BehaviorProjectileDispense()
        {
            private static final String __OBFID = "CL_00001406";
            /**
             * Return the projectile entity spawned by this dispense behavior.
             */
            protected IProjectile getProjectileEntity(World p_82499_1_, IPosition p_82499_2_)
            {
                return new EntityExpBottle(p_82499_1_, p_82499_2_.getX(), p_82499_2_.getY(), p_82499_2_.getZ());
            }
            protected float func_82498_a()
            {
                return super.func_82498_a() * 0.5F;
            }
            protected float func_82500_b()
            {
                return super.func_82500_b() * 1.25F;
            }
        });
        BlockDispenser.dispenseBehaviorRegistry.putObject(Items.potionitem, new IBehaviorDispenseItem()
        {
            private final BehaviorDefaultDispenseItem field_150843_b = new BehaviorDefaultDispenseItem();
            private static final String __OBFID = "CL_00001407";
            /**
             * Dispenses the specified ItemStack from a dispenser.
             */
            public ItemStack dispense(IBlockSource p_82482_1_, final ItemStack p_82482_2_)
            {
                return ItemPotion.isSplash(p_82482_2_.getItemDamage()) ? (new BehaviorProjectileDispense()
                {
                    private static final String __OBFID = "CL_00001408";
                    /**
                     * Return the projectile entity spawned by this dispense behavior.
                     */
                    protected IProjectile getProjectileEntity(World p_82499_1_, IPosition p_82499_2_)
                    {
                        return new EntityPotion(p_82499_1_, p_82499_2_.getX(), p_82499_2_.getY(), p_82499_2_.getZ(), p_82482_2_.copy());
                    }
                    protected float func_82498_a()
                    {
                        return super.func_82498_a() * 0.5F;
                    }
                    protected float func_82500_b()
                    {
                        return super.func_82500_b() * 1.25F;
                    }
                }).dispense(p_82482_1_, p_82482_2_): this.field_150843_b.dispense(p_82482_1_, p_82482_2_);
            }
        });
        BlockDispenser.dispenseBehaviorRegistry.putObject(Items.spawn_egg, new BehaviorDefaultDispenseItem()
        {
            private static final String __OBFID = "CL_00001410";
            /**
             * Dispense the specified stack, play the dispense sound and spawn particles.
             */
            public ItemStack dispenseStack(IBlockSource p_82487_1_, ItemStack p_82487_2_)
            {
                EnumFacing enumfacing = BlockDispenser.func_149937_b(p_82487_1_.getBlockMetadata());
                double d0 = p_82487_1_.getX() + (double)enumfacing.getFrontOffsetX();
                double d1 = (double)((float)p_82487_1_.getYInt() + 0.2F);
                double d2 = p_82487_1_.getZ() + (double)enumfacing.getFrontOffsetZ();
                Entity entity = ItemMonsterPlacer.spawnCreature(p_82487_1_.getWorld(), p_82487_2_.getItemDamage(), d0, d1, d2);

                if (entity instanceof EntityLivingBase && p_82487_2_.hasDisplayName())
                {
                    ((EntityLiving)entity).setCustomNameTag(p_82487_2_.getDisplayName());
                }

                p_82487_2_.splitStack(1);
                return p_82487_2_;
            }
        });
        BlockDispenser.dispenseBehaviorRegistry.putObject(Items.fireworks, new BehaviorDefaultDispenseItem()
        {
            private static final String __OBFID = "CL_00001411";
            /**
             * Dispense the specified stack, play the dispense sound and spawn particles.
             */
            public ItemStack dispenseStack(IBlockSource p_82487_1_, ItemStack p_82487_2_)
            {
                EnumFacing enumfacing = BlockDispenser.func_149937_b(p_82487_1_.getBlockMetadata());
                double d0 = p_82487_1_.getX() + (double)enumfacing.getFrontOffsetX();
                double d1 = (double)((float)p_82487_1_.getYInt() + 0.2F);
                double d2 = p_82487_1_.getZ() + (double)enumfacing.getFrontOffsetZ();
                EntityFireworkRocket entityfireworkrocket = new EntityFireworkRocket(p_82487_1_.getWorld(), d0, d1, d2, p_82487_2_);
                p_82487_1_.getWorld().spawnEntityInWorld(entityfireworkrocket);
                p_82487_2_.splitStack(1);
                return p_82487_2_;
            }
            /**
             * Play the dispense sound from the specified block.
             */
            protected void playDispenseSound(IBlockSource p_82485_1_)
            {
                p_82485_1_.getWorld().playAuxSFX(1002, p_82485_1_.getXInt(), p_82485_1_.getYInt(), p_82485_1_.getZInt(), 0);
            }
        });
        BlockDispenser.dispenseBehaviorRegistry.putObject(Items.fire_charge, new BehaviorDefaultDispenseItem()
        {
            private static final String __OBFID = "CL_00001412";
            /**
             * Dispense the specified stack, play the dispense sound and spawn particles.
             */
            public ItemStack dispenseStack(IBlockSource p_82487_1_, ItemStack p_82487_2_)
            {
                EnumFacing enumfacing = BlockDispenser.func_149937_b(p_82487_1_.getBlockMetadata());
                IPosition iposition = BlockDispenser.func_149939_a(p_82487_1_);
                double d0 = iposition.getX() + (double)((float)enumfacing.getFrontOffsetX() * 0.3F);
                double d1 = iposition.getY() + (double)((float)enumfacing.getFrontOffsetX() * 0.3F);
                double d2 = iposition.getZ() + (double)((float)enumfacing.getFrontOffsetZ() * 0.3F);
                World world = p_82487_1_.getWorld();
                Random random = world.rand;
                double d3 = random.nextGaussian() * 0.05D + (double)enumfacing.getFrontOffsetX();
                double d4 = random.nextGaussian() * 0.05D + (double)enumfacing.getFrontOffsetY();
                double d5 = random.nextGaussian() * 0.05D + (double)enumfacing.getFrontOffsetZ();
                world.spawnEntityInWorld(new EntitySmallFireball(world, d0, d1, d2, d3, d4, d5));
                p_82487_2_.splitStack(1);
                return p_82487_2_;
            }
            /**
             * Play the dispense sound from the specified block.
             */
            protected void playDispenseSound(IBlockSource p_82485_1_)
            {
                p_82485_1_.getWorld().playAuxSFX(1009, p_82485_1_.getXInt(), p_82485_1_.getYInt(), p_82485_1_.getZInt(), 0);
            }
        });
        BlockDispenser.dispenseBehaviorRegistry.putObject(Items.boat, new BehaviorDefaultDispenseItem()
        {
            private final BehaviorDefaultDispenseItem field_150842_b = new BehaviorDefaultDispenseItem();
            private static final String __OBFID = "CL_00001413";
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
                Material material = world.getBlock(i, j, k).getMaterial();
                double d3;

                if (Material.water.equals(material))
                {
                    d3 = 1.0D;
                }
                else
                {
                    if (!Material.air.equals(material) || !Material.water.equals(world.getBlock(i, j - 1, k).getMaterial()))
                    {
                        return this.field_150842_b.dispense(p_82487_1_, p_82487_2_);
                    }

                    d3 = 0.0D;
                }

                EntityBoat entityboat = new EntityBoat(world, d0, d1 + d3, d2);
                world.spawnEntityInWorld(entityboat);
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
        });
        BehaviorDefaultDispenseItem behaviordefaultdispenseitem = new BehaviorDefaultDispenseItem()
        {
            private final BehaviorDefaultDispenseItem field_150841_b = new BehaviorDefaultDispenseItem();
            private static final String __OBFID = "CL_00001399";
            /**
             * Dispense the specified stack, play the dispense sound and spawn particles.
             */
            public ItemStack dispenseStack(IBlockSource p_82487_1_, ItemStack p_82487_2_)
            {
                ItemBucket itembucket = (ItemBucket)p_82487_2_.getItem();
                int i = p_82487_1_.getXInt();
                int j = p_82487_1_.getYInt();
                int k = p_82487_1_.getZInt();
                EnumFacing enumfacing = BlockDispenser.func_149937_b(p_82487_1_.getBlockMetadata());

                if (itembucket.tryPlaceContainedLiquid(p_82487_1_.getWorld(), i + enumfacing.getFrontOffsetX(), j + enumfacing.getFrontOffsetY(), k + enumfacing.getFrontOffsetZ()))
                {
                    p_82487_2_.func_150996_a(Items.bucket);
                    p_82487_2_.stackSize = 1;
                    return p_82487_2_;
                }
                else
                {
                    return this.field_150841_b.dispense(p_82487_1_, p_82487_2_);
                }
            }
        };
        BlockDispenser.dispenseBehaviorRegistry.putObject(Items.lava_bucket, behaviordefaultdispenseitem);
        BlockDispenser.dispenseBehaviorRegistry.putObject(Items.water_bucket, behaviordefaultdispenseitem);
        BlockDispenser.dispenseBehaviorRegistry.putObject(Items.bucket, new BehaviorDefaultDispenseItem()
        {
            private final BehaviorDefaultDispenseItem field_150840_b = new BehaviorDefaultDispenseItem();
            private static final String __OBFID = "CL_00001400";
            /**
             * Dispense the specified stack, play the dispense sound and spawn particles.
             */
            public ItemStack dispenseStack(IBlockSource p_82487_1_, ItemStack p_82487_2_)
            {
                EnumFacing enumfacing = BlockDispenser.func_149937_b(p_82487_1_.getBlockMetadata());
                World world = p_82487_1_.getWorld();
                int i = p_82487_1_.getXInt() + enumfacing.getFrontOffsetX();
                int j = p_82487_1_.getYInt() + enumfacing.getFrontOffsetY();
                int k = p_82487_1_.getZInt() + enumfacing.getFrontOffsetZ();
                Material material = world.getBlock(i, j, k).getMaterial();
                int l = world.getBlockMetadata(i, j, k);
                Item item;

                if (Material.water.equals(material) && l == 0)
                {
                    item = Items.water_bucket;
                }
                else
                {
                    if (!Material.lava.equals(material) || l != 0)
                    {
                        return super.dispenseStack(p_82487_1_, p_82487_2_);
                    }

                    item = Items.lava_bucket;
                }

                world.setBlockToAir(i, j, k);

                if (--p_82487_2_.stackSize == 0)
                {
                    p_82487_2_.func_150996_a(item);
                    p_82487_2_.stackSize = 1;
                }
                else if (((TileEntityDispenser)p_82487_1_.getBlockTileEntity()).func_146019_a(new ItemStack(item)) < 0)
                {
                    this.field_150840_b.dispense(p_82487_1_, new ItemStack(item));
                }

                return p_82487_2_;
            }
        });
        BlockDispenser.dispenseBehaviorRegistry.putObject(Items.flint_and_steel, new BehaviorDefaultDispenseItem()
        {
            private boolean field_150839_b = true;
            private static final String __OBFID = "CL_00001401";
            /**
             * Dispense the specified stack, play the dispense sound and spawn particles.
             */
            protected ItemStack dispenseStack(IBlockSource p_82487_1_, ItemStack p_82487_2_)
            {
                EnumFacing enumfacing = BlockDispenser.func_149937_b(p_82487_1_.getBlockMetadata());
                World world = p_82487_1_.getWorld();
                int i = p_82487_1_.getXInt() + enumfacing.getFrontOffsetX();
                int j = p_82487_1_.getYInt() + enumfacing.getFrontOffsetY();
                int k = p_82487_1_.getZInt() + enumfacing.getFrontOffsetZ();

                if (world.isAirBlock(i, j, k))
                {
                    world.setBlock(i, j, k, Blocks.fire);

                    if (p_82487_2_.attemptDamageItem(1, world.rand))
                    {
                        p_82487_2_.stackSize = 0;
                    }
                }
                else if (world.getBlock(i, j, k) == Blocks.tnt)
                {
                    Blocks.tnt.onBlockDestroyedByPlayer(world, i, j, k, 1);
                    world.setBlockToAir(i, j, k);
                }
                else
                {
                    this.field_150839_b = false;
                }

                return p_82487_2_;
            }
            /**
             * Play the dispense sound from the specified block.
             */
            protected void playDispenseSound(IBlockSource p_82485_1_)
            {
                if (this.field_150839_b)
                {
                    p_82485_1_.getWorld().playAuxSFX(1000, p_82485_1_.getXInt(), p_82485_1_.getYInt(), p_82485_1_.getZInt(), 0);
                }
                else
                {
                    p_82485_1_.getWorld().playAuxSFX(1001, p_82485_1_.getXInt(), p_82485_1_.getYInt(), p_82485_1_.getZInt(), 0);
                }
            }
        });
        BlockDispenser.dispenseBehaviorRegistry.putObject(Items.dye, new BehaviorDefaultDispenseItem()
        {
            private boolean field_150838_b = true;
            private static final String __OBFID = "CL_00001402";
            /**
             * Dispense the specified stack, play the dispense sound and spawn particles.
             */
            protected ItemStack dispenseStack(IBlockSource p_82487_1_, ItemStack p_82487_2_)
            {
                if (p_82487_2_.getItemDamage() == 15)
                {
                    EnumFacing enumfacing = BlockDispenser.func_149937_b(p_82487_1_.getBlockMetadata());
                    World world = p_82487_1_.getWorld();
                    int i = p_82487_1_.getXInt() + enumfacing.getFrontOffsetX();
                    int j = p_82487_1_.getYInt() + enumfacing.getFrontOffsetY();
                    int k = p_82487_1_.getZInt() + enumfacing.getFrontOffsetZ();

                    if (ItemDye.func_150919_a(p_82487_2_, world, i, j, k))
                    {
                        if (!world.isRemote)
                        {
                            world.playAuxSFX(2005, i, j, k, 0);
                        }
                    }
                    else
                    {
                        this.field_150838_b = false;
                    }

                    return p_82487_2_;
                }
                else
                {
                    return super.dispenseStack(p_82487_1_, p_82487_2_);
                }
            }
            /**
             * Play the dispense sound from the specified block.
             */
            protected void playDispenseSound(IBlockSource p_82485_1_)
            {
                if (this.field_150838_b)
                {
                    p_82485_1_.getWorld().playAuxSFX(1000, p_82485_1_.getXInt(), p_82485_1_.getYInt(), p_82485_1_.getZInt(), 0);
                }
                else
                {
                    p_82485_1_.getWorld().playAuxSFX(1001, p_82485_1_.getXInt(), p_82485_1_.getYInt(), p_82485_1_.getZInt(), 0);
                }
            }
        });
        BlockDispenser.dispenseBehaviorRegistry.putObject(Item.getItemFromBlock(Blocks.tnt), new BehaviorDefaultDispenseItem()
        {
            private static final String __OBFID = "CL_00001403";
            /**
             * Dispense the specified stack, play the dispense sound and spawn particles.
             */
            protected ItemStack dispenseStack(IBlockSource p_82487_1_, ItemStack p_82487_2_)
            {
                EnumFacing enumfacing = BlockDispenser.func_149937_b(p_82487_1_.getBlockMetadata());
                World world = p_82487_1_.getWorld();
                int i = p_82487_1_.getXInt() + enumfacing.getFrontOffsetX();
                int j = p_82487_1_.getYInt() + enumfacing.getFrontOffsetY();
                int k = p_82487_1_.getZInt() + enumfacing.getFrontOffsetZ();
                EntityTNTPrimed entitytntprimed = new EntityTNTPrimed(world, (double)((float)i + 0.5F), (double)((float)j + 0.5F), (double)((float)k + 0.5F), (EntityLivingBase)null);
                world.spawnEntityInWorld(entitytntprimed);
                --p_82487_2_.stackSize;
                return p_82487_2_;
            }
        });
    }

    public static void func_151354_b()
    {
        if (!field_151355_a)
        {
            field_151355_a = true;
            Block.registerBlocks();
            BlockFire.func_149843_e();
            Item.registerItems();
            StatList.func_151178_a();
            func_151353_a();
        }
    }
}