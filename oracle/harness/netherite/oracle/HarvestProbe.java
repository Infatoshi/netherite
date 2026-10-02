package netherite.oracle;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import com.google.gson.JsonPrimitive;
import java.io.BufferedOutputStream;
import java.io.DataOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.lang.reflect.Field;
import java.util.Random;
import net.minecraft.block.Block;
import net.minecraft.block.material.Material;
import net.minecraft.enchantment.Enchantment;
import net.minecraft.enchantment.EnchantmentHelper;
import net.minecraft.entity.player.EntityPlayerMP;
import net.minecraft.init.Blocks;
import net.minecraft.init.Items;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.nbt.NBTTagList;
import net.minecraft.potion.Potion;
import net.minecraft.potion.PotionEffect;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.world.WorldServer;

/**
 * Reference for the native port of block breaking speed and harvestability:
 * Block.getPlayerRelativeBlockHardness, EntityPlayer.canHarvestBlock and
 * getCurrentPlayerStrVsBlock (func_146096_a), the ItemTool/ItemPickaxe/ItemAxe/
 * ItemSpade/ItemShears/ItemSword func_150893_a (speed) and func_150897_b
 * (harvest) tables, and Block.getBlockHardness.
 *
 * Every case sets one block in a far raw chunk and asks the game for the three
 * values with one held item. The held item is enumerated, not drawn: every
 * (block, meta) pair is asked about an empty hand, the five materials of each
 * of the four tool classes, shears and the block's own item. The random extras
 * come from Random(opseed): the Efficiency level on the held stack (0..5), the
 * Haste and Mining Fatigue amplifiers (0 = none), onGround and the block's
 * metadata. InventoryPlayer.func_146023_a and func_146025_b read
 * inventory.mainInventory[inventory.currentItem]; getCurrentPlayerStrVsBlock
 * reads getHeldItem(), which for EntityPlayerMP is
 * inventory.getCurrentItem() with the same slot index, so both slots are set.
 *
 * The probe stands on the block it asks about, feet at y+1, and every block
 * around the player at the feet and eye height is air, so isInsideOfMaterial
 * (water) is false in every case and the water divisor is never entered. Water
 * is the one branch of getCurrentPlayerStrVsBlock this reference does not
 * cover; the manifest says so.
 *
 * Whatever else the client thread did to the player between cases (food,
 * movement, effects) is discarded: each case writes onGround, position,
 * potion effects and the inventory slots it reads, then reads the three
 * values, then the next case writes them again. Only isPotionActive(digSpeed)
 * / isPotionActive(digSlowdown) / the amplifier of each, onGround, the main
 * inventory slots 0..8 and the cape slot are read, so nothing else can matter.
 * The potion map is emptied before every case, so a stale amplifier cannot
 * survive into a case that draws "none".
 *
 * Output DIR/manifest.json and DIR/cases.bin, one record per case:
 *   x, y, z            int32 LE each (the block's position)
 *   dim                int8 (0 = overworld)
 *   block_ip          int32 LE (Block.getIdFromBlock of Block.getBlockById(block))
 *   meta               int8
 *   hardness           float LE bits (Block.getBlockHardness at the case's coords)
 *   state              uint8 (see STATE_ below)
 *   haste              uint8 (Potion.digSpeed amplifier + 1, 0 = not active)
 *   fatigue            uint8 (Potion.digSlowdown amplifier + 1, 0 = not active)
 *   held_item_id       int16 LE (Item.getIdFromItem(getItem()), 0 when empty)
 *   held_stack_size    int8 (0 when empty, 1 otherwise)
 *   held_enchant_id    int16 LE (Enchantment.efficiency.effectId, 0 when none)
 *   held_enchant_lvl   int16 LE
 *   resp_item_id       int16 LE (the same item in the main inventory slot)
 *   response_size      int8
 *   resp_enchant_id    int16 LE
 *   resp_enchant_lvl   int16 LE
 *   item0..item3       int16 LE each (the four items this case's state carries:
 *                      always the current one, then the three it can switch to
 *                      with the vanilla 1..9 hotbar keys; 0 = empty)
 *   can_harvest        uint8 (EntityPlayer.canHarvestBlock)
 *   speed              float LE bits (getCurrentPlayerStrVsBlock(block, false))
 *   rel_hardness       float LE bits (getPlayerRelativeBlockHardness)
 *   flags              uint8 (bit 0: in water at the case's coords, bit 1: above
 *                      y 255, bit 2: the player is inside water, bit 3: onGround)
 * Records are 61 bytes; a 4-byte LE case count precedes the first one.
 *
 * The four inventory slots a case fills are the held item followed by the next
 * three pool items, so the case carries the state the native port may copy
 * verbatim as well as the one stack the three answers depend on.
 */
