package netherite.oracle;

import static netherite.oracle.GoldBot.*;

import java.util.HashSet;
import net.minecraft.entity.Entity;
import net.minecraft.entity.item.EntityItem;
import net.minecraft.item.Item;
import net.minecraft.util.AxisAlignedBB;
import net.minecraft.util.MathHelper;
import net.minecraft.item.ItemStack;

/**
 * GoldBot segment G28: the kit for the End, made at the stronghold from
 * what the trek carried (3 string, 29 feathers, cobblestone, raw meat, 2
 * coal). GoldEnd shoots the crystals with a bow (endbot-s42 spent 22
 * arrows on its 14) and eats cooked meat; the stronghold stage digs stone
 * brick with a pickaxe. So: logs from the nearest trees, one gravel block,
 * a table with sticks, a bow, a stone shovel (gravel breaks in 5 ticks, 18
 * by hand), two stone pickaxes and a furnace; the raw meat cooks with the
 * coal while the gravel is placed and mined again until FLINT_WANT flint
 * (BlockGravel drops flint one time in ten); then arrows, four per flint,
 * stick and feather. Everything is ordinary input.
 */
final class GoldKit
{
    private GoldKit() {}

    static final int STRING = 287, FEATHER = 288, BOW = 261, ARROW = 262, SSHOVEL = 273;
    /** 48 arrows from 12 flint, plus the 3 the trek picked up. */
    static final int FLINT_WANT = 12, LOGS_WANT = 5;

    static final Recipe BOW_R = new Recipe("bow", BOW, 1, false, new String[] {" SX", "S X", " SX"}, 'S', STICK, 'X', STRING);
    static final Recipe SSHOVEL_R = new Recipe("stone shovel", SSHOVEL, 1, false, new String[] {"C", "S", "S"}, 'C', COBBLE, 'S', STICK);
    static final Recipe ARROW_R = new Recipe("arrows", ARROW, 4, false, new String[] {"F", "S", "E"}, 'F', FLINT, 'S', STICK, 'E', FEATHER);
    static final Recipe PLANKS2_ALL = new Recipe("planks", PLANKS, 4, true, new String[] {"L"}, 'L', LOG2);

    /** Dropped to make room: nothing the kit, the stronghold or the End uses. */
    static final int[] KIT_JUNK = {3, 2, 337, 352, 334, 295, 6, 344, 37, 38, 87, 12, 112, 289, 375, 15, 264};
    /** Seed 1: the stronghold's stone brick, books and bookshelves, silverfish
     * stone and flowers too. */
    static final int[] S1_KIT_JUNK = {3, 2, 337, 352, 334, 295, 6, 344, 37, 38, 87, 12, 112, 289, 375, 15, 264, 98, 340, 47, 97, 175, 13};
    /** Seed 1, after the sticks: the leftover planks and logs as well. */
    static final int[] S1_KIT_JUNK2 = {3, 2, 337, 352, 334, 295, 6, 344, 37, 38, 87, 12, 112, 289, 375, 15, 264, 98, 340, 47, 97, 175, 13, 5, 17, 162, 39, 40};

    static void log(String s)
    {
        System.out.println("GOLDBOT t=" + Oracle.tick + " " + s);
    }

    /** The largest single stack of an item (a recipe takes from one). */
    static int stack(int id)
    {
        int n = 0;
        for (int i = 0; i < 36; ++i)
        {
            ItemStack s = cp.inventory.mainInventory[i];
            if (s != null && net.minecraft.item.Item.getIdFromItem(s.getItem()) == id) n = Math.max(n, s.stackSize);
        }
        return n;
    }

    /** Pick up the dropped items of the given kinds within 6 blocks: wait
     * while one falls or its pickup delay runs, stand while it lies in the
     * pickup box (the player's box grown by 1, 0.5, 1), else walk to a cell
     * whose box takes it in. */
    static final class Pickup extends Task
    {
        final int[] ids;
        final HashSet<Integer> ignore = new HashSet<Integer>();
        int t, target = -1, walks, inBox;

        Pickup(int[] ids) { this.ids = ids; }

