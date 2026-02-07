package net.minecraft.village;

import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;
import java.util.TreeMap;
import net.minecraft.entity.EntityLivingBase;
import net.minecraft.entity.monster.EntityIronGolem;
import net.minecraft.entity.passive.EntityVillager;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.init.Blocks;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.nbt.NBTTagList;
import net.minecraft.util.AxisAlignedBB;
import net.minecraft.util.ChunkCoordinates;
import net.minecraft.util.MathHelper;
import net.minecraft.util.Vec3;
import net.minecraft.world.World;

public class Village
{
    private World worldObj;
    /** list of VillageDoorInfo objects */
    private final List villageDoorInfoList = new ArrayList();
    /**
     * This is the sum of all door coordinates and used to calculate the actual village center by dividing by the number
     * of doors.
     */
    private final ChunkCoordinates centerHelper = new ChunkCoordinates(0, 0, 0);
    /** This is the actual village center. */
    private final ChunkCoordinates center = new ChunkCoordinates(0, 0, 0);
    private int villageRadius;
    private int lastAddDoorTimestamp;
    private int tickCounter;
    private int numVillagers;
    /** Timestamp of tick count when villager last bred */
    private int noBreedTicks;
    /** List of player reputations with this village */
    private TreeMap playerReputation = new TreeMap();
    private List villageAgressors = new ArrayList();
    private int numIronGolems;
    private static final String __OBFID = "CL_00001631";

    public Village() {}

    public Village(World p_i1675_1_)
    {
        this.worldObj = p_i1675_1_;
    }

    public void func_82691_a(World p_82691_1_)
    {
        this.worldObj = p_82691_1_;
    }

    /**
     * Called periodically by VillageCollection
     */
    public void tick(int p_75560_1_)
    {
        this.tickCounter = p_75560_1_;
        this.removeDeadAndOutOfRangeDoors();
        this.removeDeadAndOldAgressors();

        if (p_75560_1_ % 20 == 0)
        {
            this.updateNumVillagers();
        }

        if (p_75560_1_ % 30 == 0)
        {
            this.updateNumIronGolems();
        }

        int j = this.numVillagers / 10;

        if (this.numIronGolems < j && this.villageDoorInfoList.size() > 20 && this.worldObj.rand.nextInt(7000) == 0)
        {
            Vec3 vec3 = this.tryGetIronGolemSpawningLocation(MathHelper.floor_float((float)this.center.posX), MathHelper.floor_float((float)this.center.posY), MathHelper.floor_float((float)this.center.posZ), 2, 4, 2);

            if (vec3 != null)
            {
                EntityIronGolem entityirongolem = new EntityIronGolem(this.worldObj);
                entityirongolem.setPosition(vec3.xCoord, vec3.yCoord, vec3.zCoord);
                this.worldObj.spawnEntityInWorld(entityirongolem);
                ++this.numIronGolems;
            }
        }
    }

    /**
     * Tries up to 10 times to get a valid spawning location before eventually failing and returning null.
     */
    private Vec3 tryGetIronGolemSpawningLocation(int p_75559_1_, int p_75559_2_, int p_75559_3_, int p_75559_4_, int p_75559_5_, int p_75559_6_)
    {
        for (int k1 = 0; k1 < 10; ++k1)
        {
            int l1 = p_75559_1_ + this.worldObj.rand.nextInt(16) - 8;
            int i2 = p_75559_2_ + this.worldObj.rand.nextInt(6) - 3;
            int j2 = p_75559_3_ + this.worldObj.rand.nextInt(16) - 8;

            if (this.isInRange(l1, i2, j2) && this.isValidIronGolemSpawningLocation(l1, i2, j2, p_75559_4_, p_75559_5_, p_75559_6_))
            {
                return Vec3.createVectorHelper((double)l1, (double)i2, (double)j2);
            }
        }

        return null;
    }

