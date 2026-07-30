// Verbatim MC 1.11.2 chest insert/extract ground truth. Eval-pure: no game launch.
//
// Logic copied VERBATIM from the decompiled oracle:
//   net/minecraft/inventory/InventoryBasic.java  addItem(), setInventorySlotContents(),
//                                                 getStackInSlot(), decrStackSize()
//   net/minecraft/item/ItemStack.java            areItemsEqual()=item+damage, grow/shrink/copy
//   net/minecraft/tileentity/TileEntityChest.java update()  (lid-angle subset)
// getInventoryStackLimit()==64 (InventoryBasic default). All battery items (apple 260,
// bread 297, coal 263, iron ingot 265) have getMaxStackSize()==64, so the merge cap j is 64.
// Enchanted books: areItemsEqual is item+damage only; max stack 1 still blocks merge.
// Hand-port twin of C (not live Mojang execution).
//
// CUT (matches core/tile_entity_chest.h): double chest, loot tables, sounds, player proximity
// sync, adjacent-chest lid gating. Output format matches cpu/tile_entity_chest.c:
//   marks 0..5: 27 counts + lid + players + total + leftover count
//   marks 6..8: same + book extra (slot0/1 item+n_ench+e0id+e0lvl+e1id+e1lvl + leftover item+n_ench)
// All as %016x.
public class Golden {
    static final int SLOTS = 27;
    static final int STACK_LIMIT = 64;   // InventoryBasic.getInventoryStackLimit()
    static final int APPLE = 260, BREAD = 297, COAL = 263, IRON = 265;
    static final int MAX_ENCHANTS = 8;

    static class Stack {
        int item, count, meta, nEnchants;
        int[] enchId = new int[MAX_ENCHANTS];
        int[] enchLvl = new int[MAX_ENCHANTS];
        Stack(int i, int c, int m) { item = i; count = c; meta = m; nEnchants = 0; }
        boolean isEmpty() { return count <= 0 || item == 0; }
        Stack copy() {
            Stack s = new Stack(item, count, meta);
            s.nEnchants = nEnchants;
            for (int e = 0; e < nEnchants; ++e) {
                s.enchId[e] = enchId[e]; s.enchLvl[e] = enchLvl[e];
            }
            return s;
        }
    }
    static Stack empty() { return new Stack(0, 0, 0); }
    static Stack mkBookMulti() {
        Stack s = new Stack(ENCHANTED_BOOK, 1, 0);
        s.nEnchants = 2;
        s.enchId[0] = 16; s.enchLvl[0] = 3;
        s.enchId[1] = 34; s.enchLvl[1] = 1;
        return s;
    }
    static Stack mkBookSharp5() {
        Stack s = new Stack(ENCHANTED_BOOK, 1, 0);
        s.nEnchants = 1;
        s.enchId[0] = 16; s.enchLvl[0] = 5;
        return s;
    }

    // ItemStack.areItemsEqual / isItemEqual: item + damage only (no NBT).
    // InventoryBasic.addItem uses this; max-stack-1 books still fail to merge.
    static boolean areItemsEqual(Stack a, Stack b) {
        if (a.isEmpty() || b.isEmpty()) return false;
        return a.item == b.item && a.meta == b.meta;
    }

    Stack[] slots = new Stack[SLOTS];
    float lidAngle = 0.0F, prevLidAngle = 0.0F;
    int numPlayersUsing = 0, ticksSinceSync = 0;

    Golden() { for (int i = 0; i < SLOTS; ++i) slots[i] = empty(); }

    static final int ENCHANTED_BOOK = 403;
    int getInventoryStackLimit() { return STACK_LIMIT; }
    /* Item.getItemStackLimit subset: enchanted books max stack 1. */
    int getMaxStackSize(Stack s) {
        if (s.item == ENCHANTED_BOOK) return 1;
        return STACK_LIMIT;
    }

    // InventoryBasic.setInventorySlotContents: store, then clamp count to the inventory
    // stack limit (silently dropping any excess).
    void setInventorySlotContents(int index, Stack stack) {
        slots[index] = stack;
        if (!stack.isEmpty() && stack.count > getInventoryStackLimit())
            stack.count = getInventoryStackLimit();
    }

    // InventoryBasic.addItem VERBATIM.
    Stack addItem(Stack stack) {
        Stack itemstack = stack.copy();
        for (int i = 0; i < SLOTS; ++i) {
            Stack itemstack1 = slots[i];
            if (itemstack1.isEmpty()) {
                setInventorySlotContents(i, itemstack);
                return empty();
            }
            if (areItemsEqual(itemstack1, itemstack)) {
                int j = Math.min(getInventoryStackLimit(), getMaxStackSize(itemstack1));
                int k = Math.min(itemstack.count, j - itemstack1.count);
                if (k > 0) {
                    itemstack1.count += k;   // grow
                    itemstack.count -= k;    // shrink
                    if (itemstack.isEmpty()) return empty();
                }
            }
        }
        return itemstack;
    }

