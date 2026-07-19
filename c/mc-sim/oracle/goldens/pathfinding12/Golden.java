// Verbatim MC 1.11.2 PathFinder + WalkNodeProcessor + PathHeap + PathPoint ground truth over a
// synthetic-grid battery. Eval-pure: no game launch.
//
// Logic transcribed from the decompiled oracle (java/oracle-src/net/minecraft/pathfinding/):
//   PathFinder.java       - the A* loop (200-iter cap, changeDistance decrease-key, maxDistance
//                           gates, closest-point fallback, createEntityPath backtrace).
//   PathHeap.java         - binary min-heap keyed on distanceToTarget (sortBack strict <,
//                           sortForward right-child-on-tie, >= stop).
//   PathPoint.java        - makeHash, distanceManhattan, distanceTo (MathHelper.sqrt), isAssigned.
//   WalkNodeProcessor.java- getPathNodeTypeRaw / getPathNodeType (1-arg + entity-size sweep),
//                           getSafePoint, findPathOptions, getStart, getPathPointToCoords.
//   NodeProcessor.java    - initProcessor (entitySize = floor(w+1)), openPoint (pointMap).
//   World.java            - collidesWithAnyBlock (func_191504_a) + Block.addCollisionBoxToList
//                           + AxisAlignedBB.intersects (strict <,>).
//
// INTERPRETIVE LAYER (self-authored this round, mirrors core/path_node_processor.h exactly): the
// synthetic block model getPathNodeTypeRaw classifies. A future live-game round verifies it against
// real MC. Output matches cpu/pathfinding12.c: per case resultLen, x,y,z of each point (start..end),
// then Float.floatToRawIntBits(end.totalPathDistance); all %08x.
public class Golden {
    // ---- PathNodeType ordinals (PathNodeType.java) ----
    static final int PNT_BLOCKED=0, PNT_OPEN=1, PNT_WALKABLE=2, PNT_TRAPDOOR=3, PNT_FENCE=4,
        PNT_LAVA=5, PNT_WATER=6, PNT_RAIL=7, PNT_DANGER_FIRE=8, PNT_DAMAGE_FIRE=9,
        PNT_DANGER_CACTUS=10, PNT_DAMAGE_CACTUS=11, PNT_DANGER_OTHER=12, PNT_DAMAGE_OTHER=13,
        PNT_DOOR_OPEN=14, PNT_DOOR_WOOD_CLOSED=15, PNT_DOOR_IRON_CLOSED=16, PNT_COUNT=17;

    static float pntPriority(int t) {
        switch (t) {
            case PNT_BLOCKED: return -1.0f;
            case PNT_OPEN: return 0.0f;
            case PNT_WALKABLE: return 0.0f;
            case PNT_TRAPDOOR: return 0.0f;
            case PNT_FENCE: return -1.0f;
            case PNT_LAVA: return -1.0f;
            case PNT_WATER: return 8.0f;
            case PNT_RAIL: return 0.0f;
            case PNT_DANGER_FIRE: return 8.0f;
            case PNT_DAMAGE_FIRE: return 16.0f;
            case PNT_DANGER_CACTUS: return 8.0f;
            case PNT_DAMAGE_CACTUS: return -1.0f;
            case PNT_DANGER_OTHER: return 8.0f;
            case PNT_DAMAGE_OTHER: return -1.0f;
            case PNT_DOOR_OPEN: return 0.0f;
            case PNT_DOOR_WOOD_CLOSED: return -1.0f;
            case PNT_DOOR_IRON_CLOSED: return -1.0f;
            default: return -1.0f;
        }
    }

    // ---- materials + block ids ----
    static final int PM_AIR=0, PM_ROCK=1, PM_WATER=2, PM_LAVA=3, PM_WOOD=4, PM_IRON=5, PM_FIRE=6;
    static final int PB_AIR=0, PB_STONE=1, PB_WATER=2, PB_LAVA=3, PB_FENCE=4, PB_DOOR_WC=5,
        PB_DOOR_WO=6, PB_RAIL=7, PB_CACTUS=8, PB_FIRE=9, PB_TRAPDOOR=10;

    static class BlockDef {
        int material; boolean isPassable;
        boolean isTrapdoor, isFire, isCactus, isDoor, doorOpen, isRail, isFenceLike, isBurning;
        boolean hasCollision;
        double cminX, cminY, cminZ, cmaxX, cmaxY, cmaxZ;
    }

