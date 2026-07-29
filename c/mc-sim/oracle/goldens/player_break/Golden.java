// Verbatim MC 1.11.2 progressive block break ground truth (vanilla formula), driven by the SAME
// scenario battery as core/player_break.h. Output: raw hex u64 lines (progress float bits, harvest).
// Sources (java/oracle-src/net/minecraft + forge):
//   server/management/PlayerInteractionManager.java onBlockClicked / updateBlockRemoving
//   block/Block.java getPlayerRelativeBlockHardness -> ForgeHooks.blockStrength
//   net/minecraftforge/common/ForgeHooks.java blockStrength (199-215)
//   entity/player/EntityPlayer.java getDigSpeed (887-942)
//   entity/player/InventoryPlayer.java getStrVsBlock / canHarvestBlock
//   item/ItemPickaxe.java getStrVsBlock / canHarvestBlock
//   item/Item.ToolMaterial efficiencyOnProperMaterial
//   block/material/Material.java ROCK requires tool
public class Golden {
    static final int NTICKS_DEFAULT = 160;
    static final int NSCENARIOS = 12;

    static final int HAND = 0;
    static final int WOODEN_PICKAXE = 270;
    static final int IRON_PICKAXE = 257;
    static final int BLK_STONE = 1;
    static final int BLK_DIRT = 3;
    static final int BLK_IRON_ORE = 15;
    static final int BLK_COAL_ORE = 16;
    static final int BLK_GOLD_ORE = 14;
    static final int BLK_DIAMOND_ORE = 56;
    static final int BLK_OBSIDIAN = 49;
    static final int BLK_COBBLESTONE = 4;
    static final int BLK_LAPIS_ORE = 21;

    static final class PbInput {
        int blockId, blockMeta, toolId, toolMeta;
        int efficiency, hasteAmp, fatigueAmp;
        int inWater, aquaAffinity, onGround, creative;
    }

    // block_props_table / Block.setHardness subset used by the battery
    static float hardness(int blockId) {
        switch (blockId) {
            case 1:  return 1.5F;  // stone
            case 3:  return 0.5F;  // dirt
            case 4:  return 2.0F;  // cobble
            case 14: return 3.0F;  // gold ore
            case 15: return 3.0F;  // iron ore
            case 16: return 3.0F;  // coal ore
            case 21: return 3.0F;  // lapis ore
            case 49: return 50.0F; // obsidian
            case 56: return 3.0F;  // diamond ore
            default: return 1.5F;
        }
    }

    static int toolMaterial(int toolId) {
        switch (toolId) {
            case 270: case 271: case 272: return 0; // wood
            case 274: case 275: case 273: return 1; // stone
            case 257: case 258: case 256: return 2; // iron
            case 278: case 279: case 277: return 3; // diamond
            case 285: case 286: case 284: return 4; // gold
            default: return -1;
        }
    }

    static boolean isPickaxe(int toolId) {
        return toolId == 270 || toolId == 274 || toolId == 257 || toolId == 278 || toolId == 285;
    }

    static int harvestLevel(int mat) {
        switch (mat) {
            case 0: return 0; // wood
            case 1: return 1; // stone
            case 2: return 2; // iron
            case 3: return 3; // diamond
            case 4: return 0; // gold
            default: return -1;
        }
    }

    // Item.ToolMaterial.getEfficiencyOnProperMaterial
    static float toolEfficiency(int mat) {
        switch (mat) {
            case 0: return 2.0F;
            case 1: return 4.0F;
            case 2: return 6.0F;
            case 3: return 8.0F;
            case 4: return 12.0F;
            default: return 1.0F;
        }
    }

    // Material.ROCK / ore requires tool
    static boolean requiresTool(int blockId) {
        return blockId == BLK_STONE || blockId == BLK_COBBLESTONE || blockId == BLK_IRON_ORE
            || blockId == BLK_COAL_ORE || blockId == BLK_GOLD_ORE || blockId == BLK_DIAMOND_ORE
            || blockId == BLK_OBSIDIAN || blockId == BLK_LAPIS_ORE
            || blockId == 73 || blockId == 74;
    }

    // ItemPickaxe.canHarvestBlock + harvest-level gates (ita_pickaxe_can_harvest mirror)
    static boolean pickaxeCanHarvest(int mat, int blockId) {
        int hl = harvestLevel(mat);
        if (blockId == BLK_OBSIDIAN) return hl == 3;
        if (blockId == BLK_DIAMOND_ORE || blockId == 57) return hl >= 2;
        if (blockId == BLK_GOLD_ORE || blockId == 41) return hl >= 2;
        if (blockId == BLK_IRON_ORE || blockId == 42) return hl >= 1;
        if (blockId == BLK_LAPIS_ORE || blockId == 21) return hl >= 1;
        if (blockId == 73 || blockId == 74) return hl >= 2;
        if (blockId == BLK_STONE || blockId == BLK_COBBLESTONE || blockId == BLK_COAL_ORE
            || blockId == BLK_IRON_ORE)
            return true;
        return false;
    }

    static boolean canHarvest(int toolId, int blockId) {
        if (!requiresTool(blockId)) return true;
        if (toolId == HAND) return false;
        if (!isPickaxe(toolId)) return false;
        int mat = toolMaterial(toolId);
        if (mat < 0) return false;
        return pickaxeCanHarvest(mat, blockId);
    }