        Object tick()
        {
            if (child == FAIL && target >= 0 && ++walks > 2)
            {
                ignore.add(target);
                walks = 0;
            }
            child = null;
            if (++t > 300) return DONE;
            EntityItem e = null;
            double bd = 36.0;
            for (Object o : ws.loadedEntityList)
            {
                if (!(o instanceof EntityItem) || ((Entity)o).isDead || ignore.contains(((Entity)o).getEntityId())) continue;
                EntityItem it = (EntityItem)o;
                int d = Item.getIdFromItem(it.getEntityItem().getItem());
                boolean ok = false;
                for (int k : ids) if (d == k) ok = true;
                double dd = it.getDistanceSq(sp.posX, sp.posY, sp.posZ);
                if (ok && dd < bd)
                {
                    bd = dd;
                    e = it;
                }
            }
            if (e == null) return DONE;
            if (e.getEntityId() != target)
            {
                target = e.getEntityId();
                walks = 0;
                inBox = 0;
            }
            if (!e.onGround || e.delayBeforeCanPickup > 0) return idle();
            // seed 1: not while the player falls (a walk to the cell it is
            // falling into ends at once, and the pickup looped without an act)
            if (s1 && s1Num() >= 5 && !cp.onGround && !cp.isInWater()) return idle();
            AxisAlignedBB box = sp.boundingBox.expand(1.0D, 0.5D, 1.0D);
            if (box.intersectsWith(e.boundingBox))
            {
                if (++inBox > 40) ignore.add(target);
                return idle();
            }
            final double ix = e.posX, iz = e.posZ;
            final int iy = MathHelper.floor_double(e.posY + 0.05);
            return new Walk("item " + target, (a, b, c) -> Math.abs(ix - (a + 0.5)) < 1.2 && Math.abs(iz - (c + 0.5)) < 1.2 && b <= iy && b >= iy - 2, 0.4);
        }
    }

    /** A cell under open sky with the eight around it level, free and floored. */
    static boolean open(int a, int b, int c)
    {
        if (!stand(a, b, c) || !ws.canBlockSeeTheSky(a, b + 1, c)) return false;
        for (int dx = -1; dx <= 1; ++dx)
            for (int dz = -1; dz <= 1; ++dz)
                if (!pass(a + dx, b, c + dz) || !pass(a + dx, b + 1, c + dz) || !floor(a + dx, b - 1, c + dz) || water(a + dx, b, c + dz)) return false;
        return true;
    }

    static final class SegKit extends Seg
    {
        final HashSet<Long> badTrees = new HashSet<Long>();
        int chops, webs, loaded, hunts, clears;
        boolean webWalked, webDug, tidied, tidied2;
        int[] web, start;
        int[] table, furnace;

