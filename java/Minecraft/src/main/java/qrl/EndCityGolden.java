package qrl;

import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Random;
import net.minecraft.init.Bootstrap;
import net.minecraft.util.Rotation;
import net.minecraft.util.datafix.DataFixer;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.gen.structure.StructureBoundingBox;
import net.minecraft.world.gen.structure.StructureComponent;
import net.minecraft.world.gen.structure.StructureComponentTemplate;
import net.minecraft.world.gen.structure.StructureEndCityPieces;
import net.minecraft.world.gen.structure.template.TemplateManager;

/** Real 1.11.2 StructureEndCityPieces graph signature for the C oracle. */
public final class EndCityGolden {
    private static final List<String> NAMES=Arrays.asList(
        "base_floor","base_roof","bridge_end","bridge_gentle_stairs",
        "bridge_piece","bridge_steep_stairs","fat_tower_base",
        "fat_tower_middle","fat_tower_top","second_floor","second_floor_2",
        "second_roof","ship","third_floor","third_floor_b","third_floor_c",
        "third_roof","tower_base","tower_floor","tower_piece","tower_top");

    private static long add(long h,int value) {
        h^=value & 0xffffffffL;
        return h*0x100000001b3L;
    }

    private static void one(long seed,int cx,int cz) throws Exception {
        Random base=new Random(seed);
        long mixed=(long)cx*base.nextLong() ^ (long)cz*base.nextLong() ^ seed;
        Random random=new Random(mixed);
        if (System.getenv("ENDCITY_VERBOSE") != null) {
            Random probe=new Random(mixed);
            System.out.printf("R %016x %d %d %d %d%n",mixed,
                probe.nextInt(2),probe.nextInt(2),probe.nextInt(3),probe.nextInt());
        }
        Random rr=new Random((long)cx+(long)cz*10387313L);
        Rotation rotation=Rotation.values()[rr.nextInt(4)];
        TemplateManager manager=new TemplateManager("",new DataFixer(922));
        List<StructureComponent> pieces=new ArrayList<StructureComponent>();
        StructureEndCityPieces.startHouseTower(manager,
            new BlockPos(cx*16+8,70,cz*16+8),rotation,pieces,random);
        Field nameField=StructureEndCityPieces.CityTemplate.class
            .getDeclaredField("pieceName");
        nameField.setAccessible(true);
        Field positionField=StructureComponentTemplate.class
            .getDeclaredField("templatePosition");
        positionField.setAccessible(true);
        long hash=0xcbf29ce484222325L;
        boolean ship=false;
        for (StructureComponent component:pieces) {
            String name=(String)nameField.get(component);
            BlockPos pos=(BlockPos)positionField.get(component);
            StructureBoundingBox box=component.getBoundingBox();
            int index=NAMES.indexOf(name);
            ship|="ship".equals(name);
            hash=add(hash,index); hash=add(hash,pos.getX());
            hash=add(hash,pos.getY()); hash=add(hash,pos.getZ());
            hash=add(hash,component.getComponentType());
            hash=add(hash,box.minX); hash=add(hash,box.minY); hash=add(hash,box.minZ);
            hash=add(hash,box.maxX); hash=add(hash,box.maxY); hash=add(hash,box.maxZ);
            if (System.getenv("ENDCITY_VERBOSE") != null)
                System.out.printf("P %s %d %d %d %d %d %d %d %d %d %d%n",
                    name,pos.getX(),pos.getY(),pos.getZ(),component.getComponentType(),
                    box.minX,box.minY,box.minZ,box.maxX,box.maxY,box.maxZ);
        }
        System.out.printf("%d %d %d %d %d %016x%n",
            seed,cx,cz,pieces.size(),ship?1:0,hash);
    }

    public static void main(String[] args) throws Exception {
        Bootstrap.register();
        one(0L,2,3);
        one(1L,-4,7);
        one(123456789L,60,-41);
        one(-99887766L,-90,-72);
    }
}
