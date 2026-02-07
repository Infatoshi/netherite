package net.minecraft.world.gen.structure;

import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;
import java.util.Random;
import net.minecraft.init.Blocks;
import net.minecraft.init.Items;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.tileentity.TileEntityMobSpawner;
import net.minecraft.util.WeightedRandomChestContent;
import net.minecraft.world.ChunkPosition;
import net.minecraft.world.World;

import net.minecraftforge.common.ChestGenHooks;
import static net.minecraftforge.common.ChestGenHooks.*;

public class StructureStrongholdPieces
{
    private static final StructureStrongholdPieces.PieceWeight[] pieceWeightArray = new StructureStrongholdPieces.PieceWeight[] {new StructureStrongholdPieces.PieceWeight(StructureStrongholdPieces.Straight.class, 40, 0), new StructureStrongholdPieces.PieceWeight(StructureStrongholdPieces.Prison.class, 5, 5), new StructureStrongholdPieces.PieceWeight(StructureStrongholdPieces.LeftTurn.class, 20, 0), new StructureStrongholdPieces.PieceWeight(StructureStrongholdPieces.RightTurn.class, 20, 0), new StructureStrongholdPieces.PieceWeight(StructureStrongholdPieces.RoomCrossing.class, 10, 6), new StructureStrongholdPieces.PieceWeight(StructureStrongholdPieces.StairsStraight.class, 5, 5), new StructureStrongholdPieces.PieceWeight(StructureStrongholdPieces.Stairs.class, 5, 5), new StructureStrongholdPieces.PieceWeight(StructureStrongholdPieces.Crossing.class, 5, 4), new StructureStrongholdPieces.PieceWeight(StructureStrongholdPieces.ChestCorridor.class, 5, 4), new StructureStrongholdPieces.PieceWeight(StructureStrongholdPieces.Library.class, 10, 2)
    {
        private static final String __OBFID = "CL_00000484";
        public boolean canSpawnMoreStructuresOfType(int p_75189_1_)
        {
            return super.canSpawnMoreStructuresOfType(p_75189_1_) && p_75189_1_ > 4;
        }
    }, new StructureStrongholdPieces.PieceWeight(StructureStrongholdPieces.PortalRoom.class, 20, 1)
    {
        private static final String __OBFID = "CL_00000485";
        public boolean canSpawnMoreStructuresOfType(int p_75189_1_)
        {
            return super.canSpawnMoreStructuresOfType(p_75189_1_) && p_75189_1_ > 5;
        }
    }
                                                                                                                             };
    private static List structurePieceList;
    private static Class strongComponentType;
    static int totalWeight;
    private static final StructureStrongholdPieces.Stones strongholdStones = new StructureStrongholdPieces.Stones(null);
    private static final String __OBFID = "CL_00000483";

    public static void registerStrongholdPieces()
    {
        MapGenStructureIO.func_143031_a(StructureStrongholdPieces.ChestCorridor.class, "SHCC");
        MapGenStructureIO.func_143031_a(StructureStrongholdPieces.Corridor.class, "SHFC");
        MapGenStructureIO.func_143031_a(StructureStrongholdPieces.Crossing.class, "SH5C");
        MapGenStructureIO.func_143031_a(StructureStrongholdPieces.LeftTurn.class, "SHLT");
        MapGenStructureIO.func_143031_a(StructureStrongholdPieces.Library.class, "SHLi");
        MapGenStructureIO.func_143031_a(StructureStrongholdPieces.PortalRoom.class, "SHPR");
        MapGenStructureIO.func_143031_a(StructureStrongholdPieces.Prison.class, "SHPH");
        MapGenStructureIO.func_143031_a(StructureStrongholdPieces.RightTurn.class, "SHRT");
        MapGenStructureIO.func_143031_a(StructureStrongholdPieces.RoomCrossing.class, "SHRC");
        MapGenStructureIO.func_143031_a(StructureStrongholdPieces.Stairs.class, "SHSD");
        MapGenStructureIO.func_143031_a(StructureStrongholdPieces.Stairs2.class, "SHStart");
        MapGenStructureIO.func_143031_a(StructureStrongholdPieces.Straight.class, "SHS");
        MapGenStructureIO.func_143031_a(StructureStrongholdPieces.StairsStraight.class, "SHSSD");
    }

    /**
     * sets up Arrays with the Structure pieces and their weights
     */
    public static void prepareStructurePieces()
    {
        structurePieceList = new ArrayList();
        StructureStrongholdPieces.PieceWeight[] apieceweight = pieceWeightArray;
        int i = apieceweight.length;

        for (int j = 0; j < i; ++j)
        {
            StructureStrongholdPieces.PieceWeight pieceweight = apieceweight[j];
            pieceweight.instancesSpawned = 0;
            structurePieceList.add(pieceweight);
        }

        strongComponentType = null;
    }

    private static boolean canAddStructurePieces()
    {
        boolean flag = false;
        totalWeight = 0;
        StructureStrongholdPieces.PieceWeight pieceweight;

        for (Iterator iterator = structurePieceList.iterator(); iterator.hasNext(); totalWeight += pieceweight.pieceWeight)
        {
            pieceweight = (StructureStrongholdPieces.PieceWeight)iterator.next();

            if (pieceweight.instancesLimit > 0 && pieceweight.instancesSpawned < pieceweight.instancesLimit)
            {
                flag = true;
            }
        }

        return flag;
    }

    /**
     * translates the PieceWeight class to the Component class
     */
    private static StructureStrongholdPieces.Stronghold getStrongholdComponentFromWeightedPiece(Class p_75200_0_, List p_75200_1_, Random p_75200_2_, int p_75200_3_, int p_75200_4_, int p_75200_5_, int p_75200_6_, int p_75200_7_)
    {
        Object object = null;

        if (p_75200_0_ == StructureStrongholdPieces.Straight.class)
        {
            object = StructureStrongholdPieces.Straight.findValidPlacement(p_75200_1_, p_75200_2_, p_75200_3_, p_75200_4_, p_75200_5_, p_75200_6_, p_75200_7_);
        }
        else if (p_75200_0_ == StructureStrongholdPieces.Prison.class)
        {
            object = StructureStrongholdPieces.Prison.findValidPlacement(p_75200_1_, p_75200_2_, p_75200_3_, p_75200_4_, p_75200_5_, p_75200_6_, p_75200_7_);
        }
        else if (p_75200_0_ == StructureStrongholdPieces.LeftTurn.class)
        {
            object = StructureStrongholdPieces.LeftTurn.findValidPlacement(p_75200_1_, p_75200_2_, p_75200_3_, p_75200_4_, p_75200_5_, p_75200_6_, p_75200_7_);
        }
        else if (p_75200_0_ == StructureStrongholdPieces.RightTurn.class)
        {
            object = StructureStrongholdPieces.RightTurn.findValidPlacement(p_75200_1_, p_75200_2_, p_75200_3_, p_75200_4_, p_75200_5_, p_75200_6_, p_75200_7_);
        }
        else if (p_75200_0_ == StructureStrongholdPieces.RoomCrossing.class)
        {
            object = StructureStrongholdPieces.RoomCrossing.findValidPlacement(p_75200_1_, p_75200_2_, p_75200_3_, p_75200_4_, p_75200_5_, p_75200_6_, p_75200_7_);
        }
        else if (p_75200_0_ == StructureStrongholdPieces.StairsStraight.class)
        {
            object = StructureStrongholdPieces.StairsStraight.findValidPlacement(p_75200_1_, p_75200_2_, p_75200_3_, p_75200_4_, p_75200_5_, p_75200_6_, p_75200_7_);
        }
        else if (p_75200_0_ == StructureStrongholdPieces.Stairs.class)
        {
            object = StructureStrongholdPieces.Stairs.getStrongholdStairsComponent(p_75200_1_, p_75200_2_, p_75200_3_, p_75200_4_, p_75200_5_, p_75200_6_, p_75200_7_);
        }
        else if (p_75200_0_ == StructureStrongholdPieces.Crossing.class)
        {
            object = StructureStrongholdPieces.Crossing.findValidPlacement(p_75200_1_, p_75200_2_, p_75200_3_, p_75200_4_, p_75200_5_, p_75200_6_, p_75200_7_);
        }
        else if (p_75200_0_ == StructureStrongholdPieces.ChestCorridor.class)
        {
            object = StructureStrongholdPieces.ChestCorridor.findValidPlacement(p_75200_1_, p_75200_2_, p_75200_3_, p_75200_4_, p_75200_5_, p_75200_6_, p_75200_7_);
        }
        else if (p_75200_0_ == StructureStrongholdPieces.Library.class)
        {
            object = StructureStrongholdPieces.Library.findValidPlacement(p_75200_1_, p_75200_2_, p_75200_3_, p_75200_4_, p_75200_5_, p_75200_6_, p_75200_7_);
        }
        else if (p_75200_0_ == StructureStrongholdPieces.PortalRoom.class)
        {
            object = StructureStrongholdPieces.PortalRoom.findValidPlacement(p_75200_1_, p_75200_2_, p_75200_3_, p_75200_4_, p_75200_5_, p_75200_6_, p_75200_7_);
        }

        return (StructureStrongholdPieces.Stronghold)object;
    }

    private static StructureStrongholdPieces.Stronghold getNextComponent(StructureStrongholdPieces.Stairs2 p_75201_0_, List p_75201_1_, Random p_75201_2_, int p_75201_3_, int p_75201_4_, int p_75201_5_, int p_75201_6_, int p_75201_7_)
    {
        if (!canAddStructurePieces())
        {
            return null;
        }
        else
        {
            if (strongComponentType != null)
            {
                StructureStrongholdPieces.Stronghold stronghold = getStrongholdComponentFromWeightedPiece(strongComponentType, p_75201_1_, p_75201_2_, p_75201_3_, p_75201_4_, p_75201_5_, p_75201_6_, p_75201_7_);
                strongComponentType = null;

                if (stronghold != null)
                {
                    return stronghold;
                }
            }

            int k1 = 0;

            while (k1 < 5)
            {
                ++k1;
                int j1 = p_75201_2_.nextInt(totalWeight);
                Iterator iterator = structurePieceList.iterator();

                while (iterator.hasNext())
                {
                    StructureStrongholdPieces.PieceWeight pieceweight = (StructureStrongholdPieces.PieceWeight)iterator.next();
                    j1 -= pieceweight.pieceWeight;

                    if (j1 < 0)
                    {
                        if (!pieceweight.canSpawnMoreStructuresOfType(p_75201_7_) || pieceweight == p_75201_0_.strongholdPieceWeight)
                        {
                            break;
                        }

                        StructureStrongholdPieces.Stronghold stronghold1 = getStrongholdComponentFromWeightedPiece(pieceweight.pieceClass, p_75201_1_, p_75201_2_, p_75201_3_, p_75201_4_, p_75201_5_, p_75201_6_, p_75201_7_);

                        if (stronghold1 != null)
                        {
                            ++pieceweight.instancesSpawned;
                            p_75201_0_.strongholdPieceWeight = pieceweight;

                            if (!pieceweight.canSpawnMoreStructures())
                            {
                                structurePieceList.remove(pieceweight);
                            }

                            return stronghold1;
                        }
                    }
                }
            }

            StructureBoundingBox structureboundingbox = StructureStrongholdPieces.Corridor.func_74992_a(p_75201_1_, p_75201_2_, p_75201_3_, p_75201_4_, p_75201_5_, p_75201_6_);

            if (structureboundingbox != null && structureboundingbox.minY > 1)
            {
                return new StructureStrongholdPieces.Corridor(p_75201_7_, p_75201_2_, structureboundingbox, p_75201_6_);
            }
            else
            {
                return null;
            }
        }
    }

    private static StructureComponent getNextValidComponent(StructureStrongholdPieces.Stairs2 p_75196_0_, List p_75196_1_, Random p_75196_2_, int p_75196_3_, int p_75196_4_, int p_75196_5_, int p_75196_6_, int p_75196_7_)
    {
        if (p_75196_7_ > 50)
        {
            return null;
        }
        else if (Math.abs(p_75196_3_ - p_75196_0_.getBoundingBox().minX) <= 112 && Math.abs(p_75196_5_ - p_75196_0_.getBoundingBox().minZ) <= 112)
        {
            StructureStrongholdPieces.Stronghold stronghold = getNextComponent(p_75196_0_, p_75196_1_, p_75196_2_, p_75196_3_, p_75196_4_, p_75196_5_, p_75196_6_, p_75196_7_ + 1);

            if (stronghold != null)
            {
                p_75196_1_.add(stronghold);
                p_75196_0_.field_75026_c.add(stronghold);
            }

            return stronghold;
        }
        else
        {
            return null;
        }
    }

