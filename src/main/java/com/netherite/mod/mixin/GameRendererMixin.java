package com.netherite.mod.mixin;

import com.netherite.mod.FrameGrabber;
import com.netherite.mod.NetheriteConfig;
import com.netherite.mod.NetheriteMod;
import net.minecraft.client.render.GameRenderer;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Unique;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(GameRenderer.class)
public class GameRendererMixin {
    @Unique private static final int PROFILE_INTERVAL = 500;
    @Unique private static long profileRenderNs = 0;
    @Unique private static int profileCount = 0;
    @Unique private static long renderStartNs = 0;

    @Inject(method = "render", at = @At("HEAD"), cancellable = true)
    private void onRenderStart(float tickDelta, long startTime, boolean tick, CallbackInfo ci) {
        if (NetheriteConfig.INSTANCE.skipRender) {
            FrameGrabber.INSTANCE.onFrameReady();
            ci.cancel();
            return;
        }
        renderStartNs = System.nanoTime();
    }

    @Inject(method = "render", at = @At("TAIL"))
    private void onRenderEnd(float tickDelta, long startTime, boolean tick, CallbackInfo ci) {
        long renderNs = System.nanoTime() - renderStartNs;
        profileRenderNs += renderNs;
        profileCount++;

        if (profileCount >= PROFILE_INTERVAL) {
            double avgRenderUs = profileRenderNs / (double) profileCount / 1000.0;
            NetheriteMod.LOGGER.info(
                "GameRenderer profile (n={}): render={}us",
                profileCount, String.format("%.1f", avgRenderUs));
            profileRenderNs = 0;
            profileCount = 0;
        }

        FrameGrabber.INSTANCE.onFrameReady();
    }
}
