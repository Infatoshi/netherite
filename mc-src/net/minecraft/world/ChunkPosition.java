package net.minecraft.world;

import net.minecraft.util.MathHelper;
import net.minecraft.util.Vec3;

public class ChunkPosition
{
    public final int chunkPosX;
    public final int chunkPosY;
    public final int chunkPosZ;
    private static final String __OBFID = "CL_00000132";

    public ChunkPosition(int p_i45363_1_, int p_i45363_2_, int p_i45363_3_)
    {
        this.chunkPosX = p_i45363_1_;
        this.chunkPosY = p_i45363_2_;
        this.chunkPosZ = p_i45363_3_;
    }

    public ChunkPosition(Vec3 p_i45364_1_)
    {
        this(MathHelper.floor_double(p_i45364_1_.xCoord), MathHelper.floor_double(p_i45364_1_.yCoord), MathHelper.floor_double(p_i45364_1_.zCoord));
    }

    public boolean equals(Object p_equals_1_)
    {
        if (!(p_equals_1_ instanceof ChunkPosition))
        {
            return false;
        }
        else
        {
            ChunkPosition chunkposition = (ChunkPosition)p_equals_1_;
            return chunkposition.chunkPosX == this.chunkPosX && chunkposition.chunkPosY == this.chunkPosY && chunkposition.chunkPosZ == this.chunkPosZ;
        }
    }

    public int hashCode()
    {
        return this.chunkPosX * 8976890 + this.chunkPosY * 981131 + this.chunkPosZ;
    }
}