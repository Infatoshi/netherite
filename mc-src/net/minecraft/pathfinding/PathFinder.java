package net.minecraft.pathfinding;

import net.minecraft.block.Block;
import net.minecraft.block.material.Material;
import net.minecraft.entity.Entity;
import net.minecraft.init.Blocks;
import net.minecraft.util.IntHashMap;
import net.minecraft.util.MathHelper;
import net.minecraft.world.IBlockAccess;

public class PathFinder
{
    /** Used to find obstacles */
    private IBlockAccess worldMap;
    /** The path being generated */
    private Path path = new Path();
    /** The points in the path */
    private IntHashMap pointMap = new IntHashMap();
    /** Selection of path points to add to the path */
    private PathPoint[] pathOptions = new PathPoint[32];
    /** should the PathFinder go through wodden door blocks */
    private boolean isWoddenDoorAllowed;
    /** should the PathFinder disregard BlockMovement type materials in its path */
    private boolean isMovementBlockAllowed;
    private boolean isPathingInWater;
    /** tells the FathFinder to not stop pathing underwater */
    private boolean canEntityDrown;
    private static final String __OBFID = "CL_00000576";

    public PathFinder(IBlockAccess p_i2137_1_, boolean p_i2137_2_, boolean p_i2137_3_, boolean p_i2137_4_, boolean p_i2137_5_)
    {
        this.worldMap = p_i2137_1_;
        this.isWoddenDoorAllowed = p_i2137_2_;
        this.isMovementBlockAllowed = p_i2137_3_;
        this.isPathingInWater = p_i2137_4_;
        this.canEntityDrown = p_i2137_5_;
    }

    /**
     * Creates a path from one entity to another within a minimum distance
     */
    public PathEntity createEntityPathTo(Entity p_75856_1_, Entity p_75856_2_, float p_75856_3_)
    {
        return this.createEntityPathTo(p_75856_1_, p_75856_2_.posX, p_75856_2_.boundingBox.minY, p_75856_2_.posZ, p_75856_3_);
    }

    /**
     * Creates a path from an entity to a specified location within a minimum distance
     */
    public PathEntity createEntityPathTo(Entity p_75859_1_, int p_75859_2_, int p_75859_3_, int p_75859_4_, float p_75859_5_)
    {
        return this.createEntityPathTo(p_75859_1_, (double)((float)p_75859_2_ + 0.5F), (double)((float)p_75859_3_ + 0.5F), (double)((float)p_75859_4_ + 0.5F), p_75859_5_);
    }

    /**
     * Internal implementation of creating a path from an entity to a point
     */
    private PathEntity createEntityPathTo(Entity p_75857_1_, double p_75857_2_, double p_75857_4_, double p_75857_6_, float p_75857_8_)
    {
        this.path.clearPath();
        this.pointMap.clearMap();
        boolean flag = this.isPathingInWater;
        int i = MathHelper.floor_double(p_75857_1_.boundingBox.minY + 0.5D);

        if (this.canEntityDrown && p_75857_1_.isInWater())
        {
            i = (int)p_75857_1_.boundingBox.minY;

            for (Block block = this.worldMap.getBlock(MathHelper.floor_double(p_75857_1_.posX), i, MathHelper.floor_double(p_75857_1_.posZ)); block == Blocks.flowing_water || block == Blocks.water; block = this.worldMap.getBlock(MathHelper.floor_double(p_75857_1_.posX), i, MathHelper.floor_double(p_75857_1_.posZ)))
            {
                ++i;
            }

            flag = this.isPathingInWater;
            this.isPathingInWater = false;
        }
        else
        {
            i = MathHelper.floor_double(p_75857_1_.boundingBox.minY + 0.5D);
        }

        PathPoint pathpoint2 = this.openPoint(MathHelper.floor_double(p_75857_1_.boundingBox.minX), i, MathHelper.floor_double(p_75857_1_.boundingBox.minZ));
        PathPoint pathpoint = this.openPoint(MathHelper.floor_double(p_75857_2_ - (double)(p_75857_1_.width / 2.0F)), MathHelper.floor_double(p_75857_4_), MathHelper.floor_double(p_75857_6_ - (double)(p_75857_1_.width / 2.0F)));
        PathPoint pathpoint1 = new PathPoint(MathHelper.floor_float(p_75857_1_.width + 1.0F), MathHelper.floor_float(p_75857_1_.height + 1.0F), MathHelper.floor_float(p_75857_1_.width + 1.0F));
        PathEntity pathentity = this.addToPath(p_75857_1_, pathpoint2, pathpoint, pathpoint1, p_75857_8_);
        this.isPathingInWater = flag;
        return pathentity;
    }

