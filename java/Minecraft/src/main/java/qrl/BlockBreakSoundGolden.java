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
            ResourceLocation hitName =
                SoundEvent.REGISTRY.getNameForObject(type.getHitSound());
            ResourceLocation fallName =
                SoundEvent.REGISTRY.getNameForObject(type.getFallSound());
            ResourceLocation stepName =
                SoundEvent.REGISTRY.getNameForObject(type.getStepSound());
            float volume = (type.getVolume() + 1.0F) / 2.0F;
            float pitch = type.getPitch() * 0.8F;
            float hitVolume = (type.getVolume() + 1.0F) / 8.0F;
            float hitPitch = type.getPitch() * 0.5F;
            float fallVolume = type.getVolume() * 0.5F;
            float fallPitch = type.getPitch() * 0.75F;
            float stepVolume = type.getVolume() * 0.15F;
            float stepPitch = type.getPitch();
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
                ResourceLocation candidateHitName =
                    SoundEvent.REGISTRY.getNameForObject(
                        candidateType.getHitSound());
                ResourceLocation candidateFallName =
                    SoundEvent.REGISTRY.getNameForObject(
                        candidateType.getFallSound());
                ResourceLocation candidateStepName =
                    SoundEvent.REGISTRY.getNameForObject(
                        candidateType.getStepSound());
                float candidateVolume =
                    (candidateType.getVolume() + 1.0F) / 2.0F;
                float candidatePitch = candidateType.getPitch() * 0.8F;
                float candidateHitVolume =
                    (candidateType.getVolume() + 1.0F) / 8.0F;
                float candidateHitPitch = candidateType.getPitch() * 0.5F;
                float candidateFallVolume = candidateType.getVolume() * 0.5F;
                float candidateFallPitch = candidateType.getPitch() * 0.75F;
                float candidateStepVolume = candidateType.getVolume() * 0.15F;
                float candidateStepPitch = candidateType.getPitch();
                if (!name.equals(candidateName)
                        || !placeName.equals(candidatePlaceName)
                        || !hitName.equals(candidateHitName)
                        || !fallName.equals(candidateFallName)
                        || !stepName.equals(candidateStepName)
                        || Float.floatToRawIntBits(volume)
                            != Float.floatToRawIntBits(candidateVolume)
                        || Float.floatToRawIntBits(pitch)
                            != Float.floatToRawIntBits(candidatePitch)
                        || Float.floatToRawIntBits(hitVolume)
                            != Float.floatToRawIntBits(candidateHitVolume)
                        || Float.floatToRawIntBits(hitPitch)
                            != Float.floatToRawIntBits(candidateHitPitch)
                        || Float.floatToRawIntBits(fallVolume)
                            != Float.floatToRawIntBits(candidateFallVolume)
                        || Float.floatToRawIntBits(fallPitch)
                            != Float.floatToRawIntBits(candidateFallPitch)
                        || Float.floatToRawIntBits(stepVolume)
                            != Float.floatToRawIntBits(candidateStepVolume)
                        || Float.floatToRawIntBits(stepPitch)
                            != Float.floatToRawIntBits(candidateStepPitch)) {
                    throw new AssertionError(
                        "state-specific block sound for block " + id);
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
            System.out.printf("H %d %s %08x %08x%n", id,
                hitName == null ? "" : hitName.toString(),
                Float.floatToRawIntBits(hitVolume),
                Float.floatToRawIntBits(hitPitch));
            System.out.printf("F %d %s %08x %08x%n", id,
                fallName == null ? "" : fallName.toString(),
                Float.floatToRawIntBits(fallVolume),
                Float.floatToRawIntBits(fallPitch));
            System.out.printf("S %d %s %08x %08x%n", id,
                stepName == null ? "" : stepName.toString(),
                Float.floatToRawIntBits(stepVolume),
                Float.floatToRawIntBits(stepPitch));
        }
    }
}
