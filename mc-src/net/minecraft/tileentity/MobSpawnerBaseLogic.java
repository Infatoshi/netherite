package net.minecraft.tileentity;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityList;
import net.minecraft.entity.EntityLiving;
import net.minecraft.entity.EntityLivingBase;
import net.minecraft.entity.IEntityLivingData;
import net.minecraft.nbt.NBTBase;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.nbt.NBTTagList;
import net.minecraft.util.AxisAlignedBB;
import net.minecraft.util.WeightedRandom;
import net.minecraft.world.World;

public abstract class MobSpawnerBaseLogic
{
    /** The delay to spawn. */
    public int spawnDelay = 20;
    private String entityTypeName = "Pig";
    /** List of entities to spawn. */
    private List potentialEntitySpawns;
    private MobSpawnerBaseLogic.WeightedRandomMinecart randomEntity;
    public double field_98287_c;
    public double field_98284_d;
    private int minSpawnDelay = 200;
    private int maxSpawnDelay = 800;
    /** A counter for spawn tries. */
    private int spawnCount = 4;
    private Entity field_98291_j;
    private int maxNearbyEntities = 6;
    /** The distance from which a player activates the spawner. */
    private int activatingRangeFromPlayer = 16;
    /** The range coefficient for spawning entities around. */
    private int spawnRange = 4;
    private static final String __OBFID = "CL_00000129";

    /**
     * Gets the entity name that should be spawned.
     */
    public String getEntityNameToSpawn()
    {
        if (this.getRandomEntity() == null)
        {
            if (this.entityTypeName.equals("Minecart"))
            {
                this.entityTypeName = "MinecartRideable";
            }

            return this.entityTypeName;
        }
        else
        {
            return this.getRandomEntity().entityTypeName;
        }
    }

    public void setEntityName(String p_98272_1_)
    {
        this.entityTypeName = p_98272_1_;
    }

    /**
     * Returns true if there's a player close enough to this mob spawner to activate it.
     */
    public boolean isActivated()
    {
        return this.getSpawnerWorld().getClosestPlayer((double)this.getSpawnerX() + 0.5D, (double)this.getSpawnerY() + 0.5D, (double)this.getSpawnerZ() + 0.5D, (double)this.activatingRangeFromPlayer) != null;
    }

    public void updateSpawner()
    {
        if (this.isActivated())
        {
            double d2;

            if (this.getSpawnerWorld().isRemote)
            {
                double d0 = (double)((float)this.getSpawnerX() + this.getSpawnerWorld().rand.nextFloat());
                double d1 = (double)((float)this.getSpawnerY() + this.getSpawnerWorld().rand.nextFloat());
                d2 = (double)((float)this.getSpawnerZ() + this.getSpawnerWorld().rand.nextFloat());
                this.getSpawnerWorld().spawnParticle("smoke", d0, d1, d2, 0.0D, 0.0D, 0.0D);
                this.getSpawnerWorld().spawnParticle("flame", d0, d1, d2, 0.0D, 0.0D, 0.0D);

                if (this.spawnDelay > 0)
                {
                    --this.spawnDelay;
                }

                this.field_98284_d = this.field_98287_c;
                this.field_98287_c = (this.field_98287_c + (double)(1000.0F / ((float)this.spawnDelay + 200.0F))) % 360.0D;
            }
            else
            {
                if (this.spawnDelay == -1)
                {
                    this.resetTimer();
                }

                if (this.spawnDelay > 0)
                {
                    --this.spawnDelay;
                    return;
                }

                boolean flag = false;

                for (int i = 0; i < this.spawnCount; ++i)
                {
                    Entity entity = EntityList.createEntityByName(this.getEntityNameToSpawn(), this.getSpawnerWorld());

                    if (entity == null)
                    {
                        return;
                    }

                    int j = this.getSpawnerWorld().getEntitiesWithinAABB(entity.getClass(), AxisAlignedBB.getBoundingBox((double)this.getSpawnerX(), (double)this.getSpawnerY(), (double)this.getSpawnerZ(), (double)(this.getSpawnerX() + 1), (double)(this.getSpawnerY() + 1), (double)(this.getSpawnerZ() + 1)).expand((double)(this.spawnRange * 2), 4.0D, (double)(this.spawnRange * 2))).size();

                    if (j >= this.maxNearbyEntities)
                    {
                        this.resetTimer();
                        return;
                    }

                    d2 = (double)this.getSpawnerX() + (this.getSpawnerWorld().rand.nextDouble() - this.getSpawnerWorld().rand.nextDouble()) * (double)this.spawnRange;
                    double d3 = (double)(this.getSpawnerY() + this.getSpawnerWorld().rand.nextInt(3) - 1);
                    double d4 = (double)this.getSpawnerZ() + (this.getSpawnerWorld().rand.nextDouble() - this.getSpawnerWorld().rand.nextDouble()) * (double)this.spawnRange;
                    EntityLiving entityliving = entity instanceof EntityLiving ? (EntityLiving)entity : null;
                    entity.setLocationAndAngles(d2, d3, d4, this.getSpawnerWorld().rand.nextFloat() * 360.0F, 0.0F);

                    if (entityliving == null || entityliving.getCanSpawnHere())
                    {
                        this.func_98265_a(entity);
                        this.getSpawnerWorld().playAuxSFX(2004, this.getSpawnerX(), this.getSpawnerY(), this.getSpawnerZ(), 0);

                        if (entityliving != null)
                        {
                            entityliving.spawnExplosionParticle();
                        }

                        flag = true;
                    }
                }

                if (flag)
                {
                    this.resetTimer();
                }
            }
        }
    }

