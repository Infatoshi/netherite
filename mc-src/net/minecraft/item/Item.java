package net.minecraft.item;

import com.google.common.collect.HashMultimap;
import com.google.common.collect.Multimap;
import com.google.common.collect.Sets;

import cpw.mods.fml.common.registry.GameData;
import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Random;
import java.util.Set;
import java.util.UUID;
import net.minecraft.block.Block;
import net.minecraft.block.BlockDirt;
import net.minecraft.block.BlockDoublePlant;
import net.minecraft.block.BlockFlower;
import net.minecraft.block.BlockNewLog;
import net.minecraft.block.BlockOldLog;
import net.minecraft.block.BlockQuartz;
import net.minecraft.block.BlockSand;
import net.minecraft.block.BlockSandStone;
import net.minecraft.block.BlockSapling;
import net.minecraft.block.BlockSilverfish;
import net.minecraft.block.BlockStoneBrick;
import net.minecraft.block.BlockWall;
import net.minecraft.block.BlockWood;
import net.minecraft.block.material.Material;
import net.minecraft.client.gui.FontRenderer;
import net.minecraft.client.gui.ScaledResolution;
import net.minecraft.client.model.ModelBiped;
import net.minecraft.client.renderer.texture.IIconRegister;
import net.minecraft.creativetab.CreativeTabs;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityLivingBase;
import net.minecraft.entity.item.EntityItem;
import net.minecraft.entity.item.EntityItemFrame;
import net.minecraft.entity.item.EntityPainting;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.entity.player.EntityPlayerMP;
import net.minecraft.init.Blocks;
import net.minecraft.init.Items;
import net.minecraft.potion.Potion;
import net.minecraft.potion.PotionHelper;
import net.minecraft.util.IIcon;
import net.minecraft.util.MathHelper;
import net.minecraft.util.MovingObjectPosition;
import net.minecraft.util.RegistryNamespaced;
import net.minecraft.util.StatCollector;
import net.minecraft.util.Vec3;
import net.minecraft.util.WeightedRandomChestContent;
import net.minecraft.world.World;
import net.minecraftforge.common.ChestGenHooks;
import net.minecraftforge.common.util.EnumHelper;

public class Item
{
    public static final RegistryNamespaced itemRegistry = GameData.getItemRegistry();
    protected static final UUID field_111210_e = UUID.fromString("CB3F55D3-645C-4F38-A497-9C13A33DB5CF");
    private CreativeTabs tabToDisplayOn;
    /** The RNG used by the Item subclasses. */
    protected static Random itemRand = new Random();
    /** Maximum size of the stack. */
    protected int maxStackSize = 64;
    /** Maximum damage an item can handle. */
    private int maxDamage;
    /** If true, render the object in full 3D, like weapons and tools. */
    protected boolean bFull3D;
    /** Some items (like dyes) have multiple subtypes on same item, this is field define this behavior */
    protected boolean hasSubtypes;
    private Item containerItem;
    private String potionEffect;
    /** The unlocalized name of this item. */
    private String unlocalizedName;
    /** Icon index in the icons table. */
    @SideOnly(Side.CLIENT)
    protected IIcon itemIcon;
    /** The string associated with this Item's Icon. */
    protected String iconString;
    private static final String __OBFID = "CL_00000041";

    public final cpw.mods.fml.common.registry.RegistryDelegate<Item> delegate =
            ((cpw.mods.fml.common.registry.FMLControlledNamespacedRegistry)itemRegistry).getDelegate(this, Item.class);
    public static int getIdFromItem(Item p_150891_0_)
    {
        return p_150891_0_ == null ? 0 : itemRegistry.getIDForObject(p_150891_0_);
    }

    public static Item getItemById(int p_150899_0_)
    {
        return (Item)itemRegistry.getObjectById(p_150899_0_);
    }

    public static Item getItemFromBlock(Block p_150898_0_)
    {
        return getItemById(Block.getIdFromBlock(p_150898_0_));
    }

