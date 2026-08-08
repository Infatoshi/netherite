package qrl;

import java.io.File;
import java.io.PrintWriter;
import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import net.minecraft.block.Block;
import net.minecraft.block.state.IBlockState;
import net.minecraft.init.Blocks;
import net.minecraft.init.Bootstrap;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.tileentity.TileEntityStructure;
import net.minecraft.util.Mirror;
import net.minecraft.util.ResourceLocation;
import net.minecraft.util.Rotation;
import net.minecraft.util.datafix.DataFixer;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.gen.structure.template.Template;
import net.minecraft.world.gen.structure.template.TemplateManager;

/** Developer tool: convert owned vanilla mansion templates to compact C data. */
public final class MansionTemplateDump {
    private static final String[] NAMES = {
        "1x1_a1", "1x1_a2", "1x1_a3", "1x1_a4", "1x1_a5",
        "1x1_as1", "1x1_as2", "1x1_as3", "1x1_as4",
        "1x1_b1", "1x1_b2", "1x1_b3", "1x1_b4", "1x1_b5",
        "1x2_a1", "1x2_a2", "1x2_a3", "1x2_a4", "1x2_a5",
        "1x2_a6", "1x2_a7", "1x2_a8", "1x2_a9",
        "1x2_b1", "1x2_b2", "1x2_b3", "1x2_b4", "1x2_b5",
        "1x2_c1", "1x2_c2", "1x2_c3", "1x2_c4", "1x2_c_stairs",
        "1x2_d1", "1x2_d2", "1x2_d3", "1x2_d4", "1x2_d5",
        "1x2_d_stairs", "1x2_s1", "1x2_s2", "1x2_se1",
        "2x2_a1", "2x2_a2", "2x2_a3", "2x2_a4",
        "2x2_b1", "2x2_b2", "2x2_b3", "2x2_b4", "2x2_b5", "2x2_s1",
        "carpet_east", "carpet_north", "carpet_south", "carpet_south_2",
        "carpet_west", "carpet_west_2", "corridor_floor", "entrance",
        "indoors_door", "indoors_door_2", "indoors_wall", "indoors_wall_2",
        "roof", "roof_corner", "roof_front", "roof_inner_corner",
        "small_wall", "small_wall_corner", "wall_corner", "wall_flat",
        "wall_window"
    };

    private static final class StateKey {
        final int id;
        final int meta;

        StateKey(IBlockState state) {
            id = Block.getIdFromBlock(state.getBlock());
            meta = state.getBlock().getMetaFromState(state);
        }

        public int hashCode() { return id * 31 + meta; }
        public boolean equals(Object other) {
            if (!(other instanceof StateKey)) return false;
            StateKey key = (StateKey)other;
            return id == key.id && meta == key.meta;
        }
    }

    private static final class BlockOut {
        final int x, y, z, state;
        BlockOut(BlockPos pos, int stateIn) {
            x = pos.getX(); y = pos.getY(); z = pos.getZ(); state = stateIn;
        }
    }

    private static final class MarkerOut {
        final int x, y, z, kind;
        MarkerOut(BlockPos pos, int kindIn) {
            x = pos.getX(); y = pos.getY(); z = pos.getZ(); kind = kindIn;
        }
    }

    private static int markerKind(String metadata) {
        if ("ChestWest".equals(metadata)) return 1;
        if ("ChestEast".equals(metadata)) return 2;
        if ("ChestSouth".equals(metadata)) return 3;
        if ("ChestNorth".equals(metadata)) return 4;
        if ("Mage".equals(metadata)) return 5;
        if ("Warrior".equals(metadata)) return 6;
        return 0;
    }

