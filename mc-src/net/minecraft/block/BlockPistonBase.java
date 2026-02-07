package net.minecraft.block;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.List;
import net.minecraft.block.material.Material;
import net.minecraft.client.renderer.texture.IIconRegister;
import net.minecraft.creativetab.CreativeTabs;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityLivingBase;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.init.Blocks;
import net.minecraft.item.ItemStack;
import net.minecraft.server.management.PlayerManager;
import net.minecraft.tileentity.TileEntity;
import net.minecraft.tileentity.TileEntityPiston;
import net.minecraft.util.AxisAlignedBB;
import net.minecraft.util.Facing;
import net.minecraft.util.IIcon;
import net.minecraft.util.MathHelper;
import net.minecraft.world.IBlockAccess;
import net.minecraft.world.World;

public class BlockPistonBase extends Block
{
    /** This piston is the sticky one? */
    private final boolean isSticky;
    /** Only visible when piston is extended */
    @SideOnly(Side.CLIENT)
    private IIcon innerTopIcon;
    /** Bottom side texture */
    @SideOnly(Side.CLIENT)
    private IIcon bottomIcon;
    /** Top icon of piston depends on (either sticky or normal) */
    @SideOnly(Side.CLIENT)
    private IIcon topIcon;
    private static final String __OBFID = "CL_00000366";

    public BlockPistonBase(boolean p_i45443_1_)
    {
        super(Material.piston);
        this.isSticky = p_i45443_1_;
        this.setStepSound(soundTypePiston);
        this.setHardness(0.5F);
        this.setCreativeTab(CreativeTabs.tabRedstone);
    }

    @SideOnly(Side.CLIENT)
    public IIcon getPistonExtensionTexture()
    {
        return this.topIcon;
    }

    @SideOnly(Side.CLIENT)
    public void func_150070_b(float p_150070_1_, float p_150070_2_, float p_150070_3_, float p_150070_4_, float p_150070_5_, float p_150070_6_)
    {
        this.setBlockBounds(p_150070_1_, p_150070_2_, p_150070_3_, p_150070_4_, p_150070_5_, p_150070_6_);
    }

    /**
     * The type of render function that is called for this block
     */
    public int getRenderType()
    {
        return 16;
    }

    /**
     * Gets the block's texture. Args: side, meta
     */
    @SideOnly(Side.CLIENT)
    public IIcon getIcon(int p_149691_1_, int p_149691_2_)
    {
        int k = getPistonOrientation(p_149691_2_);
        return k > 5 ? this.topIcon : (p_149691_1_ == k ? (!isExtended(p_149691_2_) && this.minX <= 0.0D && this.minY <= 0.0D && this.minZ <= 0.0D && this.maxX >= 1.0D && this.maxY >= 1.0D && this.maxZ >= 1.0D ? this.topIcon : this.innerTopIcon) : (p_149691_1_ == Facing.oppositeSide[k] ? this.bottomIcon : this.blockIcon));
    }

    @SideOnly(Side.CLIENT)
    public static IIcon getPistonBaseIcon(String p_150074_0_)
    {
        return p_150074_0_ == "piston_side" ? Blocks.piston.blockIcon : (p_150074_0_ == "piston_top_normal" ? Blocks.piston.topIcon : (p_150074_0_ == "piston_top_sticky" ? Blocks.sticky_piston.topIcon : (p_150074_0_ == "piston_inner" ? Blocks.piston.innerTopIcon : null)));
    }

    @SideOnly(Side.CLIENT)
    public void registerBlockIcons(IIconRegister p_149651_1_)
    {
        this.blockIcon = p_149651_1_.registerIcon("piston_side");
        this.topIcon = p_149651_1_.registerIcon(this.isSticky ? "piston_top_sticky" : "piston_top_normal");
        this.innerTopIcon = p_149651_1_.registerIcon("piston_inner");
        this.bottomIcon = p_149651_1_.registerIcon("piston_bottom");
    }

    /**
     * Is this block (a) opaque and (b) a full 1m cube?  This determines whether or not to render the shared face of two
     * adjacent blocks and also whether the player can attach torches, redstone wire, etc to this block.
     */
    public boolean isOpaqueCube()
    {
        return false;
    }

