// Verbatim MC 1.11.2 inventory stack-rules ground truth. Eval-pure: no game launch.
// Args: [scenario] to run one, else the whole battery (matches cpu/inventory_stack_rules.c).
//
// Logic copied VERBATIM from the decompiled oracle:
//   net/minecraft/entity/player/InventoryPlayer.java  canMergeStacks(), storeItemStack(),
//     storePartialItemStack(), addItemStackToInventory(), decrStackSize(), getBestHotbarSlot(),
//     pickItem(), changeCurrentItem(), getFirstEmptyStack(), getInventoryStackLimit()==64
//   net/minecraft/item/ItemStack.java  isStackable(), splitStack(), getMaxStackSize()
//   net/minecraft/inventory/ItemStackHelper.java  getAndSplit()
// Registry substitution: Item objects -> legacy int ids (core/items_core.h). getMaxStackSize is
// baked from the item table (pickaxes / buckets / enchanted_book 403 == 1, else 64).
// Battery stacks carry no StoredEnchantments; getBestHotbarSlot's second loop returns currentItem.
//
// CUT (matches core/inventory_stack_rules.h): armor slots, creative-mode branches (never taken:
// non-creative), GUI/animation/NBT, offhand beyond slot 40. Output format matches
// cpu/inventory_stack_rules.c: 10 scenarios * 8 u32 fields, %08x.
public class Golden {
    static final int MAIN_SLOTS = 36, OFFHAND_SLOT = 40, INV_LIMIT = 64;
    static final int AIR = 0, STONE = 1, IRON_ORE = 15, APPLE = 260, BREAD = 297,
        WOODEN_PICKAXE = 270, STONE_PICKAXE = 274, ENCHANTED_BOOK = 403,
        BUCKET = 325, WATER_BUCKET = 326, LAVA_BUCKET = 327;

    static class Stack {
        int item, count, meta;
        Stack(int i, int c, int m) { item = i; count = c; meta = m; }
        Stack copy() { return new Stack(item, count, meta); }
    }
    static Stack empty() { return new Stack(AIR, 0, 0); }
    static Stack mk(int i, int c, int m) { return new Stack(i, c, m); }
    static boolean isEmpty(Stack s) { return s.item == AIR || s.count <= 0; }

    static int maxStackSize(int item, int meta) {
        if (item == WOODEN_PICKAXE || item == STONE_PICKAXE) return 1;
        if (item == ENCHANTED_BOOK) return 1;
        if (item == BUCKET || item == WATER_BUCKET || item == LAVA_BUCKET) return 1;
        return INV_LIMIT;
    }
    static boolean isDamageable(int item) { return item == WOODEN_PICKAXE || item == STONE_PICKAXE; }
    static boolean isStackable(int item, int meta) {
        return maxStackSize(item, meta) > 1 && (!isDamageable(item) || meta <= 0);
    }
    static boolean stackEqualExact(Stack a, Stack b) {
        if (isEmpty(a) || isEmpty(b)) return false;
        return a.item == b.item && a.meta == b.meta;
    }

    Stack[] main = new Stack[MAIN_SLOTS];
    Stack offhand = empty();
    int currentItem = 0;

    Golden() { for (int i = 0; i < MAIN_SLOTS; ++i) main[i] = empty(); }

    Stack getStack(int index) {
        if (index >= 0 && index < MAIN_SLOTS) return main[index];
        if (index == OFFHAND_SLOT) return offhand;
        return empty();
    }
    void setStack(int index, Stack s) {
        if (index >= 0 && index < MAIN_SLOTS) main[index] = s;
        else if (index == OFFHAND_SLOT) offhand = s;
    }

