package netheritemod;

/**
 * Pure tape-format helpers for authoritative container identity.
 *
 * Contract (human tapes, new recordings):
 * <ul>
 *   <li>{@code gopen}: {@code [gui, wid, ctype, x, y, z, slots, cur]} or
 *       furnace with trailing {@code [burn,current,cook,total]}.
 *       Player inventory omits pos/slots/cur: {@code [gui, 0, "player"]}.</li>
 *   <li>{@code gclk}: {@code [gui, slot, button, typeOrd, typeName, wid]}
 *       (legacy 5-tuple GuiInventory-only still accepted by replay).</li>
 *   <li>{@code gclose}: {@code [wid, gui]}.</li>
 * </ul>
 *
 * Replayable ctypes: {@code player}, {@code workbench}, {@code furnace},
 * {@code chest}. Everything else (double_chest, enchant, brewing, ...) is
 * recorded when observed but fails closed at replay.
 *
 * No Minecraft types: unit-tested via {@link #main}.
 */
public final class ContainerTapeFormat {
    public static final String CTYPE_PLAYER = "player";
    public static final String CTYPE_WORKBENCH = "workbench";
    public static final String CTYPE_FURNACE = "furnace";
    public static final String CTYPE_CHEST = "chest";
    public static final String CTYPE_DOUBLE_CHEST = "double_chest";
    public static final String CTYPE_UNSUPPORTED = "unsupported";

    /** Single-chest ContainerChest slot list length (27 + 27 + 9). */
    public static final int CHEST_SINGLE_SLOTS = 63;
    /** Double-chest ContainerChest slot list length (54 + 27 + 9). */
    public static final int CHEST_DOUBLE_SLOTS = 90;

    private ContainerTapeFormat() {}

    /** Map Gui* simple name (+ optional slot count) to tape ctype. */
    public static String ctypeForGui(String gui, int slotCount) {
        if (gui == null) return CTYPE_UNSUPPORTED;
        if ("GuiInventory".equals(gui)) return CTYPE_PLAYER;
        if ("GuiCrafting".equals(gui)) return CTYPE_WORKBENCH;
        if ("GuiFurnace".equals(gui)) return CTYPE_FURNACE;
        if ("GuiChest".equals(gui)) {
            if (slotCount > CHEST_SINGLE_SLOTS) return CTYPE_DOUBLE_CHEST;
            return CTYPE_CHEST;
        }
        return CTYPE_UNSUPPORTED;
    }

    public static boolean isReplayableCtype(String ctype) {
        return CTYPE_PLAYER.equals(ctype)
                || CTYPE_WORKBENCH.equals(ctype)
                || CTYPE_FURNACE.equals(ctype)
                || CTYPE_CHEST.equals(ctype);
    }

    /** Expected block id at world pos for ctype, or -1 if none. */
    public static int expectedBlockId(String ctype) {
        if (CTYPE_WORKBENCH.equals(ctype)) return 58;
        if (CTYPE_FURNACE.equals(ctype)) return 61; /* lit 62 also ok at check site */
        if (CTYPE_CHEST.equals(ctype) || CTYPE_DOUBLE_CHEST.equals(ctype)) return 54;
        return -1;
    }

    public static boolean furnaceBlockOk(int blockId) {
        return blockId == 61 || blockId == 62;
    }

    public static String formatGclk(String gui, int slotId, int mouseButton,
            int typeOrd, String typeName, int windowId) {
        String g = gui != null ? gui : "";
        String n = typeName != null ? typeName : "";
        return "[\"" + g + "\"," + slotId + "," + mouseButton + ","
                + typeOrd + ",\"" + n + "\"," + windowId + "]";
    }

    public static String formatGclose(int windowId, String gui) {
        String g = gui != null ? gui : "";
        return "[" + windowId + ",\"" + g + "\"]";
    }