    /**
     * Adds a path from start to end and returns the whole path (args: unused, start, end, unused, maxDistance)
     */
    private PathEntity addToPath(Entity p_75861_1_, PathPoint p_75861_2_, PathPoint p_75861_3_, PathPoint p_75861_4_, float p_75861_5_)
    {
        p_75861_2_.totalPathDistance = 0.0F;
        p_75861_2_.distanceToNext = p_75861_2_.distanceToSquared(p_75861_3_);
        p_75861_2_.distanceToTarget = p_75861_2_.distanceToNext;
        this.path.clearPath();
        this.path.addPoint(p_75861_2_);
        PathPoint pathpoint3 = p_75861_2_;

        while (!this.path.isPathEmpty())
        {
            PathPoint pathpoint4 = this.path.dequeue();

            if (pathpoint4.equals(p_75861_3_))
            {
                return this.createEntityPath(p_75861_2_, p_75861_3_);
            }

            if (pathpoint4.distanceToSquared(p_75861_3_) < pathpoint3.distanceToSquared(p_75861_3_))
            {
                pathpoint3 = pathpoint4;
            }

            pathpoint4.isFirst = true;
            int i = this.findPathOptions(p_75861_1_, pathpoint4, p_75861_4_, p_75861_3_, p_75861_5_);

            for (int j = 0; j < i; ++j)
            {
                PathPoint pathpoint5 = this.pathOptions[j];
                float f1 = pathpoint4.totalPathDistance + pathpoint4.distanceToSquared(pathpoint5);

                if (!pathpoint5.isAssigned() || f1 < pathpoint5.totalPathDistance)
                {
                    pathpoint5.previous = pathpoint4;
                    pathpoint5.totalPathDistance = f1;
                    pathpoint5.distanceToNext = pathpoint5.distanceToSquared(p_75861_3_);

                    if (pathpoint5.isAssigned())
                    {
                        this.path.changeDistance(pathpoint5, pathpoint5.totalPathDistance + pathpoint5.distanceToNext);
                    }
                    else
                    {
                        pathpoint5.distanceToTarget = pathpoint5.totalPathDistance + pathpoint5.distanceToNext;
                        this.path.addPoint(pathpoint5);
                    }
                }
            }
        }

        if (pathpoint3 == p_75861_2_)
        {
            return null;
        }
        else
        {
            return this.createEntityPath(p_75861_2_, pathpoint3);
        }
    }

