package qrl;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import com.google.gson.JsonPrimitive;
import java.lang.reflect.Field;
import java.util.LinkedList;
import java.util.List;
import java.util.Random;
import java.util.concurrent.atomic.AtomicLong;
import net.minecraft.block.Block;
import net.minecraft.entity.Entity;
import net.minecraft.entity.item.EntityMinecartChest;
import net.minecraft.init.Blocks;
import net.minecraft.inventory.InventoryBasic;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.nbt.NBTTagList;
import net.minecraft.util.EnumFacing;
import net.minecraft.util.math.AxisAlignedBB;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.WorldServer;
import net.minecraft.world.gen.structure.StructureBoundingBox;
import net.minecraft.world.gen.structure.StructureComponent;
import net.minecraft.world.gen.structure.StructureMineshaftPieces;
import net.minecraft.world.storage.loot.LootContext;
import net.minecraft.world.storage.loot.LootTable;
import net.minecraft.world.storage.loot.LootTableList;

/** Direct, render-free oracle for the real 1.11.2 mineshaft piece tree. */
final class MineshaftOracle {
    private MineshaftOracle() {}

    private static final class BuiltStart {
        final List<StructureComponent> pieces;
        final StructureBoundingBox total;
        BuiltStart(List<StructureComponent> pieces, StructureBoundingBox total) {
            this.pieces = pieces;
            this.total = total;
        }
    }

    private static Object field(Object owner, String name) throws Exception {
        Field f = owner.getClass().getDeclaredField(name);
        f.setAccessible(true);
        return f.get(owner);
    }

    private static long cursor(Random rand) throws Exception {
        Field f = Random.class.getDeclaredField("seed");
        f.setAccessible(true);
        return ((AtomicLong)f.get(rand)).get();
    }

    static JsonObject loot(WorldServer world, long seed) {
        JsonObject out = new JsonObject();
        try {
            InventoryBasic inventory = new InventoryBasic("mineshaft", false, 27);
            LootTable table = world.getLootTableManager().getLootTableFromLocation(
                LootTableList.CHESTS_ABANDONED_MINESHAFT);
            table.fillInventory(inventory, new Random(seed),
                new LootContext.Builder(world).build());
            JsonArray values = new JsonArray();
            int nonempty = 0;
            for (int slot = 0; slot < 27; ++slot) {
                ItemStack stack = inventory.getStackInSlot(slot);
                int item = stack.isEmpty() ? 0 : Item.getIdFromItem(stack.getItem());
                int count = stack.isEmpty() ? 0 : stack.getCount();
                int meta = stack.isEmpty() ? 0 : stack.getItemDamage();
                int n = 0, id0 = 0, level0 = 0, id1 = 0, level1 = 0;
                if (!stack.isEmpty() && stack.hasTagCompound()) {
                    NBTTagList enchants = stack.getTagCompound().getTagList(
                        "StoredEnchantments", 10);
                    n = enchants.tagCount();
                    if (n > 0) {
                        NBTTagCompound e = enchants.getCompoundTagAt(0);
                        id0 = e.getShort("id"); level0 = e.getShort("lvl");
                    }
                    if (n > 1) {
                        NBTTagCompound e = enchants.getCompoundTagAt(1);
                        id1 = e.getShort("id"); level1 = e.getShort("lvl");
                    }
                }
                values.add(new JsonPrimitive(item));
                values.add(new JsonPrimitive(count));
                values.add(new JsonPrimitive(meta));
                values.add(new JsonPrimitive(n));
                values.add(new JsonPrimitive(id0));
                values.add(new JsonPrimitive(level0));
                values.add(new JsonPrimitive(id1));
                values.add(new JsonPrimitive(level1));
                if (!stack.isEmpty()) ++nonempty;
            }
            values.add(new JsonPrimitive(nonempty));
            out.addProperty("ok", true);
            out.add("values", values);
        } catch (Throwable t) {
            out.addProperty("ok", false);
            out.addProperty("error", t.toString());
        }
        return out;
    }

    private static JsonArray box(StructureBoundingBox b) {
        JsonArray out = new JsonArray();
        out.add(new JsonPrimitive(b.minX));
        out.add(new JsonPrimitive(b.minY));
        out.add(new JsonPrimitive(b.minZ));
        out.add(new JsonPrimitive(b.maxX));
        out.add(new JsonPrimitive(b.maxY));
        out.add(new JsonPrimitive(b.maxZ));
        return out;
    }

