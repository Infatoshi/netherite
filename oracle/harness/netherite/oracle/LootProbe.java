package netherite.oracle;

import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonPrimitive;
import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.List;
import java.util.Random;
import net.minecraft.init.Items;
import net.minecraft.inventory.InventoryBasic;
import net.minecraft.item.ItemStack;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.util.WeightedRandomChestContent;
import net.minecraft.world.gen.structure.StructureNetherBridgePieces;
import net.minecraft.world.gen.structure.ComponentScatteredFeaturePieces;
import net.minecraft.world.gen.structure.StructureMineshaftPieces;
import net.minecraft.world.gen.structure.StructureStrongholdPieces;
import net.minecraft.world.gen.structure.StructureVillagePieces;
import net.minecraft.world.gen.structure.StructureComponent;
import net.minecraft.world.gen.feature.WorldGenDungeons;
import net.minecraft.world.WorldServer;

/**
 * Chest loot, the reference for the native port of
 * WeightedRandomChestContent.generateChestContents over every structure's
 * table, including the enchanted book entry.
 *
 * Each table is drawn many times with the structure's own exact call
 * expression: the same table array (the book entry appended through
 * func_92080_a where the structure does that), the same left-to-right draw
 * order, the same count expression and the same inventory the contents land in
 * (an InventoryBasic of 27 slots, the size the chests, dispensers and chest
 * minecarts these structures place all have). Each draw runs from its own
 * Random seeded with the run seed mixed with the draw index, so a run is
 * self-contained and reproducible; the oracle prints both seeds per draw.
 *
 * A "case" is one call into the generator. For a chest that is one
 * generateChestContents, for the jungle pyramid's two dispensers one
 * func_150706_a over junglePyramidsDispenserContents.
 *
 * Runs on its own thread (the OTHER role, like ChunkDump) while the server is
 * parked, so no CLIENT or SERVER RNG stream moves. Only the tables, the item
 * registry and ItemEnchantedBook/EnchantmentHelper are read.
 *
 * Output DIR/manifest.json with the seed, the draw count, the cases (name, the
 * structure's call expression, the table's entries as id/damage/min/max/weight)
 * and the output layout, and DIR/draws.jsonl, one line per case:
 *
 *   {"case":"dungeon","draw":0,"seed":1234,"slots":[{"slot":3,"item":<canonical
 *    stack NBT as in StructuresProbe.canon>},...]}
 *
 * Only non-empty slots appear, sorted by slot.
 */
