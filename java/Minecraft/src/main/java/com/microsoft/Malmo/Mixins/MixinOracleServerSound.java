package com.microsoft.Malmo.Mixins;

import javax.annotation.Nullable;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.util.SoundCategory;
import net.minecraft.util.SoundEvent;
import net.minecraft.world.ServerWorldEventHandler;
import net.minecraft.world.WorldServer;
import org.spongepowered.asm.mixin.Final;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import qrl.Recorder;

@Mixin(ServerWorldEventHandler.class)
public abstract class MixinOracleServerSound {
    @Shadow @Final private WorldServer world;

    @Inject(method = "playSoundToAllNearExcept", at = @At("HEAD"))
    private void qrl$captureServerSound(
            @Nullable EntityPlayer player, SoundEvent soundIn,
            SoundCategory category, double x, double y, double z,
            float volume, float pitch, CallbackInfo ci) {
        Recorder.oracleCaptureServerSound(
            world, soundIn, category, x, y, z, volume, pitch);
    }
}
