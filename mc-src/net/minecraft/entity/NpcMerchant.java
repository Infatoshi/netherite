package net.minecraft.entity;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.inventory.InventoryMerchant;
import net.minecraft.item.ItemStack;
import net.minecraft.village.MerchantRecipe;
import net.minecraft.village.MerchantRecipeList;

@SideOnly(Side.CLIENT)
public class NpcMerchant implements IMerchant
{
    /** Instance of Merchants Inventory. */
    private InventoryMerchant theMerchantInventory;
    /** This merchant's current player customer. */
    private EntityPlayer customer;
    /** The MerchantRecipeList instance. */
    private MerchantRecipeList recipeList;
    private static final String __OBFID = "CL_00001705";

    public NpcMerchant(EntityPlayer p_i1746_1_)
    {
        this.customer = p_i1746_1_;
        this.theMerchantInventory = new InventoryMerchant(p_i1746_1_, this);
    }

    public EntityPlayer getCustomer()
    {
        return this.customer;
    }

    public void setCustomer(EntityPlayer p_70932_1_) {}

    public MerchantRecipeList getRecipes(EntityPlayer p_70934_1_)
    {
        return this.recipeList;
    }

    public void setRecipes(MerchantRecipeList p_70930_1_)
    {
        this.recipeList = p_70930_1_;
    }

    public void useRecipe(MerchantRecipe p_70933_1_) {}

    public void func_110297_a_(ItemStack p_110297_1_) {}
}