package net.minecraft.pathfinding;

import net.minecraft.block.Block;
import net.minecraft.block.material.Material;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityLiving;
import net.minecraft.entity.SharedMonsterAttributes;
import net.minecraft.entity.ai.attributes.IAttributeInstance;
import net.minecraft.entity.monster.EntityZombie;
import net.minecraft.entity.passive.EntityChicken;
import net.minecraft.init.Blocks;
import net.minecraft.util.MathHelper;
import net.minecraft.util.Vec3;
import net.minecraft.world.World;

public class PathNavigate
{
    private EntityLiving theEntity;
    private World worldObj;
    /** The PathEntity being followed. */
    private PathEntity currentPath;
    private double speed;
    /** The number of blocks (extra) +/- in each axis that get pulled out as cache for the pathfinder's search space */
    private IAttributeInstance pathSearchRange;
    private boolean noSunPathfind;
    /** Time, in number of ticks, following the current path */
    private int totalTicks;
    /** The time when the last position check was done (to detect successful movement) */
    private int ticksAtLastPos;
    /** Coordinates of the entity's position last time a check was done (part of monitoring getting 'stuck') */
    private Vec3 lastPosCheck = Vec3.createVectorHelper(0.0D, 0.0D, 0.0D);
    /** Specifically, if a wooden door block is even considered to be passable by the pathfinder */
    private boolean canPassOpenWoodenDoors = true;
    /** If door blocks are considered passable even when closed */
    private boolean canPassClosedWoodenDoors;
    /** If water blocks are avoided (at least by the pathfinder) */
    private boolean avoidsWater;
    /**
     * If the entity can swim. Swimming AI enables this and the pathfinder will also cause the entity to swim straight
     * upwards when underwater
     */
    private boolean canSwim;
    private static final String __OBFID = "CL_00001627";

    public PathNavigate(EntityLiving p_i1671_1_, World p_i1671_2_)
    {
        this.theEntity = p_i1671_1_;
        this.worldObj = p_i1671_2_;
        this.pathSearchRange = p_i1671_1_.getEntityAttribute(SharedMonsterAttributes.followRange);
    }

    public void setAvoidsWater(boolean p_75491_1_)
    {
        this.avoidsWater = p_75491_1_;
    }

    public boolean getAvoidsWater()
    {
        return this.avoidsWater;
    }

    public void setBreakDoors(boolean p_75498_1_)
    {
        this.canPassClosedWoodenDoors = p_75498_1_;
    }

    /**
     * Sets if the entity can enter open doors
     */
    public void setEnterDoors(boolean p_75490_1_)
    {
        this.canPassOpenWoodenDoors = p_75490_1_;
    }

    /**
     * Returns true if the entity can break doors, false otherwise
     */
    public boolean getCanBreakDoors()
    {
        return this.canPassClosedWoodenDoors;
    }

    /**
     * Sets if the path should avoid sunlight
     */
    public void setAvoidSun(boolean p_75504_1_)
    {
        this.noSunPathfind = p_75504_1_;
    }

    /**
     * Sets the speed
     */
    public void setSpeed(double p_75489_1_)
    {
        this.speed = p_75489_1_;
    }

    /**
     * Sets if the entity can swim
     */
    public void setCanSwim(boolean p_75495_1_)
    {
        this.canSwim = p_75495_1_;
    }

    /**
     * Gets the maximum distance that the path finding will search in.
     */
    public float getPathSearchRange()
    {
        return (float)this.pathSearchRange.getAttributeValue();
    }

    /**
     * Returns the path to the given coordinates
     */
    public PathEntity getPathToXYZ(double p_75488_1_, double p_75488_3_, double p_75488_5_)
    {
        return !this.canNavigate() ? null : this.worldObj.getEntityPathToXYZ(this.theEntity, MathHelper.floor_double(p_75488_1_), (int)p_75488_3_, MathHelper.floor_double(p_75488_5_), this.getPathSearchRange(), this.canPassOpenWoodenDoors, this.canPassClosedWoodenDoors, this.avoidsWater, this.canSwim);
    }

    /**
     * Try to find and set a path to XYZ. Returns true if successful.
     */
    public boolean tryMoveToXYZ(double p_75492_1_, double p_75492_3_, double p_75492_5_, double p_75492_7_)
    {
        PathEntity pathentity = this.getPathToXYZ((double)MathHelper.floor_double(p_75492_1_), (double)((int)p_75492_3_), (double)MathHelper.floor_double(p_75492_5_));
        return this.setPath(pathentity, p_75492_7_);
    }

