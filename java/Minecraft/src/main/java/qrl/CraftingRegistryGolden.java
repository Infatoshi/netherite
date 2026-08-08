package qrl;

import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.List;
import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import com.google.gson.JsonPrimitive;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.init.Bootstrap;
import net.minecraft.init.Items;
import net.minecraft.init.PotionTypes;
import net.minecraft.inventory.Container;
import net.minecraft.inventory.InventoryCrafting;
import net.minecraft.item.Item;
import net.minecraft.item.ItemArmor;
import net.minecraft.item.ItemMap;
import net.minecraft.item.ItemStack;
import net.minecraft.item.crafting.CraftingManager;
import net.minecraft.item.crafting.IRecipe;
import net.minecraft.item.crafting.ShapedRecipes;
import net.minecraft.item.crafting.ShapelessRecipes;
import net.minecraft.util.NonNullList;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.nbt.NBTTagList;
import net.minecraft.nbt.CompressedStreamTools;
import net.minecraft.potion.PotionUtils;
import net.minecraft.tileentity.BannerPattern;
import net.minecraft.world.World;
import net.minecraft.world.storage.MapData;
import net.minecraft.world.storage.MapDecoration;
import net.minecraftforge.oredict.ShapedOreRecipe;
import net.minecraftforge.oredict.ShapelessOreRecipe;

/** Exhaustive ordinary-recipe matcher/result/remainder oracle. */
public final class CraftingRegistryGolden {
    private static final Container OWNER = new Container() {
        public boolean canInteractWith(EntityPlayer player) { return true; }
    };

    private static Object field(Object owner, String name) throws Exception {
        for (Class<?> type = owner.getClass(); type != null;
                type = type.getSuperclass()) {
            try {
                Field field = type.getDeclaredField(name);
                field.setAccessible(true);
                return field.get(owner);
            } catch (NoSuchFieldException ignored) { }
        }
        throw new NoSuchFieldException(owner.getClass().getName() + "." + name);
    }

    private static ItemStack first(Object ingredient) {
        if (ingredient instanceof ItemStack)
            return ((ItemStack)ingredient).copy();
        if (ingredient instanceof List<?>) {
            for (Object choice : (List<?>)ingredient)
                if (choice instanceof ItemStack
                        && !((ItemStack)choice).isEmpty())
                    return ((ItemStack)choice).copy();
        }
        return ItemStack.EMPTY;
    }

    private static final class Ordinary {
        boolean shaped;
        int width, height;
        final List<ItemStack> ingredients = new ArrayList<ItemStack>();
    }

    private static Ordinary ordinary(IRecipe recipe) throws Exception {
        Ordinary out = new Ordinary();
        if (recipe.getClass() == ShapedRecipes.class) {
            ShapedRecipes value = (ShapedRecipes)recipe;
            out.shaped = true;
            out.width = value.recipeWidth;
            out.height = value.recipeHeight;
            for (ItemStack stack : value.recipeItems)
                out.ingredients.add(stack.copy());
        } else if (recipe.getClass() == ShapelessRecipes.class) {
            for (ItemStack stack : ((ShapelessRecipes)recipe).recipeItems)
                out.ingredients.add(stack.copy());
        } else if (recipe.getClass() == ShapedOreRecipe.class) {
            out.shaped = true;
            out.width = ((Integer)field(recipe, "width")).intValue();
            out.height = ((Integer)field(recipe, "height")).intValue();
            for (Object ingredient : (Object[])field(recipe, "input"))
                out.ingredients.add(first(ingredient));
        } else if (recipe.getClass() == ShapelessOreRecipe.class) {
            for (Object ingredient : (List<?>)field(recipe, "input"))
                out.ingredients.add(first(ingredient));
        } else {
            return null;
        }
        return out;
    }

    private static InventoryCrafting grid(
            Ordinary recipe, int offsetX, int offsetY, boolean mirror) {
        InventoryCrafting grid = new InventoryCrafting(OWNER, 3, 3);
        if (recipe.shaped) {
            for (int y = 0; y < recipe.height; ++y)
                for (int x = 0; x < recipe.width; ++x) {
                    int sourceX = mirror ? recipe.width - x - 1 : x;
                    ItemStack stack = recipe.ingredients.get(
                        sourceX + y * recipe.width);
                    grid.setInventorySlotContents(
                        x + offsetX + (y + offsetY) * 3, stack.copy());
                }
        } else {
            for (int index = 0; index < recipe.ingredients.size(); ++index) {
                int source = mirror
                    ? recipe.ingredients.size() - index - 1 : index;
                grid.setInventorySlotContents(
                    index, recipe.ingredients.get(source).copy());
            }
        }
        return grid;
    }

