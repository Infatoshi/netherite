package netherite.oracle;

import com.google.gson.JsonObject;
import java.io.BufferedOutputStream;
import java.io.DataOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.lang.reflect.Field;
import java.nio.charset.Charset;
import java.util.ArrayList;
import java.util.List;
import java.util.TreeMap;
import net.minecraft.block.Block;
import net.minecraft.block.BlockDoublePlant;
import net.minecraft.block.BlockGrass;
import net.minecraft.block.BlockSlab;
import net.minecraft.world.biome.BiomeGenBase;
import net.minecraft.world.biome.BiomeGenMutated;
import net.minecraft.client.Minecraft;
import net.minecraft.init.Blocks;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.tileentity.TileEntity;
import net.minecraft.util.IIcon;
import net.minecraft.world.ColorizerFoliage;
import net.minecraft.world.ColorizerGrass;
import net.minecraft.world.IBlockAccess;

/**
 * The renderer's lookup tables, read out of the live oracle rather than
 * transcribed: every registered block's texture for each (side, meta), the
 * render properties the mesher branches on, the block bounds each
 * setBlockBoundsBasedOnState leaves behind, and the grass and foliage
 * colormaps the client loaded from its resources.
 *
 * Written as DIR/table.bin (layout fixed and generated below, and echoed in
 * DIR/table.json) so the native mesher can look up a texture by name and an
 * icon's atlas entry (MeshProbe's atlas.json) without a texture pack or a
 * colormap file of its own.
 *
 * Also a runnable oracle probe (`make run CLASS=RenderTable NAME=... 
 * CMD='{"out":"/abs/dir"}'`) so the table can be regenerated on its own;
 * MeshProbe writes it into its own directory as part of a recording, which is
 * what the native mesh check reads.
 */
public final class RenderTable
{
    /** the ids the native port's 12-bit block array can hold */
    static final int IDS = 4096;
    /** metas a getIcon(side, meta) lookup covers */
    static final int METAS = 16;
    /** sides a getIcon(side, meta) lookup covers */
    static final int SIDES = 6;
    /** no icon at this (id, meta, side) */
    static final int NO_ICON = 0xffff;

    private RenderTable() {}

    static JsonObject run(IntegratedServer server, JsonObject cmd) throws Exception
    {
        return write(Minecraft.getMinecraft(), new File(cmd.get("out").getAsString()));
    }