    // InventoryBasic.decrStackSize (get-and-split; copies StoredEnchantments).
    Stack decrStackSize(int index, int amount) {
        Stack slot = slots[index];
        if (slot.isEmpty() || amount <= 0) return empty();
        int take = Math.min(amount, slot.count);
        Stack out = new Stack(slot.item, take, slot.meta);
        out.nEnchants = slot.nEnchants;
        for (int e = 0; e < slot.nEnchants; ++e) {
            out.enchId[e] = slot.enchId[e]; out.enchLvl[e] = slot.enchLvl[e];
        }
        slot.count -= take;
        if (slot.count <= 0) slots[index] = empty();
        return out;
    }

    // setInventorySlotContents wrapper used by the battery's tec_set_slot (also clamps).
    void setSlot(int index, Stack stack) {
        if (!stack.isEmpty() && stack.count > STACK_LIMIT) stack.count = STACK_LIMIT;
        slots[index] = stack;
    }

    void open()  { if (numPlayersUsing < 0) numPlayersUsing = 0; numPlayersUsing++; }
    void close() { if (numPlayersUsing > 0) numPlayersUsing--; }

    // TileEntityChest.update lid-angle subset (no adjacent chest / sounds / sync branch).
    void tick() {
        ticksSinceSync++;
        prevLidAngle = lidAngle;
        if (numPlayersUsing == 0 && lidAngle > 0.0F || numPlayersUsing > 0 && lidAngle < 1.0F) {
            if (numPlayersUsing > 0) lidAngle += 0.1F;
            else lidAngle -= 0.1F;
            if (lidAngle > 1.0F) lidAngle = 1.0F;
            if (lidAngle < 0.0F) lidAngle = 0.0F;
        }
    }

    int totalItems() { int s = 0; for (int i = 0; i < SLOTS; ++i) s += slots[i].count; return s; }

    void dumpMark(Stack leftover, StringBuilder sb) {
        for (int i = 0; i < SLOTS; ++i) emit(sb, slots[i].count);
        emit(sb, Float.floatToRawIntBits(lidAngle));
        emit(sb, numPlayersUsing);
        emit(sb, totalItems());
        emit(sb, leftover.count);
    }
    void dumpBookExtra(Stack leftover, StringBuilder sb) {
        for (int s = 0; s < 2; ++s) {
            Stack sl = slots[s];
            emit(sb, sl.item);
            emit(sb, sl.nEnchants);
            emit(sb, sl.nEnchants > 0 ? sl.enchId[0] : 0);
            emit(sb, sl.nEnchants > 0 ? sl.enchLvl[0] : 0);
            emit(sb, sl.nEnchants > 1 ? sl.enchId[1] : 0);
            emit(sb, sl.nEnchants > 1 ? sl.enchLvl[1] : 0);
        }
        emit(sb, leftover.item);
        emit(sb, leftover.nEnchants);
    }
    static void emit(StringBuilder sb, int v) {
        sb.append(String.format("%016x", ((long) v) & 0xFFFFFFFFL)).append('\n');
    }

    public static void main(String[] args) {
        Golden c = new Golden();
        StringBuilder sb = new StringBuilder();
        Stack leftover;

        leftover = c.addItem(new Stack(APPLE, 20, 0));
        c.dumpMark(leftover, sb);

        leftover = c.addItem(new Stack(APPLE, 50, 0));
        c.dumpMark(leftover, sb);

        leftover = c.addItem(new Stack(BREAD, 30, 0));
        c.dumpMark(leftover, sb);

        c.open();
        for (int t = 0; t < 5; ++t) c.tick();
        leftover = c.addItem(new Stack(IRON, 10, 0));
        c.dumpMark(leftover, sb);

        leftover = c.decrStackSize(0, 15);
        c.setSlot(5, new Stack(COAL, 40, 0));
        leftover = c.addItem(new Stack(APPLE, 6, 0));
        c.close();
        for (int t = 0; t < 12; ++t) c.tick();
        c.dumpMark(leftover, sb);

        leftover = c.addItem(new Stack(BREAD, 200, 0));
        c.dumpMark(leftover, sb);

        /* Marks 6..8: enchanted-book max stack 1 + StoredEnchantments */
        Golden bc = new Golden();
        leftover = bc.addItem(mkBookMulti());
        bc.dumpMark(leftover, sb);
        bc.dumpBookExtra(leftover, sb);

        leftover = bc.addItem(mkBookMulti());
        bc.dumpMark(leftover, sb);
        bc.dumpBookExtra(leftover, sb);

        leftover = bc.addItem(mkBookSharp5());
        leftover = bc.decrStackSize(0, 1); /* extract multi with tags */
        bc.dumpMark(leftover, sb);
        bc.dumpBookExtra(leftover, sb);

        System.out.print(sb);
    }
}
