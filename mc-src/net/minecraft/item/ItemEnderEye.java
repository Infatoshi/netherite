package net.minecraft.item;

import net.minecraft.block.Block;
import net.minecraft.block.BlockEndPortalFrame;
import net.minecraft.creativetab.CreativeTabs;
import net.minecraft.entity.item.EntityEnderEye;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.init.Blocks;
import net.minecraft.util.Direction;
import net.minecraft.util.MovingObjectPosition;
import net.minecraft.world.ChunkPosition;
import net.minecraft.world.World;

public class ItemEnderEye extends Item
{
    private static final String __OBFID = "CL_00000026";

    public ItemEnderEye()
    {
        this.setCreativeTab(CreativeTabs.tabMisc);
    }

    /**
     * Callback for item usage. If the item does something special on right clicking, he will have one of those. Return
     * True if something happen and false if it don't. This is for ITEMS, not BLOCKS
     */
    public boolean onItemUse(ItemStack p_77648_1_, EntityPlayer p_77648_2_, World p_77648_3_, int p_77648_4_, int p_77648_5_, int p_77648_6_, int p_77648_7_, float p_77648_8_, float p_77648_9_, float p_77648_10_)
    {
        Block block = p_77648_3_.getBlock(p_77648_4_, p_77648_5_, p_77648_6_);
        int i1 = p_77648_3_.getBlockMetadata(p_77648_4_, p_77648_5_, p_77648_6_);

        if (p_77648_2_.canPlayerEdit(p_77648_4_, p_77648_5_, p_77648_6_, p_77648_7_, p_77648_1_) && block == Blocks.end_portal_frame && !BlockEndPortalFrame.isEnderEyeInserted(i1))
        {
            if (p_77648_3_.isRemote)
            {
                return true;
            }
            else
            {
                p_77648_3_.setBlockMetadataWithNotify(p_77648_4_, p_77648_5_, p_77648_6_, i1 + 4, 2);
                p_77648_3_.func_147453_f(p_77648_4_, p_77648_5_, p_77648_6_, Blocks.end_portal_frame);
                --p_77648_1_.stackSize;
                int j1;

                for (j1 = 0; j1 < 16; ++j1)
                {
                    double d0 = (double)((float)p_77648_4_ + (5.0F + itemRand.nextFloat() * 6.0F) / 16.0F);
                    double d1 = (double)((float)p_77648_5_ + 0.8125F);
                    double d2 = (double)((float)p_77648_6_ + (5.0F + itemRand.nextFloat() * 6.0F) / 16.0F);
                    double d3 = 0.0D;
                    double d4 = 0.0D;
                    double d5 = 0.0D;
                    p_77648_3_.spawnParticle("smoke", d0, d1, d2, d3, d4, d5);
                }

                j1 = i1 & 3;
                int j2 = 0;
                int k1 = 0;
                boolean flag1 = false;
                boolean flag = true;
                int k2 = Direction.rotateRight[j1];
                int l1;
                int i2;
                int l2;

                for (l1 = -2; l1 <= 2; ++l1)
                {
                    l2 = p_77648_4_ + Direction.offsetX[k2] * l1;
                    i2 = p_77648_6_ + Direction.offsetZ[k2] * l1;

                    if (p_77648_3_.getBlock(l2, p_77648_5_, i2) == Blocks.end_portal_frame)
                    {
                        if (!BlockEndPortalFrame.isEnderEyeInserted(p_77648_3_.getBlockMetadata(l2, p_77648_5_, i2)))
                        {
                            flag = false;
                            break;
                        }

                        k1 = l1;

                        if (!flag1)
                        {
                            j2 = l1;
                            flag1 = true;
                        }
                    }
                }

                if (flag && k1 == j2 + 2)
                {
                    for (l1 = j2; l1 <= k1; ++l1)
                    {
                        l2 = p_77648_4_ + Direction.offsetX[k2] * l1;
                        i2 = p_77648_6_ + Direction.offsetZ[k2] * l1;
                        l2 += Direction.offsetX[j1] * 4;
                        i2 += Direction.offsetZ[j1] * 4;

                        if (p_77648_3_.getBlock(l2, p_77648_5_, i2) != Blocks.end_portal_frame || !BlockEndPortalFrame.isEnderEyeInserted(p_77648_3_.getBlockMetadata(l2, p_77648_5_, i2)))
                        {
                            flag = false;
                            break;
                        }
                    }

                    int i3;

                    for (l1 = j2 - 1; l1 <= k1 + 1; l1 += 4)
                    {
                        for (l2 = 1; l2 <= 3; ++l2)
                        {
                            i2 = p_77648_4_ + Direction.offsetX[k2] * l1;
                            i3 = p_77648_6_ + Direction.offsetZ[k2] * l1;
                            i2 += Direction.offsetX[j1] * l2;
                            i3 += Direction.offsetZ[j1] * l2;

                            if (p_77648_3_.getBlock(i2, p_77648_5_, i3) != Blocks.end_portal_frame || !BlockEndPortalFrame.isEnderEyeInserted(p_77648_3_.getBlockMetadata(i2, p_77648_5_, i3)))
                            {
                                flag = false;
                                break;
                            }
                        }
                    }

                    if (flag)
                    {
                        for (l1 = j2; l1 <= k1; ++l1)
                        {
                            for (l2 = 1; l2 <= 3; ++l2)
                            {
                                i2 = p_77648_4_ + Direction.offsetX[k2] * l1;
                                i3 = p_77648_6_ + Direction.offsetZ[k2] * l1;
                                i2 += Direction.offsetX[j1] * l2;
                                i3 += Direction.offsetZ[j1] * l2;
                                p_77648_3_.setBlock(i2, p_77648_5_, i3, Blocks.end_portal, 0, 2);
                            }
                        }
                    }
                }

                return true;
            }
        }
        else
        {
            return false;
        }
    }