    /**
     * populates pathOptions with available points and returns the number of options found (args: unused1, currentPoint,
     * unused2, targetPoint, maxDistance)
     */
    private int findPathOptions(Entity p_75860_1_, PathPoint p_75860_2_, PathPoint p_75860_3_, PathPoint p_75860_4_, float p_75860_5_)
    {
        int i = 0;
        byte b0 = 0;

        if (this.getVerticalOffset(p_75860_1_, p_75860_2_.xCoord, p_75860_2_.yCoord + 1, p_75860_2_.zCoord, p_75860_3_) == 1)
        {
            b0 = 1;
        }

        PathPoint pathpoint3 = this.getSafePoint(p_75860_1_, p_75860_2_.xCoord, p_75860_2_.yCoord, p_75860_2_.zCoord + 1, p_75860_3_, b0);
        PathPoint pathpoint4 = this.getSafePoint(p_75860_1_, p_75860_2_.xCoord - 1, p_75860_2_.yCoord, p_75860_2_.zCoord, p_75860_3_, b0);
        PathPoint pathpoint5 = this.getSafePoint(p_75860_1_, p_75860_2_.xCoord + 1, p_75860_2_.yCoord, p_75860_2_.zCoord, p_75860_3_, b0);
        PathPoint pathpoint6 = this.getSafePoint(p_75860_1_, p_75860_2_.xCoord, p_75860_2_.yCoord, p_75860_2_.zCoord - 1, p_75860_3_, b0);

        if (pathpoint3 != null && !pathpoint3.isFirst && pathpoint3.distanceTo(p_75860_4_) < p_75860_5_)
        {
            this.pathOptions[i++] = pathpoint3;
        }

        if (pathpoint4 != null && !pathpoint4.isFirst && pathpoint4.distanceTo(p_75860_4_) < p_75860_5_)
        {
            this.pathOptions[i++] = pathpoint4;
        }

        if (pathpoint5 != null && !pathpoint5.isFirst && pathpoint5.distanceTo(p_75860_4_) < p_75860_5_)
        {
            this.pathOptions[i++] = pathpoint5;
        }

        if (pathpoint6 != null && !pathpoint6.isFirst && pathpoint6.distanceTo(p_75860_4_) < p_75860_5_)
        {
            this.pathOptions[i++] = pathpoint6;
        }

        return i;
    }

    /**
     * Returns a point that the entity can safely move to
     */
    private PathPoint getSafePoint(Entity p_75858_1_, int p_75858_2_, int p_75858_3_, int p_75858_4_, PathPoint p_75858_5_, int p_75858_6_)
    {
        PathPoint pathpoint1 = null;
        int i1 = this.getVerticalOffset(p_75858_1_, p_75858_2_, p_75858_3_, p_75858_4_, p_75858_5_);

        if (i1 == 2)
        {
            return this.openPoint(p_75858_2_, p_75858_3_, p_75858_4_);
        }
        else
        {
            if (i1 == 1)
            {
                pathpoint1 = this.openPoint(p_75858_2_, p_75858_3_, p_75858_4_);
            }

            if (pathpoint1 == null && p_75858_6_ > 0 && i1 != -3 && i1 != -4 && this.getVerticalOffset(p_75858_1_, p_75858_2_, p_75858_3_ + p_75858_6_, p_75858_4_, p_75858_5_) == 1)
            {
                pathpoint1 = this.openPoint(p_75858_2_, p_75858_3_ + p_75858_6_, p_75858_4_);
                p_75858_3_ += p_75858_6_;
            }

            if (pathpoint1 != null)
            {
                int j1 = 0;
                int k1 = 0;

                while (p_75858_3_ > 0)
                {
                    k1 = this.getVerticalOffset(p_75858_1_, p_75858_2_, p_75858_3_ - 1, p_75858_4_, p_75858_5_);

                    if (this.isPathingInWater && k1 == -1)
                    {
                        return null;
                    }

                    if (k1 != 1)
                    {
                        break;
                    }

                    if (j1++ >= p_75858_1_.getMaxSafePointTries())
                    {
                        return null;
                    }

                    --p_75858_3_;

                    if (p_75858_3_ > 0)
                    {
                        pathpoint1 = this.openPoint(p_75858_2_, p_75858_3_, p_75858_4_);
                    }
                }

                if (k1 == -2)
                {
                    return null;
                }
            }

            return pathpoint1;
        }
    }

    /**
     * Returns a mapped point or creates and adds one
     */
    private final PathPoint openPoint(int p_75854_1_, int p_75854_2_, int p_75854_3_)
    {
        int l = PathPoint.makeHash(p_75854_1_, p_75854_2_, p_75854_3_);
        PathPoint pathpoint = (PathPoint)this.pointMap.lookup(l);

        if (pathpoint == null)
        {
            pathpoint = new PathPoint(p_75854_1_, p_75854_2_, p_75854_3_);
            this.pointMap.addKey(l, pathpoint);
        }

        return pathpoint;
    }