    static BlockDef blockdef(int id) {
        BlockDef d = new BlockDef();
        d.material = PM_AIR; d.isPassable = true;
        d.cmaxX = d.cmaxY = d.cmaxZ = 1.0;
        switch (id) {
            case PB_AIR: d.material = PM_AIR; d.isPassable = true; break;
            case PB_STONE: d.material = PM_ROCK; d.isPassable = false; d.hasCollision = true; break;
            case PB_WATER: d.material = PM_WATER; d.isPassable = true; break;
            case PB_LAVA: d.material = PM_LAVA; d.isPassable = true; break;
            case PB_FENCE: d.material = PM_WOOD; d.isPassable = false; d.isFenceLike = true;
                d.hasCollision = true; d.cmaxY = 1.5; break;
            case PB_DOOR_WC: d.material = PM_WOOD; d.isPassable = false; d.isDoor = true; d.doorOpen = false; break;
            case PB_DOOR_WO: d.material = PM_WOOD; d.isPassable = true; d.isDoor = true; d.doorOpen = true; break;
            case PB_RAIL: d.material = PM_ROCK; d.isPassable = true; d.isRail = true; break;
            case PB_CACTUS: d.material = PM_ROCK; d.isPassable = false; d.isCactus = true; d.hasCollision = true; break;
            case PB_FIRE: d.material = PM_FIRE; d.isPassable = true; d.isFire = true; break;
            case PB_TRAPDOOR: d.material = PM_WOOD; d.isPassable = false; d.isTrapdoor = true; break;
            default: break;
        }
        return d;
    }

    // ---- entity ----
    static class Entity {
        float width, height, stepHeight;
        boolean canSwim, canEnterDoors, canBreakDoors;
        int maxFallHeight;
        float[] pathPriority = new float[PNT_COUNT];
        double posX, posY, posZ;
        boolean onGround, inWater;
        int sizeX, sizeY, sizeZ;
    }

    float getPathPriority(int t) { return ent.pathPriority[t]; }

    // ---- world grid ----
    static final int PNP_DX=32, PNP_DY=24, PNP_DZ=32, PNP_VOL=PNP_DX*PNP_DY*PNP_DZ;
    boolean in(int x,int y,int z){ return x>=0&&x<PNP_DX&&y>=0&&y<PNP_DY&&z>=0&&z<PNP_DZ; }
    int gidx(int x,int y,int z){ return (y*PNP_DZ+z)*PNP_DX+x; }
    int getblock(int x,int y,int z){ return in(x,y,z)?(blocks[gidx(x,y,z)]&0xFF):PB_AIR; }
    void setblock(int x,int y,int z,int id){ if(in(x,y,z)) blocks[gidx(x,y,z)]=(byte)id; }

    // ---- PathPoint ----
    static class PfPoint {
        int x,y,z,hash,index;
        float totalPathDistance, distanceToNext, distanceToTarget;
        int previous; boolean visited;
        float distanceFromOrigin, cost, costMalus;
        int nodeType;
    }

    static int makeHash(int x,int y,int z){
        return (y & 255) | ((x & 32767) << 8) | ((z & 32767) << 24)
             | (x < 0 ? Integer.MIN_VALUE : 0) | (z < 0 ? 32768 : 0);
    }
    static float distanceManhattan(PfPoint a, PfPoint b){
        float f  = (float)Math.abs(b.x - a.x);
        float f1 = (float)Math.abs(b.y - a.y);
        float f2 = (float)Math.abs(b.z - a.z);
        return f + f1 + f2;
    }
    static float distanceTo(PfPoint a, PfPoint b){
        float f  = (float)(b.x - a.x);
        float f1 = (float)(b.y - a.y);
        float f2 = (float)(b.z - a.z);
        return (float)Math.sqrt((double)(f*f + f1*f1 + f2*f2));
    }
    static int floorD(double v){ int i=(int)v; return v < (double)i ? i-1 : i; }