    private static void appendStack(StringBuilder line, ItemStack stack) {
        if (stack == null || stack.isEmpty()) {
            line.append(" 0:0:0");
            return;
        }
        line.append(' ').append(Item.getIdFromItem(stack.getItem()))
            .append(':').append(stack.getCount())
            .append(':').append(stack.getMetadata());
    }

    private static String run(int index, String variant,
            InventoryCrafting grid) {
        ItemStack result = CraftingManager.getInstance()
            .findMatchingRecipe(grid, null);
        NonNullList<ItemStack> remaining = CraftingManager.getInstance()
            .getRemainingItems(grid, null);
        StringBuilder line = new StringBuilder();
        line.append("R ").append(index).append(' ').append(variant);
        appendStack(line, result);
        for (ItemStack stack : remaining) appendStack(line, stack);
        return line.toString();
    }

    private static ItemStack stack(int item, int count, int meta) {
        return new ItemStack(Item.getItemById(item), count, meta);
    }

    private static void put(InventoryCrafting grid, int slot,
            int item, int count, int meta) {
        grid.setInventorySlotContents(slot, stack(item, count, meta));
    }

    private static String specialRun(String name, InventoryCrafting grid,
            World world) {
        List<IRecipe> recipes = CraftingManager.getInstance().getRecipeList();
        int matched = -1;
        ItemStack result = ItemStack.EMPTY;
        NonNullList<ItemStack> remaining = NonNullList.withSize(9,
            ItemStack.EMPTY);
        for (int index = 0; index < recipes.size(); ++index) {
            IRecipe recipe = recipes.get(index);
            if (!recipe.matches(grid, world)) continue;
            matched = index;
            result = recipe.getCraftingResult(grid);
            remaining = recipe.getRemainingItems(grid);
            break;
        }
        StringBuilder line = new StringBuilder();
        line.append("S ").append(name).append(' ').append(matched);
        appendStack(line, result);
        line.append(" repair=").append(result.isEmpty()
            ? 0 : result.getRepairCost());
        NBTTagCompound tag = result.hasTagCompound()
            ? result.getTagCompound() : new NBTTagCompound();
        try {
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            DataOutputStream stream = new DataOutputStream(bytes);
            CompressedStreamTools.write(tag, stream);
            stream.close();
            line.append(" tag=");
            for (byte value : bytes.toByteArray())
                line.append(String.format("%02x", value & 255));
        } catch (Exception exception) {
            throw new RuntimeException(exception);
        }
        for (ItemStack stack : remaining) appendStack(line, stack);
        return line.toString();
    }

    private static String specialTargetRun(String name,
            InventoryCrafting grid, World world, int target) {
        IRecipe recipe = CraftingManager.getInstance().getRecipeList()
            .get(target);
        boolean matches = recipe.matches(grid, world);
        ItemStack result = matches
            ? recipe.getCraftingResult(grid) : ItemStack.EMPTY;
        NonNullList<ItemStack> remaining = matches
            ? recipe.getRemainingItems(grid)
            : NonNullList.withSize(9, ItemStack.EMPTY);
        StringBuilder line = new StringBuilder();
        line.append("S ").append(name).append(' ')
            .append(matches ? target : -1);
        appendStack(line, result);
        line.append(" repair=").append(result.isEmpty()
            ? 0 : result.getRepairCost());
        NBTTagCompound tag = result.hasTagCompound()
            ? result.getTagCompound() : new NBTTagCompound();
        try {
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            DataOutputStream stream = new DataOutputStream(bytes);
            CompressedStreamTools.write(tag, stream);
            stream.close();
            line.append(" tag=");
            for (byte value : bytes.toByteArray())
                line.append(String.format("%02x", value & 255));
        } catch (Exception exception) {
            throw new RuntimeException(exception);
        }
        for (ItemStack stack : remaining) appendStack(line, stack);
        return line.toString();
    }

    private static InventoryCrafting copyGrid(InventoryCrafting source) {
        InventoryCrafting copy = new InventoryCrafting(OWNER, 3, 3);
        for (int slot = 0; slot < 9; ++slot)
            copy.setInventorySlotContents(
                slot, source.getStackInSlot(slot).copy());
        return copy;
    }