    public static void registerItems()
    {
        itemRegistry.addObject(256, "iron_shovel", (new ItemSpade(Item.ToolMaterial.IRON)).setUnlocalizedName("shovelIron").setTextureName("iron_shovel"));
        itemRegistry.addObject(257, "iron_pickaxe", (new ItemPickaxe(Item.ToolMaterial.IRON)).setUnlocalizedName("pickaxeIron").setTextureName("iron_pickaxe"));
        itemRegistry.addObject(258, "iron_axe", (new ItemAxe(Item.ToolMaterial.IRON)).setUnlocalizedName("hatchetIron").setTextureName("iron_axe"));
        itemRegistry.addObject(259, "flint_and_steel", (new ItemFlintAndSteel()).setUnlocalizedName("flintAndSteel").setTextureName("flint_and_steel"));
        itemRegistry.addObject(260, "apple", (new ItemFood(4, 0.3F, false)).setUnlocalizedName("apple").setTextureName("apple"));
        itemRegistry.addObject(261, "bow", (new ItemBow()).setUnlocalizedName("bow").setTextureName("bow"));
        itemRegistry.addObject(262, "arrow", (new Item()).setUnlocalizedName("arrow").setCreativeTab(CreativeTabs.tabCombat).setTextureName("arrow"));
        itemRegistry.addObject(263, "coal", (new ItemCoal()).setUnlocalizedName("coal").setTextureName("coal"));
        itemRegistry.addObject(264, "diamond", (new Item()).setUnlocalizedName("diamond").setCreativeTab(CreativeTabs.tabMaterials).setTextureName("diamond"));
        itemRegistry.addObject(265, "iron_ingot", (new Item()).setUnlocalizedName("ingotIron").setCreativeTab(CreativeTabs.tabMaterials).setTextureName("iron_ingot"));
        itemRegistry.addObject(266, "gold_ingot", (new Item()).setUnlocalizedName("ingotGold").setCreativeTab(CreativeTabs.tabMaterials).setTextureName("gold_ingot"));
        itemRegistry.addObject(267, "iron_sword", (new ItemSword(Item.ToolMaterial.IRON)).setUnlocalizedName("swordIron").setTextureName("iron_sword"));
        itemRegistry.addObject(268, "wooden_sword", (new ItemSword(Item.ToolMaterial.WOOD)).setUnlocalizedName("swordWood").setTextureName("wood_sword"));
        itemRegistry.addObject(269, "wooden_shovel", (new ItemSpade(Item.ToolMaterial.WOOD)).setUnlocalizedName("shovelWood").setTextureName("wood_shovel"));
        itemRegistry.addObject(270, "wooden_pickaxe", (new ItemPickaxe(Item.ToolMaterial.WOOD)).setUnlocalizedName("pickaxeWood").setTextureName("wood_pickaxe"));
        itemRegistry.addObject(271, "wooden_axe", (new ItemAxe(Item.ToolMaterial.WOOD)).setUnlocalizedName("hatchetWood").setTextureName("wood_axe"));
        itemRegistry.addObject(272, "stone_sword", (new ItemSword(Item.ToolMaterial.STONE)).setUnlocalizedName("swordStone").setTextureName("stone_sword"));
        itemRegistry.addObject(273, "stone_shovel", (new ItemSpade(Item.ToolMaterial.STONE)).setUnlocalizedName("shovelStone").setTextureName("stone_shovel"));
        itemRegistry.addObject(274, "stone_pickaxe", (new ItemPickaxe(Item.ToolMaterial.STONE)).setUnlocalizedName("pickaxeStone").setTextureName("stone_pickaxe"));
        itemRegistry.addObject(275, "stone_axe", (new ItemAxe(Item.ToolMaterial.STONE)).setUnlocalizedName("hatchetStone").setTextureName("stone_axe"));
        itemRegistry.addObject(276, "diamond_sword", (new ItemSword(Item.ToolMaterial.EMERALD)).setUnlocalizedName("swordDiamond").setTextureName("diamond_sword"));
        itemRegistry.addObject(277, "diamond_shovel", (new ItemSpade(Item.ToolMaterial.EMERALD)).setUnlocalizedName("shovelDiamond").setTextureName("diamond_shovel"));
        itemRegistry.addObject(278, "diamond_pickaxe", (new ItemPickaxe(Item.ToolMaterial.EMERALD)).setUnlocalizedName("pickaxeDiamond").setTextureName("diamond_pickaxe"));
        itemRegistry.addObject(279, "diamond_axe", (new ItemAxe(Item.ToolMaterial.EMERALD)).setUnlocalizedName("hatchetDiamond").setTextureName("diamond_axe"));
        itemRegistry.addObject(280, "stick", (new Item()).setFull3D().setUnlocalizedName("stick").setCreativeTab(CreativeTabs.tabMaterials).setTextureName("stick"));
        itemRegistry.addObject(281, "bowl", (new Item()).setUnlocalizedName("bowl").setCreativeTab(CreativeTabs.tabMaterials).setTextureName("bowl"));
        itemRegistry.addObject(282, "mushroom_stew", (new ItemSoup(6)).setUnlocalizedName("mushroomStew").setTextureName("mushroom_stew"));
        itemRegistry.addObject(283, "golden_sword", (new ItemSword(Item.ToolMaterial.GOLD)).setUnlocalizedName("swordGold").setTextureName("gold_sword"));
        itemRegistry.addObject(284, "golden_shovel", (new ItemSpade(Item.ToolMaterial.GOLD)).setUnlocalizedName("shovelGold").setTextureName("gold_shovel"));
        itemRegistry.addObject(285, "golden_pickaxe", (new ItemPickaxe(Item.ToolMaterial.GOLD)).setUnlocalizedName("pickaxeGold").setTextureName("gold_pickaxe"));
        itemRegistry.addObject(286, "golden_axe", (new ItemAxe(Item.ToolMaterial.GOLD)).setUnlocalizedName("hatchetGold").setTextureName("gold_axe"));
        itemRegistry.addObject(287, "string", (new ItemReed(Blocks.tripwire)).setUnlocalizedName("string").setCreativeTab(CreativeTabs.tabMaterials).setTextureName("string"));
        itemRegistry.addObject(288, "feather", (new Item()).setUnlocalizedName("feather").setCreativeTab(CreativeTabs.tabMaterials).setTextureName("feather"));
        itemRegistry.addObject(289, "gunpowder", (new Item()).setUnlocalizedName("sulphur").setPotionEffect(PotionHelper.gunpowderEffect).setCreativeTab(CreativeTabs.tabMaterials).setTextureName("gunpowder"));
        itemRegistry.addObject(290, "wooden_hoe", (new ItemHoe(Item.ToolMaterial.WOOD)).setUnlocalizedName("hoeWood").setTextureName("wood_hoe"));
        itemRegistry.addObject(291, "stone_hoe", (new ItemHoe(Item.ToolMaterial.STONE)).setUnlocalizedName("hoeStone").setTextureName("stone_hoe"));
        itemRegistry.addObject(292, "iron_hoe", (new ItemHoe(Item.ToolMaterial.IRON)).setUnlocalizedName("hoeIron").setTextureName("iron_hoe"));
        itemRegistry.addObject(293, "diamond_hoe", (new ItemHoe(Item.ToolMaterial.EMERALD)).setUnlocalizedName("hoeDiamond").setTextureName("diamond_hoe"));
        itemRegistry.addObject(294, "golden_hoe", (new ItemHoe(Item.ToolMaterial.GOLD)).setUnlocalizedName("hoeGold").setTextureName("gold_hoe"));
        itemRegistry.addObject(295, "wheat_seeds", (new ItemSeeds(Blocks.wheat, Blocks.farmland)).setUnlocalizedName("seeds").setTextureName("seeds_wheat"));
        itemRegistry.addObject(296, "wheat", (new Item()).setUnlocalizedName("wheat").setCreativeTab(CreativeTabs.tabMaterials).setTextureName("wheat"));
        itemRegistry.addObject(297, "bread", (new ItemFood(5, 0.6F, false)).setUnlocalizedName("bread").setTextureName("bread"));
        itemRegistry.addObject(298, "leather_helmet", (new ItemArmor(ItemArmor.ArmorMaterial.CLOTH, 0, 0)).setUnlocalizedName("helmetCloth").setTextureName("leather_helmet"));
        itemRegistry.addObject(299, "leather_chestplate", (new ItemArmor(ItemArmor.ArmorMaterial.CLOTH, 0, 1)).setUnlocalizedName("chestplateCloth").setTextureName("leather_chestplate"));
        itemRegistry.addObject(300, "leather_leggings", (new ItemArmor(ItemArmor.ArmorMaterial.CLOTH, 0, 2)).setUnlocalizedName("leggingsCloth").setTextureName("leather_leggings"));
        itemRegistry.addObject(301, "leather_boots", (new ItemArmor(ItemArmor.ArmorMaterial.CLOTH, 0, 3)).setUnlocalizedName("bootsCloth").setTextureName("leather_boots"));
        itemRegistry.addObject(302, "chainmail_helmet", (new ItemArmor(ItemArmor.ArmorMaterial.CHAIN, 1, 0)).setUnlocalizedName("helmetChain").setTextureName("chainmail_helmet"));
        itemRegistry.addObject(303, "chainmail_chestplate", (new ItemArmor(ItemArmor.ArmorMaterial.CHAIN, 1, 1)).setUnlocalizedName("chestplateChain").setTextureName("chainmail_chestplate"));
        itemRegistry.addObject(304, "chainmail_leggings", (new ItemArmor(ItemArmor.ArmorMaterial.CHAIN, 1, 2)).setUnlocalizedName("leggingsChain").setTextureName("chainmail_leggings"));
        itemRegistry.addObject(305, "chainmail_boots", (new ItemArmor(ItemArmor.ArmorMaterial.CHAIN, 1, 3)).setUnlocalizedName("bootsChain").setTextureName("chainmail_boots"));
        itemRegistry.addObject(306, "iron_helmet", (new ItemArmor(ItemArmor.ArmorMaterial.IRON, 2, 0)).setUnlocalizedName("helmetIron").setTextureName("iron_helmet"));
        itemRegistry.addObject(307, "iron_chestplate", (new ItemArmor(ItemArmor.ArmorMaterial.IRON, 2, 1)).setUnlocalizedName("chestplateIron").setTextureName("iron_chestplate"));
        itemRegistry.addObject(308, "iron_leggings", (new ItemArmor(ItemArmor.ArmorMaterial.IRON, 2, 2)).setUnlocalizedName("leggingsIron").setTextureName("iron_leggings"));
        itemRegistry.addObject(309, "iron_boots", (new ItemArmor(ItemArmor.ArmorMaterial.IRON, 2, 3)).setUnlocalizedName("bootsIron").setTextureName("iron_boots"));
        itemRegistry.addObject(310, "diamond_helmet", (new ItemArmor(ItemArmor.ArmorMaterial.DIAMOND, 3, 0)).setUnlocalizedName("helmetDiamond").setTextureName("diamond_helmet"));
        itemRegistry.addObject(311, "diamond_chestplate", (new ItemArmor(ItemArmor.ArmorMaterial.DIAMOND, 3, 1)).setUnlocalizedName("chestplateDiamond").setTextureName("diamond_chestplate"));
        itemRegistry.addObject(312, "diamond_leggings", (new ItemArmor(ItemArmor.ArmorMaterial.DIAMOND, 3, 2)).setUnlocalizedName("leggingsDiamond").setTextureName("diamond_leggings"));
        itemRegistry.addObject(313, "diamond_boots", (new ItemArmor(ItemArmor.ArmorMaterial.DIAMOND, 3, 3)).setUnlocalizedName("bootsDiamond").setTextureName("diamond_boots"));
        itemRegistry.addObject(314, "golden_helmet", (new ItemArmor(ItemArmor.ArmorMaterial.GOLD, 4, 0)).setUnlocalizedName("helmetGold").setTextureName("gold_helmet"));
        itemRegistry.addObject(315, "golden_chestplate", (new ItemArmor(ItemArmor.ArmorMaterial.GOLD, 4, 1)).setUnlocalizedName("chestplateGold").setTextureName("gold_chestplate"));
        itemRegistry.addObject(316, "golden_leggings", (new ItemArmor(ItemArmor.ArmorMaterial.GOLD, 4, 2)).setUnlocalizedName("leggingsGold").setTextureName("gold_leggings"));
        itemRegistry.addObject(317, "golden_boots", (new ItemArmor(ItemArmor.ArmorMaterial.GOLD, 4, 3)).setUnlocalizedName("bootsGold").setTextureName("gold_boots"));
        itemRegistry.addObject(318, "flint", (new Item()).setUnlocalizedName("flint").setCreativeTab(CreativeTabs.tabMaterials).setTextureName("flint"));
        itemRegistry.addObject(319, "porkchop", (new ItemFood(3, 0.3F, true)).setUnlocalizedName("porkchopRaw").setTextureName("porkchop_raw"));
        itemRegistry.addObject(320, "cooked_porkchop", (new ItemFood(8, 0.8F, true)).setUnlocalizedName("porkchopCooked").setTextureName("porkchop_cooked"));
        itemRegistry.addObject(321, "painting", (new ItemHangingEntity(EntityPainting.class)).setUnlocalizedName("painting").setTextureName("painting"));
        itemRegistry.addObject(322, "golden_apple", (new ItemAppleGold(4, 1.2F, false)).setAlwaysEdible().setPotionEffect(Potion.regeneration.id, 5, 1, 1.0F).setUnlocalizedName("appleGold").setTextureName("apple_golden"));
        itemRegistry.addObject(323, "sign", (new ItemSign()).setUnlocalizedName("sign").setTextureName("sign"));
        itemRegistry.addObject(324, "wooden_door", (new ItemDoor(Material.wood)).setUnlocalizedName("doorWood").setTextureName("door_wood"));
        Item item = (new ItemBucket(Blocks.air)).setUnlocalizedName("bucket").setMaxStackSize(16).setTextureName("bucket_empty");
        itemRegistry.addObject(325, "bucket", item);
        itemRegistry.addObject(326, "water_bucket", (new ItemBucket(Blocks.flowing_water)).setUnlocalizedName("bucketWater").setContainerItem(item).setTextureName("bucket_water"));
        itemRegistry.addObject(327, "lava_bucket", (new ItemBucket(Blocks.flowing_lava)).setUnlocalizedName("bucketLava").setContainerItem(item).setTextureName("bucket_lava"));
        itemRegistry.addObject(328, "minecart", (new ItemMinecart(0)).setUnlocalizedName("minecart").setTextureName("minecart_normal"));
        itemRegistry.addObject(329, "saddle", (new ItemSaddle()).setUnlocalizedName("saddle").setTextureName("saddle"));
        itemRegistry.addObject(330, "iron_door", (new ItemDoor(Material.iron)).setUnlocalizedName("doorIron").setTextureName("door_iron"));
        itemRegistry.addObject(331, "redstone", (new ItemRedstone()).setUnlocalizedName("redstone").setPotionEffect(PotionHelper.redstoneEffect).setTextureName("redstone_dust"));
        itemRegistry.addObject(332, "snowball", (new ItemSnowball()).setUnlocalizedName("snowball").setTextureName("snowball"));
        itemRegistry.addObject(333, "boat", (new ItemBoat()).setUnlocalizedName("boat").setTextureName("boat"));
        itemRegistry.addObject(334, "leather", (new Item()).setUnlocalizedName("leather").setCreativeTab(CreativeTabs.tabMaterials).setTextureName("leather"));
        itemRegistry.addObject(335, "milk_bucket", (new ItemBucketMilk()).setUnlocalizedName("milk").setContainerItem(item).setTextureName("bucket_milk"));
        itemRegistry.addObject(336, "brick", (new Item()).setUnlocalizedName("brick").setCreativeTab(CreativeTabs.tabMaterials).setTextureName("brick"));
        itemRegistry.addObject(337, "clay_ball", (new Item()).setUnlocalizedName("clay").setCreativeTab(CreativeTabs.tabMaterials).setTextureName("clay_ball"));
        itemRegistry.addObject(338, "reeds", (new ItemReed(Blocks.reeds)).setUnlocalizedName("reeds").setCreativeTab(CreativeTabs.tabMaterials).setTextureName("reeds"));
        itemRegistry.addObject(339, "paper", (new Item()).setUnlocalizedName("paper").setCreativeTab(CreativeTabs.tabMisc).setTextureName("paper"));
        itemRegistry.addObject(340, "book", (new ItemBook()).setUnlocalizedName("book").setCreativeTab(CreativeTabs.tabMisc).setTextureName("book_normal"));
        itemRegistry.addObject(341, "slime_ball", (new Item()).setUnlocalizedName("slimeball").setCreativeTab(CreativeTabs.tabMisc).setTextureName("slimeball"));
        itemRegistry.addObject(342, "chest_minecart", (new ItemMinecart(1)).setUnlocalizedName("minecartChest").setTextureName("minecart_chest"));
        itemRegistry.addObject(343, "furnace_minecart", (new ItemMinecart(2)).setUnlocalizedName("minecartFurnace").setTextureName("minecart_furnace"));
        itemRegistry.addObject(344, "egg", (new ItemEgg()).setUnlocalizedName("egg").setTextureName("egg"));
        itemRegistry.addObject(345, "compass", (new Item()).setUnlocalizedName("compass").setCreativeTab(CreativeTabs.tabTools).setTextureName("compass"));
        itemRegistry.addObject(346, "fishing_rod", (new ItemFishingRod()).setUnlocalizedName("fishingRod").setTextureName("fishing_rod"));
        itemRegistry.addObject(347, "clock", (new Item()).setUnlocalizedName("clock").setCreativeTab(CreativeTabs.tabTools).setTextureName("clock"));
        itemRegistry.addObject(348, "glowstone_dust", (new Item()).setUnlocalizedName("yellowDust").setPotionEffect(PotionHelper.glowstoneEffect).setCreativeTab(CreativeTabs.tabMaterials).setTextureName("glowstone_dust"));
        itemRegistry.addObject(349, "fish", (new ItemFishFood(false)).setUnlocalizedName("fish").setTextureName("fish_raw").setHasSubtypes(true));
        itemRegistry.addObject(350, "cooked_fished", (new ItemFishFood(true)).setUnlocalizedName("fish").setTextureName("fish_cooked").setHasSubtypes(true));
        itemRegistry.addObject(351, "dye", (new ItemDye()).setUnlocalizedName("dyePowder").setTextureName("dye_powder"));
        itemRegistry.addObject(352, "bone", (new Item()).setUnlocalizedName("bone").setFull3D().setCreativeTab(CreativeTabs.tabMisc).setTextureName("bone"));
        itemRegistry.addObject(353, "sugar", (new Item()).setUnlocalizedName("sugar").setPotionEffect(PotionHelper.sugarEffect).setCreativeTab(CreativeTabs.tabMaterials).setTextureName("sugar"));
        itemRegistry.addObject(354, "cake", (new ItemReed(Blocks.cake)).setMaxStackSize(1).setUnlocalizedName("cake").setCreativeTab(CreativeTabs.tabFood).setTextureName("cake"));
        itemRegistry.addObject(355, "bed", (new ItemBed()).setMaxStackSize(1).setUnlocalizedName("bed").setTextureName("bed"));
        itemRegistry.addObject(356, "repeater", (new ItemReed(Blocks.unpowered_repeater)).setUnlocalizedName("diode").setCreativeTab(CreativeTabs.tabRedstone).setTextureName("repeater"));
        itemRegistry.addObject(357, "cookie", (new ItemFood(2, 0.1F, false)).setUnlocalizedName("cookie").setTextureName("cookie"));
        itemRegistry.addObject(358, "filled_map", (new ItemMap()).setUnlocalizedName("map").setTextureName("map_filled"));
        itemRegistry.addObject(359, "shears", (new ItemShears()).setUnlocalizedName("shears").setTextureName("shears"));
        itemRegistry.addObject(360, "melon", (new ItemFood(2, 0.3F, false)).setUnlocalizedName("melon").setTextureName("melon"));
        itemRegistry.addObject(361, "pumpkin_seeds", (new ItemSeeds(Blocks.pumpkin_stem, Blocks.farmland)).setUnlocalizedName("seeds_pumpkin").setTextureName("seeds_pumpkin"));
        itemRegistry.addObject(362, "melon_seeds", (new ItemSeeds(Blocks.melon_stem, Blocks.farmland)).setUnlocalizedName("seeds_melon").setTextureName("seeds_melon"));
        itemRegistry.addObject(363, "beef", (new ItemFood(3, 0.3F, true)).setUnlocalizedName("beefRaw").setTextureName("beef_raw"));
        itemRegistry.addObject(364, "cooked_beef", (new ItemFood(8, 0.8F, true)).setUnlocalizedName("beefCooked").setTextureName("beef_cooked"));
        itemRegistry.addObject(365, "chicken", (new ItemFood(2, 0.3F, true)).setPotionEffect(Potion.hunger.id, 30, 0, 0.3F).setUnlocalizedName("chickenRaw").setTextureName("chicken_raw"));
        itemRegistry.addObject(366, "cooked_chicken", (new ItemFood(6, 0.6F, true)).setUnlocalizedName("chickenCooked").setTextureName("chicken_cooked"));
        itemRegistry.addObject(367, "rotten_flesh", (new ItemFood(4, 0.1F, true)).setPotionEffect(Potion.hunger.id, 30, 0, 0.8F).setUnlocalizedName("rottenFlesh").setTextureName("rotten_flesh"));
        itemRegistry.addObject(368, "ender_pearl", (new ItemEnderPearl()).setUnlocalizedName("enderPearl").setTextureName("ender_pearl"));
        itemRegistry.addObject(369, "blaze_rod", (new Item()).setUnlocalizedName("blazeRod").setCreativeTab(CreativeTabs.tabMaterials).setTextureName("blaze_rod"));
        itemRegistry.addObject(370, "ghast_tear", (new Item()).setUnlocalizedName("ghastTear").setPotionEffect(PotionHelper.ghastTearEffect).setCreativeTab(CreativeTabs.tabBrewing).setTextureName("ghast_tear"));
        itemRegistry.addObject(371, "gold_nugget", (new Item()).setUnlocalizedName("goldNugget").setCreativeTab(CreativeTabs.tabMaterials).setTextureName("gold_nugget"));
        itemRegistry.addObject(372, "nether_wart", (new ItemSeeds(Blocks.nether_wart, Blocks.soul_sand)).setUnlocalizedName("netherStalkSeeds").setPotionEffect("+4").setTextureName("nether_wart"));
        itemRegistry.addObject(373, "potion", (new ItemPotion()).setUnlocalizedName("potion").setTextureName("potion"));
        itemRegistry.addObject(374, "glass_bottle", (new ItemGlassBottle()).setUnlocalizedName("glassBottle").setTextureName("potion_bottle_empty"));
        itemRegistry.addObject(375, "spider_eye", (new ItemFood(2, 0.8F, false)).setPotionEffect(Potion.poison.id, 5, 0, 1.0F).setUnlocalizedName("spiderEye").setPotionEffect(PotionHelper.spiderEyeEffect).setTextureName("spider_eye"));
        itemRegistry.addObject(376, "fermented_spider_eye", (new Item()).setUnlocalizedName("fermentedSpiderEye").setPotionEffect(PotionHelper.fermentedSpiderEyeEffect).setCreativeTab(CreativeTabs.tabBrewing).setTextureName("spider_eye_fermented"));
        itemRegistry.addObject(377, "blaze_powder", (new Item()).setUnlocalizedName("blazePowder").setPotionEffect(PotionHelper.blazePowderEffect).setCreativeTab(CreativeTabs.tabBrewing).setTextureName("blaze_powder"));
        itemRegistry.addObject(378, "magma_cream", (new Item()).setUnlocalizedName("magmaCream").setPotionEffect(PotionHelper.magmaCreamEffect).setCreativeTab(CreativeTabs.tabBrewing).setTextureName("magma_cream"));
        itemRegistry.addObject(379, "brewing_stand", (new ItemReed(Blocks.brewing_stand)).setUnlocalizedName("brewingStand").setCreativeTab(CreativeTabs.tabBrewing).setTextureName("brewing_stand"));
        itemRegistry.addObject(380, "cauldron", (new ItemReed(Blocks.cauldron)).setUnlocalizedName("cauldron").setCreativeTab(CreativeTabs.tabBrewing).setTextureName("cauldron"));
        itemRegistry.addObject(381, "ender_eye", (new ItemEnderEye()).setUnlocalizedName("eyeOfEnder").setTextureName("ender_eye"));
        itemRegistry.addObject(382, "speckled_melon", (new Item()).setUnlocalizedName("speckledMelon").setPotionEffect(PotionHelper.speckledMelonEffect).setCreativeTab(CreativeTabs.tabBrewing).setTextureName("melon_speckled"));
        itemRegistry.addObject(383, "spawn_egg", (new ItemMonsterPlacer()).setUnlocalizedName("monsterPlacer").setTextureName("spawn_egg"));
        itemRegistry.addObject(384, "experience_bottle", (new ItemExpBottle()).setUnlocalizedName("expBottle").setTextureName("experience_bottle"));
        itemRegistry.addObject(385, "fire_charge", (new ItemFireball()).setUnlocalizedName("fireball").setTextureName("fireball"));
        itemRegistry.addObject(386, "writable_book", (new ItemWritableBook()).setUnlocalizedName("writingBook").setCreativeTab(CreativeTabs.tabMisc).setTextureName("book_writable"));
        itemRegistry.addObject(387, "written_book", (new ItemEditableBook()).setUnlocalizedName("writtenBook").setTextureName("book_written").setMaxStackSize(16));
        itemRegistry.addObject(388, "emerald", (new Item()).setUnlocalizedName("emerald").setCreativeTab(CreativeTabs.tabMaterials).setTextureName("emerald"));
        itemRegistry.addObject(389, "item_frame", (new ItemHangingEntity(EntityItemFrame.class)).setUnlocalizedName("frame").setTextureName("item_frame"));
        itemRegistry.addObject(390, "flower_pot", (new ItemReed(Blocks.flower_pot)).setUnlocalizedName("flowerPot").setCreativeTab(CreativeTabs.tabDecorations).setTextureName("flower_pot"));
        itemRegistry.addObject(391, "carrot", (new ItemSeedFood(4, 0.6F, Blocks.carrots, Blocks.farmland)).setUnlocalizedName("carrots").setTextureName("carrot"));
        itemRegistry.addObject(392, "potato", (new ItemSeedFood(1, 0.3F, Blocks.potatoes, Blocks.farmland)).setUnlocalizedName("potato").setTextureName("potato"));
        itemRegistry.addObject(393, "baked_potato", (new ItemFood(6, 0.6F, false)).setUnlocalizedName("potatoBaked").setTextureName("potato_baked"));
        itemRegistry.addObject(394, "poisonous_potato", (new ItemFood(2, 0.3F, false)).setPotionEffect(Potion.poison.id, 5, 0, 0.6F).setUnlocalizedName("potatoPoisonous").setTextureName("potato_poisonous"));
        itemRegistry.addObject(395, "map", (new ItemEmptyMap()).setUnlocalizedName("emptyMap").setTextureName("map_empty"));
        itemRegistry.addObject(396, "golden_carrot", (new ItemFood(6, 1.2F, false)).setUnlocalizedName("carrotGolden").setPotionEffect(PotionHelper.goldenCarrotEffect).setTextureName("carrot_golden"));
        itemRegistry.addObject(397, "skull", (new ItemSkull()).setUnlocalizedName("skull").setTextureName("skull"));
        itemRegistry.addObject(398, "carrot_on_a_stick", (new ItemCarrotOnAStick()).setUnlocalizedName("carrotOnAStick").setTextureName("carrot_on_a_stick"));
        itemRegistry.addObject(399, "nether_star", (new ItemSimpleFoiled()).setUnlocalizedName("netherStar").setCreativeTab(CreativeTabs.tabMaterials).setTextureName("nether_star"));
        itemRegistry.addObject(400, "pumpkin_pie", (new ItemFood(8, 0.3F, false)).setUnlocalizedName("pumpkinPie").setCreativeTab(CreativeTabs.tabFood).setTextureName("pumpkin_pie"));
        itemRegistry.addObject(401, "fireworks", (new ItemFirework()).setUnlocalizedName("fireworks").setTextureName("fireworks"));
        itemRegistry.addObject(402, "firework_charge", (new ItemFireworkCharge()).setUnlocalizedName("fireworksCharge").setCreativeTab(CreativeTabs.tabMisc).setTextureName("fireworks_charge"));
        itemRegistry.addObject(403, "enchanted_book", (new ItemEnchantedBook()).setMaxStackSize(1).setUnlocalizedName("enchantedBook").setTextureName("book_enchanted"));
        itemRegistry.addObject(404, "comparator", (new ItemReed(Blocks.unpowered_comparator)).setUnlocalizedName("comparator").setCreativeTab(CreativeTabs.tabRedstone).setTextureName("comparator"));
        itemRegistry.addObject(405, "netherbrick", (new Item()).setUnlocalizedName("netherbrick").setCreativeTab(CreativeTabs.tabMaterials).setTextureName("netherbrick"));
        itemRegistry.addObject(406, "quartz", (new Item()).setUnlocalizedName("netherquartz").setCreativeTab(CreativeTabs.tabMaterials).setTextureName("quartz"));
        itemRegistry.addObject(407, "tnt_minecart", (new ItemMinecart(3)).setUnlocalizedName("minecartTnt").setTextureName("minecart_tnt"));
        itemRegistry.addObject(408, "hopper_minecart", (new ItemMinecart(5)).setUnlocalizedName("minecartHopper").setTextureName("minecart_hopper"));
        itemRegistry.addObject(417, "iron_horse_armor", (new Item()).setUnlocalizedName("horsearmormetal").setMaxStackSize(1).setCreativeTab(CreativeTabs.tabMisc).setTextureName("iron_horse_armor"));
        itemRegistry.addObject(418, "golden_horse_armor", (new Item()).setUnlocalizedName("horsearmorgold").setMaxStackSize(1).setCreativeTab(CreativeTabs.tabMisc).setTextureName("gold_horse_armor"));
        itemRegistry.addObject(419, "diamond_horse_armor", (new Item()).setUnlocalizedName("horsearmordiamond").setMaxStackSize(1).setCreativeTab(CreativeTabs.tabMisc).setTextureName("diamond_horse_armor"));
        itemRegistry.addObject(420, "lead", (new ItemLead()).setUnlocalizedName("leash").setTextureName("lead"));
        itemRegistry.addObject(421, "name_tag", (new ItemNameTag()).setUnlocalizedName("nameTag").setTextureName("name_tag"));
        itemRegistry.addObject(422, "command_block_minecart", (new ItemMinecart(6)).setUnlocalizedName("minecartCommandBlock").setTextureName("minecart_command_block").setCreativeTab((CreativeTabs)null));
        itemRegistry.addObject(2256, "record_13", (new ItemRecord("13")).setUnlocalizedName("record").setTextureName("record_13"));
        itemRegistry.addObject(2257, "record_cat", (new ItemRecord("cat")).setUnlocalizedName("record").setTextureName("record_cat"));
        itemRegistry.addObject(2258, "record_blocks", (new ItemRecord("blocks")).setUnlocalizedName("record").setTextureName("record_blocks"));
        itemRegistry.addObject(2259, "record_chirp", (new ItemRecord("chirp")).setUnlocalizedName("record").setTextureName("record_chirp"));
        itemRegistry.addObject(2260, "record_far", (new ItemRecord("far")).setUnlocalizedName("record").setTextureName("record_far"));
        itemRegistry.addObject(2261, "record_mall", (new ItemRecord("mall")).setUnlocalizedName("record").setTextureName("record_mall"));
        itemRegistry.addObject(2262, "record_mellohi", (new ItemRecord("mellohi")).setUnlocalizedName("record").setTextureName("record_mellohi"));
        itemRegistry.addObject(2263, "record_stal", (new ItemRecord("stal")).setUnlocalizedName("record").setTextureName("record_stal"));
        itemRegistry.addObject(2264, "record_strad", (new ItemRecord("strad")).setUnlocalizedName("record").setTextureName("record_strad"));
        itemRegistry.addObject(2265, "record_ward", (new ItemRecord("ward")).setUnlocalizedName("record").setTextureName("record_ward"));
        itemRegistry.addObject(2266, "record_11", (new ItemRecord("11")).setUnlocalizedName("record").setTextureName("record_11"));
        itemRegistry.addObject(2267, "record_wait", (new ItemRecord("wait")).setUnlocalizedName("record").setTextureName("record_wait"));
        HashSet hashset = Sets.newHashSet(new Block[] {Blocks.air, Blocks.brewing_stand, Blocks.bed, Blocks.nether_wart, Blocks.cauldron, Blocks.flower_pot, Blocks.wheat, Blocks.reeds, Blocks.cake, Blocks.skull, Blocks.piston_head, Blocks.piston_extension, Blocks.lit_redstone_ore, Blocks.powered_repeater, Blocks.pumpkin_stem, Blocks.standing_sign, Blocks.powered_comparator, Blocks.tripwire, Blocks.lit_redstone_lamp, Blocks.melon_stem, Blocks.unlit_redstone_torch, Blocks.unpowered_comparator, Blocks.redstone_wire, Blocks.wall_sign, Blocks.unpowered_repeater, Blocks.iron_door, Blocks.wooden_door});
        Iterator iterator = Block.blockRegistry.getKeys().iterator();

        while (iterator.hasNext())
        {
            String s = (String)iterator.next();
            Block block = (Block)Block.blockRegistry.getObject(s);
            Object object;

            if (block == Blocks.wool)
            {
                object = (new ItemCloth(Blocks.wool)).setUnlocalizedName("cloth");
            }
            else if (block == Blocks.stained_hardened_clay)
            {
                object = (new ItemCloth(Blocks.stained_hardened_clay)).setUnlocalizedName("clayHardenedStained");
            }
            else if (block == Blocks.stained_glass)
            {
                object = (new ItemCloth(Blocks.stained_glass)).setUnlocalizedName("stainedGlass");
            }
            else if (block == Blocks.stained_glass_pane)
            {
                object = (new ItemCloth(Blocks.stained_glass_pane)).setUnlocalizedName("stainedGlassPane");
            }
            else if (block == Blocks.carpet)
            {
                object = (new ItemCloth(Blocks.carpet)).setUnlocalizedName("woolCarpet");
            }
            else if (block == Blocks.dirt)
            {
                object = (new ItemMultiTexture(Blocks.dirt, Blocks.dirt, BlockDirt.field_150009_a)).setUnlocalizedName("dirt");
            }
            else if (block == Blocks.sand)
            {
                object = (new ItemMultiTexture(Blocks.sand, Blocks.sand, BlockSand.field_149838_a)).setUnlocalizedName("sand");
            }
            else if (block == Blocks.log)
            {
                object = (new ItemMultiTexture(Blocks.log, Blocks.log, BlockOldLog.field_150168_M)).setUnlocalizedName("log");
            }
            else if (block == Blocks.log2)
            {
                object = (new ItemMultiTexture(Blocks.log2, Blocks.log2, BlockNewLog.field_150169_M)).setUnlocalizedName("log");
            }
            else if (block == Blocks.planks)
            {
                object = (new ItemMultiTexture(Blocks.planks, Blocks.planks, BlockWood.field_150096_a)).setUnlocalizedName("wood");
            }
            else if (block == Blocks.monster_egg)
            {
                object = (new ItemMultiTexture(Blocks.monster_egg, Blocks.monster_egg, BlockSilverfish.field_150198_a)).setUnlocalizedName("monsterStoneEgg");
            }
            else if (block == Blocks.stonebrick)
            {
                object = (new ItemMultiTexture(Blocks.stonebrick, Blocks.stonebrick, BlockStoneBrick.field_150142_a)).setUnlocalizedName("stonebricksmooth");
            }
            else if (block == Blocks.sandstone)
            {
                object = (new ItemMultiTexture(Blocks.sandstone, Blocks.sandstone, BlockSandStone.field_150157_a)).setUnlocalizedName("sandStone");
            }
            else if (block == Blocks.quartz_block)
            {
                object = (new ItemMultiTexture(Blocks.quartz_block, Blocks.quartz_block, BlockQuartz.field_150191_a)).setUnlocalizedName("quartzBlock");
            }
            else if (block == Blocks.stone_slab)
            {
                object = (new ItemSlab(Blocks.stone_slab, Blocks.stone_slab, Blocks.double_stone_slab, false)).setUnlocalizedName("stoneSlab");
            }
            else if (block == Blocks.double_stone_slab)
            {
                object = (new ItemSlab(Blocks.double_stone_slab, Blocks.stone_slab, Blocks.double_stone_slab, true)).setUnlocalizedName("stoneSlab");
            }
            else if (block == Blocks.wooden_slab)
            {
                object = (new ItemSlab(Blocks.wooden_slab, Blocks.wooden_slab, Blocks.double_wooden_slab, false)).setUnlocalizedName("woodSlab");
            }
            else if (block == Blocks.double_wooden_slab)
            {
                object = (new ItemSlab(Blocks.double_wooden_slab, Blocks.wooden_slab, Blocks.double_wooden_slab, true)).setUnlocalizedName("woodSlab");
            }
            else if (block == Blocks.sapling)
            {
                object = (new ItemMultiTexture(Blocks.sapling, Blocks.sapling, BlockSapling.field_149882_a)).setUnlocalizedName("sapling");
            }
            else if (block == Blocks.leaves)
            {
                object = (new ItemLeaves(Blocks.leaves)).setUnlocalizedName("leaves");
            }
            else if (block == Blocks.leaves2)
            {
                object = (new ItemLeaves(Blocks.leaves2)).setUnlocalizedName("leaves");
            }
            else if (block == Blocks.vine)
            {
                object = new ItemColored(Blocks.vine, false);
            }
            else if (block == Blocks.tallgrass)
            {
                object = (new ItemColored(Blocks.tallgrass, true)).func_150943_a(new String[] {"shrub", "grass", "fern"});
            }
            else if (block == Blocks.yellow_flower)
            {
                object = (new ItemMultiTexture(Blocks.yellow_flower, Blocks.yellow_flower, BlockFlower.field_149858_b)).setUnlocalizedName("flower");
            }
            else if (block == Blocks.red_flower)
            {
                object = (new ItemMultiTexture(Blocks.red_flower, Blocks.red_flower, BlockFlower.field_149859_a)).setUnlocalizedName("rose");
            }
            else if (block == Blocks.snow_layer)
            {
                object = new ItemSnow(Blocks.snow_layer, Blocks.snow_layer);
            }
            else if (block == Blocks.waterlily)
            {
                object = new ItemLilyPad(Blocks.waterlily);
            }
            else if (block == Blocks.piston)
            {
                object = new ItemPiston(Blocks.piston);
            }
            else if (block == Blocks.sticky_piston)
            {
                object = new ItemPiston(Blocks.sticky_piston);
            }
            else if (block == Blocks.cobblestone_wall)
            {
                object = (new ItemMultiTexture(Blocks.cobblestone_wall, Blocks.cobblestone_wall, BlockWall.field_150092_a)).setUnlocalizedName("cobbleWall");
            }
            else if (block == Blocks.anvil)
            {
                object = (new ItemAnvilBlock(Blocks.anvil)).setUnlocalizedName("anvil");
            }
            else if (block == Blocks.double_plant)
            {
                object = (new ItemDoublePlant(Blocks.double_plant, Blocks.double_plant, BlockDoublePlant.field_149892_a)).setUnlocalizedName("doublePlant");
            }
            else
            {
                if (hashset.contains(block))
                {
                    continue;
                }

                object = new ItemBlock(block);
            }

            itemRegistry.addObject(Block.getIdFromBlock(block), s, object);
        }
    }

