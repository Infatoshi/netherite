package qrl;

import java.io.FileDescriptor;
import java.io.FileOutputStream;
import java.io.PrintStream;
import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Random;
import net.minecraft.block.Block;
import net.minecraft.block.state.IBlockState;
import net.minecraft.entity.Entity;
import net.minecraft.entity.passive.EntityVillager;
import net.minecraft.init.Biomes;
import net.minecraft.init.Blocks;
import net.minecraft.init.Bootstrap;
import net.minecraft.profiler.Profiler;
import net.minecraft.tileentity.TileEntity;
import net.minecraft.tileentity.TileEntityChest;
import net.minecraft.util.EnumFacing;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.GameType;
import net.minecraft.world.DifficultyInstance;
import net.minecraft.world.EnumDifficulty;
import net.minecraft.world.World;
import net.minecraft.world.WorldProviderSurface;
import net.minecraft.world.WorldSettings;
import net.minecraft.world.WorldType;
import net.minecraft.world.biome.Biome;
import net.minecraft.world.biome.BiomeProvider;
import net.minecraft.world.chunk.IChunkProvider;
import net.minecraft.world.gen.structure.StructureBoundingBox;
import net.minecraft.world.gen.structure.StructureComponent;
import net.minecraft.world.gen.structure.StructureVillagePieces;
import net.minecraft.world.storage.SaveHandlerMP;
import net.minecraft.world.storage.WorldInfo;

/** Emits compact, owned-build village piece templates from the real game. */
public final class VillageTemplateGolden {
    private static final PrintStream RAW=new PrintStream(
        new FileOutputStream(FileDescriptor.out));
    private static final Field ROOF=field(StructureVillagePieces.House4Garden.class,"isRoofAccessible");
    private static final Field TALL=field(StructureVillagePieces.WoodHut.class,"isTallHouse");
    private static final Field TABLE=field(StructureVillagePieces.WoodHut.class,"tablePosition");
    private static final Field ZOMBIE=field(StructureVillagePieces.Village.class,"isZombieInfested");

    private static Field field(Class<?> owner,String name) {
        try {
            Field value=owner.getDeclaredField(name);
            value.setAccessible(true);
            return value;
        } catch (Exception e) { throw new RuntimeException(e); }
    }

    private static final class FixedProvider extends BiomeProvider {
        @Override public Biome getBiome(BlockPos pos,Biome fallback) {
            return Biomes.PLAINS;
        }
    }

    private static final class MemoryWorld extends World {
        final Map<BlockPos,IBlockState> blocks=new HashMap<BlockPos,IBlockState>();
        final Map<BlockPos,TileEntity> tiles=new HashMap<BlockPos,TileEntity>();
        final List<Entity> entities=new ArrayList<Entity>();
        MemoryWorld() {
            super(new SaveHandlerMP(),new WorldInfo(new WorldSettings(0L,
                GameType.SURVIVAL,true,false,WorldType.DEFAULT),"village-template"),
                new WorldProviderSurface(),new Profiler(),false);
            this.provider.setWorld(this);
        }
        protected IChunkProvider createChunkProvider() { return null; }
        protected boolean isChunkLoaded(int x,int z,boolean allowEmpty) { return true; }
        public IBlockState getBlockState(BlockPos pos) {
            IBlockState state=blocks.get(pos);
            return state != null ? state : pos.getY()<64
                ? Blocks.STONE.getDefaultState() : Blocks.AIR.getDefaultState();
        }
        public boolean setBlockState(BlockPos pos,IBlockState state,int flags) {
            BlockPos key=pos.toImmutable();
            blocks.put(key,state);
            if (state.getBlock()==Blocks.CHEST) {
                TileEntityChest chest=new TileEntityChest();
                chest.setWorld(this); chest.setPos(key); tiles.put(key,chest);
            } else tiles.remove(key);
            return true;
        }
        public TileEntity getTileEntity(BlockPos pos) { return tiles.get(pos); }
        public BlockPos getTopSolidOrLiquidBlock(BlockPos pos) {
            return new BlockPos(pos.getX(),64,pos.getZ());
        }
        public DifficultyInstance getDifficultyForLocation(BlockPos pos) {
            return new DifficultyInstance(EnumDifficulty.NORMAL,0L,0L,0.0F);
        }
        public boolean spawnEntity(Entity entity) {
            entities.add(entity); return true;
        }
        public void notifyNeighborsOfStateChange(BlockPos pos,Block block,
                                                  boolean observers) {}
        public void updateComparatorOutputLevel(BlockPos pos,Block block) {}
    }

    private static StructureVillagePieces.Start start() throws Exception {
        Random random=new Random(0L);
        StructureVillagePieces.Start start=new StructureVillagePieces.Start(
            new FixedProvider(),0,random,0,0,
            StructureVillagePieces.getStructureVillageWeightedPieceList(random,0),0);
        ZOMBIE.set(start,false);
        return start;
    }

    private static int[] dimensions(int kind) {
        switch (kind) {
            case 2:return new int[]{3,4,2};
            case 3:return new int[]{5,6,5};
            case 4:return new int[]{5,12,9};
            case 5:return new int[]{9,9,6};
            case 6:return new int[]{4,6,5};
            case 7:return new int[]{9,7,11};
            case 8:return new int[]{13,4,9};
            case 9:return new int[]{7,4,9};
            case 10:return new int[]{10,6,7};
            case 11:return new int[]{9,7,12};
            default:return new int[]{6,15,6};
        }
    }