    /** Valid case plus three target-recipe negative controls. */
    private static void addSpecialCases(List<String> lines, String name,
            InventoryCrafting grid, World world) {
        String valid = specialRun(name, grid, world);
        lines.add(valid);
        int target = Integer.parseInt(valid.split(" ")[2]);
        if (target < 0)
            throw new IllegalStateException("valid special case did not match");
        int first = -1, empty = -1;
        for (int slot = 0; slot < 9; ++slot) {
            if (grid.getStackInSlot(slot).isEmpty()) {
                if (empty < 0) empty = slot;
            } else if (first < 0) first = slot;
        }
        InventoryCrafting mutated = copyGrid(grid);
        mutated.setInventorySlotContents(first, ItemStack.EMPTY);
        lines.add(specialTargetRun(
            name + "_edge_remove", mutated, world, target));
        mutated = copyGrid(grid);
        mutated.setInventorySlotContents(first, stack(1, 1, 0));
        lines.add(specialTargetRun(
            name + "_edge_replace", mutated, world, target));
        mutated = copyGrid(grid);
        int extra = empty >= 0 ? empty : 8;
        mutated.setInventorySlotContents(extra, stack(1, 1, 0));
        lines.add(specialTargetRun(
            name + "_edge_extra", mutated, world, target));
    }

