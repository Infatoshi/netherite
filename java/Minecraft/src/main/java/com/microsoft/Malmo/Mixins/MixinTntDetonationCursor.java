package com.microsoft.Malmo.Mixins;

import net.minecraft.entity.item.EntityTNTPrimed;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import qrl.Recorder;

/** Pins the saved oracle World.rand cursor at fuse-zero detonation. */
@Mixin(EntityTNTPrimed.class)
public abstract class MixinTntDetonationCursor {
    @Inject(method = "explode", at = @At("HEAD"))
    private void qrl$restoreTntDetonationCursor(CallbackInfo ci) {
        Recorder.oracleRestoreTntDetonationCursor(
            (EntityTNTPrimed)(Object)this);
    }
}