    // ---- state (Pf12) ----
    byte[] blocks = new byte[PNP_VOL];
    Entity ent = new Entity();
    static final int PF12_MAX_POINTS=16384, PF12_HASH=32768;
    PfPoint[] points = new PfPoint[PF12_MAX_POINTS];
    int npoints;
    int[] hashtab = new int[PF12_HASH];
    int[] heap = new int[PF12_MAX_POINTS];
    int heapCount;
    int[] pathOptions = new int[32];
    int resultLen; int[] resultPts = new int[3*512]; float resultDist; boolean overflow;

    void resetPoints(){ npoints=0; for(int i=0;i<PF12_HASH;i++) hashtab[i]=0; }

    int openPoint(int x,int y,int z){
        int h = makeHash(x,y,z);
        int mask = PF12_HASH-1;
        int slot = h & mask;
        for(;;){
            int stored = hashtab[slot];
            if(stored==0){
                if(npoints>=PF12_MAX_POINTS){ overflow=true; return 0; }
                int idx=npoints++;
                PfPoint pt=new PfPoint();
                pt.x=x; pt.y=y; pt.z=z; pt.hash=h; pt.index=-1; pt.previous=-1;
                pt.nodeType=PNT_BLOCKED;
                points[idx]=pt;
                hashtab[slot]=idx+1;
                return idx;
            }
            if(points[stored-1].hash==h) return stored-1;
            slot=(slot+1)&mask;
        }
    }

    void initProcessor(){
        resetPoints();
        ent.sizeX = floorD((double)(ent.width + 1.0f));
        ent.sizeY = floorD((double)(ent.height + 1.0f));
        ent.sizeZ = floorD((double)(ent.width + 1.0f));
    }

    boolean collidesWithAnyBlock(double minX,double minY,double minZ,double maxX,double maxY,double maxZ){
        int x0=floorD(minX)-1, x1=floorD(maxX)+1;
        int y0=floorD(minY)-1, y1=floorD(maxY)+1;
        int z0=floorD(minZ)-1, z1=floorD(maxZ)+1;
        for(int x=x0;x<=x1;x++) for(int y=y0;y<=y1;y++) for(int z=z0;z<=z1;z++){
            BlockDef d=blockdef(getblock(x,y,z));
            if(!d.hasCollision) continue;
            double bminX=d.cminX+x, bminY=d.cminY+y, bminZ=d.cminZ+z;
            double bmaxX=d.cmaxX+x, bmaxY=d.cmaxY+y, bmaxZ=d.cmaxZ+z;
            if(minX<bmaxX && maxX>bminX && minY<bmaxY && maxY>bminY && minZ<bmaxZ && maxZ>bminZ)
                return true;
        }
        return false;
    }

    double boundingMaxY(int id){ return 1.0; }

    int getPathNodeTypeRaw(int x,int y,int z){
        BlockDef d=blockdef(getblock(x,y,z));
        if(d.material==PM_AIR) return PNT_OPEN;
        if(d.isTrapdoor) return PNT_TRAPDOOR;
        if(d.isFire) return PNT_DAMAGE_FIRE;
        if(d.isCactus) return PNT_DAMAGE_CACTUS;
        if(d.isDoor && d.material==PM_WOOD && !d.doorOpen) return PNT_DOOR_WOOD_CLOSED;
        if(d.isDoor && d.material==PM_IRON && !d.doorOpen) return PNT_DOOR_IRON_CLOSED;
        if(d.isDoor && d.doorOpen) return PNT_DOOR_OPEN;
        if(d.isRail) return PNT_RAIL;
        if(!d.isFenceLike){
            if(d.material==PM_WATER) return PNT_WATER;
            if(d.material==PM_LAVA) return PNT_LAVA;
            return d.isPassable ? PNT_OPEN : PNT_BLOCKED;
        }
        return PNT_FENCE;
    }

    int getPathNodeType1(int x,int y,int z){
        int t=getPathNodeTypeRaw(x,y,z);
        if(t==PNT_OPEN && y>=1){
            int t1=getPathNodeTypeRaw(x,y-1,z);
            t=(t1!=PNT_WALKABLE && t1!=PNT_OPEN && t1!=PNT_WATER && t1!=PNT_LAVA)?PNT_WALKABLE:PNT_OPEN;
            if(t1==PNT_DAMAGE_FIRE) t=PNT_DAMAGE_FIRE;
            if(t1==PNT_DAMAGE_CACTUS) t=PNT_DAMAGE_CACTUS;
        }
        if(t==PNT_WALKABLE){
            for(int j=-1;j<=1;j++) for(int i=-1;i<=1;i++){
                if(j!=0||i!=0){
                    int nid=getblock(j+x,y,i+z);
                    BlockDef nd=blockdef(nid);
                    if(nid==PB_CACTUS) t=PNT_DANGER_CACTUS;
                    else if(nid==PB_FIRE) t=PNT_DANGER_FIRE;
                    else if(nd.isBurning) t=PNT_DAMAGE_FIRE;
                }
            }
        }
        return t;
    }