    private static JsonObject piece(StructureComponent c, int index) throws Exception {
        JsonObject out = new JsonObject();
        out.addProperty("index", index);
        out.addProperty("component", c.getComponentType());
        out.add("box", box(c.getBoundingBox()));
        EnumFacing facing = c.getCoordBaseMode();
        out.addProperty("facing", facing == null ? -1 : facing.getHorizontalIndex());

        if (c instanceof StructureMineshaftPieces.Room) {
            out.addProperty("kind", "room");
            JsonArray entrances = new JsonArray();
            @SuppressWarnings("unchecked")
            List<StructureBoundingBox> boxes =
                (List<StructureBoundingBox>)field(c, "roomsLinkedToTheRoom");
            for (StructureBoundingBox entrance : boxes) entrances.add(box(entrance));
            out.add("entrances", entrances);
        } else if (c instanceof StructureMineshaftPieces.Corridor) {
            out.addProperty("kind", "corridor");
            out.addProperty("rails", (Boolean)field(c, "hasRails"));
            out.addProperty("spiders", (Boolean)field(c, "hasSpiders"));
            out.addProperty("spawner", (Boolean)field(c, "spawnerPlaced"));
            out.addProperty("sections", (Integer)field(c, "sectionCount"));
        } else if (c instanceof StructureMineshaftPieces.Cross) {
            out.addProperty("kind", "cross");
            out.addProperty("multiple", (Boolean)field(c, "isMultipleFloors"));
        } else if (c instanceof StructureMineshaftPieces.Stairs) {
            out.addProperty("kind", "stairs");
        } else {
            out.addProperty("kind", c.getClass().getName());
        }
        return out;
    }

    private static BuiltStart buildStart(Random rand, int cx, int cz, int mineType) {
        net.minecraft.world.gen.structure.MapGenMineshaft.Type type =
            mineType == 1
                ? net.minecraft.world.gen.structure.MapGenMineshaft.Type.MESA
                : net.minecraft.world.gen.structure.MapGenMineshaft.Type.NORMAL;
        StructureMineshaftPieces.Room room = new StructureMineshaftPieces.Room(
            0, rand, (cx << 4) + 2, (cz << 4) + 2,
            type);
        List<StructureComponent> pieces = new LinkedList<StructureComponent>();
        pieces.add(room);
        room.buildComponent(room, pieces, rand);

        StructureBoundingBox total = StructureBoundingBox.getNewBoundingBox();
        for (StructureComponent p : pieces) total.expandTo(p.getBoundingBox());
        int dy;
        if (type == net.minecraft.world.gen.structure.MapGenMineshaft.Type.MESA) {
            dy = 63 - total.maxY + total.getYSize() / 2 + 5;
        } else {
            int available = 63 - 10;
            int height = total.getYSize() + 1;
            if (height < available) height += rand.nextInt(available - height);
            dy = height - total.maxY;
        }
        total.offset(0, dy, 0);
        for (StructureComponent p : pieces) p.offset(0, dy, 0);
        return new BuiltStart(pieces, total);
    }

    private static JsonObject start(Random rand, int cx, int cz, int mineType) throws Exception {
        long seed48Before = cursor(rand);
        BuiltStart built = buildStart(rand, cx, cz, mineType);

        JsonObject out = new JsonObject();
        out.addProperty("cx", cx); out.addProperty("cz", cz);
        out.addProperty("seed48_before", seed48Before);
        out.add("box", box(built.total));
        JsonArray all = new JsonArray();
        for (int i = 0; i < built.pieces.size(); ++i)
            all.add(piece(built.pieces.get(i), i));
        out.add("pieces", all);
        out.addProperty("rng_probe", rand.nextLong());
        return out;
    }

    private static BuiltStart findStart(long seed, int targetCx, int targetCz,
                                        int selected, int mineType) {
        Random rand = new Random(seed);
        long xMul = rand.nextLong();
        long zMul = rand.nextLong();
        int found = 0;
        for (int cx = targetCx - 8; cx <= targetCx + 8; ++cx) {
            for (int cz = targetCz - 8; cz <= targetCz + 8; ++cz) {
                rand.setSeed((long)cx * xMul ^ (long)cz * zMul ^ seed);
                rand.nextInt();
                if (rand.nextDouble() < 0.004D
                        && rand.nextInt(80) < Math.max(Math.abs(cx), Math.abs(cz))) {
                    if (found++ == selected) return buildStart(rand, cx, cz, mineType);
                }
            }
        }
        return null;
    }