final class LootProbe
{
    private LootProbe() {}

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
                    result[0] = probe(cmd, server.worldServers[0].getSeed());
                }
                catch (Exception e)
                {
                    error[0] = e;
                }
            }
        }, "Oracle LootProbe");
        t.start();
        t.join();
        if (error[0] != null) throw error[0];
        return result[0];
    }

    /** The count expression of a structure's chest call, so the probe draws exactly
     * what the structure draws (Java evaluates the count argument before
     * generateChestContents runs, so the order is fixed by the call site). */
    private enum Kind
    {
        FIXED_8("8"), FIXED_10("10"), FIXED_2("2"), R3_4("3 + rand.nextInt(4)"), R2_2("2 + rand.nextInt(2)"),
        R1_4("1 + rand.nextInt(4)"), R2_5("2 + rand.nextInt(5)"), R3_6("3 + rand.nextInt(6)"),
        R2_4("2 + rand.nextInt(4)");

        private final String label;

        Kind(String label) { this.label = label; }

        String count() { return this.label; }
    }

    /** One table with its structure's exact call. */
    private static final class Case
    {
        final String name;
        final WeightedRandomChestContent[] table;
        final String call;
        final Kind kind;
        final boolean dispenser;
        /** the book entry's func_92112_a arguments: min, max, weight (1,1,1 for
         * func_92114_b); -1 when the structure appends no book entry */
        final int bookMin, bookMax, bookWeight;

        Case(String name, WeightedRandomChestContent[] table, String call, Kind kind, boolean dispenser,
             int bookMin, int bookMax, int bookWeight)
        {
            this.name = name;
            this.table = table;
            this.call = call;
            this.kind = kind;
            this.dispenser = dispenser;
            this.bookMin = bookMin;
            this.bookMax = bookMax;
            this.bookWeight = bookWeight;
        }
    }

    /** A book entry built the way the structure builds it, then appended. */
    private static WeightedRandomChestContent[] withBook(Random rand, WeightedRandomChestContent[] base, boolean library)
    {
        WeightedRandomChestContent book = library
            ? Items.enchanted_book.func_92112_a(rand, 1, 5, 2)
            : Items.enchanted_book.func_92114_b(rand);
        return WeightedRandomChestContent.func_92080_a(base, new WeightedRandomChestContent[] {book});
    }

    static JsonObject probe(JsonObject cmd, long worldSeed) throws Exception
    {
        long seed = cmd.has("seed") ? cmd.get("seed").getAsLong() : worldSeed;
        int draws = cmd.has("draws") ? cmd.get("draws").getAsInt() : 2000;
        int books = cmd.has("books") ? cmd.get("books").getAsInt() : 2000;
        File dir = new File(cmd.get("out").getAsString());
        dir.mkdirs();

        // The book entries are built the way the structures build them at
        // generation time, but from a per-case Random seeded from the run seed so
        // the native side rebuilds them without depending on case order. In the
        // game the structure's own RNG (a different stream, moved earlier) builds
        // the same stack; only the chest draws below share the stream under test.
        WeightedRandomChestContent[] dungeon = withBook(new Random(mix(seed, "dungeon", -1)), table("net.minecraft.world.gen.feature.WorldGenDungeons", "field_111189_a"), false);
        WeightedRandomChestContent[] mineshaft = withBook(new Random(mix(seed, "mineshaftCorridor", -1)), table("net.minecraft.world.gen.structure.StructureMineshaftPieces", "mineshaftChestContents"), false);
        WeightedRandomChestContent[] strongholdCorridor = withBook(new Random(mix(seed, "strongholdCorridor", -1)), table("net.minecraft.world.gen.structure.StructureStrongholdPieces$ChestCorridor", "strongholdChestContents"), false);
        WeightedRandomChestContent[] strongholdLibrary = withBook(new Random(mix(seed, "strongholdLibrary", -1)), table("net.minecraft.world.gen.structure.StructureStrongholdPieces$Library", "strongholdLibraryChestContents"), true);
        WeightedRandomChestContent[] strongholdCrossing = withBook(new Random(mix(seed, "strongholdCrossing", -1)), table("net.minecraft.world.gen.structure.StructureStrongholdPieces$RoomCrossing", "strongholdRoomCrossingChestContents"), false);
        WeightedRandomChestContent[] desertTemple = withBook(new Random(mix(seed, "desertTemple", -1)), table("net.minecraft.world.gen.structure.ComponentScatteredFeaturePieces$DesertPyramid", "itemsToGenerateInTemple"), false);
        WeightedRandomChestContent[] jungleTemple = withBook(new Random(mix(seed, "jungleTemple", -1)), table("net.minecraft.world.gen.structure.ComponentScatteredFeaturePieces$JunglePyramid", "junglePyramidsChestContents"), false);
        WeightedRandomChestContent[] jungleDispenser = table("net.minecraft.world.gen.structure.ComponentScatteredFeaturePieces$JunglePyramid", "junglePyramidsDispenserContents");
        WeightedRandomChestContent[] blacksmith = table("net.minecraft.world.gen.structure.StructureVillagePieces$House2", "villageBlacksmithChestContents");
        WeightedRandomChestContent[] netherBridge = table("net.minecraft.world.gen.structure.StructureNetherBridgePieces$Piece", "field_111019_a");
        WeightedRandomChestContent[] bonusChest = table("net.minecraft.world.WorldServer", "bonusChestContent");

        List<Case> cases = new ArrayList<Case>();
        cases.add(new Case("dungeon", dungeon,
            "generateChestContents(rand, func_92080_a(dungeonTable, book(func_92114_b(rand))), inv27, 8)", Kind.FIXED_8, false, 1, 1, 1));
        cases.add(new Case("mineshaftCorridor", mineshaft,
            "generateChestContents(rand, func_92080_a(mineshaftChestContents, book(func_92114_b(rand))), inv27, 3 + rand.nextInt(4))", Kind.R3_4, false, 1, 1, 1));
        cases.add(new Case("strongholdCorridor", strongholdCorridor,
            "generateChestContents(rand, func_92080_a(strongholdChestContents, book(func_92114_b(rand))), inv27, 2 + rand.nextInt(2))", Kind.R2_2, false, 1, 1, 1));
        cases.add(new Case("strongholdLibrary", strongholdLibrary,
            "generateChestContents(rand, func_92080_a(strongholdLibraryChestContents, book(func_92112_a(rand,1,5,2))), inv27, 1 + rand.nextInt(4))", Kind.R1_4, false, 1, 5, 2));
        cases.add(new Case("strongholdCrossing", strongholdCrossing,
            "generateChestContents(rand, func_92080_a(strongholdRoomCrossingChestContents, book(func_92114_b(rand))), inv27, 1 + rand.nextInt(4))", Kind.R1_4, false, 1, 1, 1));
        cases.add(new Case("desertTemple", desertTemple,
            "generateChestContents(rand, func_92080_a(itemsToGenerateInTemple, book(func_92114_b(rand))), inv27, 2 + rand.nextInt(5))", Kind.R2_5, false, 1, 1, 1));
        cases.add(new Case("jungleTemple", jungleTemple,
            "generateChestContents(rand, func_92080_a(junglePyramidsChestContents, book(func_92114_b(rand))), inv27, 2 + rand.nextInt(5))", Kind.R2_5, false, 1, 1, 1));
        cases.add(new Case("jungleDispenser", jungleDispenser,
            "func_150706_a(rand, junglePyramidsDispenserContents, dispenser9, 2)", Kind.FIXED_2, true, -1, -1, -1));
        cases.add(new Case("blacksmith", blacksmith,
            "generateChestContents(rand, villageBlacksmithChestContents, inv27, 3 + rand.nextInt(6))", Kind.R3_6, false, -1, -1, -1));
        cases.add(new Case("netherBridge", netherBridge,
            "generateChestContents(rand, field_111019_a, inv27, 2 + rand.nextInt(4))", Kind.R2_4, false, -1, -1, -1));
        cases.add(new Case("bonusChest", bonusChest,
            "generateChestContents(rand, bonusChestContent, inv27, 10)", Kind.FIXED_10, false, -1, -1, -1));

        JsonArray caseJson = new JsonArray();
        for (Case c : cases)
        {
            JsonObject o = new JsonObject();
            o.addProperty("name", c.name);
            o.addProperty("call", c.call);
            o.addProperty("count", c.kind.count());
            o.addProperty("slots", c.dispenser ? 9 : 27);
            o.addProperty("bookSeed", c.bookMin >= 0 ? mix(seed, c.name, -1) : 0);
            if (c.bookMin >= 0)
            {
                ItemStack s = stackOf(c.table[c.table.length - 1]);
                o.add("book", StructuresProbe.canon(s.writeToNBT(new NBTTagCompound())));
            }
            JsonArray entries = new JsonArray();
            for (WeightedRandomChestContent e : c.table) entries.add(entry(e));
            o.add("entries", entries);
            caseJson.add(o);
        }

        if (cmd.has("header"))
        {
            writeHeader(new File(cmd.get("header").getAsString()), cases);
            JsonObject r = new JsonObject();
            r.addProperty("cases", cases.size());
            r.addProperty("header", cmd.get("header").getAsString());
            return r;
        }

        PrintWriter w = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "draws.jsonl")), "UTF-8"));
        for (Case c : cases)
        {
            for (int i = 0; i < draws; ++i)
            {
                // A fresh Random per draw, seeded from the run seed and the draw
                // index, so each draw is self-contained and the oracle records
                // the seed the native side must reproduce it with.
                long drawSeed = mix(seed, c.name, i);
                Random rand = new Random(drawSeed);
                InventoryBasic inv = new InventoryBasic("loot", true, c.dispenser ? 9 : 27);
                int n = evalCount(c.kind, rand);
                if (c.dispenser)
                {
                    // func_150706_a over the 9 slots a TileEntityDispenser has:
                    // same draws, same size formula, same slot picks.
                    for (int k = 0; k < n; ++k)
                    {
                        WeightedRandomChestContent e = (WeightedRandomChestContent)net.minecraft.util.WeightedRandom.getRandomItem(rand, c.table);
                        int min = intOf(e, "theMinimumChanceToGenerateItem");
                        int max = intOf(e, "theMaximumChanceToGenerateItem");
                        int size = min + rand.nextInt(max - min + 1);
                        put(rand, inv, e, size);
                    }
                }
                else
                {
                    WeightedRandomChestContent.generateChestContents(rand, c.table, inv, n);
                }
                w.println(line(c.name, i, drawSeed, inv));
            }
        }
        w.close();

        // The enchantment path on its own: many book stacks per book table, so
        // buildEnchantmentList (the map order, the conflict removals, the repeat
        // loop) is exercised well past the single stack per table the chest
        // draws above carry. func_92114_b is func_92112_a(rand, 1, 1, 1).
        PrintWriter bw = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "books.jsonl")), "UTF-8"));
        int bookLines = 0;
        for (Case c : cases)
        {
            if (c.bookMin < 0) continue;
            for (int i = 0; i < books; ++i)
            {
                long s = mix(seed, c.name, BOOK_MIX + i);
                Random rand = new Random(s);
                ItemStack stack = stackOf(Items.enchanted_book.func_92112_a(rand, c.bookMin, c.bookMax, c.bookWeight));
                JsonObject o = new JsonObject();
                o.addProperty("case", c.name);
                o.addProperty("i", i);
                o.addProperty("seed", s);
                o.add("item", StructuresProbe.canon(stack.writeToNBT(new NBTTagCompound())));
                bw.println(o.toString());
                ++bookLines;
            }
        }
        bw.close();

        JsonObject m = new JsonObject();
        m.addProperty("seed", seed);
        m.addProperty("worldSeed", worldSeed);
        m.addProperty("draws", draws);
        m.addProperty("books", books);
        m.addProperty("bookBase", BOOK_MIX);
        m.addProperty("bookLines", bookLines);
        m.addProperty("slots", 27);
        m.addProperty("dispenserSlots", 9);
        m.add("cases", caseJson);
        m.addProperty("mix", "drawSeed = seed * 1000003 + caseIndex * 1000000007 + draw, the same mix the native check rebuilds; see LootProbe.mix");
        m.addProperty("order", "one line per case, then draw index");
        m.addProperty("canonical", "slots: non-empty slots sorted by slot, each {\"slot\":n,\"item\":<canonical ItemStack NBT>}; compound: object with String.compareTo key order; list: array; scalar strings b: s: i: l: f:<8 hex raw float bits> d:<16 hex raw double bits> str: ba: ia:");
        PrintWriter mw = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "manifest.json")), "UTF-8"));
        mw.println(m.toString());
        mw.close();

        JsonObject r = new JsonObject();
        r.addProperty("cases", cases.size());
        r.addProperty("draws", cases.size() * draws);
        r.addProperty("dir", dir.getPath());
        return r;
    }

    /**
     * The loot tables as a C header for the native port. Every value comes from
     * the live table object (item id through Item.getIdFromItem, damage and
     * counts through ItemStack and the private fields), so nothing is
     * transcribed. The book entry the structure appends through func_92080_a is
     * the table's last row, with the enchantability level addRandomEnchantment
     * runs at, and func_92112_a's min/max/weight arguments (1,1,1 for
     * func_92114_b). Tabbed lines spell out the structure's call expression.
     */
    private static void writeHeader(File out, List<Case> cases) throws Exception
    {
        out.getParentFile().mkdirs();
        PrintWriter w = new PrintWriter(new OutputStreamWriter(new FileOutputStream(out), "UTF-8"));
        w.println("/* Minecraft 1.7.10 chest loot tables: every WeightedRandomChestContent array a");
        w.println(" * structure chest or dispenser draws from. Generated by netherite.oracle.LootProbe");
        w.println(" * from the oracle (make run CLASS=LootProbe CMD='{\"header\":\"...\"}'); do not edit");
        w.println(" * by hand. A row's book_enchantability is -1 for a plain entry and 30 for the");
        w.println(" * book entry each structure appends through func_92080_a; book_min/book_max/");
        w.println(" * book_weight are func_92112_a's arguments, 1/1/1 for func_92114_b. Item ids");
        w.println(" * are the ones in items.h. */");
        w.println("#ifndef NETHERITE_LOOT_TABLES_H");
        w.println("#define NETHERITE_LOOT_TABLES_H");
        w.println();
        w.println("#include \"loot.h\"");
        w.println();
        for (Case c : cases)
        {
            String up = ident(c.name).toUpperCase();
            w.println("/* " + c.name + ": " + c.call + " */");
            w.println("static const struct loot_entry LOOT_" + up + "[] = {");
            for (int i = 0; i < c.table.length; ++i)
            {
                WeightedRandomChestContent e = c.table[i];
                ItemStack s = stackOf(e);
                boolean book = c.table.length - 1 == i && c.bookMin >= 0;
                w.println(String.format("    {%d, %d, %d, %d, %d, %d, %d, %d, %d},",
                    net.minecraft.item.Item.getIdFromItem(s.getItem()), s.getItemDamage(),
                    intOf(e, "theMinimumChanceToGenerateItem"), intOf(e, "theMaximumChanceToGenerateItem"),
                    intOf(e, "itemWeight"), book ? 30 : -1,
                    book ? c.bookMin : 0, book ? c.bookMax : 0, book ? c.bookWeight : 0));
            }
            w.println("};");
            w.println();
        }
        w.println("static const struct loot_table LOOT_TABLES[] = {");
        for (Case c : cases)
        {
            String up = ident(c.name).toUpperCase();
            w.println(String.format("    {\"%s\", LOOT_%s, (int)(sizeof LOOT_%s / sizeof LOOT_%s[0])},",
                c.name, up, up, up));
        }
        w.println("};");
        w.println();
        w.println("/* Every table, NULL-terminated, for a caller that wants to walk them all. */");
        w.println("#define LOOT_TABLES_ALL \\");
        StringBuilder all = new StringBuilder();
        for (int i = 0; i < cases.size(); ++i) all.append("    &LOOT_TABLES[").append(i).append("], \\\n");
        all.append("    NULL");
        w.println(all.toString());
        w.println();
        w.println("#endif");
        w.close();
    }

    private static String ident(String name)
    {
        StringBuilder b = new StringBuilder();
        for (int i = 0; i < name.length(); ++i)
        {
            char ch = name.charAt(i);
            if (Character.isUpperCase(ch)) b.append('_');
            b.append(ch);
        }
        return b.toString();
    }

    /** Book-sweep seeds start here, past every chest draw index for a case. */
    private static final int BOOK_MIX = 1000000;

    /** The seed one draw runs from. */
    private static long mix(long seed, String name, int draw)
    {
        int which = 0;
        for (int i = 0; i < name.length(); ++i) which = which * 31 + name.charAt(i);
        return seed * 1000003L + (long)which * 1000000007L + draw;
    }

    /** The structure's own count expression, drawn from rand at call time. */
    private static int evalCount(Kind kind, Random rand)
    {
        switch (kind)
        {
            case FIXED_8: return 8;
            case FIXED_10: return 10;
            case FIXED_2: return 2;
            case R3_4: return 3 + rand.nextInt(4);
            case R2_2: return 2 + rand.nextInt(2);
            case R1_4: return 1 + rand.nextInt(4);
            case R2_5: return 2 + rand.nextInt(5);
            case R3_6: return 3 + rand.nextInt(6);
            case R2_4: return 2 + rand.nextInt(4);
            default: throw new IllegalStateException("unknown count kind: " + kind);
        }
    }

    /**
     * TileEntityDispenser has 9 slots and WeightedRandomChestContent.func_150706_a
     * wants a TileEntityDispenser, which the probe does not need to build for a
     * draw that only depends on the table, the RNG and setInventorySlotContents.
     * This is func_150706_a's body over an IInventory, so jungleDispenser cases
     * match the structure's call exactly (the book entry is not in that table
     * and the size formula is the same one).
     */
    private static void put(Random rand, InventoryBasic inv, WeightedRandomChestContent e, int size) throws Exception
    {
        ItemStack base = stackOf(e);
        if (base.getMaxStackSize() >= size)
        {
            ItemStack s = base.copy();
            s.stackSize = size;
            inv.setInventorySlotContents(rand.nextInt(inv.getSizeInventory()), s);
        }
        else
        {
            for (int i = 0; i < size; ++i)
            {
                ItemStack s = base.copy();
                s.stackSize = 1;
                inv.setInventorySlotContents(rand.nextInt(inv.getSizeInventory()), s);
            }
        }
    }

    private static ItemStack stackOf(WeightedRandomChestContent e) throws Exception
    {
        return (ItemStack)field(e, "theItemId");
    }

    private static int intOf(WeightedRandomChestContent e, String name) throws Exception
    {
        return ((Integer)field(e, name)).intValue();
    }

    private static Object field(Object o, String name) throws Exception
    {
        for (Class<?> c = o.getClass(); c != null; c = c.getSuperclass())
        {
            try
            {
                Field f = c.getDeclaredField(name);
                f.setAccessible(true);
                return f.get(o);
            }
            catch (NoSuchFieldException ignored)
            {
            }
        }
        throw new NoSuchFieldException(name + " on " + o.getClass());
    }

    private static JsonObject entry(WeightedRandomChestContent e)
    {
        ItemStack s;
        int min, max, weight;
        try
        {
            s = stackOf(e);
            min = intOf(e, "theMinimumChanceToGenerateItem");
            max = intOf(e, "theMaximumChanceToGenerateItem");
            weight = intOf(e, "itemWeight");
        }
        catch (Exception ex)
        {
            throw new RuntimeException(ex);
        }
        JsonObject o = new JsonObject();
        o.addProperty("item", net.minecraft.item.Item.getIdFromItem(s.getItem()));
        o.addProperty("damage", s.getItemDamage());
        o.addProperty("min", min);
        o.addProperty("max", max);
        o.addProperty("weight", weight);
        NBTTagCompound tag = s.stackTagCompound;
        o.add("tag", tag == null ? com.google.gson.JsonNull.INSTANCE : StructuresProbe.canon(tag));
        return o;
    }

    /** One draw line: the case, its index, its seed and its non-empty slots. */
    private static String line(String name, int draw, long seed, InventoryBasic inv) throws Exception
    {
        JsonObject o = new JsonObject();
        o.addProperty("case", name);
        o.addProperty("draw", draw);
        o.addProperty("seed", seed);
        JsonArray slots = new JsonArray();
        for (int i = 0; i < inv.getSizeInventory(); ++i)
        {
            ItemStack s = inv.getStackInSlot(i);
            if (s == null) continue;
            JsonObject e = new JsonObject();
            e.addProperty("slot", i);
            e.add("item", StructuresProbe.canon(s.writeToNBT(new NBTTagCompound())));
            slots.add(e);
        }
        o.add("slots", slots);
        return o.toString();
    }

    @SuppressWarnings("unchecked")
    private static WeightedRandomChestContent[] table(String className, String name) throws Exception
    {
        Field f = Class.forName(className).getDeclaredField(name);
        f.setAccessible(true);
        return (WeightedRandomChestContent[])f.get(null);
    }
}