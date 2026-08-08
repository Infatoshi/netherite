// Verbatim-modeled MC 1.11.2 interaction-block meta state machine (eval-pure, no game launch).
//
// Logic from decompiled oracle:
//   block/BlockDoor.java              onBlockActivated / neighborChanged OPEN+POWERED bits
//   block/BlockTrapDoor.java          onBlockActivated / neighborChanged OPEN bit
//   block/BlockFenceGate.java         onBlockActivated facing flip + OPEN; neighbor OPEN+POWERED
//   block/BlockLever.java             onBlockActivated cycle POWERED
//   block/BlockButton.java            onBlockActivated set POWERED; updateTick/checkPressed clear
//   block/BlockPressurePlate.java     setRedstoneStrength POWERED meta 0/1
//   block/BlockPressurePlateWeighted  power = ceil(min(n,maxW)/maxW * 15)
//
// Pure SM: one packed state u16 = id<<4|meta per case. No world, TE, sound, or multi-block pairing.
// Output: 48 cases * 4 u32 (packed_out, is_open, is_powered, accepted) as %08x.
// Beds/chests/hoppers/redstone dust OUT.
public class Golden {
    static final int WOODEN_DOOR = 64, LEVER = 69, STONE_PLATE = 70, IRON_DOOR = 71,
        WOODEN_PLATE = 72, STONE_BUTTON = 77, TRAPDOOR = 96, FENCE_GATE = 107,
        WOODEN_BUTTON = 143, LIGHT_PLATE = 147, HEAVY_PLATE = 148, IRON_TRAPDOOR = 167;
    static final int ACT_CLICK = 0, ACT_NEIGHBOR = 1, ACT_ENTITY = 2, ACT_RELEASE = 3;
    static final int H_SOUTH = 0, H_WEST = 1, H_NORTH = 2, H_EAST = 3;
    static final int NCASES = 48, FIELDS = 4;

    static final class Case {
        int blockId, metaIn, action, arg0;
        Case(int id, int meta, int act, int a0) {
            blockId = id; metaIn = meta; action = act; arg0 = a0;
        }
    }

    static final class Result {
        int metaOut, isOpen, isPowered, accepted;
    }

    static int pack(int id, int meta) {
        return ((id & 0xFFF) << 4) | (meta & 15);
    }

    static boolean isWoodDoor(int id) { return id == WOODEN_DOOR; }
    static boolean isIronDoor(int id) { return id == IRON_DOOR; }
    static boolean isDoor(int id) { return isWoodDoor(id) || isIronDoor(id); }
    static boolean isWoodTrap(int id) { return id == TRAPDOOR; }
    static boolean isIronTrap(int id) { return id == IRON_TRAPDOOR; }
    static boolean isTrap(int id) { return isWoodTrap(id) || isIronTrap(id); }
    static boolean isGate(int id) { return id == FENCE_GATE; }
    static boolean isLever(int id) { return id == LEVER; }
    static boolean isStoneBtn(int id) { return id == STONE_BUTTON; }
    static boolean isWoodBtn(int id) { return id == WOODEN_BUTTON; }
    static boolean isBtn(int id) { return isStoneBtn(id) || isWoodBtn(id); }
    static boolean isBinPlate(int id) { return id == STONE_PLATE || id == WOODEN_PLATE; }
    static boolean isWeighted(int id) { return id == LIGHT_PLATE || id == HEAVY_PLATE; }

    static int plateMaxWeight(int id) {
        if (id == LIGHT_PLATE) return 15;
        if (id == HEAVY_PLATE) return 150;
        return 0;
    }

    // MathHelper.ceil(float)
    static int ceilF(float v) {
        int i = (int)v;
        return v > (float)i ? i + 1 : i;
    }

    static int weightedStrength(int count, int maxW) {
        if (count < 0) count = 0;
        if (maxW <= 0) return 0;
        int i = count < maxW ? count : maxW;
        if (i <= 0) return 0;
        float f = (float)i / (float)maxW;
        return ceilF(f * 15.0f);
    }

    static boolean doorIsUpper(int meta) { return (meta & 8) != 0; }
    static boolean doorOpen(int meta) { return !doorIsUpper(meta) && (meta & 4) != 0; }
    static boolean doorPowered(int meta) { return doorIsUpper(meta) && (meta & 2) != 0; }

