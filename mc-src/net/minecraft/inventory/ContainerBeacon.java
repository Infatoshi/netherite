package net.minecraft.inventory;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.entity.player.InventoryPlayer;
import net.minecraft.init.Items;
import net.minecraft.item.ItemStack;
import net.minecraft.tileentity.TileEntityBeacon;

public class ContainerBeacon extends Container
{
    private TileEntityBeacon tileBeacon;
    /** This beacon's slot where you put in Emerald, Diamond, Gold or Iron Ingot. */
    private final ContainerBeacon.BeaconSlot beaconSlot;
    private int field_82865_g;
    private int field_82867_h;
    private int field_82868_i;
    private static final String __OBFID = "CL_00001735";

    public ContainerBeacon(InventoryPlayer p_i1802_1_, TileEntityBeacon p_i1802_2_)
    {
        this.tileBeacon = p_i1802_2_;
        this.addSlotToContainer(this.beaconSlot = new ContainerBeacon.BeaconSlot(p_i1802_2_, 0, 136, 110));
        byte b0 = 36;
        short short1 = 137;
        int i;

        for (i = 0; i < 3; ++i)
        {
            for (int j = 0; j < 9; ++j)
            {
                this.addSlotToContainer(new Slot(p_i1802_1_, j + i * 9 + 9, b0 + j * 18, short1 + i * 18));
            }
        }

        for (i = 0; i < 9; ++i)
        {
            this.addSlotToContainer(new Slot(p_i1802_1_, i, b0 + i * 18, 58 + short1));
        }

        this.field_82865_g = p_i1802_2_.getLevels();
        this.field_82867_h = p_i1802_2_.getPrimaryEffect();
        this.field_82868_i = p_i1802_2_.getSecondaryEffect();
    }

    public void addCraftingToCrafters(ICrafting p_75132_1_)
    {
        super.addCraftingToCrafters(p_75132_1_);
        p_75132_1_.sendProgressBarUpdate(this, 0, this.field_82865_g);
        p_75132_1_.sendProgressBarUpdate(this, 1, this.field_82867_h);
        p_75132_1_.sendProgressBarUpdate(this, 2, this.field_82868_i);
    }

    @SideOnly(Side.CLIENT)
    public void updateProgressBar(int p_75137_1_, int p_75137_2_)
    {
        if (p_75137_1_ == 0)
        {
            this.tileBeacon.func_146005_c(p_75137_2_);
        }

        if (p_75137_1_ == 1)
        {
            this.tileBeacon.setPrimaryEffect(p_75137_2_);
        }

        if (p_75137_1_ == 2)
        {
            this.tileBeacon.setSecondaryEffect(p_75137_2_);
        }
    }

    public TileEntityBeacon func_148327_e()
    {
        return this.tileBeacon;
    }

    public boolean canInteractWith(EntityPlayer p_75145_1_)
    {
        return this.tileBeacon.isUseableByPlayer(p_75145_1_);
    }

    /**
     * Called when a player shift-clicks on a slot. You must override this or you will crash when someone does that.
     */
    public ItemStack transferStackInSlot(EntityPlayer p_82846_1_, int p_82846_2_)
    {
        ItemStack itemstack = null;
        Slot slot = (Slot)this.inventorySlots.get(p_82846_2_);

        if (slot != null && slot.getHasStack())
        {
            ItemStack itemstack1 = slot.getStack();
            itemstack = itemstack1.copy();

            if (p_82846_2_ == 0)
            {
                if (!this.mergeItemStack(itemstack1, 1, 37, true))
                {
                    return null;
                }

                slot.onSlotChange(itemstack1, itemstack);
            }
            else if (!this.beaconSlot.getHasStack() && this.beaconSlot.isItemValid(itemstack1) && itemstack1.stackSize == 1)
            {
                if (!this.mergeItemStack(itemstack1, 0, 1, false))
                {
                    return null;
                }
            }
            else if (p_82846_2_ >= 1 && p_82846_2_ < 28)
            {
                if (!this.mergeItemStack(itemstack1, 28, 37, false))
                {
                    return null;
                }
            }
            else if (p_82846_2_ >= 28 && p_82846_2_ < 37)
            {
                if (!this.mergeItemStack(itemstack1, 1, 28, false))
                {
                    return null;
                }
            }
            else if (!this.mergeItemStack(itemstack1, 1, 37, false))
            {
                return null;
            }

            if (itemstack1.stackSize == 0)
            {
                slot.putStack((ItemStack)null);
            }
            else
            {
                slot.onSlotChanged();
            }

            if (itemstack1.stackSize == itemstack.stackSize)
            {
                return null;
            }

            slot.onPickupFromSlot(p_82846_1_, itemstack1);
        }

        return itemstack;
    }

    class BeaconSlot extends Slot
    {
        private static final String __OBFID = "CL_00001736";

        public BeaconSlot(IInventory p_i1801_2_, int p_i1801_3_, int p_i1801_4_, int p_i1801_5_)
        {
            super(p_i1801_2_, p_i1801_3_, p_i1801_4_, p_i1801_5_);
        }

        /**
         * Check if the stack is a valid item for this slot. Always true beside for the armor slots.
         */
        public boolean isItemValid(ItemStack p_75214_1_)
        {
            return p_75214_1_ != null && p_75214_1_.getItem() != null && p_75214_1_.getItem().isBeaconPayment(p_75214_1_);
        }

        /**
         * Returns the maximum stack size for a given slot (usually the same as getInventoryStackLimit(), but 1 in the
         * case of armor slots)
         */
        public int getSlotStackLimit()
        {
            return 1;
        }
    }
}