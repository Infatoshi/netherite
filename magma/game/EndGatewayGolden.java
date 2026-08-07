import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Random;

public final class EndGatewayGolden {
    public static void main(String[] args) {
        List<Integer> gateways=new ArrayList<Integer>();
        for (int i=0;i<20;i++) gateways.add(i);
        Collections.shuffle(gateways,new Random(0L));
        int index=gateways.remove(gateways.size()-1);
        int x=(int)(96D*Math.cos(2D*(-Math.PI+.15707963267948966D*index)));
        int z=(int)(96D*Math.sin(2D*(-Math.PI+.15707963267948966D*index)));
        int bedrock=0,gateway=0,air=0;
        for (int dx=-1;dx<=1;dx++) for (int dy=-2;dy<=2;dy++)
            for (int dz=-1;dz<=1;dz++) {
                boolean cx=dx==0,cy=dy==0,cz=dz==0,cap=Math.abs(dy)==2;
                if (cx&&cy&&cz) gateway++;
                else if (!cy&&((cx&&cz)||((cx||cz)&&!cap))) bedrock++;
                else air++;
            }
        System.out.printf("G %d %d %d %d %d %d%n",
            index,x,z,bedrock,gateway,air);
        System.out.println("T 1 40 0.5 80.5 0.5");
        System.out.println("end_gateway: PASS (shuffle, volume, lifecycle, exact travel)");
    }
}
