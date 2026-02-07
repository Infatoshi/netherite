package net.minecraft.item;

import com.google.common.collect.HashMultimap;
import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import net.minecraft.client.renderer.texture.IIconRegister;
import net.minecraft.creativetab.CreativeTabs;
import net.minecraft.entity.ai.attributes.AttributeModifier;
import net.minecraft.entity.ai.attributes.IAttribute;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.entity.projectile.EntityPotion;
import net.minecraft.init.Items;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.nbt.NBTTagList;
import net.minecraft.potion.Potion;
import net.minecraft.potion.PotionEffect;
import net.minecraft.potion.PotionHelper;
import net.minecraft.util.EnumChatFormatting;
import net.minecraft.util.IIcon;
import net.minecraft.util.StatCollector;
import net.minecraft.world.World;

public class ItemPotion extends Item
{
    /**
     * Contains a map from integers to the list of potion effects that potions with that damage value confer (to prevent
     * recalculating it).
     */
    private HashMap effectCache = new HashMap();
    private static final Map field_77835_b = new LinkedHashMap();
    @SideOnly(Side.CLIENT)
    private IIcon field_94591_c;
    @SideOnly(Side.CLIENT)
    private IIcon field_94590_d;
    @SideOnly(Side.CLIENT)
    private IIcon field_94592_ct;
    private static final String __OBFID = "CL_00000055";

    public ItemPotion()
    {
        this.setMaxStackSize(1);
        this.setHasSubtypes(true);
        this.setMaxDamage(0);
        this.setCreativeTab(CreativeTabs.tabBrewing);
    }

    /**
     * Returns a list of potion effects for the specified itemstack.
     */
    public List getEffects(ItemStack p_77832_1_)
    {
        if (p_77832_1_.hasTagCompound() && p_77832_1_.getTagCompound().hasKey("CustomPotionEffects", 9))
        {
            ArrayList arraylist = new ArrayList();
            NBTTagList nbttaglist = p_77832_1_.getTagCompound().getTagList("CustomPotionEffects", 10);

            for (int i = 0; i < nbttaglist.tagCount(); ++i)
            {
                NBTTagCompound nbttagcompound = nbttaglist.getCompoundTagAt(i);
                PotionEffect potioneffect = PotionEffect.readCustomPotionEffectFromNBT(nbttagcompound);

                if (potioneffect != null)
                {
                    arraylist.add(potioneffect);
                }
            }

            return arraylist;
        }
        else
        {
            List list = (List)this.effectCache.get(Integer.valueOf(p_77832_1_.getItemDamage()));

            if (list == null)
            {
                list = PotionHelper.getPotionEffects(p_77832_1_.getItemDamage(), false);
                this.effectCache.put(Integer.valueOf(p_77832_1_.getItemDamage()), list);
            }

            return list;
        }
    }

    /**
     * Returns a list of effects for the specified potion damage value.
     */
    public List getEffects(int p_77834_1_)
    {
        List list = (List)this.effectCache.get(Integer.valueOf(p_77834_1_));

        if (list == null)
        {
            list = PotionHelper.getPotionEffects(p_77834_1_, false);
            this.effectCache.put(Integer.valueOf(p_77834_1_), list);
        }

        return list;
    }

    public ItemStack onEaten(ItemStack p_77654_1_, World p_77654_2_, EntityPlayer p_77654_3_)
    {
        if (!p_77654_3_.capabilities.isCreativeMode)
        {
            --p_77654_1_.stackSize;
        }

        if (!p_77654_2_.isRemote)
        {
            List list = this.getEffects(p_77654_1_);

            if (list != null)
            {
                Iterator iterator = list.iterator();

                while (iterator.hasNext())
                {
                    PotionEffect potioneffect = (PotionEffect)iterator.next();
                    p_77654_3_.addPotionEffect(new PotionEffect(potioneffect));
                }
            }
        }

        if (!p_77654_3_.capabilities.isCreativeMode)
        {
            if (p_77654_1_.stackSize <= 0)
            {
                return new ItemStack(Items.glass_bottle);
            }

            p_77654_3_.inventory.addItemStackToInventory(new ItemStack(Items.glass_bottle));
        }

        return p_77654_1_;
    }

