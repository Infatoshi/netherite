// Verbatim MC 1.11.2 collision math: AxisAlignedBB (constructor + offset + addCoord + intersects
// + calculate{X,Y,Z}Offset) from net/minecraft/util/math/AxisAlignedBB.java, and the
// Entity.move(MoverType.SELF,...) collision slice + resetPositionToBB from
// net/minecraft/entity/Entity.java. Real decompiled MC code only - the vanilla ground truth.
// NOT derived from any hand-port.
//
// The world getCollisionBoxes(...) query is reduced to iterating a FIXED baked list of solid block
// AABBs and collecting those that intersect the motion-expanded entity box, exactly as
// Block.addCollisionBoxToList does (entityBox.intersectsWith(blockBox)). Branches trimmed (all
// inactive for MoverType.SELF, noClip=false, isInWeb=false, non-EntityPlayer, stepHeight=0):
// PISTON, web, sneak step-back, stepHeight step-up, and the post-resolution side effects
// (updateFallState, fence/wall feet lookup, sounds, fire, doBlockCollisions). motionY zeroing on
// vertical collision is Block.onLanded for a standard block (motionY = 0.0D); slime/bed bounce CUT.
//
// Baked scenes mirror core/physics_collision_math.h mc_pcm_scenario exactly. Prints 12 doubles
// (%016x of Double.doubleToRawLongBits) then 3 flags (%08x) per scenario, matching cpu/*.c.
import java.util.List;
import java.util.ArrayList;

public class Golden {

    // ---- verbatim net/minecraft/util/math/AxisAlignedBB.java (methods used) ----
    static final class AxisAlignedBB {
        public final double minX, minY, minZ, maxX, maxY, maxZ;

        public AxisAlignedBB(double x1, double y1, double z1, double x2, double y2, double z2) {
            this.minX = Math.min(x1, x2);
            this.minY = Math.min(y1, y2);
            this.minZ = Math.min(z1, z2);
            this.maxX = Math.max(x1, x2);
            this.maxY = Math.max(y1, y2);
            this.maxZ = Math.max(z1, z2);
        }

        public AxisAlignedBB addCoord(double x, double y, double z) {
            double d0 = this.minX;
            double d1 = this.minY;
            double d2 = this.minZ;
            double d3 = this.maxX;
            double d4 = this.maxY;
            double d5 = this.maxZ;
            if (x < 0.0D) { d0 += x; } else if (x > 0.0D) { d3 += x; }
            if (y < 0.0D) { d1 += y; } else if (y > 0.0D) { d4 += y; }
            if (z < 0.0D) { d2 += z; } else if (z > 0.0D) { d5 += z; }
            return new AxisAlignedBB(d0, d1, d2, d3, d4, d5);
        }

        public AxisAlignedBB offset(double x, double y, double z) {
            return new AxisAlignedBB(this.minX + x, this.minY + y, this.minZ + z, this.maxX + x, this.maxY + y, this.maxZ + z);
        }

        public double calculateXOffset(AxisAlignedBB other, double offsetX) {
            if (other.maxY > this.minY && other.minY < this.maxY && other.maxZ > this.minZ && other.minZ < this.maxZ) {
                if (offsetX > 0.0D && other.maxX <= this.minX) {
                    double d1 = this.minX - other.maxX;
                    if (d1 < offsetX) { offsetX = d1; }
                } else if (offsetX < 0.0D && other.minX >= this.maxX) {
                    double d0 = this.maxX - other.minX;
                    if (d0 > offsetX) { offsetX = d0; }
                }
                return offsetX;
            } else {
                return offsetX;
            }
        }

        public double calculateYOffset(AxisAlignedBB other, double offsetY) {
            if (other.maxX > this.minX && other.minX < this.maxX && other.maxZ > this.minZ && other.minZ < this.maxZ) {
                if (offsetY > 0.0D && other.maxY <= this.minY) {
                    double d1 = this.minY - other.maxY;
                    if (d1 < offsetY) { offsetY = d1; }
                } else if (offsetY < 0.0D && other.minY >= this.maxY) {
                    double d0 = this.maxY - other.minY;
                    if (d0 > offsetY) { offsetY = d0; }
                }
                return offsetY;
            } else {
                return offsetY;
            }
        }

        public double calculateZOffset(AxisAlignedBB other, double offsetZ) {
            if (other.maxX > this.minX && other.minX < this.maxX && other.maxY > this.minY && other.minY < this.maxY) {
                if (offsetZ > 0.0D && other.maxZ <= this.minZ) {
                    double d1 = this.minZ - other.maxZ;
                    if (d1 < offsetZ) { offsetZ = d1; }
                } else if (offsetZ < 0.0D && other.minZ >= this.maxZ) {
                    double d0 = this.maxZ - other.minZ;
                    if (d0 > offsetZ) { offsetZ = d0; }
                }
                return offsetZ;
            } else {
                return offsetZ;
            }
        }