    public Item setMaxStackSize(int p_77625_1_)
    {
        this.maxStackSize = p_77625_1_;
        return this;
    }

    /**
     * Returns 0 for /terrain.png, 1 for /gui/items.png
     */
    @SideOnly(Side.CLIENT)
    public int getSpriteNumber()
    {
        return 1;
    }

    /**
     * Gets an icon index based on an item's damage value
     */
    @SideOnly(Side.CLIENT)
    public IIcon getIconFromDamage(int p_77617_1_)
    {
        return this.itemIcon;
    }

    /**
     * Returns the icon index of the stack given as argument.
     */
    @SideOnly(Side.CLIENT)
    public IIcon getIconIndex(ItemStack p_77650_1_)
    {
        return this.getIconFromDamage(p_77650_1_.getItemDamage());
    }

    /**
     * Callback for item usage. If the item does something special on right clicking, he will have one of those. Return
     * True if something happen and false if it don't. This is for ITEMS, not BLOCKS
     */
    public boolean onItemUse(ItemStack p_77648_1_, EntityPlayer p_77648_2_, World p_77648_3_, int p_77648_4_, int p_77648_5_, int p_77648_6_, int p_77648_7_, float p_77648_8_, float p_77648_9_, float p_77648_10_)
    {
        return false;
    }

