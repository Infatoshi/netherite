package com.netherite.mod.mixin;

import com.netherite.mod.FrameGrabber;
import net.minecraft.client.render.GameRenderer;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(GameRenderer.class)
public class GameRendererMixin {
    @Inject(method = "render", at = @At("TAIL"))
    private void onRenderEnd(float tickDelta, long startTime, boolean tick, CallbackInfo ci) {
        FrameGrabber.INSTANCE.onFrameReady();
    }
}
