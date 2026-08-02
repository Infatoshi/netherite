package com.microsoft.Malmo.Mixins;

import java.io.FileWriter;
import java.util.Random;

import org.spongepowered.asm.mixin.Final;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Overwrite;
import org.spongepowered.asm.mixin.Shadow;

import net.minecraft.block.Block;
import net.minecraft.block.BlockBush;
import net.minecraft.block.state.IBlockState;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.EnumSkyBlock;
import net.minecraft.world.World;
import net.minecraft.world.gen.feature.WorldGenBush;

/**
 * Worldgen-flywheel probe: logs every WorldGenBush (mushroom) candidate with the exact
 * populate-time light values java's canBlockStay saw (getLight, raw SKY and BLOCK light,
 * isAreaLoaded(17) gate state) so the C replay's populate-time light emulation can be
 * validated cell-by-cell. RNG-neutral: the generate() body is a verbatim copy of vanilla;
 * logging never touches the Random. Enabled only when run/qrl_bushdbg.txt exists (its
 * content = log path), same sidecar pattern as MixinDoublePlantProbe.
 */
@Mixin(WorldGenBush.class)
public abstract class MixinBushProbe {
    @Shadow @Final private BlockBush block;

    private static FileWriter qbsh$out;
    private static boolean qbsh$checked;

    private static synchronized void qbsh$log(String s) {
        if (!qbsh$checked) {
            qbsh$checked = true;
            try {
                java.io.File f = new java.io.File("qrl_bushdbg.txt");
                if (f.isFile()) {
                    String path = new String(java.nio.file.Files.readAllBytes(f.toPath())).trim();
                    if (!path.isEmpty()) qbsh$out = new FileWriter(path, true);
                }
            } catch (Exception e) { }
        }
        if (qbsh$out == null) return;
        try { qbsh$out.write(s); qbsh$out.write('\n'); qbsh$out.flush(); } catch (Exception e) { }
    }

    private static String qbsh$id(World w, BlockPos p) {
        IBlockState s = w.getBlockState(p);
        return Block.getIdFromBlock(s.getBlock()) + ":" + s.getBlock().getMetaFromState(s);
    }

    /**
     * @author NetheriteMod worldgen flywheel
     * @reason verbatim vanilla body + per-candidate light logging (no RNG change)
     */
    @Overwrite
    public boolean generate(World worldIn, Random rand, BlockPos position) {
        for (int i = 0; i < 64; ++i) {
            BlockPos blockpos = position.add(rand.nextInt(8) - rand.nextInt(8),
                    rand.nextInt(4) - rand.nextInt(4), rand.nextInt(8) - rand.nextInt(8));

            boolean air = worldIn.isAirBlock(blockpos);
            boolean sky = !worldIn.provider.hasNoSky() || blockpos.getY() < worldIn.getHeight() - 1;
            boolean stay = this.block.canBlockStay(worldIn, blockpos, this.block.getDefaultState());
            boolean place = air && sky && stay;
            if (qbsh$out != null || !qbsh$checked) {
                qbsh$log("BSH " + Block.getIdFromBlock(this.block)
                        + " " + blockpos.getX() + " " + blockpos.getY() + " " + blockpos.getZ()
                        + " light=" + worldIn.getLight(blockpos)
                        + " sky=" + worldIn.getLightFor(EnumSkyBlock.SKY, blockpos)
                        + " blk=" + worldIn.getLightFor(EnumSkyBlock.BLOCK, blockpos)
                        + " loaded17=" + (worldIn.isAreaLoaded(blockpos, 17) ? 1 : 0)
                        + " pos=" + qbsh$id(worldIn, blockpos)
                        + " down=" + qbsh$id(worldIn, blockpos.down())
                        + " placed=" + (place ? 1 : 0));
            }
            if (place) {
                worldIn.setBlockState(blockpos, this.block.getDefaultState(), 2);
            }
        }

        return true;
    }
}
