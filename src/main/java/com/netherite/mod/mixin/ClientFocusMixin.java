package com.netherite.mod.mixin;

import net.minecraft.client.MinecraftClient;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(MinecraftClient.class)
public class ClientFocusMixin {
    @Inject(method = "isWindowFocused", at = @At("HEAD"), cancellable = true)
    private void alwaysFocused(CallbackInfoReturnable<Boolean> cir) {
        if (Boolean.getBoolean("netherite.headless")) {
            cir.setReturnValue(true);
        }
    }
}