    public float func_150893_a(ItemStack p_150893_1_, Block p_150893_2_)
    {
        return 1.0F;
    }

    /**
     * Called whenever this item is equipped and the right mouse button is pressed. Args: itemStack, world, entityPlayer
     */
    public ItemStack onItemRightClick(ItemStack p_77659_1_, World p_77659_2_, EntityPlayer p_77659_3_)
    {
        return p_77659_1_;
    }

    public ItemStack onEaten(ItemStack p_77654_1_, World p_77654_2_, EntityPlayer p_77654_3_)
    {
        return p_77654_1_;
    }

    /**
     * Returns the maximum size of the stack for a specific item.
     */
    @Deprecated
    public int getItemStackLimit()
    {
        return this.maxStackSize;
    }

    /**
     * Returns the metadata of the block which this Item (ItemBlock) can place
     */
    public int getMetadata(int p_77647_1_)
    {
        return 0;
    }

    public boolean getHasSubtypes()
    {
        return this.hasSubtypes;
    }

    public Item setHasSubtypes(boolean p_77627_1_)
    {
        this.hasSubtypes = p_77627_1_;
        return this;
    }

    /**
     * Returns the maximum damage an item can take.
     */
    public int getMaxDamage()
    {
        return this.maxDamage;
    }

    /**
     * set max damage of an Item
     */
    public Item setMaxDamage(int p_77656_1_)
    {
        this.maxDamage = p_77656_1_;
        return this;
    }

