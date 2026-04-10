package com.netherite.mod;

import net.minecraft.client.MinecraftClient;
import net.minecraft.client.option.KeyBinding;

import java.nio.MappedByteBuffer;

/**
 * Reads actions from shared memory and injects them as player input.
 * Runs on END_CLIENT_TICK.
 */
public class ActionInjector {
    public static final ActionInjector INSTANCE = new ActionInjector();

    private static final int MAGIC = 0x4E455441; // "NETA"
    private static final int SHMEM_SIZE = 4096;

    private ShmemBuffer shmem;
    private int lastTickNumber = -1;

    public void init(int instanceId) {
        shmem = new ShmemBuffer("netherite_action_" + instanceId, SHMEM_SIZE);
        // Write initial magic so Python can detect the buffer exists
        MappedByteBuffer buf = shmem.getBuffer();
        buf.putInt(0, MAGIC);
        buf.putInt(12, 0); // not ready
        buf.force();
        NetheriteMod.LOGGER.info("ActionInjector: shmem ready for instance {}", instanceId);
    }

    public void tick(MinecraftClient mc) {
        if (shmem == null || mc.player == null) return;

        if (WorldController.INSTANCE.isStartLatched()) {
            clearKeys(mc);
            lastTickNumber = -1;
            return;
        }

        MappedByteBuffer buf = shmem.getBuffer();
        int magic = buf.getInt(0);
        if (magic != MAGIC) return;

        int ready = buf.getInt(12);
        if (ready == 0) return;

        int tickNumber = buf.getInt(4);

        // Read action payload at offset 16
        boolean forward = buf.get(16) != 0;
        boolean back = buf.get(17) != 0;
        boolean left = buf.get(18) != 0;
        boolean right = buf.get(19) != 0;
        boolean jump = buf.get(20) != 0;
        boolean sneak = buf.get(21) != 0;
        boolean sprint = buf.get(22) != 0;
        boolean attack = buf.get(23) != 0;
        boolean use = buf.get(24) != 0;
        byte cameraDx = buf.get(25);
        byte cameraDy = buf.get(26);

        // Movement keys: held every tick
        setKey(mc.options.forwardKey, forward);
        setKey(mc.options.backKey, back);
        setKey(mc.options.leftKey, left);
        setKey(mc.options.rightKey, right);
        setKey(mc.options.jumpKey, jump);
        setKey(mc.options.sneakKey, sneak);
        setKey(mc.options.sprintKey, sprint);
        setKey(mc.options.attackKey, attack);
        setKey(mc.options.useKey, use);

        // Camera delta: apply once per new tick number
        if (tickNumber != lastTickNumber) {
            float yaw = mc.player.getYaw() + cameraDx;
            float pitch = mc.player.getPitch() + cameraDy;
            pitch = Math.max(-90.0f, Math.min(90.0f, pitch));
            mc.player.setYaw(yaw);
            mc.player.setPitch(pitch);
            lastTickNumber = tickNumber;
        }
    }

    private static void setKey(KeyBinding key, boolean pressed) {
        KeyBinding.setKeyPressed(key.getDefaultKey(), pressed);
    }

    private static void clearKeys(MinecraftClient mc) {
        setKey(mc.options.forwardKey, false);
        setKey(mc.options.backKey, false);
        setKey(mc.options.leftKey, false);
        setKey(mc.options.rightKey, false);
        setKey(mc.options.jumpKey, false);
        setKey(mc.options.sneakKey, false);
        setKey(mc.options.sprintKey, false);
        setKey(mc.options.attackKey, false);
        setKey(mc.options.useKey, false);
    }
}
