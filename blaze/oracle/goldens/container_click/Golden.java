// Verbatim-modeled MC 1.11.2 Container.slotClick ground truth. Eval-pure: no game launch.
//
// Logic from the decompiled oracle:
//   net/minecraft/inventory/Container.java  slotClick (PICKUP/QUICK_MOVE/THROW), mergeItemStack
//   net/minecraft/inventory/Slot.java       getStack/putStack/decrStackSize/getItemStackLimit
//   net/minecraft/item/ItemStack.java       splitStack/grow/shrink/isEmpty/getMaxStackSize
//   net/minecraft/inventory/ClickType.java  ordinals PICKUP=0 QUICK_MOVE=1 THROW=4
//
// Synthetic 9-slot inventory + cursor (no crafting matrix). Drop = discard (no entity spawn).
// Stack limits: pickaxes/buckets/enchanted_book max 1, else 64 (matches items_core /
// inventory_stack_rules). Emit includes StoredEnchantments subset (n + 8 id/level pairs;
// battery stacks always have n=0).
//
// CUT: drag painting (QUICK_CRAFT), CLONE, SWAP, PICKUP_ALL, armor special cases,
// full arbitrary NBT beyond StoredEnchantments, recipe book, creative. Cursor drop is
// PICKUP slotId=-999 (vanilla); THROW drops from slot when cursor empty.
// Output: 32 clicks * 10 stacks * (4 + 2*8) fields as %08x
//   (phase1 ordinary 16 + phase2 StoredEnchantments books 16).
public class Golden {
    static final int SLOTS = 9, INV_LIMIT = 64, NUM_CLICKS = 32, PHASE1 = 16, PHASE2 = 16;
    static final int MAX_ENCHANTS = 8;
    static final int PICKUP = 0, QUICK_MOVE = 1, THROW = 4;
    static final int AIR = 0, STONE = 1, IRON_ORE = 15, APPLE = 260, BREAD = 297,
        WOODEN_PICKAXE = 270, STONE_PICKAXE = 274, ENCHANTED_BOOK = 403,
        BUCKET = 325, WATER_BUCKET = 326, LAVA_BUCKET = 327;

    static class Stack {
        int item, count, meta, nEnchants;
        int[] enchId = new int[MAX_ENCHANTS];
        int[] enchLvl = new int[MAX_ENCHANTS];
        Stack(int i, int c, int m) { item = i; count = c; meta = m; nEnchants = 0; }
        Stack copy() {
            Stack s = new Stack(item, count, meta);
            s.nEnchants = nEnchants;
            for (int e = 0; e < nEnchants; ++e) {
                s.enchId[e] = enchId[e]; s.enchLvl[e] = enchLvl[e];
            }
            return s;
        }
    }
    static Stack empty() { return new Stack(AIR, 0, 0); }
    static Stack mk(int i, int c, int m) { return new Stack(i, c, m); }
    static Stack mkBookMulti() {
        Stack s = mk(ENCHANTED_BOOK, 1, 0);
        s.nEnchants = 2;
        s.enchId[0] = 16; s.enchLvl[0] = 3; /* Sharpness III */
        s.enchId[1] = 34; s.enchLvl[1] = 1; /* Unbreaking I */
        return s;
    }
    static Stack mkBookSharp5() {
        Stack s = mk(ENCHANTED_BOOK, 1, 0);
        s.nEnchants = 1;
        s.enchId[0] = 16; s.enchLvl[0] = 5; /* Sharpness V */
        return s;
    }
    static boolean isEmpty(Stack s) { return s.item == AIR || s.count <= 0; }
    static void normalize(Stack s) {
        if (s.count <= 0 || s.item == AIR) {
            s.item = AIR; s.count = 0; s.meta = 0; s.nEnchants = 0;
        }
    }
    static int maxStackSize(int item, int meta) {
        if (item == WOODEN_PICKAXE || item == STONE_PICKAXE) return 1;
        if (item == ENCHANTED_BOOK) return 1;
        if (item == BUCKET || item == WATER_BUCKET || item == LAVA_BUCKET) return 1;
        return INV_LIMIT;
    }
    static boolean isStackable(Stack s) {
        return !isEmpty(s) && maxStackSize(s.item, s.meta) > 1;
    }
    static boolean stackMatch(Stack a, Stack b) {
        if (isEmpty(a) || isEmpty(b)) return false;
        if (a.item != b.item || a.meta != b.meta || a.nEnchants != b.nEnchants) return false;
        for (int e = 0; e < a.nEnchants; ++e)
            if (a.enchId[e] != b.enchId[e] || a.enchLvl[e] != b.enchLvl[e]) return false;
        return true;
    }

