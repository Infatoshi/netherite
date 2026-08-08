package com.microsoft.Malmo.Mixins;

import net.minecraft.entity.item.EntityItem;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Redirect;
import qrl.Recorder;

/** Keep server-side EntityItem constructors on the parked Math.random cursor. */
@Mixin(EntityItem.class)
public abstract class MixinOracleEntityItemMath {
    @Redirect(
        method = "<init>*",
        at = @At(
            value = "INVOKE",
            target = "Ljava/lang/Math;random()D"))
    private double qrl$serverRandom() {
        return Recorder.oracleServerMathRandom();
    }
}
