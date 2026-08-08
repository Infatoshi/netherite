package com.microsoft.Malmo.Mixins;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

import net.minecraft.client.audio.SoundManager;

import netheritemod.QLaunch;

/**
 * CLI-instance strip (qrl_launch.json strip.sound): never boot the paulscode
 * SoundSystem. Cancelling loadSoundSystem() leaves SoundManager.loaded == false,
 * and every play/stop/tick path in SoundManager already no-ops when !loaded, so
 * this is the supported "sound absent" state - no threads, no OpenAL, ~1s less
 * startup. Rendering is untouched (sound has no pixels).
 */
@Mixin(SoundManager.class)
public abstract class MixinNoSound {
    @Inject(method = "loadSoundSystem", at = @At("HEAD"), cancellable = true)
    private void qrl$noSoundSystem(CallbackInfo ci) {
        if (QLaunch.NO_SOUND) ci.cancel();
    }
}
