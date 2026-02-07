package net.minecraft.inventory;

import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.item.ItemStack;

public class InventoryLargeChest implements IInventory
{
    /** Name of the chest. */
    private String name;
    /** Inventory object corresponding to double chest upper part */
    private IInventory upperChest;
    /** Inventory object corresponding to double chest lower part */
    private IInventory lowerChest;
    private static final String __OBFID = "CL_00001507";

    public InventoryLargeChest(String p_i1559_1_, IInventory p_i1559_2_, IInventory p_i1559_3_)
    {
        this.name = p_i1559_1_;

        if (p_i1559_2_ == null)
        {
            p_i1559_2_ = p_i1559_3_;
        }

        if (p_i1559_3_ == null)
        {
            p_i1559_3_ = p_i1559_2_;
        }

        this.upperChest = p_i1559_2_;
        this.lowerChest = p_i1559_3_;
    }

    /**
     * Returns the number of slots in the inventory.
     */
    public int getSizeInventory()
    {
        return this.upperChest.getSizeInventory() + this.lowerChest.getSizeInventory();
    }

    /**
     * Return whether the given inventory is part of this large chest.
     */
    public boolean isPartOfLargeChest(IInventory p_90010_1_)
    {
        return this.upperChest == p_90010_1_ || this.lowerChest == p_90010_1_;
    }

    /**
     * Returns the name of the inventory
     */
    public String getInventoryName()
    {
        return this.upperChest.hasCustomInventoryName() ? this.upperChest.getInventoryName() : (this.lowerChest.hasCustomInventoryName() ? this.lowerChest.getInventoryName() : this.name);
    }

    /**
     * Returns if the inventory is named
     */
    public boolean hasCustomInventoryName()
    {
        return this.upperChest.hasCustomInventoryName() || this.lowerChest.hasCustomInventoryName();
    }

    /**
     * Returns the stack in slot i
     */
    public ItemStack getStackInSlot(int p_70301_1_)
    {
        return p_70301_1_ >= this.upperChest.getSizeInventory() ? this.lowerChest.getStackInSlot(p_70301_1_ - this.upperChest.getSizeInventory()) : this.upperChest.getStackInSlot(p_70301_1_);
    }

    /**
     * Removes from an inventory slot (first arg) up to a specified number (second arg) of items and returns them in a
     * new stack.
     */
    public ItemStack decrStackSize(int p_70298_1_, int p_70298_2_)
    {
        return p_70298_1_ >= this.upperChest.getSizeInventory() ? this.lowerChest.decrStackSize(p_70298_1_ - this.upperChest.getSizeInventory(), p_70298_2_) : this.upperChest.decrStackSize(p_70298_1_, p_70298_2_);
    }

    /**
     * When some containers are closed they call this on each slot, then drop whatever it returns as an EntityItem -
     * like when you close a workbench GUI.
     */
    public ItemStack getStackInSlotOnClosing(int p_70304_1_)
    {
        return p_70304_1_ >= this.upperChest.getSizeInventory() ? this.lowerChest.getStackInSlotOnClosing(p_70304_1_ - this.upperChest.getSizeInventory()) : this.upperChest.getStackInSlotOnClosing(p_70304_1_);
    }

    /**
     * Sets the given item stack to the specified slot in the inventory (can be crafting or armor sections).
     */
    public void setInventorySlotContents(int p_70299_1_, ItemStack p_70299_2_)
    {
        if (p_70299_1_ >= this.upperChest.getSizeInventory())
        {
            this.lowerChest.setInventorySlotContents(p_70299_1_ - this.upperChest.getSizeInventory(), p_70299_2_);
        }
        else
        {
            this.upperChest.setInventorySlotContents(p_70299_1_, p_70299_2_);
        }
    }

    /**
     * Returns the maximum stack size for a inventory slot.
     */
    public int getInventoryStackLimit()
    {
        return this.upperChest.getInventoryStackLimit();
    }

    /**
     * For tile entities, ensures the chunk containing the tile entity is saved to disk later - the game won't think it
     * hasn't changed and skip it.
     */
    public void markDirty()
    {
        this.upperChest.markDirty();
        this.lowerChest.markDirty();
    }

    /**
     * Do not make give this method the name canInteractWith because it clashes with Container
     */
    public boolean isUseableByPlayer(EntityPlayer p_70300_1_)
    {
        return this.upperChest.isUseableByPlayer(p_70300_1_) && this.lowerChest.isUseableByPlayer(p_70300_1_);
    }

    public void openInventory()
    {
        this.upperChest.openInventory();
        this.lowerChest.openInventory();
    }

    public void closeInventory()
    {
        this.upperChest.closeInventory();
        this.lowerChest.closeInventory();
    }

    /**
     * Returns true if automation is allowed to insert the given stack (ignoring stack size) into the given slot.
     */
    public boolean isItemValidForSlot(int p_94041_1_, ItemStack p_94041_2_)
    {
        return true;
    }
}