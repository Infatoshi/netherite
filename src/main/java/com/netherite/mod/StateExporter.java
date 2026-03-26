package com.netherite.mod;

import net.minecraft.client.MinecraftClient;
import net.minecraft.client.network.ClientPlayerEntity;
import net.minecraft.entity.Entity;
import net.minecraft.entity.LivingEntity;
import net.minecraft.item.ItemStack;
import net.minecraft.registry.Registries;
import net.minecraft.util.math.Box;

import java.nio.MappedByteBuffer;
import java.util.List;

/**
 * Exports player state and nearby entities to shared memory.
 * Runs on END_CLIENT_TICK.
 */
public class StateExporter {
    public static final StateExporter INSTANCE = new StateExporter();

    private static final int MAGIC = 0x4E455453; // "NETS"
    private static final int SHMEM_SIZE = 64 * 1024; // 64KB
    private static final int MAX_ENTITIES = 32;
    private static final double ENTITY_RANGE = 16.0;

    private ShmemBuffer shmem;
    private int tickCount = 0;

    public void init(int instanceId) {
        shmem = new ShmemBuffer("netherite_state_" + instanceId, SHMEM_SIZE);
        NetheriteMod.LOGGER.info("StateExporter: shmem ready for instance {}", instanceId);
    }

    public void tick(MinecraftClient mc) {
        if (shmem == null || mc.player == null || mc.world == null) return;

        ClientPlayerEntity player = mc.player;
        MappedByteBuffer buf = shmem.getBuffer();
        buf.position(0);

        // Header
        buf.putInt(MAGIC);           // offset 0
        buf.putInt(tickCount);       // offset 4
        buf.putInt(0);               // offset 8: data_size (filled later)
        buf.putInt(0);               // offset 12: ready=0

        // Player state at offset 16
        buf.putDouble(player.getX());         // 16
        buf.putDouble(player.getY());         // 24
        buf.putDouble(player.getZ());         // 32
        buf.putFloat(player.getYaw());        // 40
        buf.putFloat(player.getPitch());      // 44
        buf.putFloat(player.getHealth());     // 48
        buf.putFloat(player.getMaxHealth());  // 52
        buf.putInt(player.getHungerManager().getFoodLevel()); // 56
        buf.putFloat(player.getHungerManager().getSaturationLevel()); // 60
        buf.putInt(player.isOnGround() ? 1 : 0);  // 64
        buf.putInt(player.isTouchingWater() ? 1 : 0); // 68

        // Hotbar at offset 72: 9 slots x 8 bytes = 72 bytes
        for (int i = 0; i < 9; i++) {
            ItemStack stack = player.getInventory().getStack(i);
            if (stack.isEmpty()) {
                buf.putInt(0);  // item_id
                buf.putInt(0);  // count
            } else {
                buf.putInt(Registries.ITEM.getRawId(stack.getItem()));
                buf.putInt(stack.getCount());
            }
        }

        // Entities at offset 144
        Box scanBox = player.getBoundingBox().expand(ENTITY_RANGE);
        List<Entity> entities = mc.world.getOtherEntities(player, scanBox);
        int count = Math.min(entities.size(), MAX_ENTITIES);
        buf.putInt(count);

        for (int i = 0; i < count; i++) {
            Entity e = entities.get(i);
            buf.putInt(Registries.ENTITY_TYPE.getRawId(e.getType()));
            buf.putDouble(e.getX());
            buf.putDouble(e.getY());
            buf.putDouble(e.getZ());
            buf.putFloat(e instanceof LivingEntity le ? le.getHealth() : 0.0f);
        }

        // Set data size and ready flag
        int dataSize = buf.position() - 16;
        buf.putInt(8, dataSize);
        buf.putInt(12, 1);
        buf.force();

        tickCount++;
    }
}
