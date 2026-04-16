package com.netherite.mod.mixin;

import it.unimi.dsi.fastutil.objects.Object2IntMap;
import net.minecraft.block.BlockState;
import net.minecraft.client.render.model.BakedModelManager;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(BakedModelManager.class)
public class BakedModelManagerMixin {
    @Shadow
    private Object2IntMap<BlockState> stateLookup;

    @Inject(method = "shouldRerender", at = @At("HEAD"), cancellable = true)
    private void netherite$guardUninitializedStateLookup(
            BlockState from,
            BlockState to,
            CallbackInfoReturnable<Boolean> cir
    ) {
        if (stateLookup == null) {
            cir.setReturnValue(true);
        }
    }
}
