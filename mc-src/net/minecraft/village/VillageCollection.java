package net.minecraft.village;

import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;
import net.minecraft.block.BlockDoor;
import net.minecraft.init.Blocks;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.nbt.NBTTagList;
import net.minecraft.util.ChunkCoordinates;
import net.minecraft.world.World;
import net.minecraft.world.WorldSavedData;

public class VillageCollection extends WorldSavedData
{
    private World worldObj;
    /**
     * This is a black hole. You can add data to this list through a public interface, but you can't query that
     * information in any way and it's not used internally either.
     */
    private final List villagerPositionsList = new ArrayList();
    private final List newDoors = new ArrayList();
    private final List villageList = new ArrayList();
    private int tickCounter;
    private static final String __OBFID = "CL_00001635";

    public VillageCollection(String p_i1677_1_)
    {
        super(p_i1677_1_);
    }

    public VillageCollection(World p_i1678_1_)
    {
        super("villages");
        this.worldObj = p_i1678_1_;
        this.markDirty();
    }

    public void func_82566_a(World p_82566_1_)
    {
        this.worldObj = p_82566_1_;
        Iterator iterator = this.villageList.iterator();

        while (iterator.hasNext())
        {
            Village village = (Village)iterator.next();
            village.func_82691_a(p_82566_1_);
        }
    }

    /**
     * This is a black hole. You can add data to this list through a public interface, but you can't query that
     * information in any way and it's not used internally either.
     */
    public void addVillagerPosition(int p_75551_1_, int p_75551_2_, int p_75551_3_)
    {
        if (this.villagerPositionsList.size() <= 64)
        {
            if (!this.isVillagerPositionPresent(p_75551_1_, p_75551_2_, p_75551_3_))
            {
                this.villagerPositionsList.add(new ChunkCoordinates(p_75551_1_, p_75551_2_, p_75551_3_));
            }
        }
    }

    /**
     * Runs a single tick for the village collection
     */
    public void tick()
    {
        ++this.tickCounter;
        Iterator iterator = this.villageList.iterator();

        while (iterator.hasNext())
        {
            Village village = (Village)iterator.next();
            village.tick(this.tickCounter);
        }

        this.removeAnnihilatedVillages();
        this.dropOldestVillagerPosition();
        this.addNewDoorsToVillageOrCreateVillage();

        if (this.tickCounter % 400 == 0)
        {
            this.markDirty();
        }
    }

    private void removeAnnihilatedVillages()
    {
        Iterator iterator = this.villageList.iterator();

        while (iterator.hasNext())
        {
            Village village = (Village)iterator.next();

            if (village.isAnnihilated())
            {
                iterator.remove();
                this.markDirty();
            }
        }
    }

    /**
     * Get a list of villages.
     */
    public List getVillageList()
    {
        return this.villageList;
    }

    /**
     * Finds the nearest village, but only the given coordinates are withing it's bounding box plus the given the
     * distance.
     */
    public Village findNearestVillage(int p_75550_1_, int p_75550_2_, int p_75550_3_, int p_75550_4_)
    {
        Village village = null;
        float f = Float.MAX_VALUE;
        Iterator iterator = this.villageList.iterator();

        while (iterator.hasNext())
        {
            Village village1 = (Village)iterator.next();
            float f1 = village1.getCenter().getDistanceSquared(p_75550_1_, p_75550_2_, p_75550_3_);

            if (f1 < f)
            {
                float f2 = (float)(p_75550_4_ + village1.getVillageRadius());

                if (f1 <= f2 * f2)
                {
                    village = village1;
                    f = f1;
                }
            }
        }

        return village;
    }

    private void dropOldestVillagerPosition()
    {
        if (!this.villagerPositionsList.isEmpty())
        {
            this.addUnassignedWoodenDoorsAroundToNewDoorsList((ChunkCoordinates)this.villagerPositionsList.remove(0));
        }
    }

    private void addNewDoorsToVillageOrCreateVillage()
    {
        int i = 0;

        while (i < this.newDoors.size())
        {
            VillageDoorInfo villagedoorinfo = (VillageDoorInfo)this.newDoors.get(i);
            boolean flag = false;
            Iterator iterator = this.villageList.iterator();

            while (true)
            {
                if (iterator.hasNext())
                {
                    Village village = (Village)iterator.next();
                    int j = (int)village.getCenter().getDistanceSquared(villagedoorinfo.posX, villagedoorinfo.posY, villagedoorinfo.posZ);
                    float k = 32f + village.getVillageRadius(); //BugFix: Avoid int wrapping

                    if (j > k * k)
                    {
                        continue;
                    }

                    village.addVillageDoorInfo(villagedoorinfo);
                    flag = true;
                }

                if (!flag)
                {
                    Village village1 = new Village(this.worldObj);
                    village1.addVillageDoorInfo(villagedoorinfo);
                    this.villageList.add(village1);
                    this.markDirty();
                }

                ++i;
                break;
            }
        }

        this.newDoors.clear();
    }

    private void addUnassignedWoodenDoorsAroundToNewDoorsList(ChunkCoordinates p_75546_1_)
    {
        byte b0 = 16;
        byte b1 = 4;
        byte b2 = 16;

        for (int i = p_75546_1_.posX - b0; i < p_75546_1_.posX + b0; ++i)
        {
            for (int j = p_75546_1_.posY - b1; j < p_75546_1_.posY + b1; ++j)
            {
                for (int k = p_75546_1_.posZ - b2; k < p_75546_1_.posZ + b2; ++k)
                {
                    if (this.isWoodenDoorAt(i, j, k))
                    {
                        VillageDoorInfo villagedoorinfo = this.getVillageDoorAt(i, j, k);

                        if (villagedoorinfo == null)
                        {
                            this.addDoorToNewListIfAppropriate(i, j, k);
                        }
                        else
                        {
                            villagedoorinfo.lastActivityTimestamp = this.tickCounter;
                        }
                    }
                }
            }
        }
    }

