package net.minecraft.entity.player;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.concurrent.Callable;
import net.minecraft.block.Block;
import net.minecraft.crash.CrashReport;
import net.minecraft.crash.CrashReportCategory;
import net.minecraft.inventory.IInventory;
import net.minecraft.item.Item;
import net.minecraft.item.ItemArmor;
import net.minecraft.item.ItemStack;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.nbt.NBTTagList;
import net.minecraft.util.ReportedException;

public class InventoryPlayer implements IInventory
{
    /** An array of 36 item stacks indicating the main player inventory (including the visible bar). */
    public ItemStack[] mainInventory = new ItemStack[36];
    /** An array of 4 item stacks containing the currently worn armor pieces. */
    public ItemStack[] armorInventory = new ItemStack[4];
    /** The index of the currently held item (0-8). */
    public int currentItem;
    /** The current ItemStack. */
    @SideOnly(Side.CLIENT)
    private ItemStack currentItemStack;
    /** The player whose inventory this is. */
    public EntityPlayer player;
    private ItemStack itemStack;
    /**
     * Set true whenever the inventory changes. Nothing sets it false so you will have to write your own code to check
     * it and reset the value.
     */
    public boolean inventoryChanged;
    private static final String __OBFID = "CL_00001709";

    public InventoryPlayer(EntityPlayer p_i1750_1_)
    {
        this.player = p_i1750_1_;
    }

    /**
     * Returns the item stack currently held by the player.
     */
    public ItemStack getCurrentItem()
    {
        return this.currentItem < 9 && this.currentItem >= 0 ? this.mainInventory[this.currentItem] : null;
    }

    /**
     * Get the size of the player hotbar inventory
     */
    public static int getHotbarSize()
    {
        return 9;
    }

    private int func_146029_c(Item p_146029_1_)
    {
        for (int i = 0; i < this.mainInventory.length; ++i)
        {
            if (this.mainInventory[i] != null && this.mainInventory[i].getItem() == p_146029_1_)
            {
                return i;
            }
        }

        return -1;
    }

    @SideOnly(Side.CLIENT)
    private int func_146024_c(Item p_146024_1_, int p_146024_2_)
    {
        for (int j = 0; j < this.mainInventory.length; ++j)
        {
            if (this.mainInventory[j] != null && this.mainInventory[j].getItem() == p_146024_1_ && this.mainInventory[j].getItemDamage() == p_146024_2_)
            {
                return j;
            }
        }

        return -1;
    }

    /**
     * stores an itemstack in the users inventory
     */
    private int storeItemStack(ItemStack p_70432_1_)
    {
        for (int i = 0; i < this.mainInventory.length; ++i)
        {
            if (this.mainInventory[i] != null && this.mainInventory[i].getItem() == p_70432_1_.getItem() && this.mainInventory[i].isStackable() && this.mainInventory[i].stackSize < this.mainInventory[i].getMaxStackSize() && this.mainInventory[i].stackSize < this.getInventoryStackLimit() && (!this.mainInventory[i].getHasSubtypes() || this.mainInventory[i].getItemDamage() == p_70432_1_.getItemDamage()) && ItemStack.areItemStackTagsEqual(this.mainInventory[i], p_70432_1_))
            {
                return i;
            }
        }

        return -1;
    }

    /**
     * Returns the first item stack that is empty.
     */
    public int getFirstEmptyStack()
    {
        for (int i = 0; i < this.mainInventory.length; ++i)
        {
            if (this.mainInventory[i] == null)
            {
                return i;
            }
        }

        return -1;
    }

    @SideOnly(Side.CLIENT)
    public void func_146030_a(Item p_146030_1_, int p_146030_2_, boolean p_146030_3_, boolean p_146030_4_)
    {
        boolean flag2 = true;
        this.currentItemStack = this.getCurrentItem();
        int k;

        if (p_146030_3_)
        {
            k = this.func_146024_c(p_146030_1_, p_146030_2_);
        }
        else
        {
            k = this.func_146029_c(p_146030_1_);
        }

        if (k >= 0 && k < 9)
        {
            this.currentItem = k;
        }
        else
        {
            if (p_146030_4_ && p_146030_1_ != null)
            {
                int j = this.getFirstEmptyStack();

                if (j >= 0 && j < 9)
                {
                    this.currentItem = j;
                }

                this.func_70439_a(p_146030_1_, p_146030_2_);
            }
        }
    }

