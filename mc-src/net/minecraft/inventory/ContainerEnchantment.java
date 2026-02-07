package net.minecraft.inventory;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.List;
import java.util.Random;
import net.minecraft.enchantment.EnchantmentData;
import net.minecraft.enchantment.EnchantmentHelper;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.entity.player.InventoryPlayer;
import net.minecraft.init.Blocks;
import net.minecraft.init.Items;
import net.minecraft.item.ItemStack;
import net.minecraft.world.World;
import net.minecraftforge.common.ForgeHooks;

public class ContainerEnchantment extends Container
{
    /** SlotEnchantmentTable object with ItemStack to be enchanted */
    public IInventory tableInventory = new InventoryBasic("Enchant", true, 1)
    {
        private static final String __OBFID = "CL_00001746";
        /**
         * Returns the maximum stack size for a inventory slot.
         */
        public int getInventoryStackLimit()
        {
            return 1;
        }
        /**
         * For tile entities, ensures the chunk containing the tile entity is saved to disk later - the game won't think
         * it hasn't changed and skip it.
         */
        public void markDirty()
        {
            super.markDirty();
            ContainerEnchantment.this.onCraftMatrixChanged(this);
        }
    };
    /** current world (for bookshelf counting) */
    private World worldPointer;
    private int posX;
    private int posY;
    private int posZ;
    private Random rand = new Random();
    /** used as seed for EnchantmentNameParts (see GuiEnchantment) */
    public long nameSeed;
    /** 3-member array storing the enchantment levels of each slot */
    public int[] enchantLevels = new int[3];
    private static final String __OBFID = "CL_00001745";

    public ContainerEnchantment(InventoryPlayer p_i1811_1_, World p_i1811_2_, int p_i1811_3_, int p_i1811_4_, int p_i1811_5_)
    {
        this.worldPointer = p_i1811_2_;
        this.posX = p_i1811_3_;
        this.posY = p_i1811_4_;
        this.posZ = p_i1811_5_;
        this.addSlotToContainer(new Slot(this.tableInventory, 0, 25, 47)
        {
            private static final String __OBFID = "CL_00001747";
            /**
             * Check if the stack is a valid item for this slot. Always true beside for the armor slots.
             */
            public boolean isItemValid(ItemStack p_75214_1_)
            {
                return true;
            }
        });
        int l;

        for (l = 0; l < 3; ++l)
        {
            for (int i1 = 0; i1 < 9; ++i1)
            {
                this.addSlotToContainer(new Slot(p_i1811_1_, i1 + l * 9 + 9, 8 + i1 * 18, 84 + l * 18));
            }
        }

        for (l = 0; l < 9; ++l)
        {
            this.addSlotToContainer(new Slot(p_i1811_1_, l, 8 + l * 18, 142));
        }
    }

    public void addCraftingToCrafters(ICrafting p_75132_1_)
    {
        super.addCraftingToCrafters(p_75132_1_);
        p_75132_1_.sendProgressBarUpdate(this, 0, this.enchantLevels[0]);
        p_75132_1_.sendProgressBarUpdate(this, 1, this.enchantLevels[1]);
        p_75132_1_.sendProgressBarUpdate(this, 2, this.enchantLevels[2]);
    }

    /**
     * Looks for changes made in the container, sends them to every listener.
     */
    public void detectAndSendChanges()
    {
        super.detectAndSendChanges();

        for (int i = 0; i < this.crafters.size(); ++i)
        {
            ICrafting icrafting = (ICrafting)this.crafters.get(i);
            icrafting.sendProgressBarUpdate(this, 0, this.enchantLevels[0]);
            icrafting.sendProgressBarUpdate(this, 1, this.enchantLevels[1]);
            icrafting.sendProgressBarUpdate(this, 2, this.enchantLevels[2]);
        }
    }

    @SideOnly(Side.CLIENT)
    public void updateProgressBar(int p_75137_1_, int p_75137_2_)
    {
        if (p_75137_1_ >= 0 && p_75137_1_ <= 2)
        {
            this.enchantLevels[p_75137_1_] = p_75137_2_;
        }
        else
        {
            super.updateProgressBar(p_75137_1_, p_75137_2_);
        }
    }

