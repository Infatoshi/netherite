package com.microsoft.Malmo.Mixins;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

import net.minecraft.util.math.MathHelper;
import netheritemod.QSinNative;

/**
 * Routes MathHelper.sin(float) through a native C kernel (or sabotages it) based on
 * the qsin_mode.txt sidecar, read once via netheritemod.QSinNative.MODE. HEAD inject + cancellable so MODE=0
 * leaves the original Java table lookup untouched (one build, three modes).
 */
@Mixin(MathHelper.class)
public abstract class MixinMathHelper {
    @Inject(method = "sin", at = @At("HEAD"), cancellable = true)
    private static void qsin$onSin(float value, CallbackInfoReturnable<Float> cir) {
        int mode = QSinNative.MODE;
        if (mode == 1) {
            cir.setReturnValue(QSinNative.nsin(value));
        } else if (mode == 2) {
            cir.setReturnValue(0.0f);
        }
    }
}