    public Entity func_98265_a(Entity p_98265_1_)
    {
        if (this.getRandomEntity() != null)
        {
            NBTTagCompound nbttagcompound = new NBTTagCompound();
            p_98265_1_.writeToNBTOptional(nbttagcompound);
            Iterator iterator = this.getRandomEntity().field_98222_b.func_150296_c().iterator();

            while (iterator.hasNext())
            {
                String s = (String)iterator.next();
                NBTBase nbtbase = this.getRandomEntity().field_98222_b.getTag(s);
                nbttagcompound.setTag(s, nbtbase.copy());
            }

            p_98265_1_.readFromNBT(nbttagcompound);

            if (p_98265_1_.worldObj != null)
            {
                p_98265_1_.worldObj.spawnEntityInWorld(p_98265_1_);
            }

            NBTTagCompound nbttagcompound2;

            for (Entity entity1 = p_98265_1_; nbttagcompound.hasKey("Riding", 10); nbttagcompound = nbttagcompound2)
            {
                nbttagcompound2 = nbttagcompound.getCompoundTag("Riding");
                Entity entity2 = EntityList.createEntityByName(nbttagcompound2.getString("id"), p_98265_1_.worldObj);

                if (entity2 != null)
                {
                    NBTTagCompound nbttagcompound1 = new NBTTagCompound();
                    entity2.writeToNBTOptional(nbttagcompound1);
                    Iterator iterator1 = nbttagcompound2.func_150296_c().iterator();

                    while (iterator1.hasNext())
                    {
                        String s1 = (String)iterator1.next();
                        NBTBase nbtbase1 = nbttagcompound2.getTag(s1);
                        nbttagcompound1.setTag(s1, nbtbase1.copy());
                    }

                    entity2.readFromNBT(nbttagcompound1);
                    entity2.setLocationAndAngles(entity1.posX, entity1.posY, entity1.posZ, entity1.rotationYaw, entity1.rotationPitch);

                    if (p_98265_1_.worldObj != null)
                    {
                        p_98265_1_.worldObj.spawnEntityInWorld(entity2);
                    }

                    entity1.mountEntity(entity2);
                }

                entity1 = entity2;
            }
        }
        else if (p_98265_1_ instanceof EntityLivingBase && p_98265_1_.worldObj != null)
        {
            ((EntityLiving)p_98265_1_).onSpawnWithEgg((IEntityLivingData)null);
            this.getSpawnerWorld().spawnEntityInWorld(p_98265_1_);
        }

        return p_98265_1_;
    }