    public boolean isDamageable()
    {
        return this.maxDamage > 0 && !this.hasSubtypes;
    }

    /**
     * Current implementations of this method in child classes do not use the entry argument beside ev. They just raise
     * the damage on the stack.
     */
    public boolean hitEntity(ItemStack p_77644_1_, EntityLivingBase p_77644_2_, EntityLivingBase p_77644_3_)
    {
        return false;
    }

    public boolean onBlockDestroyed(ItemStack p_150894_1_, World p_150894_2_, Block p_150894_3_, int p_150894_4_, int p_150894_5_, int p_150894_6_, EntityLivingBase p_150894_7_)
    {
        return false;
    }

    public boolean func_150897_b(Block p_150897_1_)
    {
        return false;
    }

    /**
     * Returns true if the item can be used on the given entity, e.g. shears on sheep.
     */
    public boolean itemInteractionForEntity(ItemStack p_111207_1_, EntityPlayer p_111207_2_, EntityLivingBase p_111207_3_)
    {
        return false;
    }

    /**
     * Sets bFull3D to True and return the object.
     */
    public Item setFull3D()
    {
        this.bFull3D = true;
        return this;
    }

    /**
     * Returns True is the item is renderer in full 3D when hold.
     */
    @SideOnly(Side.CLIENT)
    public boolean isFull3D()
    {
        return this.bFull3D;
    }

    /**
     * Returns true if this item should be rotated by 180 degrees around the Y axis when being held in an entities
     * hands.
     */
    @SideOnly(Side.CLIENT)
    public boolean shouldRotateAroundWhenRendering()
    {
        return false;
    }

    /**
     * Sets the unlocalized name of this item to the string passed as the parameter, prefixed by "item."
     */
    public Item setUnlocalizedName(String p_77655_1_)
    {
        this.unlocalizedName = p_77655_1_;
        return this;
    }

    /**
     * Translates the unlocalized name of this item, but without the .name suffix, so the translation fails and the
     * unlocalized name itself is returned.
     */
    public String getUnlocalizedNameInefficiently(ItemStack p_77657_1_)
    {
        String s = this.getUnlocalizedName(p_77657_1_);
        return s == null ? "" : StatCollector.translateToLocal(s);
    }

    /**
     * Returns the unlocalized name of this item.
     */
    public String getUnlocalizedName()
    {
        return "item." + this.unlocalizedName;
    }