    static void fillFlags(int id, int meta, Result r) {
        r.isOpen = 0;
        r.isPowered = 0;
        if (isDoor(id)) {
            r.isOpen = doorOpen(meta) ? 1 : 0;
            r.isPowered = doorPowered(meta) ? 1 : 0;
        } else if (isTrap(id)) {
            r.isOpen = ((meta & 4) != 0) ? 1 : 0;
            r.isPowered = r.isOpen;
        } else if (isGate(id)) {
            r.isOpen = ((meta & 4) != 0) ? 1 : 0;
            r.isPowered = ((meta & 8) != 0) ? 1 : 0;
        } else if (isLever(id)) {
            r.isPowered = ((meta & 8) != 0) ? 1 : 0;
        } else if (isBtn(id)) {
            r.isPowered = ((meta & 8) != 0) ? 1 : 0;
        } else if (isBinPlate(id)) {
            r.isPowered = (meta == 1) ? 1 : 0;
        } else if (isWeighted(id)) {
            r.isPowered = (meta > 0) ? 1 : 0;
        }
    }

    static Result doorClick(int id, int meta) {
        Result r = new Result();
        r.metaOut = meta & 15;
        r.accepted = 0;
        if (isIronDoor(id)) {
            fillFlags(id, r.metaOut, r);
            return r;
        }
        if (doorIsUpper(meta)) {
            fillFlags(id, r.metaOut, r);
            r.accepted = 0;
            return r;
        }
        r.metaOut = (meta & 15) ^ 4;
        r.accepted = 1;
        fillFlags(id, r.metaOut, r);
        return r;
    }

    static Result doorNeighbor(int id, int meta, int powered) {
        Result r = new Result();
        int m = meta & 15;
        int flag = powered != 0 ? 1 : 0;
        r.accepted = 1;
        if (doorIsUpper(m)) {
            if (flag != 0) m = m | 2;
            else m = m & ~2;
        } else {
            if (flag != 0) m = m | 4;
            else m = m & ~4;
        }
        r.metaOut = m & 15;
        fillFlags(id, r.metaOut, r);
        return r;
    }

    static Result trapClick(int id, int meta) {
        Result r = new Result();
        r.metaOut = meta & 15;
        if (isIronTrap(id)) {
            r.accepted = 0;
            fillFlags(id, r.metaOut, r);
            return r;
        }
        r.metaOut = (meta & 15) ^ 4;
        r.accepted = 1;
        fillFlags(id, r.metaOut, r);
        return r;
    }

    static Result trapNeighbor(int id, int meta, int powered) {
        Result r = new Result();
        int m = meta & 15;
        if (powered != 0) m = m | 4;
        else m = m & ~4;
        r.metaOut = m;
        r.accepted = 1;
        fillFlags(id, r.metaOut, r);
        return r;
    }

    static Result gateClick(int id, int meta, int playerH) {
        Result r = new Result();
        int m = meta & 15;
        int facing = m & 3;
        boolean open = (m & 4) != 0;
        boolean powered = (m & 8) != 0;
        playerH &= 3;
        if (open) {
            open = false;
        } else {
            if (facing == ((playerH + 2) & 3))
                facing = playerH;
            open = true;
        }
        m = (facing & 3) | (open ? 4 : 0) | (powered ? 8 : 0);
        r.metaOut = m;
        r.accepted = 1;
        fillFlags(id, r.metaOut, r);
        return r;
    }

    static Result gateNeighbor(int id, int meta, int powered) {
        Result r = new Result();
        int m = meta & 15;
        int flag = powered != 0 ? 1 : 0;
        boolean curP = (m & 8) != 0;
        if (curP != (flag != 0)) {
            int facing = m & 3;
            m = (facing & 3) | (flag != 0 ? 4 : 0) | (flag != 0 ? 8 : 0);
        }
        r.metaOut = m & 15;
        r.accepted = 1;
        fillFlags(id, r.metaOut, r);
        return r;
    }

    static Result leverClick(int id, int meta) {
        Result r = new Result();
        r.metaOut = (meta & 15) ^ 8;
        r.accepted = 1;
        fillFlags(id, r.metaOut, r);
        return r;
    }

