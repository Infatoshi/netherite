package netherite.oracle;

import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.tileentity.TileEntity;
import net.minecraft.tileentity.TileEntityChest;
import net.minecraft.tileentity.TileEntityDispenser;
import net.minecraft.tileentity.TileEntityFlowerPot;
import net.minecraft.tileentity.TileEntityFurnace;
import net.minecraft.tileentity.TileEntityMobSpawner;
import net.minecraft.world.WorldServer;
import net.minecraft.world.chunk.Chunk;
import net.minecraft.world.gen.feature.WorldGenDungeons;

/**
 * The dungeon feature's half of the feature probe (FeatureProbe.java): its
 * Config row, and the tile entity record the probe writes beside the block
 * writes.
 *
 * WorldGenDungeons is the first population feature that leaves tile entities:
 * the chests it fills from the dungeon loot table and the mob spawner it sets
 * at the room's centre. Those live outside the block arrays the probe's 3x3
 * hash and final.bin.gz cover, so the probe records them per case:
 *
 *   DIR/tileentities.bin, one record per case, the entities that case left
 *   different from the case before it, in (x, z, y) order:
 *
 *     uint32 count (LE)
 *     per entry: x int32 LE, y int32 LE, z int32 LE, kind uint8,
 *                len uint32 LE, len bytes of canonical NBT text
 *
 *   kind 0 is a position whose entity is gone, 1 a chest, 2 a mob spawner, 3 a
 *   furnace (a village house's), 4 a dispenser (the jungle temple's arrow
 *   traps, recorded by the structure block probe) and 5 a flower pot (the
 *   witch hut's). The text is the entity's
 *   writeToNBT as StructuresProbe.canon renders it, so a chest carries its
 *   Items list (each stack with its Slot), a spawner the MobSpawnerBaseLogic
 *   fields and its EntityId, a dispenser its nine Items slots and a flower pot
 *   its Item and Data. Only features whose Config row says tileEntities produce
 *   this file; every other feature's probe output is byte for byte what it was.
 */
final class FeatureProbeDungeons
{
    private FeatureProbeDungeons() {}

    static final String FEATURE = "dungeons";

    /**
     * The dungeon's row in FeatureProbe.FEATURES: WorldGenDungeons picks its own
     * spot (ChunkProviderGenerate calls it at rand.nextInt(256)), so the case
     * only says where to try. The band is the deep half of the world, where the
     * floor and ceiling the feature demands are actually solid rock; margin
     * covers its reach, four blocks out in x and z and one below to four above.
     */
    static FeatureProbe.Config row()
    {
        return new FeatureProbe.Config(FEATURE, 8, 64, 5, new WorldGenDungeons(), true);
    }

    /** One tile entity of the region, in the order the delta is written. */
    static final class Entry implements Comparable<Entry>
    {
        final int x, y, z, kind;
        final String text;

        Entry(int x, int y, int z, int kind, String text)
        {
            this.x = x;
            this.y = y;
            this.z = z;
            this.kind = kind;
            this.text = text;
        }

        public int compareTo(Entry o)
        {
            if (this.x != o.x) return this.x < o.x ? -1 : 1;
            if (this.z != o.z) return this.z < o.z ? -1 : 1;
            if (this.y != o.y) return this.y < o.y ? -1 : 1;
            return 0;
        }
    }

    /** The write side of the record: the region's entities after each case,
     * diffed against the case before. */
    static final class Tiles
    {
        private final OutputStream out;
        private List<Entry> prev = new ArrayList<Entry>();
        private int entries;

        Tiles(File dir) throws IOException
        {
            this.out = new BufferedOutputStream(new FileOutputStream(new File(dir, "tileentities.bin")), 1 << 16);
        }

        /** One case's record, taken right after its generate call. */
        void record(WorldServer ws, int x0, int x1, int z0, int z1) throws IOException
        {
            List<Entry> cur = snapshot(ws, x0, x1, z0, z1);
            List<Entry> delta = new ArrayList<Entry>();
            int i = 0, j = 0;

            while (i < this.prev.size() || j < cur.size())
            {
                Entry a = i < this.prev.size() ? this.prev.get(i) : null;
                Entry b = j < cur.size() ? cur.get(j) : null;
                int c = a == null ? 1 : b == null ? -1 : a.compareTo(b);

                if (c < 0)
                {
                    delta.add(new Entry(a.x, a.y, a.z, 0, ""));
                    ++i;
                }
                else if (c > 0)
                {
                    delta.add(b);
                    ++j;
                }
                else
                {
                    if (!a.text.equals(b.text)) delta.add(b);
                    ++i;
                    ++j;
                }
            }

            byte[] head = new byte[17];
            le32(head, 0, delta.size());
            this.out.write(head, 0, 4);

            for (Entry e : delta)
            {
                le32(head, 0, e.x);
                le32(head, 4, e.y);
                le32(head, 8, e.z);
                head[12] = (byte)e.kind;
                le32(head, 13, e.text.length());
                this.out.write(head);
                this.out.write(e.text.getBytes(StandardCharsets.UTF_8));
                ++this.entries;
            }

            this.prev = cur;
        }

        int entries()
        {
            return this.entries;
        }

        void close() throws IOException
        {
            this.out.close();
        }

        /** Every tile entity the loaded region holds, in (x, z, y) order. */
        private static List<Entry> snapshot(WorldServer ws, int x0, int x1, int z0, int z1)
        {
            List<Entry> out = new ArrayList<Entry>();

            for (int lx = x0; lx <= x1; ++lx)
            {
                for (int lz = z0; lz <= z1; ++lz)
                {
                    Chunk c = ws.getChunkFromChunkCoords(lx, lz);

                    for (Object o : c.chunkTileEntityMap.values())
                    {
                        TileEntity te = (TileEntity)o;
                        int kind = te instanceof TileEntityChest ? 1
                            : te instanceof TileEntityMobSpawner ? 2
                            : te instanceof TileEntityFurnace ? 3
                            : te instanceof TileEntityDispenser ? 4
                            : te instanceof TileEntityFlowerPot ? 5 : 0;

                        if (kind == 0)
                            throw new IllegalStateException("the probe region holds a " + te.getClass().getName()
                                + "; the record knows only the chest, the mob spawner, the furnace, the dispenser and the flower pot");

                        out.add(new Entry(te.field_145851_c, te.field_145848_d, te.field_145849_e, kind, canon(te)));
                    }
                }
            }

            Collections.sort(out);
            return out;
        }

        /** The entity's writeToNBT as canonical text (TileEntity.writeToNBT
         * writes into the compound it is handed and returns nothing). */
        private static String canon(TileEntity te)
        {
            NBTTagCompound t = new NBTTagCompound();
            te.writeToNBT(t);
            return StructuresProbe.canon(t).toString();
        }

        private static void le32(byte[] a, int o, int v)
        {
            a[o] = (byte)v;
            a[o + 1] = (byte)(v >> 8);
            a[o + 2] = (byte)(v >> 16);
            a[o + 3] = (byte)(v >> 24);
        }
    }

    /** What the manifest says about the file, for the native check. */
    static final String LAYOUT = "per case: count uint32 LE, then per entry x int32 LE, y int32 LE, z int32 LE, "
        + "kind uint8 (0 the entity is gone, 1 chest, 2 mob spawner, 3 furnace, 4 dispenser, 5 flower pot), len uint32 LE, "
        + "len bytes of the entity's writeToNBT as canonical NBT text; the entries are what the case left "
        + "different from the case before, sorted by x, then z, then y";

    static final String NOTE = "the tile entities the region holds after each case, diffed against the case before";
}