    private boolean isValidIronGolemSpawningLocation(int p_75563_1_, int p_75563_2_, int p_75563_3_, int p_75563_4_, int p_75563_5_, int p_75563_6_)
    {
        if (!World.doesBlockHaveSolidTopSurface(this.worldObj, p_75563_1_, p_75563_2_ - 1, p_75563_3_))
        {
            return false;
        }
        else
        {
            int k1 = p_75563_1_ - p_75563_4_ / 2;
            int l1 = p_75563_3_ - p_75563_6_ / 2;

            for (int i2 = k1; i2 < k1 + p_75563_4_; ++i2)
            {
                for (int j2 = p_75563_2_; j2 < p_75563_2_ + p_75563_5_; ++j2)
                {
                    for (int k2 = l1; k2 < l1 + p_75563_6_; ++k2)
                    {
                        if (this.worldObj.getBlock(i2, j2, k2).isNormalCube())
                        {
                            return false;
                        }
                    }
                }
            }

            return true;
        }
    }

    private void updateNumIronGolems()
    {
        List list = this.worldObj.getEntitiesWithinAABB(EntityIronGolem.class, AxisAlignedBB.getBoundingBox((double)(this.center.posX - this.villageRadius), (double)(this.center.posY - 4), (double)(this.center.posZ - this.villageRadius), (double)(this.center.posX + this.villageRadius), (double)(this.center.posY + 4), (double)(this.center.posZ + this.villageRadius)));
        this.numIronGolems = list.size();
    }

    private void updateNumVillagers()
    {
        List list = this.worldObj.getEntitiesWithinAABB(EntityVillager.class, AxisAlignedBB.getBoundingBox((double)(this.center.posX - this.villageRadius), (double)(this.center.posY - 4), (double)(this.center.posZ - this.villageRadius), (double)(this.center.posX + this.villageRadius), (double)(this.center.posY + 4), (double)(this.center.posZ + this.villageRadius)));
        this.numVillagers = list.size();

        if (this.numVillagers == 0)
        {
            this.playerReputation.clear();
        }
    }

    public ChunkCoordinates getCenter()
    {
        return this.center;
    }

    public int getVillageRadius()
    {
        return this.villageRadius;
    }

    /**
     * Actually get num village door info entries, but that boils down to number of doors. Called by
     * EntityAIVillagerMate and VillageSiege
     */
    public int getNumVillageDoors()
    {
        return this.villageDoorInfoList.size();
    }

    public int getTicksSinceLastDoorAdding()
    {
        return this.tickCounter - this.lastAddDoorTimestamp;
    }

    public int getNumVillagers()
    {
        return this.numVillagers;
    }

    /**
     * Returns true, if the given coordinates are within the bounding box of the village.
     */
    public boolean isInRange(int p_75570_1_, int p_75570_2_, int p_75570_3_)
    {
        return this.center.getDistanceSquared(p_75570_1_, p_75570_2_, p_75570_3_) < (float)(this.villageRadius * this.villageRadius);
    }

    /**
     * called only by class EntityAIMoveThroughVillage
     */
    public List getVillageDoorInfoList()
    {
        return this.villageDoorInfoList;
    }

    public VillageDoorInfo findNearestDoor(int p_75564_1_, int p_75564_2_, int p_75564_3_)
    {
        VillageDoorInfo villagedoorinfo = null;
        int l = Integer.MAX_VALUE;
        Iterator iterator = this.villageDoorInfoList.iterator();

        while (iterator.hasNext())
        {
            VillageDoorInfo villagedoorinfo1 = (VillageDoorInfo)iterator.next();
            int i1 = villagedoorinfo1.getDistanceSquared(p_75564_1_, p_75564_2_, p_75564_3_);

            if (i1 < l)
            {
                villagedoorinfo = villagedoorinfo1;
                l = i1;
            }
        }

        return villagedoorinfo;
    }

    /**
     * Find a door suitable for shelter. If there are more doors in a distance of 16 blocks, then the least restricted
     * one (i.e. the one protecting the lowest number of villagers) of them is chosen, else the nearest one regardless
     * of restriction.
     */
    public VillageDoorInfo findNearestDoorUnrestricted(int p_75569_1_, int p_75569_2_, int p_75569_3_)
    {
        VillageDoorInfo villagedoorinfo = null;
        int l = Integer.MAX_VALUE;
        Iterator iterator = this.villageDoorInfoList.iterator();

        while (iterator.hasNext())
        {
            VillageDoorInfo villagedoorinfo1 = (VillageDoorInfo)iterator.next();
            int i1 = villagedoorinfo1.getDistanceSquared(p_75569_1_, p_75569_2_, p_75569_3_);

            if (i1 > 256)
            {
                i1 *= 1000;
            }
            else
            {
                i1 = villagedoorinfo1.getDoorOpeningRestrictionCounter();
            }

            if (i1 < l)
            {
                villagedoorinfo = villagedoorinfo1;
                l = i1;
            }
        }

        return villagedoorinfo;
    }

