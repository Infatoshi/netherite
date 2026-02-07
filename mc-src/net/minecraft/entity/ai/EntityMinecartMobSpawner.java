package net.minecraft.entity.ai;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.block.Block;
import net.minecraft.entity.item.EntityMinecart;
import net.minecraft.init.Blocks;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.tileentity.MobSpawnerBaseLogic;
import net.minecraft.util.MathHelper;
import net.minecraft.world.World;

public class EntityMinecartMobSpawner extends EntityMinecart
{
    /** Mob spawner logic for this spawner minecart. */
    private final MobSpawnerBaseLogic mobSpawnerLogic = new MobSpawnerBaseLogic()
    {
        private static final String __OBFID = "CL_00001679";
        public void func_98267_a(int p_98267_1_)
        {
            EntityMinecartMobSpawner.this.worldObj.setEntityState(EntityMinecartMobSpawner.this, (byte)p_98267_1_);
        }
        public World getSpawnerWorld()
        {
            return EntityMinecartMobSpawner.this.worldObj;
        }
        public int getSpawnerX()
        {
            return MathHelper.floor_double(EntityMinecartMobSpawner.this.posX);
        }
        public int getSpawnerY()
        {
            return MathHelper.floor_double(EntityMinecartMobSpawner.this.posY);
        }
        public int getSpawnerZ()
        {
            return MathHelper.floor_double(EntityMinecartMobSpawner.this.posZ);
        }
    };
    private static final String __OBFID = "CL_00001678";

    public EntityMinecartMobSpawner(World p_i1725_1_)
    {
        super(p_i1725_1_);
    }

    public EntityMinecartMobSpawner(World p_i1726_1_, double p_i1726_2_, double p_i1726_4_, double p_i1726_6_)
    {
        super(p_i1726_1_, p_i1726_2_, p_i1726_4_, p_i1726_6_);
    }

    public int getMinecartType()
    {
        return 4;
    }

    public Block func_145817_o()
    {
        return Blocks.mob_spawner;
    }

    /**
     * (abstract) Protected helper method to read subclass entity data from NBT.
     */
    protected void readEntityFromNBT(NBTTagCompound p_70037_1_)
    {
        super.readEntityFromNBT(p_70037_1_);
        this.mobSpawnerLogic.readFromNBT(p_70037_1_);
    }

    /**
     * (abstract) Protected helper method to write subclass entity data to NBT.
     */
    protected void writeEntityToNBT(NBTTagCompound p_70014_1_)
    {
        super.writeEntityToNBT(p_70014_1_);
        this.mobSpawnerLogic.writeToNBT(p_70014_1_);
    }

    @SideOnly(Side.CLIENT)
    public void handleHealthUpdate(byte p_70103_1_)
    {
        this.mobSpawnerLogic.setDelayToMin(p_70103_1_);
    }

    /**
     * Called to update the entity's position/logic.
     */
    public void onUpdate()
    {
        super.onUpdate();
        this.mobSpawnerLogic.updateSpawner();
    }

    @SideOnly(Side.CLIENT)
    public MobSpawnerBaseLogic func_98039_d()
    {
        return this.mobSpawnerLogic;
    }
}