        public boolean intersectsWith(AxisAlignedBB other) {
            return this.intersects(other.minX, other.minY, other.minZ, other.maxX, other.maxY, other.maxZ);
        }

        public boolean intersects(double x1, double y1, double z1, double x2, double y2, double z2) {
            return this.minX < x2 && this.maxX > x1 && this.minY < y2 && this.maxY > y1 && this.minZ < z2 && this.maxZ > z1;
        }
    }

    // ---- entity state (the Entity fields Entity.move reads/writes in this slice) ----
    static double posX, posY, posZ;
    static double motionX, motionY, motionZ;
    static boolean onGround, isCollidedHorizontally, isCollidedVertically, isCollided;
    static AxisAlignedBB boundingBox;

    static AxisAlignedBB getEntityBoundingBox() { return boundingBox; }
    static void setEntityBoundingBox(AxisAlignedBB bb) { boundingBox = bb; }

    // verbatim Entity.resetPositionToBB
    static void resetPositionToBB() {
        AxisAlignedBB axisalignedbb = getEntityBoundingBox();
        posX = (axisalignedbb.minX + axisalignedbb.maxX) / 2.0D;
        posY = axisalignedbb.minY;
        posZ = (axisalignedbb.minZ + axisalignedbb.maxZ) / 2.0D;
    }

    // world.getCollisionBoxes(this, aabb) reduced to the baked block list (Block.addCollisionBoxToList:
    // collect boxes where entityBox.intersectsWith(blockBox)).
    static List<AxisAlignedBB> getCollisionBoxes(AxisAlignedBB aabb, List<AxisAlignedBB> world) {
        List<AxisAlignedBB> list = new ArrayList<AxisAlignedBB>();
        for (int i = 0; i < world.size(); ++i) {
            AxisAlignedBB axisalignedbb = world.get(i);
            if (aabb.intersectsWith(axisalignedbb)) {
                list.add(axisalignedbb);
            }
        }
        return list;
    }

    // verbatim Entity.move(MoverType.SELF, x, y, z) collision slice (trims documented above).
    static void move(double x, double y, double z, List<AxisAlignedBB> world) {
        double d2 = x;
        double d3 = y;
        double d4 = z;

        List<AxisAlignedBB> list1 = getCollisionBoxes(getEntityBoundingBox().addCoord(x, y, z), world);

        if (y != 0.0D) {
            int k = 0;

            for (int l = list1.size(); k < l; ++k) {
                y = ((AxisAlignedBB)list1.get(k)).calculateYOffset(getEntityBoundingBox(), y);
            }

            setEntityBoundingBox(getEntityBoundingBox().offset(0.0D, y, 0.0D));
        }

        if (x != 0.0D) {
            int j5 = 0;

            for (int l5 = list1.size(); j5 < l5; ++j5) {
                x = ((AxisAlignedBB)list1.get(j5)).calculateXOffset(getEntityBoundingBox(), x);
            }

            if (x != 0.0D) {
                setEntityBoundingBox(getEntityBoundingBox().offset(x, 0.0D, 0.0D));
            }
        }

        if (z != 0.0D) {
            int k5 = 0;

            for (int i6 = list1.size(); k5 < i6; ++k5) {
                z = ((AxisAlignedBB)list1.get(k5)).calculateZOffset(getEntityBoundingBox(), z);
            }

            if (z != 0.0D) {
                setEntityBoundingBox(getEntityBoundingBox().offset(0.0D, 0.0D, z));
            }
        }

        // boolean flag = onGround || d3 != y && d3 < 0.0D; gates only the dead stepHeight branch.

        resetPositionToBB();
        isCollidedHorizontally = d2 != x || d4 != z;
        isCollidedVertically = d3 != y;
        onGround = isCollidedVertically && d3 < 0.0D;
        isCollided = isCollidedHorizontally || isCollidedVertically;

        if (d2 != x) {
            motionX = 0.0D;
        }

        if (d4 != z) {
            motionZ = 0.0D;
        }

        if (d3 != y) {
            motionY = 0.0D;   // Block.onLanded (standard block): entityIn.motionY = 0.0D
        }
    }

    // ---- baked scenes, mirroring core/physics_collision_math.h mc_pcm_scenario ----
    static AxisAlignedBB playerBox(double px, double py, double pz) {
        return new AxisAlignedBB(px - 0.3D, py, pz - 0.3D, px + 0.3D, py + 1.8D, pz + 0.3D);
    }