    /**
     * Called upon block activation (right click on the block.)
     */
    public boolean onBlockActivated(World p_149727_1_, int p_149727_2_, int p_149727_3_, int p_149727_4_, EntityPlayer p_149727_5_, int p_149727_6_, float p_149727_7_, float p_149727_8_, float p_149727_9_)
    {
        return false;
    }

    /**
     * Called when the block is placed in the world.
     */
    public void onBlockPlacedBy(World p_149689_1_, int p_149689_2_, int p_149689_3_, int p_149689_4_, EntityLivingBase p_149689_5_, ItemStack p_149689_6_)
    {
        int l = determineOrientation(p_149689_1_, p_149689_2_, p_149689_3_, p_149689_4_, p_149689_5_);
        p_149689_1_.setBlockMetadataWithNotify(p_149689_2_, p_149689_3_, p_149689_4_, l, 2);

        if (!p_149689_1_.isRemote)
        {
            this.updatePistonState(p_149689_1_, p_149689_2_, p_149689_3_, p_149689_4_);
        }
    }

    /**
     * Lets the block know when one of its neighbor changes. Doesn't know which neighbor changed (coordinates passed are
     * their own) Args: x, y, z, neighbor Block
     */
    public void onNeighborBlockChange(World p_149695_1_, int p_149695_2_, int p_149695_3_, int p_149695_4_, Block p_149695_5_)
    {
        if (!p_149695_1_.isRemote)
        {
            this.updatePistonState(p_149695_1_, p_149695_2_, p_149695_3_, p_149695_4_);
        }
    }

    /**
     * Called whenever the block is added into the world. Args: world, x, y, z
     */
    public void onBlockAdded(World p_149726_1_, int p_149726_2_, int p_149726_3_, int p_149726_4_)
    {
        if (!p_149726_1_.isRemote && p_149726_1_.getTileEntity(p_149726_2_, p_149726_3_, p_149726_4_) == null)
        {
            this.updatePistonState(p_149726_1_, p_149726_2_, p_149726_3_, p_149726_4_);
        }
    }

    /**
     * handles attempts to extend or retract the piston.
     */
    private void updatePistonState(World p_150078_1_, int p_150078_2_, int p_150078_3_, int p_150078_4_)
    {
        int l = p_150078_1_.getBlockMetadata(p_150078_2_, p_150078_3_, p_150078_4_);
        int i1 = getPistonOrientation(l);

        if (i1 != 7)
        {
            boolean flag = this.isIndirectlyPowered(p_150078_1_, p_150078_2_, p_150078_3_, p_150078_4_, i1);

            if (flag && !isExtended(l))
            {
                if (canExtend(p_150078_1_, p_150078_2_, p_150078_3_, p_150078_4_, i1))
                {
                    p_150078_1_.addBlockEvent(p_150078_2_, p_150078_3_, p_150078_4_, this, 0, i1);
                }
            }
            else if (!flag && isExtended(l))
            {
                p_150078_1_.setBlockMetadataWithNotify(p_150078_2_, p_150078_3_, p_150078_4_, i1, 2);
                p_150078_1_.addBlockEvent(p_150078_2_, p_150078_3_, p_150078_4_, this, 1, i1);
            }
        }
    }

    private boolean isIndirectlyPowered(World p_150072_1_, int p_150072_2_, int p_150072_3_, int p_150072_4_, int p_150072_5_)
    {
        return p_150072_5_ != 0 && p_150072_1_.getIndirectPowerOutput(p_150072_2_, p_150072_3_ - 1, p_150072_4_, 0) ? true : (p_150072_5_ != 1 && p_150072_1_.getIndirectPowerOutput(p_150072_2_, p_150072_3_ + 1, p_150072_4_, 1) ? true : (p_150072_5_ != 2 && p_150072_1_.getIndirectPowerOutput(p_150072_2_, p_150072_3_, p_150072_4_ - 1, 2) ? true : (p_150072_5_ != 3 && p_150072_1_.getIndirectPowerOutput(p_150072_2_, p_150072_3_, p_150072_4_ + 1, 3) ? true : (p_150072_5_ != 5 && p_150072_1_.getIndirectPowerOutput(p_150072_2_ + 1, p_150072_3_, p_150072_4_, 5) ? true : (p_150072_5_ != 4 && p_150072_1_.getIndirectPowerOutput(p_150072_2_ - 1, p_150072_3_, p_150072_4_, 4) ? true : (p_150072_1_.getIndirectPowerOutput(p_150072_2_, p_150072_3_, p_150072_4_, 0) ? true : (p_150072_1_.getIndirectPowerOutput(p_150072_2_, p_150072_3_ + 2, p_150072_4_, 1) ? true : (p_150072_1_.getIndirectPowerOutput(p_150072_2_, p_150072_3_ + 1, p_150072_4_ - 1, 2) ? true : (p_150072_1_.getIndirectPowerOutput(p_150072_2_, p_150072_3_ + 1, p_150072_4_ + 1, 3) ? true : (p_150072_1_.getIndirectPowerOutput(p_150072_2_ - 1, p_150072_3_ + 1, p_150072_4_, 4) ? true : p_150072_1_.getIndirectPowerOutput(p_150072_2_ + 1, p_150072_3_ + 1, p_150072_4_, 5)))))))))));
    }

