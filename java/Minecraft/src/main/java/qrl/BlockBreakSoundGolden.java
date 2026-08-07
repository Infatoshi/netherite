package qrl;

import net.minecraft.block.Block;
import net.minecraft.block.SoundType;
import net.minecraft.block.state.IBlockState;
import net.minecraft.init.Bootstrap;
import net.minecraft.util.ResourceLocation;
import net.minecraft.util.SoundEvent;
import net.minecraft.util.math.BlockPos;

/** Complete 1.11.2 block-id to break/place SoundType oracle. */
public final class BlockBreakSoundGolden {
    private BlockBreakSoundGolden() { }

    public static void main(String[] args) {
        Bootstrap.register();
        for (int id = 0; id <= 255; ++id) {
            Block block = Block.getBlockById(id);
            if (block == null) continue;
            IBlockState state = block.getDefaultState();
            if (state.getMaterial() == net.minecraft.block.material.Material.AIR)
                continue;
            SoundType type = block.getSoundType(
                state, null, BlockPos.ORIGIN, null);
            SoundEvent sound = type.getBreakSound();
            ResourceLocation name =
                SoundEvent.REGISTRY.getNameForObject(sound);
            ResourceLocation placeName =
                SoundEvent.REGISTRY.getNameForObject(type.getPlaceSound());
            float volume = (type.getVolume() + 1.0F) / 2.0F;
            float pitch = type.getPitch() * 0.8F;
            for (IBlockState candidate
                    : block.getBlockState().getValidStates()) {
                SoundType candidateType = block.getSoundType(
                    candidate, null, BlockPos.ORIGIN, null);
                ResourceLocation candidateName =
                    SoundEvent.REGISTRY.getNameForObject(
                        candidateType.getBreakSound());
                ResourceLocation candidatePlaceName =
                    SoundEvent.REGISTRY.getNameForObject(
                        candidateType.getPlaceSound());
                float candidateVolume =
                    (candidateType.getVolume() + 1.0F) / 2.0F;
                float candidatePitch = candidateType.getPitch() * 0.8F;
                if (!name.equals(candidateName)
                        || !placeName.equals(candidatePlaceName)
                        || Float.floatToRawIntBits(volume)
                            != Float.floatToRawIntBits(candidateVolume)
                        || Float.floatToRawIntBits(pitch)
                            != Float.floatToRawIntBits(candidatePitch)) {
                    throw new AssertionError(
                        "state-specific break sound for block " + id);
                }
            }
            System.out.printf("B %d %s %08x %08x%n", id,
                name == null ? "" : name.toString(),
                Float.floatToRawIntBits(volume),
                Float.floatToRawIntBits(pitch));
            System.out.printf("P %d %s %08x %08x%n", id,
                placeName == null ? "" : placeName.toString(),
                Float.floatToRawIntBits(volume),
                Float.floatToRawIntBits(pitch));
        }
    }
}