    private void resetTimer()
    {
        if (this.maxSpawnDelay <= this.minSpawnDelay)
        {
            this.spawnDelay = this.minSpawnDelay;
        }
        else
        {
            int i = this.maxSpawnDelay - this.minSpawnDelay;
            this.spawnDelay = this.minSpawnDelay + this.getSpawnerWorld().rand.nextInt(i);
        }

        if (this.potentialEntitySpawns != null && this.potentialEntitySpawns.size() > 0)
        {
            this.setRandomEntity((MobSpawnerBaseLogic.WeightedRandomMinecart)WeightedRandom.getRandomItem(this.getSpawnerWorld().rand, this.potentialEntitySpawns));
        }

        this.func_98267_a(1);
    }

    public void readFromNBT(NBTTagCompound p_98270_1_)
    {
        this.entityTypeName = p_98270_1_.getString("EntityId");
        this.spawnDelay = p_98270_1_.getShort("Delay");

        if (p_98270_1_.hasKey("SpawnPotentials", 9))
        {
            this.potentialEntitySpawns = new ArrayList();
            NBTTagList nbttaglist = p_98270_1_.getTagList("SpawnPotentials", 10);

            for (int i = 0; i < nbttaglist.tagCount(); ++i)
            {
                this.potentialEntitySpawns.add(new MobSpawnerBaseLogic.WeightedRandomMinecart(nbttaglist.getCompoundTagAt(i)));
            }
        }
        else
        {
            this.potentialEntitySpawns = null;
        }

        if (p_98270_1_.hasKey("SpawnData", 10))
        {
            this.setRandomEntity(new MobSpawnerBaseLogic.WeightedRandomMinecart(p_98270_1_.getCompoundTag("SpawnData"), this.entityTypeName));
        }
        else
        {
            this.setRandomEntity((MobSpawnerBaseLogic.WeightedRandomMinecart)null);
        }

        if (p_98270_1_.hasKey("MinSpawnDelay", 99))
        {
            this.minSpawnDelay = p_98270_1_.getShort("MinSpawnDelay");
            this.maxSpawnDelay = p_98270_1_.getShort("MaxSpawnDelay");
            this.spawnCount = p_98270_1_.getShort("SpawnCount");
        }

        if (p_98270_1_.hasKey("MaxNearbyEntities", 99))
        {
            this.maxNearbyEntities = p_98270_1_.getShort("MaxNearbyEntities");
            this.activatingRangeFromPlayer = p_98270_1_.getShort("RequiredPlayerRange");
        }

        if (p_98270_1_.hasKey("SpawnRange", 99))
        {
            this.spawnRange = p_98270_1_.getShort("SpawnRange");
        }

        if (this.getSpawnerWorld() != null && this.getSpawnerWorld().isRemote)
        {
            this.field_98291_j = null;
        }
    }

    public void writeToNBT(NBTTagCompound p_98280_1_)
    {
        p_98280_1_.setString("EntityId", this.getEntityNameToSpawn());
        p_98280_1_.setShort("Delay", (short)this.spawnDelay);
        p_98280_1_.setShort("MinSpawnDelay", (short)this.minSpawnDelay);
        p_98280_1_.setShort("MaxSpawnDelay", (short)this.maxSpawnDelay);
        p_98280_1_.setShort("SpawnCount", (short)this.spawnCount);
        p_98280_1_.setShort("MaxNearbyEntities", (short)this.maxNearbyEntities);
        p_98280_1_.setShort("RequiredPlayerRange", (short)this.activatingRangeFromPlayer);
        p_98280_1_.setShort("SpawnRange", (short)this.spawnRange);

        if (this.getRandomEntity() != null)
        {
            p_98280_1_.setTag("SpawnData", this.getRandomEntity().field_98222_b.copy());
        }

        if (this.getRandomEntity() != null || this.potentialEntitySpawns != null && this.potentialEntitySpawns.size() > 0)
        {
            NBTTagList nbttaglist = new NBTTagList();

            if (this.potentialEntitySpawns != null && this.potentialEntitySpawns.size() > 0)
            {
                Iterator iterator = this.potentialEntitySpawns.iterator();

                while (iterator.hasNext())
                {
                    MobSpawnerBaseLogic.WeightedRandomMinecart weightedrandomminecart = (MobSpawnerBaseLogic.WeightedRandomMinecart)iterator.next();
                    nbttaglist.appendTag(weightedrandomminecart.func_98220_a());
                }
            }
            else
            {
                nbttaglist.appendTag(this.getRandomEntity().func_98220_a());
            }

            p_98280_1_.setTag("SpawnPotentials", nbttaglist);
        }
    }

