package com.microsoft.Malmo.Mixins;

import net.minecraft.entity.EntityLivingBase;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Redirect;
import qrl.Recorder;

/** Keep server-side living construction and damage on the parked cursor. */
@Mixin(EntityLivingBase.class)
public abstract class MixinOracleLivingMath {
    @Redirect(
        method = {"<init>", "attackEntityFrom"},
        at = @At(
            value = "INVOKE",
            target = "Ljava/lang/Math;random()D"))
    private double qrl$serverRandom() {
        return Recorder.oracleServerMathRandom();
    }
}
