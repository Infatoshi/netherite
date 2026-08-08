package com.microsoft.Malmo.Mixins;

import net.minecraft.entity.item.EntityXPOrb;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Redirect;
import qrl.Recorder;

/** Keep server-side XP-orb constructors on the parked Math.random cursor. */
@Mixin(EntityXPOrb.class)
public abstract class MixinOracleXpOrbMath {
    @Redirect(
        method = "<init>*",
        at = @At(
            value = "INVOKE",
            target = "Ljava/lang/Math;random()D"))
    private double qrl$serverRandom() {
        return Recorder.oracleServerMathRandom();
    }
}
