package netherite.oracle;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.Collections;
import java.util.Iterator;
import java.util.Map;
import java.util.Random;
import java.util.TreeMap;
import net.minecraft.entity.Entity;
import net.minecraft.entity.passive.EntityVillager;
import net.minecraft.item.Item;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.util.Tuple;
import net.minecraft.village.MerchantRecipe;
import net.minecraft.village.MerchantRecipeList;

/**
 * Villager trade generation, the reference for the native port of
 * EntityVillager.addDefaultEquipmentAndRecipies: the two static price tables
 * (villagersSellingList, blacksmithSellingList), the per-profession draw
 * sequences, the librarian's enchanted book and the priest's enchanted tools
 * (EnchantmentHelper.addRandomEnchantment), the shuffle, and
 * MerchantRecipeList.addToListWithCheck (the rule that replaces a recipe of
 * the same items with a cheaper one).
 *
 * A "case" is one villager of one profession driven through the unlock path:
 * a first addDefaultEquipmentAndRecipies(1) (what getRecipes does), then two
 * unlock ticks. An unlock tick is what updateAITick runs when
 * needsInitilization fires: every disabled recipe of the existing list gains
 * maxUses += rand.nextInt(6) + rand.nextInt(6) + 2 (only when the list has
 * more than one recipe), then addDefaultEquipmentAndRecipies(n). The probe
 * disables every recipe (func_82785_h) before ticks two and three so the
 * boost draws happen, and passes n = 1, 3, 7 so the trim bound (var9 < n and
 * var9 < the fresh list's size) is exercised past what a real unlock (always
 * n = 1) reaches. After every tick the recipe list's NBT and the Random's
 * 48-bit state are recorded.
 *
 * Each case runs from its own villager whose Entity.rand is a fresh
 * Random(caseSeed) (the field is replaced by reflection), so a case is
 * self-contained and the native side rebuilds it from the recorded seed; the
 * villagers are never added to the world. The one exception is the shuffle:
 * Collections.shuffle(List)'s Random is a shared JVM-wide static, so the probe
 * pins it to its own stream and records its state with every tick.
 *
 * Runs on its own thread (the OTHER role, like ChunkDump) while the server is
 * parked, so no CLIENT or SERVER RNG stream moves.
 *
 * Output DIR/manifest.json and DIR/lines.jsonl, one line per case and tick:
 *
 *   {"prof":2,"case":17,"seed":1234,"step":1,"count":3,"state":48bit,"gaussian":false,
 *    "nbt":<canonical MerchantRecipeList.getRecipiesAsTags() or null>}
 */
