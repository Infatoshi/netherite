package com.netherite.mod.mixin;

import net.minecraft.server.MinecraftServer;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(MinecraftServer.class)
public class ServerTickMixin {
    @Inject(method = "shouldKeepTicking", at = @At("HEAD"), cancellable = true)
    private void uncapTickRate(CallbackInfoReturnable<Boolean> cir) {
        if (!Boolean.getBoolean("netherite.uncapped")) return;

        MinecraftServer server = (MinecraftServer) (Object) this;
        // Only uncap after players have joined (world gen needs normal timing)
        if (server.getCurrentPlayerCount() > 0) {
            cir.setReturnValue(false);
        }
    }
}