    /**
     * How long it takes to use or consume an item
     */
    public int getMaxItemUseDuration(ItemStack p_77626_1_)
    {
        return 32;
    }

    /**
     * returns the action that specifies what animation to play when the items is being used
     */
    public EnumAction getItemUseAction(ItemStack p_77661_1_)
    {
        return EnumAction.drink;
    }

    /**
     * Called whenever this item is equipped and the right mouse button is pressed. Args: itemStack, world, entityPlayer
     */
    public ItemStack onItemRightClick(ItemStack p_77659_1_, World p_77659_2_, EntityPlayer p_77659_3_)
    {
        if (isSplash(p_77659_1_.getItemDamage()))
        {
            if (!p_77659_3_.capabilities.isCreativeMode)
            {
                --p_77659_1_.stackSize;
            }

            p_77659_2_.playSoundAtEntity(p_77659_3_, "random.bow", 0.5F, 0.4F / (itemRand.nextFloat() * 0.4F + 0.8F));

            if (!p_77659_2_.isRemote)
            {
                p_77659_2_.spawnEntityInWorld(new EntityPotion(p_77659_2_, p_77659_3_, p_77659_1_));
            }

            return p_77659_1_;
        }
        else
        {
            p_77659_3_.setItemInUse(p_77659_1_, this.getMaxItemUseDuration(p_77659_1_));
            return p_77659_1_;
        }
    }

    /**
     * Callback for item usage. If the item does something special on right clicking, he will have one of those. Return
     * True if something happen and false if it don't. This is for ITEMS, not BLOCKS
     */
    public boolean onItemUse(ItemStack p_77648_1_, EntityPlayer p_77648_2_, World p_77648_3_, int p_77648_4_, int p_77648_5_, int p_77648_6_, int p_77648_7_, float p_77648_8_, float p_77648_9_, float p_77648_10_)
    {
        return false;
    }

    /**
     * Gets an icon index based on an item's damage value
     */
    @SideOnly(Side.CLIENT)
    public IIcon getIconFromDamage(int p_77617_1_)
    {
        /**
         * returns wether or not a potion is a throwable splash potion based on damage value
         */
        return isSplash(p_77617_1_) ? this.field_94591_c : this.field_94590_d;
    }

    /**
     * Gets an icon index based on an item's damage value and the given render pass
     */
    @SideOnly(Side.CLIENT)
    public IIcon getIconFromDamageForRenderPass(int p_77618_1_, int p_77618_2_)
    {
        return p_77618_2_ == 0 ? this.field_94592_ct : super.getIconFromDamageForRenderPass(p_77618_1_, p_77618_2_);
    }

    /**
     * returns wether or not a potion is a throwable splash potion based on damage value
     */
    public static boolean isSplash(int p_77831_0_)
    {
        return (p_77831_0_ & 16384) != 0;
    }

    @SideOnly(Side.CLIENT)
    public int getColorFromDamage(int p_77620_1_)
    {
        return PotionHelper.func_77915_a(p_77620_1_, false);
    }

    @SideOnly(Side.CLIENT)
    public int getColorFromItemStack(ItemStack p_82790_1_, int p_82790_2_)
    {
        return p_82790_2_ > 0 ? 16777215 : this.getColorFromDamage(p_82790_1_.getItemDamage());
    }

    @SideOnly(Side.CLIENT)
    public boolean requiresMultipleRenderPasses()
    {
        return true;
    }