    /** One valid, NBT-discriminating input for every non-ordinary recipe. */
    public static List<String> captureSpecialLines(World world)
            throws Exception {
        List<String> lines = new ArrayList<String>();
        InventoryCrafting grid;

        grid = new InventoryCrafting(OWNER, 3, 3);
        ItemStack map = ItemMap.setupNewMap(world, 0.0, 0.0,
            (byte)1, true, false);
        for (int slot = 0; slot < 9; ++slot)
            grid.setInventorySlotContents(slot,
                slot == 4 ? map.copy() : stack(339, 1, 0));
        addSpecialCases(lines, "map_extend", grid, world);
        MapData mapData = Items.FILLED_MAP.getMapData(map, world);
        mapData.scale = 4;
        lines.add(specialTargetRun(
            "map_extend_scale4", grid, world, 136));
        mapData.scale = 1;
        mapData.mapDecorations.put("fixture", new MapDecoration(
            MapDecoration.Type.MANSION, (byte)0, (byte)0, (byte)0));
        lines.add(specialTargetRun(
            "map_extend_mansion", grid, world, 136));
        mapData.mapDecorations.put("fixture", new MapDecoration(
            MapDecoration.Type.MONUMENT, (byte)0, (byte)0, (byte)0));
        lines.add(specialTargetRun(
            "map_extend_monument", grid, world, 136));
        grid.setInventorySlotContents(4, stack(358, 1, 30000));
        lines.add(specialTargetRun(
            "map_extend_missing_data", grid, world, 136));

        grid = new InventoryCrafting(OWNER, 3, 3);
        ItemStack armor = stack(299, 1, 0);
        ((ItemArmor)armor.getItem()).setColor(armor, 0x204060);
        grid.setInventorySlotContents(0, armor);
        put(grid, 1, 351, 1, 1);
        put(grid, 2, 351, 1, 11);
        addSpecialCases(lines, "armor_dye", grid, world);

        grid = new InventoryCrafting(OWNER, 3, 3);
        grid.setInventorySlotContents(0, stack(299, 1, 0));
        put(grid, 1, 351, 1, 11);
        addSpecialCases(lines, "armor_dye_uncolored", grid, world);

        for (int dye = 0; dye < 16; ++dye) {
            grid = new InventoryCrafting(OWNER, 3, 3);
            armor = stack(299, 1, 37);
            ((ItemArmor)armor.getItem()).setColor(armor, 0x204060);
            grid.setInventorySlotContents(0, armor);
            put(grid, 8, 351, 1, dye);
            addSpecialCases(lines, "armor_dye_" + dye, grid, world);
        }

        grid = new InventoryCrafting(OWNER, 3, 3);
        put(grid, 0, 339, 1, 0); put(grid, 1, 289, 1, 0);
        put(grid, 2, 289, 1, 0);
        addSpecialCases(lines, "firework", grid, world);

        grid = new InventoryCrafting(OWNER, 3, 3);
        put(grid, 0, 289, 1, 0); put(grid, 1, 351, 1, 1);
        put(grid, 2, 264, 1, 0); put(grid, 3, 264, 1, 0);
        put(grid, 4, 348, 1, 0); put(grid, 5, 348, 1, 0);
        put(grid, 6, 288, 1, 0);
        addSpecialCases(lines,
            "firework_star_repeat_modifiers", grid, world);

        grid = new InventoryCrafting(OWNER, 3, 3);
        ItemStack fireworkStar = stack(402, 1, 0);
        NBTTagCompound starRoot = new NBTTagCompound();
        NBTTagCompound explosion = new NBTTagCompound();
        explosion.setByte("Type", (byte)3);
        explosion.setIntArray("Colors", new int[] {11743532});
        starRoot.setTag("Explosion", explosion);
        fireworkStar.setTagCompound(starRoot);
        grid.setInventorySlotContents(0, fireworkStar.copy());
        put(grid, 8, 351, 1, 4);
        addSpecialCases(lines, "firework_fade", grid, world);

        grid = new InventoryCrafting(OWNER, 3, 3);
        put(grid, 0, 339, 1, 0);
        put(grid, 1, 289, 1, 0); put(grid, 2, 289, 1, 0);
        put(grid, 3, 289, 1, 0);
        grid.setInventorySlotContents(4, fireworkStar.copy());
        ItemStack secondStar = fireworkStar.copy();
        secondStar.getTagCompound().getCompoundTag("Explosion")
            .setByte("Type", (byte)4);
        grid.setInventorySlotContents(5, secondStar);
        addSpecialCases(lines, "firework_rocket_stars", grid, world);

        for (BannerPattern bannerPattern : BannerPattern.values()) {
            if (!bannerPattern.hasPattern()) continue;
            grid = new InventoryCrafting(OWNER, 3, 3);
            if (bannerPattern.hasPatternItem()) {
                put(grid, 0, 425, 1, 0);
                put(grid, 1, 351, 1, 14);
                grid.setInventorySlotContents(
                    2, bannerPattern.getPatternItem().copy());
            } else {
                String[] shape = bannerPattern.getPatterns();
                int bannerSlot = -1;
                for (int slot = 0; slot < 9; ++slot) {
                    if (shape[slot / 3].charAt(slot % 3) == '#')
                        put(grid, slot, 351, 1, 14);
                    else if (bannerSlot < 0)
                        bannerSlot = slot;
                }
                if (bannerSlot < 0)
                    throw new IllegalStateException(
                        "banner pattern has no banner slot: "
                        + bannerPattern.getHashname());
                put(grid, bannerSlot, 425, 1, 0);
            }
            addSpecialCases(lines,
                "banner_pattern_" + bannerPattern.getHashname(),
                grid, world);
        }

        grid = new InventoryCrafting(OWNER, 3, 3);
        for (int slot = 0; slot < 9; ++slot) put(grid, slot, 262, 1, 0);
        grid.setInventorySlotContents(4, PotionUtils.addPotionToItemStack(
            stack(441, 1, 0), PotionTypes.STRONG_POISON));
        addSpecialCases(lines, "tipped_arrow", grid, world);

        grid = new InventoryCrafting(OWNER, 3, 3);
        grid.setInventorySlotContents(0, map.copy());
        put(grid, 1, 395, 1, 0); put(grid, 2, 395, 1, 0);
        addSpecialCases(lines, "map_clone", grid, world);

        grid = new InventoryCrafting(OWNER, 3, 3);
        ItemStack book = stack(387, 1, 0);
        NBTTagCompound bookTag = new NBTTagCompound();
        bookTag.setString("title", "Parity");
        bookTag.setString("author", "Oracle");
        bookTag.setInteger("generation", 1);
        NBTTagList pages = new NBTTagList();
        pages.appendTag(new net.minecraft.nbt.NBTTagString("{\"text\":\"A\"}"));
        bookTag.setTag("pages", pages); book.setTagCompound(bookTag);
        grid.setInventorySlotContents(0, book);
        put(grid, 1, 386, 1, 0); put(grid, 2, 386, 1, 0);
        addSpecialCases(lines, "book_clone", grid, world);

        grid = new InventoryCrafting(OWNER, 3, 3);
        put(grid, 0, 267, 1, 190); put(grid, 1, 267, 1, 210);
        addSpecialCases(lines, "repair", grid, world);

        for (Item item : Item.REGISTRY) {
            if (!item.isRepairable() || item.getMaxDamage() <= 0) continue;
            int itemId = Item.getIdFromItem(item);
            int maximum = item.getMaxDamage();
            grid = new InventoryCrafting(OWNER, 3, 3);
            put(grid, 0, itemId, 1, maximum / 3);
            put(grid, 8, itemId, 1, maximum * 2 / 3);
            addSpecialCases(lines, "repair_" + itemId, grid, world);
        }

        NBTTagCompound bannerRoot = new NBTTagCompound();
        NBTTagCompound blockEntity = new NBTTagCompound();
        blockEntity.setInteger("Base", 0);
        NBTTagList patterns = new NBTTagList();
        NBTTagCompound pattern = new NBTTagCompound();
        pattern.setString("Pattern", "cre"); pattern.setInteger("Color", 14);
        patterns.appendTag(pattern); blockEntity.setTag("Patterns", patterns);
        bannerRoot.setTag("BlockEntityTag", blockEntity);

        grid = new InventoryCrafting(OWNER, 3, 3);
        put(grid, 0, 442, 1, 0);
        ItemStack patternedBanner = stack(425, 1, 0);
        patternedBanner.setTagCompound(bannerRoot.copy());
        grid.setInventorySlotContents(1, patternedBanner.copy());
        addSpecialCases(lines, "shield_decor", grid, world);

        grid = new InventoryCrafting(OWNER, 3, 3);
        grid.setInventorySlotContents(0, patternedBanner.copy());
        put(grid, 1, 425, 1, 0);
        addSpecialCases(lines, "banner_duplicate", grid, world);

        for (int dye = 0; dye < 16; ++dye) {
            grid = new InventoryCrafting(OWNER, 3, 3);
            ItemStack shulker = stack(219 + dye, 1, 0);
            NBTTagCompound shulkerRoot = new NBTTagCompound();
            NBTTagCompound shulkerBlock = new NBTTagCompound();
            shulkerBlock.setString("CustomName", "Parity Box");
            shulkerRoot.setTag("BlockEntityTag", shulkerBlock);
            shulker.setTagCompound(shulkerRoot);
            grid.setInventorySlotContents(0, shulker);
            put(grid, 8, 351, 1, dye);
            addSpecialCases(lines, "shulker_color_" + dye, grid, world);
        }
        return lines;
    }