    int getPathNodeTypeSize(int x,int y,int z,int xSize,int ySize,int zSize,
                            boolean canBreakDoors,boolean canEnterDoors){
        int enumset=0; int center=PNT_BLOCKED;
        int ebx=floorD(ent.posX), eby=floorD(ent.posY), ebz=floorD(ent.posZ);
        boolean entOnRail=blockdef(getblock(ebx,eby,ebz)).isRail
                       || blockdef(getblock(ebx,eby-1,ebz)).isRail;
        for(int i=0;i<xSize;i++) for(int j=0;j<ySize;j++) for(int k=0;k<zSize;k++){
            int l=i+x, i1=j+y, j1=k+z;
            int t1=getPathNodeType1(l,i1,j1);
            if(t1==PNT_DOOR_WOOD_CLOSED && canBreakDoors && canEnterDoors) t1=PNT_WALKABLE;
            if(t1==PNT_DOOR_OPEN && !canEnterDoors) t1=PNT_BLOCKED;
            if(t1==PNT_RAIL && !entOnRail) t1=PNT_FENCE;
            if(i==0&&j==0&&k==0) center=t1;
            enumset |= (1<<t1);
        }
        if((enumset & (1<<PNT_FENCE))!=0) return PNT_FENCE;
        int best=PNT_BLOCKED;
        for(int t=0;t<PNT_COUNT;t++){
            if((enumset & (1<<t))==0) continue;
            if(getPathPriority(t) < 0.0f) return t;
            if(getPathPriority(t) >= getPathPriority(best)) best=t;
        }
        if(center==PNT_OPEN && getPathPriority(best)==0.0f) return PNT_OPEN;
        return best;
    }

    int getPathNodeTypeEnt(int x,int y,int z){
        return getPathNodeTypeSize(x,y,z,ent.sizeX,ent.sizeY,ent.sizeZ,ent.canBreakDoors,ent.canEnterDoors);
    }

    int getSafePoint(int x,int y,int z,int step,double p5,int facingX,int facingZ){
        int pathpoint=-1;
        int belowId=getblock(x,y-1,z);
        double d0=(double)y-(1.0-boundingMaxY(belowId));
        if(d0-p5>1.125) return -1;
        int nodetype=getPathNodeTypeEnt(x,y,z);
        float f=getPathPriority(nodetype);
        double d1=(double)ent.width/2.0;
        if(f>=0.0f){
            pathpoint=openPoint(x,y,z);
            points[pathpoint].nodeType=nodetype;
            float cm=points[pathpoint].costMalus;
            points[pathpoint].costMalus = cm>f?cm:f;
        }
        if(nodetype==PNT_WALKABLE) return pathpoint;
        if(pathpoint==-1 && step>0 && nodetype!=PNT_FENCE && nodetype!=PNT_TRAPDOOR){
            pathpoint=getSafePoint(x,y+1,z,step-1,p5,facingX,facingZ);
            if(pathpoint!=-1 &&
               (points[pathpoint].nodeType==PNT_OPEN || points[pathpoint].nodeType==PNT_WALKABLE) &&
               ent.width<1.0f){
                double d2=(double)(x-facingX)+0.5;
                double d3=(double)(z-facingZ)+0.5;
                double aMinX=d2-d1, aMinY=(double)y+0.001, aMinZ=d3-d1;
                double aMaxX=d2+d1, aMaxY=(double)((float)y+ent.height), aMaxZ=d3+d1;
                double bb1maxY=boundingMaxY(getblock(x,y,z));
                double ext=bb1maxY-0.002;
                double a2MaxY=aMaxY, a2MinY=aMinY;
                if(ext<0.0) a2MinY+=ext; else a2MaxY+=ext;
                if(collidesWithAnyBlock(aMinX,a2MinY,aMinZ,aMaxX,a2MaxY,aMaxZ)) pathpoint=-1;
            }
        }
        if(nodetype==PNT_OPEN){
            double aMinX=(double)x-d1+0.5, aMinY=(double)y+0.001, aMinZ=(double)z-d1+0.5;
            double aMaxX=(double)x+d1+0.5, aMaxY=(double)((float)y+ent.height), aMaxZ=(double)z+d1+0.5;
            if(collidesWithAnyBlock(aMinX,aMinY,aMinZ,aMaxX,aMaxY,aMaxZ)) return -1;
            if(ent.width>=1.0f){
                int t1=getPathNodeTypeEnt(x,y-1,z);
                if(t1==PNT_BLOCKED){
                    pathpoint=openPoint(x,y,z);
                    points[pathpoint].nodeType=PNT_WALKABLE;
                    float cm=points[pathpoint].costMalus;
                    points[pathpoint].costMalus = cm>f?cm:f;
                    return pathpoint;
                }
            }
            int i=0;
            while(y>0 && nodetype==PNT_OPEN){
                --y;
                if(i++ >= ent.maxFallHeight) return -1;
                nodetype=getPathNodeTypeEnt(x,y,z);
                f=getPathPriority(nodetype);
                if(nodetype!=PNT_OPEN && f>=0.0f){
                    pathpoint=openPoint(x,y,z);
                    points[pathpoint].nodeType=nodetype;
                    float cm=points[pathpoint].costMalus;
                    points[pathpoint].costMalus = cm>f?cm:f;
                    break;
                }
                if(f<0.0f) return -1;
            }
        }
        return pathpoint;
    }