    /**
     * Clear this player's inventory (including armor), using the specified Item and metadata as filters or -1 for no
     * filter.
     */
    public int clearInventory(Item p_146027_1_, int p_146027_2_)
    {
        int j = 0;
        int k;
        ItemStack itemstack;

        for (k = 0; k < this.mainInventory.length; ++k)
        {
            itemstack = this.mainInventory[k];

            if (itemstack != null && (p_146027_1_ == null || itemstack.getItem() == p_146027_1_) && (p_146027_2_ <= -1 || itemstack.getItemDamage() == p_146027_2_))
            {
                j += itemstack.stackSize;
                this.mainInventory[k] = null;
            }
        }

        for (k = 0; k < this.armorInventory.length; ++k)
        {
            itemstack = this.armorInventory[k];

            if (itemstack != null && (p_146027_1_ == null || itemstack.getItem() == p_146027_1_) && (p_146027_2_ <= -1 || itemstack.getItemDamage() == p_146027_2_))
            {
                j += itemstack.stackSize;
                this.armorInventory[k] = null;
            }
        }

        if (this.itemStack != null)
        {
            if (p_146027_1_ != null && this.itemStack.getItem() != p_146027_1_)
            {
                return j;
            }

            if (p_146027_2_ > -1 && this.itemStack.getItemDamage() != p_146027_2_)
            {
                return j;
            }

            j += this.itemStack.stackSize;
            this.setItemStack((ItemStack)null);
        }

        return j;
    }

    /**
     * Switch the current item to the next one or the previous one
     */
    @SideOnly(Side.CLIENT)
    public void changeCurrentItem(int p_70453_1_)
    {
        if (p_70453_1_ > 0)
        {
            p_70453_1_ = 1;
        }

        if (p_70453_1_ < 0)
        {
            p_70453_1_ = -1;
        }

        for (this.currentItem -= p_70453_1_; this.currentItem < 0; this.currentItem += 9)
        {
            ;
        }

        while (this.currentItem >= 9)
        {
            this.currentItem -= 9;
        }
    }

    @SideOnly(Side.CLIENT)
    public void func_70439_a(Item p_70439_1_, int p_70439_2_)
    {
        if (p_70439_1_ != null)
        {
            if (this.currentItemStack != null && this.currentItemStack.isItemEnchantable() && this.func_146024_c(this.currentItemStack.getItem(), this.currentItemStack.getItemDamageForDisplay()) == this.currentItem)
            {
                return;
            }

            int j = this.func_146024_c(p_70439_1_, p_70439_2_);

            if (j >= 0)
            {
                int k = this.mainInventory[j].stackSize;
                this.mainInventory[j] = this.mainInventory[this.currentItem];
                this.mainInventory[this.currentItem] = new ItemStack(p_70439_1_, k, p_70439_2_);
            }
            else
            {
                this.mainInventory[this.currentItem] = new ItemStack(p_70439_1_, 1, p_70439_2_);
            }
        }
    }

    /**
     * This function stores as many items of an ItemStack as possible in a matching slot and returns the quantity of
     * left over items.
     */
    private int storePartialItemStack(ItemStack p_70452_1_)
    {
        Item item = p_70452_1_.getItem();
        int i = p_70452_1_.stackSize;
        int j;

        if (p_70452_1_.getMaxStackSize() == 1)
        {
            j = this.getFirstEmptyStack();

            if (j < 0)
            {
                return i;
            }
            else
            {
                if (this.mainInventory[j] == null)
                {
                    this.mainInventory[j] = ItemStack.copyItemStack(p_70452_1_);
                }

                return 0;
            }
        }
        else
        {
            j = this.storeItemStack(p_70452_1_);

            if (j < 0)
            {
                j = this.getFirstEmptyStack();
            }

            if (j < 0)
            {
                return i;
            }
            else
            {
                if (this.mainInventory[j] == null)
                {
                    this.mainInventory[j] = new ItemStack(item, 0, p_70452_1_.getItemDamage());

                    if (p_70452_1_.hasTagCompound())
                    {
                        this.mainInventory[j].setTagCompound((NBTTagCompound)p_70452_1_.getTagCompound().copy());
                    }
                }

                int k = i;

                if (i > this.mainInventory[j].getMaxStackSize() - this.mainInventory[j].stackSize)
                {
                    k = this.mainInventory[j].getMaxStackSize() - this.mainInventory[j].stackSize;
                }

                if (k > this.getInventoryStackLimit() - this.mainInventory[j].stackSize)
                {
                    k = this.getInventoryStackLimit() - this.mainInventory[j].stackSize;
                }

                if (k == 0)
                {
                    return i;
                }
                else
                {
                    i -= k;
                    this.mainInventory[j].stackSize += k;
                    this.mainInventory[j].animationsToGo = 5;
                    return i;
                }
            }
        }
    }

