package net.minecraft.block.material;

public class MaterialTransparent extends Material
{
    private static final String __OBFID = "CL_00000540";

    public MaterialTransparent(MapColor p_i2113_1_)
    {
        super(p_i2113_1_);
        this.setReplaceable();
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