    static double dx, dy, dz;

    static List<AxisAlignedBB> scenario(int idx) {
        List<AxisAlignedBB> world = new ArrayList<AxisAlignedBB>();
        motionX = 0.0D; motionY = 0.0D; motionZ = 0.0D;
        onGround = false; isCollidedHorizontally = false; isCollidedVertically = false; isCollided = false;

        if (idx == 0) {
            posX = 0.0D; posY = 5.0D; posZ = 0.0D;
            boundingBox = playerBox(posX, posY, posZ);
            dx = 0.0D; dy = -10.0D; dz = 0.0D;
            motionY = -10.0D;
            for (int bx = -1; bx <= 1; ++bx)
                for (int bz = -1; bz <= 1; ++bz)
                    world.add(new AxisAlignedBB((double)bx, 0.0D, (double)bz, (double)(bx + 1), 1.0D, (double)(bz + 1)));
        } else if (idx == 1) {
            posX = 0.0D; posY = 1.0D; posZ = 0.0D;
            boundingBox = playerBox(posX, posY, posZ);
            dx = 5.0D; dy = 0.0D; dz = 0.0D;
            motionX = 5.0D;
            onGround = true;
            for (int by = 1; by <= 2; ++by)
                for (int bz = -1; bz <= 0; ++bz)
                    world.add(new AxisAlignedBB(2.0D, (double)by, (double)bz, 3.0D, (double)(by + 1), (double)(bz + 1)));
        } else if (idx == 2) {
            posX = 0.0D; posY = 1.0D; posZ = 0.0D;
            boundingBox = playerBox(posX, posY, posZ);
            dx = 5.0D; dy = 0.0D; dz = 5.0D;
            motionX = 5.0D; motionZ = 5.0D;
            onGround = true;
            for (int by = 1; by <= 2; ++by)
                for (int bz = -1; bz <= 1; ++bz)
                    world.add(new AxisAlignedBB(2.0D, (double)by, (double)bz, 3.0D, (double)(by + 1), (double)(bz + 1)));
            for (int by = 1; by <= 2; ++by)
                for (int bx = -1; bx <= 1; ++bx)
                    world.add(new AxisAlignedBB((double)bx, (double)by, 2.0D, (double)(bx + 1), (double)(by + 1), 3.0D));
        } else {
            posX = 0.0D; posY = 5.0D; posZ = 0.0D;
            boundingBox = playerBox(posX, posY, posZ);
            dx = 1.0D; dy = -1.0D; dz = 1.0D;
            motionX = 1.0D; motionY = -1.0D; motionZ = 1.0D;
        }
        return world;
    }

    static final int NUM_SCENARIOS = 4;

    static void runScenario(int idx, StringBuilder sb) {
        List<AxisAlignedBB> world = scenario(idx);
        move(dx, dy, dz, world);
        sb.append(String.format("%016x%n", Double.doubleToRawLongBits(posX)));
        sb.append(String.format("%016x%n", Double.doubleToRawLongBits(posY)));
        sb.append(String.format("%016x%n", Double.doubleToRawLongBits(posZ)));
        sb.append(String.format("%016x%n", Double.doubleToRawLongBits(motionX)));
        sb.append(String.format("%016x%n", Double.doubleToRawLongBits(motionY)));
        sb.append(String.format("%016x%n", Double.doubleToRawLongBits(motionZ)));
        sb.append(String.format("%016x%n", Double.doubleToRawLongBits(boundingBox.minX)));
        sb.append(String.format("%016x%n", Double.doubleToRawLongBits(boundingBox.minY)));
        sb.append(String.format("%016x%n", Double.doubleToRawLongBits(boundingBox.minZ)));
        sb.append(String.format("%016x%n", Double.doubleToRawLongBits(boundingBox.maxX)));
        sb.append(String.format("%016x%n", Double.doubleToRawLongBits(boundingBox.maxY)));
        sb.append(String.format("%016x%n", Double.doubleToRawLongBits(boundingBox.maxZ)));
        sb.append(String.format("%08x%n", isCollidedHorizontally ? 1 : 0));
        sb.append(String.format("%08x%n", isCollidedVertically ? 1 : 0));
        sb.append(String.format("%08x%n", onGround ? 1 : 0));
    }

    public static void main(String[] args) {
        StringBuilder sb = new StringBuilder();
        if (args.length > 0) {
            runScenario(Integer.parseInt(args[0]), sb);
        } else {
            for (int i = 0; i < NUM_SCENARIOS; ++i) runScenario(i, sb);
        }
        System.out.print(sb);
    }
}