    /**
     * Decrement the number of animations remaining. Only called on client side. This is used to handle the animation of
     * receiving a block.
     */
    public void decrementAnimations()
    {
        for (int i = 0; i < this.mainInventory.length; ++i)
        {
            if (this.mainInventory[i] != null)
            {
                this.mainInventory[i].updateAnimation(this.player.worldObj, this.player, i, this.currentItem == i);
            }
        }

        for (int i = 0; i < armorInventory.length; i++)
        {
            if (armorInventory[i] != null)
            {
                armorInventory[i].getItem().onArmorTick(player.worldObj, player, armorInventory[i]);
            }
        }
    }

    /**
     * removed one item of specified Item from inventory (if it is in a stack, the stack size will reduce with 1)
     */
    public boolean consumeInventoryItem(Item p_146026_1_)
    {
        int i = this.func_146029_c(p_146026_1_);

        if (i < 0)
        {
            return false;
        }
        else
        {
            if (--this.mainInventory[i].stackSize <= 0)
            {
                this.mainInventory[i] = null;
            }

            return true;
        }
    }

    /**
     * Checks if a specified Item is inside the inventory
     */
    public boolean hasItem(Item p_146028_1_)
    {
        int i = this.func_146029_c(p_146028_1_);
        return i >= 0;
    }

    /**
     * Adds the item stack to the inventory, returns false if it is impossible.
     */
    public boolean addItemStackToInventory(final ItemStack p_70441_1_)
    {
        if (p_70441_1_ != null && p_70441_1_.stackSize != 0 && p_70441_1_.getItem() != null)
        {
            try
            {
                int i;

                if (p_70441_1_.isItemDamaged())
                {
                    i = this.getFirstEmptyStack();

                    if (i >= 0)
                    {
                        this.mainInventory[i] = ItemStack.copyItemStack(p_70441_1_);
                        this.mainInventory[i].animationsToGo = 5;
                        p_70441_1_.stackSize = 0;
                        return true;
                    }
                    else if (this.player.capabilities.isCreativeMode)
                    {
                        p_70441_1_.stackSize = 0;
                        return true;
                    }
                    else
                    {
                        return false;
                    }
                }
                else
                {
                    do
                    {
                        i = p_70441_1_.stackSize;
                        p_70441_1_.stackSize = this.storePartialItemStack(p_70441_1_);
                    }
                    while (p_70441_1_.stackSize > 0 && p_70441_1_.stackSize < i);

                    if (p_70441_1_.stackSize == i && this.player.capabilities.isCreativeMode)
                    {
                        p_70441_1_.stackSize = 0;
                        return true;
                    }
                    else
                    {
                        return p_70441_1_.stackSize < i;
                    }
                }
            }
            catch (Throwable throwable)
            {
                CrashReport crashreport = CrashReport.makeCrashReport(throwable, "Adding item to inventory");
                CrashReportCategory crashreportcategory = crashreport.makeCategory("Item being added");
                crashreportcategory.addCrashSection("Item ID", Integer.valueOf(Item.getIdFromItem(p_70441_1_.getItem())));
                crashreportcategory.addCrashSection("Item data", Integer.valueOf(p_70441_1_.getItemDamage()));
                crashreportcategory.addCrashSectionCallable("Item name", new Callable()
                {
                    private static final String __OBFID = "CL_00001710";
                    public String call()
                    {
                        return p_70441_1_.getDisplayName();
                    }
                });
                throw new ReportedException(crashreport);
            }
        }
        else
        {
            return false;
        }
    }

    /**
     * Removes from an inventory slot (first arg) up to a specified number (second arg) of items and returns them in a
     * new stack.
     */
    public ItemStack decrStackSize(int p_70298_1_, int p_70298_2_)
    {
        ItemStack[] aitemstack = this.mainInventory;

        if (p_70298_1_ >= this.mainInventory.length)
        {
            aitemstack = this.armorInventory;
            p_70298_1_ -= this.mainInventory.length;
        }

        if (aitemstack[p_70298_1_] != null)
        {
            ItemStack itemstack;

            if (aitemstack[p_70298_1_].stackSize <= p_70298_2_)
            {
                itemstack = aitemstack[p_70298_1_];
                aitemstack[p_70298_1_] = null;
                return itemstack;
            }
            else
            {
                itemstack = aitemstack[p_70298_1_].splitStack(p_70298_2_);

                if (aitemstack[p_70298_1_].stackSize == 0)
                {
                    aitemstack[p_70298_1_] = null;
                }

                return itemstack;
            }
        }
        else
        {
            return null;
        }
    }