    Stack[] slots = new Stack[SLOTS];
    Stack cursor = empty();

    Golden() { for (int i = 0; i < SLOTS; ++i) slots[i] = empty(); }

    Stack split(Stack src, int amount) {
        int take = Math.min(amount, src.count);
        if (take <= 0 || isEmpty(src)) return empty();
        Stack out = mk(src.item, take, src.meta);
        out.nEnchants = src.nEnchants;
        for (int e = 0; e < src.nEnchants; ++e) {
            out.enchId[e] = src.enchId[e]; out.enchLvl[e] = src.enchLvl[e];
        }
        src.count -= take;
        normalize(src);
        return out;
    }
    Stack decrSlot(int index, int amount) { return split(slots[index], amount); }
    void grow(Stack s, int n) { s.count += n; normalize(s); }
    void shrink(Stack s, int n) { s.count -= n; normalize(s); }

    // mergeItemStack into all slots except exclude (synthetic transfer target).
    boolean mergeExcept(Stack stack, int exclude) {
        boolean flag = false;
        if (isStackable(stack)) {
            for (int i = 0; i < SLOTS && !isEmpty(stack); ++i) {
                if (i == exclude) continue;
                Stack slot = slots[i];
                if (isEmpty(slot) || !stackMatch(slot, stack)) continue;
                int j = slot.count + stack.count;
                int maxSize = Math.min(INV_LIMIT, maxStackSize(stack.item, stack.meta));
                if (j <= maxSize) {
                    stack.count = 0;
                    normalize(stack);
                    slot.count = j;
                    flag = true;
                } else if (slot.count < maxSize) {
                    shrink(stack, maxSize - slot.count);
                    slot.count = maxSize;
                    flag = true;
                }
            }
        }
        if (!isEmpty(stack)) {
            for (int i = 0; i < SLOTS; ++i) {
                if (i == exclude) continue;
                if (!isEmpty(slots[i])) continue;
                if (stack.count > INV_LIMIT) slots[i] = split(stack, INV_LIMIT);
                else slots[i] = split(stack, stack.count);
                flag = true;
                break;
            }
        }
        return flag;
    }

    // transferStackInSlot: merge source into others; return original if anything moved.
    Stack transferStackInSlot(int index) {
        if (index < 0 || index >= SLOTS) return empty();
        Stack slot = slots[index];
        if (isEmpty(slot)) return empty();
        Stack original = slot.copy();
        int before = slot.count;
        mergeExcept(slot, index);
        if (slot.count == before) return empty();
        return original;
    }

    void slotClick(int slotId, int dragType, int clickType) {
        if ((clickType == PICKUP || clickType == QUICK_MOVE) && (dragType == 0 || dragType == 1)) {
            if (slotId == -999) {
                if (!isEmpty(cursor)) {
                    if (dragType == 0) cursor = empty();
                    else split(cursor, 1);
                }
            } else if (clickType == QUICK_MOVE) {
                if (slotId < 0 || slotId >= SLOTS) return;
                for (;;) {
                    Stack moved = transferStackInSlot(slotId);
                    if (isEmpty(moved)) break;
                    if (isEmpty(slots[slotId]) || slots[slotId].item != moved.item) break;
                }
            } else {
                if (slotId < 0 || slotId >= SLOTS) return;
                Stack slot = slots[slotId];
                if (isEmpty(slot)) {
                    if (!isEmpty(cursor)) {
                        int l2 = dragType == 0 ? cursor.count : 1;
                        if (l2 > INV_LIMIT) l2 = INV_LIMIT;
                        slots[slotId] = split(cursor, l2);
                    }
                } else {
                    if (isEmpty(cursor)) {
                        int k2 = dragType == 0 ? slot.count : (slot.count + 1) / 2;
                        cursor = decrSlot(slotId, k2);
                    } else if (stackMatch(slot, cursor)) {
                        int j2 = dragType == 0 ? cursor.count : 1;
                        int lim = INV_LIMIT;
                        int maxs = maxStackSize(cursor.item, cursor.meta);
                        if (j2 > lim - slot.count) j2 = lim - slot.count;
                        if (j2 > maxs - slot.count) j2 = maxs - slot.count;
                        if (j2 > 0) {
                            shrink(cursor, j2);
                            grow(slot, j2);
                        }
                    } else if (cursor.count <= INV_LIMIT) {
                        Stack tmp = slot.copy();
                        slots[slotId] = cursor;
                        cursor = tmp;
                    }
                }
            }
        } else if (clickType == THROW && isEmpty(cursor) && slotId >= 0 && slotId < SLOTS) {
            Stack slot = slots[slotId];
            if (!isEmpty(slot)) {
                int n = dragType == 0 ? 1 : slot.count;
                decrSlot(slotId, n);
            }
        }
    }

