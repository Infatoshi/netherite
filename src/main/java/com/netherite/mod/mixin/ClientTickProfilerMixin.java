package com.netherite.mod.mixin;

import com.netherite.mod.NetheriteMod;
import net.minecraft.client.MinecraftClient;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Unique;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(MinecraftClient.class)
public class ClientTickProfilerMixin {
    @Unique private static final int PROFILE_INTERVAL = 500;

    // Full frame (render call to render call)
    @Unique private static long lastFrameEndNs = 0;
    @Unique private static long profileFrameIntervalNs = 0;

    // Client tick
    @Unique private static long tickStartNs = 0;
    @Unique private static long profileClientTickNs = 0;

    @Unique private static int profileCount = 0;

    @Inject(method = "tick", at = @At("HEAD"))
    private void onTickStart(CallbackInfo ci) {
        tickStartNs = System.nanoTime();
    }

    @Inject(method = "tick", at = @At("TAIL"))
    private void onTickEnd(CallbackInfo ci) {
        long tickNs = System.nanoTime() - tickStartNs;
        profileClientTickNs += tickNs;
    }

    @Inject(method = "render", at = @At("HEAD"))
    private void onFrameStart(boolean tick, CallbackInfo ci) {
        long now = System.nanoTime();
        if (lastFrameEndNs > 0) {
            profileFrameIntervalNs += (now - lastFrameEndNs);
        }
    }

    @Inject(method = "render", at = @At("TAIL"))
    private void onFrameEnd(boolean tick, CallbackInfo ci) {
        lastFrameEndNs = System.nanoTime();
        profileCount++;

        if (profileCount >= PROFILE_INTERVAL) {
            double avgFrameIntervalUs = profileFrameIntervalNs / (double) profileCount / 1000.0;
            double avgClientTickUs = profileClientTickNs / (double) profileCount / 1000.0;
            double fps = 1_000_000.0 / avgFrameIntervalUs;

            NetheriteMod.LOGGER.info(
                "ClientTick profile (n={}): frameInterval={}us ({}fps), clientTick={}us",
                profileCount,
                String.format("%.1f", avgFrameIntervalUs),
                String.format("%.0f", fps),
                String.format("%.1f", avgClientTickUs));

            profileFrameIntervalNs = 0;
            profileClientTickNs = 0;
            profileCount = 0;
        }
    }
}