    public VillageDoorInfo getVillageDoorAt(int p_75578_1_, int p_75578_2_, int p_75578_3_)
    {
        if (this.center.getDistanceSquared(p_75578_1_, p_75578_2_, p_75578_3_) > (float)(this.villageRadius * this.villageRadius))
        {
            return null;
        }
        else
        {
            Iterator iterator = this.villageDoorInfoList.iterator();
            VillageDoorInfo villagedoorinfo;

            do
            {
                if (!iterator.hasNext())
                {
                    return null;
                }

                villagedoorinfo = (VillageDoorInfo)iterator.next();
            }
            while (villagedoorinfo.posX != p_75578_1_ || villagedoorinfo.posZ != p_75578_3_ || Math.abs(villagedoorinfo.posY - p_75578_2_) > 1);

            return villagedoorinfo;
        }
    }

    public void addVillageDoorInfo(VillageDoorInfo p_75576_1_)
    {
        this.villageDoorInfoList.add(p_75576_1_);
        this.centerHelper.posX += p_75576_1_.posX;
        this.centerHelper.posY += p_75576_1_.posY;
        this.centerHelper.posZ += p_75576_1_.posZ;
        this.updateVillageRadiusAndCenter();
        this.lastAddDoorTimestamp = p_75576_1_.lastActivityTimestamp;
    }

    /**
     * Returns true, if there is not a single village door left. Called by VillageCollection
     */
    public boolean isAnnihilated()
    {
        return this.villageDoorInfoList.isEmpty();
    }

    public void addOrRenewAgressor(EntityLivingBase p_75575_1_)
    {
        Iterator iterator = this.villageAgressors.iterator();
        Village.VillageAgressor villageagressor;

        do
        {
            if (!iterator.hasNext())
            {
                this.villageAgressors.add(new Village.VillageAgressor(p_75575_1_, this.tickCounter));
                return;
            }

            villageagressor = (Village.VillageAgressor)iterator.next();
        }
        while (villageagressor.agressor != p_75575_1_);

        villageagressor.agressionTime = this.tickCounter;
    }

    public EntityLivingBase findNearestVillageAggressor(EntityLivingBase p_75571_1_)
    {
        double d0 = Double.MAX_VALUE;
        Village.VillageAgressor villageagressor = null;

        for (int i = 0; i < this.villageAgressors.size(); ++i)
        {
            Village.VillageAgressor villageagressor1 = (Village.VillageAgressor)this.villageAgressors.get(i);
            double d1 = villageagressor1.agressor.getDistanceSqToEntity(p_75571_1_);

            if (d1 <= d0)
            {
                villageagressor = villageagressor1;
                d0 = d1;
            }
        }

        return villageagressor != null ? villageagressor.agressor : null;
    }

    public EntityPlayer func_82685_c(EntityLivingBase p_82685_1_)
    {
        double d0 = Double.MAX_VALUE;
        EntityPlayer entityplayer = null;
        Iterator iterator = this.playerReputation.keySet().iterator();

        while (iterator.hasNext())
        {
            String s = (String)iterator.next();

            if (this.isPlayerReputationTooLow(s))
            {
                EntityPlayer entityplayer1 = this.worldObj.getPlayerEntityByName(s);

                if (entityplayer1 != null)
                {
                    double d1 = entityplayer1.getDistanceSqToEntity(p_82685_1_);

                    if (d1 <= d0)
                    {
                        entityplayer = entityplayer1;
                        d0 = d1;
                    }
                }
            }
        }

        return entityplayer;
    }

    private void removeDeadAndOldAgressors()
    {
        Iterator iterator = this.villageAgressors.iterator();

        while (iterator.hasNext())
        {
            Village.VillageAgressor villageagressor = (Village.VillageAgressor)iterator.next();

            if (!villageagressor.agressor.isEntityAlive() || Math.abs(this.tickCounter - villageagressor.agressionTime) > 300)
            {
                iterator.remove();
            }
        }
    }