    public boolean onBlockEventReceived(World p_149696_1_, int p_149696_2_, int p_149696_3_, int p_149696_4_, int p_149696_5_, int p_149696_6_)
    {
        if (!p_149696_1_.isRemote)
        {
            boolean flag = this.isIndirectlyPowered(p_149696_1_, p_149696_2_, p_149696_3_, p_149696_4_, p_149696_6_);

            if (flag && p_149696_5_ == 1)
            {
                p_149696_1_.setBlockMetadataWithNotify(p_149696_2_, p_149696_3_, p_149696_4_, p_149696_6_ | 8, 2);
                return false;
            }

            if (!flag && p_149696_5_ == 0)
            {
                return false;
            }
        }

        if (p_149696_5_ == 0)
        {
            if (!this.tryExtend(p_149696_1_, p_149696_2_, p_149696_3_, p_149696_4_, p_149696_6_))
            {
                return false;
            }

            p_149696_1_.setBlockMetadataWithNotify(p_149696_2_, p_149696_3_, p_149696_4_, p_149696_6_ | 8, 2);
            p_149696_1_.playSoundEffect((double)p_149696_2_ + 0.5D, (double)p_149696_3_ + 0.5D, (double)p_149696_4_ + 0.5D, "tile.piston.out", 0.5F, p_149696_1_.rand.nextFloat() * 0.25F + 0.6F);
        }
        else if (p_149696_5_ == 1)
        {
            TileEntity tileentity1 = p_149696_1_.getTileEntity(p_149696_2_ + Facing.offsetsXForSide[p_149696_6_], p_149696_3_ + Facing.offsetsYForSide[p_149696_6_], p_149696_4_ + Facing.offsetsZForSide[p_149696_6_]);

            if (tileentity1 instanceof TileEntityPiston)
            {
                ((TileEntityPiston)tileentity1).clearPistonTileEntity();
            }

            p_149696_1_.setBlock(p_149696_2_, p_149696_3_, p_149696_4_, Blocks.piston_extension, p_149696_6_, 3);
            p_149696_1_.setTileEntity(p_149696_2_, p_149696_3_, p_149696_4_, BlockPistonMoving.getTileEntity(this, p_149696_6_, p_149696_6_, false, true));

            if (this.isSticky)
            {
                int j1 = p_149696_2_ + Facing.offsetsXForSide[p_149696_6_] * 2;
                int k1 = p_149696_3_ + Facing.offsetsYForSide[p_149696_6_] * 2;
                int l1 = p_149696_4_ + Facing.offsetsZForSide[p_149696_6_] * 2;
                Block block = p_149696_1_.getBlock(j1, k1, l1);
                int i2 = p_149696_1_.getBlockMetadata(j1, k1, l1);
                boolean flag1 = false;

                if (block == Blocks.piston_extension)
                {
                    TileEntity tileentity = p_149696_1_.getTileEntity(j1, k1, l1);

                    if (tileentity instanceof TileEntityPiston)
                    {
                        TileEntityPiston tileentitypiston = (TileEntityPiston)tileentity;

                        if (tileentitypiston.getPistonOrientation() == p_149696_6_ && tileentitypiston.isExtending())
                        {
                            tileentitypiston.clearPistonTileEntity();
                            block = tileentitypiston.getStoredBlockID();
                            i2 = tileentitypiston.getBlockMetadata();
                            flag1 = true;
                        }
                    }
                }

                if (!flag1 && block.getMaterial() != Material.air && canPushBlock(block, p_149696_1_, j1, k1, l1, false) && (block.getMobilityFlag() == 0 || block == Blocks.piston || block == Blocks.sticky_piston))
                {
                    p_149696_2_ += Facing.offsetsXForSide[p_149696_6_];
                    p_149696_3_ += Facing.offsetsYForSide[p_149696_6_];
                    p_149696_4_ += Facing.offsetsZForSide[p_149696_6_];
                    p_149696_1_.setBlock(p_149696_2_, p_149696_3_, p_149696_4_, Blocks.piston_extension, i2, 3);
                    p_149696_1_.setTileEntity(p_149696_2_, p_149696_3_, p_149696_4_, BlockPistonMoving.getTileEntity(block, i2, p_149696_6_, false, false));
                    p_149696_1_.setBlockToAir(j1, k1, l1);
                }
                else if (!flag1)
                {
                    p_149696_1_.setBlockToAir(p_149696_2_ + Facing.offsetsXForSide[p_149696_6_], p_149696_3_ + Facing.offsetsYForSide[p_149696_6_], p_149696_4_ + Facing.offsetsZForSide[p_149696_6_]);
                }
            }
            else
            {
                p_149696_1_.setBlockToAir(p_149696_2_ + Facing.offsetsXForSide[p_149696_6_], p_149696_3_ + Facing.offsetsYForSide[p_149696_6_], p_149696_4_ + Facing.offsetsZForSide[p_149696_6_]);
            }

            p_149696_1_.playSoundEffect((double)p_149696_2_ + 0.5D, (double)p_149696_3_ + 0.5D, (double)p_149696_4_ + 0.5D, "tile.piston.in", 0.5F, p_149696_1_.rand.nextFloat() * 0.15F + 0.6F);
        }

        return true;
    }