    int findPathOptions(int[] opts, int curIdx, int targetIdx, float maxDistance){
        int i=0, j=0;
        int cx=points[curIdx].x, cy=points[curIdx].y, cz=points[curIdx].z;
        int headType=getPathNodeTypeEnt(cx,cy+1,cz);
        if(getPathPriority(headType)>=0.0f){
            float sh=ent.stepHeight>1.0f?ent.stepHeight:1.0f;
            j=floorD((double)sh);
        }
        int belowId=getblock(cx,cy-1,cz);
        double d0=(double)cy-(1.0-boundingMaxY(belowId));
        int pS=getSafePoint(cx,cy,cz+1,j,d0,0,1);
        int pW=getSafePoint(cx-1,cy,cz,j,d0,-1,0);
        int pE=getSafePoint(cx+1,cy,cz,j,d0,1,0);
        int pN=getSafePoint(cx,cy,cz-1,j,d0,0,-1);
        PfPoint tp=points[targetIdx];
        if(pS!=-1 && !points[pS].visited && distanceTo(points[pS],tp)<maxDistance) opts[i++]=pS;
        if(pW!=-1 && !points[pW].visited && distanceTo(points[pW],tp)<maxDistance) opts[i++]=pW;
        if(pE!=-1 && !points[pE].visited && distanceTo(points[pE],tp)<maxDistance) opts[i++]=pE;
        if(pN!=-1 && !points[pN].visited && distanceTo(points[pN],tp)<maxDistance) opts[i++]=pN;
        boolean flag =(pN==-1 || points[pN].nodeType==PNT_OPEN || points[pN].costMalus!=0.0f);
        boolean flag1=(pS==-1 || points[pS].nodeType==PNT_OPEN || points[pS].costMalus!=0.0f);
        boolean flag2=(pE==-1 || points[pE].nodeType==PNT_OPEN || points[pE].costMalus!=0.0f);
        boolean flag3=(pW==-1 || points[pW].nodeType==PNT_OPEN || points[pW].costMalus!=0.0f);
        if(flag && flag3){ int q=getSafePoint(cx-1,cy,cz-1,j,d0,0,-1);
            if(q!=-1 && !points[q].visited && distanceTo(points[q],tp)<maxDistance) opts[i++]=q; }
        if(flag && flag2){ int q=getSafePoint(cx+1,cy,cz-1,j,d0,0,-1);
            if(q!=-1 && !points[q].visited && distanceTo(points[q],tp)<maxDistance) opts[i++]=q; }
        if(flag1 && flag3){ int q=getSafePoint(cx-1,cy,cz+1,j,d0,0,1);
            if(q!=-1 && !points[q].visited && distanceTo(points[q],tp)<maxDistance) opts[i++]=q; }
        if(flag1 && flag2){ int q=getSafePoint(cx+1,cy,cz+1,j,d0,0,1);
            if(q!=-1 && !points[q].visited && distanceTo(points[q],tp)<maxDistance) opts[i++]=q; }
        return i;
    }

