package com.microsoft.Malmo.Mixins;

import java.util.Random;
import net.minecraft.item.ItemShears;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Redirect;

/** Test-only cursor injection for Forge ItemShears' clock-seeded Random. */
@Mixin(ItemShears.class)
public abstract class MixinOracleShearsRandom {
    @Redirect(
        method = "itemInteractionForEntity",
        at = @At(value = "NEW", target = "java/util/Random"))
    private Random qrl$constructRandom() {
        return qrl.OracleShearsRandom.construct();
    }
}