    /**
     * Updates the blocks bounds based on its current state. Args: world, x, y, z
     */
    public void setBlockBoundsBasedOnState(IBlockAccess p_149719_1_, int p_149719_2_, int p_149719_3_, int p_149719_4_)
    {
        int l = p_149719_1_.getBlockMetadata(p_149719_2_, p_149719_3_, p_149719_4_);

        if (isExtended(l))
        {
            float f = 0.25F;

            switch (getPistonOrientation(l))
            {
                case 0:
                    this.setBlockBounds(0.0F, 0.25F, 0.0F, 1.0F, 1.0F, 1.0F);
                    break;
                case 1:
                    this.setBlockBounds(0.0F, 0.0F, 0.0F, 1.0F, 0.75F, 1.0F);
                    break;
                case 2:
                    this.setBlockBounds(0.0F, 0.0F, 0.25F, 1.0F, 1.0F, 1.0F);
                    break;
                case 3:
                    this.setBlockBounds(0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 0.75F);
                    break;
                case 4:
                    this.setBlockBounds(0.25F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F);
                    break;
                case 5:
                    this.setBlockBounds(0.0F, 0.0F, 0.0F, 0.75F, 1.0F, 1.0F);
            }
        }
        else
        {
            this.setBlockBounds(0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F);
        }
    }

    /**
     * Sets the block's bounds for rendering it as an item
     */
    public void setBlockBoundsForItemRender()
    {
        this.setBlockBounds(0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F);
    }

    /**
     * Adds all intersecting collision boxes to a list. (Be sure to only add boxes to the list if they intersect the
     * mask.) Parameters: World, X, Y, Z, mask, list, colliding entity
     */
    public void addCollisionBoxesToList(World p_149743_1_, int p_149743_2_, int p_149743_3_, int p_149743_4_, AxisAlignedBB p_149743_5_, List p_149743_6_, Entity p_149743_7_)
    {
        this.setBlockBounds(0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F);
        super.addCollisionBoxesToList(p_149743_1_, p_149743_2_, p_149743_3_, p_149743_4_, p_149743_5_, p_149743_6_, p_149743_7_);
    }