    /**
     * When some containers are closed they call this on each slot, then drop whatever it returns as an EntityItem -
     * like when you close a workbench GUI.
     */
    public ItemStack getStackInSlotOnClosing(int p_70304_1_)
    {
        ItemStack[] aitemstack = this.mainInventory;

        if (p_70304_1_ >= this.mainInventory.length)
        {
            aitemstack = this.armorInventory;
            p_70304_1_ -= this.mainInventory.length;
        }

        if (aitemstack[p_70304_1_] != null)
        {
            ItemStack itemstack = aitemstack[p_70304_1_];
            aitemstack[p_70304_1_] = null;
            return itemstack;
        }
        else
        {
            return null;
        }
    }

    /**
     * Sets the given item stack to the specified slot in the inventory (can be crafting or armor sections).
     */
    public void setInventorySlotContents(int p_70299_1_, ItemStack p_70299_2_)
    {
        ItemStack[] aitemstack = this.mainInventory;

        if (p_70299_1_ >= aitemstack.length)
        {
            p_70299_1_ -= aitemstack.length;
            aitemstack = this.armorInventory;
        }

        aitemstack[p_70299_1_] = p_70299_2_;
    }

    public float func_146023_a(Block p_146023_1_)
    {
        float f = 1.0F;

        if (this.mainInventory[this.currentItem] != null)
        {
            f *= this.mainInventory[this.currentItem].func_150997_a(p_146023_1_);
        }

        return f;
    }

    /**
     * Writes the inventory out as a list of compound tags. This is where the slot indices are used (+100 for armor, +80
     * for crafting).
     */
    public NBTTagList writeToNBT(NBTTagList p_70442_1_)
    {
        int i;
        NBTTagCompound nbttagcompound;

        for (i = 0; i < this.mainInventory.length; ++i)
        {
            if (this.mainInventory[i] != null)
            {
                nbttagcompound = new NBTTagCompound();
                nbttagcompound.setByte("Slot", (byte)i);
                this.mainInventory[i].writeToNBT(nbttagcompound);
                p_70442_1_.appendTag(nbttagcompound);
            }
        }

        for (i = 0; i < this.armorInventory.length; ++i)
        {
            if (this.armorInventory[i] != null)
            {
                nbttagcompound = new NBTTagCompound();
                nbttagcompound.setByte("Slot", (byte)(i + 100));
                this.armorInventory[i].writeToNBT(nbttagcompound);
                p_70442_1_.appendTag(nbttagcompound);
            }
        }

        return p_70442_1_;
    }

    /**
     * Reads from the given tag list and fills the slots in the inventory with the correct items.
     */
    public void readFromNBT(NBTTagList p_70443_1_)
    {
        this.mainInventory = new ItemStack[36];
        this.armorInventory = new ItemStack[4];

        for (int i = 0; i < p_70443_1_.tagCount(); ++i)
        {
            NBTTagCompound nbttagcompound = p_70443_1_.getCompoundTagAt(i);
            int j = nbttagcompound.getByte("Slot") & 255;
            ItemStack itemstack = ItemStack.loadItemStackFromNBT(nbttagcompound);

            if (itemstack != null)
            {
                if (j >= 0 && j < this.mainInventory.length)
                {
                    this.mainInventory[j] = itemstack;
                }

                if (j >= 100 && j < this.armorInventory.length + 100)
                {
                    this.armorInventory[j - 100] = itemstack;
                }
            }
        }
    }

    /**
     * Returns the number of slots in the inventory.
     */
    public int getSizeInventory()
    {
        return this.mainInventory.length + 4;
    }

    /**
     * Returns the stack in slot i
     */
    public ItemStack getStackInSlot(int p_70301_1_)
    {
        ItemStack[] aitemstack = this.mainInventory;

        if (p_70301_1_ >= aitemstack.length)
        {
            p_70301_1_ -= aitemstack.length;
            aitemstack = this.armorInventory;
        }

        return aitemstack[p_70301_1_];
    }

    /**
     * Returns the name of the inventory
     */
    public String getInventoryName()
    {
        return "container.inventory";
    }

    /**
     * Returns if the inventory is named
     */
    public boolean hasCustomInventoryName()
    {
        return false;
    }

    /**
     * Returns the maximum stack size for a inventory slot.
     */
    public int getInventoryStackLimit()
    {
        return 64;
    }