    /**
     * Player open: {@code ["GuiInventory",0,"player"]}.
     * Block open: {@code [gui,wid,ctype,x,y,z,slotsJson,curJson]} (+ optional prop).
     * slotsJson/curJson are pre-serialized JSON fragments (array or 0).
     */
    public static String formatGopenPlayer(String gui, int windowId) {
        String g = gui != null ? gui : "GuiInventory";
        return "[\"" + g + "\"," + windowId + ",\"" + CTYPE_PLAYER + "\"]";
    }

    public static String formatGopenBlock(String gui, int windowId, String ctype,
            int x, int y, int z, String slotsJson, String curJson, String propJson) {
        String g = gui != null ? gui : "";
        String c = ctype != null ? ctype : CTYPE_UNSUPPORTED;
        StringBuilder b = new StringBuilder(96);
        b.append("[\"").append(g).append("\",")
         .append(windowId).append(",\"")
         .append(c).append("\",")
         .append(x).append(',').append(y).append(',').append(z).append(',')
         .append(slotsJson != null ? slotsJson : "[]").append(',')
         .append(curJson != null ? curJson : "0");
        if (propJson != null && !propJson.isEmpty()) {
            b.append(',').append(propJson);
        }
        b.append(']');
        return b.toString();
    }

    /** Self-test entry (no JUnit). Exit 0 on success. */
    public static void main(String[] args) {
        assertEq(ctypeForGui("GuiInventory", 46), CTYPE_PLAYER);
        assertEq(ctypeForGui("GuiCrafting", 46), CTYPE_WORKBENCH);
        assertEq(ctypeForGui("GuiFurnace", 39), CTYPE_FURNACE);
        assertEq(ctypeForGui("GuiChest", CHEST_SINGLE_SLOTS), CTYPE_CHEST);
        assertEq(ctypeForGui("GuiChest", CHEST_DOUBLE_SLOTS), CTYPE_DOUBLE_CHEST);
        assertEq(ctypeForGui("GuiEnchantment", 0), CTYPE_UNSUPPORTED);
        assertTrue(isReplayableCtype(CTYPE_PLAYER));
        assertTrue(isReplayableCtype(CTYPE_WORKBENCH));
        assertTrue(isReplayableCtype(CTYPE_FURNACE));
        assertTrue(isReplayableCtype(CTYPE_CHEST));
        assertTrue(!isReplayableCtype(CTYPE_DOUBLE_CHEST));
        assertTrue(!isReplayableCtype(CTYPE_UNSUPPORTED));
        assertEq(expectedBlockId(CTYPE_WORKBENCH), 58);
        assertTrue(furnaceBlockOk(61) && furnaceBlockOk(62) && !furnaceBlockOk(54));
        assertEq(formatGclk("GuiInventory", 36, 0, 0, "PICKUP", 0),
                "[\"GuiInventory\",36,0,0,\"PICKUP\",0]");
        assertEq(formatGclose(1, "GuiChest"), "[1,\"GuiChest\"]");
        assertEq(formatGopenPlayer("GuiInventory", 0),
                "[\"GuiInventory\",0,\"player\"]");
        assertEq(formatGopenBlock("GuiChest", 2, CTYPE_CHEST, 1, 64, 3,
                        "[[264,0,1],0]", "0", null),
                "[\"GuiChest\",2,\"chest\",1,64,3,[[264,0,1],0],0]");
        assertEq(formatGopenBlock("GuiFurnace", 3, CTYPE_FURNACE, 0, 70, 0,
                        "[0,0,0]", "0", "[10,200,5,200]"),
                "[\"GuiFurnace\",3,\"furnace\",0,70,0,[0,0,0],0,[10,200,5,200]]");
        System.out.println("ContainerTapeFormat self-test: ok");
    }

    private static void assertEq(Object a, Object b) {
        if (a == null ? b != null : !a.equals(b)) {
            throw new AssertionError("expected " + b + " got " + a);
        }
    }

    private static void assertTrue(boolean v) {
        if (!v) throw new AssertionError("expected true");
    }
}