    private void removeDeadAndOutOfRangeDoors()
    {
        boolean flag = false;
        boolean flag1 = this.worldObj.rand.nextInt(50) == 0;
        Iterator iterator = this.villageDoorInfoList.iterator();

        while (iterator.hasNext())
        {
            VillageDoorInfo villagedoorinfo = (VillageDoorInfo)iterator.next();

            if (flag1)
            {
                villagedoorinfo.resetDoorOpeningRestrictionCounter();
            }

            if (!this.isBlockDoor(villagedoorinfo.posX, villagedoorinfo.posY, villagedoorinfo.posZ) || Math.abs(this.tickCounter - villagedoorinfo.lastActivityTimestamp) > 1200)
            {
                this.centerHelper.posX -= villagedoorinfo.posX;
                this.centerHelper.posY -= villagedoorinfo.posY;
                this.centerHelper.posZ -= villagedoorinfo.posZ;
                flag = true;
                villagedoorinfo.isDetachedFromVillageFlag = true;
                iterator.remove();
            }
        }

        if (flag)
        {
            this.updateVillageRadiusAndCenter();
        }
    }

    private boolean isBlockDoor(int p_75574_1_, int p_75574_2_, int p_75574_3_)
    {
        return this.worldObj.getBlock(p_75574_1_, p_75574_2_, p_75574_3_) == Blocks.wooden_door;
    }

    private void updateVillageRadiusAndCenter()
    {
        int i = this.villageDoorInfoList.size();

        if (i == 0)
        {
            this.center.set(0, 0, 0);
            this.villageRadius = 0;
        }
        else
        {
            this.center.set(this.centerHelper.posX / i, this.centerHelper.posY / i, this.centerHelper.posZ / i);
            int j = 0;
            VillageDoorInfo villagedoorinfo;

            for (Iterator iterator = this.villageDoorInfoList.iterator(); iterator.hasNext(); j = Math.max(villagedoorinfo.getDistanceSquared(this.center.posX, this.center.posY, this.center.posZ), j))
            {
                villagedoorinfo = (VillageDoorInfo)iterator.next();
            }

            this.villageRadius = Math.max(32, (int)Math.sqrt((double)j) + 1);
        }
    }

    /**
     * Return the village reputation for a player
     */
    public int getReputationForPlayer(String p_82684_1_)
    {
        Integer integer = (Integer)this.playerReputation.get(p_82684_1_);
        return integer != null ? integer.intValue() : 0;
    }

    /**
     * Set the village reputation for a player.
     */
    public int setReputationForPlayer(String p_82688_1_, int p_82688_2_)
    {
        int j = this.getReputationForPlayer(p_82688_1_);
        int k = MathHelper.clamp_int(j + p_82688_2_, -30, 10);
        this.playerReputation.put(p_82688_1_, Integer.valueOf(k));
        return k;
    }

    /**
     * Return whether this player has a too low reputation with this village.
     */
    public boolean isPlayerReputationTooLow(String p_82687_1_)
    {
        return this.getReputationForPlayer(p_82687_1_) <= -15;
    }

    /**
     * Read this village's data from NBT.
     */
    public void readVillageDataFromNBT(NBTTagCompound p_82690_1_)
    {
        this.numVillagers = p_82690_1_.getInteger("PopSize");
        this.villageRadius = p_82690_1_.getInteger("Radius");
        this.numIronGolems = p_82690_1_.getInteger("Golems");
        this.lastAddDoorTimestamp = p_82690_1_.getInteger("Stable");
        this.tickCounter = p_82690_1_.getInteger("Tick");
        this.noBreedTicks = p_82690_1_.getInteger("MTick");
        this.center.posX = p_82690_1_.getInteger("CX");
        this.center.posY = p_82690_1_.getInteger("CY");
        this.center.posZ = p_82690_1_.getInteger("CZ");
        this.centerHelper.posX = p_82690_1_.getInteger("ACX");
        this.centerHelper.posY = p_82690_1_.getInteger("ACY");
        this.centerHelper.posZ = p_82690_1_.getInteger("ACZ");
        NBTTagList nbttaglist = p_82690_1_.getTagList("Doors", 10);

        for (int i = 0; i < nbttaglist.tagCount(); ++i)
        {
            NBTTagCompound nbttagcompound1 = nbttaglist.getCompoundTagAt(i);
            VillageDoorInfo villagedoorinfo = new VillageDoorInfo(nbttagcompound1.getInteger("X"), nbttagcompound1.getInteger("Y"), nbttagcompound1.getInteger("Z"), nbttagcompound1.getInteger("IDX"), nbttagcompound1.getInteger("IDZ"), nbttagcompound1.getInteger("TS"));
            this.villageDoorInfoList.add(villagedoorinfo);
        }

        NBTTagList nbttaglist1 = p_82690_1_.getTagList("Players", 10);

        for (int j = 0; j < nbttaglist1.tagCount(); ++j)
        {
            NBTTagCompound nbttagcompound2 = nbttaglist1.getCompoundTagAt(j);
            this.playerReputation.put(nbttagcompound2.getString("Name"), Integer.valueOf(nbttagcompound2.getInteger("S")));
        }
    }

