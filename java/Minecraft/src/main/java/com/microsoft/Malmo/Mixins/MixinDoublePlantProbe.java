package com.microsoft.Malmo.Mixins;

import java.io.FileWriter;
import java.util.Random;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Overwrite;
import org.spongepowered.asm.mixin.Shadow;

import net.minecraft.block.Block;
import net.minecraft.block.BlockDoublePlant;
import net.minecraft.block.state.IBlockState;
import net.minecraft.init.Blocks;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.World;
import net.minecraft.world.gen.feature.WorldGenDoublePlant;

/**
 * Worldgen-flywheel probe: logs every WorldGenDoublePlant candidate's predicate inputs
 * (block ids at pos / pos.up / pos.down at EVALUATION time) so the C replay can diff the
 * exact populate-time state java saw. RNG-neutral: the generate() body is a verbatim copy
 * of vanilla; logging never touches the Random. Enabled only when run/qrl_dplantdbg.txt
 * exists (its content = log path), same sidecar pattern as WorldGenProbe.
 */
@Mixin(WorldGenDoublePlant.class)
public abstract class MixinDoublePlantProbe {
    @Shadow private BlockDoublePlant.EnumPlantType plantType;

    private static FileWriter qdpl$out;
    private static boolean qdpl$checked;

    private static synchronized void qdpl$log(String s) {
        if (!qdpl$checked) {
            qdpl$checked = true;
            try {
                java.io.File f = new java.io.File("qrl_dplantdbg.txt");
                if (f.isFile()) {
                    String path = new String(java.nio.file.Files.readAllBytes(f.toPath())).trim();
                    if (!path.isEmpty()) qdpl$out = new FileWriter(path, true);
                }
            } catch (Exception e) { }
        }
        if (qdpl$out == null) return;
        try { qdpl$out.write(s); qdpl$out.write('\n'); qdpl$out.flush(); } catch (Exception e) { }
    }

    private static String qdpl$id(World w, BlockPos p) {
        IBlockState s = w.getBlockState(p);
        return Block.getIdFromBlock(s.getBlock()) + ":" + s.getBlock().getMetaFromState(s);
    }

    /**
     * @author NetheriteMod worldgen flywheel
     * @reason verbatim vanilla body + per-candidate predicate logging (no RNG change)
     */
    @Overwrite
    public boolean generate(World worldIn, Random rand, BlockPos position) {
        boolean flag = false;

        for (int i = 0; i < 64; ++i) {
            BlockPos blockpos = position.add(rand.nextInt(8) - rand.nextInt(8),
                    rand.nextInt(4) - rand.nextInt(4), rand.nextInt(8) - rand.nextInt(8));
            boolean air = worldIn.isAirBlock(blockpos);
            boolean sky = !worldIn.provider.hasNoSky() || blockpos.getY() < 254;
            boolean can = air && sky && Blocks.DOUBLE_PLANT.canPlaceBlockAt(worldIn, blockpos);
            qdpl$log("DPL " + blockpos.getX() + " " + blockpos.getY() + " " + blockpos.getZ()
                    + " pos=" + qdpl$id(worldIn, blockpos)
                    + " up=" + qdpl$id(worldIn, blockpos.up())
                    + " down=" + qdpl$id(worldIn, blockpos.down())
                    + " placed=" + (can ? 1 : 0));
            if (can) {
                Blocks.DOUBLE_PLANT.placeAt(worldIn, blockpos, this.plantType, 2);
                flag = true;
            }
        }

        return flag;
    }
}