    /**
     * Returns a bounding box from the pool of bounding boxes (this means this box can change after the pool has been
     * cleared to be reused)
     */
    public AxisAlignedBB getCollisionBoundingBoxFromPool(World p_149668_1_, int p_149668_2_, int p_149668_3_, int p_149668_4_)
    {
        this.setBlockBoundsBasedOnState(p_149668_1_, p_149668_2_, p_149668_3_, p_149668_4_);
        return super.getCollisionBoundingBoxFromPool(p_149668_1_, p_149668_2_, p_149668_3_, p_149668_4_);
    }

    /**
     * If this block doesn't render as an ordinary block it will return False (examples: signs, buttons, stairs, etc)
     */
    public boolean renderAsNormalBlock()
    {
        return false;
    }

    public static int getPistonOrientation(int p_150076_0_)
    {
        return p_150076_0_ & 7;
    }

    /**
     * Determine if the metadata is related to something powered.
     */
    public static boolean isExtended(int p_150075_0_)
    {
        return (p_150075_0_ & 8) != 0;
    }

    /**
     * gets the way this piston should face for that entity that placed it.
     */
    public static int determineOrientation(World p_150071_0_, int p_150071_1_, int p_150071_2_, int p_150071_3_, EntityLivingBase p_150071_4_)
    {
        if (MathHelper.abs((float)p_150071_4_.posX - (float)p_150071_1_) < 2.0F && MathHelper.abs((float)p_150071_4_.posZ - (float)p_150071_3_) < 2.0F)
        {
            double d0 = p_150071_4_.posY + 1.82D - (double)p_150071_4_.yOffset;

            if (d0 - (double)p_150071_2_ > 2.0D)
            {
                return 1;
            }

            if ((double)p_150071_2_ - d0 > 0.0D)
            {
                return 0;
            }
        }

        int l = MathHelper.floor_double((double)(p_150071_4_.rotationYaw * 4.0F / 360.0F) + 0.5D) & 3;
        return l == 0 ? 2 : (l == 1 ? 5 : (l == 2 ? 3 : (l == 3 ? 4 : 0)));
    }

    /**
     * returns true if the piston can push the specified block
     */
    private static boolean canPushBlock(Block p_150080_0_, World p_150080_1_, int p_150080_2_, int p_150080_3_, int p_150080_4_, boolean p_150080_5_)
    {
        if (p_150080_0_ == Blocks.obsidian)
        {
            return false;
        }
        else
        {
            if (p_150080_0_ != Blocks.piston && p_150080_0_ != Blocks.sticky_piston)
            {
                if (p_150080_0_.getBlockHardness(p_150080_1_, p_150080_2_, p_150080_3_, p_150080_4_) == -1.0F)
                {
                    return false;
                }

                if (p_150080_0_.getMobilityFlag() == 2)
                {
                    return false;
                }

                if (p_150080_0_.getMobilityFlag() == 1)
                {
                    if (!p_150080_5_)
                    {
                        return false;
                    }

                    return true;
                }
            }
            else if (isExtended(p_150080_1_.getBlockMetadata(p_150080_2_, p_150080_3_, p_150080_4_)))
            {
                return false;
            }

            return !(p_150080_1_.getBlock(p_150080_2_, p_150080_3_, p_150080_4_).hasTileEntity(p_150080_1_.getBlockMetadata(p_150080_2_, p_150080_3_, p_150080_4_)));
            
        }
    }

    /**
     * checks to see if this piston could push the blocks in front of it.
     */
    private static boolean canExtend(World p_150077_0_, int p_150077_1_, int p_150077_2_, int p_150077_3_, int p_150077_4_)
    {
        int i1 = p_150077_1_ + Facing.offsetsXForSide[p_150077_4_];
        int j1 = p_150077_2_ + Facing.offsetsYForSide[p_150077_4_];
        int k1 = p_150077_3_ + Facing.offsetsZForSide[p_150077_4_];
        int l1 = 0;

        while (true)
        {
            if (l1 < 13)
            {
                if (j1 <= 0 || j1 >= p_150077_0_.getHeight())
                {
                    return false;
                }

                Block block = p_150077_0_.getBlock(i1, j1, k1);

                if (!block.isAir(p_150077_0_, i1, j1, k1))
                {
                    if (!canPushBlock(block, p_150077_0_, i1, j1, k1, true))
                    {
                        return false;
                    }

                    if (block.getMobilityFlag() != 1)
                    {
                        if (l1 == 12)
                        {
                            return false;
                        }

                        i1 += Facing.offsetsXForSide[p_150077_4_];
                        j1 += Facing.offsetsYForSide[p_150077_4_];
                        k1 += Facing.offsetsZForSide[p_150077_4_];
                        ++l1;
                        continue;
                    }
                }
            }

            return true;
        }
    }