    /**
     * Checks if an entity collides with blocks at a position. Returns 1 if clear, 0 for colliding with any solid block,
     * -1 for water(if avoiding water) but otherwise clear, -2 for lava, -3 for fence, -4 for closed trapdoor, 2 if
     * otherwise clear except for open trapdoor or water(if not avoiding)
     */
    public int getVerticalOffset(Entity p_75855_1_, int p_75855_2_, int p_75855_3_, int p_75855_4_, PathPoint p_75855_5_)
    {
        return func_82565_a(p_75855_1_, p_75855_2_, p_75855_3_, p_75855_4_, p_75855_5_, this.isPathingInWater, this.isMovementBlockAllowed, this.isWoddenDoorAllowed);
    }

    public static int func_82565_a(Entity p_82565_0_, int p_82565_1_, int p_82565_2_, int p_82565_3_, PathPoint p_82565_4_, boolean p_82565_5_, boolean p_82565_6_, boolean p_82565_7_)
    {
        boolean flag3 = false;

        for (int l = p_82565_1_; l < p_82565_1_ + p_82565_4_.xCoord; ++l)
        {
            for (int i1 = p_82565_2_; i1 < p_82565_2_ + p_82565_4_.yCoord; ++i1)
            {
                for (int j1 = p_82565_3_; j1 < p_82565_3_ + p_82565_4_.zCoord; ++j1)
                {
                    Block block = p_82565_0_.worldObj.getBlock(l, i1, j1);

                    if (block.getMaterial() != Material.air)
                    {
                        if (block == Blocks.trapdoor)
                        {
                            flag3 = true;
                        }
                        else if (block != Blocks.flowing_water && block != Blocks.water)
                        {
                            if (!p_82565_7_ && block == Blocks.wooden_door)
                            {
                                return 0;
                            }
                        }
                        else
                        {
                            if (p_82565_5_)
                            {
                                return -1;
                            }

                            flag3 = true;
                        }

                        int k1 = block.getRenderType();

                        if (p_82565_0_.worldObj.getBlock(l, i1, j1).getRenderType() == 9)
                        {
                            int j2 = MathHelper.floor_double(p_82565_0_.posX);
                            int l1 = MathHelper.floor_double(p_82565_0_.posY);
                            int i2 = MathHelper.floor_double(p_82565_0_.posZ);

                            if (p_82565_0_.worldObj.getBlock(j2, l1, i2).getRenderType() != 9 && p_82565_0_.worldObj.getBlock(j2, l1 - 1, i2).getRenderType() != 9)
                            {
                                return -3;
                            }
                        }
                        else if (!block.getBlocksMovement(p_82565_0_.worldObj, l, i1, j1) && (!p_82565_6_ || block != Blocks.wooden_door))
                        {
                            if (k1 == 11 || block == Blocks.fence_gate || k1 == 32)
                            {
                                return -3;
                            }

                            if (block == Blocks.trapdoor)
                            {
                                return -4;
                            }

                            Material material = block.getMaterial();

                            if (material != Material.lava)
                            {
                                return 0;
                            }

                            if (!p_82565_0_.handleLavaMovement())
                            {
                                return -2;
                            }
                        }
                    }
                }
            }
        }

        return flag3 ? 2 : 1;
    }

    /**
     * Returns a new PathEntity for a given start and end point
     */
    private PathEntity createEntityPath(PathPoint p_75853_1_, PathPoint p_75853_2_)
    {
        int i = 1;
        PathPoint pathpoint2;

        for (pathpoint2 = p_75853_2_; pathpoint2.previous != null; pathpoint2 = pathpoint2.previous)
        {
            ++i;
        }

        PathPoint[] apathpoint = new PathPoint[i];
        pathpoint2 = p_75853_2_;
        --i;

        for (apathpoint[i] = p_75853_2_; pathpoint2.previous != null; apathpoint[i] = pathpoint2)
        {
            pathpoint2 = pathpoint2.previous;
            --i;
        }

        return new PathEntity(apathpoint);
    }
}