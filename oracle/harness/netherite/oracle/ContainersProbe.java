package netherite.oracle;

import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonNull;
import com.google.gson.JsonObject;
import com.google.gson.JsonPrimitive;
import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;
import java.util.Random;
import java.util.zip.GZIPOutputStream;
import net.minecraft.entity.Entity;
import net.minecraft.entity.item.EntityItem;
import net.minecraft.entity.player.EntityPlayerMP;
import net.minecraft.init.Blocks;
import net.minecraft.inventory.Container;
import net.minecraft.inventory.ContainerChest;
import net.minecraft.inventory.ContainerFurnace;
import net.minecraft.inventory.ContainerPlayer;
import net.minecraft.inventory.ContainerWorkbench;
import net.minecraft.inventory.IInventory;
import net.minecraft.inventory.InventoryCrafting;
import net.minecraft.inventory.Slot;
import net.minecraft.item.Item;
import net.minecraft.item.ItemArmor;
import net.minecraft.item.ItemStack;
import net.minecraft.item.crafting.CraftingManager;
import net.minecraft.item.crafting.FurnaceRecipes;
import net.minecraft.item.crafting.ShapedRecipes;
import net.minecraft.item.crafting.ShapelessRecipes;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.nbt.NBTTagList;
import net.minecraft.nbt.NBTTagString;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.tileentity.TileEntityChest;
import net.minecraft.tileentity.TileEntityFurnace;
import net.minecraft.world.WorldServer;
import net.minecraft.world.WorldSettings;

/**
 * Inventory clicking, the reference for the native port (csrc/engine/container.c).
 *
 * Five container kinds, each filled with random stacks and then clicked
 * randomly (slot, button, mode) for thousands of clicks, including clicks
 * outside the window (slot -999):
 *
 *   player     ContainerPlayer over the server player's own inventory: a 2x2
 *              grid, the crafted-result slot, the four armor slots, 27 main and
 *              9 hotbar slots, 45 in all.
 *   workbench  ContainerWorkbench over a real crafting table in the world: 3x3.
 *   furnace    ContainerFurnace over a real TileEntityFurnace.
 *   chest27    ContainerChest over a real single chest.
 *   chest54    ContainerChest over the InventoryLargeChest of two real chests.
 *
 * Every row after a click records the whole container (every slot in slot
 * order), the cursor stack (InventoryPlayer.getItemStack), the value slotClick
 * returned, and every stack the click dropped into the world. A "fill" row
 * (re)sets the player inventory, the chest, the furnace, the crafting grid and
 * the cursor; a "close" row at the end of each kind is Container.onContainerClosed
 * plus the container's own close. The native test replays the rows in order from
 * a fresh container of the same kind and compares all of it.
 *
 * Drops are the EntityItems the click spawned, read out of the world and then
 * removed again (with their chunk entry) so the run does not accumulate them;
 * their motion is not recorded, only the stacks. The experience orbs a furnace
 * output spawns are removed the same way and are not part of the row.
 *
 * The fill and the click plan come from one java.util.Random(opseed) shared by
 * the kinds in the order listed above, so a file is a fixed sequence for a given
 * seed. Nothing here needs a second run to agree: the native side never
 * regenerates the plan, it reads it.
 *
 * Items that make CraftingManager.findMatchingRecipe read the World or throw
 * are kept out of the fill pools, because the native port calls
 * craft_find_matching with a null world (see csrc/engine/crafting.h):
 * filled_map (358) feeds RecipesMapExtending, which reads MapData from the
 * world, and map (395) only feeds RecipesMapCloning. written_book (387) only
 * ever enters a pool already carrying a title, author and pages, because
 * RecipeBookCloning.getCraftingResult dereferences its tag. Dye (351) never
 * carries the wildcard damage 32767, because RecipeFireworks indexes ItemDye's
 * colour array with it. Those four rules are the only exclusions.
 *
 * Output DIR/manifest.json and DIR/&lt;kind&gt;.jsonl.gz, one JSON object per line
 * (gzipped), the layout described in the manifest.
 */
final class ContainersProbe
{
    private ContainersProbe() {}

    /** Stacks a random fill may draw, built once from the item registry. */
    private static ItemStack[] pool;
    /** Stacks carrying NBT, drawn instead of a pool entry now and then. */
    private static ItemStack[] tagged;
    /** Item ids with a smelting result. */
    private static int[] smeltables;
    /** Item ids whose stack the furnace burns. */
    private static int[] fuels;
    /** Item ids valid in an armor slot: ItemArmor, plus pumpkin and skull. */
    private static int[] armorPool;
    /** Item ids the planted subtype-damage case uses: with subtypes, stackable to
     * 64, and neither smeltable nor a furnace fuel. */
    private static int[] SUBTYPE_ITEMS;
    /** Item ids the planted fills use: plain, stackable to 64, and neither
     * smeltable nor a furnace fuel, so a shift-click of one always takes the
     * merge path and not the furnace input or fuel path. */
    private static int[] BOUNDARY_ITEMS;
    /** Recipe-list indices of the shaped and the shapeless recipes. */
    private static int[] shapedIdx;
    private static int[] shapelessIdx;

