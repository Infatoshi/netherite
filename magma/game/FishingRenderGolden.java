public final class FishingRenderGolden {
    static final float[] SIN = new float[65536];
    static {
        for (int i=0;i<SIN.length;i++)
            SIN[i]=(float)Math.sin((double)i*Math.PI*2.0D/65536.0D);
    }
    static float sin(float x){return SIN[(int)(x*10430.378F)&65535];}
    static float cos(float x){return SIN[(int)(x*10430.378F+16384.0F)&65535];}
    static double[] pitch(double[] v,float a){
        float c=cos(a),s=sin(a);
        return new double[]{v[0],v[1]*(double)c+v[2]*(double)s,
            v[2]*(double)c-v[1]*(double)s};
    }
    static double[] yaw(double[] v,float a){
        float c=cos(a),s=sin(a);
        return new double[]{v[0]*(double)c+v[2]*(double)s,v[1],
            v[2]*(double)c-v[0]*(double)s};
    }
    static void out(float x,float y,float z){
        System.out.printf("%08x %08x %08x%n",
            Float.floatToRawIntBits(x),Float.floatToRawIntBits(y),
            Float.floatToRawIntBits(z));
    }
    public static void main(String[] args){
        int hand=1;
        float fovSetting=70.0F;
        float f10=fovSetting/100.0F;
        float f7=0.36F;
        float f8=sin((float)Math.sqrt((double)f7)*(float)Math.PI);
        double[] v={(double)hand*-0.36D*(double)f10,
            -0.045D*(double)f10,0.4D};
        v=pitch(v,-(-12.0F)*0.017453292F);
        v=yaw(v,-37.0F*0.017453292F);
        v=yaw(v,f8*0.5F);
        v=pitch(v,-f8*0.7F);
        double d4=24.5D+v[0],d5=70.0D+v[1],d6=-24.75D+v[2];
        double hx=31.25D,hy=65.5D,hz=-18.75D;
        double d10=(double)((float)(d4-hx));
        double d11=(double)((float)(d5-(hy+0.25D)))+1.62F;
        double d12=(double)((float)(d6-hz));
        System.out.println("P 17");
        for(int i=0;i<=16;i++){
            float t=(float)i/16.0F;
            out((float)(hx+d10*(double)t),
                (float)(hy+d11*(double)(t*t+t)*0.5D+0.25D),
                (float)(hz+d12*(double)t));
        }
        System.out.println("V 192");
    }
}
