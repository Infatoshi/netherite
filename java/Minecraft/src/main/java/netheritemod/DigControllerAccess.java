package netheritemod;

import net.minecraft.util.math.BlockPos;

/**
 * Duck interface mixed onto {@code PlayerControllerMP} so dig-trace emission can
 * read private controller fields without reflection. Implemented by
 * {@code MixinPlayerControllerDig}.
 */
public interface DigControllerAccess {
    BlockPos dig$currentBlock();
    float dig$curBlockDamageMP();
    int dig$blockHitDelay();
    boolean dig$isHittingBlock();
}