    // InventoryPlayer.canMergeStacks VERBATIM.
    boolean canMergeStacks(Stack s1, Stack s2) {
        return !isEmpty(s1) && stackEqualExact(s1, s2) && isStackable(s1.item, s1.meta)
            && s1.count < maxStackSize(s1.item, s1.meta) && s1.count < INV_LIMIT;
    }
    // InventoryPlayer.getFirstEmptyStack.
    int getFirstEmptyStack() {
        for (int i = 0; i < MAIN_SLOTS; ++i) if (isEmpty(main[i])) return i;
        return -1;
    }
    // InventoryPlayer.storeItemStack.
    int storeItemStack(Stack in) {
        if (canMergeStacks(getStack(currentItem), in)) return currentItem;
        if (canMergeStacks(getStack(OFFHAND_SLOT), in)) return OFFHAND_SLOT;
        for (int i = 0; i < MAIN_SLOTS; ++i) if (canMergeStacks(main[i], in)) return i;
        return -1;
    }
    // InventoryPlayer.storePartialItemStack VERBATIM.
    int storePartialItemStack(Stack in) {
        int i = in.count;
        int j = storeItemStack(in);
        if (j == -1) j = getFirstEmptyStack();
        if (j == -1) return i;
        Stack slot = getStack(j);
        if (isEmpty(slot)) {
            slot = in.copy();
            slot.count = 0;
            setStack(j, slot);
            slot = getStack(j);
        }
        int k = i;
        if (i > maxStackSize(slot.item, slot.meta) - slot.count) k = maxStackSize(slot.item, slot.meta) - slot.count;
        if (k > INV_LIMIT - slot.count) k = INV_LIMIT - slot.count;
        if (k == 0) return i;
        i = i - k;
        slot.count += k;             // grow(k)
        setStack(j, slot);
        return i;
    }
    // InventoryPlayer.addItemStackToInventory VERBATIM (non-creative).
    int addItemStackToInventory(Stack in) {
        if (isEmpty(in)) return 0;
        if (isDamageable(in.item) && in.meta > 0) {   // isItemDamaged
            int j = getFirstEmptyStack();
            if (j >= 0) { setStack(j, in.copy()); in.count = 0; return 1; }
            return 0;
        }
        int prev;
        while (true) {
            prev = in.count;
            in.count = storePartialItemStack(in);
            if (isEmpty(in) || in.count >= prev) break;
        }
        return in.count < prev ? 1 : 0;
    }
    // ItemStack.splitStack + ItemStackHelper.getAndSplit.
    Stack split(Stack src, int amount) {
        int take = Math.min(amount, src.count);
        Stack out = mk(src.item, take, src.meta);
        src.count -= take;
        if (src.count <= 0) { src.item = AIR; src.count = 0; src.meta = 0; }
        return out;
    }
    Stack decrStackSize(int index, int count) {
        Stack slot;
        if (index >= 0 && index < MAIN_SLOTS) slot = main[index];
        else if (index == OFFHAND_SLOT) slot = offhand;
        else return empty();
        if (isEmpty(slot) || count <= 0) return empty();
        return split(slot, count);
    }
    // InventoryPlayer.getBestHotbarSlot (all subset stacks unenchanted).
    int getBestHotbarSlot() {
        for (int i = 0; i < 9; ++i) { int j = (currentItem + i) % 9; if (isEmpty(main[j])) return j; }
        for (int i = 0; i < 9; ++i) { int j = (currentItem + i) % 9; return j; }
        return currentItem;
    }
    void changeCurrentItem(int direction) {
        if (direction > 0) direction = 1;
        if (direction < 0) direction = -1;
        for (currentItem -= direction; currentItem < 0; currentItem += 9) ;
        while (currentItem >= 9) currentItem -= 9;
    }
    // InventoryPlayer.pickItem.
    void pickItem(int index) {
        currentItem = getBestHotbarSlot();
        Stack tmp = main[currentItem];
        main[currentItem] = main[index];
        main[index] = tmp;
    }

    int hotbarTotal() { int s = 0; for (int i = 0; i < 9; ++i) s += main[i].count; return s; }
    int mainTotal() { int s = 0; for (int i = 9; i < MAIN_SLOTS; ++i) s += main[i].count; return s; }