    private static StructureComponent piece(int kind,int variant,EnumFacing facing)
            throws Exception {
        StructureVillagePieces.Start start=start();
        if (kind==0) {
            start.setCoordBaseMode(facing);
            return start;
        }
        int[] d=dimensions(kind);
        int ax=facing==EnumFacing.WEST ? d[2]-1 : 0;
        int az=facing==EnumFacing.NORTH ? d[2]-1 : 0;
        StructureBoundingBox box=StructureBoundingBox.getComponentToAddBoundingBox(
            ax,64,az,0,0,0,d[0],d[1],d[2],facing);
        Random random=new Random(9000L+kind*31L+variant);
        StructureComponent result;
        switch (kind) {
            case 2: result=new StructureVillagePieces.Torch(start,1,random,box,facing); break;
            case 3:
                result=new StructureVillagePieces.House4Garden(start,1,random,box,facing);
                ROOF.set(result,variant!=0); break;
            case 4: result=new StructureVillagePieces.Church(start,1,random,box,facing); break;
            case 5: result=new StructureVillagePieces.House1(start,1,random,box,facing); break;
            case 6:
                result=new StructureVillagePieces.WoodHut(start,1,random,box,facing);
                TALL.set(result,variant/3!=0); TABLE.set(result,variant%3); break;
            case 7: result=new StructureVillagePieces.Hall(start,1,random,box,facing); break;
            case 8: result=new StructureVillagePieces.Field1(start,1,random,box,facing); break;
            case 9: result=new StructureVillagePieces.Field2(start,1,random,box,facing); break;
            case 10: result=new StructureVillagePieces.House2(start,1,random,box,facing); break;
            case 11: result=new StructureVillagePieces.House3(start,1,random,box,facing); break;
            default: throw new AssertionError(kind);
        }
        ZOMBIE.set(result,false);
        return result;
    }

    private static void emit(int kind,int variant,EnumFacing facing) throws Exception {
        MemoryWorld world=new MemoryWorld();
        StructureComponent piece=piece(kind,variant,facing);
        StructureBoundingBox all=new StructureBoundingBox(-64,0,-64,64,255,64);
        if (!piece.addComponentParts(world,new Random(0x51eedL),all))
            throw new AssertionError("placement failed");
        StructureBoundingBox box=piece.getBoundingBox();
        List<Map.Entry<BlockPos,IBlockState>> cells=
            new ArrayList<Map.Entry<BlockPos,IBlockState>>(world.blocks.entrySet());
        Collections.sort(cells,new Comparator<Map.Entry<BlockPos,IBlockState>>() {
            public int compare(Map.Entry<BlockPos,IBlockState> a,
                               Map.Entry<BlockPos,IBlockState> b) {
                BlockPos p=a.getKey(),q=b.getKey();
                if (p.getY()!=q.getY()) return p.getY()-q.getY();
                if (p.getZ()!=q.getZ()) return p.getZ()-q.getZ();
                return p.getX()-q.getX();
            }
        });
        List<String> output=new ArrayList<String>();
        for (Map.Entry<BlockPos,IBlockState> entry:cells) {
            BlockPos pos=entry.getKey();
            if (pos.getX()<box.minX-1 || pos.getX()>box.maxX+1
                    || pos.getZ()<box.minZ-1 || pos.getZ()>box.maxZ+1
                    || pos.getY()<box.minY-1 || pos.getY()>box.maxY+1)
                continue;
            IBlockState state=entry.getValue();
            int id=Block.getIdFromBlock(state.getBlock());
            if (id==59 || id==141 || id==142 || id==207) continue;
            int meta=state.getBlock().getMetaFromState(state);
            output.add(String.format("B %d %d %d %d",pos.getX()-box.minX,
                pos.getY()-box.minY,pos.getZ()-box.minZ,(id<<4)|(meta&15)));
        }
        RAW.printf("T %d %d %d %d %d %d %d %d%n",kind,variant,
            facing.getIndex(),box.getXSize(),box.getYSize(),box.getZSize(),
            box.minY,output.size());
        for (String line:output) RAW.println(line);
        Collections.sort(world.entities,new Comparator<Entity>() {
            public int compare(Entity a,Entity b) {
                int value=Double.compare(a.posX,b.posX);
                if (value!=0) return value;
                value=Double.compare(a.posY,b.posY);
                return value!=0 ? value : Double.compare(a.posZ,b.posZ);
            }
        });
        for (Entity entity:world.entities) {
            if (!(entity instanceof EntityVillager)) continue;
            EntityVillager villager=(EntityVillager)entity;
            RAW.printf("V %d %d %d %d %d %d %d%n",kind,variant,
                facing.getIndex(),(int)Math.floor(villager.posX)-box.minX,
                (int)Math.floor(villager.posY)-box.minY,
                (int)Math.floor(villager.posZ)-box.minZ,
                villager.getProfession());
        }
    }

    public static void main(String[] args) throws Exception {
        Bootstrap.register();
        int[] kinds={0,2,3,4,5,6,7,8,9,10,11};
        EnumFacing[] facings={EnumFacing.NORTH,EnumFacing.SOUTH,
            EnumFacing.WEST,EnumFacing.EAST};
        for (int kind:kinds) {
            int variants=kind==3?2:kind==6?6:1;
            for (int variant=0;variant<variants;variant++)
                for (EnumFacing facing:facings) emit(kind,variant,facing);
        }
    }
}
