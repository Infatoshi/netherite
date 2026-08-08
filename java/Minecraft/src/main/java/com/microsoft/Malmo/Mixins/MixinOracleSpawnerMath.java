package com.microsoft.Malmo.Mixins;

import net.minecraft.world.WorldEntitySpawner;
import net.minecraft.world.World;
import net.minecraft.world.biome.Biome;
import java.util.Random;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.Redirect;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import qrl.Recorder;

/** Keep natural-spawn Math.random calls on the parked server cursor. */
@Mixin(WorldEntitySpawner.class)
public abstract class MixinOracleSpawnerMath {
    @Inject(method = "performWorldGenSpawning", at = @At("HEAD"),
            cancellable = true)
    private static void qrl$suppressControlledWorldgenPacks(
            World world, Biome biome, int x, int z, int width, int depth,
            Random random, CallbackInfo ci) {
        if (Recorder.oracleSuppressWorldgenSpawning()) ci.cancel();
    }

    @Redirect(
        method = "findChunksForSpawning",
        at = @At(
            value = "INVOKE",
            target = "Ljava/lang/Math;random()D"))
    private double qrl$serverRandom() {
        return Recorder.oracleServerMathRandom();
    }

    @Redirect(
        method = "findChunksForSpawning",
        at = @At(
            value = "INVOKE",
            target = "Ljava/util/Collections;shuffle(Ljava/util/List;)V"))
    private void qrl$serverShuffle(java.util.List<?> values) {
        Recorder.oracleServerShuffle(values);
    }
}