    void emit(StringBuilder sb, Stack leftover, int opOk, int mergeSlot) {
        u(sb, currentItem);
        u(sb, leftover != null ? leftover.count : 0);
        u(sb, leftover != null ? leftover.item : 0);
        u(sb, hotbarTotal());
        u(sb, mainTotal());
        u(sb, main[0].count);
        u(sb, opOk != 0 ? 1 : 0);
        u(sb, mergeSlot);
    }
    static void u(StringBuilder sb, int v) {
        sb.append(String.format("%08x", ((long) v) & 0xFFFFFFFFL)).append('\n');
    }

    static void runScenario(int idx, StringBuilder sb) {
        Golden inv = new Golden();
        Stack incoming, leftover, split;
        int opOk, mergeSlot;
        switch (idx) {
        case 0:
            inv.currentItem = 0;
            incoming = mk(APPLE, 30, 0);
            leftover = incoming.copy();
            opOk = inv.addItemStackToInventory(leftover);
            mergeSlot = inv.storeItemStack(incoming);
            inv.emit(sb, leftover, opOk, mergeSlot);
            break;
        case 1:
            inv.main[0] = mk(APPLE, 40, 0);
            incoming = mk(APPLE, 30, 0);
            leftover = incoming.copy();
            opOk = inv.addItemStackToInventory(leftover);
            inv.emit(sb, leftover, opOk, 0);
            break;
        case 2:
            inv.currentItem = 3;
            inv.main[3] = mk(BREAD, 20, 0);
            incoming = mk(BREAD, 10, 0);
            leftover = incoming.copy();
            opOk = inv.addItemStackToInventory(leftover);
            inv.emit(sb, leftover, opOk, 3);
            break;
        case 3:
            inv.main[5] = mk(APPLE, 50, 0);
            split = inv.decrStackSize(5, 32);
            leftover = split;
            opOk = split.count == 32 ? 1 : 0;
            inv.emit(sb, leftover, opOk, 5);
            break;
        case 4:
            inv.currentItem = 2;
            mergeSlot = inv.getBestHotbarSlot();
            inv.emit(sb, empty(), 1, mergeSlot);
            break;
        case 5:
            inv.currentItem = 4;
            for (int i = 0; i < 9; ++i) inv.main[i] = mk(APPLE, 8, 0);
            mergeSlot = inv.getBestHotbarSlot();
            inv.emit(sb, empty(), 1, mergeSlot);
            break;
        case 6:
            inv.currentItem = 8;
            inv.changeCurrentItem(1);
            mergeSlot = inv.currentItem;
            inv.changeCurrentItem(-1);
            inv.emit(sb, empty(), 1, mergeSlot);
            break;
        case 7:
            inv.currentItem = 1;
            inv.main[15] = mk(STONE, 10, 0);
            inv.main[1] = mk(IRON_ORE, 5, 0);
            inv.pickItem(15);
            mergeSlot = inv.currentItem;
            inv.emit(sb, empty(), 1, mergeSlot);
            break;
        case 8:
            incoming = mk(WOODEN_PICKAXE, 1, 0);
            leftover = incoming.copy();
            opOk = inv.addItemStackToInventory(leftover);
            incoming = mk(WOODEN_PICKAXE, 1, 0);
            opOk &= inv.addItemStackToInventory(incoming);
            leftover = incoming.copy();
            mergeSlot = inv.getFirstEmptyStack();
            inv.emit(sb, leftover, opOk, mergeSlot);
            break;
        case 9:
            inv.currentItem = 0;
            inv.main[0] = mk(APPLE, 64, 0);
            inv.offhand = mk(APPLE, 10, 0);
            incoming = mk(APPLE, 60, 0);
            leftover = incoming.copy();
            opOk = inv.addItemStackToInventory(leftover);
            inv.emit(sb, leftover, opOk, OFFHAND_SLOT);
            break;
        default:
            inv.emit(sb, empty(), 0, -1);
            break;
        }
    }

    public static void main(String[] args) {
        StringBuilder sb = new StringBuilder();
        if (args.length > 0) {
            runScenario(Integer.parseInt(args[0]), sb);
        } else {
            for (int i = 0; i < 10; ++i) runScenario(i, sb);
        }
        System.out.print(sb);
    }
}
