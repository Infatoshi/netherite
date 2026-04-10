package com.netherite.mod.mixin;

import com.netherite.mod.NetheriteMod;
import com.netherite.mod.WorldController;
import net.minecraft.server.MinecraftServer;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Unique;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

import java.util.function.BooleanSupplier;

@Mixin(MinecraftServer.class)
public class ServerTickMixin {
    @Unique private static final int PROFILE_INTERVAL = 500;
    @Unique private static long tickStartNs = 0;
    @Unique private static long profileServerTickNs = 0;
    @Unique private static int profileCount = 0;

    @Inject(method = "tick", at = @At("HEAD"), cancellable = true)
    private void freezeStartup(BooleanSupplier shouldKeepTicking, CallbackInfo ci) {
        if (WorldController.INSTANCE.isStartLatched()) {
            ci.cancel();
            return;
        }
        tickStartNs = System.nanoTime();
    }

    @Inject(method = "tick", at = @At("TAIL"))
    private void onTickEnd(BooleanSupplier shouldKeepTicking, CallbackInfo ci) {
        if (tickStartNs == 0) return;

        long tickNs = System.nanoTime() - tickStartNs;
        profileServerTickNs += tickNs;
        profileCount++;

        if (profileCount >= PROFILE_INTERVAL) {
            double avgServerTickUs = profileServerTickNs / (double) profileCount / 1000.0;
            NetheriteMod.LOGGER.info(
                "ServerTick profile (n={}): serverTick={}us",
                profileCount, String.format("%.1f", avgServerTickUs));
            profileServerTickNs = 0;
            profileCount = 0;
        }
        tickStartNs = 0;
    }

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