    /**
     * Returns the unlocalized name of this item. This version accepts an ItemStack so different stacks can have
     * different names based on their damage or NBT.
     */
    public String getUnlocalizedName(ItemStack p_77667_1_)
    {
        return "item." + this.unlocalizedName;
    }

    public Item setContainerItem(Item p_77642_1_)
    {
        this.containerItem = p_77642_1_;
        return this;
    }

    /**
     * If this returns true, after a recipe involving this item is crafted the container item will be added to the
     * player's inventory instead of remaining in the crafting grid.
     */
    public boolean doesContainerItemLeaveCraftingGrid(ItemStack p_77630_1_)
    {
        return true;
    }

    /**
     * If this function returns true (or the item is damageable), the ItemStack's NBT tag will be sent to the client.
     */
    public boolean getShareTag()
    {
        return true;
    }

    public Item getContainerItem()
    {
        return this.containerItem;
    }

    /**
     * True if this Item has a container item (a.k.a. crafting result)
     */
    @Deprecated // Use ItemStack sensitive version below.
    public boolean hasContainerItem()
    {
        return this.containerItem != null;
    }

    @SideOnly(Side.CLIENT)
    public int getColorFromItemStack(ItemStack p_82790_1_, int p_82790_2_)
    {
        return 16777215;
    }

    /**
     * Called each tick as long the item is on a player inventory. Uses by maps to check if is on a player hand and
     * update it's contents.
     */
    public void onUpdate(ItemStack p_77663_1_, World p_77663_2_, Entity p_77663_3_, int p_77663_4_, boolean p_77663_5_) {}

    /**
     * Called when item is crafted/smelted. Used only by maps so far.
     */
    public void onCreated(ItemStack p_77622_1_, World p_77622_2_, EntityPlayer p_77622_3_) {}

    /**
     * false for all Items except sub-classes of ItemMapBase
     */
    public boolean isMap()
    {
        return false;
    }

    /**
     * returns the action that specifies what animation to play when the items is being used
     */
    public EnumAction getItemUseAction(ItemStack p_77661_1_)
    {
        return EnumAction.none;
    }

    /**
     * How long it takes to use or consume an item
     */
    public int getMaxItemUseDuration(ItemStack p_77626_1_)
    {
        return 0;
    }

    /**
     * called when the player releases the use item button. Args: itemstack, world, entityplayer, itemInUseCount
     */
    public void onPlayerStoppedUsing(ItemStack p_77615_1_, World p_77615_2_, EntityPlayer p_77615_3_, int p_77615_4_) {}

    /**
     * Sets the string representing this item's effect on a potion when used as an ingredient.
     */
    public Item setPotionEffect(String p_77631_1_)
    {
        this.potionEffect = p_77631_1_;
        return this;
    }

    /**
     * Returns a string representing what this item does to a potion.
     */
    public String getPotionEffect(ItemStack p_150896_1_)
    {
        return this.potionEffect;
    }

    /**
     * Returns true if this item serves as a potion ingredient (its ingredient information is not null).
     */
    public boolean isPotionIngredient(ItemStack p_150892_1_)
    {
        return this.getPotionEffect(p_150892_1_) != null;
    }

    /**
     * allows items to add custom lines of information to the mouseover description
     */
    @SideOnly(Side.CLIENT)
    public void addInformation(ItemStack p_77624_1_, EntityPlayer p_77624_2_, List p_77624_3_, boolean p_77624_4_) {}

    public String getItemStackDisplayName(ItemStack p_77653_1_)
    {
        return ("" + StatCollector.translateToLocal(this.getUnlocalizedNameInefficiently(p_77653_1_) + ".name")).trim();
    }

    @SideOnly(Side.CLIENT)
    @Deprecated
    public boolean hasEffect(ItemStack p_77636_1_)
    {
        return p_77636_1_.isItemEnchanted();
    }

    /**
     * Return an item rarity from EnumRarity
     */
    public EnumRarity getRarity(ItemStack p_77613_1_)
    {
        return p_77613_1_.isItemEnchanted() ? EnumRarity.rare : EnumRarity.common;
    }

    /**
     * Checks isDamagable and if it cannot be stacked
     */
    public boolean isItemTool(ItemStack p_77616_1_)
    {
        return this.getItemStackLimit(p_77616_1_) == 1 && this.isDamageable();
    }

    protected MovingObjectPosition getMovingObjectPositionFromPlayer(World p_77621_1_, EntityPlayer p_77621_2_, boolean p_77621_3_)
    {
        float f = 1.0F;
        float f1 = p_77621_2_.prevRotationPitch + (p_77621_2_.rotationPitch - p_77621_2_.prevRotationPitch) * f;
        float f2 = p_77621_2_.prevRotationYaw + (p_77621_2_.rotationYaw - p_77621_2_.prevRotationYaw) * f;
        double d0 = p_77621_2_.prevPosX + (p_77621_2_.posX - p_77621_2_.prevPosX) * (double)f;
        double d1 = p_77621_2_.prevPosY + (p_77621_2_.posY - p_77621_2_.prevPosY) * (double)f + (double)(p_77621_1_.isRemote ? p_77621_2_.getEyeHeight() - p_77621_2_.getDefaultEyeHeight() : p_77621_2_.getEyeHeight()); // isRemote check to revert changes to ray trace position due to adding the eye height clientside and player yOffset differences
        double d2 = p_77621_2_.prevPosZ + (p_77621_2_.posZ - p_77621_2_.prevPosZ) * (double)f;
        Vec3 vec3 = Vec3.createVectorHelper(d0, d1, d2);
        float f3 = MathHelper.cos(-f2 * 0.017453292F - (float)Math.PI);
        float f4 = MathHelper.sin(-f2 * 0.017453292F - (float)Math.PI);
        float f5 = -MathHelper.cos(-f1 * 0.017453292F);
        float f6 = MathHelper.sin(-f1 * 0.017453292F);
        float f7 = f4 * f5;
        float f8 = f3 * f5;
        double d3 = 5.0D;
        if (p_77621_2_ instanceof EntityPlayerMP)
        {
            d3 = ((EntityPlayerMP)p_77621_2_).theItemInWorldManager.getBlockReachDistance();
        }
        Vec3 vec31 = vec3.addVector((double)f7 * d3, (double)f6 * d3, (double)f8 * d3);
        return p_77621_1_.func_147447_a(vec3, vec31, p_77621_3_, !p_77621_3_, false);
    }

    /**
     * Return the enchantability factor of the item, most of the time is based on material.
     */
    public int getItemEnchantability()
    {
        return 0;
    }

    @SideOnly(Side.CLIENT)
    public boolean requiresMultipleRenderPasses()
    {
        return false;
    }

    /**
     * Gets an icon index based on an item's damage value and the given render pass
     */
    @SideOnly(Side.CLIENT)
    public IIcon getIconFromDamageForRenderPass(int p_77618_1_, int p_77618_2_)
    {
        return this.getIconFromDamage(p_77618_1_);
    }

    /**
     * returns a list of items with the same ID, but different meta (eg: dye returns 16 items)
     */
    @SideOnly(Side.CLIENT)
    public void getSubItems(Item p_150895_1_, CreativeTabs p_150895_2_, List p_150895_3_)
    {
        p_150895_3_.add(new ItemStack(p_150895_1_, 1, 0));
    }

    /**
     * returns this;
     */
    public Item setCreativeTab(CreativeTabs p_77637_1_)
    {
        this.tabToDisplayOn = p_77637_1_;
        return this;
    }

    /**
     * gets the CreativeTab this item is displayed on
     */
    @SideOnly(Side.CLIENT)
    public CreativeTabs getCreativeTab()
    {
        return this.tabToDisplayOn;
    }

    /**
     * Returns true if players can use this item to affect the world (e.g. placing blocks, placing ender eyes in portal)
     * when not in creative
     */
    public boolean canItemEditBlocks()
    {
        return true;
    }

    /**
     * Return whether this item is repairable in an anvil.
     */
    public boolean getIsRepairable(ItemStack p_82789_1_, ItemStack p_82789_2_)
    {
        return false;
    }

    @SideOnly(Side.CLIENT)
    public void registerIcons(IIconRegister p_94581_1_)
    {
        this.itemIcon = p_94581_1_.registerIcon(this.getIconString());
    }

    /**
     * Gets a map of item attribute modifiers, used by ItemSword to increase hit damage.
     */
    @Deprecated
    public Multimap getItemAttributeModifiers()
    {
        return HashMultimap.create();
    }

    public Item setTextureName(String p_111206_1_)
    {
        this.iconString = p_111206_1_;
        return this;
    }

    /**
     * Returns the string associated with this Item's Icon.
     */
    @SideOnly(Side.CLIENT)
    protected String getIconString()
    {
        return this.iconString == null ? "MISSING_ICON_ITEM_" + itemRegistry.getIDForObject(this) + "_" + this.unlocalizedName : this.iconString;
    }

    /* ======================================== FORGE START =====================================*/
    /**
     * ItemStack sensitive version of getItemAttributeModifiers
     */
    public Multimap getAttributeModifiers(ItemStack stack)
    {
        return this.getItemAttributeModifiers();
    }

    /**
     * Called when a player drops the item into the world,
     * returning false from this will prevent the item from
     * being removed from the players inventory and spawning
     * in the world
     *
     * @param player The player that dropped the item
     * @param item The item stack, before the item is removed.
     */
    public boolean onDroppedByPlayer(ItemStack item, EntityPlayer player)
    {
        return true;
    }

    /**
     * This is called when the item is used, before the block is activated.
     * @param stack The Item Stack
     * @param player The Player that used the item
     * @param world The Current World
     * @param x Target X Position
     * @param y Target Y Position
     * @param z Target Z Position
     * @param side The side of the target hit
     * @return Return true to prevent any further processing.
     */
    public boolean onItemUseFirst(ItemStack stack, EntityPlayer player, World world, int x, int y, int z, int side, float hitX, float hitY, float hitZ)
    {
        return false;
    }