    public static class ChestCorridor extends StructureStrongholdPieces.Stronghold
        {
            /** List of items that Stronghold chests can contain. */
            public static final WeightedRandomChestContent[] strongholdChestContents = new WeightedRandomChestContent[] {new WeightedRandomChestContent(Items.ender_pearl, 0, 1, 1, 10), new WeightedRandomChestContent(Items.diamond, 0, 1, 3, 3), new WeightedRandomChestContent(Items.iron_ingot, 0, 1, 5, 10), new WeightedRandomChestContent(Items.gold_ingot, 0, 1, 3, 5), new WeightedRandomChestContent(Items.redstone, 0, 4, 9, 5), new WeightedRandomChestContent(Items.bread, 0, 1, 3, 15), new WeightedRandomChestContent(Items.apple, 0, 1, 3, 15), new WeightedRandomChestContent(Items.iron_pickaxe, 0, 1, 1, 5), new WeightedRandomChestContent(Items.iron_sword, 0, 1, 1, 5), new WeightedRandomChestContent(Items.iron_chestplate, 0, 1, 1, 5), new WeightedRandomChestContent(Items.iron_helmet, 0, 1, 1, 5), new WeightedRandomChestContent(Items.iron_leggings, 0, 1, 1, 5), new WeightedRandomChestContent(Items.iron_boots, 0, 1, 1, 5), new WeightedRandomChestContent(Items.golden_apple, 0, 1, 1, 1), new WeightedRandomChestContent(Items.saddle, 0, 1, 1, 1), new WeightedRandomChestContent(Items.iron_horse_armor, 0, 1, 1, 1), new WeightedRandomChestContent(Items.golden_horse_armor, 0, 1, 1, 1), new WeightedRandomChestContent(Items.diamond_horse_armor, 0, 1, 1, 1)};
            private boolean hasMadeChest;
            private static final String __OBFID = "CL_00000487";

            public ChestCorridor() {}

            public ChestCorridor(int p_i2071_1_, Random p_i2071_2_, StructureBoundingBox p_i2071_3_, int p_i2071_4_)
            {
                super(p_i2071_1_);
                this.coordBaseMode = p_i2071_4_;
                this.field_143013_d = this.getRandomDoor(p_i2071_2_);
                this.boundingBox = p_i2071_3_;
            }

            protected void func_143012_a(NBTTagCompound p_143012_1_)
            {
                super.func_143012_a(p_143012_1_);
                p_143012_1_.setBoolean("Chest", this.hasMadeChest);
            }

            protected void func_143011_b(NBTTagCompound p_143011_1_)
            {
                super.func_143011_b(p_143011_1_);
                this.hasMadeChest = p_143011_1_.getBoolean("Chest");
            }

            /**
             * Initiates construction of the Structure Component picked, at the current Location of StructGen
             */
            public void buildComponent(StructureComponent p_74861_1_, List p_74861_2_, Random p_74861_3_)
            {
                this.getNextComponentNormal((StructureStrongholdPieces.Stairs2)p_74861_1_, p_74861_2_, p_74861_3_, 1, 1);
            }

            public static StructureStrongholdPieces.ChestCorridor findValidPlacement(List p_75000_0_, Random p_75000_1_, int p_75000_2_, int p_75000_3_, int p_75000_4_, int p_75000_5_, int p_75000_6_)
            {
                StructureBoundingBox structureboundingbox = StructureBoundingBox.getComponentToAddBoundingBox(p_75000_2_, p_75000_3_, p_75000_4_, -1, -1, 0, 5, 5, 7, p_75000_5_);
                /**
                 * returns false if the Structure Bounding Box goes below 10
                 */
                return canStrongholdGoDeeper(structureboundingbox) && StructureComponent.findIntersecting(p_75000_0_, structureboundingbox) == null ? new StructureStrongholdPieces.ChestCorridor(p_75000_6_, p_75000_1_, structureboundingbox, p_75000_5_) : null;
            }

