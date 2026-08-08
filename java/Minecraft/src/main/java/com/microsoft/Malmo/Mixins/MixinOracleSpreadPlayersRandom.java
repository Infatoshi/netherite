package com.microsoft.Malmo.Mixins;

import java.util.Random;
import net.minecraft.command.CommandSpreadPlayers;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Redirect;

/** Test-only seed injection for CommandSpreadPlayers' local Random. */
@Mixin(CommandSpreadPlayers.class)
public abstract class MixinOracleSpreadPlayersRandom {
    @Redirect(
        method = "spread",
        at = @At(value = "NEW", target = "java/util/Random"))
    private Random qrl$constructRandom() {
        return qrl.OracleSpreadPlayersRandom.construct();
    }
}
