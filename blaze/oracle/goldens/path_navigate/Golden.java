// Verbatim MC 1.11.2 PathNavigateGround path-FOLLOW (not findPath) ground truth.
// Eval-pure: no game launch. Synthetic hand-built paths of 4-8 points on open flat ground.
//
// Logic copied VERBATIM from the decompiled oracle:
//   PathNavigate.java
//     pathFollow()             L267-303
//     onUpdateNavigation()     L226-265  (pathFollow + getPosition; stuck/repath CUT)
//     noPath()                 L352-355
//   PathNavigateGround.java
//     canNavigate()            L33-36    (onGround only; swim/ride CUT)
//     getEntityPosition()      L38-41
//     getPathablePosY()        L121-123  (non-swim)
//     isDirectPathBetweenPoints L174-250 OPEN-GRID: always clear when d2 >= 1e-8
//   Path.java
//     getVectorFromIndex L90-96, getPosition L101-104, getCurrentPos L106-110
//
// Entity steps toward current waypoint by fixed speed on a flat plane (MoveHelper substitute).
// Output matches cpu/path_navigate.c: per case x PN_TICKS x (index, finished, x,y,z bits) %016x.

public class Golden {
    static final int NUM_CASES = 6;
    static final int TICKS = 40;
    static final int MAX_PTS = 16;

    static final class Point {
        int x, y, z;
        Point(int x, int y, int z) { this.x = x; this.y = y; this.z = z; }
    }

    static final class Path {
        Point[] pts = new Point[MAX_PTS];
        int pathLen;
        int pathIndex;
    }

    static final class Nav {
        double posX, posY, posZ;
        float width, height;
        boolean onGround;
        double speed;
        Path path = new Path();
    }

    // ---- MathHelper ----
    static int floor(double v) {
        int i = (int)v;
        return v < (double)i ? i - 1 : i;
    }
    static int ceil(float v) {
        int i = (int)v;
        return v > (float)i ? i + 1 : i;
    }
    static float abs(float v) {
        return v >= 0.0F ? v : -v;
    }

    // PathNavigateGround.getPathablePosY non-swim
    static int getPathablePosY(Nav n) {
        return (int)(n.posY + 0.5D);
    }

    // PathNavigateGround.getEntityPosition
    static double[] getEntityPosition(Nav n) {
        return new double[] { n.posX, (double)getPathablePosY(n), n.posZ };
    }

    // PathNavigateGround.canNavigate flat
    static boolean canNavigate(Nav n) {
        return n.onGround;
    }

    static boolean noPath(Path p) {
        return p.pathLen <= 0 || p.pathIndex >= p.pathLen;
    }

    // Path.getVectorFromIndex
    static double[] getVectorFromIndex(Nav n, int index) {
        int off = (int)(n.width + 1.0F);
        Point pt = n.path.pts[index];
        return new double[] {
            (double)pt.x + (double)off * 0.5D,
            (double)pt.y,
            (double)pt.z + (double)off * 0.5D
        };
    }

    static double[] getPosition(Nav n) {
        return getVectorFromIndex(n, n.path.pathIndex);
    }

    static double[] getCurrentPos(Path p) {
        Point pt = p.pts[p.pathIndex];
        return new double[] { (double)pt.x, (double)pt.y, (double)pt.z };
    }

    // OPEN-GRID isDirectPathBetweenPoints (PathNavigateGround L174-186 early-out only)
    static boolean isDirectPathBetweenPoints(double x1, double y1, double z1,
                                             double x2, double y2, double z2,
                                             int sizeX, int sizeY, int sizeZ) {
        double d0 = x2 - x1;
        double d1 = z2 - z1;
        double d2 = d0 * d0 + d1 * d1;
        return !(d2 < 1.0E-8D);
    }

    // PathNavigate.pathFollow L267-303 (checkForStuck CUT)
    static void pathFollow(Nav n) {
        double[] ent = getEntityPosition(n);
        double ey = ent[1];

        int i = n.path.pathLen;
        for (int j = n.path.pathIndex; j < n.path.pathLen; ++j) {
            if ((double)n.path.pts[j].y != Math.floor(ey)) {
                i = j;
                break;
            }
        }

        float maxDistanceToWaypoint = n.width > 0.75F ? n.width / 2.0F : 0.75F - n.width / 2.0F;
        double[] cur = getCurrentPos(n.path);

        if (abs((float)(n.posX - (cur[0] + 0.5D))) < maxDistanceToWaypoint
            && abs((float)(n.posZ - (cur[2] + 0.5D))) < maxDistanceToWaypoint
            && Math.abs(n.posY - cur[1]) < 1.0D) {
            n.path.pathIndex = n.path.pathIndex + 1;
        }

        int k = ceil(n.width);
        int l = ceil(n.height);
        int i1 = k;

        if (!noPath(n.path)) {
            for (int j1 = i - 1; j1 >= n.path.pathIndex; --j1) {
                double[] v = getVectorFromIndex(n, j1);
                if (isDirectPathBetweenPoints(ent[0], ent[1], ent[2], v[0], v[1], v[2], k, l, i1)) {
                    n.path.pathIndex = j1;
                    break;
                }
            }
        }
    }