    /**
     * Returns the path to the given EntityLiving
     */
    public PathEntity getPathToEntityLiving(Entity p_75494_1_)
    {
        return !this.canNavigate() ? null : this.worldObj.getPathEntityToEntity(this.theEntity, p_75494_1_, this.getPathSearchRange(), this.canPassOpenWoodenDoors, this.canPassClosedWoodenDoors, this.avoidsWater, this.canSwim);
    }

    /**
     * Try to find and set a path to EntityLiving. Returns true if successful.
     */
    public boolean tryMoveToEntityLiving(Entity p_75497_1_, double p_75497_2_)
    {
        PathEntity pathentity = this.getPathToEntityLiving(p_75497_1_);
        return pathentity != null ? this.setPath(pathentity, p_75497_2_) : false;
    }

    /**
     * sets the active path data if path is 100% unique compared to old path, checks to adjust path for sun avoiding
     * ents and stores end coords
     */
    public boolean setPath(PathEntity p_75484_1_, double p_75484_2_)
    {
        if (p_75484_1_ == null)
        {
            this.currentPath = null;
            return false;
        }
        else
        {
            if (!p_75484_1_.isSamePath(this.currentPath))
            {
                this.currentPath = p_75484_1_;
            }

            if (this.noSunPathfind)
            {
                this.removeSunnyPath();
            }

            if (this.currentPath.getCurrentPathLength() == 0)
            {
                return false;
            }
            else
            {
                this.speed = p_75484_2_;
                Vec3 vec3 = this.getEntityPosition();
                this.ticksAtLastPos = this.totalTicks;
                this.lastPosCheck.xCoord = vec3.xCoord;
                this.lastPosCheck.yCoord = vec3.yCoord;
                this.lastPosCheck.zCoord = vec3.zCoord;
                return true;
            }
        }
    }

    /**
     * gets the actively used PathEntity
     */
    public PathEntity getPath()
    {
        return this.currentPath;
    }

    public void onUpdateNavigation()
    {
        ++this.totalTicks;

        if (!this.noPath())
        {
            if (this.canNavigate())
            {
                this.pathFollow();
            }

            if (!this.noPath())
            {
                Vec3 vec3 = this.currentPath.getPosition(this.theEntity);

                if (vec3 != null)
                {
                    this.theEntity.getMoveHelper().setMoveTo(vec3.xCoord, vec3.yCoord, vec3.zCoord, this.speed);
                }
            }
        }
    }

    private void pathFollow()
    {
        Vec3 vec3 = this.getEntityPosition();
        int i = this.currentPath.getCurrentPathLength();

        for (int j = this.currentPath.getCurrentPathIndex(); j < this.currentPath.getCurrentPathLength(); ++j)
        {
            if (this.currentPath.getPathPointFromIndex(j).yCoord != (int)vec3.yCoord)
            {
                i = j;
                break;
            }
        }

        float f = this.theEntity.width * this.theEntity.width;
        int k;

        for (k = this.currentPath.getCurrentPathIndex(); k < i; ++k)
        {
            if (vec3.squareDistanceTo(this.currentPath.getVectorFromIndex(this.theEntity, k)) < (double)f)
            {
                this.currentPath.setCurrentPathIndex(k + 1);
            }
        }

        k = MathHelper.ceiling_float_int(this.theEntity.width);
        int l = (int)this.theEntity.height + 1;
        int i1 = k;

        for (int j1 = i - 1; j1 >= this.currentPath.getCurrentPathIndex(); --j1)
        {
            if (this.isDirectPathBetweenPoints(vec3, this.currentPath.getVectorFromIndex(this.theEntity, j1), k, l, i1))
            {
                this.currentPath.setCurrentPathIndex(j1);
                break;
            }
        }

        if (this.totalTicks - this.ticksAtLastPos > 100)
        {
            if (vec3.squareDistanceTo(this.lastPosCheck) < 2.25D)
            {
                this.clearPathEntity();
            }

            this.ticksAtLastPos = this.totalTicks;
            this.lastPosCheck.xCoord = vec3.xCoord;
            this.lastPosCheck.yCoord = vec3.yCoord;
            this.lastPosCheck.zCoord = vec3.zCoord;
        }
    }