    static Result btnClick(int id, int meta) {
        Result r = new Result();
        int m = meta & 15;
        r.accepted = 1;
        if ((m & 8) == 0) m = m | 8;
        r.metaOut = m;
        fillFlags(id, r.metaOut, r);
        return r;
    }

    static Result btnRelease(int id, int meta, int entityPresent) {
        Result r = new Result();
        int m = meta & 15;
        r.accepted = 1;
        if ((m & 8) != 0) {
            if (isWoodBtn(id)) {
                if (entityPresent == 0) m = m & ~8;
            } else {
                m = m & ~8;
            }
        }
        r.metaOut = m;
        fillFlags(id, r.metaOut, r);
        return r;
    }

    static Result btnEntity(int id, int meta, int present) {
        Result r = new Result();
        int m = meta & 15;
        r.accepted = 1;
        if (isWoodBtn(id) && present != 0 && (m & 8) == 0) m = m | 8;
        r.metaOut = m;
        fillFlags(id, r.metaOut, r);
        return r;
    }

    static Result binPlateSet(int id, int meta, int count) {
        Result r = new Result();
        r.metaOut = count > 0 ? 1 : 0;
        r.accepted = 1;
        fillFlags(id, r.metaOut, r);
        return r;
    }

    static Result wPlateSet(int id, int meta, int count) {
        Result r = new Result();
        r.metaOut = weightedStrength(count, plateMaxWeight(id)) & 15;
        r.accepted = 1;
        fillFlags(id, r.metaOut, r);
        return r;
    }

    static Result apply(Case c) {
        int id = c.blockId;
        int meta = c.metaIn & 15;
        int act = c.action;
        int a0 = c.arg0;
        Result r = new Result();
        r.metaOut = meta;
        r.isOpen = 0;
        r.isPowered = 0;
        r.accepted = 0;

        if (isDoor(id)) {
            if (act == ACT_CLICK) return doorClick(id, meta);
            if (act == ACT_NEIGHBOR) return doorNeighbor(id, meta, a0);
        } else if (isTrap(id)) {
            if (act == ACT_CLICK) return trapClick(id, meta);
            if (act == ACT_NEIGHBOR) return trapNeighbor(id, meta, a0);
        } else if (isGate(id)) {
            if (act == ACT_CLICK) return gateClick(id, meta, a0);
            if (act == ACT_NEIGHBOR) return gateNeighbor(id, meta, a0);
        } else if (isLever(id)) {
            if (act == ACT_CLICK) return leverClick(id, meta);
        } else if (isBtn(id)) {
            if (act == ACT_CLICK) return btnClick(id, meta);
            if (act == ACT_RELEASE) return btnRelease(id, meta, a0);
            if (act == ACT_ENTITY) return btnEntity(id, meta, a0);
        } else if (isBinPlate(id)) {
            if (act == ACT_ENTITY || act == ACT_RELEASE) return binPlateSet(id, meta, a0);
        } else if (isWeighted(id)) {
            if (act == ACT_ENTITY || act == ACT_RELEASE) return wPlateSet(id, meta, a0);
        }
        fillFlags(id, r.metaOut, r);
        return r;
    }

