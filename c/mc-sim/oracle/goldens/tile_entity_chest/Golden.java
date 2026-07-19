// Verbatim MC 1.11.2 chest insert/extract ground truth. Eval-pure: no game launch.
//
// Logic copied VERBATIM from the decompiled oracle:
//   net/minecraft/inventory/InventoryBasic.java  addItem(), setInventorySlotContents(),
//                                                 getStackInSlot(), decrStackSize()
//   net/minecraft/item/ItemStack.java            areItemsEqual(), grow(), shrink(), copy()
//   net/minecraft/tileentity/TileEntityChest.java update()  (lid-angle subset)
// getInventoryStackLimit()==64 (InventoryBasic default). All battery items (apple 260,
// bread 297, coal 263, iron ingot 265) have getMaxStackSize()==64, so the merge cap j is 64.
//
// CUT (matches core/tile_entity_chest.h): double chest, loot, NBT, sounds, player proximity
// sync, adjacent-chest lid gating. Output format matches cpu/tile_entity_chest.c
// (6 marks * (27 slot counts + lid bits + players + total + leftover count), %016llx).
public class Golden {
    static final int SLOTS = 27;
    static final int STACK_LIMIT = 64;   // InventoryBasic.getInventoryStackLimit()
    static final int APPLE = 260, BREAD = 297, COAL = 263, IRON = 265;

    static class Stack {
        int item, count, meta;
        Stack(int i, int c, int m) { item = i; count = c; meta = m; }
        boolean isEmpty() { return count <= 0 || item == 0; }
        Stack copy() { return new Stack(item, count, meta); }
    }
    static Stack empty() { return new Stack(0, 0, 0); }

    // ItemStack.areItemsEqual: same item + same meta (our stacks carry no NBT).
    static boolean areItemsEqual(Stack a, Stack b) {
        if (a.isEmpty() || b.isEmpty()) return false;
        return a.item == b.item && a.meta == b.meta;
    }

    Stack[] slots = new Stack[SLOTS];
    float lidAngle = 0.0F, prevLidAngle = 0.0F;
    int numPlayersUsing = 0, ticksSinceSync = 0;

    Golden() { for (int i = 0; i < SLOTS; ++i) slots[i] = empty(); }

    int getInventoryStackLimit() { return STACK_LIMIT; }
    int getMaxStackSize(Stack s) { return STACK_LIMIT; }

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

    // InventoryBasic.decrStackSize (get-and-split).
    Stack decrStackSize(int index, int amount) {
        Stack slot = slots[index];
        if (slot.isEmpty() || amount <= 0) return empty();
        int take = Math.min(amount, slot.count);
        Stack out = new Stack(slot.item, take, slot.meta);
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

        System.out.print(sb);
    }
}