    @SideOnly(Side.CLIENT)
    public boolean isEffectInstant(int p_77833_1_)
    {
        List list = this.getEffects(p_77833_1_);

        if (list != null && !list.isEmpty())
        {
            Iterator iterator = list.iterator();
            PotionEffect potioneffect;

            do
            {
                if (!iterator.hasNext())
                {
                    return false;
                }

                potioneffect = (PotionEffect)iterator.next();
            }
            while (!Potion.potionTypes[potioneffect.getPotionID()].isInstant());

            return true;
        }
        else
        {
            return false;
        }
    }

    public String getItemStackDisplayName(ItemStack p_77653_1_)
    {
        if (p_77653_1_.getItemDamage() == 0)
        {
            return StatCollector.translateToLocal("item.emptyPotion.name").trim();
        }
        else
        {
            String s = "";

            if (isSplash(p_77653_1_.getItemDamage()))
            {
                s = StatCollector.translateToLocal("potion.prefix.grenade").trim() + " ";
            }

            List list = Items.potionitem.getEffects(p_77653_1_);
            String s1;

            if (list != null && !list.isEmpty())
            {
                s1 = ((PotionEffect)list.get(0)).getEffectName();
                s1 = s1 + ".postfix";
                return s + StatCollector.translateToLocal(s1).trim();
            }
            else
            {
                s1 = PotionHelper.func_77905_c(p_77653_1_.getItemDamage());
                return StatCollector.translateToLocal(s1).trim() + " " + super.getItemStackDisplayName(p_77653_1_);
            }
        }
    }

    /**
     * allows items to add custom lines of information to the mouseover description
     */
    @SideOnly(Side.CLIENT)
    public void addInformation(ItemStack p_77624_1_, EntityPlayer p_77624_2_, List p_77624_3_, boolean p_77624_4_)
    {
        if (p_77624_1_.getItemDamage() != 0)
        {
            List list1 = Items.potionitem.getEffects(p_77624_1_);
            HashMultimap hashmultimap = HashMultimap.create();
            Iterator iterator1;

            if (list1 != null && !list1.isEmpty())
            {
                iterator1 = list1.iterator();

                while (iterator1.hasNext())
                {
                    PotionEffect potioneffect = (PotionEffect)iterator1.next();
                    String s1 = StatCollector.translateToLocal(potioneffect.getEffectName()).trim();
                    Potion potion = Potion.potionTypes[potioneffect.getPotionID()];
                    Map map = potion.func_111186_k();

                    if (map != null && map.size() > 0)
                    {
                        Iterator iterator = map.entrySet().iterator();

                        while (iterator.hasNext())
                        {
                            Entry entry = (Entry)iterator.next();
                            AttributeModifier attributemodifier = (AttributeModifier)entry.getValue();
                            AttributeModifier attributemodifier1 = new AttributeModifier(attributemodifier.getName(), potion.func_111183_a(potioneffect.getAmplifier(), attributemodifier), attributemodifier.getOperation());
                            hashmultimap.put(((IAttribute)entry.getKey()).getAttributeUnlocalizedName(), attributemodifier1);
                        }
                    }

                    if (potioneffect.getAmplifier() > 0)
                    {
                        s1 = s1 + " " + StatCollector.translateToLocal("potion.potency." + potioneffect.getAmplifier()).trim();
                    }

                    if (potioneffect.getDuration() > 20)
                    {
                        s1 = s1 + " (" + Potion.getDurationString(potioneffect) + ")";
                    }

                    if (potion.isBadEffect())
                    {
                        p_77624_3_.add(EnumChatFormatting.RED + s1);
                    }
                    else
                    {
                        p_77624_3_.add(EnumChatFormatting.GRAY + s1);
                    }
                }
            }
            else
            {
                String s = StatCollector.translateToLocal("potion.empty").trim();
                p_77624_3_.add(EnumChatFormatting.GRAY + s);
            }

            if (!hashmultimap.isEmpty())
            {
                p_77624_3_.add("");
                p_77624_3_.add(EnumChatFormatting.DARK_PURPLE + StatCollector.translateToLocal("potion.effects.whenDrank"));
                iterator1 = hashmultimap.entries().iterator();

                while (iterator1.hasNext())
                {
                    Entry entry1 = (Entry)iterator1.next();
                    AttributeModifier attributemodifier2 = (AttributeModifier)entry1.getValue();
                    double d0 = attributemodifier2.getAmount();
                    double d1;

                    if (attributemodifier2.getOperation() != 1 && attributemodifier2.getOperation() != 2)
                    {
                        d1 = attributemodifier2.getAmount();
                    }
                    else
                    {
                        d1 = attributemodifier2.getAmount() * 100.0D;
                    }

                    if (d0 > 0.0D)
                    {
                        p_77624_3_.add(EnumChatFormatting.BLUE + StatCollector.translateToLocalFormatted("attribute.modifier.plus." + attributemodifier2.getOperation(), new Object[] {ItemStack.field_111284_a.format(d1), StatCollector.translateToLocal("attribute.name." + (String)entry1.getKey())}));
                    }
                    else if (d0 < 0.0D)
                    {
                        d1 *= -1.0D;
                        p_77624_3_.add(EnumChatFormatting.RED + StatCollector.translateToLocalFormatted("attribute.modifier.take." + attributemodifier2.getOperation(), new Object[] {ItemStack.field_111284_a.format(d1), StatCollector.translateToLocal("attribute.name." + (String)entry1.getKey())}));
                    }
                }
            }
        }
    }

