

package com.microsoft.Malmo.Mixins;


import java.util.UUID;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

import com.microsoft.Malmo.Utils.SeedHelper;

import net.minecraft.client.resources.DefaultPlayerSkin;


@Mixin(DefaultPlayerSkin.class)
public abstract class MixinRandomSkinTexture {
    // Randomize the skin for agents ignoring UUID
    @Inject(method = "isSlimSkin", at = @At("HEAD"), cancellable = true)
    private static void isSlimSkin(UUID playerUUID, CallbackInfoReturnable<Boolean> cir){
        // determinism.pin_skin (qrl_launch.json): SeedHelper's seed is per-launch
        // random, so the slim/classic arm flips between launches and pollutes
        // cross-launch pixel diffs. Pin to the classic (Steve) model.
        if (netheritemod.QLaunch.PIN_SKIN) {
            cir.setReturnValue(false);
            cir.cancel();
            return;
        }
        cir.setReturnValue(((playerUUID.hashCode() + SeedHelper.getWorldSeed()) & 1) == 0);
        cir.cancel();
    }
  
}