    public boolean func_146025_b(Block p_146025_1_)
    {
        if (p_146025_1_.getMaterial().isToolNotRequired())
        {
            return true;
        }
        else
        {
            ItemStack itemstack = this.getStackInSlot(this.currentItem);
            return itemstack != null ? itemstack.func_150998_b(p_146025_1_) : false;
        }
    }

    /**
     * returns a player armor item (as itemstack) contained in specified armor slot.
     */
    public ItemStack armorItemInSlot(int p_70440_1_)
    {
        return this.armorInventory[p_70440_1_];
    }

    /**
     * Based on the damage values and maximum damage values of each armor item, returns the current armor value.
     */
    public int getTotalArmorValue()
    {
        int i = 0;

        for (int j = 0; j < this.armorInventory.length; ++j)
        {
            if (this.armorInventory[j] != null && this.armorInventory[j].getItem() instanceof ItemArmor)
            {
                int k = ((ItemArmor)this.armorInventory[j].getItem()).damageReduceAmount;
                i += k;
            }
        }

        return i;
    }

    /**
     * Damages armor in each slot by the specified amount.
     */
    public void damageArmor(float p_70449_1_)
    {
        p_70449_1_ /= 4.0F;

        if (p_70449_1_ < 1.0F)
        {
            p_70449_1_ = 1.0F;
        }

        for (int i = 0; i < this.armorInventory.length; ++i)
        {
            if (this.armorInventory[i] != null && this.armorInventory[i].getItem() instanceof ItemArmor)
            {
                this.armorInventory[i].damageItem((int)p_70449_1_, this.player);

                if (this.armorInventory[i].stackSize == 0)
                {
                    this.armorInventory[i] = null;
                }
            }
        }
    }

    /**
     * Drop all armor and main inventory items.
     */
    public void dropAllItems()
    {
        int i;

        for (i = 0; i < this.mainInventory.length; ++i)
        {
            if (this.mainInventory[i] != null)
            {
                this.player.func_146097_a(this.mainInventory[i], true, false);
                this.mainInventory[i] = null;
            }
        }

        for (i = 0; i < this.armorInventory.length; ++i)
        {
            if (this.armorInventory[i] != null)
            {
                this.player.func_146097_a(this.armorInventory[i], true, false);
                this.armorInventory[i] = null;
            }
        }
    }

    /**
     * For tile entities, ensures the chunk containing the tile entity is saved to disk later - the game won't think it
     * hasn't changed and skip it.
     */
    public void markDirty()
    {
        this.inventoryChanged = true;
    }

    public void setItemStack(ItemStack p_70437_1_)
    {
        this.itemStack = p_70437_1_;
    }

    public ItemStack getItemStack()
    {
        return this.itemStack;
    }

    /**
     * Do not make give this method the name canInteractWith because it clashes with Container
     */
    public boolean isUseableByPlayer(EntityPlayer p_70300_1_)
    {
        return this.player.isDead ? false : p_70300_1_.getDistanceSqToEntity(this.player) <= 64.0D;
    }

    /**
     * Returns true if the specified ItemStack exists in the inventory.
     */
    public boolean hasItemStack(ItemStack p_70431_1_)
    {
        int i;

        for (i = 0; i < this.armorInventory.length; ++i)
        {
            if (this.armorInventory[i] != null && this.armorInventory[i].isItemEqual(p_70431_1_))
            {
                return true;
            }
        }

        for (i = 0; i < this.mainInventory.length; ++i)
        {
            if (this.mainInventory[i] != null && this.mainInventory[i].isItemEqual(p_70431_1_))
            {
                return true;
            }
        }

        return false;
    }

    public void openInventory() {}

    public void closeInventory() {}

    /**
     * Returns true if automation is allowed to insert the given stack (ignoring stack size) into the given slot.
     */
    public boolean isItemValidForSlot(int p_94041_1_, ItemStack p_94041_2_)
    {
        return true;
    }

    /**
     * Copy the ItemStack contents from another InventoryPlayer instance
     */
    public void copyInventory(InventoryPlayer p_70455_1_)
    {
        int i;

        for (i = 0; i < this.mainInventory.length; ++i)
        {
            this.mainInventory[i] = ItemStack.copyItemStack(p_70455_1_.mainInventory[i]);
        }

        for (i = 0; i < this.armorInventory.length; ++i)
        {
            this.armorInventory[i] = ItemStack.copyItemStack(p_70455_1_.armorInventory[i]);
        }

        this.currentItem = p_70455_1_.currentItem;
    }
}