    @SideOnly(Side.CLIENT)
    public boolean hasEffect(ItemStack p_77636_1_)
    {
        List list = this.getEffects(p_77636_1_);
        return list != null && !list.isEmpty();
    }

    /**
     * returns a list of items with the same ID, but different meta (eg: dye returns 16 items)
     */
    @SideOnly(Side.CLIENT)
    public void getSubItems(Item p_150895_1_, CreativeTabs p_150895_2_, List p_150895_3_)
    {
        super.getSubItems(p_150895_1_, p_150895_2_, p_150895_3_);
        int j;

        if (field_77835_b.isEmpty())
        {
            for (int i = 0; i <= 15; ++i)
            {
                for (j = 0; j <= 1; ++j)
                {
                    int k;

                    if (j == 0)
                    {
                        k = i | 8192;
                    }
                    else
                    {
                        k = i | 16384;
                    }

                    for (int l = 0; l <= 2; ++l)
                    {
                        int i1 = k;

                        if (l != 0)
                        {
                            if (l == 1)
                            {
                                i1 = k | 32;
                            }
                            else if (l == 2)
                            {
                                i1 = k | 64;
                            }
                        }

                        List list1 = PotionHelper.getPotionEffects(i1, false);

                        if (list1 != null && !list1.isEmpty())
                        {
                            field_77835_b.put(list1, Integer.valueOf(i1));
                        }
                    }
                }
            }
        }

        Iterator iterator = field_77835_b.values().iterator();

        while (iterator.hasNext())
        {
            j = ((Integer)iterator.next()).intValue();
            p_150895_3_.add(new ItemStack(p_150895_1_, 1, j));
        }
    }

    @SideOnly(Side.CLIENT)
    public void registerIcons(IIconRegister p_94581_1_)
    {
        this.field_94590_d = p_94581_1_.registerIcon(this.getIconString() + "_" + "bottle_drinkable");
        this.field_94591_c = p_94581_1_.registerIcon(this.getIconString() + "_" + "bottle_splash");
        this.field_94592_ct = p_94581_1_.registerIcon(this.getIconString() + "_" + "overlay");
    }

    @SideOnly(Side.CLIENT)
    public static IIcon func_94589_d(String p_94589_0_)
    {
        return p_94589_0_.equals("bottle_drinkable") ? Items.potionitem.field_94590_d : (p_94589_0_.equals("bottle_splash") ? Items.potionitem.field_94591_c : (p_94589_0_.equals("overlay") ? Items.potionitem.field_94592_ct : null));
    }
}