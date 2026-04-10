package com.netherite.mod.mixin;

import net.minecraft.client.render.RenderTickCounter;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/**
 * When uncapped, force exactly 1 client tick per rendered frame.
 * This makes client TPS = FPS. At 260 FPS cap, you get 260 TPS.
 */
@Mixin(RenderTickCounter.class)
public class RenderTickCounterMixin {
    @Shadow public float tickDelta;

    @Inject(method = "beginRenderTick", at = @At("HEAD"), cancellable = true)
    private void forceOneTick(long timeMillis, CallbackInfoReturnable<Integer> cir) {
        if (Boolean.getBoolean("netherite.uncapped")) {
            this.tickDelta = 0.0f;
            cir.setReturnValue(1);
        }
    }
}
