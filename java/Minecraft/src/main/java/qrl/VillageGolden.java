package qrl;

import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.List;
import java.util.Random;
import net.minecraft.block.Block;
import net.minecraft.init.Biomes;
import net.minecraft.init.Bootstrap;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.biome.Biome;
import net.minecraft.world.biome.BiomeProvider;
import net.minecraft.world.gen.structure.StructureBoundingBox;
import net.minecraft.world.gen.structure.StructureComponent;
import net.minecraft.world.gen.structure.StructureVillagePieces;

/** Exact 1.11.2 recursive village-piece graph signature for the C oracle. */
public final class VillageGolden {
    private static final Field PATH_LENGTH=field(StructureVillagePieces.Path.class,"length");
    private static final Field ROOF=field(StructureVillagePieces.House4Garden.class,"isRoofAccessible");
    private static final Field TALL=field(StructureVillagePieces.WoodHut.class,"isTallHouse");
    private static final Field TABLE=field(StructureVillagePieces.WoodHut.class,"tablePosition");
    private static final Field[] FIELD1={field(StructureVillagePieces.Field1.class,"cropTypeA"),field(StructureVillagePieces.Field1.class,"cropTypeB"),field(StructureVillagePieces.Field1.class,"cropTypeC"),field(StructureVillagePieces.Field1.class,"cropTypeD")};
    private static final Field[] FIELD2={field(StructureVillagePieces.Field2.class,"cropTypeA"),field(StructureVillagePieces.Field2.class,"cropTypeB")};
    private static final Field STRUCTURE_TYPE=field(StructureVillagePieces.Village.class,"structureType");
    private static final Field ZOMBIE=field(StructureVillagePieces.Village.class,"isZombieInfested");

    private static Field field(Class<?> owner,String name) {
        try {
            Field value=owner.getDeclaredField(name);
            value.setAccessible(true);
            return value;
        } catch (Exception e) { throw new RuntimeException(e); }
    }

    private static final class FixedProvider extends BiomeProvider {
        private final Biome biome;
        FixedProvider(Biome biome) { this.biome=biome; }
        @Override public Biome getBiome(BlockPos pos,Biome fallback) { return biome; }
    }

    private static int kind(StructureComponent piece) {
        if (piece instanceof StructureVillagePieces.Start) return 0;
        if (piece instanceof StructureVillagePieces.Path) return 1;
        if (piece instanceof StructureVillagePieces.Torch) return 2;
        if (piece instanceof StructureVillagePieces.House4Garden) return 3;
        if (piece instanceof StructureVillagePieces.Church) return 4;
        if (piece instanceof StructureVillagePieces.House1) return 5;
        if (piece instanceof StructureVillagePieces.WoodHut) return 6;
        if (piece instanceof StructureVillagePieces.Hall) return 7;
        if (piece instanceof StructureVillagePieces.Field1) return 8;
        if (piece instanceof StructureVillagePieces.Field2) return 9;
        if (piece instanceof StructureVillagePieces.House2) return 10;
        if (piece instanceof StructureVillagePieces.House3) return 11;
        throw new AssertionError(piece.getClass());
    }

    private static int[] extras(StructureComponent piece) throws Exception {
        int[] e=new int[4];
        if (piece instanceof StructureVillagePieces.Start) {
            e[0]=(Integer)STRUCTURE_TYPE.get(piece);
            e[1]=(Boolean)ZOMBIE.get(piece)?1:0;
        } else if (piece instanceof StructureVillagePieces.Path) {
            e[0]=(Integer)PATH_LENGTH.get(piece);
        } else if (piece instanceof StructureVillagePieces.House4Garden) {
            e[0]=(Boolean)ROOF.get(piece)?1:0;
        } else if (piece instanceof StructureVillagePieces.WoodHut) {
            e[0]=(Boolean)TALL.get(piece)?1:0;
            e[1]=(Integer)TABLE.get(piece);
        } else if (piece instanceof StructureVillagePieces.Field1) {
            for (int i=0;i<4;i++) e[i]=Block.getIdFromBlock((Block)FIELD1[i].get(piece));
        } else if (piece instanceof StructureVillagePieces.Field2) {
            for (int i=0;i<2;i++) e[i]=Block.getIdFromBlock((Block)FIELD2[i].get(piece));
        }
        return e;
    }

    private static long add(long hash,int value) {
        hash^=value & 0xffffffffL;
        return hash*0x100000001b3L;
    }

    private static void one(long seed,int x,int z,Biome biome,int biomeType) throws Exception {
        Random random=new Random(seed);
        List<StructureVillagePieces.PieceWeight> weights=
            StructureVillagePieces.getStructureVillageWeightedPieceList(random,0);
        StructureVillagePieces.Start start=new StructureVillagePieces.Start(
            new FixedProvider(biome),0,random,x,z,weights,0);
        List<StructureComponent> pieces=new ArrayList<StructureComponent>();
        pieces.add(start);
        start.buildComponent(start,pieces,random);
        while (!start.pendingRoads.isEmpty() || !start.pendingHouses.isEmpty()) {
            if (!start.pendingRoads.isEmpty()) {
                int i=random.nextInt(start.pendingRoads.size());
                StructureComponent piece=start.pendingRoads.remove(i);
                piece.buildComponent(start,pieces,random);
            } else {
                int i=random.nextInt(start.pendingHouses.size());
                StructureComponent piece=start.pendingHouses.remove(i);
                piece.buildComponent(start,pieces,random);
            }
        }
        int nonRoads=0;
        long hash=0xcbf29ce484222325L;
        for (int index=0;index<pieces.size();index++) {
            StructureComponent piece=pieces.get(index);
            StructureBoundingBox box=piece.getBoundingBox();
            int k=kind(piece);
            int[] e=extras(piece);
            if (!(piece instanceof StructureVillagePieces.Road)) nonRoads++;
            hash=add(hash,k); hash=add(hash,piece.getComponentType());
            hash=add(hash,piece.getCoordBaseMode().getIndex());
            hash=add(hash,box.minX); hash=add(hash,box.minY); hash=add(hash,box.minZ);
            hash=add(hash,box.maxX); hash=add(hash,box.maxY); hash=add(hash,box.maxZ);
            for (int value:e) hash=add(hash,value);
            if (System.getenv("VILLAGE_VERBOSE") != null)
                System.out.printf("P %d %d %d %d %d %d %d %d %d %d %d %d %d %d%n",
                    index,k,piece.getComponentType(),piece.getCoordBaseMode().getIndex(),
                    box.minX,box.minY,box.minZ,box.maxX,box.maxY,box.maxZ,
                    e[0],e[1],e[2],e[3]);
        }
        System.out.printf("%d %d %d %d %d %d %016x%n",
            seed,x,z,biomeType,pieces.size(),nonRoads>2?1:0,hash);
    }

    public static void main(String[] args) throws Exception {
        Bootstrap.register();
        one(0L,2,2,Biomes.PLAINS,0);
        one(1L,-62,114,Biomes.DESERT,1);
        one(123456789L,962,-654,Biomes.SAVANNA,2);
        one(-99887766L,-1438,-1150,Biomes.TAIGA,3);
        one(49L,322,706,Biomes.PLAINS,0);
        one(9876543212345L,-318,514,Biomes.DESERT,1);
    }
}
