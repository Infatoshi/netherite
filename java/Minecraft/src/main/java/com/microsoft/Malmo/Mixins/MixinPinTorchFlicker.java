package com.microsoft.Malmo.Mixins;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

import net.minecraft.client.renderer.EntityRenderer;

import netheritemod.QLaunch;

/**
 * Determinism pin (qrl_launch.json determinism.pin_flicker): torch flicker is driven by
 * Math.random() (EntityRenderer.updateTorchFlicker), the ONE unseedable RNG that feeds
 * pixels - every blocklight-lit lightmap texel scales by (torchFlickerX*0.1 + 1.5).
 * Irrelevant in pure-skylight scenes, but the nether is 100% blocklight, so cross-launch
 * pixel diffs there are impossible without this pin. Zeroing the fields gives the fixed
 * 1.5x factor; lightmapUpdateNeeded must still be set (vanilla sets it here) or the
 * lightmap texture never refreshes. Off by default = vanilla flicker.
 */
@Mixin(EntityRenderer.class)
public abstract class MixinPinTorchFlicker {
    @Shadow private float torchFlickerX;
    @Shadow private float torchFlickerDX;
    @Shadow private boolean lightmapUpdateNeeded;

    @Inject(method = "updateTorchFlicker", at = @At("HEAD"), cancellable = true)
    private void qrl$pinFlicker(CallbackInfo ci) {
        if (!QLaunch.PIN_FLICKER) return;
        this.torchFlickerX = 0.0F;
        this.torchFlickerDX = 0.0F;
        this.lightmapUpdateNeeded = true;
        ci.cancel();
    }
}