    /**
     * Write this village's data to NBT.
     */
    public void writeVillageDataToNBT(NBTTagCompound p_82689_1_)
    {
        p_82689_1_.setInteger("PopSize", this.numVillagers);
        p_82689_1_.setInteger("Radius", this.villageRadius);
        p_82689_1_.setInteger("Golems", this.numIronGolems);
        p_82689_1_.setInteger("Stable", this.lastAddDoorTimestamp);
        p_82689_1_.setInteger("Tick", this.tickCounter);
        p_82689_1_.setInteger("MTick", this.noBreedTicks);
        p_82689_1_.setInteger("CX", this.center.posX);
        p_82689_1_.setInteger("CY", this.center.posY);
        p_82689_1_.setInteger("CZ", this.center.posZ);
        p_82689_1_.setInteger("ACX", this.centerHelper.posX);
        p_82689_1_.setInteger("ACY", this.centerHelper.posY);
        p_82689_1_.setInteger("ACZ", this.centerHelper.posZ);
        NBTTagList nbttaglist = new NBTTagList();
        Iterator iterator = this.villageDoorInfoList.iterator();

        while (iterator.hasNext())
        {
            VillageDoorInfo villagedoorinfo = (VillageDoorInfo)iterator.next();
            NBTTagCompound nbttagcompound1 = new NBTTagCompound();
            nbttagcompound1.setInteger("X", villagedoorinfo.posX);
            nbttagcompound1.setInteger("Y", villagedoorinfo.posY);
            nbttagcompound1.setInteger("Z", villagedoorinfo.posZ);
            nbttagcompound1.setInteger("IDX", villagedoorinfo.insideDirectionX);
            nbttagcompound1.setInteger("IDZ", villagedoorinfo.insideDirectionZ);
            nbttagcompound1.setInteger("TS", villagedoorinfo.lastActivityTimestamp);
            nbttaglist.appendTag(nbttagcompound1);
        }

        p_82689_1_.setTag("Doors", nbttaglist);
        NBTTagList nbttaglist1 = new NBTTagList();
        Iterator iterator1 = this.playerReputation.keySet().iterator();

        while (iterator1.hasNext())
        {
            String s = (String)iterator1.next();
            NBTTagCompound nbttagcompound2 = new NBTTagCompound();
            nbttagcompound2.setString("Name", s);
            nbttagcompound2.setInteger("S", ((Integer)this.playerReputation.get(s)).intValue());
            nbttaglist1.appendTag(nbttagcompound2);
        }

        p_82689_1_.setTag("Players", nbttaglist1);
    }

    /**
     * Prevent villager breeding for a fixed interval of time
     */
    public void endMatingSeason()
    {
        this.noBreedTicks = this.tickCounter;
    }

    /**
     * Return whether villagers mating refractory period has passed
     */
    public boolean isMatingSeason()
    {
        return this.noBreedTicks == 0 || this.tickCounter - this.noBreedTicks >= 3600;
    }

    public void setDefaultPlayerReputation(int p_82683_1_)
    {
        Iterator iterator = this.playerReputation.keySet().iterator();

        while (iterator.hasNext())
        {
            String s = (String)iterator.next();
            this.setReputationForPlayer(s, p_82683_1_);
        }
    }

    class VillageAgressor
    {
        public EntityLivingBase agressor;
        public int agressionTime;
        private static final String __OBFID = "CL_00001632";

        VillageAgressor(EntityLivingBase p_i1674_2_, int p_i1674_3_)
        {
            this.agressor = p_i1674_2_;
            this.agressionTime = p_i1674_3_;
        }
    }
}