package net.minecraft.util;

import net.minecraft.entity.Entity;

public class MovingObjectPosition
{
    /** What type of ray trace hit was this? 0 = block, 1 = entity */
    public MovingObjectPosition.MovingObjectType typeOfHit;
    /** x coordinate of the block ray traced against */
    public int blockX;
    /** y coordinate of the block ray traced against */
    public int blockY;
    /** z coordinate of the block ray traced against */
    public int blockZ;
    /**
     * Which side was hit. If its -1 then it went the full length of the ray trace. Bottom = 0, Top = 1, East = 2, West
     * = 3, North = 4, South = 5.
     */
    public int sideHit;
    /** The vector position of the hit */
    public Vec3 hitVec;
    /** The hit entity */
    public Entity entityHit;
    private static final String __OBFID = "CL_00000610";

    /** Used to determine what sub-segment is hit */
    public int subHit = -1;

    /** Used to add extra hit info */
    public Object hitInfo = null;

    public MovingObjectPosition(int p_i2303_1_, int p_i2303_2_, int p_i2303_3_, int p_i2303_4_, Vec3 p_i2303_5_)
    {
        this(p_i2303_1_, p_i2303_2_, p_i2303_3_, p_i2303_4_, p_i2303_5_, true);
    }

    public MovingObjectPosition(int p_i45481_1_, int p_i45481_2_, int p_i45481_3_, int p_i45481_4_, Vec3 p_i45481_5_, boolean p_i45481_6_)
    {
        this.typeOfHit = p_i45481_6_ ? MovingObjectPosition.MovingObjectType.BLOCK : MovingObjectPosition.MovingObjectType.MISS;
        this.blockX = p_i45481_1_;
        this.blockY = p_i45481_2_;
        this.blockZ = p_i45481_3_;
        this.sideHit = p_i45481_4_;
        this.hitVec = Vec3.createVectorHelper(p_i45481_5_.xCoord, p_i45481_5_.yCoord, p_i45481_5_.zCoord);
    }

    public MovingObjectPosition(Entity p_i2304_1_)
    {
        this(p_i2304_1_, Vec3.createVectorHelper(p_i2304_1_.posX, p_i2304_1_.posY, p_i2304_1_.posZ));
    }

    public MovingObjectPosition(Entity p_i45482_1_, Vec3 p_i45482_2_)
    {
        this.typeOfHit = MovingObjectPosition.MovingObjectType.ENTITY;
        this.entityHit = p_i45482_1_;
        this.hitVec = p_i45482_2_;
    }

    public String toString()
    {
        return "HitResult{type=" + this.typeOfHit + ", x=" + this.blockX + ", y=" + this.blockY + ", z=" + this.blockZ + ", f=" + this.sideHit + ", pos=" + this.hitVec + ", entity=" + this.entityHit + '}';
    }

    public static enum MovingObjectType
    {
        MISS,
        BLOCK,
        ENTITY;

        private static final String __OBFID = "CL_00000611";
    }
}