    void setupStart() {
        for (int i = 0; i < SLOTS; ++i) slots[i] = empty();
        cursor = empty();
        slots[0] = mk(APPLE, 32, 0);
        slots[1] = mk(APPLE, 40, 0);
        slots[2] = mk(BREAD, 10, 0);
        slots[3] = empty();
        slots[4] = mk(STONE, 64, 0);
        slots[5] = mk(WOODEN_PICKAXE, 1, 0);
        slots[6] = mk(IRON_ORE, 16, 0);
        slots[7] = empty();
        slots[8] = mk(BREAD, 5, 0);
    }

    void setupEnchants() {
        for (int i = 0; i < SLOTS; ++i) slots[i] = empty();
        cursor = empty();
        slots[0] = mkBookMulti();
        slots[1] = mkBookSharp5();
        slots[2] = empty();
        slots[3] = empty();
        slots[4] = mkBookMulti();
        slots[5] = mk(APPLE, 16, 0);
        slots[6] = empty();
        slots[7] = empty();
        slots[8] = mk(BREAD, 4, 0);
    }

    static class Click {
        int slotId, button, type;
        Click(int s, int b, int t) { slotId = s; button = b; type = t; }
    }

    static Click[] tape() {
        return new Click[] {
            /* Phase 1: ordinary stacks */
            new Click(0, 0, PICKUP),
            new Click(3, 0, PICKUP),
            new Click(2, 1, PICKUP),
            new Click(7, 1, PICKUP),
            new Click(7, 0, PICKUP),
            new Click(8, 0, PICKUP),
            new Click(5, 0, PICKUP),
            new Click(6, 0, PICKUP),
            new Click(-999, 1, PICKUP),
            new Click(-999, 0, PICKUP),
            new Click(1, 0, PICKUP),
            new Click(0, 0, PICKUP),
            new Click(0, 0, QUICK_MOVE),
            new Click(4, 0, THROW),
            new Click(4, 1, THROW),
            new Click(8, 0, QUICK_MOVE),
            /* Phase 2: StoredEnchantments books */
            new Click(0, 0, PICKUP),
            new Click(2, 0, PICKUP),
            new Click(1, 0, PICKUP),
            new Click(2, 0, PICKUP),
            new Click(3, 0, PICKUP),
            new Click(4, 0, PICKUP),
            new Click(3, 0, PICKUP),
            new Click(6, 0, PICKUP),
            new Click(3, 0, PICKUP),
            new Click(-999, 0, PICKUP),
            new Click(2, 0, PICKUP),
            new Click(0, 0, PICKUP),
            new Click(0, 0, QUICK_MOVE),
            new Click(6, 0, THROW),
            new Click(5, 1, PICKUP),
            new Click(7, 1, PICKUP),
        };
    }

    void emitStack(StringBuilder sb, Stack s) {
        u(sb, s.item);
        u(sb, s.count);
        u(sb, s.meta);
        u(sb, s.nEnchants);
        for (int e = 0; e < MAX_ENCHANTS; ++e) {
            if (e < s.nEnchants) {
                u(sb, s.enchId[e]);
                u(sb, s.enchLvl[e]);
            } else {
                u(sb, 0);
                u(sb, 0);
            }
        }
    }
    void emit(StringBuilder sb) {
        for (int i = 0; i < SLOTS; ++i) emitStack(sb, slots[i]);
        emitStack(sb, cursor);
    }
    static void u(StringBuilder sb, int v) {
        sb.append(String.format("%08x", ((long) v) & 0xFFFFFFFFL)).append('\n');
    }

    public static void main(String[] args) {
        Golden g = new Golden();
        Click[] t = tape();
        StringBuilder sb = new StringBuilder();
        g.setupStart();
        for (int c = 0; c < PHASE1; ++c) {
            g.slotClick(t[c].slotId, t[c].button, t[c].type);
            g.emit(sb);
        }
        g.setupEnchants();
        for (int c = 0; c < PHASE2; ++c) {
            int idx = PHASE1 + c;
            g.slotClick(t[idx].slotId, t[idx].button, t[idx].type);
            g.emit(sb);
        }
        System.out.print(sb);
    }
}