    // Flat-plane step toward waypoint (MoveHelper substitute)
    static void stepToward(Nav n) {
        if (noPath(n.path)) return;
        double[] t = getPosition(n);
        double dx = t[0] - n.posX;
        double dz = t[2] - n.posZ;
        double dist = Math.sqrt(dx * dx + dz * dz);
        if (dist > 1.0E-8D) {
            double step = n.speed < dist ? n.speed : dist;
            n.posX += (dx / dist) * step;
            n.posZ += (dz / dist) * step;
        }
    }

    static void tick(Nav n) {
        if (noPath(n.path)) return;
        if (canNavigate(n)) pathFollow(n);
        if (!noPath(n.path)) stepToward(n);
    }

    static void setPt(Path p, int i, int x, int y, int z) {
        p.pts[i] = new Point(x, y, z);
    }

    static Nav buildCase(int id) {
        Nav n = new Nav();
        n.width = 0.6F;
        n.height = 1.95F;
        n.onGround = true;
        n.speed = 0.25D;
        n.posY = 5.0D;
        n.path.pathIndex = 0;

        switch (id) {
        case 0:
            n.path.pathLen = 6;
            setPt(n.path, 0, 2, 5, 2); setPt(n.path, 1, 4, 5, 2); setPt(n.path, 2, 6, 5, 2);
            setPt(n.path, 3, 8, 5, 2); setPt(n.path, 4, 10, 5, 2); setPt(n.path, 5, 12, 5, 2);
            n.posX = 2.5D; n.posZ = 2.5D;
            break;
        case 1:
            n.path.pathLen = 6;
            setPt(n.path, 0, 2, 5, 2); setPt(n.path, 1, 4, 5, 2); setPt(n.path, 2, 6, 5, 2);
            setPt(n.path, 3, 8, 5, 2); setPt(n.path, 4, 10, 5, 2); setPt(n.path, 5, 12, 5, 2);
            n.posX = 1.2D; n.posZ = 2.5D;
            break;
        case 2:
            n.width = 1.4F;
            n.path.pathLen = 5;
            setPt(n.path, 0, 2, 5, 4); setPt(n.path, 1, 5, 5, 4); setPt(n.path, 2, 8, 5, 4);
            setPt(n.path, 3, 11, 5, 4); setPt(n.path, 4, 14, 5, 4);
            n.posX = 2.5D; n.posZ = 4.5D;
            n.speed = 0.3D;
            break;
        case 3:
            n.path.pathLen = 7;
            setPt(n.path, 0, 2, 5, 2); setPt(n.path, 1, 4, 5, 2); setPt(n.path, 2, 6, 5, 2);
            setPt(n.path, 3, 8, 5, 2); setPt(n.path, 4, 8, 5, 4); setPt(n.path, 5, 8, 5, 6);
            setPt(n.path, 6, 8, 5, 8);
            n.posX = 2.5D; n.posZ = 2.5D;
            n.speed = 0.2D;
            break;
        case 4:
            n.path.pathLen = 5;
            setPt(n.path, 0, 0, 5, 0); setPt(n.path, 1, 2, 5, 0); setPt(n.path, 2, 4, 5, 0);
            setPt(n.path, 3, 6, 5, 0); setPt(n.path, 4, 8, 5, 0);
            n.path.pathIndex = 2;
            n.posX = 4.5D; n.posZ = 0.5D;
            break;
        case 5:
            n.path.pathLen = 4;
            setPt(n.path, 0, 2, 5, 2); setPt(n.path, 1, 4, 5, 2); setPt(n.path, 2, 6, 5, 2);
            setPt(n.path, 3, 8, 5, 2);
            n.posX = 8.5D; n.posZ = 2.5D;
            n.path.pathIndex = 3;
            break;
        default:
            n.path.pathLen = 0;
            break;
        }
        return n;
    }

    static void emit(long v) {
        System.out.printf("%016x%n", v & 0xFFFFFFFFFFFFFFFFL);
    }

    public static void main(String[] args) {
        for (int c = 0; c < NUM_CASES; ++c) {
            Nav n = buildCase(c);
            for (int t = 0; t < TICKS; ++t) {
                tick(n);
                emit((long)n.path.pathIndex);
                emit(noPath(n.path) ? 1L : 0L);
                emit(Double.doubleToRawLongBits(n.posX));
                emit(Double.doubleToRawLongBits(n.posY));
                emit(Double.doubleToRawLongBits(n.posZ));
            }
        }
    }
}