    /**
     * Called whenever this item is equipped and the right mouse button is pressed. Args: itemStack, world, entityPlayer
     */
    public ItemStack onItemRightClick(ItemStack p_77659_1_, World p_77659_2_, EntityPlayer p_77659_3_)
    {
        MovingObjectPosition movingobjectposition = this.getMovingObjectPositionFromPlayer(p_77659_2_, p_77659_3_, false);

        if (movingobjectposition != null && movingobjectposition.typeOfHit == MovingObjectPosition.MovingObjectType.BLOCK && p_77659_2_.getBlock(movingobjectposition.blockX, movingobjectposition.blockY, movingobjectposition.blockZ) == Blocks.end_portal_frame)
        {
            return p_77659_1_;
        }
        else
        {
            if (!p_77659_2_.isRemote)
            {
                ChunkPosition chunkposition = p_77659_2_.findClosestStructure("Stronghold", (int)p_77659_3_.posX, (int)p_77659_3_.posY, (int)p_77659_3_.posZ);

                if (chunkposition != null)
                {
                    EntityEnderEye entityendereye = new EntityEnderEye(p_77659_2_, p_77659_3_.posX, p_77659_3_.posY + 1.62D - (double)p_77659_3_.yOffset, p_77659_3_.posZ);
                    entityendereye.moveTowards((double)chunkposition.chunkPosX, chunkposition.chunkPosY, (double)chunkposition.chunkPosZ);
                    p_77659_2_.spawnEntityInWorld(entityendereye);
                    p_77659_2_.playSoundAtEntity(p_77659_3_, "random.bow", 0.5F, 0.4F / (itemRand.nextFloat() * 0.4F + 0.8F));
                    p_77659_2_.playAuxSFXAtEntity((EntityPlayer)null, 1002, (int)p_77659_3_.posX, (int)p_77659_3_.posY, (int)p_77659_3_.posZ, 0);

                    if (!p_77659_3_.capabilities.isCreativeMode)
                    {
                        --p_77659_1_.stackSize;
                    }
                }
            }

            return p_77659_1_;
        }
    }
}