        Object tick()
        {
            switch (phase)
            {
                case 0:
                    start = cell();
                    log("the kit for the End at " + where() + " inv=" + invString());
                    phase = 1;
                    if (Tidy.free() < 10) return new Tidy(KIT_JUNK);
                    return idle();
                case 1:
                {
                    // seed 1: meat for the End (its fight is healed by eating),
                    // ten pieces while animals are near
                    if (s1 && count(RAW_MEAT) + count(COOKED_MEAT) < 10 && ++hunts <= 3 && animalWithin(64.0))
                    {
                        log("meat " + (count(RAW_MEAT) + count(COOKED_MEAT)) + ": a hunt before the kit");
                        return new Hunt(10, 1);
                    }
                    // wood: sticks for the bow, shovel, picks and arrows, a table
                    if (count(LOG, LOG2) >= LOGS_WANT)
                    {
                        phase = 3;
                        return idle();
                    }
                    need(++chops <= 10, "no wood after 10 trees");
                    Chop c = new Chop(badTrees);
                    need(c.pick(), "no tree the walk reaches");
                    phase = 2;
                    return c;
                }
                case 2:
                    phase = 1;
                    return idle();
                case 3:
                    // seed 1: the trek brought no string; a cobweb cut with
                    // the sword drops one (the mineshafts under the stronghold)
                    // seed 1 from S21: over the land to above the nearest
                    // cobweb first (a dig of 145 blocks and 33 down found no
                    // route within the planner's nodes)
                    if (s1 && s1Num() >= 21 && count(STRING) < 3 && !webWalked)
                    {
                        webWalked = true;
                        int[] c0 = cell(), w = null;
                        double bd = 1e18;
                        for (int[] a : scan(new int[] {30}, 6, 90))
                        {
                            double d = (a[0] - c0[0]) * (double)(a[0] - c0[0]) + (a[2] - c0[2]) * (double)(a[2] - c0[2]);
                            if (d < bd)
                            {
                                bd = d;
                                w = a;
                            }
                        }
                        // over the land to low ground within 30 blocks of it (the
                        // hilltop straight above it stood 60 blocks over it)
                        if (w != null)
                        {
                            web = w;
                            final int wx = w[0], wy = w[1], wz = w[2];
                            if (bd > 30.0 * 30.0)
                            {
                                log("string " + count(STRING) + ": over the land toward the cobweb at " + w[0] + "," + w[1] + "," + w[2]);
                                return new Walk("toward the cobweb", (a, b, cc) -> (a - wx) * (a - wx) + (cc - wz) * (cc - wz) < 900 && b >= ws.getHeightValue(a, cc) - 1
                                    && b <= wy + 36, 0.6).hint(wx + 0.5, wz + 0.5);
                            }
                        }
                    }
                    // then down to its level beside it (a dig of 60 blocks
                    // straight to a cobweb cell found no route)
                    if (s1 && s1Num() >= 21 && count(STRING) < 3 && web != null && !webDug && Math.abs(cell()[1] - web[1]) > 6)
                    {
                        webDug = true;
                        final int wx = web[0], wy = web[1], wz = web[2];
                        log("string " + count(STRING) + ": down to the cobweb's level from " + where());
                        DigTo d = new DigTo("down to the cobwebs", (a, b, cc) -> Math.abs(b - wy) <= 3 && (a - wx) * (a - wx) + (cc - wz) * (cc - wz) < 256, wx + 0.5, wy, wz + 0.5);
                        d.maxNodes = 150000;
                        // greedy: sixty levels of stone cost more than every
                        // walk around the hill the plain weight tries first
                        d.heur = 15.0;
                        return d;
                    }
                    if (s1 && count(STRING) < 3 && ++webs <= 2)
                    {
                        log("string " + count(STRING) + ": cobwebs");
                        return new OreLoop(new int[] {30}, new int[] {STRING}, 3, 6, 90);
                    }
                    need(count(STRING) >= 3, "no string for the bow");
                    phase = 4;
                    if (count(GRAVEL) + count(FLINT) > 0) return idle();
                    log("gravel for flint");
                    return new OreLoop(new int[] {GRAVEL}, new int[] {GRAVEL, FLINT}, 1, 40, 90);
                case 4:
                {
                    need(count(GRAVEL) + count(FLINT) > 0, "no gravel");
                    // the table, the furnace and the gravel want room: out of
                    // the dig onto open level ground
                    int[] c = cell();
                    phase = 40;
                    if (open(c[0], c[1], c[2])) return idle();
                    log("to open ground from " + where());
                    DigTo up = new DigTo("open ground", GoldKit::open, sp.posX, ws.getHeightValue(c[0], c[2]), sp.posZ);
                    // seed 1 from S21: greedy up from the cobwebs' depth, back
                    // toward where the kit began (the hill over the cobwebs
                    // stood 60 blocks above them)
                    if (s1 && s1Num() >= 21)
                    {
                        up = new DigTo("open ground", GoldKit::open, start[0] + 0.5, start[1], start[2] + 0.5);
                        up.heur = 15.0;
                    }
                    return up;
                }
                case 40:
                    need(child != FAIL, "no way to open ground");
                    // seed 1 from S21: the plants around cut first (tall grass
                    // left no air cell to place the table in)
                    if (s1 && s1Num() >= 21 && ++clears <= 20)
                    {
                        int[] c = cell();
                        for (int dx = -1; dx <= 1; ++dx)
                            for (int dz = -1; dz <= 1; ++dz)
                                for (int dy = 0; dy <= 1; ++dy)
                                {
                                    int x = c[0] + dx, y = c[1] + dy, z = c[2] + dz;
                                    if ((dx != 0 || dz != 0) && id(x, y, z) != 0 && pass(x, y, z) && !water(x, y, z)) return new Mine(x, y, z, true);
                                }
                    }
                    phase = 5;
                    if (count(LOG) > 0)
                    {
                        Craft c = new Craft(false, PLANKS_ALL, 1);
                        c.fresh = true;
                        return c;
                    }
                    return idle();
                case 5:
                    phase = 6;
                    if (count(LOG2) > 0)
                    {
                        Craft c = new Craft(false, PLANKS2_ALL, 1);
                        c.fresh = true;
                        return c;
                    }
                    return idle();
                case 6:
                    // seed 1 from S21: room for the crafts (the dig to the
                    // cobwebs filled the pack with stone brick and books)
                    if (s1 && s1Num() >= 21 && Tidy.free() < 5 && !tidied)
                    {
                        tidied = true;
                        return new Tidy(S1_KIT_JUNK);
                    }
                    need(stack(PLANKS) >= 4, "no planks for a table");
                    phase = 7;
                    return new Craft(false, TABLE_R, 1);
                case 7:
                    phase = 8;
                    return new ToHotbar(TABLE);
                case 8:
                    need(child != FAIL, "the table not on the hotbar");
                    phase = 9;
                    return new PlaceAny(TABLE, TABLE);
                case 9:
                {
                    need(child != FAIL, "the table not placed");
                    table = findBlock(new int[] {TABLE}, 6);
                    need(table != null, "the table is gone");
                    // 3 bow + 2 shovel + 4 picks + 12 arrows = 21
                    int want = 21 - count(STICK), n = Math.min((want + 3) / 4, stack(PLANKS) / 2);
                    phase = 10;
                    if (n <= 0) return idle();
                    Craft c = new Craft(true, STICKS, n);
                    return c;
                }
                case 10:
                    need(child != FAIL, "no sticks");
                    // seed 1 from S21: five results want five free slots; the
                    // planks and logs left over from the sticks go too
                    if (s1 && s1Num() >= 21 && Tidy.free() < 7 && !tidied2)
                    {
                        tidied2 = true;
                        return new Tidy(S1_KIT_JUNK2);
                    }
                    phase = 11;
                    log("sticks " + count(STICK) + ", string " + count(STRING) + ", feathers " + count(FEATHER) + ", cobble " + count(COBBLE));
                    return new Craft(true, BOW_R, 1, SSHOVEL_R, 1, SPICK_R, 2, FURNACE_R, 1);
                case 11:
                    need(child != FAIL, "bow, shovel, picks, furnace not crafted");
                    phase = 12;
                    return new ToHotbar(FURNACE);
                case 12:
                    need(child != FAIL, "the furnace not on the hotbar");
                    phase = 13;
                    return new PlaceAny(FURNACE, FURNACE);
                case 13:
                    need(child != FAIL, "the furnace not placed");
                    furnace = findBlock(new int[] {FURNACE}, 6);
                    need(furnace != null, "the furnace is gone");
                    phase = 14;
                    if (count(CHICKEN) > 0 && count(COAL) > 0) return new FurnaceLoad(furnace, new int[] {CHICKEN}, COAL, 1);
                    return idle();
                case 14:
                    need(child != FAIL, "the furnace not loaded");
                    phase = 15;
                    return new ToHotbar(SSHOVEL);
                case 15:
                    phase = 16;
                    log("flint from gravel, " + count(GRAVEL) + " gravel");
                    return new GetFlint(FLINT_WANT, 400);
                case 16:
                {
                    log("flint " + count(FLINT) + " at " + where());
                    phase = 161;
                    // seed 1: the gravel work can end down a slope, out of the
                    // walk's reach of the furnace: dig back beside it
                    int[] c = cell();
                    if (s1 && s1Num() >= 5 && (Math.abs(c[0] - furnace[0]) + Math.abs(c[2] - furnace[2]) > 3 || Math.abs(c[1] - furnace[1]) > 1))
                    {
                        final int[] f = furnace;
                        log("back to the furnace at " + f[0] + "," + f[1] + "," + f[2]);
                        return new DigTo("the furnace", (a, b, cc) -> Math.abs(a - f[0]) + Math.abs(cc - f[2]) <= 2 && Math.abs(b - f[1]) <= 1
                            && !(a == f[0] && cc == f[2]), f[0] + 0.5, f[1], f[2] + 0.5);
                    }
                    if (s1) return idle();
                    // seed 42: the furnace at once, as before the seed-1 step
                    phase = 17;
                    return new FurnaceTake(furnace);
                }
                case 161:
                    phase = 17;
                    return new FurnaceTake(furnace);
                case 17:
                    phase = 18;
                    if (s1)
                    {
                        // seed 1: the larger of the raw pork and beef, fuel for all
                        // of it (one kind fits the furnace's input)
                        int kind = count(BEEF) >= count(PORK) ? BEEF : PORK;
                        loaded = stack(kind);
                        if (loaded > 0 && count(COAL) > 0) return new FurnaceLoad(furnace, new int[] {kind}, COAL, Math.min(stack(COAL), (loaded + 7) / 8));
                        return idle();
                    }
                    if (count(BEEF) > 0 && count(COAL) > 0) return new FurnaceLoad(furnace, new int[] {BEEF}, COAL, 1);
                    return idle();
                case 18:
                {
                    need(child != FAIL, "the furnace not loaded again");
                    int n = Math.min(count(FLINT), Math.min(stack(FEATHER), stack(STICK)));
                    need(n > 0, "nothing for arrows");
                    phase = 19;
                    return new Craft(true, ARROW_R, n);
                }
                case 19:
                    need(child != FAIL, "no arrows");
                    log("arrows " + count(ARROW));
                    phase = 20;
                    if (s1) return new WaitSmelt(furnace, loaded);
                    return new WaitSmelt(furnace, count(BEEF) + (furnaceOut() == null ? 0 : furnaceOut().stackSize));
                case 20:
                    phase = 21;
                    return new FurnaceTake(furnace);
                case 21:
                    phase = 22;
                    return new ToHotbar(SPICK);
                case 22:
                    phase = 23;
                    return new ToHotbar(COOKED_MEAT);
                case 23:
                    log("kit done at " + where() + ": arrows " + count(ARROW) + ", bow " + count(BOW) + ", cooked " + count(COOKED_MEAT)
                        + ", picks " + count(SPICK) + ", eyes " + count(EYE) + " inv=" + invString());
                    phase = 24;
                    return new Idle(3);
                default:
                    return DONE;
            }
        }

        ItemStack furnaceOut()
        {
            net.minecraft.tileentity.TileEntityFurnace f = furnaceAt(furnace);
            return f == null ? null : f.getStackInSlot(2);
        }
    }
}
