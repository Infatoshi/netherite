package qrl;

import net.minecraft.entity.Entity;
import net.minecraft.entity.monster.EntityZombie;
import net.minecraft.init.Blocks;
import net.minecraft.init.Bootstrap;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.nbt.NBTTagList;
import net.minecraft.tileentity.TileEntityMobSpawner;
import net.minecraft.util.math.BlockPos;

/** Exact 1.11.2 server-side TileEntityMobSpawner scalar/RNG oracle. */
public final class SpawnerGolden {
    private static final BlockPos POS = new BlockPos(12, 78, 8);

    private static TileEntityMobSpawner fixture(
            MinecartGolden.MemoryWorld world, int delay,
            int minDelay, int maxDelay, int spawnCount,
            int maxNearby, int spawnRange, boolean savedPotentials) {
        world.put(POS, Blocks.MOB_SPAWNER.getDefaultState());
        TileEntityMobSpawner tile = new TileEntityMobSpawner();
        tile.setWorld(world);
        tile.setPos(POS);
        NBTTagCompound nbt = new NBTTagCompound();
        nbt.setString("id", "minecraft:mob_spawner");
        nbt.setInteger("x", POS.getX());
        nbt.setInteger("y", POS.getY());
        nbt.setInteger("z", POS.getZ());
        nbt.setShort("Delay", (short)delay);
        nbt.setShort("MinSpawnDelay", (short)minDelay);
        nbt.setShort("MaxSpawnDelay", (short)maxDelay);
        nbt.setShort("SpawnCount", (short)spawnCount);
        nbt.setShort("MaxNearbyEntities", (short)maxNearby);
        nbt.setShort("RequiredPlayerRange", (short)16);
        nbt.setShort("SpawnRange", (short)spawnRange);
        NBTTagCompound spawn = new NBTTagCompound();
        spawn.setString("id", "minecraft:zombie");
        nbt.setTag("SpawnData", spawn);
        if (savedPotentials) {
            NBTTagCompound row = new NBTTagCompound();
            row.setInteger("Weight", 1);
            row.setTag("Entity", spawn.copy());
            NBTTagList rows = new NBTTagList();
            rows.appendTag(row);
            nbt.setTag("SpawnPotentials", rows);
        }
        tile.readFromNBT(nbt);
        return tile;
    }

    private static int delay(TileEntityMobSpawner tile) {
        return tile.writeToNBT(new NBTTagCompound()).getShort("Delay");
    }

    private static void addPlayer(MinecartGolden.MemoryWorld world) {
        MinecartGolden.TestPlayer player =
            new MinecartGolden.TestPlayer(world);
        player.setPosition(12.5, 78.5, 8.5);
        world.playerEntities.add(player);
    }

    private static void inactiveAndCountdown() throws Exception {
        MinecartGolden.MemoryWorld world = new MinecartGolden.MemoryWorld();
        TileEntityMobSpawner tile = fixture(world, 20, 7, 11, 1, 6, 4, false);
        world.rand.setSeed(0L ^ 0x5deece66dL);
        tile.update();
        System.out.printf("T0 %d %012x%n",
            delay(tile), MinecartGolden.rawSeed(world.rand));

        world = new MinecartGolden.MemoryWorld();
        tile = fixture(world, 20, 7, 11, 1, 6, 4, false);
        addPlayer(world);
        world.rand.setSeed(0L ^ 0x5deece66dL);
        tile.update();
        System.out.printf("T1 %d %012x%n",
            delay(tile), MinecartGolden.rawSeed(world.rand));
    }

    private static void nearbyReset(boolean savedPotentials) throws Exception {
        MinecartGolden.MemoryWorld world = new MinecartGolden.MemoryWorld();
        TileEntityMobSpawner tile = fixture(
            world, 0, 7, 11, 1, 1, 4, savedPotentials);
        addPlayer(world);
        EntityZombie nearby = new EntityZombie(world);
        nearby.setPosition(12.5, 78.0, 8.5);
        world.entities.add(nearby);
        world.rand.setSeed(0L ^ 0x5deece66dL);
        tile.update();
        System.out.printf("T%c %d %012x %d%n",
            savedPotentials ? 'R' : 'C', delay(tile),
            MinecartGolden.rawSeed(world.rand), world.entities.size());
    }

    public static void main(String[] args) throws Exception {
        Bootstrap.register();
        inactiveAndCountdown();
        nearbyReset(false);
        nearbyReset(true);
        System.out.println("spawner_live: PASS");
    }

    private SpawnerGolden() {}
}