    int getPathPointToCoords(double x,double y,double z){ return openPoint(floorD(x),floorD(y),floorD(z)); }

    int getStart(){
        int i;
        double minY=ent.posY;
        if(ent.canSwim && ent.inWater){
            i=(int)minY;
            for(int bl=getblock(floorD(ent.posX),i,floorD(ent.posZ)); bl==PB_WATER;
                bl=getblock(floorD(ent.posX),i,floorD(ent.posZ))) ++i;
        } else if(ent.onGround){
            i=floorD(minY+0.5);
        } else {
            int bx=floorD(ent.posX), by=floorD(minY), bz=floorD(ent.posZ);
            while((blockdef(getblock(bx,by,bz)).material==PM_AIR ||
                   blockdef(getblock(bx,by,bz)).isPassable) && by>0) --by;
            i=by+1;
        }
        int bx=floorD(ent.posX), bz=floorD(ent.posZ);
        int t1=getPathNodeTypeEnt(bx,i,bz);
        if(getPathPriority(t1)<0.0f){
            double minX=ent.posX-(double)ent.width/2.0, maxX=ent.posX+(double)ent.width/2.0;
            double minZ=ent.posZ-(double)ent.width/2.0, maxZ=ent.posZ+(double)ent.width/2.0;
            double[][] corners={{minX,minZ},{minX,maxZ},{maxX,minZ},{maxX,maxZ}};
            for(int cc=0;cc<4;cc++){
                int cxx=floorD(corners[cc][0]), czz=floorD(corners[cc][1]);
                int tt=getPathNodeTypeEnt(cxx,i,czz);
                if(getPathPriority(tt)>=0.0f) return openPoint(cxx,i,czz);
            }
        }
        return openPoint(bx,i,bz);
    }

    // ---- PathHeap ----
    void heapClear(){ heapCount=0; }
    boolean heapEmpty(){ return heapCount==0; }
    void sortBack(int index){
        int pidx=heap[index];
        float f=points[pidx].distanceToTarget;
        while(index>0){
            int ii=(index-1)>>1;
            int pidx1=heap[ii];
            if(f>=points[pidx1].distanceToTarget) break;
            heap[index]=pidx1; points[pidx1].index=index; index=ii;
        }
        heap[index]=pidx; points[pidx].index=index;
    }
    void sortForward(int index){
        int pidx=heap[index];
        float f=points[pidx].distanceToTarget;
        for(;;){
            int ii=1+(index<<1);
            int jj=ii+1;
            if(ii>=heapCount) break;
            int pidx1=heap[ii];
            float f1=points[pidx1].distanceToTarget;
            int pidx2; float f2;
            if(jj>=heapCount){ pidx2=-1; f2=Float.POSITIVE_INFINITY; }
            else { pidx2=heap[jj]; f2=points[pidx2].distanceToTarget; }
            if(f1<f2){
                if(f1>=f) break;
                heap[index]=pidx1; points[pidx1].index=index; index=ii;
            } else {
                if(f2>=f) break;
                heap[index]=pidx2; points[pidx2].index=index; index=jj;
            }
        }
        heap[index]=pidx; points[pidx].index=index;
    }
    void heapAddPoint(int pidx){
        int c=heapCount;
        heap[c]=pidx; points[pidx].index=c;
        sortBack(c); heapCount=c+1;
    }
    int heapDequeue(){
        int ret=heap[0];
        heapCount-=1;
        heap[0]=heap[heapCount];
        if(heapCount>0) sortForward(0);
        points[ret].index=-1;
        return ret;
    }
    void changeDistance(int pidx, float distance){
        float f=points[pidx].distanceToTarget;
        points[pidx].distanceToTarget=distance;
        if(distance<f) sortBack(points[pidx].index);
        else sortForward(points[pidx].index);
    }