final class HarvestProbe
{
    private HarvestProbe() {}

    /** InventoryPlayer.itemStack, the stack getHeldItem() returns; no setter in 1.7.10. */
    static final Field FIELD_ITEM_STACK = itemStackField();

    static Field itemStackField()
    {
        try
        {
            Field f = net.minecraft.entity.player.InventoryPlayer.class.getDeclaredField("itemStack");
            f.setAccessible(true);
            return f;
        }
        catch (Exception e)
        {
            throw new RuntimeException(e);
        }
    }

    /** water: the cases where Material.water is in the way and this probe skips */
    static final int STATE_DRY = 0, STATE_WATER_SKIPPED = 1;

    static final int REC_BYTES = 61;

    /** metas per block for blocks whose render type is not 31 (variant blocks run 16) */
    static final int METAS = 3;

    /** random-extra draws per (block, meta, held item) */
    static final int EXTRAS = 5;

    /** the largest item pool: empty, the block's own item, 20 tools and shears */
    static final int POOL_MAX = 24;

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
                    result[0] = dump(server, cmd);
                }
                catch (Exception e)
                {
                    error[0] = e;
                }
            }
        }, "Oracle HarvestProbe");
        t.start();
        t.join();
        if (error[0] != null) throw error[0];
        return result[0];
    }

    static JsonObject dump(IntegratedServer server, JsonObject cmd) throws Exception
    {
        int cx = cmd.has("cx") ? cmd.get("cx").getAsInt() : 2000;
        int cz = cmd.has("cz") ? cmd.get("cz").getAsInt() : 2000;
        int y = cmd.has("y") ? cmd.get("y").getAsInt() : 100;
        long opseed = cmd.has("opseed") ? cmd.get("opseed").getAsLong() : 1L;
        File dir = new File(cmd.get("out").getAsString());
        dir.mkdirs();

        WorldServer ws = server.worldServers[0];
        long seed = ws.getSeed();
        EntityPlayerMP p = (EntityPlayerMP)server.getConfigurationManager().playerEntityList.get(0);
        Probe.rawChunks = true;
        File f = new File(dir, "cases.bin");
        byte[] head = new byte[4]; // the case count, patched once every case is written
        java.io.RandomAccessFile raw = new java.io.RandomAccessFile(f, "rw");
        raw.write(head);
        DataOutputStream out = new DataOutputStream(new BufferedOutputStream(new java.io.FileOutputStream(raw.getFD()), 1 << 16));
        byte[] rec = new byte[REC_BYTES];

        int cases = 0, skipped = 0, over = 0, inWater = 0;
        JsonArray materialList = materials();
        Random r = new Random(opseed);
        int[] pool = new int[POOL_MAX];

        for (int id = 0; id < BlockTable.IDS; ++id)
        {
            if (!Block.blockRegistry.containsID(id)) continue;
            Block base = Block.getBlockById(id);
            int metas = base.getRenderType() == 31 && id != 0 ? 16 : METAS;
            int np = itemPool(base, pool);
            int bx = cx << 4, bz = cz << 4;
            ws.getChunkFromChunkCoords(cx, cz); // raw: no population

            // the block's own cell is rewritten for every case; the cells the player could
            // be inside of (feet at y + 1, eyes at y + 2) are cleared once, before any case
            for (int i = -2; i <= 2; ++i)
            {
                for (int k = -2; k <= 2; ++k)
                {
                    ws.setBlock(bx + i, y + 1, bz + k, Blocks.air, 0, 2);
                    ws.setBlock(bx + i, y + 2, bz + k, Blocks.air, 0, 2);
                }
            }

            for (int m = 0; m < metas; ++m)
            {
                // every pool item EXTRAS times, each case with its own random extras; the
                // case index walks the pool, so the held item is enumerated, not drawn
                for (int s = 0; s < np * EXTRAS; ++s)
                {
                    int eff = r.nextInt(6);
                    int haste = r.nextInt(3);
                    int fatigue = r.nextInt(4);
                    boolean onGround = r.nextBoolean();
                    int meta = id == 0 ? 0 : (m + r.nextInt(16)) & 15;
                    int j = s % np; // the held item is pool[j]

                    // the four items this case carries: the held one, then the three pool
                    // items after it, wrapping
                    int[] four = new int[4];
                    for (int k = 0; k < 4; ++k) four[k] = pool[(j + k) % np];

                    for (int i = 0; i < 9; ++i) p.inventory.mainInventory[i] = null;
                    for (int i = 0; i < 4; ++i)
                    {
                        ItemStack st = itemStack(four[i]);
                        setEfficiency(st, eff);
                        p.inventory.mainInventory[i] = st;
                    }
                    p.inventory.currentItem = 0;
                    FIELD_ITEM_STACK.set(p.inventory, p.inventory.mainInventory[0]);

                    ws.setBlock(bx, y, bz, base, meta, 2);
                    p.setPosition(bx + 0.5D, y + 1.0D, bz + 0.5D);
                    p.onGround = onGround;
                    p.clearActivePotions();
                    if (haste > 0) p.addPotionEffect(new PotionEffect(Potion.digSpeed.id, 600, haste - 1));
                    if (fatigue > 0) p.addPotionEffect(new PotionEffect(Potion.digSlowdown.id, 600, fatigue - 1));

                    boolean wet = ws.getBlock(bx, y, bz).getMaterial() == Material.water;
                    boolean eyeWet = p.isInsideOfMaterial(Material.water);
                    boolean above = y > 255;
                    int state;
                    boolean harvest = false;
                    float hardness, rel, speed;
                    if (wet || eyeWet || above) // the flags still record why
                    {
                        hardness = rel = speed = 0.0F;
                        state = STATE_WATER_SKIPPED;
                        ++skipped;
                        if (wet) ++inWater;
                        if (above) ++over;
                    }
                    else
                    {
                        hardness = base.getBlockHardness(ws, bx, y, bz);
                        harvest = p.canHarvestBlock(base);
                        speed = p.getCurrentPlayerStrVsBlock(base, false);
                        rel = base.getPlayerRelativeBlockHardness(p, ws, bx, y, bz);
                        state = STATE_DRY;
                    }

                    ItemStack held = p.inventory.getCurrentItem();
                    ItemStack resp = p.inventory.mainInventory[p.inventory.currentItem];
                    int o = 4;
                    putI32(rec, o, bx); o += 4;
                    putI32(rec, o, y); o += 4;
                    putI32(rec, o, bz); o += 4;
                    rec[o++] = 0;
                    putI32(rec, o, id); o += 4;
                    rec[o++] = (byte)meta;
                    putF32(rec, o, hardness); o += 4;
                    rec[o++] = (byte)state;
                    rec[o++] = (byte)haste;
                    rec[o++] = (byte)fatigue;
                    putI16(rec, o, held == null ? 0 : Item.getIdFromItem(held.getItem())); o += 2;
                    rec[o++] = (byte)(held == null ? 0 : 1);
                    int[] e = enchant(held);
                    putI16(rec, o, e[0]); o += 2;
                    putI16(rec, o, e[1]); o += 2;
                    putI16(rec, o, resp == null ? 0 : Item.getIdFromItem(resp.getItem())); o += 2;
                    rec[o++] = (byte)(resp == null ? 0 : 1);
                    int[] e2 = enchant(resp);
                    putI16(rec, o, e2[0]); o += 2;
                    putI16(rec, o, e2[1]); o += 2;
                    for (int i = 0; i < 4; ++i) { putI16(rec, o, four[i]); o += 2; }
                    rec[o++] = (byte)(harvest ? 1 : 0);
                    putF32(rec, o, speed); o += 4;
                    putF32(rec, o, rel); o += 4;
                    rec[o++] = (byte)((wet ? 1 : 0) | (above ? 2 : 0) | (eyeWet ? 4 : 0) | (onGround ? 8 : 0));
                    if (o != REC_BYTES) throw new IllegalStateException("record layout " + o);
                    out.write(rec);
                    ++cases;
                }
            }
        }

        // restore the player: what a server player looks like on an empty world
        for (int i = 0; i < 4; ++i) p.inventory.mainInventory[i] = null;
        p.inventory.currentItem = 0;
        FIELD_ITEM_STACK.set(p.inventory, null);
        p.clearActivePotions();
        ws.setBlock(cx << 4, y, cz << 4, Blocks.air, 0, 2);
        out.flush();
        out.close();
        putI32(head, 0, cases);
        java.io.RandomAccessFile patch = new java.io.RandomAccessFile(f, "rw");
        patch.seek(0);
        patch.write(head);
        int size = (int)patch.length();
        patch.close();
        if (size != 4 + cases * REC_BYTES) throw new IllegalStateException("cases.bin is " + size + " bytes for " + cases + " cases");

        JsonObject m = new JsonObject();
        m.addProperty("seed", seed);
        m.addProperty("cx", cx);
        m.addProperty("cz", cz);
        m.addProperty("y", y);
        m.addProperty("opseed", opseed);
        m.addProperty("cases", cases);
        m.addProperty("case_bytes", REC_BYTES);
        m.addProperty("skipped", skipped);
        m.addProperty("skipped_water_block", inWater);
        m.addProperty("skipped_above_y255", over);
        m.addProperty("order", "block id 0..4095, then meta m, then s = 0..np*EXTRAS-1 whose pool item is s % np; block_id in the file is the block's own id, cp to csrc/engine/blocks.h");
        m.addProperty("pool", "per (block, meta): an empty hand (item 0), the block's own item (Item.getItemFromBlock, absent for air and for blocks with no item), the five materials of each of ItemPickaxe/ItemAxe/ItemSpade/ItemSword (wooden, stone, iron, diamond, golden) and ItemShears, duplicates dropped. item0 in a record is the held pool item; np is that list's length, 22 or 23.");
        m.add("materials", materialList);
        m.addProperty("layout", "4 bytes LE case count, then cases: x i32, y i32, z i32, dim i8, block_ip i32, meta i8, hardness f32 bits, state u8, haste u8, fatigue u8, held_item_id i16, held_stack_size i8, held_enchant_id i16, held_enchant_lvl i16, resp_item_id i16, resp_stack_size i8, resp_enchant_id i16, resp_enchant_lvl i16, item0 i16, item1 i16, item2 i16, item3 i16, can_harvest u8, speed f32 bits, rel_hardness f32 bits, flags u8");
        m.addProperty("layout_offsets", "from the record's first byte: x 4, y 8, z 12, dim 16, block_ip 17, meta 21, hardness 22, state 26, haste 27, fatigue 28, held_item_id 29, held_stack_size 31, held_enchant_id 32, held_enchant_lvl 34, resp_item_id 36, resp_stack_size 38, resp_enchant_id 39, resp_enchant_lvl 41, item0 43, item1 45, item2 47, item3 49, can_harvest 51, speed 52, rel_hardness 56, flags 60");
        m.addProperty("layout_helper", "held_* is getHeldItem() at the case's position/ground/potions; resp_* is inventory.mainInventory[inventory.currentItem], the slots func_146023_a and func_146025_b read. Both carry the same item and Efficiency level. item0 is that item; item1..3 are the next three pool items, wrapping. The native check needs item0; item1..3 are the four-item state the native port may copy verbatim. haste and fatigue are the amplifier plus 1 (0 = the potion is not active). flags bit 0 is water in the block's own cell, bit 1 above y 255, bit 2 the player inside water, bit 3 onGround, the value the case ran with.");
        m.addProperty("draws", "per case from Random(opseed), in this order: efficiency = nextInt(6); haste = nextInt(3) (0 none, else amplifier haste-1); fatigue = nextInt(4) (0 none, else amplifier fatigue-1); onGround = nextBoolean(); meta = (m + nextInt(16)) & 15. The held item is not drawn: each case takes pool[s % np], so every pool item gets EXTRAS independent extra draws per meta.");
        m.addProperty("metas", "m runs METAS = 3 for every block except those with getRenderType() == 31 and id != 0, where it runs 16");
        m.addProperty("player", "feet at (x + 0.5, y + 1, z + 0.5), onGround from the draw, potion map emptied then Haste/Mining Fatigue added, held item as above");
        m.addProperty("water", "not covered: the three blocks at the block's own cell, the feet and the eyes are all air in every case, so isInsideOfMaterial(Material.water) is false and the /5 water divisor is never entered. state records it if it ever were; those cases carry zeros and their flags say why");
        m.addProperty("skipped_blocks", "block ids with no block object are not written at all; block id 0 is air and is kept as a real case");
        m.addProperty("restored", "the player's main inventory slots 0..8 are cleared, currentItem is 0, the potion map is empty and the probe block is set to air before the probe returns");
        PrintWriter w = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "manifest.json")), "UTF-8"));
        w.println(m.toString());
        w.close();

        JsonObject res = new JsonObject();
        res.addProperty("dir", dir.getPath());
        res.addProperty("cases", cases);
        res.addProperty("skipped", skipped);
        res.addProperty("inWater", inWater);
        res.addProperty("above255", over);
        return res;
    }

    /**
     * For every registered block, its id and the index of its Material in the
     * MATERIALS table of csrc/engine/blocks.h, as BlockTable builds that table:
     * entries in block-id order, skipping a Material already listed. So the
     * native check can translate a record's block id straight into a material
     * index and compare the two block tables too.
     */
    static JsonArray materials()
    {
        JsonArray a = new JsonArray();
        java.util.List<Material> seen = new java.util.ArrayList<Material>();

        for (int id = 0; id < BlockTable.IDS; ++id)
        {
            if (!Block.blockRegistry.containsID(id)) continue;
            Material mt = Block.getBlockById(id).getMaterial();
            int idx = seen.indexOf(mt);
            if (idx < 0)
            {
                seen.add(mt);
                idx = seen.size() - 1;
            }
            JsonObject e = new JsonObject();
            e.addProperty("id", id);
            e.addProperty("material", idx);
            a.add(e);
        }
        return a;
    }

    /**
     * The held items every (block, meta) pair is asked about: an empty hand, the
     * block's own item (an ItemBlock unless the block has none), the five materials
     * of each of ItemPickaxe/ItemAxe/ItemSpade/ItemSword and ItemShears. Duplicates
     * and items with no object drop out. Returns the count written into pool.
     */
    static int itemPool(Block base, int[] pool)
    {
        Item[] tools = {
            Items.wooden_pickaxe, Items.stone_pickaxe, Items.iron_pickaxe, Items.diamond_pickaxe, Items.golden_pickaxe,
            Items.wooden_axe, Items.stone_axe, Items.iron_axe, Items.diamond_axe, Items.golden_axe,
            Items.wooden_shovel, Items.stone_shovel, Items.iron_shovel, Items.diamond_shovel, Items.golden_shovel,
            Items.wooden_sword, Items.stone_sword, Items.iron_sword, Items.diamond_sword, Items.golden_sword,
            Items.shears};
        java.util.List<Integer> list = new java.util.ArrayList<Integer>();
        list.add(0);
        int self = ID(Item.getItemFromBlock(base));
        for (int i = -1; i < tools.length; ++i)
        {
            int id = i < 0 ? self : ID(tools[i]);
            if (id != 0 && list.indexOf(id) < 0) list.add(id);
        }
        int n = 0;
        for (int id : list) pool[n++] = id;
        return n;
    }

    /** A stack of one of that item, or null for item id 0 or an id with no item object. */
    static ItemStack itemStack(int id)
    {
        Item it = id == 0 ? null : Item.getItemById(id);
        return it == null ? null : new ItemStack(it, 1, 0);
    }

    /** The registry id of an Item object, 0 for null. */
    static int ID(Item it)
    {
        return it == null ? 0 : Item.getIdFromItem(it);
    }

    /** Efficiency level on the stack, 0 when the stack is empty. */
    static int[] enchant(ItemStack s) throws Exception
    {
        if (s == null) return new int[] {0, 0};
        int lvl = EnchantmentHelper.getEnchantmentLevel(Enchantment.efficiency.effectId, s);
        return new int[] {Enchantment.efficiency.effectId, lvl};
    }

    /** Puts the Efficiency enchantment on the stack at level lvl; level 0 adds nothing. */
    static void setEfficiency(ItemStack s, int lvl)
    {
        if (s == null) return;
        s.stackTagCompound = null;
        if (lvl <= 0) return;
        NBTTagCompound tag = new NBTTagCompound();
        NBTTagList list = new NBTTagList();
        NBTTagCompound e = new NBTTagCompound();
        e.setShort("id", (short)Enchantment.efficiency.effectId);
        e.setShort("lvl", (short)lvl);
        list.appendTag(e);
        tag.setTag("ench", list);
        s.stackTagCompound = tag;
    }

    static void putI16(byte[] a, int o, int v)
    {
        a[o] = (byte)v;
        a[o + 1] = (byte)(v >> 8);
    }

    static void putI32(byte[] a, int o, int v)
    {
        a[o] = (byte)v;
        a[o + 1] = (byte)(v >> 8);
        a[o + 2] = (byte)(v >> 16);
        a[o + 3] = (byte)(v >> 24);
    }

    static void putF32(byte[] a, int o, float v)
    {
        putI32(a, o, Float.floatToRawIntBits(v));
    }
}