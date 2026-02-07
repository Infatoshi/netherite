package net.minecraft.village;

public class VillageDoorInfo
{
    public final int posX;
    public final int posY;
    public final int posZ;
    public final int insideDirectionX;
    public final int insideDirectionZ;
    public int lastActivityTimestamp;
    public boolean isDetachedFromVillageFlag;
    private int doorOpeningRestrictionCounter;
    private static final String __OBFID = "CL_00001630";

    public VillageDoorInfo(int p_i1673_1_, int p_i1673_2_, int p_i1673_3_, int p_i1673_4_, int p_i1673_5_, int p_i1673_6_)
    {
        this.posX = p_i1673_1_;
        this.posY = p_i1673_2_;
        this.posZ = p_i1673_3_;
        this.insideDirectionX = p_i1673_4_;
        this.insideDirectionZ = p_i1673_5_;
        this.lastActivityTimestamp = p_i1673_6_;
    }

    /**
     * Returns the squared distance between this door and the given coordinate.
     */
    public int getDistanceSquared(int p_75474_1_, int p_75474_2_, int p_75474_3_)
    {
        int l = p_75474_1_ - this.posX;
        int i1 = p_75474_2_ - this.posY;
        int j1 = p_75474_3_ - this.posZ;
        return l * l + i1 * i1 + j1 * j1;
    }

    /**
     * Get the square of the distance from a location 2 blocks away from the door considered 'inside' and the given
     * arguments
     */
    public int getInsideDistanceSquare(int p_75469_1_, int p_75469_2_, int p_75469_3_)
    {
        int l = p_75469_1_ - this.posX - this.insideDirectionX;
        int i1 = p_75469_2_ - this.posY;
        int j1 = p_75469_3_ - this.posZ - this.insideDirectionZ;
        return l * l + i1 * i1 + j1 * j1;
    }

    public int getInsidePosX()
    {
        return this.posX + this.insideDirectionX;
    }

    public int getInsidePosY()
    {
        return this.posY;
    }

    public int getInsidePosZ()
    {
        return this.posZ + this.insideDirectionZ;
    }

    public boolean isInside(int p_75467_1_, int p_75467_2_)
    {
        int k = p_75467_1_ - this.posX;
        int l = p_75467_2_ - this.posZ;
        return k * this.insideDirectionX + l * this.insideDirectionZ >= 0;
    }

    public void resetDoorOpeningRestrictionCounter()
    {
        this.doorOpeningRestrictionCounter = 0;
    }

    public void incrementDoorOpeningRestrictionCounter()
    {
        ++this.doorOpeningRestrictionCounter;
    }

    public int getDoorOpeningRestrictionCounter()
    {
        return this.doorOpeningRestrictionCounter;
    }
}