    /**
     * If null path or reached the end
     */
    public boolean noPath()
    {
        return this.currentPath == null || this.currentPath.isFinished();
    }

    /**
     * sets active PathEntity to null
     */
    public void clearPathEntity()
    {
        this.currentPath = null;
    }

    private Vec3 getEntityPosition()
    {
        return Vec3.createVectorHelper(this.theEntity.posX, (double)this.getPathableYPos(), this.theEntity.posZ);
    }

    /**
     * Gets the safe pathing Y position for the entity depending on if it can path swim or not
     */
    private int getPathableYPos()
    {
        if (this.theEntity.isInWater() && this.canSwim)
        {
            int i = (int)this.theEntity.boundingBox.minY;
            Block block = this.worldObj.getBlock(MathHelper.floor_double(this.theEntity.posX), i, MathHelper.floor_double(this.theEntity.posZ));
            int j = 0;

            do
            {
                if (block != Blocks.flowing_water && block != Blocks.water)
                {
                    return i;
                }

                ++i;
                block = this.worldObj.getBlock(MathHelper.floor_double(this.theEntity.posX), i, MathHelper.floor_double(this.theEntity.posZ));
                ++j;
            }
            while (j <= 16);

            return (int)this.theEntity.boundingBox.minY;
        }
        else
        {
            return (int)(this.theEntity.boundingBox.minY + 0.5D);
        }
    }

    /**
     * If on ground or swimming and can swim
     */
    private boolean canNavigate()
    {
        return this.theEntity.onGround || this.canSwim && this.isInLiquid() || this.theEntity.isRiding() && this.theEntity instanceof EntityZombie && this.theEntity.ridingEntity instanceof EntityChicken;
    }

    /**
     * Returns true if the entity is in water or lava, false otherwise
     */
    private boolean isInLiquid()
    {
        return this.theEntity.isInWater() || this.theEntity.handleLavaMovement();
    }

    /**
     * Trims path data from the end to the first sun covered block
     */
    private void removeSunnyPath()
    {
        if (!this.worldObj.canBlockSeeTheSky(MathHelper.floor_double(this.theEntity.posX), (int)(this.theEntity.boundingBox.minY + 0.5D), MathHelper.floor_double(this.theEntity.posZ)))
        {
            for (int i = 0; i < this.currentPath.getCurrentPathLength(); ++i)
            {
                PathPoint pathpoint = this.currentPath.getPathPointFromIndex(i);

                if (this.worldObj.canBlockSeeTheSky(pathpoint.xCoord, pathpoint.yCoord, pathpoint.zCoord))
                {
                    this.currentPath.setCurrentPathLength(i - 1);
                    return;
                }
            }
        }
    }

    /**
     * Returns true when an entity of specified size could safely walk in a straight line between the two points. Args:
     * pos1, pos2, entityXSize, entityYSize, entityZSize
     */
    private boolean isDirectPathBetweenPoints(Vec3 p_75493_1_, Vec3 p_75493_2_, int p_75493_3_, int p_75493_4_, int p_75493_5_)
    {
        int l = MathHelper.floor_double(p_75493_1_.xCoord);
        int i1 = MathHelper.floor_double(p_75493_1_.zCoord);
        double d0 = p_75493_2_.xCoord - p_75493_1_.xCoord;
        double d1 = p_75493_2_.zCoord - p_75493_1_.zCoord;
        double d2 = d0 * d0 + d1 * d1;

        if (d2 < 1.0E-8D)
        {
            return false;
        }
        else
        {
            double d3 = 1.0D / Math.sqrt(d2);
            d0 *= d3;
            d1 *= d3;
            p_75493_3_ += 2;
            p_75493_5_ += 2;

            if (!this.isSafeToStandAt(l, (int)p_75493_1_.yCoord, i1, p_75493_3_, p_75493_4_, p_75493_5_, p_75493_1_, d0, d1))
            {
                return false;
            }
            else
            {
                p_75493_3_ -= 2;
                p_75493_5_ -= 2;
                double d4 = 1.0D / Math.abs(d0);
                double d5 = 1.0D / Math.abs(d1);
                double d6 = (double)(l * 1) - p_75493_1_.xCoord;
                double d7 = (double)(i1 * 1) - p_75493_1_.zCoord;

                if (d0 >= 0.0D)
                {
                    ++d6;
                }

                if (d1 >= 0.0D)
                {
                    ++d7;
                }

                d6 /= d0;
                d7 /= d1;
                int j1 = d0 < 0.0D ? -1 : 1;
                int k1 = d1 < 0.0D ? -1 : 1;
                int l1 = MathHelper.floor_double(p_75493_2_.xCoord);
                int i2 = MathHelper.floor_double(p_75493_2_.zCoord);
                int j2 = l1 - l;
                int k2 = i2 - i1;

                do
                {
                    if (j2 * j1 <= 0 && k2 * k1 <= 0)
                    {
                        return true;
                    }

                    if (d6 < d7)
                    {
                        d6 += d4;
                        l += j1;
                        j2 = l1 - l;
                    }
                    else
                    {
                        d7 += d5;
                        i1 += k1;
                        k2 = i2 - i1;
                    }
                }
                while (this.isSafeToStandAt(l, (int)p_75493_1_.yCoord, i1, p_75493_3_, p_75493_4_, p_75493_5_, p_75493_1_, d0, d1));

                return false;
            }
        }
    }