    /**
     * Sets the delay to minDelay if parameter given is 1, else return false.
     */
    public boolean setDelayToMin(int p_98268_1_)
    {
        if (p_98268_1_ == 1 && this.getSpawnerWorld().isRemote)
        {
            this.spawnDelay = this.minSpawnDelay;
            return true;
        }
        else
        {
            return false;
        }
    }

    @SideOnly(Side.CLIENT)
    public Entity func_98281_h()
    {
        if (this.field_98291_j == null)
        {
            Entity entity = EntityList.createEntityByName(this.getEntityNameToSpawn(), (World)null);
            entity = this.func_98265_a(entity);
            this.field_98291_j = entity;
        }

        return this.field_98291_j;
    }

    public MobSpawnerBaseLogic.WeightedRandomMinecart getRandomEntity()
    {
        return this.randomEntity;
    }

    public void setRandomEntity(MobSpawnerBaseLogic.WeightedRandomMinecart p_98277_1_)
    {
        this.randomEntity = p_98277_1_;
    }

    public abstract void func_98267_a(int p_98267_1_);

    public abstract World getSpawnerWorld();

    public abstract int getSpawnerX();

    public abstract int getSpawnerY();

    public abstract int getSpawnerZ();

    public class WeightedRandomMinecart extends WeightedRandom.Item
    {
        public final NBTTagCompound field_98222_b;
        public final String entityTypeName;
        private static final String __OBFID = "CL_00000130";

        public WeightedRandomMinecart(NBTTagCompound p_i1945_2_)
        {
            super(p_i1945_2_.getInteger("Weight"));
            NBTTagCompound nbttagcompound1 = p_i1945_2_.getCompoundTag("Properties");
            String s = p_i1945_2_.getString("Type");

            if (s.equals("Minecart"))
            {
                if (nbttagcompound1 != null)
                {
                    switch (nbttagcompound1.getInteger("Type"))
                    {
                        case 0:
                            s = "MinecartRideable";
                            break;
                        case 1:
                            s = "MinecartChest";
                            break;
                        case 2:
                            s = "MinecartFurnace";
                    }
                }
                else
                {
                    s = "MinecartRideable";
                }
            }

            this.field_98222_b = nbttagcompound1;
            this.entityTypeName = s;
        }

        public WeightedRandomMinecart(NBTTagCompound p_i1946_2_, String p_i1946_3_)
        {
            super(1);

            if (p_i1946_3_.equals("Minecart"))
            {
                if (p_i1946_2_ != null)
                {
                    switch (p_i1946_2_.getInteger("Type"))
                    {
                        case 0:
                            p_i1946_3_ = "MinecartRideable";
                            break;
                        case 1:
                            p_i1946_3_ = "MinecartChest";
                            break;
                        case 2:
                            p_i1946_3_ = "MinecartFurnace";
                    }
                }
                else
                {
                    p_i1946_3_ = "MinecartRideable";
                }
            }

            this.field_98222_b = p_i1946_2_;
            this.entityTypeName = p_i1946_3_;
        }

        public NBTTagCompound func_98220_a()
        {
            NBTTagCompound nbttagcompound = new NBTTagCompound();
            nbttagcompound.setTag("Properties", this.field_98222_b);
            nbttagcompound.setString("Type", this.entityTypeName);
            nbttagcompound.setInteger("Weight", this.itemWeight);
            return nbttagcompound;
        }
    }
}