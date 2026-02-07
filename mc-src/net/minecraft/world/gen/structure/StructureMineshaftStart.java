package net.minecraft.world.gen.structure;

import java.util.Random;
import net.minecraft.world.World;

public class StructureMineshaftStart extends StructureStart
{
    private static final String __OBFID = "CL_00000450";

    public StructureMineshaftStart() {}

    public StructureMineshaftStart(World p_i2039_1_, Random p_i2039_2_, int p_i2039_3_, int p_i2039_4_)
    {
        super(p_i2039_3_, p_i2039_4_);
        StructureMineshaftPieces.Room room = new StructureMineshaftPieces.Room(0, p_i2039_2_, (p_i2039_3_ << 4) + 2, (p_i2039_4_ << 4) + 2);
        this.components.add(room);
        room.buildComponent(room, this.components, p_i2039_2_);
        this.updateBoundingBox();
        this.markAvailableHeight(p_i2039_1_, p_i2039_2_, 10);
    }
}