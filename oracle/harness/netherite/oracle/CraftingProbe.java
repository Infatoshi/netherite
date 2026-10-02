package netherite.oracle;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import com.google.gson.JsonPrimitive;
import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Random;
import net.minecraft.block.Block;
import net.minecraft.init.Bootstrap;
import net.minecraft.inventory.Container;
import net.minecraft.inventory.InventoryCrafting;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.item.crafting.CraftingManager;
import net.minecraft.item.crafting.FurnaceRecipes;
import net.minecraft.item.crafting.IRecipe;
import net.minecraft.item.crafting.ShapedRecipes;
import net.minecraft.item.crafting.ShapelessRecipes;
import net.minecraft.server.integrated.IntegratedServer;

/**
 * Crafting and smelting cases, the reference for the native port.
 *
 * Cases are generated from `new java.util.Random(opseed)`, so native can make
 * the same sequence from the same seed. Each case fills a 3x3 or a 2x2
 * InventoryCrafting (the workbench and the player grid) and records the grid and
 * what CraftingManager.findMatchingRecipe returns for it:
 *
 *   50%  a random recipe's pattern placed at a random offset. A shaped recipe's
 *        pattern is placed as stored half the time and mirrored the other half,
 *        which is exactly what ShapedRecipes.matches tries; a shapeless recipe's
 *        ingredients go into random distinct slots.
 *   20%  one of those, then one random change (a slot emptied, or a slot set to
 *        a random stack).
 *   15%  every slot an independently random item and damage.
 *   15%  two damaged copies of one repairable item, in two random slots: the
 *        findMatchingRecipe tool repair path.
 *
 * The grid item set is fixed: every item that exists in the registry (so the
 * wildcard damage 32767 and the exact damage rules are both exercised), plus a
 * spread of damage values, plus a few stacks carrying NBT.
 *
 * Every smelting input is also recorded: every key of smeltingList, every
 * distinct item and every item/damage pair near one (the wildcard rule), each
 * with getSmeltingResult and getSmeltingExperience.
 *
 * Output DIR/manifest.json, DIR/cases.jsonl and DIR/smelting.jsonl. Item stacks
 * print as {"item":id,"count":n,"damage":d,"tag":<canonical NBT or null>}, the
 * canonical form csrc/engine/nbtjson.c reads. A case where vanilla itself throws
 * (the world argument is null and RecipesMapExtending reads it, or a written
 * book in the grid has no tag compound) records "throw":<exception simple name>
 * instead of "out".
 */
final class CraftingProbe
{
    private CraftingProbe() {}

    /** items a random grid or change may draw, built once from the registry */
    private static ItemStack[] pool;
    /** stacks with NBT a random change may draw instead of a pool entry */
    private static ItemStack[] tagged;
    /** items whose getMaxDamage is positive, for the repair path */
    private static int[] repairables;

    /** One container for the hand-built grids: a crafting inventory only ever
     * calls onCraftMatrixChanged on it, which Container leaves empty. */
    private static final Container EVENT = new Container()
    {
        public boolean canInteractWith(net.minecraft.entity.player.EntityPlayer p) { return true; }
    };

    static JsonObject run(final IntegratedServer server, final JsonObject cmd) throws Exception
    {
        final JsonObject[] result = new JsonObject[1];
        final Exception[] error = new Exception[1];
        Thread t = new Thread(new Runnable()
        {
            public void run()
            {
                try
                {
                    result[0] = probe(cmd);
                }
                catch (Exception e)
                {
                    error[0] = e;
                }
            }
        }, "Oracle CraftingProbe");
        t.start();
        t.join();
        if (error[0] != null) throw error[0];
        return result[0];
    }

