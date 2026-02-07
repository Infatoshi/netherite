package net.minecraft.block.material;

public class MaterialPortal extends Material
{
    private static final String __OBFID = "CL_00000545";

    public MaterialPortal(MapColor p_i2118_1_)
    {
        super(p_i2118_1_);
    }

    public boolean isSolid()
    {
        return false;
    }

    /**
     * Will prevent grass from growing on dirt underneath and kill any grass below it if it returns true
     */
    public boolean getCanBlockGrass()
    {
        return false;
    }

    /**
     * Returns if this material is considered solid or not
     */
    public boolean blocksMovement()
    {
        return false;
    }
}