    // pickaxe effective on rock/ore battery blocks (ita_tool_dig_speed / ItemPickaxe.getStrVsBlock)
    static boolean pickaxeEffective(int blockId) {
        if (blockId == BLK_OBSIDIAN || blockId == 73 || blockId == 74) return false;
        return blockId == BLK_STONE || blockId == BLK_COBBLESTONE || blockId == BLK_IRON_ORE
            || blockId == BLK_COAL_ORE || blockId == BLK_GOLD_ORE || blockId == BLK_DIAMOND_ORE
            || blockId == BLK_LAPIS_ORE;
    }

    // InventoryPlayer.getStrVsBlock / ItemPickaxe.getStrVsBlock
    static float strVsBlock(int toolId, int blockId) {
        if (toolId == HAND) return 1.0F;
        int mat = toolMaterial(toolId);
        if (mat < 0 || !isPickaxe(toolId)) return 1.0F;
        if (pickaxeEffective(blockId)) return toolEfficiency(mat);
        if (blockId == BLK_STONE || blockId == BLK_COBBLESTONE || blockId == BLK_IRON_ORE
            || blockId == BLK_COAL_ORE)
            return toolEfficiency(mat);
        return 1.0F;
    }

    // EntityPlayer.getDigSpeed (887-942)
    static float getDigSpeed(PbInput in) {
        float f = strVsBlock(in.toolId, in.blockId);
        if (f > 1.0F) {
            int i = in.efficiency;
            if (i > 0 && in.toolId != HAND)
                f += (float)(i * i + 1);
        }
        if (in.hasteAmp >= 0)
            f *= 1.0F + (float)(in.hasteAmp + 1) * 0.2F;
        if (in.fatigueAmp >= 0) {
            float f1;
            switch (in.fatigueAmp) {
                case 0:  f1 = 0.3F; break;
                case 1:  f1 = 0.09F; break;
                case 2:  f1 = 0.0027F; break;
                default: f1 = 8.1E-4F; break;
            }
            f *= f1;
        }
        if (in.inWater != 0 && in.aquaAffinity == 0)
            f /= 5.0F;
        if (in.onGround == 0)
            f /= 5.0F;
        return f < 0.0F ? 0.0F : f;
    }

    // ForgeHooks.blockStrength (199-215)
    static float relativeHardness(PbInput in) {
        float h = hardness(in.blockId);
        if (h < 0.0F) return 0.0F;
        float dig = getDigSpeed(in);
        if (!canHarvest(in.toolId, in.blockId))
            return dig / h / 100.0F;
        return dig / h / 30.0F;
    }

    static PbInput scenario(int si) {
        PbInput in = new PbInput();
        in.blockId = BLK_STONE;
        in.blockMeta = 0;
        in.toolId = HAND;
        in.toolMeta = 0;
        in.efficiency = 0;
        in.hasteAmp = -1;
        in.fatigueAmp = -1;
        in.inWater = 0;
        in.aquaAffinity = 0;
        in.onGround = 1;
        in.creative = 0;
        switch (si) {
            case 0: break; // hand vs stone
            case 1: in.toolId = WOODEN_PICKAXE; break;
            case 2: in.toolId = IRON_PICKAXE; break;
            case 3: in.blockId = BLK_DIRT; break;
            case 4: in.toolId = WOODEN_PICKAXE; in.blockId = BLK_IRON_ORE; break;
            case 5: in.toolId = IRON_PICKAXE; in.inWater = 1; in.aquaAffinity = 0; break;
            case 6: in.toolId = IRON_PICKAXE; in.onGround = 0; break;
            case 7: in.toolId = IRON_PICKAXE; in.hasteAmp = 0; break;
            case 8: in.toolId = IRON_PICKAXE; in.hasteAmp = 1; break;
            case 9: in.toolId = IRON_PICKAXE; in.fatigueAmp = 0; break;
            case 10: in.toolId = IRON_PICKAXE; in.fatigueAmp = 1; break;
            case 11: in.creative = 1; break;
            default: break;
        }
        return in;
    }

    static void tick(PbInput in, float[] progress, int[] harvested) {
        if (harvested[0] != 0) return;
        if (in.creative != 0) {
            progress[0] = 1.0F;
            harvested[0] = 1;
            return;
        }
        progress[0] += relativeHardness(in);
        if (progress[0] >= 1.0F)
            harvested[0] = 1;
    }

    static String hex64(long v) {
        String s = Long.toHexString(v);
        if (s.length() >= 16) return s;
        StringBuilder sb = new StringBuilder(16);
        for (int i = s.length(); i < 16; ++i) sb.append('0');
        sb.append(s);
        return sb.toString();
    }

    public static void main(String[] args) {
        int nticks = args.length > 0 ? Integer.parseInt(args[0]) : NTICKS_DEFAULT;
        if (nticks < 1) nticks = NTICKS_DEFAULT;
        StringBuilder sb = new StringBuilder();
        for (int si = 0; si < NSCENARIOS; ++si) {
            PbInput in = scenario(si);
            float[] progress = new float[] { 0.0F };
            int[] harvested = new int[] { 0 };
            for (int t = 0; t < nticks; ++t) {
                tick(in, progress, harvested);
                int bits = Float.floatToRawIntBits(progress[0]);
                sb.append(hex64(bits & 0xffffffffL)).append('\n');
                sb.append(hex64(harvested[0] & 0xffffffffL)).append('\n');
            }
        }
        System.out.print(sb.toString());
    }
}