    /**
     * Metadata-sensitive version of getStrVsBlock
     * @param itemstack The Item Stack
     * @param block The block the item is trying to break
     * @param metadata The items current metadata
     * @return The damage strength
     */
    public float getDigSpeed(ItemStack itemstack, Block block, int metadata)
    {
        return func_150893_a(itemstack, block);
    }

    protected boolean canRepair = true;
    /**
     * Called by CraftingManager to determine if an item is reparable.
     * @return True if reparable
     */
    public boolean isRepairable()
    {
        return canRepair && isDamageable();
    }

    /**
     * Call to disable repair recipes.
     * @return The current Item instance
     */
    public Item setNoRepair()
    {
        canRepair = false;
        return this;
    }

    /**
     * Called before a block is broken.  Return true to prevent default block harvesting.
     *
     * Note: In SMP, this is called on both client and server sides!
     *
     * @param itemstack The current ItemStack
     * @param X The X Position
     * @param Y The X Position
     * @param Z The X Position
     * @param player The Player that is wielding the item
     * @return True to prevent harvesting, false to continue as normal
     */
    public boolean onBlockStartBreak(ItemStack itemstack, int X, int Y, int Z, EntityPlayer player)
    {
        return false;
    }

    /**
     * Called each tick while using an item.
     * @param stack The Item being used
     * @param player The Player using the item
     * @param count The amount of time in tick the item has been used for continuously
     */
    public void onUsingTick(ItemStack stack, EntityPlayer player, int count)
    {
    }

    /**
     * Called when the player Left Clicks (attacks) an entity.
     * Processed before damage is done, if return value is true further processing is canceled
     * and the entity is not attacked.
     *
     * @param stack The Item being used
     * @param player The player that is attacking
     * @param entity The entity being attacked
     * @return True to cancel the rest of the interaction.
     */
    public boolean onLeftClickEntity(ItemStack stack, EntityPlayer player, Entity entity)
    {
        return false;
    }

    /**
     * Player, Render pass, and item usage sensitive version of getIconIndex.
     *
     * @param stack The item stack to get the icon for. (Usually this, and usingItem will be the same if usingItem is not null)
     * @param renderPass The pass to get the icon for, 0 is default.
     * @param player The player holding the item
     * @param usingItem The item the player is actively using. Can be null if not using anything.
     * @param useRemaining The ticks remaining for the active item.
     * @return The icon index
     */
    public IIcon getIcon(ItemStack stack, int renderPass, EntityPlayer player, ItemStack usingItem, int useRemaining)
    {
        return getIcon(stack, renderPass);
    }

    /**
     * Returns the number of render passes/layers this item has.
     * Usually equates to ItemRenderer.renderItem being called for this many passes.
     * Does not get called unless requiresMultipleRenderPasses() is true;
     *
     * @param metadata The item's metadata
     * @return The number of passes to run.
     */
    public int getRenderPasses(int metadata)
    {
        return requiresMultipleRenderPasses() ? 2 : 1;
    }

    /**
     * ItemStack sensitive version of getContainerItem.
     * Returns a full ItemStack instance of the result.
     *
     * @param itemStack The current ItemStack
     * @return The resulting ItemStack
     */
    public ItemStack getContainerItem(ItemStack itemStack)
    {
        if (!hasContainerItem(itemStack))
        {
            return null;
        }
        return new ItemStack(getContainerItem());
    }

    /**
     * ItemStack sensitive version of hasContainerItem
     * @param stack The current item stack
     * @return True if this item has a 'container'
     */
    public boolean hasContainerItem(ItemStack stack)
    {
        /**
         * True if this Item has a container item (a.k.a. crafting result)
         */
        return hasContainerItem();
    }

    /**
     * Retrieves the normal 'lifespan' of this item when it is dropped on the ground as a EntityItem.
     * This is in ticks, standard result is 6000, or 5 mins.
     *
     * @param itemStack The current ItemStack
     * @param world The world the entity is in
     * @return The normal lifespan in ticks.
     */
    public int getEntityLifespan(ItemStack itemStack, World world)
    {
        return 6000;
    }

    /**
     * Determines if this Item has a special entity for when they are in the world.
     * Is called when a EntityItem is spawned in the world, if true and Item#createCustomEntity
     * returns non null, the EntityItem will be destroyed and the new Entity will be added to the world.
     *
     * @param stack The current item stack
     * @return True of the item has a custom entity, If true, Item#createCustomEntity will be called
     */
    public boolean hasCustomEntity(ItemStack stack)
    {
        return false;
    }

    /**
     * This function should return a new entity to replace the dropped item.
     * Returning null here will not kill the EntityItem and will leave it to function normally.
     * Called when the item it placed in a world.
     *
     * @param world The world object
     * @param location The EntityItem object, useful for getting the position of the entity
     * @param itemstack The current item stack
     * @return A new Entity object to spawn or null
     */
    public Entity createEntity(World world, Entity location, ItemStack itemstack)
    {
        return null;
    }

    /**
     * Called by the default implemetation of EntityItem's onUpdate method, allowing for cleaner
     * control over the update of the item without having to write a subclass.
     *
     * @param entityItem The entity Item
     * @return Return true to skip any further update code.
     */
    public boolean onEntityItemUpdate(EntityItem entityItem)
    {
        return false;
    }

    /**
     * Gets a list of tabs that items belonging to this class can display on,
     * combined properly with getSubItems allows for a single item to span
     * many sub-items across many tabs.
     *
     * @return A list of all tabs that this item could possibly be one.
     */
    public CreativeTabs[] getCreativeTabs()
    {
        return new CreativeTabs[]{ getCreativeTab() };
    }

    /**
     * Determines the base experience for a player when they remove this item from a furnace slot.
     * This number must be between 0 and 1 for it to be valid.
     * This number will be multiplied by the stack size to get the total experience.
     *
     * @param item The item stack the player is picking up.
     * @return The amount to award for each item.
     */
    public float getSmeltingExperience(ItemStack item)
    {
        return -1; //-1 will default to the old lookups.
    }

    /**
     * Return the correct icon for rendering based on the supplied ItemStack and render pass.
     *
     * Defers to {@link #getIconFromDamageForRenderPass(int, int)}
     * @param stack to render for
     * @param pass the multi-render pass
     * @return the icon
     */
    public IIcon getIcon(ItemStack stack, int pass)
    {
        /**
         * Gets an icon index based on an item's damage value and the given render pass
         */
        return getIconFromDamageForRenderPass(stack.getItemDamage(), pass);
    }

    /**
     * Generates the base Random item for a specific instance of the chest gen,
     * Enchanted books use this to pick a random enchantment.
     *
     * @param chest The chest category to generate for
     * @param rnd World RNG
     * @param original Original result registered with the chest gen hooks.
     * @return New values to use as the random item, typically this will be original
     */
    public WeightedRandomChestContent getChestGenBase(ChestGenHooks chest, Random rnd, WeightedRandomChestContent original)
    {
        if (this instanceof ItemEnchantedBook)
        {
            return ((ItemEnchantedBook)this).func_92112_a(rnd,
                    original.theMinimumChanceToGenerateItem,
                    original.theMaximumChanceToGenerateItem, original.itemWeight);
        }
        return original;
    }

    /**
     *
     * Should this item, when held, allow sneak-clicks to pass through to the underlying block?
     *
     * @param world The world
     * @param x The X Position
     * @param y The X Position
     * @param z The X Position
     * @param player The Player that is wielding the item
     * @return
     */
    public boolean doesSneakBypassUse(World world, int x, int y, int z, EntityPlayer player)
    {
        return false;
    }

    /**
     * Called to tick armor in the armor slot. Override to do something
     *
     * @param world
     * @param player
     * @param itemStack
     */
    public void onArmorTick(World world, EntityPlayer player, ItemStack itemStack)
    {

    }

    /**
     * Determines if the specific ItemStack can be placed in the specified armor slot.
     *
     * @param stack The ItemStack
     * @param armorType Armor slot ID: 0: Helmet, 1: Chest, 2: Legs, 3: Boots
     * @param entity The entity trying to equip the armor
     * @return True if the given ItemStack can be inserted in the slot
     */
    public boolean isValidArmor(ItemStack stack, int armorType, Entity entity)
    {
        if (this instanceof ItemArmor)
        {
            return ((ItemArmor)this).armorType == armorType;
        }

        if (armorType == 0)
        {
            return this == Item.getItemFromBlock(Blocks.pumpkin) || this == Items.skull;
        }

        return false;
    }

    /**
     * Allow or forbid the specific book/item combination as an anvil enchant
     *
     * @param stack The item
     * @param book The book
     * @return if the enchantment is allowed
     */
    public boolean isBookEnchantable(ItemStack stack, ItemStack book)
    {
        return true;
    }

    /**
     * Called by RenderBiped and RenderPlayer to determine the armor texture that
     * should be use for the currently equiped item.
     * This will only be called on instances of ItemArmor.
     *
     * Returning null from this function will use the default value.
     *
     * @param stack ItemStack for the equpt armor
     * @param entity The entity wearing the armor
     * @param slot The slot the armor is in
     * @param type The subtype, can be null or "overlay"
     * @return Path of texture to bind, or null to use default
     */
    public String getArmorTexture(ItemStack stack, Entity entity, int slot, String type)
    {
        return null;
    }

    /**
     * Returns the font renderer used to render tooltips and overlays for this item.
     * Returning null will use the standard font renderer.
     *
     * @param stack The current item stack
     * @return A instance of FontRenderer or null to use default
     */
    @SideOnly(Side.CLIENT)
    public FontRenderer getFontRenderer(ItemStack stack)
    {
        return null;
    }

    /**
     * Override this method to have an item handle its own armor rendering.
     *
     * @param  entityLiving  The entity wearing the armor
     * @param  itemStack  The itemStack to render the model of
     * @param  armorSlot  0=head, 1=torso, 2=legs, 3=feet
     *
     * @return  A ModelBiped to render instead of the default
     */
    @SideOnly(Side.CLIENT)
    public ModelBiped getArmorModel(EntityLivingBase entityLiving, ItemStack itemStack, int armorSlot)
    {
        return null;
    }