    private static void removeFirst(InventoryCrafting grid) {
        for (int slot = 0; slot < 9; ++slot)
            if (!grid.getStackInSlot(slot).isEmpty()) {
                grid.setInventorySlotContents(slot, ItemStack.EMPTY);
                return;
            }
    }

    public static List<String> captureLines() throws Exception {
        List<String> lines = new ArrayList<String>();
        List<IRecipe> recipes = CraftingManager.getInstance().getRecipeList();
        int ordinaryCount = 0;
        for (int index = 0; index < recipes.size(); ++index) {
            Ordinary recipe = ordinary(recipes.get(index));
            if (recipe == null) continue;
            ++ordinaryCount;
            lines.add(run(index, "P", grid(recipe, 0, 0, false)));
            if (recipe.shaped
                    && (recipe.width < 3 || recipe.height < 3))
                lines.add(run(index, "O", grid(
                    recipe, 3 - recipe.width, 3 - recipe.height, false)));
            if ((recipe.shaped && recipe.width > 1)
                    || (!recipe.shaped && recipe.ingredients.size() > 1))
                lines.add(run(index, "M", grid(recipe, 0, 0, true)));
            InventoryCrafting negative = grid(recipe, 0, 0, false);
            removeFirst(negative);
            lines.add(run(index, "N", negative));
        }
        lines.add("COUNT " + ordinaryCount + " " + recipes.size());
        return lines;
    }

    public static String captureJson(World world) throws Exception {
        JsonObject out = new JsonObject();
        out.addProperty("ok", true);
        out.addProperty("schema", "qrl.crafting_registry_cases.v1");
        JsonArray lines = new JsonArray();
        for (String line : captureLines())
            lines.add(new JsonPrimitive(line));
        out.add("lines", lines);
        JsonArray special = new JsonArray();
        for (String line : captureSpecialLines(world))
            special.add(new JsonPrimitive(line));
        out.add("special", special);
        return out.toString();
    }

    public static void main(String[] args) throws Exception {
        Bootstrap.register();
        for (String line : captureLines()) System.out.println(line);
    }
}
