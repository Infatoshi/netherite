package com.microsoft.Malmo.Mixins;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

import net.minecraft.client.gui.GuiBossOverlay;

import netheritemod.QLaunch;

/**
 * CLI-instance GUI strip (qrl_launch.json strip.overlays): never draw the boss health
 * bar (dragon/wither). Boss state stays in the world and is observable over the NetheriteMod
 * bridge; the overlay is pure HUD chrome that pollutes whole-frame pixel diffs.
 * Hotbar, health/hunger/xp bars, and crosshair are intentionally untouched.
 * (The vignette needs no mixin: fancyGraphics:false already skips it.)
 */
@Mixin(GuiBossOverlay.class)
public abstract class MixinStripBossBar {
    @Inject(method = "renderBossHealth", at = @At("HEAD"), cancellable = true)
    private void qrl$noBossBar(CallbackInfo ci) {
        if (QLaunch.STRIP_OVERLAYS) ci.cancel();
    }
}