    /** One container kind under test. */
    private static final class Kind
    {
        String name;
        Container container;
        IInventory chest;      /* null when the kind has no chest */
        IInventory result;    /* the crafted-result inventory */
        IInventory furnace;
        InventoryCrafting grid;
        int gridSize;
        int chestSize;
        int nslots;
    }

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
                    result[0] = probe(server, cmd);
                }
                catch (Exception e)
                {
                    error[0] = e;
                }
            }
        }, "Oracle ContainersProbe");
        t.start();
        t.join();
        if (error[0] != null) throw error[0];
        return result[0];
    }

    static JsonObject probe(IntegratedServer server, JsonObject cmd) throws Exception
    {
        int clicks = cmd.has("clicks") ? cmd.get("clicks").getAsInt() : 20000;
        long opseed = cmd.has("opseed") ? cmd.get("opseed").getAsLong() : 3L;
        File dir = new File(cmd.get("out").getAsString());
        dir.mkdirs();

        WorldServer ws = server.worldServers[0];
        EntityPlayerMP p = (EntityPlayerMP)server.getConfigurationManager().playerEntityList.get(0);
        survival(p);
        buildPools();

        // The blocks the container kinds sit on or over: a crafting table, a
        // furnace, one chest of its own, and two chests side by side for the
        // double chest. High above the player's own chunk, so nothing in the
        // terrain is disturbed.
        int bx = (int)Math.floor(p.posX / 16.0D) * 16 + 4;
        int bz = (int)Math.floor(p.posZ / 16.0D) * 16 + 4;
        int by = 200;
        ws.setBlock(bx, by, bz, Blocks.crafting_table, 0, 2);
        ws.setBlock(bx + 2, by, bz, Blocks.furnace, 0, 2);
        ws.setBlock(bx + 4, by, bz, Blocks.chest, 0, 2);
        ws.setBlock(bx + 5, by, bz, Blocks.chest, 0, 2);
        TileEntityFurnace furnace = (TileEntityFurnace)ws.getTileEntity(bx + 2, by, bz);
        TileEntityChest single = (TileEntityChest)ws.getTileEntity(bx + 4, by, bz);
        IInventory large = Blocks.chest.func_149951_m(ws, bx + 5, by, bz);
        if (furnace == null || single == null || large == null || large.getSizeInventory() != 54)
            throw new IllegalStateException("the container blocks did not appear in the world");

        List<Kind> kinds = new ArrayList<Kind>();
        Kind k = new Kind();
        k.name = "player";
        k.container = new ContainerPlayer(p.inventory, false, p);
        k.grid = ((ContainerPlayer)k.container).craftMatrix;
        k.gridSize = 4;
        k.result = ((ContainerPlayer)k.container).craftResult;
        kinds.add(k);
        k = new Kind();
        k.name = "workbench";
        k.container = new ContainerWorkbench(p.inventory, ws, bx, by, bz);
        k.grid = ((ContainerWorkbench)k.container).craftMatrix;
        k.gridSize = 9;
        k.result = ((ContainerWorkbench)k.container).craftResult;
        kinds.add(k);
        k = new Kind();
        k.name = "furnace";
        k.container = new ContainerFurnace(p.inventory, furnace);
        k.furnace = furnace;
        kinds.add(k);
        k = new Kind();
        k.name = "chest27";
        k.container = new ContainerChest(p.inventory, single);
        k.chest = single;
        k.chestSize = 27;
        kinds.add(k);
        k = new Kind();
        k.name = "chest54";
        k.container = new ContainerChest(p.inventory, large);
        k.chest = large;
        k.chestSize = 54;
        kinds.add(k);

        Random rand = new Random(opseed);
        JsonObject manifest = new JsonObject();
        JsonArray kindList = new JsonArray();
        int totalClicks = 0;
        int totalDrops = 0;

        for (Kind kind : kinds)
        {
            kind.nslots = kind.container.inventorySlots.size();
            int drops = play(ws, p, kind, clicks, rand, dir);
            totalClicks += clicks;
            totalDrops += drops;
            JsonObject m = new JsonObject();
            m.addProperty("kind", kind.name);
            m.addProperty("container", kind.container.getClass().getSimpleName());
            m.addProperty("file", kind.name + ".jsonl.gz");
            m.addProperty("slots", kind.nslots);
            m.addProperty("clicks", clicks);
            m.addProperty("drops", drops);
            m.addProperty("gridSize", kind.gridSize);
            m.addProperty("chestSize", kind.chestSize);
            m.add("layout", layout(kind));
            kindList.add(m);
        }

        manifest.addProperty("seed", ws.getSeed());
        manifest.addProperty("opseed", opseed);
        manifest.addProperty("clicksPerKind", clicks);
        manifest.addProperty("totalClicks", totalClicks);
        manifest.addProperty("totalDrops", totalDrops);
        manifest.addProperty("poolSize", pool.length);
        manifest.addProperty("taggedSize", tagged.length);
        manifest.addProperty("smeltables", smeltables.length);
        manifest.addProperty("fuels", fuels.length);
        manifest.addProperty("armorPool", armorPool.length);
        manifest.add("kinds", kindList);
        manifest.addProperty("order", "the kinds in the order listed above, each file by line");
        manifest.addProperty("stack",
            "null, or [item,count,damage] , or [item,count,damage,<canonical NBT>]; item is the registry id");
        manifest.addProperty("fill",
            "{\"op\":\"fill\",\"player\":<40 stacks, main 0..35 then armor 0..3>,\"chest\":<chestSize, absent for the "
            + "kinds with no chest>,\"furnace\":<3, likewise>,\"grid\":<gridSize, likewise>,\"cursor\":<stack>,"
            + "\"slots\":<the container after the fill>}");
        manifest.addProperty("click",
            "{\"op\":\"c\",\"n\":<click index within the kind>,\"slot\":s,\"button\":b,\"mode\":m,"
            + "\"ret\":<slotClick's return value>,\"cursor\":<stack>,\"slots\":<every slot, slot order>,"
            + "\"drops\":[<stacks, drop order>]}");
        manifest.addProperty("close",
            "{\"op\":\"close\",\"ret\":null,\"cursor\":<stack>,\"slots\":<...>,\"drops\":<...>}: "
            + "Container.onContainerClosed plus the container's own close, once, last");
        manifest.addProperty("grid",
            "the crafting matrix is filled through InventoryCrafting.setInventorySlotContents, so the result slot is "
            + "recomputed by findMatchingRecipe; the grid never holds filled_map, map, or an untagged written_book, "
            + "and dye never carries damage 32767 (see the class comment)");
        manifest.addProperty("survival",
            "the server player is put in survival and its capabilities cleared before the first kind: mode 3 does "
            + "nothing, and no click is ever in creative");
        manifest.addProperty("not_recorded",
            "the dropped EntityItem's motion, the experience orbs a furnace output spawns, achievements and stats");

        PrintWriter mw = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "manifest.json")), "UTF-8"));
        mw.println(manifest.toString());
        mw.close();

        JsonObject r = new JsonObject();
        r.addProperty("dir", dir.getPath());
        r.addProperty("clicks", totalClicks);
        r.addProperty("drops", totalDrops);
        r.addProperty("poolSize", pool.length);
        r.add("kinds", kindList);
        return r;
    }

    /** The slots of a kind, for the manifest: which inventory and index each is. */
    private static JsonArray layout(Kind kind) throws Exception
    {
        JsonArray a = new JsonArray();

        for (int i = 0; i < kind.nslots; ++i)
        {
            Slot s = kind.container.getSlot(i);
            String inv;

            if (s.inventory == kind.result) inv = "result";
            else if (s.inventory == kind.grid) inv = "grid";
            else if (kind.chest != null && s.inventory == kind.chest) inv = "chest";
            else if (kind.furnace != null && s.inventory == kind.furnace) inv = "furnace";
            else inv = "player";

            a.add(new JsonPrimitive(inv + "[" + RecipeTable.intField(Slot.class, s, "slotIndex") + "] limit "
                + s.getSlotStackLimit() + " " + slotClass(s)));
        }

        return a;
    }

    /** A slot's own class, or the one it was subclassed from when anonymous. */
    private static String slotClass(Slot s)
    {
        String name = s.getClass().getSimpleName();
        return name.isEmpty() ? s.getClass().getSuperclass().getSimpleName() : name;
    }

    /**
     * Fill, click, close one kind. Returns the drop count. The initial fill and
     * every refill is a "fill" row, so the native side never has to guess the
     * starting contents.
     */
    private static int play(WorldServer ws, EntityPlayerMP p, Kind kind, int clicks, Random rand, File dir) throws Exception
    {
        clear(p, kind);
        PrintWriter w = new PrintWriter(new OutputStreamWriter(
            new GZIPOutputStream(new FileOutputStream(new File(dir, kind.name + ".jsonl.gz")), 1 << 16), "UTF-8"));
        int drops = 0;
        int sinceFill = 0;
        int plantAt = 0;
        fill(rand, p, kind, w);
        List<ItemStack> got = new ArrayList<ItemStack>();
        int[][] burst = new int[0][];
        int bi = 0;

        for (int n = 0; n < clicks; ++n)
        {
            // a container that has been emptied of everything clickable gets a
            // refill, so the clicks keep having something to move
            if (sinceFill > 400 && isEmpty(kind))
            {
                fill(rand, p, kind, w);
                sinceFill = 0;
            }

            // every so often plant the states random clicking reaches rarely (a
            // merge one item over the destination's limit, a full stack next to
            // a partial one with a cursor that has room for one, an armor stack
            // of more than one) and click exactly those slots
            if (bi >= burst.length && n >= plantAt)
            {
                int[] pl = plant(rand, p, kind);

                if (pl != null)
                {
                    writeFill(w, p, kind);
                    burst = planted(rand, pl);
                    bi = 0;
                    plantAt = n + 30 + rand.nextInt(60);
                    sinceFill = 0;
                }
            }

            if (bi >= burst.length)
            {
                burst = plan(rand, p, kind);
                bi = 0;
            }

            int[] next = burst[bi++];
            int mode = next[0], slot = next[1], button = next[2];
            int mark = ws.loadedEntityList.size();
            ItemStack ret = kind.container.slotClick(slot, button, mode, p);
            got.clear();
            drops += reap(ws, mark, got);

            JsonObject row = new JsonObject();
            row.addProperty("op", "c");
            row.addProperty("n", n);
            row.addProperty("slot", slot);
            row.addProperty("button", button);
            row.addProperty("mode", mode);
            row.add("ret", stack(ret));
            row.add("cursor", stack(p.inventory.getItemStack()));
            row.add("slots", slots(kind));
            row.add("drops", dropsJson(got));
            w.println(row.toString());
            ++sinceFill;
        }

        int mark = ws.loadedEntityList.size();
        // the close drops the cursor and, for the grid kinds, the grid: fill so
        // there is something to drop
        fill(rand, p, kind, w, true);
        kind.container.onContainerClosed(p);
        got.clear();
        drops += reap(ws, mark, got);
        JsonObject row = new JsonObject();
        row.addProperty("op", "close");
        row.add("ret", JsonNull.INSTANCE);
        row.add("cursor", stack(p.inventory.getItemStack()));
        row.add("slots", slots(kind));
        row.add("drops", dropsJson(got));
        w.println(row.toString());
        w.close();
        return drops;
    }

    /** Everything a kind can hold, back to empty. */
    private static void clear(EntityPlayerMP p, Kind kind)
    {
        for (int i = 0; i < p.inventory.mainInventory.length; ++i) p.inventory.mainInventory[i] = null;
        for (int i = 0; i < p.inventory.armorInventory.length; ++i) p.inventory.armorInventory[i] = null;
        p.inventory.setItemStack((ItemStack)null);

        if (kind.chest != null)
            for (int i = 0; i < kind.chest.getSizeInventory(); ++i) kind.chest.setInventorySlotContents(i, (ItemStack)null);

        if (kind.furnace != null)
            for (int i = 0; i < 3; ++i) kind.furnace.setInventorySlotContents(i, (ItemStack)null);

        if (kind.grid != null)
            for (int i = 0; i < kind.gridSize; ++i) kind.grid.setInventorySlotContents(i, (ItemStack)null);
    }

    private static boolean isEmpty(Kind kind)
    {
        for (int i = 0; i < kind.nslots; ++i)
        {
            if (((Slot)kind.container.inventorySlots.get(i)).getHasStack()) return false;
        }

        return true;
    }

    /**
     * A random fill, through the inventories (not the slots): the player's 40,
     * the chest, the furnace (an input, a fuel, an output) and the grid, where
     * one fill in four lays a random recipe's pattern down instead of random
     * stacks, so craftable grids and crafted results show up often. One fill in
     * six is a planted fill instead (see plant).
     */
    private static void fill(Random rand, EntityPlayerMP p, Kind kind, PrintWriter w) throws Exception
    {
        fill(rand, p, kind, w, false);
    }

    /**
     * The random fill. full forces the cursor and the grid to hold something, so
     * the close row (which drops the cursor and, for the grid kinds, the grid)
     * has something to drop.
     */
    private static void fill(Random rand, EntityPlayerMP p, Kind kind, PrintWriter w, boolean full) throws Exception
    {
        if (!full && rand.nextInt(6) == 0 && plant(rand, p, kind) != null)
        {
            writeFill(w, p, kind);
            return;
        }

        clear(p, kind);

        for (int i = 0; i < 36; ++i)
        {
            p.inventory.mainInventory[i] = rand.nextInt(100) < 55 ? randomStack(rand) : null;
        }

        for (int i = 0; i < 4; ++i)
        {
            p.inventory.armorInventory[i] = rand.nextInt(100) < 50 ? armorStack(rand) : null;
        }

        if (kind.chest != null)
        {
            for (int i = 0; i < kind.chestSize; ++i)
            {
                kind.chest.setInventorySlotContents(i, rand.nextInt(100) < 35 ? randomStack(rand) : null);
            }
        }

        if (kind.furnace != null)
        {
            kind.furnace.setInventorySlotContents(0, rand.nextInt(100) < 60 ? smeltInput(rand) : null);
            kind.furnace.setInventorySlotContents(1, rand.nextInt(100) < 45 ? fuel(rand) : null);
            kind.furnace.setInventorySlotContents(2, rand.nextInt(100) < 30 ? randomStack(rand) : null);
        }

        if (kind.grid != null)
        {
            if (!full && rand.nextInt(4) == 0)
            {
                recipe(rand, kind);
            }
            else
            {
                for (int i = 0; i < kind.gridSize; ++i)
                {
                    kind.grid.setInventorySlotContents(i, full || rand.nextInt(100) < 40 ? randomStack(rand) : null);
                }
            }
        }

        p.inventory.setItemStack(full || rand.nextInt(100) < 12 ? randomStack(rand) : null);
        writeFill(w, p, kind);
    }

    /** The fill row: every inventory and the cursor, and the slots they make. */
    private static void writeFill(PrintWriter w, EntityPlayerMP p, Kind kind)
    {
        JsonObject row = new JsonObject();
        row.addProperty("op", "fill");
        JsonArray player = new JsonArray();

        for (int i = 0; i < 36; ++i) player.add(stack(p.inventory.mainInventory[i]));
        for (int i = 0; i < 4; ++i) player.add(stack(p.inventory.armorInventory[i]));
        row.add("player", player);

        if (kind.chest != null)
        {
            JsonArray a = new JsonArray();
            for (int i = 0; i < kind.chestSize; ++i) a.add(stack(kind.chest.getStackInSlot(i)));
            row.add("chest", a);
        }

        if (kind.furnace != null)
        {
            JsonArray a = new JsonArray();
            for (int i = 0; i < 3; ++i) a.add(stack(kind.furnace.getStackInSlot(i)));
            row.add("furnace", a);
        }

        if (kind.grid != null)
        {
            JsonArray a = new JsonArray();
            for (int i = 0; i < kind.gridSize; ++i) a.add(stack(kind.grid.getStackInSlot(i)));
            row.add("grid", a);
        }

        row.add("cursor", stack(p.inventory.getItemStack()));
        row.add("slots", slots(kind));
        w.println(row.toString());
    }

    /**
     * The states random clicking reaches rarely, planted into an otherwise empty
     * container so the clicks that need them always have them:
     *
     *   {src, dst}      a stack of 3 of one item in src, 62 of it in dst: a
     *                   shift-click on src merges 3 into 62, which is one item
     *                   over the stack limit, the boundary mergeItemStack has
     *                   two branches for.
     *   {low, high}     a full stack of that item at each, with the cursor
     *                   holding 63 of it: the double-click collect has room for
     *                   one, so which slot it takes from is decided by the pass
     *                   (a full stack is only taken from on the second).
     *   {armorSrc, armorSlot}  an armor stack of 3 and the empty armor slot that
     *                   takes it: the click and the drag both have to clamp the
     *                   amount to the slot's limit of 1.
     *   {fuelSrc, smeltSrc}    a furnace fuel and a smeltable in the player's
     *                   main slots with the furnace's own three empty: the fuel
     *                   has to land in the fuel slot and the smeltable in the
     *                   input slot, and nowhere else.
     *
     * The cursor is either 63 of the boundary item (the collect's two passes) or,
     * for the kinds with a grid and when the coin says so, a copy of the result
     * slot's stack after a recipe is planted: that is the state where the
     * collect has to refuse the crafted-result slot (Container.func_94530_a).
     *
     * Returns {src, dst, low, high, armorSrc, armorSlot, src2, dst2, fuelSrc,
     * smeltSrc}, or null when this kind has no room for the plant. Every slot
     * index is in the container's slot list, which is what the clicks address.
     */
    private static int[] plant(Random r, EntityPlayerMP p, Kind kind) throws Exception
    {
        int[][] pairs = plantPairs(kind);

        if (pairs == null || BOUNDARY_ITEMS.length == 0) return null;

        clear(p, kind);
        int item = BOUNDARY_ITEMS[r.nextInt(BOUNDARY_ITEMS.length)];
        Item it = Item.getItemById(item);
        int max = it.getItemStackLimit();
        int[] pair = pairs[r.nextInt(pairs.length)];
        int src = pair[0], dst = pair[1];

        kind.container.putStackInSlot(src, new ItemStack(it, 3, 0));
        kind.container.putStackInSlot(dst, new ItemStack(it, max - 2, 0));

        // the slots the plant may still use: everything but the result slot, the
        // grid and the pair it already filled
        List<Integer> free = new ArrayList<Integer>();

        for (int i = 0; i < kind.nslots; ++i)
        {
            if (i == src || i == dst) continue;

            Slot s = kind.container.getSlot(i);

            if (s.inventory == kind.result || s.inventory == kind.grid) continue;

            free.add(Integer.valueOf(i));
        }

        // a full stack on each side of dst, for the pass in each direction
        int low = free.isEmpty() ? -1 : free.get(0).intValue();
        int high = free.isEmpty() ? -1 : free.get(free.size() - 1).intValue();

        if (low >= 0) kind.container.putStackInSlot(low, new ItemStack(it, max, 0));
        if (high >= 0 && high != low) kind.container.putStackInSlot(high, new ItemStack(it, max, 0));

        /* Two armor cases: one for a slot whose slot limit is 1 because it is
         * an armor slot, one for a slot whose slot limit is 1 while the cursor's
         * item stacks to 64 (a pumpkin or a skull in the helmet slot), which is
         * the only place Slot.getSlotStackLimit and the item's own stack limit
         * disagree. */
        int armorSrc = -1, armorSlot = -1, armorSrc2 = -1, armorSlot2 = -1;

        if (kind.name.equals("player"))
        {
            List<ItemStack> cases = new ArrayList<ItemStack>();

            for (int a = 5; a < 9; ++a)
            {
                for (int i = 0; i < armorPool.length; ++i)
                {
                    Item candidate = Item.getItemById(armorPool[i]);

                    if (candidate instanceof ItemArmor && ((ItemArmor)candidate).armorType == a - 5)
                    {
                        cases.add(new ItemStack(candidate, 3, 0));
                        break;
                    }
                }
            }

            if (cases.size() >= 2)
            {
                for (int i = 0; i < free.size(); ++i)
                {
                    int slot = free.get(i).intValue();

                    if (slot < 9) continue;

                    if (armorSrc < 0)
                    {
                        armorSrc = slot;
                        armorSlot = 6;
                        kind.container.putStackInSlot(slot, cases.get(1));
                    }
                    else
                    {
                        armorSrc2 = slot;
                        armorSlot2 = 5;
                        kind.container.putStackInSlot(slot, new ItemStack(Item.getItemById(86), 3, 0));   /* pumpkin */
                        break;
                    }
                }
            }
        }

        /* the same item with two different damages: for an item with subtypes
         * the merge demands the damage match, so this is the case that tells a
         * port that dropped the rule apart */
        int[] sub = subtypePairs(kind);
        int src2 = -1, dst2 = -1;

        if (sub != null && SUBTYPE_ITEMS.length > 0)
        {
            int y = SUBTYPE_ITEMS[r.nextInt(SUBTYPE_ITEMS.length)];
            Item yit = Item.getItemById(y);
            src2 = sub[0];
            dst2 = sub[1];

            if (free.contains(Integer.valueOf(src2)) && free.contains(Integer.valueOf(dst2)))
            {
                kind.container.putStackInSlot(src2, new ItemStack(yit, 3, 0));
                kind.container.putStackInSlot(dst2, new ItemStack(yit, yit.getItemStackLimit() - 2, 3));
            }
            else
            {
                src2 = -1;
                dst2 = -1;
            }
        }

        /* a fuel and a smeltable in the player's main slots, the furnace's own
         * three empty: a shift-click of each has one slot it may land in */
        int fuelSrc = -1, smeltSrc = -1;

        if (kind.furnace != null && fuels.length > 0 && smeltables.length > 0)
        {
            for (int i = 0; i < free.size(); ++i)
            {
                int slot = free.get(i).intValue();

                if (slot < 3 || slot > 29) continue;

                if (fuelSrc < 0)
                {
                    fuelSrc = slot;
                    ItemStack f = new ItemStack(Item.getItemById(fuels[r.nextInt(fuels.length)]), 3, 0);
                    kind.container.putStackInSlot(slot, f);
                }
                else if (smeltSrc < 0)
                {
                    smeltSrc = slot;
                    ItemStack s = new ItemStack(Item.getItemById(smeltables[r.nextInt(smeltables.length)]), 3, 0);
                    kind.container.putStackInSlot(slot, s);
                    break;
                }
            }
        }

        /* the cursor: 63 of the boundary item, or a copy of a planted recipe's
         * result, which is what makes the collect reach for the result slot */
        boolean resultCursor = false;

        if (kind.grid != null && r.nextBoolean())
        {
            recipe(r, kind);
            ItemStack res = kind.container.getSlot(0).getStack();

            if (res != null)
            {
                ItemStack cursor = res.copy();
                cursor.stackSize = 1 + r.nextInt(res.getMaxStackSize());
                p.inventory.setItemStack(cursor);
                resultCursor = true;
            }
        }

        if (!resultCursor) p.inventory.setItemStack(new ItemStack(it, max - 1, 0));

        return new int[] {src, dst, low, high, armorSrc, armorSlot, src2, dst2, fuelSrc, smeltSrc, armorSrc2,
            armorSlot2};
    }

    /** The {source, destination} pair the subtype-damage plant uses, or null. */
    private static int[] subtypePairs(Kind kind)
    {
        if (kind.name.equals("player")) return new int[] {11, 37};
        if (kind.name.equals("workbench")) return new int[] {12, 38};
        if (kind.name.equals("furnace")) return new int[] {5, 31};
        if (kind.name.equals("chest27")) return new int[] {31, 2};

        return new int[] {61, 2};
    }

    /**
     * The {source, destination} pairs plant uses, one per shift-click path, with
     * the destination chosen as the first slot the merge encounters (the last for
     * a reverse merge). The result slot is never a source: a click can only ever
     * empty it, so a fill cannot plant a result stack the grid does not make.
     */
    private static int[][] plantPairs(Kind kind)
    {
        if (kind.name.equals("player"))
        {
            return new int[][] {{10, 36}, {36, 9}, {2, 9}};
        }

        if (kind.name.equals("workbench"))
        {
            return new int[][] {{11, 37}, {38, 10}};
        }

        if (kind.name.equals("furnace"))
        {
            return new int[][] {{4, 30}, {31, 3}, {0, 3}, {2, 38}};
        }

        if (kind.name.equals("chest27"))
        {
            return new int[][] {{3, 62}, {30, 0}};
        }

        return new int[][] {{3, 89}, {60, 0}};
    }

    /** The clicks a planted state is for, plus a couple of neighbours. */
    private static int[][] planted(Random r, int[] pl)
    {
        List<int[]> out = new ArrayList<int[]>();
        int armorSrc = pl[4], armorSlot = pl[5];
        int fuelSrc = pl[8], smeltSrc = pl[9];

        out.add(click(1, pl[0], r.nextInt(2)));                    /* the boundary merge */

        if (pl[6] >= 0) out.add(click(1, pl[6], r.nextInt(2)));    /* the subtype damage pair */

        if (fuelSrc >= 0)
        {
            out.add(click(1, fuelSrc, r.nextInt(2)));
            out.add(click(5, -999, 0));
            out.add(click(5, fuelSrc, 1));
            out.add(click(5, -999, 2));
        }

        if (smeltSrc >= 0) out.add(click(1, smeltSrc, r.nextInt(2)));

        out.add(click(6, pl[1], 0));                               /* collect, forward */
        out.add(click(6, pl[1], 1));                               /* collect, backward */

        if (pl[2] >= 0) out.add(click(6, pl[2], r.nextInt(2)));

        if (armorSrc >= 0)
        {
            out.add(click(0, armorSrc, 0));
            out.add(click(0, armorSlot, 0));
            out.add(click(0, armorSlot, 1));

            // and the same slot through a whole client drag, both splits
            for (int split = 0; split < 2; ++split)
            {
                out.add(click(5, -999, split << 2));
                out.add(click(5, armorSlot, 1 | (split << 2)));
                out.add(click(5, -999, 2 | (split << 2)));
            }
        }

        if (pl[10] >= 0)
        {
            out.add(click(0, pl[10], 0));
            out.add(click(0, pl[11], 0));

            for (int split = 0; split < 2; ++split)
            {
                out.add(click(5, -999, split << 2));
                out.add(click(5, pl[11], 1 | (split << 2)));
                out.add(click(5, -999, 2 | (split << 2)));
            }
        }

        out.add(uniform(r, 16));
        return out.toArray(new int[0][]);
    }

    /** The next burst of clicks, each {mode, slot, button}. */
    private static int[][] plan(Random r, EntityPlayerMP p, Kind kind) throws Exception
    {
        int n = kind.nslots;

        // A drag the previous plan left open (a mode 5 start with no commit) is
        // committed here: read the container's own state, so an add click never
        // lands on a slot outside the window (which vanilla does not guard).
        if (dragState(kind.container) != 0)
        {
            return new int[][] {r.nextInt(4) == 0
                ? click(0, -999, 0)                              /* abort it like any other click */
                : click(5, -999, 2 | (r.nextInt(2) << 2))};      /* the client's commit */
        }

        int roll = r.nextInt(100);

        if (roll < 18)
        {
            return new int[][] {uniform(r, n)};
        }

        if (roll < 38)
        {
            int f = filled(r, kind);
            int e = empty(r, kind);

            if (f < 0 || e < 0) return new int[][] {uniform(r, n)};
            return new int[][] {r.nextBoolean() ? click(0, f, 0) : click(1, f, r.nextInt(2)),
                                click(r.nextInt(2), e, r.nextInt(2))};
        }

        if (roll < 56)
        {
            int f = filled(r, kind);

            if (f < 0) return new int[][] {uniform(r, n)};
            return new int[][] {click(1, f, r.nextInt(2)), click(r.nextInt(2), f, r.nextInt(2))};
        }

        if (roll < 70)
        {
            return drag(r, n);
        }

        if (roll < 78)
        {
            int f = filled(r, kind);

            if (f < 0) return new int[][] {uniform(r, n)};

            // mode 4 needs an empty cursor: holding something, the throw click
            // does nothing at all, so the cursor is dropped first
            if (p.inventory.getItemStack() == null)
            {
                return new int[][] {click(4, f, r.nextInt(2))};
            }

            return new int[][] {click(0, -999, 0), click(4, f, r.nextInt(2))};
        }

        if (roll < 83)
        {
            int f = filled(r, kind);
            return new int[][] {click(2, f >= 0 ? f : r.nextInt(n), r.nextInt(9))};
        }

        if (roll < 88)
        {
            // mode 6 needs the clicked slot to be empty, and a cursor to collect
            // onto: an empty cursor is filled from a filled slot first
            int e = empty(r, kind);
            int f = filled(r, kind);

            if (e < 0) return new int[][] {uniform(r, n)};

            if (p.inventory.getItemStack() == null && f >= 0)
            {
                return new int[][] {click(0, f, 0), click(6, e, r.nextInt(2))};
            }

            return new int[][] {click(6, e, r.nextInt(2))};
        }

        if (roll < 91)
        {
            return collect(r, kind, n);
        }

        if (roll < 94)
        {
            return armor(r, kind, n);
        }

        if (roll < 98)
        {
            int f = filled(r, kind);

            // outside the window with something on the cursor: a drop
            return f >= 0
                ? new int[][] {click(0, f, 0), click(r.nextInt(2), -999, r.nextInt(2))}
                : new int[][] {click(r.nextInt(2), -999, r.nextInt(2))};
        }

        return new int[][] {malformed(r, n)};
    }

    /**
     * The armor slots, which have a stack limit of 1 and take only what fits
     * them: pick up an armor stack and put it in the slot that takes it, either
     * with a click (Slot.getSlotStackLimit clamps the amount) or with a whole
     * client drag (the commit clamps each slot's share).
     */
    private static int[][] armor(Random r, Kind kind, int n)
    {
        if (kind.name.equals("player"))
        {
            for (int a = 5; a < 9; ++a)
            {
                Slot slot = kind.container.getSlot(a);

                if (slot.getHasStack()) continue;

                for (int s = 9; s < n; ++s)
                {
                    Slot from = kind.container.getSlot(s);

                    if (from.getHasStack() && from.getStack().stackSize > 1 && slot.isItemValid(from.getStack()))
                    {
                        if (r.nextBoolean())
                        {
                            return new int[][] {click(0, s, 0), click(0, a, r.nextInt(2))};
                        }

                        int split = r.nextInt(2);
                        return new int[][] {click(0, s, 0), click(5, -999, split << 2),
                                            click(5, a, 1 | (split << 2)), click(5, -999, 2 | (split << 2))};
                    }
                }
            }
        }

        return new int[][] {uniform(r, n)};
    }

    /**
     * The double-click collect the player and workbench containers refuse to do
     * from the crafted-result slot: put a stack that the result slot also holds
     * on the cursor, then double-click an empty slot. Container.func_94530_a is
     * what makes the difference, and random clicks reach this state rarely.
     */
    private static int[][] collect(Random r, Kind kind, int n)
    {
        if (kind.gridSize == 0) return new int[][] {uniform(r, n)};

        ItemStack result = kind.container.getSlot(0).getStack();

        if (result == null) return new int[][] {uniform(r, n)};

        int src = -1;
        int dst = -1;

        for (int i = 1; i < n; ++i)
        {
            Slot s = kind.container.getSlot(i);

            if (dst < 0 && !s.getHasStack()) dst = i;

            if (src < 0 && s.getHasStack() && s.getStack().getItem() == result.getItem()
                && s.getStack().getItemDamage() == result.getItemDamage()
                && ItemStack.areItemStackTagsEqual(s.getStack(), result)
                && result.stackSize + s.getStack().stackSize <= s.getStack().getMaxStackSize())
            {
                src = i;
            }
        }

        if (src < 0 || dst < 0) return new int[][] {uniform(r, n)};

        return new int[][] {click(0, src, 0), click(6, dst, 0), uniform(r, n)};
    }

    /**
     * The clicks a drag needs, as the client sends them: a start at slot -999
     * (func_94534_d(0, split)), one add per slot (func_94534_d(1, split)) and a
     * commit at slot -999 (func_94534_d(2, split)).
     */
    private static int[][] drag(Random r, int n)
    {
        int split = r.nextInt(2);
        int adds = 1 + r.nextInt(4);
        int[][] out = new int[adds + 2][];
        out[0] = click(5, -999, split << 2);

        for (int i = 0; i < adds; ++i)
        {
            out[i + 1] = click(5, r.nextInt(n), 1 | (split << 2));
        }

        out[adds + 1] = click(5, -999, 2 | (split << 2));
        return out;
    }

    /** A click the client never sends: an unknown mode, an unknown drag state. */
    private static int[] malformed(Random r, int n)
    {
        switch (r.nextInt(4))
        {
            case 0: return click(7 + r.nextInt(3), r.nextInt(n), r.nextInt(4));
            case 1: return click(5, -999, r.nextInt(16));
            case 2: return click(r.nextInt(2), -1 - r.nextInt(999), r.nextInt(3));
            default: return click(5, r.nextInt(n), r.nextInt(16));
        }
    }

    /**
     * One fully random click, in the ranges vanilla's branches actually reach.
     * A drag start, a commit and an abort never read the slot, so those may be
     * outside the window; an add does, so its slot is always in the window.
     */
    private static int[] uniform(Random r, int n)
    {
        int mode = MODES[r.nextInt(MODES.length)];
        int slot;
        int button;

        switch (mode)
        {
            case 2:
                slot = r.nextInt(n);
                button = r.nextInt(9);
                break;
            case 4:
            case 6:
                slot = r.nextInt(n);
                button = r.nextInt(2);
                break;
            case 5:
                if (r.nextInt(4) == 0)
                {
                    slot = -999;
                    button = (r.nextBoolean() ? 0 : 2) | (r.nextInt(4) << 2);
                }
                else
                {
                    slot = r.nextInt(n);
                    button = r.nextInt(16);
                }
                break;
            default:
                slot = r.nextInt(8) == 0 ? (r.nextInt(2) == 0 ? -999 : -1 - r.nextInt(40)) : r.nextInt(n);
                button = r.nextInt(4) == 0 ? r.nextInt(4) : r.nextInt(2);
        }

        return click(mode, slot, button);
    }

    private static final int[] MODES = {0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 3, 4, 4, 5, 5, 5, 6, 6, 8};

    private static int[] click(int mode, int slot, int button)
    {
        return new int[] {mode, slot, button};
    }

    /** A random slot that holds a stack, or -1. */
    private static int filled(Random r, Kind kind)
    {
        List<Integer> has = new ArrayList<Integer>();

        for (int i = 0; i < kind.nslots; ++i)
        {
            if (((Slot)kind.container.inventorySlots.get(i)).getHasStack()) has.add(Integer.valueOf(i));
        }

        return has.isEmpty() ? -1 : has.get(r.nextInt(has.size())).intValue();
    }

    /** A random slot that does not, or -1. */
    private static int empty(Random r, Kind kind)
    {
        List<Integer> has = new ArrayList<Integer>();

        for (int i = 0; i < kind.nslots; ++i)
        {
            if (!((Slot)kind.container.inventorySlots.get(i)).getHasStack()) has.add(Integer.valueOf(i));
        }

        return has.isEmpty() ? -1 : has.get(r.nextInt(has.size())).intValue();
    }

    /** Container.field_94536_g: 0 idle, 1 a drag in progress, 2 a finished one. */
    private static int dragState(Container c) throws Exception
    {
        return RecipeTable.intField(Container.class, c, "field_94536_g");
    }

    /** The player has to be in survival: every click is a survival click. */
    private static void survival(EntityPlayerMP p)
    {
        p.theItemInWorldManager.setGameType(WorldSettings.GameType.SURVIVAL);
        p.capabilities.isCreativeMode = false;
        p.capabilities.disableDamage = false;
        p.capabilities.allowEdit = true;
        p.capabilities.allowFlying = false;
        p.capabilities.isFlying = false;
    }

    /**
     * The EntityItems the last click spawned, in the order they were dropped,
     * taken out of the world and out of their chunk again so a long run does not
     * accumulate them. Experience orbs and anything else are removed too, but
     * are not returned.
     */
    private static int reap(WorldServer ws, int mark, List<ItemStack> out)
    {
        List l = ws.loadedEntityList;
        int dropped = 0;

        for (int i = l.size() - 1; i >= mark; --i)
        {
            Entity e = (Entity)l.get(i);

            if (e instanceof EntityItem)
            {
                out.add(0, ((EntityItem)e).getEntityItem());
                ++dropped;
            }

            e.setDead();

            // spawnEntityInWorld added it to a loaded chunk just now, so the
            // chunk is still there to take it back out of
            if (e.addedToChunk)
            {
                ws.getChunkFromChunkCoords(e.chunkCoordX, e.chunkCoordZ).removeEntity(e);
            }

            l.remove(i);
        }

        return dropped;
    }

    private static JsonArray slots(Kind kind)
    {
        JsonArray a = new JsonArray();

        for (int i = 0; i < kind.nslots; ++i)
        {
            a.add(stack(((Slot)kind.container.inventorySlots.get(i)).getStack()));
        }

        return a;
    }

    private static JsonArray dropsJson(List<ItemStack> drops)
    {
        JsonArray a = new JsonArray();

        for (ItemStack s : drops) a.add(stack(s));

        return a;
    }

    /** null, [item,count,damage], or [item,count,damage,<canonical NBT>]. */
    static JsonElement stack(ItemStack s)
    {
        if (s == null) return JsonNull.INSTANCE;

        JsonArray a = new JsonArray();
        a.add(new JsonPrimitive(Integer.valueOf(Item.getIdFromItem(s.getItem()))));
        a.add(new JsonPrimitive(Integer.valueOf(s.stackSize)));
        a.add(new JsonPrimitive(Integer.valueOf(s.getItemDamage())));

        if (s.hasTagCompound()) a.add(StructuresProbe.canon(s.getTagCompound()));

        return a;
    }

    /**
     * The fill pools. Every registered item at damage 0 and its subtypes, its
     * wildcard damage, and a spread of damage for the tools, plus a handful of
     * NBT-carrying stacks. The four exclusions in the class comment are applied
     * here.
     */
    private static void buildPools()
    {
        if (pool != null) return;

        List<ItemStack> p = new ArrayList<ItemStack>();
        List<ItemStack> t = new ArrayList<ItemStack>();
        List<Integer> sm = new ArrayList<Integer>();
        List<Integer> fu = new ArrayList<Integer>();
        List<Integer> ar = new ArrayList<Integer>();

        for (int id = 0; id < 4096; ++id)
        {
            if (!Item.itemRegistry.containsID(id)) continue;
            Item it = Item.getItemById(id);
            if (it == null) continue;
            if (id == 358 || id == 395 || id == 387) continue;

            int max = it.getHasSubtypes() ? 16 : 1;

            for (int d = 0; d < max; ++d)
            {
                if (id == 351 && d > 15) continue;
                p.add(new ItemStack(it, 1, d));
            }

            if (id != 351) p.add(new ItemStack(it, 1, 32767));

            if (it.getMaxDamage() > 0 && !it.getHasSubtypes())
            {
                p.add(new ItemStack(it, 1, 1));
                p.add(new ItemStack(it, 1, it.getMaxDamage() / 2));
                p.add(new ItemStack(it, 1, it.getMaxDamage() - 1));
            }

            if (it instanceof ItemArmor || id == 86 || id == 397) ar.add(Integer.valueOf(id));
            if (FurnaceRecipes.smelting().func_151395_a(new ItemStack(it, 1, 0)) != null) sm.add(Integer.valueOf(id));
            if (TileEntityFurnace.func_145952_a(new ItemStack(it, 1, 0)) > 0) fu.add(Integer.valueOf(id));
        }

        ItemStack namedPick = new ItemStack(Item.getItemById(270), 1, 10);
        namedPick.setStackDisplayName("named pick");
        t.add(namedPick);
        ItemStack written = new ItemStack(Item.getItemById(387), 1, 0);
        written.setTagCompound(new NBTTagCompound());
        written.getTagCompound().setString("title", "a book");
        written.getTagCompound().setString("author", "someone");
        NBTTagList pages = new NBTTagList();
        pages.appendTag(new NBTTagString("a page"));
        written.getTagCompound().setTag("pages", pages);
        t.add(written);
        ItemStack ench = new ItemStack(Item.getItemById(278), 1, 0);
        ench.setTagCompound(new NBTTagCompound());
        ench.getTagCompound().setBoolean("Unbreakable", true);
        t.add(ench);
        ItemStack cloth = new ItemStack(Item.getItemById(299), 1, 0);
        cloth.setTagCompound(new NBTTagCompound());
        cloth.getTagCompound().setTag("display", new NBTTagCompound());
        cloth.getTagCompound().getCompoundTag("display").setInteger("color", 0x336699);
        t.add(cloth);
        ItemStack book = new ItemStack(Item.getItemById(340), 1, 0);
        book.setStackDisplayName("a book");
        t.add(book);
        ItemStack planks = new ItemStack(Item.getItemById(5), 3, 2);
        planks.setStackDisplayName("planks");
        t.add(planks);

        pool = p.toArray(new ItemStack[0]);
        tagged = t.toArray(new ItemStack[0]);
        smeltables = toInts(sm);
        fuels = toInts(fu);
        armorPool = toInts(ar);

        List<Integer> boundary = new ArrayList<Integer>();

        for (int id = 0; id < 4096; ++id)
        {
            if (!Item.itemRegistry.containsID(id)) continue;
            Item it = Item.getItemById(id);
            if (it == null || it.getHasSubtypes() || it.getItemStackLimit() != 64) continue;
            if (id == 358 || id == 395 || id == 387 || id == 351) continue;
            ItemStack s = new ItemStack(it, 1, 0);
            if (FurnaceRecipes.smelting().func_151395_a(s) != null) continue;
            if (TileEntityFurnace.func_145952_a(s) > 0) continue;
            boundary.add(Integer.valueOf(id));
        }

        BOUNDARY_ITEMS = toInts(boundary);

        List<Integer> subitems = new ArrayList<Integer>();

        for (int id = 0; id < 4096; ++id)
        {
            if (!Item.itemRegistry.containsID(id)) continue;
            Item it = Item.getItemById(id);
            if (it == null || !it.getHasSubtypes() || it.getItemStackLimit() != 64) continue;
            ItemStack s = new ItemStack(it, 1, 0);
            if (FurnaceRecipes.smelting().func_151395_a(s) != null) continue;
            if (TileEntityFurnace.func_145952_a(s) > 0) continue;
            subitems.add(Integer.valueOf(id));
        }

        SUBTYPE_ITEMS = toInts(subitems);

        List recipes = CraftingManager.getInstance().getRecipeList();
        List<Integer> shaped = new ArrayList<Integer>();
        List<Integer> shapeless = new ArrayList<Integer>();

        for (int i = 0; i < recipes.size(); ++i)
        {
            Object o = recipes.get(i);

            if (o instanceof ShapedRecipes)
            {
                shaped.add(Integer.valueOf(i));
            }
            else if (o instanceof ShapelessRecipes)
            {
                shapeless.add(Integer.valueOf(i));
            }
        }

        shapedIdx = toInts(shaped);
        shapelessIdx = toInts(shapeless);
    }

    private static int[] toInts(List<Integer> l)
    {
        int[] a = new int[l.size()];

        for (int i = 0; i < a.length; ++i) a[i] = l.get(i).intValue();

        return a;
    }

    private static ItemStack randomStack(Random r)
    {
        ItemStack s;

        if (r.nextInt(12) == 0)
        {
            s = tagged[r.nextInt(tagged.length)].copy();
            s.stackSize = 1 + r.nextInt(3);
        }
        else
        {
            s = pool[r.nextInt(pool.length)].copy();
            s.stackSize = 1 + r.nextInt(Math.max(1, s.getMaxStackSize()));
        }

        return s;
    }

    private static ItemStack armorStack(Random r)
    {
        if (r.nextInt(6) == 0) return randomStack(r);
        Item i = Item.getItemById(armorPool[r.nextInt(armorPool.length)]);
        return new ItemStack(i, 1, r.nextInt(4) == 0 && i.getMaxDamage() > 0 ? r.nextInt(i.getMaxDamage()) : 0);
    }

    private static ItemStack smeltInput(Random r)
    {
        if (r.nextInt(5) == 0) return randomStack(r);
        ItemStack s = new ItemStack(Item.getItemById(smeltables[r.nextInt(smeltables.length)]), 1, 0);
        s.stackSize = 1 + r.nextInt(Math.max(1, s.getMaxStackSize()));
        return s;
    }

    private static ItemStack fuel(Random r)
    {
        if (r.nextInt(5) == 0) return randomStack(r);
        ItemStack s = new ItemStack(Item.getItemById(fuels[r.nextInt(fuels.length)]), 1, 0);
        s.stackSize = 1 + r.nextInt(Math.max(1, s.getMaxStackSize()));
        return s;
    }

    /**
     * Lay a random recipe's pattern into the grid: a shaped recipe's pattern at
     * a random offset, as stored or mirrored, or a shapeless recipe's
     * ingredients in distinct random slots. A recipe too wide for the grid is
     * retried, then the fill falls back to random stacks.
     */
    private static void recipe(Random r, Kind kind) throws Exception
    {
        int side = kind.gridSize == 9 ? 3 : 2;
        List recipes = CraftingManager.getInstance().getRecipeList();

        for (int tries = 0; tries < 24; ++tries)
        {
            Object o;

            if (r.nextInt(3) == 0 && shapelessIdx.length > 0)
            {
                o = recipes.get(shapelessIdx[r.nextInt(shapelessIdx.length)]);
            }
            else if (shapedIdx.length > 0)
            {
                o = recipes.get(shapedIdx[r.nextInt(shapedIdx.length)]);
            }
            else
            {
                return;
            }

            if (o instanceof ShapedRecipes)
            {
                ShapedRecipes rec = (ShapedRecipes)o;
                int width = RecipeTable.intField(ShapedRecipes.class, rec, "recipeWidth");
                int height = RecipeTable.intField(ShapedRecipes.class, rec, "recipeHeight");
                if (width > side || height > side) continue;
                ItemStack[] pat = (ItemStack[])RecipeTable.field(ShapedRecipes.class, "recipeItems").get(rec);
                int ox = r.nextInt(side - width + 1);
                int oz = r.nextInt(side - height + 1);
                boolean mirror = r.nextBoolean();

                for (int y = 0; y < height; ++y)
                {
                    for (int x = 0; x < width; ++x)
                    {
                        ItemStack s = pat[x + y * width];
                        if (s == null || !safe(s)) continue;
                        int sx = mirror ? width - x - 1 : x;
                        kind.grid.setInventorySlotContents((ox + sx) + (oz + y) * side, maybeTagged(s, r));
                    }
                }
            }
            else
            {
                ShapelessRecipes rec = (ShapelessRecipes)o;
                List items = (List)RecipeTable.field(ShapelessRecipes.class, "recipeItems").get(rec);
                if (items.size() > side * side) continue;
                List<Integer> free = new ArrayList<Integer>();

                for (int i = 0; i < side * side; ++i) free.add(Integer.valueOf(i));

                for (Object x : items)
                {
                    ItemStack s = (ItemStack)x;
                    if (!safe(s)) break;
                    kind.grid.setInventorySlotContents(free.remove(r.nextInt(free.size())).intValue(), maybeTagged(s, r));
                }
            }

            return;
        }
    }

    /** A recipe ingredient a grid may hold (see the class comment). */
    private static boolean safe(ItemStack s)
    {
        int id = Item.getIdFromItem(s.getItem());

        if (id == 358 || id == 395) return false;
        if (id == 387) return s.hasTagCompound() && s.getTagCompound().hasKey("title");
        if (id == 351 && s.getItemDamage() > 15) return false;

        return true;
    }

    /** One time in eight a recipe ingredient is laid down as a tagged copy. */
    private static ItemStack maybeTagged(ItemStack s, Random r)
    {
        if (r.nextInt(8) == 0)
        {
            ItemStack t = tagged[r.nextInt(tagged.length)];

            if (t.getItem() == s.getItem() && t.getItemDamage() == s.getItemDamage()) return t.copy();
        }

        return s.copy();
    }
}