    /**
     * Returns true when an entity could stand at a position, including solid blocks under the entire entity. Args:
     * xOffset, yOffset, zOffset, entityXSize, entityYSize, entityZSize, originPosition, vecX, vecZ
     */
    private boolean isSafeToStandAt(int p_75483_1_, int p_75483_2_, int p_75483_3_, int p_75483_4_, int p_75483_5_, int p_75483_6_, Vec3 p_75483_7_, double p_75483_8_, double p_75483_10_)
    {
        int k1 = p_75483_1_ - p_75483_4_ / 2;
        int l1 = p_75483_3_ - p_75483_6_ / 2;

        if (!this.isPositionClear(k1, p_75483_2_, l1, p_75483_4_, p_75483_5_, p_75483_6_, p_75483_7_, p_75483_8_, p_75483_10_))
        {
            return false;
        }
        else
        {
            for (int i2 = k1; i2 < k1 + p_75483_4_; ++i2)
            {
                for (int j2 = l1; j2 < l1 + p_75483_6_; ++j2)
                {
                    double d2 = (double)i2 + 0.5D - p_75483_7_.xCoord;
                    double d3 = (double)j2 + 0.5D - p_75483_7_.zCoord;

                    if (d2 * p_75483_8_ + d3 * p_75483_10_ >= 0.0D)
                    {
                        Block block = this.worldObj.getBlock(i2, p_75483_2_ - 1, j2);
                        Material material = block.getMaterial();

                        if (material == Material.air)
                        {
                            return false;
                        }

                        if (material == Material.water && !this.theEntity.isInWater())
                        {
                            return false;
                        }

                        if (material == Material.lava)
                        {
                            return false;
                        }
                    }
                }
            }

            return true;
        }
    }

    /**
     * Returns true if an entity does not collide with any solid blocks at the position. Args: xOffset, yOffset,
     * zOffset, entityXSize, entityYSize, entityZSize, originPosition, vecX, vecZ
     */
    private boolean isPositionClear(int p_75496_1_, int p_75496_2_, int p_75496_3_, int p_75496_4_, int p_75496_5_, int p_75496_6_, Vec3 p_75496_7_, double p_75496_8_, double p_75496_10_)
    {
        for (int k1 = p_75496_1_; k1 < p_75496_1_ + p_75496_4_; ++k1)
        {
            for (int l1 = p_75496_2_; l1 < p_75496_2_ + p_75496_5_; ++l1)
            {
                for (int i2 = p_75496_3_; i2 < p_75496_3_ + p_75496_6_; ++i2)
                {
                    double d2 = (double)k1 + 0.5D - p_75496_7_.xCoord;
                    double d3 = (double)i2 + 0.5D - p_75496_7_.zCoord;

                    if (d2 * p_75496_8_ + d3 * p_75496_10_ >= 0.0D)
                    {
                        Block block = this.worldObj.getBlock(k1, l1, i2);

                        if (!block.getBlocksMovement(this.worldObj, k1, l1, i2))
                        {
                            return false;
                        }
                    }
                }
            }
        }

        return true;
    }
}