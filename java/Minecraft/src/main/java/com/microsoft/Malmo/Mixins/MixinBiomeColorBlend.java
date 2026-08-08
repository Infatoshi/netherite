package com.microsoft.Malmo.Mixins;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

import net.minecraft.util.math.BlockPos;
import net.minecraft.world.IBlockAccess;
import net.minecraft.world.biome.BiomeColorHelper;
import netheritemod.QBiomeNative;

/**
 * Routes BiomeColorHelper.getGrassColorAtPos (the 3x3 biome grass-color blend, kernel 18)
 * through the native C kernel (or sabotages it) based on QBiomeNative.MODE, read once.
 * HEAD inject + cancellable so MODE=0 leaves the original Java loop untouched. Grass is
 * the dominant tinted surface on the superflat course, so this is a whole-frame-visible
 * meshing-stage drop-in.
 */
@Mixin(BiomeColorHelper.class)
public abstract class MixinBiomeColorBlend {
    @Inject(method = "getGrassColorAtPos", at = @At("HEAD"), cancellable = true)
    private static void qbiome$onGrass(IBlockAccess blockAccess, BlockPos pos,
                                       CallbackInfoReturnable<Integer> cir) {
        int mode = QBiomeNative.MODE;
        if (mode == 0) return;
        if (mode == 2) { cir.setReturnValue(0xFF00FF); return; }   // sabotage: magenta grass
        int[] c = new int[9];
        int n = 0;
        for (BlockPos.MutableBlockPos mp : BlockPos.getAllInBoxMutable(pos.add(-1, 0, -1), pos.add(1, 0, 1))) {
            c[n++] = blockAccess.getBiome(mp).getGrassColorAtPos(mp);
        }
        cir.setReturnValue(QBiomeNative.nblend(c));
    }
}