            /**
             * second Part of Structure generating, this for example places Spiderwebs, Mob Spawners, it closes
             * Mineshafts at the end, it adds Fences...
             */
            public boolean addComponentParts(World p_74875_1_, Random p_74875_2_, StructureBoundingBox p_74875_3_)
            {
                if (this.isLiquidInStructureBoundingBox(p_74875_1_, p_74875_3_))
                {
                    return false;
                }
                else
                {
                    this.fillWithRandomizedBlocks(p_74875_1_, p_74875_3_, 0, 0, 0, 4, 4, 6, true, p_74875_2_, StructureStrongholdPieces.strongholdStones);
                    this.placeDoor(p_74875_1_, p_74875_2_, p_74875_3_, this.field_143013_d, 1, 1, 0);
                    this.placeDoor(p_74875_1_, p_74875_2_, p_74875_3_, StructureStrongholdPieces.Stronghold.Door.OPENING, 1, 1, 6);
                    this.fillWithBlocks(p_74875_1_, p_74875_3_, 3, 1, 2, 3, 1, 4, Blocks.stonebrick, Blocks.stonebrick, false);
                    this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stone_slab, 5, 3, 1, 1, p_74875_3_);
                    this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stone_slab, 5, 3, 1, 5, p_74875_3_);
                    this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stone_slab, 5, 3, 2, 2, p_74875_3_);
                    this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stone_slab, 5, 3, 2, 4, p_74875_3_);
                    int i;

                    for (i = 2; i <= 4; ++i)
                    {
                        this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stone_slab, 5, 2, 1, i, p_74875_3_);
                    }

                    if (!this.hasMadeChest)
                    {
                        i = this.getYWithOffset(2);
                        int j = this.getXWithOffset(3, 3);
                        int k = this.getZWithOffset(3, 3);

                        if (p_74875_3_.isVecInside(j, i, k))
                        {
                            this.hasMadeChest = true;
                            this.generateStructureChestContents(p_74875_1_, p_74875_3_, p_74875_2_, 3, 2, 3, ChestGenHooks.getItems(STRONGHOLD_CORRIDOR, p_74875_2_), ChestGenHooks.getCount(STRONGHOLD_CORRIDOR, p_74875_2_));
                        }
                    }

                    return true;
                }
            }
        }

    public static class Corridor extends StructureStrongholdPieces.Stronghold
        {
            private int field_74993_a;
            private static final String __OBFID = "CL_00000488";

            public Corridor() {}

            public Corridor(int p_i2072_1_, Random p_i2072_2_, StructureBoundingBox p_i2072_3_, int p_i2072_4_)
            {
                super(p_i2072_1_);
                this.coordBaseMode = p_i2072_4_;
                this.boundingBox = p_i2072_3_;
                this.field_74993_a = p_i2072_4_ != 2 && p_i2072_4_ != 0 ? p_i2072_3_.getXSize() : p_i2072_3_.getZSize();
            }

            protected void func_143012_a(NBTTagCompound p_143012_1_)
            {
                super.func_143012_a(p_143012_1_);
                p_143012_1_.setInteger("Steps", this.field_74993_a);
            }

            protected void func_143011_b(NBTTagCompound p_143011_1_)
            {
                super.func_143011_b(p_143011_1_);
                this.field_74993_a = p_143011_1_.getInteger("Steps");
            }

            public static StructureBoundingBox func_74992_a(List p_74992_0_, Random p_74992_1_, int p_74992_2_, int p_74992_3_, int p_74992_4_, int p_74992_5_)
            {
                boolean flag = true;
                StructureBoundingBox structureboundingbox = StructureBoundingBox.getComponentToAddBoundingBox(p_74992_2_, p_74992_3_, p_74992_4_, -1, -1, 0, 5, 5, 4, p_74992_5_);
                StructureComponent structurecomponent = StructureComponent.findIntersecting(p_74992_0_, structureboundingbox);

                if (structurecomponent == null)
                {
                    return null;
                }
                else
                {
                    if (structurecomponent.getBoundingBox().minY == structureboundingbox.minY)
                    {
                        for (int i1 = 3; i1 >= 1; --i1)
                        {
                            structureboundingbox = StructureBoundingBox.getComponentToAddBoundingBox(p_74992_2_, p_74992_3_, p_74992_4_, -1, -1, 0, 5, 5, i1 - 1, p_74992_5_);

                            if (!structurecomponent.getBoundingBox().intersectsWith(structureboundingbox))
                            {
                                return StructureBoundingBox.getComponentToAddBoundingBox(p_74992_2_, p_74992_3_, p_74992_4_, -1, -1, 0, 5, 5, i1, p_74992_5_);
                            }
                        }
                    }

                    return null;
                }
            }

            /**
             * second Part of Structure generating, this for example places Spiderwebs, Mob Spawners, it closes
             * Mineshafts at the end, it adds Fences...
             */
            public boolean addComponentParts(World p_74875_1_, Random p_74875_2_, StructureBoundingBox p_74875_3_)
            {
                if (this.isLiquidInStructureBoundingBox(p_74875_1_, p_74875_3_))
                {
                    return false;
                }
                else
                {
                    for (int i = 0; i < this.field_74993_a; ++i)
                    {
                        this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stonebrick, 0, 0, 0, i, p_74875_3_);
                        this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stonebrick, 0, 1, 0, i, p_74875_3_);
                        this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stonebrick, 0, 2, 0, i, p_74875_3_);
                        this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stonebrick, 0, 3, 0, i, p_74875_3_);
                        this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stonebrick, 0, 4, 0, i, p_74875_3_);

                        for (int j = 1; j <= 3; ++j)
                        {
                            this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stonebrick, 0, 0, j, i, p_74875_3_);
                            this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.air, 0, 1, j, i, p_74875_3_);
                            this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.air, 0, 2, j, i, p_74875_3_);
                            this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.air, 0, 3, j, i, p_74875_3_);
                            this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stonebrick, 0, 4, j, i, p_74875_3_);
                        }

                        this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stonebrick, 0, 0, 4, i, p_74875_3_);
                        this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stonebrick, 0, 1, 4, i, p_74875_3_);
                        this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stonebrick, 0, 2, 4, i, p_74875_3_);
                        this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stonebrick, 0, 3, 4, i, p_74875_3_);
                        this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stonebrick, 0, 4, 4, i, p_74875_3_);
                    }

                    return true;
                }
            }
        }

    public static class Crossing extends StructureStrongholdPieces.Stronghold
        {
            private boolean field_74996_b;
            private boolean field_74997_c;
            private boolean field_74995_d;
            private boolean field_74999_h;
            private static final String __OBFID = "CL_00000489";

            public Crossing() {}

            public Crossing(int p_i2073_1_, Random p_i2073_2_, StructureBoundingBox p_i2073_3_, int p_i2073_4_)
            {
                super(p_i2073_1_);
                this.coordBaseMode = p_i2073_4_;
                this.field_143013_d = this.getRandomDoor(p_i2073_2_);
                this.boundingBox = p_i2073_3_;
                this.field_74996_b = p_i2073_2_.nextBoolean();
                this.field_74997_c = p_i2073_2_.nextBoolean();
                this.field_74995_d = p_i2073_2_.nextBoolean();
                this.field_74999_h = p_i2073_2_.nextInt(3) > 0;
            }

            protected void func_143012_a(NBTTagCompound p_143012_1_)
            {
                super.func_143012_a(p_143012_1_);
                p_143012_1_.setBoolean("leftLow", this.field_74996_b);
                p_143012_1_.setBoolean("leftHigh", this.field_74997_c);
                p_143012_1_.setBoolean("rightLow", this.field_74995_d);
                p_143012_1_.setBoolean("rightHigh", this.field_74999_h);
            }

            protected void func_143011_b(NBTTagCompound p_143011_1_)
            {
                super.func_143011_b(p_143011_1_);
                this.field_74996_b = p_143011_1_.getBoolean("leftLow");
                this.field_74997_c = p_143011_1_.getBoolean("leftHigh");
                this.field_74995_d = p_143011_1_.getBoolean("rightLow");
                this.field_74999_h = p_143011_1_.getBoolean("rightHigh");
            }

            /**
             * Initiates construction of the Structure Component picked, at the current Location of StructGen
             */
            public void buildComponent(StructureComponent p_74861_1_, List p_74861_2_, Random p_74861_3_)
            {
                int i = 3;
                int j = 5;

                if (this.coordBaseMode == 1 || this.coordBaseMode == 2)
                {
                    i = 8 - i;
                    j = 8 - j;
                }

                this.getNextComponentNormal((StructureStrongholdPieces.Stairs2)p_74861_1_, p_74861_2_, p_74861_3_, 5, 1);

                if (this.field_74996_b)
                {
                    this.getNextComponentX((StructureStrongholdPieces.Stairs2)p_74861_1_, p_74861_2_, p_74861_3_, i, 1);
                }

                if (this.field_74997_c)
                {
                    this.getNextComponentX((StructureStrongholdPieces.Stairs2)p_74861_1_, p_74861_2_, p_74861_3_, j, 7);
                }

                if (this.field_74995_d)
                {
                    this.getNextComponentZ((StructureStrongholdPieces.Stairs2)p_74861_1_, p_74861_2_, p_74861_3_, i, 1);
                }

                if (this.field_74999_h)
                {
                    this.getNextComponentZ((StructureStrongholdPieces.Stairs2)p_74861_1_, p_74861_2_, p_74861_3_, j, 7);
                }
            }

            public static StructureStrongholdPieces.Crossing findValidPlacement(List p_74994_0_, Random p_74994_1_, int p_74994_2_, int p_74994_3_, int p_74994_4_, int p_74994_5_, int p_74994_6_)
            {
                StructureBoundingBox structureboundingbox = StructureBoundingBox.getComponentToAddBoundingBox(p_74994_2_, p_74994_3_, p_74994_4_, -4, -3, 0, 10, 9, 11, p_74994_5_);
                /**
                 * returns false if the Structure Bounding Box goes below 10
                 */
                return canStrongholdGoDeeper(structureboundingbox) && StructureComponent.findIntersecting(p_74994_0_, structureboundingbox) == null ? new StructureStrongholdPieces.Crossing(p_74994_6_, p_74994_1_, structureboundingbox, p_74994_5_) : null;
            }

            /**
             * second Part of Structure generating, this for example places Spiderwebs, Mob Spawners, it closes
             * Mineshafts at the end, it adds Fences...
             */
            public boolean addComponentParts(World p_74875_1_, Random p_74875_2_, StructureBoundingBox p_74875_3_)
            {
                if (this.isLiquidInStructureBoundingBox(p_74875_1_, p_74875_3_))
                {
                    return false;
                }
                else
                {
                    this.fillWithRandomizedBlocks(p_74875_1_, p_74875_3_, 0, 0, 0, 9, 8, 10, true, p_74875_2_, StructureStrongholdPieces.strongholdStones);
                    this.placeDoor(p_74875_1_, p_74875_2_, p_74875_3_, this.field_143013_d, 4, 3, 0);

                    if (this.field_74996_b)
                    {
                        this.fillWithBlocks(p_74875_1_, p_74875_3_, 0, 3, 1, 0, 5, 3, Blocks.air, Blocks.air, false);
                    }

                    if (this.field_74995_d)
                    {
                        this.fillWithBlocks(p_74875_1_, p_74875_3_, 9, 3, 1, 9, 5, 3, Blocks.air, Blocks.air, false);
                    }

                    if (this.field_74997_c)
                    {
                        this.fillWithBlocks(p_74875_1_, p_74875_3_, 0, 5, 7, 0, 7, 9, Blocks.air, Blocks.air, false);
                    }

                    if (this.field_74999_h)
                    {
                        this.fillWithBlocks(p_74875_1_, p_74875_3_, 9, 5, 7, 9, 7, 9, Blocks.air, Blocks.air, false);
                    }

                    this.fillWithBlocks(p_74875_1_, p_74875_3_, 5, 1, 10, 7, 3, 10, Blocks.air, Blocks.air, false);
                    this.fillWithRandomizedBlocks(p_74875_1_, p_74875_3_, 1, 2, 1, 8, 2, 6, false, p_74875_2_, StructureStrongholdPieces.strongholdStones);
                    this.fillWithRandomizedBlocks(p_74875_1_, p_74875_3_, 4, 1, 5, 4, 4, 9, false, p_74875_2_, StructureStrongholdPieces.strongholdStones);
                    this.fillWithRandomizedBlocks(p_74875_1_, p_74875_3_, 8, 1, 5, 8, 4, 9, false, p_74875_2_, StructureStrongholdPieces.strongholdStones);
                    this.fillWithRandomizedBlocks(p_74875_1_, p_74875_3_, 1, 4, 7, 3, 4, 9, false, p_74875_2_, StructureStrongholdPieces.strongholdStones);
                    this.fillWithRandomizedBlocks(p_74875_1_, p_74875_3_, 1, 3, 5, 3, 3, 6, false, p_74875_2_, StructureStrongholdPieces.strongholdStones);
                    this.fillWithBlocks(p_74875_1_, p_74875_3_, 1, 3, 4, 3, 3, 4, Blocks.stone_slab, Blocks.stone_slab, false);
                    this.fillWithBlocks(p_74875_1_, p_74875_3_, 1, 4, 6, 3, 4, 6, Blocks.stone_slab, Blocks.stone_slab, false);
                    this.fillWithRandomizedBlocks(p_74875_1_, p_74875_3_, 5, 1, 7, 7, 1, 8, false, p_74875_2_, StructureStrongholdPieces.strongholdStones);
                    this.fillWithBlocks(p_74875_1_, p_74875_3_, 5, 1, 9, 7, 1, 9, Blocks.stone_slab, Blocks.stone_slab, false);
                    this.fillWithBlocks(p_74875_1_, p_74875_3_, 5, 2, 7, 7, 2, 7, Blocks.stone_slab, Blocks.stone_slab, false);
                    this.fillWithBlocks(p_74875_1_, p_74875_3_, 4, 5, 7, 4, 5, 9, Blocks.stone_slab, Blocks.stone_slab, false);
                    this.fillWithBlocks(p_74875_1_, p_74875_3_, 8, 5, 7, 8, 5, 9, Blocks.stone_slab, Blocks.stone_slab, false);
                    this.fillWithBlocks(p_74875_1_, p_74875_3_, 5, 5, 7, 7, 5, 9, Blocks.double_stone_slab, Blocks.double_stone_slab, false);
                    this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.torch, 0, 6, 5, 6, p_74875_3_);
                    return true;
                }
            }
        }

    public static class LeftTurn extends StructureStrongholdPieces.Stronghold
        {
            private static final String __OBFID = "CL_00000490";

            public LeftTurn() {}

            public LeftTurn(int p_i2074_1_, Random p_i2074_2_, StructureBoundingBox p_i2074_3_, int p_i2074_4_)
            {
                super(p_i2074_1_);
                this.coordBaseMode = p_i2074_4_;
                this.field_143013_d = this.getRandomDoor(p_i2074_2_);
                this.boundingBox = p_i2074_3_;
            }

            /**
             * Initiates construction of the Structure Component picked, at the current Location of StructGen
             */
            public void buildComponent(StructureComponent p_74861_1_, List p_74861_2_, Random p_74861_3_)
            {
                if (this.coordBaseMode != 2 && this.coordBaseMode != 3)
                {
                    this.getNextComponentZ((StructureStrongholdPieces.Stairs2)p_74861_1_, p_74861_2_, p_74861_3_, 1, 1);
                }
                else
                {
                    this.getNextComponentX((StructureStrongholdPieces.Stairs2)p_74861_1_, p_74861_2_, p_74861_3_, 1, 1);
                }
            }

            public static StructureStrongholdPieces.LeftTurn findValidPlacement(List p_75010_0_, Random p_75010_1_, int p_75010_2_, int p_75010_3_, int p_75010_4_, int p_75010_5_, int p_75010_6_)
            {
                StructureBoundingBox structureboundingbox = StructureBoundingBox.getComponentToAddBoundingBox(p_75010_2_, p_75010_3_, p_75010_4_, -1, -1, 0, 5, 5, 5, p_75010_5_);
                /**
                 * returns false if the Structure Bounding Box goes below 10
                 */
                return canStrongholdGoDeeper(structureboundingbox) && StructureComponent.findIntersecting(p_75010_0_, structureboundingbox) == null ? new StructureStrongholdPieces.LeftTurn(p_75010_6_, p_75010_1_, structureboundingbox, p_75010_5_) : null;
            }

            /**
             * second Part of Structure generating, this for example places Spiderwebs, Mob Spawners, it closes
             * Mineshafts at the end, it adds Fences...
             */
            public boolean addComponentParts(World p_74875_1_, Random p_74875_2_, StructureBoundingBox p_74875_3_)
            {
                if (this.isLiquidInStructureBoundingBox(p_74875_1_, p_74875_3_))
                {
                    return false;
                }
                else
                {
                    this.fillWithRandomizedBlocks(p_74875_1_, p_74875_3_, 0, 0, 0, 4, 4, 4, true, p_74875_2_, StructureStrongholdPieces.strongholdStones);
                    this.placeDoor(p_74875_1_, p_74875_2_, p_74875_3_, this.field_143013_d, 1, 1, 0);

                    if (this.coordBaseMode != 2 && this.coordBaseMode != 3)
                    {
                        this.fillWithBlocks(p_74875_1_, p_74875_3_, 4, 1, 1, 4, 3, 3, Blocks.air, Blocks.air, false);
                    }
                    else
                    {
                        this.fillWithBlocks(p_74875_1_, p_74875_3_, 0, 1, 1, 0, 3, 3, Blocks.air, Blocks.air, false);
                    }

                    return true;
                }
            }
        }

    public static class Library extends StructureStrongholdPieces.Stronghold
        {
            /** List of items that Stronghold Library chests can contain. */
            public static final WeightedRandomChestContent[] strongholdLibraryChestContents = new WeightedRandomChestContent[] {new WeightedRandomChestContent(Items.book, 0, 1, 3, 20), new WeightedRandomChestContent(Items.paper, 0, 2, 7, 20), new WeightedRandomChestContent(Items.map, 0, 1, 1, 1), new WeightedRandomChestContent(Items.compass, 0, 1, 1, 1)};
            private boolean isLargeRoom;
            private static final String __OBFID = "CL_00000491";

            public Library() {}

            public Library(int p_i2075_1_, Random p_i2075_2_, StructureBoundingBox p_i2075_3_, int p_i2075_4_)
            {
                super(p_i2075_1_);
                this.coordBaseMode = p_i2075_4_;
                this.field_143013_d = this.getRandomDoor(p_i2075_2_);
                this.boundingBox = p_i2075_3_;
                this.isLargeRoom = p_i2075_3_.getYSize() > 6;
            }

            protected void func_143012_a(NBTTagCompound p_143012_1_)
            {
                super.func_143012_a(p_143012_1_);
                p_143012_1_.setBoolean("Tall", this.isLargeRoom);
            }

            protected void func_143011_b(NBTTagCompound p_143011_1_)
            {
                super.func_143011_b(p_143011_1_);
                this.isLargeRoom = p_143011_1_.getBoolean("Tall");
            }

            public static StructureStrongholdPieces.Library findValidPlacement(List p_75006_0_, Random p_75006_1_, int p_75006_2_, int p_75006_3_, int p_75006_4_, int p_75006_5_, int p_75006_6_)
            {
                StructureBoundingBox structureboundingbox = StructureBoundingBox.getComponentToAddBoundingBox(p_75006_2_, p_75006_3_, p_75006_4_, -4, -1, 0, 14, 11, 15, p_75006_5_);

                if (!canStrongholdGoDeeper(structureboundingbox) || StructureComponent.findIntersecting(p_75006_0_, structureboundingbox) != null)
                {
                    structureboundingbox = StructureBoundingBox.getComponentToAddBoundingBox(p_75006_2_, p_75006_3_, p_75006_4_, -4, -1, 0, 14, 6, 15, p_75006_5_);

                    if (!canStrongholdGoDeeper(structureboundingbox) || StructureComponent.findIntersecting(p_75006_0_, structureboundingbox) != null)
                    {
                        return null;
                    }
                }

                return new StructureStrongholdPieces.Library(p_75006_6_, p_75006_1_, structureboundingbox, p_75006_5_);
            }

            /**
             * second Part of Structure generating, this for example places Spiderwebs, Mob Spawners, it closes
             * Mineshafts at the end, it adds Fences...
             */
            public boolean addComponentParts(World p_74875_1_, Random p_74875_2_, StructureBoundingBox p_74875_3_)
            {
                if (this.isLiquidInStructureBoundingBox(p_74875_1_, p_74875_3_))
                {
                    return false;
                }
                else
                {
                    byte b0 = 11;

                    if (!this.isLargeRoom)
                    {
                        b0 = 6;
                    }

                    this.fillWithRandomizedBlocks(p_74875_1_, p_74875_3_, 0, 0, 0, 13, b0 - 1, 14, true, p_74875_2_, StructureStrongholdPieces.strongholdStones);
                    this.placeDoor(p_74875_1_, p_74875_2_, p_74875_3_, this.field_143013_d, 4, 1, 0);
                    this.randomlyFillWithBlocks(p_74875_1_, p_74875_3_, p_74875_2_, 0.07F, 2, 1, 1, 11, 4, 13, Blocks.web, Blocks.web, false);
                    boolean flag = true;
                    boolean flag1 = true;
                    int i;

                    for (i = 1; i <= 13; ++i)
                    {
                        if ((i - 1) % 4 == 0)
                        {
                            this.fillWithBlocks(p_74875_1_, p_74875_3_, 1, 1, i, 1, 4, i, Blocks.planks, Blocks.planks, false);
                            this.fillWithBlocks(p_74875_1_, p_74875_3_, 12, 1, i, 12, 4, i, Blocks.planks, Blocks.planks, false);
                            this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.torch, 0, 2, 3, i, p_74875_3_);
                            this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.torch, 0, 11, 3, i, p_74875_3_);

                            if (this.isLargeRoom)
                            {
                                this.fillWithBlocks(p_74875_1_, p_74875_3_, 1, 6, i, 1, 9, i, Blocks.planks, Blocks.planks, false);
                                this.fillWithBlocks(p_74875_1_, p_74875_3_, 12, 6, i, 12, 9, i, Blocks.planks, Blocks.planks, false);
                            }
                        }
                        else
                        {
                            this.fillWithBlocks(p_74875_1_, p_74875_3_, 1, 1, i, 1, 4, i, Blocks.bookshelf, Blocks.bookshelf, false);
                            this.fillWithBlocks(p_74875_1_, p_74875_3_, 12, 1, i, 12, 4, i, Blocks.bookshelf, Blocks.bookshelf, false);

                            if (this.isLargeRoom)
                            {
                                this.fillWithBlocks(p_74875_1_, p_74875_3_, 1, 6, i, 1, 9, i, Blocks.bookshelf, Blocks.bookshelf, false);
                                this.fillWithBlocks(p_74875_1_, p_74875_3_, 12, 6, i, 12, 9, i, Blocks.bookshelf, Blocks.bookshelf, false);
                            }
                        }
                    }

                    for (i = 3; i < 12; i += 2)
                    {
                        this.fillWithBlocks(p_74875_1_, p_74875_3_, 3, 1, i, 4, 3, i, Blocks.bookshelf, Blocks.bookshelf, false);
                        this.fillWithBlocks(p_74875_1_, p_74875_3_, 6, 1, i, 7, 3, i, Blocks.bookshelf, Blocks.bookshelf, false);
                        this.fillWithBlocks(p_74875_1_, p_74875_3_, 9, 1, i, 10, 3, i, Blocks.bookshelf, Blocks.bookshelf, false);
                    }

                    if (this.isLargeRoom)
                    {
                        this.fillWithBlocks(p_74875_1_, p_74875_3_, 1, 5, 1, 3, 5, 13, Blocks.planks, Blocks.planks, false);
                        this.fillWithBlocks(p_74875_1_, p_74875_3_, 10, 5, 1, 12, 5, 13, Blocks.planks, Blocks.planks, false);
                        this.fillWithBlocks(p_74875_1_, p_74875_3_, 4, 5, 1, 9, 5, 2, Blocks.planks, Blocks.planks, false);
                        this.fillWithBlocks(p_74875_1_, p_74875_3_, 4, 5, 12, 9, 5, 13, Blocks.planks, Blocks.planks, false);
                        this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.planks, 0, 9, 5, 11, p_74875_3_);
                        this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.planks, 0, 8, 5, 11, p_74875_3_);
                        this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.planks, 0, 9, 5, 10, p_74875_3_);
                        this.fillWithBlocks(p_74875_1_, p_74875_3_, 3, 6, 2, 3, 6, 12, Blocks.fence, Blocks.fence, false);
                        this.fillWithBlocks(p_74875_1_, p_74875_3_, 10, 6, 2, 10, 6, 10, Blocks.fence, Blocks.fence, false);
                        this.fillWithBlocks(p_74875_1_, p_74875_3_, 4, 6, 2, 9, 6, 2, Blocks.fence, Blocks.fence, false);
                        this.fillWithBlocks(p_74875_1_, p_74875_3_, 4, 6, 12, 8, 6, 12, Blocks.fence, Blocks.fence, false);
                        this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.fence, 0, 9, 6, 11, p_74875_3_);
                        this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.fence, 0, 8, 6, 11, p_74875_3_);
                        this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.fence, 0, 9, 6, 10, p_74875_3_);
                        i = this.getMetadataWithOffset(Blocks.ladder, 3);
                        this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.ladder, i, 10, 1, 13, p_74875_3_);
                        this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.ladder, i, 10, 2, 13, p_74875_3_);
                        this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.ladder, i, 10, 3, 13, p_74875_3_);
                        this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.ladder, i, 10, 4, 13, p_74875_3_);
                        this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.ladder, i, 10, 5, 13, p_74875_3_);
                        this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.ladder, i, 10, 6, 13, p_74875_3_);
                        this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.ladder, i, 10, 7, 13, p_74875_3_);
                        byte b1 = 7;
                        byte b2 = 7;
                        this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.fence, 0, b1 - 1, 9, b2, p_74875_3_);
                        this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.fence, 0, b1, 9, b2, p_74875_3_);
                        this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.fence, 0, b1 - 1, 8, b2, p_74875_3_);
                        this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.fence, 0, b1, 8, b2, p_74875_3_);
                        this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.fence, 0, b1 - 1, 7, b2, p_74875_3_);
                        this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.fence, 0, b1, 7, b2, p_74875_3_);
                        this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.fence, 0, b1 - 2, 7, b2, p_74875_3_);
                        this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.fence, 0, b1 + 1, 7, b2, p_74875_3_);
                        this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.fence, 0, b1 - 1, 7, b2 - 1, p_74875_3_);
                        this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.fence, 0, b1 - 1, 7, b2 + 1, p_74875_3_);
                        this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.fence, 0, b1, 7, b2 - 1, p_74875_3_);
                        this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.fence, 0, b1, 7, b2 + 1, p_74875_3_);
                        this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.torch, 0, b1 - 2, 8, b2, p_74875_3_);
                        this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.torch, 0, b1 + 1, 8, b2, p_74875_3_);
                        this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.torch, 0, b1 - 1, 8, b2 - 1, p_74875_3_);
                        this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.torch, 0, b1 - 1, 8, b2 + 1, p_74875_3_);
                        this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.torch, 0, b1, 8, b2 - 1, p_74875_3_);
                        this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.torch, 0, b1, 8, b2 + 1, p_74875_3_);
                    }

                    ChestGenHooks info = ChestGenHooks.getInfo(STRONGHOLD_LIBRARY);

                    this.generateStructureChestContents(p_74875_1_, p_74875_3_, p_74875_2_, 3, 3, 5, info.getItems(p_74875_2_), info.getCount(p_74875_2_));

                    if (this.isLargeRoom)
                    {
                        this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.air, 0, 12, 9, 1, p_74875_3_);
                        this.generateStructureChestContents(p_74875_1_, p_74875_3_, p_74875_2_, 12, 8, 1, info.getItems(p_74875_2_), info.getCount(p_74875_2_));
                    }

                    return true;
                }
            }
        }

    static class PieceWeight
        {
            public Class pieceClass;
            /**
             * This basically keeps track of the 'epicness' of a structure. Epic structure components have a higher
             * 'weight', and Structures may only grow up to a certain 'weight' before generation is stopped
             */
            public final int pieceWeight;
            public int instancesSpawned;
            /** How many Structure Pieces of this type may spawn in a structure */
            public int instancesLimit;
            private static final String __OBFID = "CL_00000492";

            public PieceWeight(Class p_i2076_1_, int p_i2076_2_, int p_i2076_3_)
            {
                this.pieceClass = p_i2076_1_;
                this.pieceWeight = p_i2076_2_;
                this.instancesLimit = p_i2076_3_;
            }

            public boolean canSpawnMoreStructuresOfType(int p_75189_1_)
            {
                return this.instancesLimit == 0 || this.instancesSpawned < this.instancesLimit;
            }

            public boolean canSpawnMoreStructures()
            {
                return this.instancesLimit == 0 || this.instancesSpawned < this.instancesLimit;
            }
        }

    public static class PortalRoom extends StructureStrongholdPieces.Stronghold
        {
            private boolean hasSpawner;
            private static final String __OBFID = "CL_00000493";

            public PortalRoom() {}

            public PortalRoom(int p_i2077_1_, Random p_i2077_2_, StructureBoundingBox p_i2077_3_, int p_i2077_4_)
            {
                super(p_i2077_1_);
                this.coordBaseMode = p_i2077_4_;
                this.boundingBox = p_i2077_3_;
            }

            protected void func_143012_a(NBTTagCompound p_143012_1_)
            {
                super.func_143012_a(p_143012_1_);
                p_143012_1_.setBoolean("Mob", this.hasSpawner);
            }

            protected void func_143011_b(NBTTagCompound p_143011_1_)
            {
                super.func_143011_b(p_143011_1_);
                this.hasSpawner = p_143011_1_.getBoolean("Mob");
            }

            /**
             * Initiates construction of the Structure Component picked, at the current Location of StructGen
             */
            public void buildComponent(StructureComponent p_74861_1_, List p_74861_2_, Random p_74861_3_)
            {
                if (p_74861_1_ != null)
                {
                    ((StructureStrongholdPieces.Stairs2)p_74861_1_).strongholdPortalRoom = this;
                }
            }

            public static StructureStrongholdPieces.PortalRoom findValidPlacement(List p_75004_0_, Random p_75004_1_, int p_75004_2_, int p_75004_3_, int p_75004_4_, int p_75004_5_, int p_75004_6_)
            {
                StructureBoundingBox structureboundingbox = StructureBoundingBox.getComponentToAddBoundingBox(p_75004_2_, p_75004_3_, p_75004_4_, -4, -1, 0, 11, 8, 16, p_75004_5_);
                /**
                 * returns false if the Structure Bounding Box goes below 10
                 */
                return canStrongholdGoDeeper(structureboundingbox) && StructureComponent.findIntersecting(p_75004_0_, structureboundingbox) == null ? new StructureStrongholdPieces.PortalRoom(p_75004_6_, p_75004_1_, structureboundingbox, p_75004_5_) : null;
            }

            /**
             * second Part of Structure generating, this for example places Spiderwebs, Mob Spawners, it closes
             * Mineshafts at the end, it adds Fences...
             */
            public boolean addComponentParts(World p_74875_1_, Random p_74875_2_, StructureBoundingBox p_74875_3_)
            {
                this.fillWithRandomizedBlocks(p_74875_1_, p_74875_3_, 0, 0, 0, 10, 7, 15, false, p_74875_2_, StructureStrongholdPieces.strongholdStones);
                this.placeDoor(p_74875_1_, p_74875_2_, p_74875_3_, StructureStrongholdPieces.Stronghold.Door.GRATES, 4, 1, 0);
                byte b0 = 6;
                this.fillWithRandomizedBlocks(p_74875_1_, p_74875_3_, 1, b0, 1, 1, b0, 14, false, p_74875_2_, StructureStrongholdPieces.strongholdStones);
                this.fillWithRandomizedBlocks(p_74875_1_, p_74875_3_, 9, b0, 1, 9, b0, 14, false, p_74875_2_, StructureStrongholdPieces.strongholdStones);
                this.fillWithRandomizedBlocks(p_74875_1_, p_74875_3_, 2, b0, 1, 8, b0, 2, false, p_74875_2_, StructureStrongholdPieces.strongholdStones);
                this.fillWithRandomizedBlocks(p_74875_1_, p_74875_3_, 2, b0, 14, 8, b0, 14, false, p_74875_2_, StructureStrongholdPieces.strongholdStones);
                this.fillWithRandomizedBlocks(p_74875_1_, p_74875_3_, 1, 1, 1, 2, 1, 4, false, p_74875_2_, StructureStrongholdPieces.strongholdStones);
                this.fillWithRandomizedBlocks(p_74875_1_, p_74875_3_, 8, 1, 1, 9, 1, 4, false, p_74875_2_, StructureStrongholdPieces.strongholdStones);
                this.fillWithBlocks(p_74875_1_, p_74875_3_, 1, 1, 1, 1, 1, 3, Blocks.flowing_lava, Blocks.flowing_lava, false);
                this.fillWithBlocks(p_74875_1_, p_74875_3_, 9, 1, 1, 9, 1, 3, Blocks.flowing_lava, Blocks.flowing_lava, false);
                this.fillWithRandomizedBlocks(p_74875_1_, p_74875_3_, 3, 1, 8, 7, 1, 12, false, p_74875_2_, StructureStrongholdPieces.strongholdStones);
                this.fillWithBlocks(p_74875_1_, p_74875_3_, 4, 1, 9, 6, 1, 11, Blocks.flowing_lava, Blocks.flowing_lava, false);
                int i;

                for (i = 3; i < 14; i += 2)
                {
                    this.fillWithBlocks(p_74875_1_, p_74875_3_, 0, 3, i, 0, 4, i, Blocks.iron_bars, Blocks.iron_bars, false);
                    this.fillWithBlocks(p_74875_1_, p_74875_3_, 10, 3, i, 10, 4, i, Blocks.iron_bars, Blocks.iron_bars, false);
                }

                for (i = 2; i < 9; i += 2)
                {
                    this.fillWithBlocks(p_74875_1_, p_74875_3_, i, 3, 15, i, 4, 15, Blocks.iron_bars, Blocks.iron_bars, false);
                }

                i = this.getMetadataWithOffset(Blocks.stone_brick_stairs, 3);
                this.fillWithRandomizedBlocks(p_74875_1_, p_74875_3_, 4, 1, 5, 6, 1, 7, false, p_74875_2_, StructureStrongholdPieces.strongholdStones);
                this.fillWithRandomizedBlocks(p_74875_1_, p_74875_3_, 4, 2, 6, 6, 2, 7, false, p_74875_2_, StructureStrongholdPieces.strongholdStones);
                this.fillWithRandomizedBlocks(p_74875_1_, p_74875_3_, 4, 3, 7, 6, 3, 7, false, p_74875_2_, StructureStrongholdPieces.strongholdStones);

                for (int j = 4; j <= 6; ++j)
                {
                    this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stone_brick_stairs, i, j, 1, 4, p_74875_3_);
                    this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stone_brick_stairs, i, j, 2, 5, p_74875_3_);
                    this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stone_brick_stairs, i, j, 3, 6, p_74875_3_);
                }

                byte b4 = 2;
                byte b1 = 0;
                byte b2 = 3;
                byte b3 = 1;

                switch (this.coordBaseMode)
                {
                    case 0:
                        b4 = 0;
                        b1 = 2;
                        break;
                    case 1:
                        b4 = 1;
                        b1 = 3;
                        b2 = 0;
                        b3 = 2;
                    case 2:
                    default:
                        break;
                    case 3:
                        b4 = 3;
                        b1 = 1;
                        b2 = 0;
                        b3 = 2;
                }

                this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.end_portal_frame, b4 + (p_74875_2_.nextFloat() > 0.9F ? 4 : 0), 4, 3, 8, p_74875_3_);
                this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.end_portal_frame, b4 + (p_74875_2_.nextFloat() > 0.9F ? 4 : 0), 5, 3, 8, p_74875_3_);
                this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.end_portal_frame, b4 + (p_74875_2_.nextFloat() > 0.9F ? 4 : 0), 6, 3, 8, p_74875_3_);
                this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.end_portal_frame, b1 + (p_74875_2_.nextFloat() > 0.9F ? 4 : 0), 4, 3, 12, p_74875_3_);
                this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.end_portal_frame, b1 + (p_74875_2_.nextFloat() > 0.9F ? 4 : 0), 5, 3, 12, p_74875_3_);
                this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.end_portal_frame, b1 + (p_74875_2_.nextFloat() > 0.9F ? 4 : 0), 6, 3, 12, p_74875_3_);
                this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.end_portal_frame, b2 + (p_74875_2_.nextFloat() > 0.9F ? 4 : 0), 3, 3, 9, p_74875_3_);
                this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.end_portal_frame, b2 + (p_74875_2_.nextFloat() > 0.9F ? 4 : 0), 3, 3, 10, p_74875_3_);
                this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.end_portal_frame, b2 + (p_74875_2_.nextFloat() > 0.9F ? 4 : 0), 3, 3, 11, p_74875_3_);
                this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.end_portal_frame, b3 + (p_74875_2_.nextFloat() > 0.9F ? 4 : 0), 7, 3, 9, p_74875_3_);
                this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.end_portal_frame, b3 + (p_74875_2_.nextFloat() > 0.9F ? 4 : 0), 7, 3, 10, p_74875_3_);
                this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.end_portal_frame, b3 + (p_74875_2_.nextFloat() > 0.9F ? 4 : 0), 7, 3, 11, p_74875_3_);

                if (!this.hasSpawner)
                {
                    int i1 = this.getYWithOffset(3);
                    int k = this.getXWithOffset(5, 6);
                    int l = this.getZWithOffset(5, 6);

                    if (p_74875_3_.isVecInside(k, i1, l))
                    {
                        this.hasSpawner = true;
                        p_74875_1_.setBlock(k, i1, l, Blocks.mob_spawner, 0, 2);
                        TileEntityMobSpawner tileentitymobspawner = (TileEntityMobSpawner)p_74875_1_.getTileEntity(k, i1, l);

                        if (tileentitymobspawner != null)
                        {
                            tileentitymobspawner.func_145881_a().setEntityName("Silverfish");
                        }
                    }
                }

                return true;
            }
        }

    public static class Prison extends StructureStrongholdPieces.Stronghold
        {
            private static final String __OBFID = "CL_00000494";

            public Prison() {}

            public Prison(int p_i2078_1_, Random p_i2078_2_, StructureBoundingBox p_i2078_3_, int p_i2078_4_)
            {
                super(p_i2078_1_);
                this.coordBaseMode = p_i2078_4_;
                this.field_143013_d = this.getRandomDoor(p_i2078_2_);
                this.boundingBox = p_i2078_3_;
            }

            /**
             * Initiates construction of the Structure Component picked, at the current Location of StructGen
             */
            public void buildComponent(StructureComponent p_74861_1_, List p_74861_2_, Random p_74861_3_)
            {
                this.getNextComponentNormal((StructureStrongholdPieces.Stairs2)p_74861_1_, p_74861_2_, p_74861_3_, 1, 1);
            }

            public static StructureStrongholdPieces.Prison findValidPlacement(List p_75016_0_, Random p_75016_1_, int p_75016_2_, int p_75016_3_, int p_75016_4_, int p_75016_5_, int p_75016_6_)
            {
                StructureBoundingBox structureboundingbox = StructureBoundingBox.getComponentToAddBoundingBox(p_75016_2_, p_75016_3_, p_75016_4_, -1, -1, 0, 9, 5, 11, p_75016_5_);
                /**
                 * returns false if the Structure Bounding Box goes below 10
                 */
                return canStrongholdGoDeeper(structureboundingbox) && StructureComponent.findIntersecting(p_75016_0_, structureboundingbox) == null ? new StructureStrongholdPieces.Prison(p_75016_6_, p_75016_1_, structureboundingbox, p_75016_5_) : null;
            }

            /**
             * second Part of Structure generating, this for example places Spiderwebs, Mob Spawners, it closes
             * Mineshafts at the end, it adds Fences...
             */
            public boolean addComponentParts(World p_74875_1_, Random p_74875_2_, StructureBoundingBox p_74875_3_)
            {
                if (this.isLiquidInStructureBoundingBox(p_74875_1_, p_74875_3_))
                {
                    return false;
                }
                else
                {
                    this.fillWithRandomizedBlocks(p_74875_1_, p_74875_3_, 0, 0, 0, 8, 4, 10, true, p_74875_2_, StructureStrongholdPieces.strongholdStones);
                    this.placeDoor(p_74875_1_, p_74875_2_, p_74875_3_, this.field_143013_d, 1, 1, 0);
                    this.fillWithBlocks(p_74875_1_, p_74875_3_, 1, 1, 10, 3, 3, 10, Blocks.air, Blocks.air, false);
                    this.fillWithRandomizedBlocks(p_74875_1_, p_74875_3_, 4, 1, 1, 4, 3, 1, false, p_74875_2_, StructureStrongholdPieces.strongholdStones);
                    this.fillWithRandomizedBlocks(p_74875_1_, p_74875_3_, 4, 1, 3, 4, 3, 3, false, p_74875_2_, StructureStrongholdPieces.strongholdStones);
                    this.fillWithRandomizedBlocks(p_74875_1_, p_74875_3_, 4, 1, 7, 4, 3, 7, false, p_74875_2_, StructureStrongholdPieces.strongholdStones);
                    this.fillWithRandomizedBlocks(p_74875_1_, p_74875_3_, 4, 1, 9, 4, 3, 9, false, p_74875_2_, StructureStrongholdPieces.strongholdStones);
                    this.fillWithBlocks(p_74875_1_, p_74875_3_, 4, 1, 4, 4, 3, 6, Blocks.iron_bars, Blocks.iron_bars, false);
                    this.fillWithBlocks(p_74875_1_, p_74875_3_, 5, 1, 5, 7, 3, 5, Blocks.iron_bars, Blocks.iron_bars, false);
                    this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.iron_bars, 0, 4, 3, 2, p_74875_3_);
                    this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.iron_bars, 0, 4, 3, 8, p_74875_3_);
                    this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.iron_door, this.getMetadataWithOffset(Blocks.iron_door, 3), 4, 1, 2, p_74875_3_);
                    this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.iron_door, this.getMetadataWithOffset(Blocks.iron_door, 3) + 8, 4, 2, 2, p_74875_3_);
                    this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.iron_door, this.getMetadataWithOffset(Blocks.iron_door, 3), 4, 1, 8, p_74875_3_);
                    this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.iron_door, this.getMetadataWithOffset(Blocks.iron_door, 3) + 8, 4, 2, 8, p_74875_3_);
                    return true;
                }
            }
        }

    public static class RightTurn extends StructureStrongholdPieces.LeftTurn
        {
            private static final String __OBFID = "CL_00000495";

            /**
             * Initiates construction of the Structure Component picked, at the current Location of StructGen
             */
            public void buildComponent(StructureComponent p_74861_1_, List p_74861_2_, Random p_74861_3_)
            {
                if (this.coordBaseMode != 2 && this.coordBaseMode != 3)
                {
                    this.getNextComponentX((StructureStrongholdPieces.Stairs2)p_74861_1_, p_74861_2_, p_74861_3_, 1, 1);
                }
                else
                {
                    this.getNextComponentZ((StructureStrongholdPieces.Stairs2)p_74861_1_, p_74861_2_, p_74861_3_, 1, 1);
                }
            }

            /**
             * second Part of Structure generating, this for example places Spiderwebs, Mob Spawners, it closes
             * Mineshafts at the end, it adds Fences...
             */
            public boolean addComponentParts(World p_74875_1_, Random p_74875_2_, StructureBoundingBox p_74875_3_)
            {
                if (this.isLiquidInStructureBoundingBox(p_74875_1_, p_74875_3_))
                {
                    return false;
                }
                else
                {
                    this.fillWithRandomizedBlocks(p_74875_1_, p_74875_3_, 0, 0, 0, 4, 4, 4, true, p_74875_2_, StructureStrongholdPieces.strongholdStones);
                    this.placeDoor(p_74875_1_, p_74875_2_, p_74875_3_, this.field_143013_d, 1, 1, 0);

                    if (this.coordBaseMode != 2 && this.coordBaseMode != 3)
                    {
                        this.fillWithBlocks(p_74875_1_, p_74875_3_, 0, 1, 1, 0, 3, 3, Blocks.air, Blocks.air, false);
                    }
                    else
                    {
                        this.fillWithBlocks(p_74875_1_, p_74875_3_, 4, 1, 1, 4, 3, 3, Blocks.air, Blocks.air, false);
                    }

                    return true;
                }
            }
        }

    public static class RoomCrossing extends StructureStrongholdPieces.Stronghold
        {
            /** Items that could generate in the chest that is located in Stronghold Room Crossing. */
            public static final WeightedRandomChestContent[] strongholdRoomCrossingChestContents = new WeightedRandomChestContent[] {new WeightedRandomChestContent(Items.iron_ingot, 0, 1, 5, 10), new WeightedRandomChestContent(Items.gold_ingot, 0, 1, 3, 5), new WeightedRandomChestContent(Items.redstone, 0, 4, 9, 5), new WeightedRandomChestContent(Items.coal, 0, 3, 8, 10), new WeightedRandomChestContent(Items.bread, 0, 1, 3, 15), new WeightedRandomChestContent(Items.apple, 0, 1, 3, 15), new WeightedRandomChestContent(Items.iron_pickaxe, 0, 1, 1, 1)};
            protected int roomType;
            private static final String __OBFID = "CL_00000496";

            public RoomCrossing() {}

            public RoomCrossing(int p_i2079_1_, Random p_i2079_2_, StructureBoundingBox p_i2079_3_, int p_i2079_4_)
            {
                super(p_i2079_1_);
                this.coordBaseMode = p_i2079_4_;
                this.field_143013_d = this.getRandomDoor(p_i2079_2_);
                this.boundingBox = p_i2079_3_;
                this.roomType = p_i2079_2_.nextInt(5);
            }

            protected void func_143012_a(NBTTagCompound p_143012_1_)
            {
                super.func_143012_a(p_143012_1_);
                p_143012_1_.setInteger("Type", this.roomType);
            }

            protected void func_143011_b(NBTTagCompound p_143011_1_)
            {
                super.func_143011_b(p_143011_1_);
                this.roomType = p_143011_1_.getInteger("Type");
            }

            /**
             * Initiates construction of the Structure Component picked, at the current Location of StructGen
             */
            public void buildComponent(StructureComponent p_74861_1_, List p_74861_2_, Random p_74861_3_)
            {
                this.getNextComponentNormal((StructureStrongholdPieces.Stairs2)p_74861_1_, p_74861_2_, p_74861_3_, 4, 1);
                this.getNextComponentX((StructureStrongholdPieces.Stairs2)p_74861_1_, p_74861_2_, p_74861_3_, 1, 4);
                this.getNextComponentZ((StructureStrongholdPieces.Stairs2)p_74861_1_, p_74861_2_, p_74861_3_, 1, 4);
            }

            public static StructureStrongholdPieces.RoomCrossing findValidPlacement(List p_75012_0_, Random p_75012_1_, int p_75012_2_, int p_75012_3_, int p_75012_4_, int p_75012_5_, int p_75012_6_)
            {
                StructureBoundingBox structureboundingbox = StructureBoundingBox.getComponentToAddBoundingBox(p_75012_2_, p_75012_3_, p_75012_4_, -4, -1, 0, 11, 7, 11, p_75012_5_);
                /**
                 * returns false if the Structure Bounding Box goes below 10
                 */
                return canStrongholdGoDeeper(structureboundingbox) && StructureComponent.findIntersecting(p_75012_0_, structureboundingbox) == null ? new StructureStrongholdPieces.RoomCrossing(p_75012_6_, p_75012_1_, structureboundingbox, p_75012_5_) : null;
            }

            /**
             * second Part of Structure generating, this for example places Spiderwebs, Mob Spawners, it closes
             * Mineshafts at the end, it adds Fences...
             */
            public boolean addComponentParts(World p_74875_1_, Random p_74875_2_, StructureBoundingBox p_74875_3_)
            {
                if (this.isLiquidInStructureBoundingBox(p_74875_1_, p_74875_3_))
                {
                    return false;
                }
                else
                {
                    this.fillWithRandomizedBlocks(p_74875_1_, p_74875_3_, 0, 0, 0, 10, 6, 10, true, p_74875_2_, StructureStrongholdPieces.strongholdStones);
                    this.placeDoor(p_74875_1_, p_74875_2_, p_74875_3_, this.field_143013_d, 4, 1, 0);
                    this.fillWithBlocks(p_74875_1_, p_74875_3_, 4, 1, 10, 6, 3, 10, Blocks.air, Blocks.air, false);
                    this.fillWithBlocks(p_74875_1_, p_74875_3_, 0, 1, 4, 0, 3, 6, Blocks.air, Blocks.air, false);
                    this.fillWithBlocks(p_74875_1_, p_74875_3_, 10, 1, 4, 10, 3, 6, Blocks.air, Blocks.air, false);
                    int i;

                    switch (this.roomType)
                    {
                        case 0:
                            this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stonebrick, 0, 5, 1, 5, p_74875_3_);
                            this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stonebrick, 0, 5, 2, 5, p_74875_3_);
                            this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stonebrick, 0, 5, 3, 5, p_74875_3_);
                            this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.torch, 0, 4, 3, 5, p_74875_3_);
                            this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.torch, 0, 6, 3, 5, p_74875_3_);
                            this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.torch, 0, 5, 3, 4, p_74875_3_);
                            this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.torch, 0, 5, 3, 6, p_74875_3_);
                            this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stone_slab, 0, 4, 1, 4, p_74875_3_);
                            this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stone_slab, 0, 4, 1, 5, p_74875_3_);
                            this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stone_slab, 0, 4, 1, 6, p_74875_3_);
                            this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stone_slab, 0, 6, 1, 4, p_74875_3_);
                            this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stone_slab, 0, 6, 1, 5, p_74875_3_);
                            this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stone_slab, 0, 6, 1, 6, p_74875_3_);
                            this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stone_slab, 0, 5, 1, 4, p_74875_3_);
                            this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stone_slab, 0, 5, 1, 6, p_74875_3_);
                            break;
                        case 1:
                            for (i = 0; i < 5; ++i)
                            {
                                this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stonebrick, 0, 3, 1, 3 + i, p_74875_3_);
                                this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stonebrick, 0, 7, 1, 3 + i, p_74875_3_);
                                this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stonebrick, 0, 3 + i, 1, 3, p_74875_3_);
                                this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stonebrick, 0, 3 + i, 1, 7, p_74875_3_);
                            }

                            this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stonebrick, 0, 5, 1, 5, p_74875_3_);
                            this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stonebrick, 0, 5, 2, 5, p_74875_3_);
                            this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stonebrick, 0, 5, 3, 5, p_74875_3_);
                            this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.flowing_water, 0, 5, 4, 5, p_74875_3_);
                            break;
                        case 2:
                            for (i = 1; i <= 9; ++i)
                            {
                                this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.cobblestone, 0, 1, 3, i, p_74875_3_);
                                this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.cobblestone, 0, 9, 3, i, p_74875_3_);
                            }

                            for (i = 1; i <= 9; ++i)
                            {
                                this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.cobblestone, 0, i, 3, 1, p_74875_3_);
                                this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.cobblestone, 0, i, 3, 9, p_74875_3_);
                            }

                            this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.cobblestone, 0, 5, 1, 4, p_74875_3_);
                            this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.cobblestone, 0, 5, 1, 6, p_74875_3_);
                            this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.cobblestone, 0, 5, 3, 4, p_74875_3_);
                            this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.cobblestone, 0, 5, 3, 6, p_74875_3_);
                            this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.cobblestone, 0, 4, 1, 5, p_74875_3_);
                            this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.cobblestone, 0, 6, 1, 5, p_74875_3_);
                            this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.cobblestone, 0, 4, 3, 5, p_74875_3_);
                            this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.cobblestone, 0, 6, 3, 5, p_74875_3_);

                            for (i = 1; i <= 3; ++i)
                            {
                                this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.cobblestone, 0, 4, i, 4, p_74875_3_);
                                this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.cobblestone, 0, 6, i, 4, p_74875_3_);
                                this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.cobblestone, 0, 4, i, 6, p_74875_3_);
                                this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.cobblestone, 0, 6, i, 6, p_74875_3_);
                            }

                            this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.torch, 0, 5, 3, 5, p_74875_3_);

                            for (i = 2; i <= 8; ++i)
                            {
                                this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.planks, 0, 2, 3, i, p_74875_3_);
                                this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.planks, 0, 3, 3, i, p_74875_3_);

                                if (i <= 3 || i >= 7)
                                {
                                    this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.planks, 0, 4, 3, i, p_74875_3_);
                                    this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.planks, 0, 5, 3, i, p_74875_3_);
                                    this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.planks, 0, 6, 3, i, p_74875_3_);
                                }

                                this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.planks, 0, 7, 3, i, p_74875_3_);
                                this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.planks, 0, 8, 3, i, p_74875_3_);
                            }

                            this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.ladder, this.getMetadataWithOffset(Blocks.ladder, 4), 9, 1, 3, p_74875_3_);
                            this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.ladder, this.getMetadataWithOffset(Blocks.ladder, 4), 9, 2, 3, p_74875_3_);
                            this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.ladder, this.getMetadataWithOffset(Blocks.ladder, 4), 9, 3, 3, p_74875_3_);
                            this.generateStructureChestContents(p_74875_1_, p_74875_3_, p_74875_2_, 3, 4, 8, ChestGenHooks.getItems(STRONGHOLD_CROSSING, p_74875_2_), ChestGenHooks.getCount(STRONGHOLD_CROSSING, p_74875_2_));
                    }

                    return true;
                }
            }
        }

    public static class Stairs extends StructureStrongholdPieces.Stronghold
        {
            private boolean field_75024_a;
            private static final String __OBFID = "CL_00000498";

            public Stairs() {}

            public Stairs(int p_i2081_1_, Random p_i2081_2_, int p_i2081_3_, int p_i2081_4_)
            {
                super(p_i2081_1_);
                this.field_75024_a = true;
                this.coordBaseMode = p_i2081_2_.nextInt(4);
                this.field_143013_d = StructureStrongholdPieces.Stronghold.Door.OPENING;

                switch (this.coordBaseMode)
                {
                    case 0:
                    case 2:
                        this.boundingBox = new StructureBoundingBox(p_i2081_3_, 64, p_i2081_4_, p_i2081_3_ + 5 - 1, 74, p_i2081_4_ + 5 - 1);
                        break;
                    default:
                        this.boundingBox = new StructureBoundingBox(p_i2081_3_, 64, p_i2081_4_, p_i2081_3_ + 5 - 1, 74, p_i2081_4_ + 5 - 1);
                }
            }

            public Stairs(int p_i2082_1_, Random p_i2082_2_, StructureBoundingBox p_i2082_3_, int p_i2082_4_)
            {
                super(p_i2082_1_);
                this.field_75024_a = false;
                this.coordBaseMode = p_i2082_4_;
                this.field_143013_d = this.getRandomDoor(p_i2082_2_);
                this.boundingBox = p_i2082_3_;
            }

            protected void func_143012_a(NBTTagCompound p_143012_1_)
            {
                super.func_143012_a(p_143012_1_);
                p_143012_1_.setBoolean("Source", this.field_75024_a);
            }

            protected void func_143011_b(NBTTagCompound p_143011_1_)
            {
                super.func_143011_b(p_143011_1_);
                this.field_75024_a = p_143011_1_.getBoolean("Source");
            }

            /**
             * Initiates construction of the Structure Component picked, at the current Location of StructGen
             */
            public void buildComponent(StructureComponent p_74861_1_, List p_74861_2_, Random p_74861_3_)
            {
                if (this.field_75024_a)
                {
                    StructureStrongholdPieces.strongComponentType = StructureStrongholdPieces.Crossing.class;
                }

                this.getNextComponentNormal((StructureStrongholdPieces.Stairs2)p_74861_1_, p_74861_2_, p_74861_3_, 1, 1);
            }

            /**
             * performs some checks, then gives out a fresh Stairs component
             */
            public static StructureStrongholdPieces.Stairs getStrongholdStairsComponent(List p_75022_0_, Random p_75022_1_, int p_75022_2_, int p_75022_3_, int p_75022_4_, int p_75022_5_, int p_75022_6_)
            {
                StructureBoundingBox structureboundingbox = StructureBoundingBox.getComponentToAddBoundingBox(p_75022_2_, p_75022_3_, p_75022_4_, -1, -7, 0, 5, 11, 5, p_75022_5_);
                /**
                 * returns false if the Structure Bounding Box goes below 10
                 */
                return canStrongholdGoDeeper(structureboundingbox) && StructureComponent.findIntersecting(p_75022_0_, structureboundingbox) == null ? new StructureStrongholdPieces.Stairs(p_75022_6_, p_75022_1_, structureboundingbox, p_75022_5_) : null;
            }

            /**
             * second Part of Structure generating, this for example places Spiderwebs, Mob Spawners, it closes
             * Mineshafts at the end, it adds Fences...
             */
            public boolean addComponentParts(World p_74875_1_, Random p_74875_2_, StructureBoundingBox p_74875_3_)
            {
                if (this.isLiquidInStructureBoundingBox(p_74875_1_, p_74875_3_))
                {
                    return false;
                }
                else
                {
                    this.fillWithRandomizedBlocks(p_74875_1_, p_74875_3_, 0, 0, 0, 4, 10, 4, true, p_74875_2_, StructureStrongholdPieces.strongholdStones);
                    this.placeDoor(p_74875_1_, p_74875_2_, p_74875_3_, this.field_143013_d, 1, 7, 0);
                    this.placeDoor(p_74875_1_, p_74875_2_, p_74875_3_, StructureStrongholdPieces.Stronghold.Door.OPENING, 1, 1, 4);
                    this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stonebrick, 0, 2, 6, 1, p_74875_3_);
                    this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stonebrick, 0, 1, 5, 1, p_74875_3_);
                    this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stone_slab, 0, 1, 6, 1, p_74875_3_);
                    this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stonebrick, 0, 1, 5, 2, p_74875_3_);
                    this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stonebrick, 0, 1, 4, 3, p_74875_3_);
                    this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stone_slab, 0, 1, 5, 3, p_74875_3_);
                    this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stonebrick, 0, 2, 4, 3, p_74875_3_);
                    this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stonebrick, 0, 3, 3, 3, p_74875_3_);
                    this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stone_slab, 0, 3, 4, 3, p_74875_3_);
                    this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stonebrick, 0, 3, 3, 2, p_74875_3_);
                    this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stonebrick, 0, 3, 2, 1, p_74875_3_);
                    this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stone_slab, 0, 3, 3, 1, p_74875_3_);
                    this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stonebrick, 0, 2, 2, 1, p_74875_3_);
                    this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stonebrick, 0, 1, 1, 1, p_74875_3_);
                    this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stone_slab, 0, 1, 2, 1, p_74875_3_);
                    this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stonebrick, 0, 1, 1, 2, p_74875_3_);
                    this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stone_slab, 0, 1, 1, 3, p_74875_3_);
                    return true;
                }
            }
        }

    public static class Stairs2 extends StructureStrongholdPieces.Stairs
        {
            public StructureStrongholdPieces.PieceWeight strongholdPieceWeight;
            public StructureStrongholdPieces.PortalRoom strongholdPortalRoom;
            public List field_75026_c = new ArrayList();
            private static final String __OBFID = "CL_00000499";

            public Stairs2() {}

            public Stairs2(int p_i2083_1_, Random p_i2083_2_, int p_i2083_3_, int p_i2083_4_)
            {
                super(0, p_i2083_2_, p_i2083_3_, p_i2083_4_);
            }

            public ChunkPosition func_151553_a()
            {
                return this.strongholdPortalRoom != null ? this.strongholdPortalRoom.func_151553_a() : super.func_151553_a();
            }
        }

    public static class StairsStraight extends StructureStrongholdPieces.Stronghold
        {
            private static final String __OBFID = "CL_00000501";

            public StairsStraight() {}

            public StairsStraight(int p_i2085_1_, Random p_i2085_2_, StructureBoundingBox p_i2085_3_, int p_i2085_4_)
            {
                super(p_i2085_1_);
                this.coordBaseMode = p_i2085_4_;
                this.field_143013_d = this.getRandomDoor(p_i2085_2_);
                this.boundingBox = p_i2085_3_;
            }

            /**
             * Initiates construction of the Structure Component picked, at the current Location of StructGen
             */
            public void buildComponent(StructureComponent p_74861_1_, List p_74861_2_, Random p_74861_3_)
            {
                this.getNextComponentNormal((StructureStrongholdPieces.Stairs2)p_74861_1_, p_74861_2_, p_74861_3_, 1, 1);
            }

            public static StructureStrongholdPieces.StairsStraight findValidPlacement(List p_75028_0_, Random p_75028_1_, int p_75028_2_, int p_75028_3_, int p_75028_4_, int p_75028_5_, int p_75028_6_)
            {
                StructureBoundingBox structureboundingbox = StructureBoundingBox.getComponentToAddBoundingBox(p_75028_2_, p_75028_3_, p_75028_4_, -1, -7, 0, 5, 11, 8, p_75028_5_);
                /**
                 * returns false if the Structure Bounding Box goes below 10
                 */
                return canStrongholdGoDeeper(structureboundingbox) && StructureComponent.findIntersecting(p_75028_0_, structureboundingbox) == null ? new StructureStrongholdPieces.StairsStraight(p_75028_6_, p_75028_1_, structureboundingbox, p_75028_5_) : null;
            }

            /**
             * second Part of Structure generating, this for example places Spiderwebs, Mob Spawners, it closes
             * Mineshafts at the end, it adds Fences...
             */
            public boolean addComponentParts(World p_74875_1_, Random p_74875_2_, StructureBoundingBox p_74875_3_)
            {
                if (this.isLiquidInStructureBoundingBox(p_74875_1_, p_74875_3_))
                {
                    return false;
                }
                else
                {
                    this.fillWithRandomizedBlocks(p_74875_1_, p_74875_3_, 0, 0, 0, 4, 10, 7, true, p_74875_2_, StructureStrongholdPieces.strongholdStones);
                    this.placeDoor(p_74875_1_, p_74875_2_, p_74875_3_, this.field_143013_d, 1, 7, 0);
                    this.placeDoor(p_74875_1_, p_74875_2_, p_74875_3_, StructureStrongholdPieces.Stronghold.Door.OPENING, 1, 1, 7);
                    int i = this.getMetadataWithOffset(Blocks.stone_stairs, 2);

                    for (int j = 0; j < 6; ++j)
                    {
                        this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stone_stairs, i, 1, 6 - j, 1 + j, p_74875_3_);
                        this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stone_stairs, i, 2, 6 - j, 1 + j, p_74875_3_);
                        this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stone_stairs, i, 3, 6 - j, 1 + j, p_74875_3_);

                        if (j < 5)
                        {
                            this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stonebrick, 0, 1, 5 - j, 1 + j, p_74875_3_);
                            this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stonebrick, 0, 2, 5 - j, 1 + j, p_74875_3_);
                            this.placeBlockAtCurrentPosition(p_74875_1_, Blocks.stonebrick, 0, 3, 5 - j, 1 + j, p_74875_3_);
                        }
                    }

                    return true;
                }
            }
        }

    static class Stones extends StructureComponent.BlockSelector
        {
            private static final String __OBFID = "CL_00000497";

            private Stones() {}

            /**
             * picks Block Ids and Metadata (Silverfish)
             */
            public void selectBlocks(Random p_75062_1_, int p_75062_2_, int p_75062_3_, int p_75062_4_, boolean p_75062_5_)
            {
                if (p_75062_5_)
                {
                    this.field_151562_a = Blocks.stonebrick;
                    float f = p_75062_1_.nextFloat();

                    if (f < 0.2F)
                    {
                        this.selectedBlockMetaData = 2;
                    }
                    else if (f < 0.5F)
                    {
                        this.selectedBlockMetaData = 1;
                    }
                    else if (f < 0.55F)
                    {
                        this.field_151562_a = Blocks.monster_egg;
                        this.selectedBlockMetaData = 2;
                    }
                    else
                    {
                        this.selectedBlockMetaData = 0;
                    }
                }
                else
                {
                    this.field_151562_a = Blocks.air;
                    this.selectedBlockMetaData = 0;
                }
            }

            Stones(Object p_i2080_1_)
            {
                this();
            }
        }

    public static class Straight extends StructureStrongholdPieces.Stronghold
        {
            private boolean expandsX;
            private boolean expandsZ;
            private static final String __OBFID = "CL_00000500";

            public Straight() {}

            public Straight(int p_i2084_1_, Random p_i2084_2_, StructureBoundingBox p_i2084_3_, int p_i2084_4_)
            {
                super(p_i2084_1_);
                this.coordBaseMode = p_i2084_4_;
                this.field_143013_d = this.getRandomDoor(p_i2084_2_);
                this.boundingBox = p_i2084_3_;
                this.expandsX = p_i2084_2_.nextInt(2) == 0;
                this.expandsZ = p_i2084_2_.nextInt(2) == 0;
            }

            protected void func_143012_a(NBTTagCompound p_143012_1_)
            {
                super.func_143012_a(p_143012_1_);
                p_143012_1_.setBoolean("Left", this.expandsX);
                p_143012_1_.setBoolean("Right", this.expandsZ);
            }

            protected void func_143011_b(NBTTagCompound p_143011_1_)
            {
                super.func_143011_b(p_143011_1_);
                this.expandsX = p_143011_1_.getBoolean("Left");
                this.expandsZ = p_143011_1_.getBoolean("Right");
            }

            /**
             * Initiates construction of the Structure Component picked, at the current Location of StructGen
             */
            public void buildComponent(StructureComponent p_74861_1_, List p_74861_2_, Random p_74861_3_)
            {
                this.getNextComponentNormal((StructureStrongholdPieces.Stairs2)p_74861_1_, p_74861_2_, p_74861_3_, 1, 1);

                if (this.expandsX)
                {
                    this.getNextComponentX((StructureStrongholdPieces.Stairs2)p_74861_1_, p_74861_2_, p_74861_3_, 1, 2);
                }

                if (this.expandsZ)
                {
                    this.getNextComponentZ((StructureStrongholdPieces.Stairs2)p_74861_1_, p_74861_2_, p_74861_3_, 1, 2);
                }
            }

            public static StructureStrongholdPieces.Straight findValidPlacement(List p_75018_0_, Random p_75018_1_, int p_75018_2_, int p_75018_3_, int p_75018_4_, int p_75018_5_, int p_75018_6_)
            {
                StructureBoundingBox structureboundingbox = StructureBoundingBox.getComponentToAddBoundingBox(p_75018_2_, p_75018_3_, p_75018_4_, -1, -1, 0, 5, 5, 7, p_75018_5_);
                /**
                 * returns false if the Structure Bounding Box goes below 10
                 */
                return canStrongholdGoDeeper(structureboundingbox) && StructureComponent.findIntersecting(p_75018_0_, structureboundingbox) == null ? new StructureStrongholdPieces.Straight(p_75018_6_, p_75018_1_, structureboundingbox, p_75018_5_) : null;
            }

            /**
             * second Part of Structure generating, this for example places Spiderwebs, Mob Spawners, it closes
             * Mineshafts at the end, it adds Fences...
             */
            public boolean addComponentParts(World p_74875_1_, Random p_74875_2_, StructureBoundingBox p_74875_3_)
            {
                if (this.isLiquidInStructureBoundingBox(p_74875_1_, p_74875_3_))
                {
                    return false;
                }
                else
                {
                    this.fillWithRandomizedBlocks(p_74875_1_, p_74875_3_, 0, 0, 0, 4, 4, 6, true, p_74875_2_, StructureStrongholdPieces.strongholdStones);
                    this.placeDoor(p_74875_1_, p_74875_2_, p_74875_3_, this.field_143013_d, 1, 1, 0);
                    this.placeDoor(p_74875_1_, p_74875_2_, p_74875_3_, StructureStrongholdPieces.Stronghold.Door.OPENING, 1, 1, 6);
                    this.func_151552_a(p_74875_1_, p_74875_3_, p_74875_2_, 0.1F, 1, 2, 1, Blocks.torch, 0);
                    this.func_151552_a(p_74875_1_, p_74875_3_, p_74875_2_, 0.1F, 3, 2, 1, Blocks.torch, 0);
                    this.func_151552_a(p_74875_1_, p_74875_3_, p_74875_2_, 0.1F, 1, 2, 5, Blocks.torch, 0);
                    this.func_151552_a(p_74875_1_, p_74875_3_, p_74875_2_, 0.1F, 3, 2, 5, Blocks.torch, 0);

                    if (this.expandsX)
                    {
                        this.fillWithBlocks(p_74875_1_, p_74875_3_, 0, 1, 2, 0, 3, 4, Blocks.air, Blocks.air, false);
                    }

                    if (this.expandsZ)
                    {
                        this.fillWithBlocks(p_74875_1_, p_74875_3_, 4, 1, 2, 4, 3, 4, Blocks.air, Blocks.air, false);
                    }

                    return true;
                }
            }
        }

    public abstract static class Stronghold extends StructureComponent
        {
            protected StructureStrongholdPieces.Stronghold.Door field_143013_d;
            private static final String __OBFID = "CL_00000503";

            public Stronghold()
            {
                this.field_143013_d = StructureStrongholdPieces.Stronghold.Door.OPENING;
            }

            protected Stronghold(int p_i2087_1_)
            {
                super(p_i2087_1_);
                this.field_143013_d = StructureStrongholdPieces.Stronghold.Door.OPENING;
            }

            protected void func_143012_a(NBTTagCompound p_143012_1_)
            {
                p_143012_1_.setString("EntryDoor", this.field_143013_d.name());
            }

            protected void func_143011_b(NBTTagCompound p_143011_1_)
            {
                this.field_143013_d = StructureStrongholdPieces.Stronghold.Door.valueOf(p_143011_1_.getString("EntryDoor"));
            }

            /**
             * builds a door of the enumerated types (empty opening is a door)
             */
            protected void placeDoor(World p_74990_1_, Random p_74990_2_, StructureBoundingBox p_74990_3_, StructureStrongholdPieces.Stronghold.Door p_74990_4_, int p_74990_5_, int p_74990_6_, int p_74990_7_)
            {
                switch (StructureStrongholdPieces.SwitchDoor.doorEnum[p_74990_4_.ordinal()])
                {
                    case 1:
                    default:
                        this.fillWithBlocks(p_74990_1_, p_74990_3_, p_74990_5_, p_74990_6_, p_74990_7_, p_74990_5_ + 3 - 1, p_74990_6_ + 3 - 1, p_74990_7_, Blocks.air, Blocks.air, false);
                        break;
                    case 2:
                        this.placeBlockAtCurrentPosition(p_74990_1_, Blocks.stonebrick, 0, p_74990_5_, p_74990_6_, p_74990_7_, p_74990_3_);
                        this.placeBlockAtCurrentPosition(p_74990_1_, Blocks.stonebrick, 0, p_74990_5_, p_74990_6_ + 1, p_74990_7_, p_74990_3_);
                        this.placeBlockAtCurrentPosition(p_74990_1_, Blocks.stonebrick, 0, p_74990_5_, p_74990_6_ + 2, p_74990_7_, p_74990_3_);
                        this.placeBlockAtCurrentPosition(p_74990_1_, Blocks.stonebrick, 0, p_74990_5_ + 1, p_74990_6_ + 2, p_74990_7_, p_74990_3_);
                        this.placeBlockAtCurrentPosition(p_74990_1_, Blocks.stonebrick, 0, p_74990_5_ + 2, p_74990_6_ + 2, p_74990_7_, p_74990_3_);
                        this.placeBlockAtCurrentPosition(p_74990_1_, Blocks.stonebrick, 0, p_74990_5_ + 2, p_74990_6_ + 1, p_74990_7_, p_74990_3_);
                        this.placeBlockAtCurrentPosition(p_74990_1_, Blocks.stonebrick, 0, p_74990_5_ + 2, p_74990_6_, p_74990_7_, p_74990_3_);
                        this.placeBlockAtCurrentPosition(p_74990_1_, Blocks.wooden_door, 0, p_74990_5_ + 1, p_74990_6_, p_74990_7_, p_74990_3_);
                        this.placeBlockAtCurrentPosition(p_74990_1_, Blocks.wooden_door, 8, p_74990_5_ + 1, p_74990_6_ + 1, p_74990_7_, p_74990_3_);
                        break;
                    case 3:
                        this.placeBlockAtCurrentPosition(p_74990_1_, Blocks.air, 0, p_74990_5_ + 1, p_74990_6_, p_74990_7_, p_74990_3_);
                        this.placeBlockAtCurrentPosition(p_74990_1_, Blocks.air, 0, p_74990_5_ + 1, p_74990_6_ + 1, p_74990_7_, p_74990_3_);
                        this.placeBlockAtCurrentPosition(p_74990_1_, Blocks.iron_bars, 0, p_74990_5_, p_74990_6_, p_74990_7_, p_74990_3_);
                        this.placeBlockAtCurrentPosition(p_74990_1_, Blocks.iron_bars, 0, p_74990_5_, p_74990_6_ + 1, p_74990_7_, p_74990_3_);
                        this.placeBlockAtCurrentPosition(p_74990_1_, Blocks.iron_bars, 0, p_74990_5_, p_74990_6_ + 2, p_74990_7_, p_74990_3_);
                        this.placeBlockAtCurrentPosition(p_74990_1_, Blocks.iron_bars, 0, p_74990_5_ + 1, p_74990_6_ + 2, p_74990_7_, p_74990_3_);
                        this.placeBlockAtCurrentPosition(p_74990_1_, Blocks.iron_bars, 0, p_74990_5_ + 2, p_74990_6_ + 2, p_74990_7_, p_74990_3_);
                        this.placeBlockAtCurrentPosition(p_74990_1_, Blocks.iron_bars, 0, p_74990_5_ + 2, p_74990_6_ + 1, p_74990_7_, p_74990_3_);
                        this.placeBlockAtCurrentPosition(p_74990_1_, Blocks.iron_bars, 0, p_74990_5_ + 2, p_74990_6_, p_74990_7_, p_74990_3_);
                        break;
                    case 4:
                        this.placeBlockAtCurrentPosition(p_74990_1_, Blocks.stonebrick, 0, p_74990_5_, p_74990_6_, p_74990_7_, p_74990_3_);
                        this.placeBlockAtCurrentPosition(p_74990_1_, Blocks.stonebrick, 0, p_74990_5_, p_74990_6_ + 1, p_74990_7_, p_74990_3_);
                        this.placeBlockAtCurrentPosition(p_74990_1_, Blocks.stonebrick, 0, p_74990_5_, p_74990_6_ + 2, p_74990_7_, p_74990_3_);
                        this.placeBlockAtCurrentPosition(p_74990_1_, Blocks.stonebrick, 0, p_74990_5_ + 1, p_74990_6_ + 2, p_74990_7_, p_74990_3_);
                        this.placeBlockAtCurrentPosition(p_74990_1_, Blocks.stonebrick, 0, p_74990_5_ + 2, p_74990_6_ + 2, p_74990_7_, p_74990_3_);
                        this.placeBlockAtCurrentPosition(p_74990_1_, Blocks.stonebrick, 0, p_74990_5_ + 2, p_74990_6_ + 1, p_74990_7_, p_74990_3_);
                        this.placeBlockAtCurrentPosition(p_74990_1_, Blocks.stonebrick, 0, p_74990_5_ + 2, p_74990_6_, p_74990_7_, p_74990_3_);
                        this.placeBlockAtCurrentPosition(p_74990_1_, Blocks.iron_door, 0, p_74990_5_ + 1, p_74990_6_, p_74990_7_, p_74990_3_);
                        this.placeBlockAtCurrentPosition(p_74990_1_, Blocks.iron_door, 8, p_74990_5_ + 1, p_74990_6_ + 1, p_74990_7_, p_74990_3_);
                        this.placeBlockAtCurrentPosition(p_74990_1_, Blocks.stone_button, this.getMetadataWithOffset(Blocks.stone_button, 4), p_74990_5_ + 2, p_74990_6_ + 1, p_74990_7_ + 1, p_74990_3_);
                        this.placeBlockAtCurrentPosition(p_74990_1_, Blocks.stone_button, this.getMetadataWithOffset(Blocks.stone_button, 3), p_74990_5_ + 2, p_74990_6_ + 1, p_74990_7_ - 1, p_74990_3_);
                }
            }

            protected StructureStrongholdPieces.Stronghold.Door getRandomDoor(Random p_74988_1_)
            {
                int i = p_74988_1_.nextInt(5);

                switch (i)
                {
                    case 0:
                    case 1:
                    default:
                        return StructureStrongholdPieces.Stronghold.Door.OPENING;
                    case 2:
                        return StructureStrongholdPieces.Stronghold.Door.WOOD_DOOR;
                    case 3:
                        return StructureStrongholdPieces.Stronghold.Door.GRATES;
                    case 4:
                        return StructureStrongholdPieces.Stronghold.Door.IRON_DOOR;
                }
            }

            /**
             * Gets the next component in any cardinal direction
             */
            protected StructureComponent getNextComponentNormal(StructureStrongholdPieces.Stairs2 p_74986_1_, List p_74986_2_, Random p_74986_3_, int p_74986_4_, int p_74986_5_)
            {
                switch (this.coordBaseMode)
                {
                    case 0:
                        return StructureStrongholdPieces.getNextValidComponent(p_74986_1_, p_74986_2_, p_74986_3_, this.boundingBox.minX + p_74986_4_, this.boundingBox.minY + p_74986_5_, this.boundingBox.maxZ + 1, this.coordBaseMode, this.getComponentType());
                    case 1:
                        return StructureStrongholdPieces.getNextValidComponent(p_74986_1_, p_74986_2_, p_74986_3_, this.boundingBox.minX - 1, this.boundingBox.minY + p_74986_5_, this.boundingBox.minZ + p_74986_4_, this.coordBaseMode, this.getComponentType());
                    case 2:
                        return StructureStrongholdPieces.getNextValidComponent(p_74986_1_, p_74986_2_, p_74986_3_, this.boundingBox.minX + p_74986_4_, this.boundingBox.minY + p_74986_5_, this.boundingBox.minZ - 1, this.coordBaseMode, this.getComponentType());
                    case 3:
                        return StructureStrongholdPieces.getNextValidComponent(p_74986_1_, p_74986_2_, p_74986_3_, this.boundingBox.maxX + 1, this.boundingBox.minY + p_74986_5_, this.boundingBox.minZ + p_74986_4_, this.coordBaseMode, this.getComponentType());
                    default:
                        return null;
                }
            }

            /**
             * Gets the next component in the +/- X direction
             */
            protected StructureComponent getNextComponentX(StructureStrongholdPieces.Stairs2 p_74989_1_, List p_74989_2_, Random p_74989_3_, int p_74989_4_, int p_74989_5_)
            {
                switch (this.coordBaseMode)
                {
                    case 0:
                        return StructureStrongholdPieces.getNextValidComponent(p_74989_1_, p_74989_2_, p_74989_3_, this.boundingBox.minX - 1, this.boundingBox.minY + p_74989_4_, this.boundingBox.minZ + p_74989_5_, 1, this.getComponentType());
                    case 1:
                        return StructureStrongholdPieces.getNextValidComponent(p_74989_1_, p_74989_2_, p_74989_3_, this.boundingBox.minX + p_74989_5_, this.boundingBox.minY + p_74989_4_, this.boundingBox.minZ - 1, 2, this.getComponentType());
                    case 2:
                        return StructureStrongholdPieces.getNextValidComponent(p_74989_1_, p_74989_2_, p_74989_3_, this.boundingBox.minX - 1, this.boundingBox.minY + p_74989_4_, this.boundingBox.minZ + p_74989_5_, 1, this.getComponentType());
                    case 3:
                        return StructureStrongholdPieces.getNextValidComponent(p_74989_1_, p_74989_2_, p_74989_3_, this.boundingBox.minX + p_74989_5_, this.boundingBox.minY + p_74989_4_, this.boundingBox.minZ - 1, 2, this.getComponentType());
                    default:
                        return null;
                }
            }

            /**
             * Gets the next component in the +/- Z direction
             */
            protected StructureComponent getNextComponentZ(StructureStrongholdPieces.Stairs2 p_74987_1_, List p_74987_2_, Random p_74987_3_, int p_74987_4_, int p_74987_5_)
            {
                switch (this.coordBaseMode)
                {
                    case 0:
                        return StructureStrongholdPieces.getNextValidComponent(p_74987_1_, p_74987_2_, p_74987_3_, this.boundingBox.maxX + 1, this.boundingBox.minY + p_74987_4_, this.boundingBox.minZ + p_74987_5_, 3, this.getComponentType());
                    case 1:
                        return StructureStrongholdPieces.getNextValidComponent(p_74987_1_, p_74987_2_, p_74987_3_, this.boundingBox.minX + p_74987_5_, this.boundingBox.minY + p_74987_4_, this.boundingBox.maxZ + 1, 0, this.getComponentType());
                    case 2:
                        return StructureStrongholdPieces.getNextValidComponent(p_74987_1_, p_74987_2_, p_74987_3_, this.boundingBox.maxX + 1, this.boundingBox.minY + p_74987_4_, this.boundingBox.minZ + p_74987_5_, 3, this.getComponentType());
                    case 3:
                        return StructureStrongholdPieces.getNextValidComponent(p_74987_1_, p_74987_2_, p_74987_3_, this.boundingBox.minX + p_74987_5_, this.boundingBox.minY + p_74987_4_, this.boundingBox.maxZ + 1, 0, this.getComponentType());
                    default:
                        return null;
                }
            }

            /**
             * returns false if the Structure Bounding Box goes below 10
             */
            protected static boolean canStrongholdGoDeeper(StructureBoundingBox p_74991_0_)
            {
                return p_74991_0_ != null && p_74991_0_.minY > 10;
            }

            public static enum Door
            {
                OPENING,
                WOOD_DOOR,
                GRATES,
                IRON_DOOR;

                private static final String __OBFID = "CL_00000504";
            }
        }

    static final class SwitchDoor
        {
            static final int[] doorEnum = new int[StructureStrongholdPieces.Stronghold.Door.values().length];
            private static final String __OBFID = "CL_00000486";

            static
            {
                try
                {
                    doorEnum[StructureStrongholdPieces.Stronghold.Door.OPENING.ordinal()] = 1;
                }
                catch (NoSuchFieldError var4)
                {
                    ;
                }

                try
                {
                    doorEnum[StructureStrongholdPieces.Stronghold.Door.WOOD_DOOR.ordinal()] = 2;
                }
                catch (NoSuchFieldError var3)
                {
                    ;
                }

                try
                {
                    doorEnum[StructureStrongholdPieces.Stronghold.Door.GRATES.ordinal()] = 3;
                }
                catch (NoSuchFieldError var2)
                {
                    ;
                }

                try
                {
                    doorEnum[StructureStrongholdPieces.Stronghold.Door.IRON_DOOR.ordinal()] = 4;
                }
                catch (NoSuchFieldError var1)
                {
                    ;
                }
            }
        }
}