    /**
     * attempts to extend the piston. returns false if impossible.
     */
    private boolean tryExtend(World p_150079_1_, int p_150079_2_, int p_150079_3_, int p_150079_4_, int p_150079_5_)
    {
        int i1 = p_150079_2_ + Facing.offsetsXForSide[p_150079_5_];
        int j1 = p_150079_3_ + Facing.offsetsYForSide[p_150079_5_];
        int k1 = p_150079_4_ + Facing.offsetsZForSide[p_150079_5_];
        int l1 = 0;

        while (true)
        {
            if (l1 < 13)
            {
                if (j1 <= 0 || j1 >= p_150079_1_.getHeight())
                {
                    return false;
                }

                Block block = p_150079_1_.getBlock(i1, j1, k1);

                if (!block.isAir(p_150079_1_, i1, j1, k1))
                {
                    if (!canPushBlock(block, p_150079_1_, i1, j1, k1, true))
                    {
                        return false;
                    }

                    if (block.getMobilityFlag() != 1)
                    {
                        if (l1 == 12)
                        {
                            return false;
                        }

                        i1 += Facing.offsetsXForSide[p_150079_5_];
                        j1 += Facing.offsetsYForSide[p_150079_5_];
                        k1 += Facing.offsetsZForSide[p_150079_5_];
                        ++l1;
                        continue;
                    }

                    //With our change to how snowballs are dropped this needs to disallow to mimic vanilla behavior.
                    float chance = block instanceof BlockSnow ? -1.0f : 1.0f;
                    block.dropBlockAsItemWithChance(p_150079_1_, i1, j1, k1, p_150079_1_.getBlockMetadata(i1, j1, k1), chance, 0);
                    p_150079_1_.setBlockToAir(i1, j1, k1);
                }
            }

            l1 = i1;
            int k3 = j1;
            int i2 = k1;
            int j2 = 0;
            Block[] ablock;
            int k2;
            int l2;
            int i3;

            for (ablock = new Block[13]; i1 != p_150079_2_ || j1 != p_150079_3_ || k1 != p_150079_4_; k1 = i3)
            {
                k2 = i1 - Facing.offsetsXForSide[p_150079_5_];
                l2 = j1 - Facing.offsetsYForSide[p_150079_5_];
                i3 = k1 - Facing.offsetsZForSide[p_150079_5_];
                Block block1 = p_150079_1_.getBlock(k2, l2, i3);
                int j3 = p_150079_1_.getBlockMetadata(k2, l2, i3);

                if (block1 == this && k2 == p_150079_2_ && l2 == p_150079_3_ && i3 == p_150079_4_)
                {
                    p_150079_1_.setBlock(i1, j1, k1, Blocks.piston_extension, p_150079_5_ | (this.isSticky ? 8 : 0), 4);
                    p_150079_1_.setTileEntity(i1, j1, k1, BlockPistonMoving.getTileEntity(Blocks.piston_head, p_150079_5_ | (this.isSticky ? 8 : 0), p_150079_5_, true, false));
                }
                else
                {
                    p_150079_1_.setBlock(i1, j1, k1, Blocks.piston_extension, j3, 4);
                    p_150079_1_.setTileEntity(i1, j1, k1, BlockPistonMoving.getTileEntity(block1, j3, p_150079_5_, true, false));
                }

                ablock[j2++] = block1;
                i1 = k2;
                j1 = l2;
            }

            i1 = l1;
            j1 = k3;
            k1 = i2;

            for (j2 = 0; i1 != p_150079_2_ || j1 != p_150079_3_ || k1 != p_150079_4_; k1 = i3)
            {
                k2 = i1 - Facing.offsetsXForSide[p_150079_5_];
                l2 = j1 - Facing.offsetsYForSide[p_150079_5_];
                i3 = k1 - Facing.offsetsZForSide[p_150079_5_];
                p_150079_1_.notifyBlocksOfNeighborChange(k2, l2, i3, ablock[j2++]);
                i1 = k2;
                j1 = l2;
            }

            return true;
        }
    }
}