final class TradesProbe
{
    private TradesProbe() {}

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
                    result[0] = probe(cmd, server.worldServers[0], server.worldServers[0].getSeed());
                }
                catch (Exception e)
                {
                    error[0] = e;
                }
            }
        }, "Oracle TradesProbe");
        t.start();
        t.join();
        if (error[0] != null) throw error[0];
        return result[0];
    }

    /** The unlock tick's n values: 1 (what getRecipes and every real unlock
     * call), then 3 and 7 to reach the trim's second bound. */
    static final int[] STEPS = {1, 3, 7};

    static JsonObject probe(JsonObject cmd, net.minecraft.world.World world, long worldSeed) throws Exception
    {
        long seed = cmd.has("seed") ? cmd.get("seed").getAsLong() : worldSeed;
        int cases = cmd.has("cases") ? cmd.get("cases").getAsInt() : 4000;
        File dir = new File(cmd.get("out").getAsString());
        dir.mkdirs();

        Field randField = net.minecraft.entity.Entity.class.getDeclaredField("rand");
        randField.setAccessible(true);
        Field gaussianField = Random.class.getDeclaredField("haveNextNextGaussian");
        gaussianField.setAccessible(true);
        Field listField = EntityVillager.class.getDeclaredField("buyingList");
        listField.setAccessible(true);
        Method addDefault = EntityVillager.class.getDeclaredMethod("addDefaultEquipmentAndRecipies", int.class);
        addDefault.setAccessible(true);
        Random vr;

        // The shuffle inside addDefaultEquipmentAndRecipies is
        // Collections.shuffle(List), whose Random is a shared static created on
        // first use (new Random() under the determinize layer), not the
        // villager's own Random. Pin it to a probe-chosen stream and record its
        // 48-bit state before every call, so the native check can replay the
        // shuffle draws from the recorded state. The field is "r" on this JDK
        // (JDK 8); find it by type so a field rename cannot slip through.
        Field shufField = null;

        for (Field f : Collections.class.getDeclaredFields())
        {
            if (f.getType() == Random.class) shufField = f;
        }

        if (shufField == null) throw new IllegalStateException("no shared shuffle Random in java.util.Collections");
        shufField.setAccessible(true);
        shufField.set(null, new Random(mix(seed, 9, 0)));   /* the native check never uses this seed: it replays from the per-tick states */

        PrintWriter w = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "lines.jsonl")), "UTF-8"));
        int[] recipeSum = new int[5], enchantedSum = new int[5];

        for (int prof = 0; prof < 5; ++prof)
        {
            for (int i = 0; i < cases; ++i)
            {
                long caseSeed = mix(seed, prof, i);
                EntityVillager v = new EntityVillager(world, prof);
                vr = new Random(caseSeed);
                randField.set(v, vr);
                MerchantRecipeList list = null;

                for (int step = 0; step < STEPS.length; ++step)
                {
                    list = (MerchantRecipeList)listField.get(v);

                    // updateAITick's unlock block: first the boost of disabled
                    // recipes, then the recipe call. Before every tick past
                    // the first, every recipe is fully used (func_82785_h), so
                    // the boost draws happen.
                    Random shuf = (Random)shufField.get(null);
                    long shufState = shuf == null ? -1L : Det.state(shuf);

                    if (step > 0 && list != null)
                    {
                        for (int k = 0; k < list.size(); ++k)
                        {
                            ((MerchantRecipe)list.get(k)).func_82785_h();
                        }
                    }

                    if (list != null && list.size() > 1)
                    {
                        Iterator it = list.iterator();

                        while (it.hasNext())
                        {
                            MerchantRecipe r = (MerchantRecipe)it.next();

                            if (r.isRecipeDisabled())
                            {
                                r.func_82783_a(vr.nextInt(6) + vr.nextInt(6) + 2);
                            }
                        }
                    }

                    addDefault.invoke(v, Integer.valueOf(STEPS[step]));

                    list = (MerchantRecipeList)listField.get(v);
                    JsonObject o = new JsonObject();
                    o.addProperty("prof", prof);
                    o.addProperty("case", i);
                    o.addProperty("seed", caseSeed);
                    o.addProperty("step", step);
                    o.addProperty("count", STEPS[step]);
                    o.addProperty("state", Det.state(vr));
                    o.addProperty("gaussian", gaussianField.getBoolean(vr));
                    o.addProperty("sh", shufState);
                    o.add("nbt", list == null ? null : StructuresProbe.canon(list.getRecipiesAsTags()));
                    w.println(o.toString());
                }

                recipeSum[prof] += list.size();

                for (int k = 0; k < list.size(); ++k)
                {
                    MerchantRecipe r = (MerchantRecipe)list.get(k);

                    if (r.getItemToSell().stackTagCompound != null)
                    {
                        ++enchantedSum[prof];
                    }
                }
            }
        }

        w.close();

        JsonObject m = new JsonObject();
        m.addProperty("seed", seed);
        m.addProperty("worldSeed", worldSeed);
        m.addProperty("cases", cases);
        m.addProperty("profs", 5);
        JsonArray steps = new JsonArray();
        for (int s : STEPS) steps.add(new com.google.gson.JsonPrimitive(Integer.valueOf(s)));
        m.add("steps", steps);
        m.addProperty("mix", "caseSeed = seed * 1000003 + stringHash(\"p\"+prof) * 1000000007 + case, the same mix the native check rebuilds; see TradesProbe.mix");
        m.addProperty("order", "prof 0..4 outer, case inner, step 0..2 innermost");
        m.addProperty("boost", "before every step past the first: every recipe disabled (uses = maxUses), then, only when the list holds more than one recipe, each disabled recipe gains rand.nextInt(6) + rand.nextInt(6) + 2 maxUses, in list order");
        m.addProperty("shuffle", "Collections.shuffle(List)'s shared static Random, pinned by the probe to new Random(mix(seed,\"shuf\",0)) at start; every line records that stream's 48-bit state before the call (\"sh\"), and the shuffle draws (nextInt(i) for i from size down to 2, only when the fresh list holds at least two recipes) are replayed from it");
        m.addProperty("canonical", "nbt: canonical MerchantRecipeList.getRecipiesAsTags() ({\"Recipes\":[...]}); compound: object with String.compareTo key order; list: array; scalar strings b: s: i: l: f:<8 hex raw float bits> d:<16 hex raw double bits> str: ba: ia:");
        m.add("tables", tables());
        JsonArray stats = new JsonArray();
        for (int prof = 0; prof < 5; ++prof)
        {
            JsonObject s = new JsonObject();
            s.addProperty("prof", prof);
            s.addProperty("cases", cases);
            s.addProperty("recipesFinal", recipeSum[prof]);
            s.addProperty("enchanted", enchantedSum[prof]);
            stats.add(s);
        }
        m.add("stats", stats);
        PrintWriter mw = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "manifest.json")), "UTF-8"));
        mw.println(m.toString());
        mw.close();

        JsonObject r = new JsonObject();
        r.addProperty("cases", 5 * cases);
        r.addProperty("lines", 5 * cases * STEPS.length);
        r.addProperty("dir", dir.getPath());
        return r;
    }

    /** Both static price tables, from the live maps, keyed by item id. The
     * native port carries the same rows and the check compares them. */
    private static JsonObject tables() throws Exception
    {
        JsonObject o = new JsonObject();
        o.add("villagerStockList", table("villagersSellingList"));
        o.add("blacksmithSellingList", table("blacksmithSellingList"));
        return o;
    }

    private static JsonArray table(String name) throws Exception
    {
        Field f = EntityVillager.class.getDeclaredField(name);
        f.setAccessible(true);
        Map map = (Map)f.get(null);
        TreeMap<Integer, int[]> sorted = new TreeMap<Integer, int[]>();

        for (Object e : map.entrySet())
        {
            Map.Entry entry = (Map.Entry)e;
            Item item = (Item)entry.getKey();
            Tuple t = (Tuple)entry.getValue();
            sorted.put(Integer.valueOf(Item.getIdFromItem(item)),
                new int[] {((Integer)t.getFirst()).intValue(), ((Integer)t.getSecond()).intValue()});
        }

        JsonArray a = new JsonArray();

        for (Map.Entry<Integer, int[]> e : sorted.entrySet())
        {
            JsonObject row = new JsonObject();
            row.addProperty("item", e.getKey().intValue());
            row.addProperty("min", e.getValue()[0]);
            row.addProperty("max", e.getValue()[1]);
            a.add(row);
        }

        return a;
    }

    /** The seed one case runs from. */
    private static long mix(long seed, int prof, int c)
    {
        String name = "p" + prof;
        int which = 0;

        for (int i = 0; i < name.length(); ++i) which = which * 31 + name.charAt(i);

        return seed * 1000003L + (long)which * 1000000007L + c;
    }
}