    private VillageDoorInfo getVillageDoorAt(int p_75547_1_, int p_75547_2_, int p_75547_3_)
    {
        Iterator iterator = this.newDoors.iterator();
        VillageDoorInfo villagedoorinfo;

        do
        {
            if (!iterator.hasNext())
            {
                iterator = this.villageList.iterator();
                VillageDoorInfo villagedoorinfo1;

                do
                {
                    if (!iterator.hasNext())
                    {
                        return null;
                    }

                    Village village = (Village)iterator.next();
                    villagedoorinfo1 = village.getVillageDoorAt(p_75547_1_, p_75547_2_, p_75547_3_);
                }
                while (villagedoorinfo1 == null);

                return villagedoorinfo1;
            }

            villagedoorinfo = (VillageDoorInfo)iterator.next();
        }
        while (villagedoorinfo.posX != p_75547_1_ || villagedoorinfo.posZ != p_75547_3_ || Math.abs(villagedoorinfo.posY - p_75547_2_) > 1);

        return villagedoorinfo;
    }

    private void addDoorToNewListIfAppropriate(int p_75542_1_, int p_75542_2_, int p_75542_3_)
    {
        int l = ((BlockDoor)Blocks.wooden_door).func_150013_e(this.worldObj, p_75542_1_, p_75542_2_, p_75542_3_);
        int i1;
        int j1;

        if (l != 0 && l != 2)
        {
            i1 = 0;

            for (j1 = -5; j1 < 0; ++j1)
            {
                if (this.worldObj.canBlockSeeTheSky(p_75542_1_, p_75542_2_, p_75542_3_ + j1))
                {
                    --i1;
                }
            }

            for (j1 = 1; j1 <= 5; ++j1)
            {
                if (this.worldObj.canBlockSeeTheSky(p_75542_1_, p_75542_2_, p_75542_3_ + j1))
                {
                    ++i1;
                }
            }

            if (i1 != 0)
            {
                this.newDoors.add(new VillageDoorInfo(p_75542_1_, p_75542_2_, p_75542_3_, 0, i1 > 0 ? -2 : 2, this.tickCounter));
            }
        }
        else
        {
            i1 = 0;

            for (j1 = -5; j1 < 0; ++j1)
            {
                if (this.worldObj.canBlockSeeTheSky(p_75542_1_ + j1, p_75542_2_, p_75542_3_))
                {
                    --i1;
                }
            }

            for (j1 = 1; j1 <= 5; ++j1)
            {
                if (this.worldObj.canBlockSeeTheSky(p_75542_1_ + j1, p_75542_2_, p_75542_3_))
                {
                    ++i1;
                }
            }

            if (i1 != 0)
            {
                this.newDoors.add(new VillageDoorInfo(p_75542_1_, p_75542_2_, p_75542_3_, i1 > 0 ? -2 : 2, 0, this.tickCounter));
            }
        }
    }

    private boolean isVillagerPositionPresent(int p_75548_1_, int p_75548_2_, int p_75548_3_)
    {
        Iterator iterator = this.villagerPositionsList.iterator();
        ChunkCoordinates chunkcoordinates;

        do
        {
            if (!iterator.hasNext())
            {
                return false;
            }

            chunkcoordinates = (ChunkCoordinates)iterator.next();
        }
        while (chunkcoordinates.posX != p_75548_1_ || chunkcoordinates.posY != p_75548_2_ || chunkcoordinates.posZ != p_75548_3_);

        return true;
    }

    private boolean isWoodenDoorAt(int p_75541_1_, int p_75541_2_, int p_75541_3_)
    {
        return this.worldObj.getBlock(p_75541_1_, p_75541_2_, p_75541_3_) == Blocks.wooden_door;
    }

    /**
     * reads in data from the NBTTagCompound into this MapDataBase
     */
    public void readFromNBT(NBTTagCompound p_76184_1_)
    {
        this.tickCounter = p_76184_1_.getInteger("Tick");
        NBTTagList nbttaglist = p_76184_1_.getTagList("Villages", 10);

        for (int i = 0; i < nbttaglist.tagCount(); ++i)
        {
            NBTTagCompound nbttagcompound1 = nbttaglist.getCompoundTagAt(i);
            Village village = new Village();
            village.readVillageDataFromNBT(nbttagcompound1);
            this.villageList.add(village);
        }
    }

    /**
     * write data to NBTTagCompound from this MapDataBase, similar to Entities and TileEntities
     */
    public void writeToNBT(NBTTagCompound p_76187_1_)
    {
        p_76187_1_.setInteger("Tick", this.tickCounter);
        NBTTagList nbttaglist = new NBTTagList();
        Iterator iterator = this.villageList.iterator();

        while (iterator.hasNext())
        {
            Village village = (Village)iterator.next();
            NBTTagCompound nbttagcompound1 = new NBTTagCompound();
            village.writeVillageDataToNBT(nbttagcompound1);
            nbttaglist.appendTag(nbttagcompound1);
        }

        p_76187_1_.setTag("Villages", nbttaglist);
    }
}