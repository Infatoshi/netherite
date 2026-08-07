import java.util.Random;

public final class WeatherRenderGolden {
    static void emit(float x,float y,float z,float u,float v,int sky,float alpha){
        int a=alpha<=0?0:alpha>=1?255:(int)(alpha*255.0F+0.5F);
        System.out.printf("%08x %08x %08x %08x %08x %08x %02x%n",
            Float.floatToRawIntBits(x),Float.floatToRawIntBits(y),Float.floatToRawIntBits(z),
            Float.floatToRawIntBits(u),Float.floatToRawIntBits(v),Float.floatToRawIntBits((float)sky),a);
    }
    static void quad(float[][] q){
        int[] order={0,1,2,0,2,3,2,1,0,3,2,0};
        for(int i:order)emit(q[i][0],q[i][1],q[i][2],q[i][3],q[i][4],(int)q[i][5],q[i][6]);
    }
    static int seed(int x,int z){return x*x*3121+x*45238971 ^ z*z*418711+z*13761;}
    static void rain(){
        int x=1,z=2,lo=65,hi=82,tick=37;float pt=0.5F,strength=0.8F;
        double rx=0.0,rz=0.5;Random r=new Random(seed(x,z));
        double scroll=-((double)(tick+x*x*3121+x*45238971+z*z*418711+z*13761&31)+(double)pt)/32.0D*(3.0D+r.nextDouble());
        float dist=0.1F,alpha=((1.0F-dist*dist)*0.5F+0.5F)*strength;
        float[][]q={{(float)(x-rx+0.5),hi,(float)(z-rz+0.5),0,(float)(lo*.25+scroll),12,alpha},
            {(float)(x+rx+0.5),hi,(float)(z+rz+0.5),1,(float)(lo*.25+scroll),12,alpha},
            {(float)(x+rx+0.5),lo,(float)(z+rz+0.5),1,(float)(hi*.25+scroll),12,alpha},
            {(float)(x-rx+0.5),lo,(float)(z-rz+0.5),0,(float)(hi*.25+scroll),12,alpha}};quad(q);
    }
    static void snow(){
        int x=-1,z=2,lo=65,hi=82,tick=37;float pt=0.5F,strength=0.8F,frame=tick+pt;
        double rx=0.0,rz=-0.5;Random r=new Random(seed(x,z));
        double sv=-((float)(tick&511)+pt)/512.0F;
        double su=r.nextDouble()+(double)frame*.01D*(double)((float)r.nextGaussian());
        double jitter=r.nextDouble()+(double)(frame*(float)r.nextGaussian())*.001D;
        float dist=0.1F,alpha=((1.0F-dist*dist)*0.3F+0.5F)*strength;
        int sky=(12*3+15)/4;
        float[][]q={{(float)(x-rx+0.5),hi,(float)(z-rz+0.5),(float)su,(float)(lo*.25+sv+jitter),sky,alpha},
            {(float)(x+rx+0.5),hi,(float)(z+rz+0.5),(float)(1+su),(float)(lo*.25+sv+jitter),sky,alpha},
            {(float)(x+rx+0.5),lo,(float)(z+rz+0.5),(float)(1+su),(float)(hi*.25+sv+jitter),sky,alpha},
            {(float)(x-rx+0.5),lo,(float)(z-rz+0.5),(float)su,(float)(hi*.25+sv+jitter),sky,alpha}};quad(q);
    }
    static void lightningVertex(double x,double y,double z){
        emit((float)x,(float)y,(float)z,0.0F,0.0F,15,0.3F);
    }
    static void lightning(){
        long vertex=0x123456789abcdefL;
        double x=4.0D,y=5.0D,z=6.0D;
        double[] pathX=new double[8],pathZ=new double[8];
        double carryX=0.0D,carryZ=0.0D;
        Random pathRandom=new Random(vertex);
        for(int i=7;i>=0;--i){
            pathX[i]=carryX;pathZ[i]=carryZ;
            carryX+=pathRandom.nextInt(11)-5;
            carryZ+=pathRandom.nextInt(11)-5;
        }
        System.out.println("L 1344");
        for(int layer=0;layer<4;++layer){
            Random branchRandom=new Random(vertex);
            for(int branch=0;branch<3;++branch){
                int top=branch>0?7-branch:7;
                int bottom=branch>0?top-2:0;
                double nextX=pathX[top]-carryX,nextZ=pathZ[top]-carryZ;
                for(int segment=top;segment>=bottom;--segment){
                    double priorX=nextX,priorZ=nextZ;
                    if(branch==0){
                        nextX+=branchRandom.nextInt(11)-5;
                        nextZ+=branchRandom.nextInt(11)-5;
                    }else{
                        nextX+=branchRandom.nextInt(31)-15;
                        nextZ+=branchRandom.nextInt(31)-15;
                    }
                    double upper=.1D+layer*.2D,lower=.1D+layer*.2D;
                    if(branch==0){
                        upper*=segment*.1D+1.0D;
                        lower*=(segment-1)*.1D+1.0D;
                    }
                    double[][] strip=new double[10][3];
                    for(int corner=0;corner<5;++corner){
                        double ux=x+.5D-upper,uz=z+.5D-upper;
                        double lx=x+.5D-lower,lz=z+.5D-lower;
                        if(corner==1||corner==2){ux+=upper*2.0D;lx+=lower*2.0D;}
                        if(corner==2||corner==3){uz+=upper*2.0D;lz+=lower*2.0D;}
                        strip[corner*2]=new double[]{lx+nextX,y+segment*16,lz+nextZ};
                        strip[corner*2+1]=new double[]{ux+priorX,y+(segment+1)*16,uz+priorZ};
                    }
                    for(int tri=0;tri<8;++tri){
                        int a=(tri&1)==0?tri:tri+1;
                        int b=(tri&1)==0?tri+1:tri;
                        lightningVertex(strip[a][0],strip[a][1],strip[a][2]);
                        lightningVertex(strip[b][0],strip[b][1],strip[b][2]);
                        lightningVertex(strip[tri+2][0],strip[tri+2][1],strip[tri+2][2]);
                    }
                }
            }
        }
    }
    public static void main(String[]args){System.out.println("12 12");rain();snow();lightning();}
}