    /** Run real addComponentParts against an all-stone 16x64 population clip.
     * This isolates piece placement and RNG from biome decoration and chunk
     * loading order while still executing Mojang's World/Block/TileEntity code. */
    static JsonObject placement(WorldServer world, long topologySeed,
                                int targetCx, int targetCz, int selected,
                                long placementSeed, int clipDx, int clipDz,
                                String file, int mineType) {
        JsonObject out = new JsonObject();
        try {
            BuiltStart built = findStart(
                topologySeed, targetCx, targetCz, selected, mineType);
            if (built == null) throw new IllegalArgumentException("start index out of range");
            int dx = 1024 - built.total.minX;
            int dz = 1024 - built.total.minZ;
            built.total.offset(dx, 0, dz);
            for (StructureComponent p : built.pieces) p.offset(dx, 0, dz);

            int x0 = 1024 + clipDx * 16;
            int z0 = 1024 + clipDz * 16;
            StructureBoundingBox clip = new StructureBoundingBox(
                x0, 0, z0, x0 + 15, 63, z0 + 15);
            BlockPos.MutableBlockPos pos = new BlockPos.MutableBlockPos();
            for (int y = 0; y <= 63; ++y)
                for (int z = z0; z <= z0 + 15; ++z)
                    for (int x = x0; x <= x0 + 15; ++x)
                        world.setBlockState(pos.setPos(x, y, z),
                                            Blocks.STONE.getDefaultState(), 2);

            AxisAlignedBB entities = new AxisAlignedBB(
                x0, 0, z0, x0 + 16, 64, z0 + 16);
            for (Entity e : world.getEntitiesWithinAABB(EntityMinecartChest.class, entities)) {
                e.setDead();
                world.removeEntity(e);
            }

            Random placement = new Random(placementSeed);
            int accepted = 0;
            for (StructureComponent p : built.pieces)
                if (p.getBoundingBox().intersectsWith(clip)
                        && p.addComponentParts(world, placement, clip)) ++accepted;

            byte[] data = new byte[16 * 64 * 16 * 2];
            int k = 0;
            for (int y = 0; y <= 63; ++y)
                for (int z = z0; z <= z0 + 15; ++z)
                    for (int x = x0; x <= x0 + 15; ++x) {
                        net.minecraft.block.state.IBlockState state =
                            world.getBlockState(pos.setPos(x, y, z));
                        Block block = state.getBlock();
                        int value = (Block.getIdFromBlock(block) << 4)
                            | block.getMetaFromState(state);
                        data[k++] = (byte)(value & 255);
                        data[k++] = (byte)((value >>> 8) & 255);
                    }
            java.io.DataOutputStream stream = new java.io.DataOutputStream(
                new java.io.BufferedOutputStream(new java.io.FileOutputStream(file)));
            stream.write(data); stream.close();

            List<EntityMinecartChest> cartEntities = new LinkedList<EntityMinecartChest>();
            for (EntityMinecartChest cart :
                    world.getEntitiesWithinAABB(EntityMinecartChest.class, entities))
                if (!cart.isDead) cartEntities.add(cart);
            JsonArray carts = new JsonArray();
            for (EntityMinecartChest cart : cartEntities) {
                NBTTagCompound tag = new NBTTagCompound();
                cart.writeToNBT(tag);
                JsonObject event = new JsonObject();
                event.addProperty("x", (int)Math.floor(cart.posX));
                event.addProperty("y", (int)Math.floor(cart.posY));
                event.addProperty("z", (int)Math.floor(cart.posZ));
                event.addProperty("loot_table", tag.getString("LootTable"));
                event.addProperty("loot_seed", tag.getLong("LootTableSeed"));
                carts.add(event);
            }
            JsonArray spawners = new JsonArray();
            for (int y = 0; y <= 63; ++y)
                for (int z = z0; z <= z0 + 15; ++z)
                    for (int x = x0; x <= x0 + 15; ++x) {
                        BlockPos at = pos.setPos(x, y, z);
                        if (world.getBlockState(at).getBlock() != Blocks.MOB_SPAWNER) continue;
                        NBTTagCompound tag = new NBTTagCompound();
                        net.minecraft.tileentity.TileEntity tile = world.getTileEntity(at);
                        if (tile != null) tile.writeToNBT(tag);
                        JsonObject event = new JsonObject();
                        event.addProperty("x", x); event.addProperty("y", y);
                        event.addProperty("z", z);
                        event.addProperty("entity", tag.getCompoundTag("SpawnData").getString("id"));
                        spawners.add(event);
                    }
            out.addProperty("ok", true);
            out.addProperty("file", file);
            out.addProperty("pieces", built.pieces.size());
            out.addProperty("accepted", accepted);
            out.addProperty("minecart_chests", cartEntities.size());
            out.add("carts", carts);
            out.add("spawners", spawners);
            out.addProperty("rng_seed48_after", cursor(placement));
        } catch (Throwable t) {
            out.addProperty("ok", false);
            out.addProperty("error", t.toString());
        }
        return out;
    }

    static JsonObject map(long seed, int targetCx, int targetCz, int mineType) {
        JsonObject out = new JsonObject();
        out.addProperty("ok", true);
        out.addProperty("seed", seed);
        out.addProperty("target_cx", targetCx);
        out.addProperty("target_cz", targetCz);
        JsonArray starts = new JsonArray();
        try {
            Random rand = new Random(seed);
            long xMul = rand.nextLong();
            long zMul = rand.nextLong();
            for (int cx = targetCx - 8; cx <= targetCx + 8; ++cx) {
                for (int cz = targetCz - 8; cz <= targetCz + 8; ++cz) {
                    rand.setSeed((long)cx * xMul ^ (long)cz * zMul ^ seed);
                    rand.nextInt();
                    if (rand.nextDouble() < 0.004D
                            && rand.nextInt(80) < Math.max(Math.abs(cx), Math.abs(cz))) {
                        starts.add(start(rand, cx, cz, mineType));
                    }
                }
            }
            out.add("starts", starts);
        } catch (Throwable t) {
            out.addProperty("ok", false);
            out.addProperty("error", t.toString());
        }
        return out;
    }
}