    static JsonObject probe(JsonObject cmd) throws Exception
    {
        int cases = cmd.has("cases") ? cmd.get("cases").getAsInt() : 20000;
        long opseed = cmd.has("opseed") ? cmd.get("opseed").getAsLong() : 1;
        File dir = new File(cmd.get("out").getAsString());
        dir.mkdirs();

        buildPool();

        Random rand = new Random(opseed);
        List recipes = CraftingManager.getInstance().getRecipeList();
        int shapedN = 0, shapelessN = 0, specialN = 0;
        for (Object o : recipes)
        {
            if (o instanceof ShapedRecipes) ++shapedN;
            else if (o instanceof ShapelessRecipes) ++shapelessN;
            else ++specialN;
        }
        int[] shapedIdx = new int[shapedN], shapelessIdx = new int[shapelessN], specialIdx = new int[specialN];
        int a = 0, b = 0, c = 0;
        for (int i = 0; i < recipes.size(); ++i)
        {
            Object o = recipes.get(i);
            if (o instanceof ShapedRecipes) shapedIdx[a++] = i;
            else if (o instanceof ShapelessRecipes) shapelessIdx[b++] = i;
            else specialIdx[c++] = i;
        }

        PrintWriter w = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "cases.jsonl")), "UTF-8"));
        int[] kindCount = new int[5];
        int threwCount = 0;
        for (int n = 0; n < cases; ++n)
        {
            int roll = rand.nextInt(100);
            int kind;
            int side = rand.nextBoolean() ? 3 : 2;
            InventoryCrafting inv = new InventoryCrafting(new Container()
            {
                public boolean canInteractWith(net.minecraft.entity.player.EntityPlayer p) { return true; }
            }, side, side);

            if (roll < 50)
            {
                kind = fillFromRecipe(inv, rand, recipes, shapedIdx, shapelessIdx, specialIdx, false);
            }
            else if (roll < 70)
            {
                kind = fillFromRecipe(inv, rand, recipes, shapedIdx, shapelessIdx, specialIdx, true);
            }
            else if (roll < 85)
            {
                kind = 3;
                fillRandom(inv, rand);
            }
            else
            {
                kind = 4;
                fillRepair(inv, rand);
            }
            ++kindCount[kind];

            ItemStack out = null;
            String threw = null;
            try
            {
                out = CraftingManager.getInstance().findMatchingRecipe(inv, null);
            }
            catch (Throwable t)
            {
                // Vanilla dereferences what the probe passes as the world
                // (RecipesMapExtending reads the filled map's MapData) or a
                // book's tag compound (RecipeBookCloning). Native reports the
                // same failure, so the case still pins that path.
                threw = t.getClass().getSimpleName();
                ++threwCount;
            }
            JsonObject row = new JsonObject();
            row.addProperty("n", n);
            row.addProperty("side", side);
            row.addProperty("plan", planName(kind));
            row.add("grid", grid(inv));
            if (threw != null) row.addProperty("throw", threw); else row.add("out", stackJson(out));
            w.println(row.toString());
        }
        w.close();

        // ---- smelting: every key, every item, every damage near a key
        PrintWriter s = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "smelting.jsonl")), "UTF-8"));
        Map smelting = FurnaceRecipes.smelting().getSmeltingList();
        int smeltN = 0;
        List<Integer> inputIds = new ArrayList<Integer>();
        for (Object k : smelting.keySet())
        {
            ItemStack key = (ItemStack)k;
            int id = Item.getIdFromItem(key.getItem());
            if (!inputIds.contains(Integer.valueOf(id))) inputIds.add(Integer.valueOf(id));
        }
        // every registered item also, not just the ones with a recipe
        for (int id = 0; id < 4096; ++id)
        {
            if (!Item.itemRegistry.containsID(id)) continue;
            if (!inputIds.contains(Integer.valueOf(id))) inputIds.add(Integer.valueOf(id));
        }
        for (Integer boxed : inputIds)
        {
            int id = boxed.intValue();
            Item it = Item.getItemById(id);
            if (it == null) continue;
            int max = it.getHasSubtypes() ? 16 : 1;
            for (int d = 0; d < max; ++d)
            {
                ItemStack in = new ItemStack(it, 1, d);
                ItemStack out = FurnaceRecipes.smelting().func_151395_a(in);
                float xp = FurnaceRecipes.smelting().func_151398_b(in);
                JsonObject row = new JsonObject();
                row.addProperty("n", smeltN++);
                row.add("in", stackJson(in));
                row.add("out", stackJson(out));
                row.addProperty("xp", Float.floatToRawIntBits(xp));
                s.println(row.toString());
            }
            // the wildcard boundary: one damage past the subtypes
            ItemStack wild = new ItemStack(it, 1, 32767);
            ItemStack out = FurnaceRecipes.smelting().func_151395_a(wild);
            float xp = FurnaceRecipes.smelting().func_151398_b(wild);
            JsonObject row = new JsonObject();
            row.addProperty("n", smeltN++);
            row.add("in", stackJson(wild));
            row.add("out", stackJson(out));
            row.addProperty("xp", Float.floatToRawIntBits(xp));
            s.println(row.toString());
        }
        s.close();

        int specialRows = specials(dir);

        JsonObject m = new JsonObject();
        m.addProperty("seed", cmd.has("seed") ? cmd.get("seed").getAsLong() : 0);
        m.addProperty("opseed", opseed);
        m.addProperty("cases", cases);
        m.addProperty("throws", threwCount);
        m.addProperty("smeltingRows", smeltN);
        m.addProperty("specialRows", specialRows);
        m.addProperty("recipes", recipes.size());
        m.addProperty("shapedRecipes", shapedN);
        m.addProperty("shapelessRecipes", shapelessN);
        m.addProperty("specialRecipes", specialN);
        JsonObject kc = new JsonObject();
        kc.addProperty("from_recipe", kindCount[0]);
        kc.addProperty("from_recipe_changed", kindCount[1]);
        kc.addProperty("random_items", kindCount[3]);
        kc.addProperty("repair", kindCount[4]);
        m.add("plans", kc);
        m.addProperty("poolSize", pool.length);
        m.addProperty("taggedSize", tagged.length);
        m.addProperty("repairables", repairables.length);
        m.addProperty("order", "cases by n; smelting by n; special by n");
        m.addProperty("special", "hand-built grids for the special recipes and the repair path, which the random plans reach rarely");
        m.addProperty("stack", "{\"item\":id,\"count\":n,\"damage\":d,\"tag\":<canonical NBT or null>}");
        m.addProperty("grid", "side*side entries in row-major order, each a stack or null");
        PrintWriter mw = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "manifest.json")), "UTF-8"));
        mw.println(m.toString());
        mw.close();

        JsonObject r = new JsonObject();
        r.addProperty("cases", cases);
        r.addProperty("throws", threwCount);
        r.addProperty("specialRows", specialRows);
        r.addProperty("smeltingRows", smeltN);
        r.add("plans", kc);
        r.addProperty("poolSize", pool.length);
        r.addProperty("repairables", repairables.length);
        r.addProperty("dir", dir.getPath());
        return r;
    }

    private static String planName(int kind)
    {
        switch (kind)
        {
            case 0: return "from_recipe";
            case 1: return "from_recipe_changed";
            case 3: return "random_items";
            default: return "repair";
        }
    }

    /**
     * Hand-built grids for the recipe classes and the repair path no random plan
     * reaches often: the armor dye paths (plain, dyed, named, with the wildcard
     * dye damage), book cloning with and without a tag compound, map cloning,
     * map extending (whose MapData read with a null world throws), the three
     * RecipeFireworks shapes, a mirrored shaped recipe over a tagged stack, and
     * the repair boundary. Returns the row count.
     */
    private static int specials(File dir) throws Exception
    {
        PrintWriter w = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "special.jsonl")), "UTF-8"));
        int n = 0;

        n = special(w, n, "repair two picks", 2, new ItemStack[] {damaged(270, 10), damaged(270, 20)});
        n = special(w, n, "repair stack of two", 2, new ItemStack[] {new ItemStack(Item.getItemById(270), 2, 10), damaged(270, 20)});
        n = special(w, n, "repair zero damage", 2, new ItemStack[] {damaged(270, 0), damaged(270, 0)});
        n = special(w, n, "repair at the cap", 2, new ItemStack[] {damaged(257, 200), damaged(257, 200)});
        n = special(w, n, "repair different items", 2, new ItemStack[] {damaged(270, 10), damaged(257, 20)});

        n = special(w, n, "armor dye plain", 3, new ItemStack[] {stack(299, 1, 0), stack(351, 1, 0), null, null});
        n = special(w, n, "armor dye on dyed", 3, new ItemStack[] {leather(299, 0x102030), stack(351, 1, 15), stack(351, 1, 1), null});
        n = special(w, n, "armor dye named", 3, new ItemStack[] {named(leather(300, 0x336699), "hat"), stack(351, 1, 5), null, null});
        n = special(w, n, "armor dye wildcard damage", 3, new ItemStack[] {stack(299, 1, 0), stack(351, 1, 32767), null, null});
        n = special(w, n, "armor dye four dyes", 3, new ItemStack[] {stack(301, 1, 0), stack(351, 1, 3), stack(351, 1, 4), stack(351, 1, 11), stack(351, 1, 14), stack(351, 1, 15)});
        n = special(w, n, "armor dye black on black", 3, new ItemStack[] {leather(299, 0), stack(351, 1, 15), null, null});
        n = special(w, n, "armor non-cloth", 3, new ItemStack[] {stack(306, 1, 0), stack(351, 1, 0), null, null});
        n = special(w, n, "armor no dye", 3, new ItemStack[] {stack(299, 1, 0), null, null, null});
        n = special(w, n, "two armor", 3, new ItemStack[] {stack(299, 1, 0), stack(300, 1, 0), stack(351, 1, 0), null});

        n = special(w, n, "book cloning", 3, new ItemStack[] {written("a title", "an author"), stack(386, 1, 0), null, null});
        n = special(w, n, "book cloning named", 3, new ItemStack[] {named(written("a title", "an author"), "my book"), stack(386, 1, 0), stack(386, 1, 0)});
        n = special(w, n, "book cloning untagged", 3, new ItemStack[] {stack(387, 1, 0), stack(386, 1, 0), null, null});
        n = special(w, n, "book cloning no writable", 3, new ItemStack[] {written("t", "a"), null, null, null});

        n = special(w, n, "map cloning", 3, new ItemStack[] {named(stack(358, 1, 3), "my map"), stack(395, 1, 0), stack(395, 1, 0), null});
        n = special(w, n, "map cloning plain", 2, new ItemStack[] {stack(358, 1, 0), stack(395, 1, 0)});
        n = special(w, n, "map cloning two filled", 3, new ItemStack[] {stack(358, 1, 0), stack(358, 1, 1), stack(395, 1, 0), null});
        n = special(w, n, "map extending", 3, new ItemStack[] {stack(339, 1, 0), stack(339, 1, 0), stack(339, 1, 0),
            stack(339, 1, 0), stack(358, 1, 32767), stack(339, 1, 0), stack(339, 1, 0), stack(339, 1, 0), stack(339, 1, 0)});
        n = special(w, n, "map extending papers only", 3, new ItemStack[] {stack(339, 1, 0), stack(339, 1, 0), stack(339, 1, 0),
            stack(339, 1, 0), stack(339, 1, 0), stack(339, 1, 0), stack(339, 1, 0), stack(339, 1, 0), stack(339, 1, 0)});

        n = special(w, n, "fireworks rocket plain", 3, new ItemStack[] {stack(289, 1, 0), stack(339, 1, 0), null, null});
        n = special(w, n, "fireworks rocket with charge", 3, new ItemStack[] {stack(289, 1, 1), stack(339, 1, 0), charge(new int[] {1973019, 11743532}, 0), null});
        n = special(w, n, "fireworks charge", 3, new ItemStack[] {stack(289, 1, 0), stack(351, 1, 1), stack(351, 1, 4),
            stack(348, 1, 0), stack(264, 1, 0), stack(288, 1, 0)});
        n = special(w, n, "fireworks charge color only", 3, new ItemStack[] {stack(289, 1, 0), stack(351, 1, 2), null, null});
        n = special(w, n, "fireworks fade", 3, new ItemStack[] {charge(new int[] {1}, 0), stack(351, 1, 2), null, null});
        n = special(w, n, "fireworks fade untagged", 3, new ItemStack[] {stack(402, 1, 0), stack(351, 1, 2), null, null});
        n = special(w, n, "fireworks dye wildcard", 3, new ItemStack[] {stack(289, 1, 0), stack(351, 1, 32767), null, null});
        n = special(w, n, "fireworks fade dye wildcard", 3, new ItemStack[] {charge(new int[] {1}, 0), stack(351, 1, 32767), null, null});
        n = special(w, n, "fireworks four gunpowder", 3, new ItemStack[] {stack(289, 2, 0), stack(339, 1, 0), stack(289, 2, 0), null});

        n = special(w, n, "mirrored result keeps the tag", 2, new ItemStack[] {named(stack(346, 1, 0), "rod"), stack(391, 1, 0), null, null});

        w.close();
        return n;
    }

    /** A hand-built grid: side*side slots in row-major order, rest empty. */
    private static int special(PrintWriter w, int n, String name, int side, ItemStack[] slots) throws Exception
    {
        InventoryCrafting inv = new InventoryCrafting(EVENT, side, side);
        for (int i = 0; i < slots.length; ++i) inv.setInventorySlotContents(i, slots[i]);

        JsonObject row = new JsonObject();
        row.addProperty("n", n);
        row.addProperty("name", name);
        row.addProperty("side", side);
        row.add("grid", grid(inv));

        String threw = null;
        ItemStack out = null;
        try
        {
            out = CraftingManager.getInstance().findMatchingRecipe(inv, null);
        }
        catch (Throwable t)
        {
            threw = t.getClass().getSimpleName();
        }

        if (threw != null) row.addProperty("throw", threw); else row.add("out", stackJson(out));
        w.println(row.toString());
        return n + 1;
    }

    private static ItemStack stack(int id, int count, int damage)
    {
        return new ItemStack(Item.getItemById(id), count, damage);
    }

    private static ItemStack damaged(int id, int damage)
    {
        return new ItemStack(Item.getItemById(id), 1, damage);
    }

    private static ItemStack leather(int id, int color)
    {
        ItemStack s = stack(id, 1, 0);
        ((net.minecraft.item.ItemArmor)Item.getItemById(id)).func_82813_b(s, color);
        return s;
    }

    private static ItemStack named(ItemStack s, String name)
    {
        s.setStackDisplayName(name);
        return s;
    }

    private static ItemStack written(String title, String author)
    {
        ItemStack s = stack(387, 1, 0);
        s.setTagCompound(new net.minecraft.nbt.NBTTagCompound());
        s.getTagCompound().setString("title", title);
        s.getTagCompound().setString("author", author);
        return s;
    }

    private static ItemStack charge(int[] colors, int type)
    {
        ItemStack s = stack(402, 1, 0);
        s.setTagCompound(new net.minecraft.nbt.NBTTagCompound());
        net.minecraft.nbt.NBTTagCompound e = new net.minecraft.nbt.NBTTagCompound();
        e.setIntArray("Colors", colors);
        e.setByte("Type", (byte)type);
        s.getTagCompound().setTag("Explosion", e);
        return s;
    }

    /**
     * The item pool: every registered item at damage 0, the subtype damages, a
     * spread of damaged copies and the wildcard 32767, so a random grid can hit
     * both the exact-damage and the wildcard rules.
     */
    private static void buildPool()
    {
        List<ItemStack> list = new ArrayList<ItemStack>();
        List<ItemStack> tags = new ArrayList<ItemStack>();
        List<Integer> rep = new ArrayList<Integer>();
        for (int id = 0; id < 4096; ++id)
        {
            if (!Item.itemRegistry.containsID(id)) continue;
            Item it = Item.getItemById(id);
            if (it == null) continue;
            int max = it.getHasSubtypes() ? 16 : 1;
            for (int d = 0; d < max; ++d) list.add(new ItemStack(it, 1, d));
            list.add(new ItemStack(it, 1, 32767));
            if (it.getMaxDamage() > 0 && !it.getHasSubtypes())
            {
                list.add(new ItemStack(it, 1, it.getMaxDamage() / 2));
                list.add(new ItemStack(it, 1, it.getMaxDamage() - 1));
                if (rep.size() < 512) rep.add(Integer.valueOf(id));
            }
        }
        // a handful of NBT-carrying stacks: the mirrored result path copies the
        // grid's tag, so a case with one has to carry a tag through
        ItemStack named = new ItemStack(Item.getItemById(270), 1, 10);
        named.setStackDisplayName("named pick");
        tags.add(named);
        ItemStack written = new ItemStack(Item.getItemById(387), 1, 0);
        written.setTagCompound(new net.minecraft.nbt.NBTTagCompound());
        written.getTagCompound().setString("title", "a book");
        written.getTagCompound().setString("author", "someone");
        tags.add(written);
        ItemStack ench = new ItemStack(Item.getItemById(278), 1, 0);
        ench.setTagCompound(new net.minecraft.nbt.NBTTagCompound());
        ench.getTagCompound().setBoolean("Unbreakable", true);
        tags.add(ench);
        ItemStack coloured = new ItemStack(Item.getItemById(299), 1, 0);
        coloured.setTagCompound(new net.minecraft.nbt.NBTTagCompound());
        coloured.getTagCompound().setTag("display", new net.minecraft.nbt.NBTTagCompound());
        coloured.getTagCompound().getCompoundTag("display").setInteger("color", 0x336699);
        tags.add(coloured);

        pool = list.toArray(new ItemStack[0]);
        tagged = tags.toArray(new ItemStack[0]);
        int[] r = new int[rep.size()];
        for (int i = 0; i < r.length; ++i) r[i] = rep.get(i).intValue();
        repairables = r;
    }

    /**
     * Place a random recipe's pattern, optionally with one random change. Shaped
     * recipes are placed as stored or mirrored; shapeless ingredients land in
     * distinct random slots. A recipe too wide for the grid is retried.
     */
    private static int fillFromRecipe(InventoryCrafting inv, Random rand, List recipes,
                                      int[] shapedIdx, int[] shapelessIdx, int[] specialIdx, boolean change) throws Exception
    {
        int side = inv.getSizeInventory() == 9 ? 3 : 2;
        for (int tries = 0; tries < 32; ++tries)
        {
            int which = rand.nextInt(3);
            Object o;
            if (which == 0 && shapedIdx.length > 0) o = recipes.get(shapedIdx[rand.nextInt(shapedIdx.length)]);
            else if (which == 1 && shapelessIdx.length > 0) o = recipes.get(shapelessIdx[rand.nextInt(shapelessIdx.length)]);
            else if (specialIdx.length > 0) o = recipes.get(specialIdx[rand.nextInt(specialIdx.length)]);
            else o = recipes.get(shapedIdx[rand.nextInt(shapedIdx.length)]);

            if (o instanceof ShapedRecipes)
            {
                ShapedRecipes rec = (ShapedRecipes)o;
                int width = RecipeTable.intField(ShapedRecipes.class, rec, "recipeWidth");
                int height = RecipeTable.intField(ShapedRecipes.class, rec, "recipeHeight");
                if (width > side || height > side) continue;
                ItemStack[] pat = (ItemStack[])RecipeTable.field(ShapedRecipes.class, "recipeItems").get(rec);
                int ox = rand.nextInt(side - width + 1), oz = rand.nextInt(side - height + 1);
                boolean mirror = rand.nextBoolean();
                for (int y = 0; y < height; ++y)
                {
                    for (int x = 0; x < width; ++x)
                    {
                        ItemStack s = pat[x + y * width];
                        if (s == null) continue;
                        int sx = mirror ? width - x - 1 : x;
                        inv.setInventorySlotContents((ox + sx) + (oz + y) * side, maybeTagged(s, rand));
                    }
                }
            }
            else if (o instanceof ShapelessRecipes)
            {
                ShapelessRecipes rec = (ShapelessRecipes)o;
                List items = (List)RecipeTable.field(ShapelessRecipes.class, "recipeItems").get(rec);
                if (items.size() > side * side) continue;
                List<Integer> slots = new ArrayList<Integer>();
                for (int i = 0; i < side * side; ++i) slots.add(Integer.valueOf(i));
                for (Object x : items)
                {
                    int pick = rand.nextInt(slots.size());
                    inv.setInventorySlotContents(slots.remove(pick).intValue(), maybeTagged((ItemStack)x, rand));
                }
            }
            else
            {
                // a special recipe: random ingredients, so its matcher gets a
                // shot, then one change if asked
                fillRandom(inv, rand);
            }

            if (change) oneChange(inv, rand, side);
            return change ? 1 : 0;
        }
        fillRandom(inv, rand);
        return change ? 1 : 0;
    }

    /** Half the time a shaped ingredient is placed as a tagged copy instead. */
    private static ItemStack maybeTagged(ItemStack s, Random rand)
    {
        if (tagged.length > 0 && rand.nextInt(8) == 0)
        {
            ItemStack t = tagged[rand.nextInt(tagged.length)];
            if (t.getItem() == s.getItem()) return t.copy();
        }
        return s.copy();
    }

    /** Empty one random occupied slot, or set one random slot to a random stack. */
    private static void oneChange(InventoryCrafting inv, Random rand, int side)
    {
        if (rand.nextBoolean())
        {
            List<Integer> filled = new ArrayList<Integer>();
            for (int i = 0; i < side * side; ++i)
            {
                if (inv.getStackInSlot(i) != null) filled.add(Integer.valueOf(i));
            }
            if (!filled.isEmpty())
            {
                inv.setInventorySlotContents(filled.get(rand.nextInt(filled.size())).intValue(), null);
            }
        }
        else
        {
            inv.setInventorySlotContents(rand.nextInt(side * side), randomStack(rand));
        }
    }

    private static void fillRandom(InventoryCrafting inv, Random rand)
    {
        for (int i = 0; i < inv.getSizeInventory(); ++i)
        {
            if (rand.nextInt(4) != 0) inv.setInventorySlotContents(i, randomStack(rand));
        }
    }

    private static ItemStack randomStack(Random rand)
    {
        ItemStack s = pool[rand.nextInt(pool.length)].copy();
        s.stackSize = 1 + rand.nextInt(3);
        return s;
    }

    /** Two damaged copies of one repairable item, in two random distinct slots. */
    private static void fillRepair(InventoryCrafting inv, Random rand)
    {
        if (repairables.length == 0) return;
        int id = repairables[rand.nextInt(repairables.length)];
        Item it = Item.getItemById(id);
        int max = it.getMaxDamage();
        int side = inv.getSizeInventory() == 9 ? 3 : 2;
        int a = rand.nextInt(side * side);
        int b = rand.nextInt(side * side);
        while (b == a) b = rand.nextInt(side * side);
        inv.setInventorySlotContents(a, new ItemStack(it, 1, rand.nextInt(max)));
        inv.setInventorySlotContents(b, new ItemStack(it, 1, rand.nextInt(max)));
    }

    private static JsonArray grid(InventoryCrafting inv)
    {
        JsonArray a = new JsonArray();
        for (int i = 0; i < inv.getSizeInventory(); ++i) a.add(stackJson(inv.getStackInSlot(i)));
        return a;
    }

    /** {"item":id,"count":n,"damage":d,"tag":<canonical NBT or null>} or null. */
    static JsonObject stackJson(ItemStack s)
    {
        if (s == null) return null;
        JsonObject o = new JsonObject();
        o.addProperty("item", Item.getIdFromItem(s.getItem()));
        o.addProperty("count", s.stackSize);
        o.addProperty("damage", s.getItemDamage());
        if (s.hasTagCompound())
        {
            o.add("tag", StructuresProbe.canon(s.getTagCompound()));
        }
        else
        {
            o.add("tag", com.google.gson.JsonNull.INSTANCE);
        }
        return o;
    }
}