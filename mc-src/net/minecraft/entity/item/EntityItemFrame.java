package net.minecraft.entity.item;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityHanging;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.init.Items;
import net.minecraft.item.ItemMap;
import net.minecraft.item.ItemStack;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.util.DamageSource;
import net.minecraft.world.World;
import net.minecraft.world.storage.MapData;

public class EntityItemFrame extends EntityHanging
{
    /** Chance for this item frame's item to drop from the frame. */
    private float itemDropChance = 1.0F;
    private static final String __OBFID = "CL_00001547";

    public EntityItemFrame(World p_i1590_1_)
    {
        super(p_i1590_1_);
    }

    public EntityItemFrame(World p_i1591_1_, int p_i1591_2_, int p_i1591_3_, int p_i1591_4_, int p_i1591_5_)
    {
        super(p_i1591_1_, p_i1591_2_, p_i1591_3_, p_i1591_4_, p_i1591_5_);
        this.setDirection(p_i1591_5_);
    }

    protected void entityInit()
    {
        this.getDataWatcher().addObjectByDataType(2, 5);
        this.getDataWatcher().addObject(3, Byte.valueOf((byte)0));
    }

    /**
     * Called when the entity is attacked.
     */
    public boolean attackEntityFrom(DamageSource p_70097_1_, float p_70097_2_)
    {
        if (this.isEntityInvulnerable())
        {
            return false;
        }
        else if (this.getDisplayedItem() != null)
        {
            if (!this.worldObj.isRemote)
            {
                this.func_146065_b(p_70097_1_.getEntity(), false);
                this.setDisplayedItem((ItemStack)null);
            }

            return true;
        }
        else
        {
            return super.attackEntityFrom(p_70097_1_, p_70097_2_);
        }
    }

    public int getWidthPixels()
    {
        return 9;
    }

    public int getHeightPixels()
    {
        return 9;
    }

    /**
     * Checks if the entity is in range to render by using the past in distance and comparing it to its average edge
     * length * 64 * renderDistanceWeight Args: distance
     */
    @SideOnly(Side.CLIENT)
    public boolean isInRangeToRenderDist(double p_70112_1_)
    {
        double d1 = 16.0D;
        d1 *= 64.0D * this.renderDistanceWeight;
        return p_70112_1_ < d1 * d1;
    }

    /**
     * Called when this entity is broken. Entity parameter may be null.
     */
    public void onBroken(Entity p_110128_1_)
    {
        this.func_146065_b(p_110128_1_, true);
    }

    public void func_146065_b(Entity p_146065_1_, boolean p_146065_2_)
    {
        ItemStack itemstack = this.getDisplayedItem();

        if (p_146065_1_ instanceof EntityPlayer)
        {
            EntityPlayer entityplayer = (EntityPlayer)p_146065_1_;

            if (entityplayer.capabilities.isCreativeMode)
            {
                this.removeFrameFromMap(itemstack);
                return;
            }
        }

        if (p_146065_2_)
        {
            this.entityDropItem(new ItemStack(Items.item_frame), 0.0F);
        }

        if (itemstack != null && this.rand.nextFloat() < this.itemDropChance)
        {
            itemstack = itemstack.copy();
            this.removeFrameFromMap(itemstack);
            this.entityDropItem(itemstack, 0.0F);
        }
    }

    /**
     * Removes the dot representing this frame's position from the map when the item frame is broken.
     */
    private void removeFrameFromMap(ItemStack p_110131_1_)
    {
        if (p_110131_1_ != null)
        {
            if (p_110131_1_.getItem() == Items.filled_map)
            {
                MapData mapdata = ((ItemMap)p_110131_1_.getItem()).getMapData(p_110131_1_, this.worldObj);
                mapdata.playersVisibleOnMap.remove("frame-" + this.getEntityId());
            }

            p_110131_1_.setItemFrame((EntityItemFrame)null);
        }
    }

    public ItemStack getDisplayedItem()
    {
        return this.getDataWatcher().getWatchableObjectItemStack(2);
    }

    public void setDisplayedItem(ItemStack p_82334_1_)
    {
        if (p_82334_1_ != null)
        {
            p_82334_1_ = p_82334_1_.copy();
            p_82334_1_.stackSize = 1;
            p_82334_1_.setItemFrame(this);
        }

        this.getDataWatcher().updateObject(2, p_82334_1_);
        this.getDataWatcher().setObjectWatched(2);
    }

    /**
     * Return the rotation of the item currently on this frame.
     */
    public int getRotation()
    {
        return this.getDataWatcher().getWatchableObjectByte(3);
    }

    public void setItemRotation(int p_82336_1_)
    {
        this.getDataWatcher().updateObject(3, Byte.valueOf((byte)(p_82336_1_ % 4)));
    }

    /**
     * (abstract) Protected helper method to write subclass entity data to NBT.
     */
    public void writeEntityToNBT(NBTTagCompound p_70014_1_)
    {
        if (this.getDisplayedItem() != null)
        {
            p_70014_1_.setTag("Item", this.getDisplayedItem().writeToNBT(new NBTTagCompound()));
            p_70014_1_.setByte("ItemRotation", (byte)this.getRotation());
            p_70014_1_.setFloat("ItemDropChance", this.itemDropChance);
        }

        super.writeEntityToNBT(p_70014_1_);
    }

    /**
     * (abstract) Protected helper method to read subclass entity data from NBT.
     */
    public void readEntityFromNBT(NBTTagCompound p_70037_1_)
    {
        NBTTagCompound nbttagcompound1 = p_70037_1_.getCompoundTag("Item");

        if (nbttagcompound1 != null && !nbttagcompound1.hasNoTags())
        {
            this.setDisplayedItem(ItemStack.loadItemStackFromNBT(nbttagcompound1));
            this.setItemRotation(p_70037_1_.getByte("ItemRotation"));

            if (p_70037_1_.hasKey("ItemDropChance", 99))
            {
                this.itemDropChance = p_70037_1_.getFloat("ItemDropChance");
            }
        }

        super.readEntityFromNBT(p_70037_1_);
    }

    /**
     * First layer of player interaction
     */
    public boolean interactFirst(EntityPlayer p_130002_1_)
    {
        if (this.getDisplayedItem() == null)
        {
            ItemStack itemstack = p_130002_1_.getHeldItem();

            if (itemstack != null && !this.worldObj.isRemote)
            {
                this.setDisplayedItem(itemstack);

                if (!p_130002_1_.capabilities.isCreativeMode && --itemstack.stackSize <= 0)
                {
                    p_130002_1_.inventory.setInventorySlotContents(p_130002_1_.inventory.currentItem, (ItemStack)null);
                }
            }
        }
        else if (!this.worldObj.isRemote)
        {
            this.setItemRotation(this.getRotation() + 1);
        }

        return true;
    }
}