    static Case[] getCases() {
        Case[] t = new Case[NCASES];
        int i = 0;
        t[i++] = new Case(WOODEN_DOOR, 0, ACT_CLICK, 0);
        t[i++] = new Case(WOODEN_DOOR, 4, ACT_CLICK, 0);
        t[i++] = new Case(WOODEN_DOOR, 1, ACT_CLICK, 0);
        t[i++] = new Case(IRON_DOOR, 0, ACT_CLICK, 0);
        t[i++] = new Case(WOODEN_DOOR, 0, ACT_NEIGHBOR, 1);
        t[i++] = new Case(WOODEN_DOOR, 4, ACT_NEIGHBOR, 0);
        t[i++] = new Case(IRON_DOOR, 0, ACT_NEIGHBOR, 1);
        t[i++] = new Case(WOODEN_DOOR, 8, ACT_NEIGHBOR, 1);
        t[i++] = new Case(WOODEN_DOOR, 10, ACT_NEIGHBOR, 0);
        t[i++] = new Case(WOODEN_DOOR, 8, ACT_CLICK, 0);

        t[i++] = new Case(TRAPDOOR, 0, ACT_CLICK, 0);
        t[i++] = new Case(TRAPDOOR, 4, ACT_CLICK, 0);
        t[i++] = new Case(TRAPDOOR, 8, ACT_CLICK, 0);
        t[i++] = new Case(TRAPDOOR, 3, ACT_CLICK, 0);
        t[i++] = new Case(IRON_TRAPDOOR, 0, ACT_CLICK, 0);
        t[i++] = new Case(TRAPDOOR, 0, ACT_NEIGHBOR, 1);
        t[i++] = new Case(TRAPDOOR, 4, ACT_NEIGHBOR, 0);
        t[i++] = new Case(IRON_TRAPDOOR, 0, ACT_NEIGHBOR, 1);

        t[i++] = new Case(FENCE_GATE, 0, ACT_CLICK, H_SOUTH);
        t[i++] = new Case(FENCE_GATE, 4, ACT_CLICK, H_SOUTH);
        t[i++] = new Case(FENCE_GATE, 0, ACT_CLICK, H_NORTH);
        t[i++] = new Case(FENCE_GATE, 2, ACT_CLICK, H_SOUTH);
        t[i++] = new Case(FENCE_GATE, 1, ACT_CLICK, H_WEST);
        t[i++] = new Case(FENCE_GATE, 0, ACT_NEIGHBOR, 1);
        t[i++] = new Case(FENCE_GATE, 12, ACT_NEIGHBOR, 0);
        t[i++] = new Case(FENCE_GATE, 4, ACT_NEIGHBOR, 1);

        t[i++] = new Case(LEVER, 0, ACT_CLICK, 0);
        t[i++] = new Case(LEVER, 8, ACT_CLICK, 0);
        t[i++] = new Case(LEVER, 1, ACT_CLICK, 0);
        t[i++] = new Case(LEVER, 2, ACT_CLICK, 0);
        t[i++] = new Case(LEVER, 3, ACT_CLICK, 0);
        t[i++] = new Case(LEVER, 4, ACT_CLICK, 0);
        t[i++] = new Case(LEVER, 5, ACT_CLICK, 0);
        t[i++] = new Case(LEVER, 6, ACT_CLICK, 0);
        t[i++] = new Case(LEVER, 7, ACT_CLICK, 0);

        t[i++] = new Case(STONE_BUTTON, 5, ACT_CLICK, 0);
        t[i++] = new Case(STONE_BUTTON, 13, ACT_CLICK, 0);
        t[i++] = new Case(STONE_BUTTON, 13, ACT_RELEASE, 0);
        t[i++] = new Case(WOODEN_BUTTON, 1, ACT_CLICK, 0);
        t[i++] = new Case(WOODEN_BUTTON, 9, ACT_RELEASE, 0);
        t[i++] = new Case(WOODEN_BUTTON, 9, ACT_RELEASE, 1);
        t[i++] = new Case(WOODEN_BUTTON, 1, ACT_ENTITY, 1);

        t[i++] = new Case(STONE_PLATE, 0, ACT_ENTITY, 1);
        t[i++] = new Case(STONE_PLATE, 1, ACT_ENTITY, 0);
        t[i++] = new Case(WOODEN_PLATE, 0, ACT_ENTITY, 3);

        t[i++] = new Case(LIGHT_PLATE, 0, ACT_ENTITY, 0);
        t[i++] = new Case(LIGHT_PLATE, 0, ACT_ENTITY, 1);
        t[i++] = new Case(HEAVY_PLATE, 0, ACT_ENTITY, 15);
        if (i != NCASES) throw new RuntimeException("case count " + i);
        return t;
    }

    public static void main(String[] args) {
        Case[] cases = getCases();
        for (int c = 0; c < NCASES; ++c) {
            Result r = apply(cases[c]);
            System.out.printf("%08x%n", pack(cases[c].blockId, r.metaOut));
            System.out.printf("%08x%n", r.isOpen);
            System.out.printf("%08x%n", r.isPowered);
            System.out.printf("%08x%n", r.accepted);
        }
    }
}