    // ---- PathFinder.findPath ----
    int findPathPts(int startIdx, int targetIdx, float maxDistance){
        resultLen=0; resultDist=0.0f;
        PfPoint s=points[startIdx], tgt=points[targetIdx];
        s.totalPathDistance=0.0f;
        s.distanceToNext=distanceManhattan(s,tgt);
        s.distanceToTarget=s.distanceToNext;
        heapClear();
        heapAddPoint(startIdx);
        int pathpoint=startIdx; int i=0;
        while(!heapEmpty()){
            ++i;
            if(i>=200) break;
            int cur=heapDequeue();
            if(points[cur].hash==tgt.hash && points[cur].x==tgt.x
               && points[cur].y==tgt.y && points[cur].z==tgt.z){
                pathpoint=targetIdx; break;
            }
            if(distanceManhattan(points[cur],tgt) < distanceManhattan(points[pathpoint],tgt))
                pathpoint=cur;
            points[cur].visited=true;
            int j=findPathOptions(pathOptions,cur,targetIdx,maxDistance);
            for(int k=0;k<j;k++){
                int pp2=pathOptions[k];
                float f=distanceManhattan(points[cur],points[pp2]);
                points[pp2].distanceFromOrigin=points[cur].distanceFromOrigin+f;
                points[pp2].cost=f+points[pp2].costMalus;
                float f1=points[cur].totalPathDistance+points[pp2].cost;
                if(points[pp2].distanceFromOrigin<maxDistance &&
                   (!(points[pp2].index>=0) || f1<points[pp2].totalPathDistance)){
                    points[pp2].previous=cur;
                    points[pp2].totalPathDistance=f1;
                    points[pp2].distanceToNext=distanceManhattan(points[pp2],tgt)+points[pp2].costMalus;
                    if(points[pp2].index>=0){
                        changeDistance(pp2, points[pp2].totalPathDistance+points[pp2].distanceToNext);
                    } else {
                        points[pp2].distanceToTarget=points[pp2].totalPathDistance+points[pp2].distanceToNext;
                        heapAddPoint(pp2);
                    }
                }
            }
        }
        if(pathpoint==startIdx){ resultLen=0; return 0; }
        int n=1;
        for(int q=pathpoint; points[q].previous!=-1; q=points[q].previous) ++n;
        int cap=resultPts.length/3;
        if(n>cap){ overflow=true; n=cap; }
        int idx=n-1; int q=pathpoint;
        resultPts[idx*3+0]=points[q].x; resultPts[idx*3+1]=points[q].y; resultPts[idx*3+2]=points[q].z;
        while(points[q].previous!=-1 && idx>0){
            q=points[q].previous; --idx;
            resultPts[idx*3+0]=points[q].x; resultPts[idx*3+1]=points[q].y; resultPts[idx*3+2]=points[q].z;
        }
        resultLen=n;
        resultDist=points[pathpoint].totalPathDistance;
        return n;
    }

    int findPath(double tx,double ty,double tz,float maxDistance){
        initProcessor();
        int startIdx=getStart();
        int targetIdx=getPathPointToCoords(tx,ty,tz);
        return findPathPts(startIdx,targetIdx,maxDistance);
    }

    // ---- battery ----
    static final int PF12_NUM_CASES=17, PF12_FLOOR_Y=4, PF12_WALK_Y=5;
    void fillAir(){ for(int i=0;i<PNP_VOL;i++) blocks[i]=PB_AIR; }
    void floorFill(int x0,int z0,int x1,int z1,int y,int id){
        for(int x=x0;x<=x1;x++) for(int z=z0;z<=z1;z++) setblock(x,y,z,id);
    }
    void box(int x0,int y0,int z0,int x1,int y1,int z1,int id){
        for(int x=x0;x<=x1;x++) for(int y=y0;y<=y1;y++) for(int z=z0;z<=z1;z++) setblock(x,y,z,id);
    }
    void entW1(){
        ent=new Entity();
        ent.width=0.6f; ent.height=1.95f; ent.stepHeight=0.6f;
        ent.canSwim=false; ent.canEnterDoors=true; ent.canBreakDoors=false;
        ent.maxFallHeight=3; ent.onGround=true; ent.inWater=false;
        for(int t=0;t<PNT_COUNT;t++) ent.pathPriority[t]=pntPriority(t);
    }
    void entW2(){ entW1(); ent.width=1.4f; }
    void setStart(int bx,int bz){ ent.posX=(double)bx+0.5; ent.posY=(double)PF12_WALK_Y; ent.posZ=(double)bz+0.5; }