    /**
     * Callback for when the crafting matrix is changed.
     */
    public void onCraftMatrixChanged(IInventory p_75130_1_)
    {
        if (p_75130_1_ == this.tableInventory)
        {
            ItemStack itemstack = p_75130_1_.getStackInSlot(0);
            int i;

            if (itemstack != null && itemstack.isItemEnchantable())
            {
                this.nameSeed = this.rand.nextLong();

                if (!this.worldPointer.isRemote)
                {
                    i = 0;
                    int j;
                    float power = 0;

                    for (j = -1; j <= 1; ++j)
                    {
                        for (int k = -1; k <= 1; ++k)
                        {
                            if ((j != 0 || k != 0) && this.worldPointer.isAirBlock(this.posX + k, this.posY, this.posZ + j) && this.worldPointer.isAirBlock(this.posX + k, this.posY + 1, this.posZ + j))
                            {
                                power += ForgeHooks.getEnchantPower(worldPointer, posX + k * 2, posY,     posZ + j * 2);
                                power += ForgeHooks.getEnchantPower(worldPointer, posX + k * 2, posY + 1, posZ + j * 2);

                                if (k != 0 && j != 0)
                                {
                                    power += ForgeHooks.getEnchantPower(worldPointer, posX + k * 2, posY,     posZ + j    );
                                    power += ForgeHooks.getEnchantPower(worldPointer, posX + k * 2, posY + 1, posZ + j    );
                                    power += ForgeHooks.getEnchantPower(worldPointer, posX + k,     posY,     posZ + j * 2);
                                    power += ForgeHooks.getEnchantPower(worldPointer, posX + k,     posY + 1, posZ + j * 2);
                                }
                            }
                        }
                    }

                    for (j = 0; j < 3; ++j)
                    {
                        this.enchantLevels[j] = EnchantmentHelper.calcItemStackEnchantability(this.rand, j, (int)power, itemstack);
                    }

                    this.detectAndSendChanges();
                }
            }
            else
            {
                for (i = 0; i < 3; ++i)
                {
                    this.enchantLevels[i] = 0;
                }
            }
        }
    }

    /**
     * enchants the item on the table using the specified slot; also deducts XP from player
     */
    public boolean enchantItem(EntityPlayer p_75140_1_, int p_75140_2_)
    {
        ItemStack itemstack = this.tableInventory.getStackInSlot(0);

        if (this.enchantLevels[p_75140_2_] > 0 && itemstack != null && (p_75140_1_.experienceLevel >= this.enchantLevels[p_75140_2_] || p_75140_1_.capabilities.isCreativeMode))
        {
            if (!this.worldPointer.isRemote)
            {
                List list = EnchantmentHelper.buildEnchantmentList(this.rand, itemstack, this.enchantLevels[p_75140_2_]);
                boolean flag = itemstack.getItem() == Items.book;

                if (list != null)
                {
                    p_75140_1_.addExperienceLevel(-this.enchantLevels[p_75140_2_]);

                    if (flag)
                    {
                        itemstack.func_150996_a(Items.enchanted_book);
                    }

                    int j = flag && list.size() > 1 ? this.rand.nextInt(list.size()) : -1;

                    for (int k = 0; k < list.size(); ++k)
                    {
                        EnchantmentData enchantmentdata = (EnchantmentData)list.get(k);

                        if (!flag || k != j)
                        {
                            if (flag)
                            {
                                Items.enchanted_book.addEnchantment(itemstack, enchantmentdata);
                            }
                            else
                            {
                                itemstack.addEnchantment(enchantmentdata.enchantmentobj, enchantmentdata.enchantmentLevel);
                            }
                        }
                    }

                    this.onCraftMatrixChanged(this.tableInventory);
                }
            }

            return true;
        }
        else
        {
            return false;
        }
    }

    /**
     * Called when the container is closed.
     */
    public void onContainerClosed(EntityPlayer p_75134_1_)
    {
        super.onContainerClosed(p_75134_1_);

        if (!this.worldPointer.isRemote)
        {
            ItemStack itemstack = this.tableInventory.getStackInSlotOnClosing(0);

            if (itemstack != null)
            {
                p_75134_1_.dropPlayerItemWithRandomChoice(itemstack, false);
            }
        }
    }

    public boolean canInteractWith(EntityPlayer p_75145_1_)
    {
        return this.worldPointer.getBlock(this.posX, this.posY, this.posZ) != Blocks.enchanting_table ? false : p_75145_1_.getDistanceSq((double)this.posX + 0.5D, (double)this.posY + 0.5D, (double)this.posZ + 0.5D) <= 64.0D;
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
            }
            else
            {
                if (((Slot)this.inventorySlots.get(0)).getHasStack() || !((Slot)this.inventorySlots.get(0)).isItemValid(itemstack1))
                {
                    return null;
                }

                if (itemstack1.hasTagCompound() && itemstack1.stackSize == 1)
                {
                    ((Slot)this.inventorySlots.get(0)).putStack(itemstack1.copy());
                    itemstack1.stackSize = 0;
                }
                else if (itemstack1.stackSize >= 1)
                {
                    ((Slot)this.inventorySlots.get(0)).putStack(new ItemStack(itemstack1.getItem(), 1, itemstack1.getItemDamage()));
                    --itemstack1.stackSize;
                }
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
}