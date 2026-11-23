package netherite.oracle;

import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import java.io.File;
import java.lang.reflect.Field;
import java.nio.file.Files;
import java.util.Random;
import java.util.concurrent.atomic.AtomicLong;
import net.minecraft.block.Block;
import net.minecraft.block.BlockBrewingStand;
import net.minecraft.block.BlockChest;
import net.minecraft.block.BlockDispenser;
import net.minecraft.block.BlockFurnace;
import net.minecraft.block.BlockHopper;
import net.minecraft.init.Blocks;

/**
 * The block singletons' own Randoms, one per Block instance, born at
 * bootstrap from the seeder and drawn only by their breakBlock inventory
 * spill: BlockChest.field_149955_b (chest, trapped chest),
 * BlockFurnace.field_149933_a (furnace, lit furnace),
 * BlockDispenser.field_149942_b (dispenser, dropper), BlockHopper.field_149922_a
 * and BlockBrewingStand.field_149961_a. A snapshot's det.nbt records each
 * group under its key (state, and gauss when a Gaussian is cached); a run
 * started from a checkpoint sets them back from the checkpoint's pre/det.nbt,
 * so a fresh JVM continues the stream the saving run left.
 */
final class BlockRands
{
    private BlockRands() {}

    static final String[] KEYS = { "furnace", "chest", "dispenser", "hopper", "brewing" };

    static Block[] blocks(String key)
    {
        if (key.equals("furnace")) return new Block[] { Blocks.furnace, Blocks.lit_furnace };
        if (key.equals("chest")) return new Block[] { Blocks.chest, Blocks.trapped_chest };
        if (key.equals("dispenser")) return new Block[] { Blocks.dispenser, Blocks.dropper };
        if (key.equals("hopper")) return new Block[] { Blocks.hopper };
        return new Block[] { Blocks.brewing_stand };
    }

    static Field field(String key) throws Exception
    {
        Field f;
        if (key.equals("furnace")) f = BlockFurnace.class.getDeclaredField("field_149933_a");
        else if (key.equals("chest")) f = BlockChest.class.getDeclaredField("field_149955_b");
        else if (key.equals("dispenser")) f = BlockDispenser.class.getDeclaredField("field_149942_b");
        else if (key.equals("hopper")) f = BlockHopper.class.getDeclaredField("field_149922_a");
        else f = BlockBrewingStand.class.getDeclaredField("field_149961_a");
        f.setAccessible(true);
        return f;
    }

    static Field randomField(String name) throws Exception
    {
        Field f = Random.class.getDeclaredField(name);
        f.setAccessible(true);
        return f;
    }

    static void write(JsonObject o)
    {
        try
        {
            Field have = randomField("haveNextNextGaussian"), next = randomField("nextNextGaussian");
            for (String key : KEYS)
            {
                Field rf = field(key);
                JsonArray a = new JsonArray();
                for (Block b : blocks(key))
                {
                    Random r = (Random)rf.get(b);
                    JsonObject e = new JsonObject();
                    e.addProperty("state", "l:" + Det.state(r));
                    if (have.getBoolean(r)) e.addProperty("gauss", String.format("d:%016x", Double.doubleToRawLongBits(next.getDouble(r))));
                    a.add(e);
                }
                o.add(key, a);
            }
        }
        catch (Exception x) { throw new IllegalStateException(x); }
    }

    /** Sets every recorded block Random from a snapshot's det.nbt; a key the file lacks keeps its state. */
    static void restore(File detNbt)
    {
        if (!detNbt.isFile()) return;
        try
        {
            JsonObject d = Rows.parse(new String(Files.readAllBytes(detNbt.toPath()), "UTF-8"));
            Field seed = randomField("seed"), have = randomField("haveNextNextGaussian"), next = randomField("nextNextGaussian");
            for (String key : KEYS)
            {
                if (!d.has(key)) continue;
                JsonArray a = d.getAsJsonArray(key);
                Field rf = field(key);
                Block[] bs = blocks(key);
                for (int i = 0; i < bs.length && i < a.size(); ++i)
                {
                    JsonObject e = a.get(i).getAsJsonObject();
                    Random r = (Random)rf.get(bs[i]);
                    ((AtomicLong)seed.get(r)).set(Long.parseLong(e.get("state").getAsString().substring(2)));
                    JsonElement g = e.get("gauss");
                    have.setBoolean(r, g != null);
                    next.setDouble(r, g == null ? 0.0 : Double.longBitsToDouble(Long.parseUnsignedLong(g.getAsString().substring(2), 16)));
                }
            }
        }
        catch (Exception x) { throw new IllegalStateException("block Randoms from " + detNbt, x); }
    }
}