    static JsonObject write(Minecraft mc, File dir) throws Exception
    {
        dir.mkdirs();

        // ---- icons: (id, meta, side) -> name. The names are written in
        // ascending order, so an index is a name's position in that order.
        String[] slotName = new String[IDS * METAS * SIDES];
        java.util.TreeSet<String> nameSet = new java.util.TreeSet<String>();
        int iconErrors = 0;

        for (int id = 0; id < IDS; ++id)
        {
            if (!Block.blockRegistry.containsID(id)) continue;
            Block b = Block.getBlockById(id);

            for (int meta = 0; meta < METAS; ++meta)
            {
                for (int side = 0; side < SIDES; ++side)
                {
                    String name = null;

                    try
                    {
                        IIcon ic = b.getIcon(side, meta);
                        if (ic != null) name = ic.getIconName();
                    }
                    catch (Throwable t)
                    {
                        ++iconErrors;
                    }

                    if (name == null) continue;
                    slotName[(id * METAS + meta) * SIDES + side] = name;
                    nameSet.add(name);
                }
            }
        }

        List<String> names = new ArrayList<String>(nameSet);
        int[] index = new int[IDS * METAS * SIDES];
        java.util.Arrays.fill(index, NO_ICON);

        for (int i = 0; i < slotName.length; ++i)
        {
            if (slotName[i] != null) index[i] = java.util.Collections.binarySearch(names, slotName[i]);
        }

        // ---- per-block render properties
        Props[] props = new Props[IDS];

        for (int id = 0; id < IDS; ++id)
        {
            if (!Block.blockRegistry.containsID(id)) continue;
            Block b = Block.getBlockById(id);
            props[id] = new Props(b);
        }

        // ---- bounds after setBlockBoundsBasedOnState, per (id, meta)
        double[] bounds = new double[IDS * METAS * 6];
        Fake fake = new Fake();

        for (int id = 0; id < IDS; ++id)
        {
            if (props[id] == null) continue;
            Block b = Block.getBlockById(id);

            for (int meta = 0; meta < METAS; ++meta)
            {
                fake.meta = meta;
                int o = (id * METAS + meta) * 6;

                try
                {
                    b.setBlockBoundsBasedOnState(fake, 0, 0, 0);
                }
                catch (Throwable t)
                {
                    // a block whose bounds need a real world keeps its construction bounds
                }

                bounds[o] = b.getBlockBoundsMinX();
                bounds[o + 1] = b.getBlockBoundsMinY();
                bounds[o + 2] = b.getBlockBoundsMinZ();
                bounds[o + 3] = b.getBlockBoundsMaxX();
                bounds[o + 4] = b.getBlockBoundsMaxY();
                bounds[o + 5] = b.getBlockBoundsMaxZ();
            }
        }

        // ---- the colormaps the reload listeners put into the colorizers
        int[] grass = (int[])field(ColorizerGrass.class, "grassBuffer").get(null);
        int[] foliage = (int[])field(ColorizerFoliage.class, "foliageBuffer").get(null);

        // ---- the icons the mesher reaches for by name rather than through
        // getIcon(side, meta): the fancy grass overlay and snowed side, and the
        // double plant's lower, upper and flower icons.
        java.util.List<String> special = new ArrayList<String>();
        special.add(iconName(beneath(Blocks.grass, "field_149993_M")));       // grass_side_snowed
        special.add(iconName(beneath(Blocks.grass, "field_149994_N")));       // grass_side_overlay
        IIcon[] dpLow = (IIcon[])beneath(Blocks.double_plant, "field_149893_M");
        IIcon[] dpHigh = (IIcon[])beneath(Blocks.double_plant, "field_149894_N");
        IIcon[] dpFlower = (IIcon[])beneath(Blocks.double_plant, "field_149891_b");

        int dpn = dpLow == null ? 0 : dpLow.length;
        int dph = dpHigh == null ? 0 : dpHigh.length;
        for (int i = 0; i < dpn; ++i) special.add(iconName(dpLow[i]));
        for (int i = 0; i < dph; ++i) special.add(iconName(dpHigh[i]));
        for (int i = 0; i < 2; ++i) special.add(iconName(dpFlower == null || i >= dpFlower.length ? null : dpFlower[i]));

        for (int i = 0; i < special.size(); ++i)
        {
            if (special.get(i).isEmpty()) throw new IllegalStateException("special render icon " + i + " is missing");
        }

        // ---- the biome color table: for each of the 256 slots, the biome the
        // oracle resolves at mesh time (func_150568_d, plains when the slot holds
        // no object) and how its grass and foliage colors are computed, worked out
        // by testing the live method against the candidates and refused when none
        // of them matches.
        BiomeRow[] rows = new BiomeRow[256];

        for (int i = 0; i < 256; ++i)
        {
            BiomeGenBase b = BiomeGenBase.func_150568_d(i);
            rows[i] = new BiomeRow(b == null ? BiomeGenBase.plains : b, i);
        }

        for (int i = 0; i < 256; ++i)
        {
            BiomeGenBase b = rows[i].biome;
            if (b instanceof BiomeGenMutated) continue;
            rows[i].grass = classify(b, rows[i], true);
            rows[i].foliage = classify(b, rows[i], false);
            verify(rows, i, true);
            verify(rows, i, false);
        }

        for (int i = 0; i < 256; ++i)
        {
            BiomeGenBase b = rows[i].biome;
            if (!(b instanceof BiomeGenMutated)) continue;
            BiomeGenBase base = (BiomeGenBase)beneath(b, "field_150611_aD");
            if (base == null) throw new IllegalStateException("biome " + i + " is a mutation with no base");
            rows[i].baseSlot = base.biomeID;
            rows[i].grass = new int[] {KIND_MUTATED, 0};
            rows[i].foliage = new int[] {KIND_MUTATED, 0};
            verify(rows, i, true);
            verify(rows, i, false);
        }

        // ---- write table.bin
        DataOutputStream out = new DataOutputStream(new BufferedOutputStream(new FileOutputStream(new File(dir, "table.bin")), 1 << 16));
        writeIntLE(out, 0x3142544d); // "MTB1"
        writeIntLE(out, 1);          // version
        writeIntLE(out, IDS);
        writeIntLE(out, METAS);
        writeIntLE(out, SIDES);
        writeIntLE(out, names.size());
        writeIntLE(out, special.size());

        for (String name : names)
        {
            byte[] utf = name.getBytes(Charset.forName("UTF-8"));
            writeShortLE(out, utf.length);
            out.write(utf);
        }

        for (String name : special)
        {
            byte[] utf = name.getBytes(Charset.forName("UTF-8"));
            writeShortLE(out, utf.length);
            out.write(utf);
        }

        for (int i = 0; i < index.length; ++i) writeShortLE(out, index[i]);
        for (int i = 0; i < 65536; ++i) writeIntLE(out, grass[i]);
        for (int i = 0; i < 65536; ++i) writeIntLE(out, foliage[i]);

        for (int id = 0; id < IDS; ++id)
        {
            Props p = props[id];
            out.write(p == null ? 0 : p.canBlockGrass);
            out.write(p == null ? 0 : p.renderAsNormalBlock);
            out.write(p == null ? 0 : p.neighborBrightness);
            out.write(p == null ? 0 : p.renderBlockPass);
            out.write(p == null ? 0 : p.opaqueCube);
            out.write(p == null ? 0 : p.isSlab);
            out.write(p == null ? 0 : p.lightValue);
            out.write(0);
            writeIntLE(out, Float.floatToRawIntBits(p == null ? 1.0F : p.aoLightValue));
        }

        for (int i = 0; i < bounds.length; ++i) writeLongLE(out, Double.doubleToRawLongBits(bounds[i]));

        for (int i = 0; i < 256; ++i)
        {
            BiomeRow r = rows[i];
            writeIntLE(out, Float.floatToRawIntBits(r.biome.temperature));
            writeIntLE(out, Float.floatToRawIntBits(r.biome.rainfall));
            writeIntLE(out, r.biome.waterColorMultiplier);
            writeIntLE(out, r.grass[1]);
            writeIntLE(out, r.foliage[1]);
            out.write(r.grass[0]);
            out.write(r.foliage[0]);
            writeShortLE(out, r.baseSlot);
            for (int k = 0; k < 8; ++k) out.write(0);
        }

        // ---- MathHelper.SIN_TABLE: the mesher's sin and cos are table lookups
        float[] sinTable = (float[])field(net.minecraft.util.MathHelper.class, "SIN_TABLE").get(null);

        for (int i = 0; i < 65536; ++i) writeIntLE(out, Float.floatToRawIntBits(sinTable[i]));
        out.close();

        JsonObject r = new JsonObject();
        r.addProperty("file", "table.bin");
        r.addProperty("icons", names.size());
        r.addProperty("special_icons", "16 names: grass_side_snowed (BlockGrass field_149993_M), grass_side_overlay (field_149994_N), the 6 double plant lower icons field_149893_M[0..5], the 6 upper icons field_149894_N[0..5] (both BlockDoublePlant.field_149892_a.length), then its flower icons field_149891_b[0..1]");
        r.addProperty("icon_errors", iconErrors);
        r.addProperty("atlas", "atlas.json maps each icon name here to minU/maxU/minV/maxV as raw float bits");
        r.addProperty("layout",
            "little endian: int32 magic 0x3142544d (\"MTB1\"), int32 version 1, int32 ids, int32 metas, int32 sides, int32 iconCount, int32 specialCount; "
            + "icon names (each uint16 length then UTF-8 bytes, ascending name order); "
            + "special icon names (each uint16 length then UTF-8 bytes, fixed order above); "
            + "iconIndex ids*metas*sides uint16 LE (index into the name list, 0xffff when getIcon(side, meta) threw or was null); "
            + "grassColormap 65536 uint32 LE (ColorizerGrass.grassBuffer, index (1 - humidity * temperature) * 255 << 8 | (1 - temperature) * 255); "
            + "foliageColormap 65536 uint32 LE (ColorizerFoliage.foliageBuffer); "
            + "renderProps ids records of {uint8 canBlockGrass, uint8 renderAsNormalBlock, uint8 neighborBrightness (Block.func_149710_n), uint8 renderBlockPass, "
            + "uint8 isOpaqueCube, uint8 isBlockSlab, uint8 lightValue, uint8 pad, int32 aoLightValue float bits}; "
            + "bounds ids*metas*6 float64 LE (minX, minY, minZ, maxX, maxY, maxZ after setBlockBoundsBasedOnState with a world of air); "
            + "biomeRows 256 records of {float32 temperature, float32 rainfall, int32 waterColorMultiplier, int32 grassConst, int32 foliageConst, uint8 grassKind, uint8 foliageKind, int16 baseSlot, 8 pad}; "
            + "kind 0 is ColorizerGrass/Foliage.getX(color of getFloatTemperature(x, y, z) clamped to 0..1, rainfall clamped to 0..1), 1 is grass ((map & 0xFEFEFE) + 0x28340A) >> 1, "
            + "2 is grass perlin(seed 2345, x * 0.0225, z * 0.0225) < -0.1 ? 5011004 : 6975545, 3 is the constant, 4 is the baseSlot's kind at (x, y, y); "
            + "the slot is the raw biome byte (& 255) of the chunk's biome array, already carrying the func_150568_d null-to-plains fallback; "
            + "sineTable 65536 float32 LE (MathHelper.SIN_TABLE, the table MathHelper.sin and MathHelper.cos index); "
            + "an id with no registered block has zeroed props and bounds.");
        PrintWriter pw = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "table.json")), Charset.forName("UTF-8")));
        pw.println(r.toString());
        pw.close();
        return r;
    }

    // ---- the biome color kinds the native mesher reproduces
    static final int KIND_MAP = 0;      // ColorizerGrass/Foliage at getFloatTemperature(x, y, z)
    static final int KIND_ROOFED = 1;   // grass: ((map & 0xFEFEFE) + 0x28340A) >> 1
    static final int KIND_SWAMP = 2;    // grass: perlin(seed 2345, x * 0.0225, z * 0.0225) < -0.1 ? 5011004 : 6975545
    static final int KIND_CONST = 3;    // a fixed color
    static final int KIND_MUTATED = 4;  // delegate to the base biome with (x, y, y)

    /** The points every classification is checked at: below and above y 64, so the height term shows. */
    static final int[][] POINTS = {{0, 40, 0}, {8, 64, -8}, {100, 70, 64}, {-1234, 90, 5678}, {9, 65, 9}, {32000, 200, -16000}};

    private static final class BiomeRow
    {
        final BiomeGenBase biome;
        int[] grass = {KIND_MAP, 0}, foliage = {KIND_MAP, 0};
        int baseSlot = -1;
        int slot;

        BiomeRow(BiomeGenBase b, int slot)
        {
            this.biome = b;
            this.slot = slot;
        }
    }

    private static float clamp01(float f)
    {
        return f < 0.0F ? 0.0F : (f > 1.0F ? 1.0F : f);
    }

    /** One slot at one point, the way the native mesher computes it; a mutation delegates to its base with z := y. */
    private static int evaluate(BiomeRow[] rows, int slot, boolean grass, int x, int y, int z) throws Exception
    {
        BiomeRow r = rows[slot];
        int[] k = grass ? r.grass : r.foliage;
        if (k[0] == KIND_MUTATED) return evaluate(rows, r.baseSlot, grass, x, y, y);
        return byKind(r, k, grass, x, y, z);
    }

    private static int byKind(BiomeRow r, int[] k, boolean grass, int x, int y, int z) throws Exception
    {
        if (k[0] == KIND_CONST) return k[1];

        if (k[0] == KIND_SWAMP)
        {
            double d = perlinB((double)x * 0.0225D, (double)z * 0.0225D);
            return d < -0.1D ? 5011004 : 6975545;
        }

        float t = clamp01(r.biome.getFloatTemperature(x, y, z));
        float h = clamp01(r.biome.getFloatRainfall());

        if (grass)
        {
            int c = ColorizerGrass.getGrassColor((double)t, (double)h);
            return k[0] == KIND_ROOFED ? ((c & 16711422) + 2634762) >> 1 : c;
        }

        return ColorizerFoliage.getFoliageColor((double)t, (double)h);
    }

    private static double perlinB(double x, double z) throws Exception
    {
        Object p = field(BiomeGenBase.class, "field_150606_ad").get(null);
        java.lang.reflect.Method m = p.getClass().getDeclaredMethod("func_151601_a", double.class, double.class);
        m.setAccessible(true);
        return ((Double)m.invoke(p, Double.valueOf(x), Double.valueOf(z))).doubleValue();
    }

    private static int liveColor(BiomeGenBase b, boolean grass, int[] p)
    {
        return grass ? b.getBiomeGrassColor(p[0], p[1], p[2]) : b.getBiomeFoliageColor(p[0], p[1], p[2]);
    }

    /** Which kind the live method runs, by testing every candidate at every point. */
    private static int[] classify(BiomeGenBase b, BiomeRow row, boolean grass) throws Exception
    {
        int[] kinds = grass ? new int[] {KIND_MAP, KIND_ROOFED, KIND_SWAMP, KIND_CONST} : new int[] {KIND_MAP, KIND_CONST};

        for (int kind : kinds)
        {
            int[] cand = {kind, 0};
            boolean ok = true;

            for (int[] p : POINTS)
            {
                int want = liveColor(b, grass, p);
                int got = kind == KIND_CONST ? want : byKind(row, cand, grass, p[0], p[1], p[2]);

                if (got != want)
                {
                    ok = false;
                    break;
                }
            }

            if (ok) return kind == KIND_CONST ? new int[] {KIND_CONST, liveColor(b, grass, POINTS[0])} : new int[] {kind, 0};
        }

        throw new IllegalStateException("slot " + row.slot + ": biome " + b.biomeName + " (" + b.biomeID + ") "
            + (grass ? "grass" : "foliage") + " color matches no known kind");
    }

    /** Refuse a row whose kind does not reproduce the live method at every point. */
    private static void verify(BiomeRow[] rows, int slot, boolean grass) throws Exception
    {
        BiomeGenBase b = rows[slot].biome;

        for (int[] p : POINTS)
        {
            int want = liveColor(b, grass, p);
            int got = evaluate(rows, slot, grass, p[0], p[1], p[2]);

            if (want != got)
            {
                StringBuilder sb = new StringBuilder("slot " + slot + ": biome " + b.biomeName + " (" + b.biomeID + ") "
                    + (grass ? "grass" : "foliage") + " at " + p[0] + "," + p[1] + "," + p[2] + ": live " + want + " kind " + got);
                BiomeGenBase base = (BiomeGenBase)beneath(b, "field_150611_aD");
                sb.append("; baseSlot " + rows[slot].baseSlot + " baseID " + (base == null ? -1 : base.biomeID)
                    + " baseSame " + (base != null && rows[slot].baseSlot >= 0 && rows[rows[slot].baseSlot].biome == base)
                    + " temp " + b.getFloatTemperature(p[0], p[1], p[2])
                    + " tempBase " + (base == null ? 0.0F : base.getFloatTemperature(p[0], p[1], p[2]))
                    + " tempSwapped " + b.getFloatTemperature(p[0], p[1], p[1])
                    + " rain " + b.getFloatRainfall() + " rowsKinds " + rows[slot].grass[0] + "/" + rows[slot].foliage[0]
                    + " baseKinds " + (rows[slot].baseSlot >= 0 ? rows[rows[slot].baseSlot].grass[0] + "/" + rows[rows[slot].baseSlot].foliage[0] : "-"));
                throw new IllegalStateException(sb.toString());
            }
        }
    }


    private static String iconName(Object o)
    {
        IIcon ic = (IIcon)o;
        return ic == null || ic.getIconName() == null ? "" : ic.getIconName();
    }

    /** An instance field of a live block object, or null when this oracle does not have it. */
    private static Object beneath(Object o, String n)
    {
        for (Class<?> c = o.getClass(); c != null; c = c.getSuperclass())
        {
            try
            {
                Field f = c.getDeclaredField(n);
                f.setAccessible(true);
                return f.get(o);
            }
            catch (NoSuchFieldException e)
            {
                continue;
            }
            catch (Exception e)
            {
                return null;
            }
        }

        return null;
    }

    private static void writeIntLE(DataOutputStream out, int v) throws Exception
    {
        out.write(v & 0xff);
        out.write(v >>> 8 & 0xff);
        out.write(v >>> 16 & 0xff);
        out.write(v >>> 24 & 0xff);
    }

    private static void writeShortLE(DataOutputStream out, int v) throws Exception
    {
        out.write(v & 0xff);
        out.write(v >>> 8 & 0xff);
    }

    private static void writeLongLE(DataOutputStream out, long v) throws Exception
    {
        for (int i = 0; i < 8; ++i) out.write((int)(v >>> (8 * i)) & 0xff);
    }

    private static Field field(Class<?> c, String name) throws Exception
    {
        Field f = c.getDeclaredField(name);
        f.setAccessible(true);
        return f;
    }

    /** One registered block's render properties, read from the live object. */
    private static class Props
    {
        int canBlockGrass, renderAsNormalBlock, neighborBrightness, renderBlockPass, opaqueCube, isSlab, lightValue;
        float aoLightValue;

        Props(Block b)
        {
            this.canBlockGrass = b.getCanBlockGrass() ? 1 : 0;
            this.renderAsNormalBlock = b.renderAsNormalBlock() ? 1 : 0;
            this.neighborBrightness = b.func_149710_n() ? 1 : 0;
            this.renderBlockPass = b.getRenderBlockPass();
            this.opaqueCube = b.isOpaqueCube() ? 1 : 0;
            this.isSlab = b instanceof BlockSlab ? 1 : 0;
            this.lightValue = b.getLightValue();
            this.aoLightValue = b.getAmbientOcclusionLightValue();
        }
    }

    /** A world of air at y=0, for the bounds pass of setBlockBoundsBasedOnState. */
    private static final class Fake implements IBlockAccess
    {
        int meta;

        public Block getBlock(int x, int y, int z)
        {
            return Blocks.air;
        }

        public TileEntity getTileEntity(int x, int y, int z)
        {
            return null;
        }

        public int getLightBrightnessForSkyBlocks(int x, int y, int z, int lightValue)
        {
            return 0;
        }

        public int getBlockMetadata(int x, int y, int z)
        {
            return this.meta;
        }

        public boolean isAirBlock(int x, int y, int z)
        {
            return true;
        }

        public BiomeGenBase getBiomeGenForCoords(int x, int z)
        {
            return BiomeGenBase.plains;
        }

        public int getHeight()
        {
            return 256;
        }

        public boolean extendedLevelsInChunkCache()
        {
            return true;
        }

        public int isBlockProvidingPowerTo(int x, int y, int z, int direction)
        {
            return 0;
        }
    }
}