package netherite.oracle;

import net.minecraft.entity.Entity;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.world.World;

/**
 * The entity the move probe drives: a plain 0.6 x 1.8 entity with a 0.5 step
 * height, spawned into a real world so moveEntity sees one, and never ticked.
 * Only moveEntity and setPosition are ever called on it. Nothing in this class
 * touches RNG or world state beyond what Entity already does in its
 * constructor, so it adds nothing to the streams a tape depends on.
 */
public class ProbeEntity extends Entity
{
    public ProbeEntity(World p_i1582_1_)
    {
        super(p_i1582_1_);
        this.setSize(0.6F, 1.8F);
        this.stepHeight = 0.5F;
    }

    protected void entityInit() {}

    /** The vanilla save/load pair, empty: the probe records raw fields instead. */
    protected void readEntityFromNBT(NBTTagCompound p_70037_1_) {}

    protected void writeEntityToNBT(NBTTagCompound p_70014_1_) {}
}