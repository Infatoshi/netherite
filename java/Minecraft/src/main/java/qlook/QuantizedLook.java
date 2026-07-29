package qlook;

import net.minecraft.client.Minecraft;
import net.minecraft.client.entity.EntityPlayerSP;
import net.minecraftforge.common.MinecraftForge;
import net.minecraftforge.fml.common.Mod;
import net.minecraftforge.fml.common.event.FMLInitializationEvent;
import net.minecraftforge.fml.common.eventhandler.SubscribeEvent;
import net.minecraftforge.fml.common.gameevent.TickEvent;
import org.lwjgl.input.Keyboard;

/**
 * Mouse-free, quantized aiming. Arrow keys snap the player's view to a 15-degree
 * grid (yaw) / 15-degree steps clamped to +/-90 (pitch). Replaces broken mouse-look
 * on macOS and defines the discrete aim quantum the RL action space will reuse.
 *
 * Self-contained second @Mod so it does not touch the Malmo mod source.
 */
@Mod(modid = QuantizedLook.MODID, name = "Quantized Look", version = "1.0",
     clientSideOnly = true, acceptableRemoteVersions = "*")
public class QuantizedLook {
    public static final String MODID = "qlook";

    static final float QUANTUM = 15.0f;  // degrees per step
    static final int INITIAL_DELAY = 4;  // ticks held before auto-repeat kicks in
    static final int REPEAT_EVERY = 2;   // ticks between repeats while held

    private static final int[] KEYS = {
        Keyboard.KEY_LEFT, Keyboard.KEY_RIGHT, Keyboard.KEY_UP, Keyboard.KEY_DOWN
    };
    private final int[] held = new int[4]; // held-tick counters per arrow key

    @Mod.EventHandler
    public void init(FMLInitializationEvent e) {
        MinecraftForge.EVENT_BUS.register(this);
    }

    @SubscribeEvent
    public void onClientTick(TickEvent.ClientTickEvent e) {
        if (e.phase != TickEvent.Phase.END) return;
        Minecraft mc = Minecraft.getMinecraft();
        EntityPlayerSP p = mc.player;
        if (p == null || mc.currentScreen != null) { // only in-world with no GUI/chat open
            for (int i = 0; i < held.length; i++) held[i] = 0;
            return;
        }
        for (int i = 0; i < KEYS.length; i++) {
            if (!Keyboard.isKeyDown(KEYS[i])) { held[i] = 0; continue; }
            held[i]++;
            boolean fire = held[i] == 1
                || (held[i] > INITIAL_DELAY && (held[i] - INITIAL_DELAY) % REPEAT_EVERY == 0);
            if (!fire) continue;
            switch (i) {
                case 0: stepYaw(p, -1);   break; // left
                case 1: stepYaw(p, +1);   break; // right
                case 2: stepPitch(p, -1); break; // up = look up
                case 3: stepPitch(p, +1); break; // down = look down
            }
        }
    }

    private void stepYaw(EntityPlayerSP p, int dir) {
        p.rotationYaw = (Math.round(p.rotationYaw / QUANTUM) + dir) * QUANTUM;
    }

    private void stepPitch(EntityPlayerSP p, int dir) {
        float v = (Math.round(p.rotationPitch / QUANTUM) + dir) * QUANTUM;
        p.rotationPitch = Math.max(-90.0f, Math.min(90.0f, v));
    }
}