    double tx,ty,tz; float maxDistance;

    void buildCase(int id, long seed){
        fillAir();
        floorFill(0,0,20,20,PF12_FLOOR_Y,PB_STONE);
        entW1();
        setStart(2,2);
        tx=14.5; ty=(double)PF12_WALK_Y+0.5; tz=8.5; maxDistance=64.0f;
        switch(id){
            case 0: break;
            case 1: box(8,5,1,8,7,12,PB_STONE); break;
            case 2: box(6,5,0,6,7,10,PB_STONE); box(10,5,4,10,7,14,PB_STONE); break;
            case 3: floorFill(9,0,20,20,5,PB_STONE); tz=8.5; tx=14.5; ty=6.5; break;
            case 4: box(9,2,0,20,4,20,PB_AIR); floorFill(9,0,20,20,1,PB_STONE); tx=14.5; ty=2.5; tz=8.5; break;
            case 5: box(9,1,0,20,4,20,PB_AIR); floorFill(9,0,20,20,0,PB_STONE); tx=14.5; ty=1.5; tz=8.5; break;
            case 6: floorFill(8,1,8,12,5,PB_FENCE); break;
            case 7: box(8,5,1,8,7,12,PB_STONE); setblock(8,5,6,PB_DOOR_WC); setblock(8,6,6,PB_AIR); break;
            case 8: box(8,5,1,8,7,12,PB_STONE); setblock(8,5,6,PB_DOOR_WO); setblock(8,6,6,PB_AIR); break;
            /* Cases 9-13 use full-width hazards so the penalty/avoidance cost is forced into the
             * final path (a partial hazard admits an optimal detour that hides costMalus). Match
             * core/pathfinding12.h exactly. */
            case 9: /* full-width water strip at x=8: forced crossing, costMalus 8 in total dist */
                box(8,5,0,8,5,20,PB_WATER); break;
            case 10: /* same water strip, canSwim on */
                box(8,5,0,8,5,20,PB_WATER); ent.canSwim=true; break;
            case 11: /* same water strip, avoid water (priority < 0) -> impassable, partial path */
                box(8,5,0,8,5,20,PB_WATER); ent.pathPriority[PNT_WATER]=-1.0f; break;
            case 12: /* full-width stone wall, single gap at z=6, cactus post at z=5 -> DANGER_CACTUS */
                box(8,5,0,8,7,20,PB_STONE);
                setblock(8,5,6,PB_AIR); setblock(8,6,6,PB_AIR); setblock(8,7,6,PB_AIR);
                setblock(8,5,5,PB_CACTUS);
                break;
            case 13: /* same, fire post at z=5 -> DANGER_FIRE */
                box(8,5,0,8,7,20,PB_STONE);
                setblock(8,5,6,PB_AIR); setblock(8,6,6,PB_AIR); setblock(8,7,6,PB_AIR);
                setblock(8,5,5,PB_FIRE);
                break;
            case 14: box(12,5,6,16,8,10,PB_STONE); tx=14.5; ty=5.5; tz=8.5; break;
            case 15: entW2(); setStart(2,2); break;
            case 16: entW2(); setStart(2,2); box(8,5,1,8,7,12,PB_STONE); break;
            default: break;
        }
    }

    StringBuilder sb=new StringBuilder();
    void emit(int v){ sb.append(String.format("%08x", v & 0xFFFFFFFFL)).append('\n'); }

    void runCase(int id, long seed){
        buildCase(id, seed);
        overflow=false;
        int n=findPath(tx,ty,tz,maxDistance);
        emit(n);
        for(int i=0;i<n;i++){ emit(resultPts[i*3+0]); emit(resultPts[i*3+1]); emit(resultPts[i*3+2]); }
        if(n>0) emit(Float.floatToRawIntBits(resultDist));
    }

    void runAll(long seed){ for(int i=0;i<PF12_NUM_CASES;i++) runCase(i, seed); }

    public static void main(String[] args){
        long seed = args.length>0 ? Long.parseLong(args[0]) : 12345L;
        Golden g=new Golden();
        g.runAll(seed);
        System.out.print(g.sb);
    }
}