    /**
     * Called when a entity tries to play the 'swing' animation.
     *
     * @param entityLiving The entity swinging the item.
     * @param stack The Item stack
     * @return True to cancel any further processing by EntityLiving
     */
    public boolean onEntitySwing(EntityLivingBase entityLiving, ItemStack stack)
    {
        return false;
    }

    /**
     * Called when the client starts rendering the HUD, for whatever item the player currently has as a helmet.
     * This is where pumpkins would render there overlay.
     *
     * @param stack The ItemStack that is equipped
     * @param player Reference to the current client entity
     * @param resolution Resolution information about the current viewport and configured GUI Scale
     * @param partialTicks Partial ticks for the renderer, useful for interpolation
     * @param hasScreen If the player has a screen up, which will be rendered after this.
     * @param mouseX Mouse's X position on screen
     * @param mouseY Mouse's Y position on screen
     */
    @SideOnly(Side.CLIENT)
    public void renderHelmetOverlay(ItemStack stack, EntityPlayer player, ScaledResolution resolution, float partialTicks, boolean hasScreen, int mouseX, int mouseY){}

    /**
     * Return the itemDamage represented by this ItemStack. Defaults to the itemDamage field on ItemStack, but can be overridden here for other sources such as NBT.
     *
     * @param stack The itemstack that is damaged
     * @return the damage value
     */
    public int getDamage(ItemStack stack)
    {
        return stack.itemDamage;
    }

    @Deprecated // This isn't 'display' its normal Damage, the above function is 'Aux' data, but the same value.
    public int getDisplayDamage(ItemStack stack)
    {
        return stack.itemDamage;
    }

    /**
     * Determines if the durability bar should be rendered for this item.
     * Defaults to vanilla stack.isDamaged behavior.
     * But modders can use this for any data they wish.
     *
     * @param stack The current Item Stack
     * @return True if it should render the 'durability' bar.
     */
    public boolean showDurabilityBar(ItemStack stack)
    {
        return stack.isItemDamaged();
    }

    /**
     * Queries the percentage of the 'Durability' bar that should be drawn.
     *
     * @param stack The current ItemStack
     * @return 1.0 for 100% 0 for 0%
     */
    public double getDurabilityForDisplay(ItemStack stack)
    {
        return (double)stack.getItemDamageForDisplay() / (double)stack.getMaxDamage();
    }

    /**
     * Return the maxDamage for this ItemStack. Defaults to the maxDamage field in this item,
     * but can be overridden here for other sources such as NBT.
     *
     * @param stack The itemstack that is damaged
     * @return the damage value
     */
    public int getMaxDamage(ItemStack stack)
    {
        /**
         * Returns the maximum damage an item can take.
         */
        return getMaxDamage();
    }

    /**
     * Return if this itemstack is damaged. Note only called if {@link #isDamageable()} is true.
     * @param stack the stack
     * @return if the stack is damaged
     */
    public boolean isDamaged(ItemStack stack)
    {
        return stack.itemDamage > 0;
    }

    /**
     * Set the damage for this itemstack. Note, this method is responsible for zero checking.
     * @param stack the stack
     * @param damage the new damage value
     */
    public void setDamage(ItemStack stack, int damage)
    {
        stack.itemDamage = damage;

        if (stack.itemDamage < 0)
        {
            stack.itemDamage = 0;
        }
    }

    /**
     * ItemStack sensitive version of {@link #canHarvestBlock(Block)}
     * @param par1Block The block trying to harvest
     * @param itemStack The itemstack used to harvest the block
     * @return true if can harvest the block
     */
    public boolean canHarvestBlock(Block par1Block, ItemStack itemStack)
    {
        return func_150897_b(par1Block);
    }

    /**
     * Render Pass sensitive version of hasEffect()
     */
    @SideOnly(Side.CLIENT)
    public boolean hasEffect(ItemStack par1ItemStack, int pass)
    {
        return hasEffect(par1ItemStack) && (pass == 0 || this != Items.potionitem);
    }

    /**
     * Gets the maximum number of items that this stack should be able to hold.
     * This is a ItemStack (and thus NBT) sensitive version of Item.getItemStackLimit()
     *
     * @param stack The ItemStack
     * @return THe maximum number this item can be stacked to
     */
    public int getItemStackLimit(ItemStack stack)
    {
        return this.getItemStackLimit();
    }

    private HashMap<String, Integer> toolClasses = new HashMap<String, Integer>();
    /**
     * Sets or removes the harvest level for the specified tool class.
     *
     * @param toolClass Class
     * @param level Harvest level:
     *     Wood:    0
     *     Stone:   1
     *     Iron:    2
     *     Diamond: 3
     *     Gold:    0
     */
    public void setHarvestLevel(String toolClass, int level)
    {
        if (level < 0)
            toolClasses.remove(toolClass);
        else
            toolClasses.put(toolClass, level);
    }

    public Set<String> getToolClasses(ItemStack stack)
    {
        return toolClasses.keySet();
    }

    /**
     * Queries the harvest level of this item stack for the specifred tool class,
     * Returns -1 if this tool is not of the specified type
     *
     * @param stack This item stack instance
     * @param toolClass Tool Class
     * @return Harvest level, or -1 if not the specified tool type.
     */
    public int getHarvestLevel(ItemStack stack, String toolClass)
    {
        Integer ret = toolClasses.get(toolClass);
        return ret == null ? -1 : ret;
    }

    /**
     * ItemStack sensitive version of getItemEnchantability
     *
     * @param stack The ItemStack
     * @return the item echantability value
     */
    public int getItemEnchantability(ItemStack stack)
    {
        /**
         * Return the enchantability factor of the item, most of the time is based on material.
         */
        return getItemEnchantability();
    }

    /**
     * Whether this Item can be used as a payment to activate the vanilla beacon.
     * @param stack the ItemStack
     * @return true if this Item can be used
     */
    public boolean isBeaconPayment(ItemStack stack)
    {
        return this == Items.emerald || this == Items.diamond || this == Items.gold_ingot || this == Items.iron_ingot;
    }
    /* ======================================== FORGE END   =====================================*/

    public static enum ToolMaterial
    {
        WOOD(0, 59, 2.0F, 0.0F, 15),
        STONE(1, 131, 4.0F, 1.0F, 5),
        IRON(2, 250, 6.0F, 2.0F, 14),
        EMERALD(3, 1561, 8.0F, 3.0F, 10),
        GOLD(0, 32, 12.0F, 0.0F, 22);
        /** The level of material this tool can harvest (3 = DIAMOND, 2 = IRON, 1 = STONE, 0 = WOOD/GOLD) */
        private final int harvestLevel;
        /** The number of uses this material allows. (wood = 59, stone = 131, iron = 250, diamond = 1561, gold = 32) */
        private final int maxUses;
        /** The strength of this tool material against blocks which it is effective against. */
        private final float efficiencyOnProperMaterial;
        /** Damage versus entities. */
        private final float damageVsEntity;
        /** Defines the natural enchantability factor of the material. */
        private final int enchantability;

        private static final String __OBFID = "CL_00000042";

        //Added by forge for custom Tool materials.
        @Deprecated public Item customCraftingMaterial = null;
        private ItemStack repairMaterial = null;

        private ToolMaterial(int p_i1874_3_, int p_i1874_4_, float p_i1874_5_, float p_i1874_6_, int p_i1874_7_)
        {
            this.harvestLevel = p_i1874_3_;
            this.maxUses = p_i1874_4_;
            this.efficiencyOnProperMaterial = p_i1874_5_;
            this.damageVsEntity = p_i1874_6_;
            this.enchantability = p_i1874_7_;
        }

        /**
         * The number of uses this material allows. (wood = 59, stone = 131, iron = 250, diamond = 1561, gold = 32)
         */
        public int getMaxUses()
        {
            return this.maxUses;
        }

        /**
         * The strength of this tool material against blocks which it is effective against.
         */
        public float getEfficiencyOnProperMaterial()
        {
            return this.efficiencyOnProperMaterial;
        }

        /**
         * Damage versus entities.
         */
        public float getDamageVsEntity()
        {
            return this.damageVsEntity;
        }

        /**
         * The level of material this tool can harvest (3 = DIAMOND, 2 = IRON, 1 = STONE, 0 = IRON/GOLD)
         */
        public int getHarvestLevel()
        {
            return this.harvestLevel;
        }

        /**
         * Return the natural enchantability factor of the material.
         */
        public int getEnchantability()
        {
            return this.enchantability;
        }

        @Deprecated // Use getRepairItemStack below
        public Item func_150995_f()
        {
            switch (this)
            {
                case WOOD:    return Item.getItemFromBlock(Blocks.planks);
                case STONE:   return Item.getItemFromBlock(Blocks.cobblestone);
                case GOLD:    return Items.gold_ingot;
                case IRON:    return Items.iron_ingot;
                case EMERALD: return Items.diamond;
                default:      return customCraftingMaterial;
            }
        }

        public ToolMaterial setRepairItem(ItemStack stack)
        {
            if (this.repairMaterial != null || customCraftingMaterial != null) throw new RuntimeException("Can not change already set repair material");
            if (this == WOOD || this == STONE || this == GOLD || this == IRON || this == EMERALD) throw new RuntimeException("Can not change vanilla tool repair materials");
            this.repairMaterial = stack;
            this.customCraftingMaterial = stack.getItem();
            return this;
        }

        public ItemStack getRepairItemStack()
        {
            if (repairMaterial != null) return repairMaterial;
            Item ret = this.func_150995_f();
            if (ret == null) return null;
            repairMaterial = new ItemStack(ret, 1, net.minecraftforge.oredict.OreDictionary.WILDCARD_VALUE);
            return repairMaterial;
        }
    }
}