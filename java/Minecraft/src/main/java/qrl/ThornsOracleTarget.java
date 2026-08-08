package qrl;

import java.util.ArrayList;
import java.util.List;
import net.minecraft.entity.Entity;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.util.DamageSource;
import net.minecraft.world.World;

/** Minimal sink used only to observe real EnchantmentThorns retaliation calls. */
final class ThornsOracleTarget extends Entity {
    final List<Float> damage = new ArrayList<Float>();

    ThornsOracleTarget(World world) {
        super(world);
    }

    @Override
    protected void entityInit() { }

    @Override
    protected void readEntityFromNBT(NBTTagCompound compound) { }

    @Override
    protected void writeEntityToNBT(NBTTagCompound compound) { }

    @Override
    public boolean attackEntityFrom(DamageSource source, float amount) {
        damage.add(Float.valueOf(amount));
        return true;
    }
}
