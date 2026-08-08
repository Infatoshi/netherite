package qrl;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import java.util.Map;
import net.minecraft.enchantment.Enchantment;
import net.minecraft.enchantment.EnchantmentData;
import net.minecraft.enchantment.EnchantmentHelper;
import net.minecraft.entity.player.EntityPlayerMP;
import net.minecraft.init.Items;
import net.minecraft.inventory.ContainerRepair;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.world.WorldServer;

/** Isolated real-1.11.2 ContainerRepair computation oracle. */
final class AnvilOracle {
    private AnvilOracle() { }

    static ItemStack stack(JsonObject value) {
        if (value == null || !value.has("item")
                || value.get("item").getAsInt() == 0)
            return ItemStack.EMPTY;
        Item item = Item.getItemById(value.get("item").getAsInt());
        if (item == null) throw new IllegalArgumentException("unknown item");
        ItemStack stack = new ItemStack(
            item,
            value.has("count") ? value.get("count").getAsInt() : 1,
            value.has("meta") ? value.get("meta").getAsInt() : 0);
        if (value.has("repair"))
            stack.setRepairCost(value.get("repair").getAsInt());
        if (value.has("name") && !value.get("name").getAsString().isEmpty())
            stack.setStackDisplayName(value.get("name").getAsString());
        if (value.has("enchants")) {
            for (com.google.gson.JsonElement element
                    : value.getAsJsonArray("enchants")) {
                JsonObject encoded = element.getAsJsonObject();
                Enchantment enchantment = Enchantment.getEnchantmentByID(
                    encoded.get("id").getAsInt());
                int level = encoded.get("level").getAsInt();
                if (enchantment == null || level <= 0)
                    throw new IllegalArgumentException("invalid enchantment");
                if (item == Items.ENCHANTED_BOOK)
                    Items.ENCHANTED_BOOK.addEnchantment(
                        stack, new EnchantmentData(enchantment, level));
                else
                    stack.addEnchantment(enchantment, level);
            }
        }
        if (value.has("lore")) {
            net.minecraft.nbt.NBTTagList lore =
                new net.minecraft.nbt.NBTTagList();
            lore.appendTag(new net.minecraft.nbt.NBTTagString(
                value.get("lore").getAsString()));
            stack.getOrCreateSubCompound("display").setTag("Lore", lore);
        }
        return stack;
    }

    private static JsonObject encode(ItemStack stack) {
        JsonObject out = new JsonObject();
        if (stack == null || stack.isEmpty()) {
            out.addProperty("item", 0);
            out.addProperty("count", 0);
            out.addProperty("meta", 0);
            out.addProperty("repair", 0);
            out.addProperty("name", "");
            out.add("enchants", new JsonArray());
            return out;
        }
        out.addProperty("item", Item.getIdFromItem(stack.getItem()));
        out.addProperty("count", stack.getCount());
        out.addProperty("meta", stack.getMetadata());
        out.addProperty("repair", stack.getRepairCost());
        out.addProperty("name", stack.hasDisplayName()
            ? stack.getDisplayName() : "");
        JsonArray enchants = new JsonArray();
        for (Map.Entry<Enchantment, Integer> entry
                : EnchantmentHelper.getEnchantments(stack).entrySet()) {
            JsonObject value = new JsonObject();
            value.addProperty(
                "id", Enchantment.getEnchantmentID(entry.getKey()));
            value.addProperty("level", entry.getValue());
            enchants.add(value);
        }
        out.add("enchants", enchants);
        return out;
    }

    static JsonObject run(WorldServer world, EntityPlayerMP player,
                          JsonObject action) {
        int oldLevel = player.experienceLevel;
        boolean oldCreative = player.capabilities.isCreativeMode;
        try {
            boolean creative = action.has("creative")
                && action.get("creative").getAsBoolean();
            player.capabilities.isCreativeMode = creative;
            player.experienceLevel = action.has("level")
                ? action.get("level").getAsInt() : 30;
            ContainerRepair repair = new ContainerRepair(
                player.inventory, world, player);
            repair.getSlot(0).putStack(stack(action.getAsJsonObject("left")));
            repair.getSlot(1).putStack(stack(action.getAsJsonObject("right")));
            if (action.has("name"))
                repair.updateItemName(action.get("name").getAsString());
            JsonObject out = new JsonObject();
            out.addProperty("ok", true);
            out.addProperty("maximum_cost", repair.maximumCost);
            out.addProperty("material_cost", repair.materialCost);
            out.add("output", encode(repair.getSlot(2).getStack()));
            out.add("left", encode(repair.getSlot(0).getStack()));
            out.add("right", encode(repair.getSlot(1).getStack()));
            return out;
        } finally {
            player.experienceLevel = oldLevel;
            player.capabilities.isCreativeMode = oldCreative;
        }
    }
}
