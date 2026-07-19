package com.microsoft.Malmo.Mixins;

import java.util.ArrayList;
import java.util.Set;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

import net.minecraft.server.management.PlayerChunkMap;
import net.minecraft.server.management.PlayerChunkMapEntry;

/**
 * Vanilla crash fix (always on; no behavior change): PlayerChunkMap.tick iterates
 * dirtyEntries (a HashSet) while PlayerChunkMapEntry.update() -> removeEntry() can
 * remove from that same set - single-threaded ConcurrentModificationException that
 * reliably kills the server right after a scripted cross-dimension teleport (entries
 * left dirty with no watching players). Drain a COPY at HEAD and clear, so the vanilla
 * loop sees an empty set and skips; semantics identical minus the crash.
 */
@Mixin(PlayerChunkMap.class)
public abstract class MixinPlayerChunkMapCMEFix {
    @Shadow private Set<PlayerChunkMapEntry> dirtyEntries;

    @Inject(method = "tick", at = @At("HEAD"))
    private void qrl$drainDirtyEntriesSafely(CallbackInfo ci) {
        if (!this.dirtyEntries.isEmpty()) {
            for (PlayerChunkMapEntry entry : new ArrayList<PlayerChunkMapEntry>(this.dirtyEntries)) {
                entry.update();
            }
            this.dirtyEntries.clear();
        }
    }
}
