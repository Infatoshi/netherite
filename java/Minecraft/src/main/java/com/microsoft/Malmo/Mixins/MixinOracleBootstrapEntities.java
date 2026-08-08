package com.microsoft.Malmo.Mixins;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

import net.minecraft.world.World;
import qrl.Recorder;

/** WorldServer entity updates are separate from WorldServer.tick in 1.11.2. */
@Mixin(World.class)
public abstract class MixinOracleBootstrapEntities {
    @Inject(method = "updateEntities", at = @At("HEAD"), cancellable = true)
    private void qrl$freezeEntityTick(CallbackInfo callback) {
        World self = (World)(Object)this;
        if (!self.isRemote && Recorder.oracleFreezeWorldBootstrap())
            callback.cancel();
    }
}
