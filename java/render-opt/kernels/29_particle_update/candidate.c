/* CANDIDATE: pure-C port of MC 1.11.2 Particle.onUpdate()
 *   (src/net/minecraft/client/particle/Particle.java:156 onUpdate + :309 move + :359 resetPositionToBB).
 *
 * Golden CAPTURED FROM REAL MINECRAFT (capture_mode "live-hook", NetheriteMod 'capture_particle'):
 * particles are constructed in OPEN AIR (y~200, canCollide=false) so move() takes the no-collision
 * branch -- the kernel is the pure physics integration (the world-coupled collision part is excluded
 * by construction, and is the part NOT ported here).
 *
 * Input record (one per line, from golden/inputs.txt), all as raw bit patterns:
 *     posX posY posZ motionX motionY motionZ  -- double rawLongBits
 *     age maxAge                              -- ints
 *     gravity width height                    -- float rawIntBits
 *
 * Logic (verbatim from onUpdate + no-collision move):
 *   age++;                                   // (expiry only calls setExpired, no pos/motion effect)
 *   motionY -= 0.04 * (double)gravity;
 *   // move(mx,my,mz): canCollide=false -> bounding box offset then recenter
 *   f = width/2.0f;
 *   bb = [posX-f, posY, posZ-f .. posX+f, posY+height, posZ+f]; bb.offset(mx,my,mz);
 *   posX = (bb.minX+bb.maxX)/2; posY = bb.minY; posZ = (bb.minZ+bb.maxZ)/2;  // resetPositionToBB
 *   onGround = (d0 != y && d0 < 0) = false   // y unchanged with no collision
 *   motion *= 0.9800000190734863;
 *
 * Output (8 lines per record): posX posY posZ motionX motionY motionZ (double rawLongBits),
 * age (int), onGround (0/1).  Must BITWISE-match golden/golden.txt. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static double d_of(int64_t b) { double d; memcpy(&d, &b, 8); return d; }
static int64_t b_of(double d) { int64_t b; memcpy(&b, &d, 8); return b; }
static float f_of(int32_t b) { float f; memcpy(&f, &b, 4); return f; }

int main(void) {
    int64_t pxb, pyb, pzb, mxb, myb, mzb;
    int age, maxAge;
    int32_t gravb, wb, hb;
    while (scanf("%lld %lld %lld %lld %lld %lld %d %d %d %d %d",
                 (long long*)&pxb, (long long*)&pyb, (long long*)&pzb,
                 (long long*)&mxb, (long long*)&myb, (long long*)&mzb,
                 &age, &maxAge, &gravb, &wb, &hb) == 11) {
        double posX = d_of(pxb), posY = d_of(pyb), posZ = d_of(pzb);
        double mx = d_of(mxb), my = d_of(myb), mz = d_of(mzb);
        float grav = f_of(gravb), width = f_of(wb), height = f_of(hb);

        age++;
        my -= 0.04 * (double)grav;

        /* move(): no-collision branch */
        float f = width / 2.0f;
        double minX = posX - (double)f, maxX = posX + (double)f;
        double minY = posY,             maxY = posY + (double)height;
        double minZ = posZ - (double)f, maxZ = posZ + (double)f;
        minX += mx; maxX += mx;
        minY += my; maxY += my;
        minZ += mz; maxZ += mz;
        (void)maxY;
        posX = (minX + maxX) / 2.0;
        posY = minY;
        posZ = (minZ + maxZ) / 2.0;
        int onGround = 0; /* d0 == y (no collision), so onGround stays false */

        mx *= 0.9800000190734863;
        my *= 0.9800000190734863;
        mz *= 0.9800000190734863;

        printf("%lld\n%lld\n%lld\n%lld\n%lld\n%lld\n%d\n%d\n",
               (long long)b_of(posX), (long long)b_of(posY), (long long)b_of(posZ),
               (long long)b_of(mx), (long long)b_of(my), (long long)b_of(mz),
               age, onGround);
    }
    return 0;
}