    @SuppressWarnings("unchecked")
    public static void main(String[] args) throws Exception {
        if (args.length != 1) throw new IllegalArgumentException("output header required");
        Bootstrap.register();
        TemplateManager manager = new TemplateManager("", new DataFixer(922));
        Field blocksField = Template.class.getDeclaredField("blocks");
        blocksField.setAccessible(true);
        Map<StateKey, Integer> stateIndices = new LinkedHashMap<StateKey, Integer>();
        List<IBlockState> states = new ArrayList<IBlockState>();
        List<List<BlockOut>> allBlocks = new ArrayList<List<BlockOut>>();
        List<List<MarkerOut>> allMarkers = new ArrayList<List<MarkerOut>>();
        List<BlockPos> sizes = new ArrayList<BlockPos>();

        for (String name : NAMES) {
            Template template = manager.getTemplate(null,
                new ResourceLocation("mansion/" + name));
            sizes.add(template.getSize());
            List<BlockOut> blocks = new ArrayList<BlockOut>();
            List<MarkerOut> markers = new ArrayList<MarkerOut>();
            for (Template.BlockInfo info :
                    (List<Template.BlockInfo>)blocksField.get(template)) {
                NBTTagCompound tag = info.tileentityData;
                if (info.blockState.getBlock() == Blocks.STRUCTURE_BLOCK && tag != null
                        && TileEntityStructure.Mode.DATA.name().equals(tag.getString("mode"))) {
                    int kind = markerKind(tag.getString("metadata"));
                    if (kind == 0)
                        throw new IllegalStateException("unknown marker " + tag);
                    markers.add(new MarkerOut(info.pos, kind));
                    continue;
                }
                StateKey key = new StateKey(info.blockState);
                Integer index = stateIndices.get(key);
                if (index == null) {
                    index = states.size();
                    stateIndices.put(key, index);
                    states.add(info.blockState);
                }
                blocks.add(new BlockOut(info.pos, index));
            }
            allBlocks.add(blocks);
            allMarkers.add(markers);
        }
        if (states.size() > 256)
            throw new IllegalStateException("too many mansion states: " + states.size());

        File output = new File(args[0]);
        output.getParentFile().mkdirs();
        PrintWriter out = new PrintWriter(output, "UTF-8");
        out.println("/* GENERATED by qrl.MansionTemplateDump from owned MC 1.11.2 assets. */");
        out.println("#ifndef MAGMA_MANSION_TEMPLATES_H");
        out.println("#define MAGMA_MANSION_TEMPLATES_H\n");
        out.println("typedef struct { unsigned char x,y,z,state; } GmMansionBlock;");
        out.println("typedef struct { unsigned char x,y,z,kind; } GmMansionMarker;");
        out.println("typedef struct { unsigned char id,meta[12]; } GmMansionState;");
        out.println("typedef struct { const char *name; unsigned char sx,sy,sz; "
            + "const GmMansionBlock *blocks; unsigned short block_count; "
            + "const GmMansionMarker *markers; unsigned char marker_count; } "
            + "GmMansionTemplate;\n");
        out.println("static const GmMansionState GM_MANSION_STATES[] = {");
        for (IBlockState state : states) {
            int id = Block.getIdFromBlock(state.getBlock());
            out.print("{" + id + ",{ ");
            boolean first = true;
            for (Mirror mirror : Mirror.values()) {
                for (Rotation rotation : Rotation.values()) {
                    IBlockState transformed = state.withMirror(mirror).withRotation(rotation);
                    if (transformed.getBlock() != state.getBlock())
                        throw new IllegalStateException("transform changed block " + state);
                    int meta = transformed.getBlock().getMetaFromState(transformed);
                    if (!first) out.print(',');
                    first = false;
                    out.print(meta);
                }
            }
            out.println("}},");
        }
        out.println("};\n");
        for (int i = 0; i < NAMES.length; ++i) {
            List<BlockOut> blocks = allBlocks.get(i);
            out.println("static const GmMansionBlock GM_MANSION_BLOCKS_" + i
                + "[" + blocks.size() + "] = {");
            for (BlockOut block : blocks)
                out.print("{" + block.x + ',' + block.y + ',' + block.z + ','
                    + block.state + "},");
            out.println("\n};");
            List<MarkerOut> markers = allMarkers.get(i);
            if (!markers.isEmpty()) {
                out.print("static const GmMansionMarker GM_MANSION_MARKERS_" + i
                    + "[" + markers.size() + "] = {");
                for (MarkerOut marker : markers)
                    out.print("{" + marker.x + ',' + marker.y + ',' + marker.z
                        + ',' + marker.kind + "},");
                out.println("};");
            }
        }
        out.println("\n#define GM_MANSION_TEMPLATE_COUNT " + NAMES.length);
        out.println("static const GmMansionTemplate "
            + "GM_MANSION_TEMPLATES[GM_MANSION_TEMPLATE_COUNT] = {");
        for (int i = 0; i < NAMES.length; ++i) {
            BlockPos size = sizes.get(i);
            String markerRef = allMarkers.get(i).isEmpty() ? "0"
                : "GM_MANSION_MARKERS_" + i;
            out.println("{\"" + NAMES[i] + "\"," + size.getX() + ','
                + size.getY() + ',' + size.getZ() + ",GM_MANSION_BLOCKS_" + i
                + ',' + allBlocks.get(i).size() + ',' + markerRef + ','
                + allMarkers.get(i).size() + "},");
        }
        out.println("};\n\n#endif");
        out.close();
        int blockCount = 0, markerCount = 0;
        for (List<BlockOut> blocks : allBlocks) blockCount += blocks.size();
        for (List<MarkerOut> markers : allMarkers) markerCount += markers.size();
        System.out.println("mansion templates: " + NAMES.length + " templates, "
            + states.size() + " states, " + blockCount + " blocks, "
            + markerCount + " markers");
        System.out.println("wrote: " + output.getCanonicalPath());
    }
}
