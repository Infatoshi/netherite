package net.minecraft.inventory;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.client.renderer.texture.TextureMap;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.item.ItemStack;
import net.minecraft.util.IIcon;
import net.minecraft.util.ResourceLocation;

public class Slot
{
    /** The index of the slot in the inventory. */
    private final int slotIndex;
    /** The inventory we want to extract a slot from. */
    public final IInventory inventory;
    /** the id of the slot(also the index in the inventory arraylist) */
    public int slotNumber;
    /** display position of the inventory slot on the screen x axis */
    public int xDisplayPosition;
    /** display position of the inventory slot on the screen y axis */
    public int yDisplayPosition;
    private static final String __OBFID = "CL_00001762";

    /** Position within background texture file, normally -1 which causes no background to be drawn. */
    protected IIcon backgroundIcon = null;

    /** Background texture file assigned to this slot, if any. Vanilla "/gui/items.png" is used if this is null. */
    @SideOnly(Side.CLIENT)
    protected ResourceLocation texture;

    public Slot(IInventory p_i1824_1_, int p_i1824_2_, int p_i1824_3_, int p_i1824_4_)
    {
        this.inventory = p_i1824_1_;
        this.slotIndex = p_i1824_2_;
        this.xDisplayPosition = p_i1824_3_;
        this.yDisplayPosition = p_i1824_4_;
    }

    /**
     * if par2 has more items than par1, onCrafting(item,countIncrease) is called
     */
    public void onSlotChange(ItemStack p_75220_1_, ItemStack p_75220_2_)
    {
        if (p_75220_1_ != null && p_75220_2_ != null)
        {
            if (p_75220_1_.getItem() == p_75220_2_.getItem())
            {
                int i = p_75220_2_.stackSize - p_75220_1_.stackSize;

                if (i > 0)
                {
                    this.onCrafting(p_75220_1_, i);
                }
            }
        }
    }

    /**
     * the itemStack passed in is the output - ie, iron ingots, and pickaxes, not ore and wood. Typically increases an
     * internal count then calls onCrafting(item).
     */
    protected void onCrafting(ItemStack p_75210_1_, int p_75210_2_) {}

    /**
     * the itemStack passed in is the output - ie, iron ingots, and pickaxes, not ore and wood.
     */
    protected void onCrafting(ItemStack p_75208_1_) {}

    public void onPickupFromSlot(EntityPlayer p_82870_1_, ItemStack p_82870_2_)
    {
        this.onSlotChanged();
    }

    /**
     * Check if the stack is a valid item for this slot. Always true beside for the armor slots.
     */
    public boolean isItemValid(ItemStack p_75214_1_)
    {
        return true;
    }

    /**
     * Helper fnct to get the stack in the slot.
     */
    public ItemStack getStack()
    {
        return this.inventory.getStackInSlot(this.slotIndex);
    }

    /**
     * Returns if this slot contains a stack.
     */
    public boolean getHasStack()
    {
        return this.getStack() != null;
    }

    /**
     * Helper method to put a stack in the slot.
     */
    public void putStack(ItemStack p_75215_1_)
    {
        this.inventory.setInventorySlotContents(this.slotIndex, p_75215_1_);
        this.onSlotChanged();
    }

    /**
     * Called when the stack in a Slot changes
     */
    public void onSlotChanged()
    {
        this.inventory.markDirty();
    }

    /**
     * Returns the maximum stack size for a given slot (usually the same as getInventoryStackLimit(), but 1 in the case
     * of armor slots)
     */
    public int getSlotStackLimit()
    {
        return this.inventory.getInventoryStackLimit();
    }

    /**
     * Decrease the size of the stack in slot (first int arg) by the amount of the second int arg. Returns the new
     * stack.
     */
    public ItemStack decrStackSize(int p_75209_1_)
    {
        return this.inventory.decrStackSize(this.slotIndex, p_75209_1_);
    }

    /**
     * returns true if this slot is in par2 of par1
     */
    public boolean isSlotInInventory(IInventory p_75217_1_, int p_75217_2_)
    {
        return p_75217_1_ == this.inventory && p_75217_2_ == this.slotIndex;
    }

    /**
     * Return whether this slot's stack can be taken from this slot.
     */
    public boolean canTakeStack(EntityPlayer p_82869_1_)
    {
        return true;
    }

    /**
     * Returns the icon index on items.png that is used as background image of the slot.
     */
    @SideOnly(Side.CLIENT)
    public IIcon getBackgroundIconIndex()
    {
        return backgroundIcon;
    }

    @SideOnly(Side.CLIENT)
    public boolean func_111238_b()
    {
        return true;
    }

    /*========================================= FORGE START =====================================*/
    /**
     * Gets the path of the texture file to use for the background image of this slot when drawing the GUI.
     * @return String: The texture file that will be used in GuiContainer.drawSlotInventory for the slot background.
     */
    @SideOnly(Side.CLIENT)
    public ResourceLocation getBackgroundIconTexture()
    {
        return (texture == null ? TextureMap.locationItemsTexture : texture);
    }

    /**
     * Sets which icon index to use as the background image of the slot when it's empty.
     * @param icon The icon to use, null for none
     */
    public void setBackgroundIcon(IIcon icon)
    {
        backgroundIcon = icon;
    }

    /**
     * Sets the texture file to use for the background image of the slot when it's empty.
     * @param textureFilename String: Path of texture file to use, or null to use "/gui/items.png"
     */
    @SideOnly(Side.CLIENT)
    public void setBackgroundIconTexture(ResourceLocation texture)
    {
        this.texture = texture;
    }

    /**
     * Retrieves the index in the inventory for this slot, this value should typically not
     * be used, but can be useful for some occasions.
     *
     * @return Index in associated inventory for this slot.
     */
    public int getSlotIndex()
    {
        /** The index of the slot in the inventory. */
        return slotIndex;
    }
    /*========================================= FORGE END =====================================*/
}