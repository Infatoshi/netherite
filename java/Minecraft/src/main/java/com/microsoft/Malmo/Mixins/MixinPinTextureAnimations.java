package com.microsoft.Malmo.Mixins;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

import net.minecraft.client.renderer.texture.TextureAtlasSprite;

import netheritemod.QLaunch;

/**
 * Freeze animated block-atlas sprites on the physical frame zero that
 * TextureMap uploads during resource loading. This makes fire, lava, water,
 * and portal pixel goldens deterministic and matches magma's frame-zero
 * atlas. The TextureMap update loop itself still runs and keeps its GL bind.
 */
@Mixin(TextureAtlasSprite.class)
public abstract class MixinPinTextureAnimations {
    @Inject(method = "updateAnimation", at = @At("HEAD"), cancellable = true)
    private void qrl$pinTextureAnimations(CallbackInfo ci) {
        if (QLaunch.PIN_TEXTURE_ANIMATIONS) ci.cancel();
    }
}
