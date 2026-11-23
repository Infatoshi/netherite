#include "raster_sky2.h"
#include "jrand.h"
#include "smath.h"

#include <math.h>
#include <pthread.h>

void raster_sky2_rotation(float angle, float *cs, float *sn)
{
    float a = angle * 360.0f * (float)(3.141592653589793 / 180.0);
    *cs = cosf(a);
    *sn = sinf(a);
}

static void celestial_quad(void *frame, float angle, int mode, float radius, float height,
                           float u0, float v0, float u1, float v1, raster_sky2_quad draw)
{
    float z0 = mode == 5 ? radius : -radius;
    float z1 = -z0;
    const float xyz[4][3] = {{-radius,height,z0}, {radius,height,z0},
                             {radius,height,z1}, {-radius,height,z1}};
    struct raster_sky2_vertex vertices[4];
    float cs, sn;
    raster_sky2_rotation(angle, &cs, &sn);
    for (int i = 0; i < 4; ++i) {
        float x = xyz[i][0], y = cs * xyz[i][1] - sn * xyz[i][2];
        float z = sn * xyz[i][1] + cs * xyz[i][2];
        vertices[i] = (struct raster_sky2_vertex){-z, y, x, 0, 0};
    }
    vertices[0].u = vertices[3].u = u0;
    vertices[1].u = vertices[2].u = u1;
    vertices[0].v = vertices[1].v = v0;
    vertices[2].v = vertices[3].v = v1;
    draw(frame, mode, vertices);
}

void raster_sky2_draw(void *frame, float angle, int moon_phase, raster_sky2_quad quad)
{
    celestial_quad(frame, angle, 4, 30.0f, 100.0f, 0, 0, 1, 1, quad);
    int col = moon_phase % 4, row = moon_phase / 4;
    celestial_quad(frame, angle, 5, 20.0f, -100.0f,
                   (float)(col + 1) / 4.0f, (float)(row + 1) / 2.0f,
                   (float)col / 4.0f, (float)row / 2.0f, quad);
}

/* RenderGlobal.renderStars: 1500 draws of Random(10842L), the kept ones as
 * quads on the radius-100 sphere, built in doubles and stored by the
 * Tessellator as floats. Built once; the list never changes. */
static struct raster_sky2_vertex stars[1500 * 4];
static int nstars = -1;

static void build_stars(void)
{
    jrand r;
    jr_seed(&r, 10842LL);
    int n = 0;
    for (int i = 0; i < 1500; ++i) {
        double x = (double)(jr_float(&r) * 2.0f - 1.0f);
        double y = (double)(jr_float(&r) * 2.0f - 1.0f);
        double z = (double)(jr_float(&r) * 2.0f - 1.0f);
        double size = (double)(0.15f + jr_float(&r) * 0.1f);
        double d = x * x + y * y + z * z;
        if (!(d < 1.0 && d > 0.01)) continue;
        d = 1.0 / sqrt(d);
        x *= d; y *= d; z *= d;
        double cx = x * 100.0, cy = y * 100.0, cz = z * 100.0;
        double yaw = fd_atan2(x, z);
        double ys = fd_sin(yaw), yc = fd_cos(yaw);
        double pitch = fd_atan2(sqrt(x * x + z * z), y);
        double ps = fd_sin(pitch), pc = fd_cos(pitch);
        double roll = jr_double(&r) * 3.141592653589793 * 2.0;
        double rs = fd_sin(roll), rc = fd_cos(roll);
        for (int k = 0; k < 4; ++k) {
            double a = 0.0;
            double b = (double)((k & 2) - 1) * size;
            double c = (double)(((k + 1) & 2) - 1) * size;
            double e = b * rc - c * rs;
            double g = c * rc + b * rs;
            double h = e * ps + a * pc;
            double j = a * ps - e * pc;
            double l = j * ys - g * yc;
            double m = g * ys + j * yc;
            stars[n * 4 + k] = (struct raster_sky2_vertex){
                (float)(cx + l), (float)(cy + h), (float)(cz + m), 0, 0};
        }
        ++n;
    }
    nstars = n;
}

/* The star list under renderSky's sun and moon matrix: glRotatef(-90, 0, 1, 0)
 * then glRotatef(angle * 360, 1, 0, 0), as celestial_quad applies it. */
void raster_sky2_stars(void *frame, float angle, raster_sky2_quad draw)
{
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, build_stars);
    float cs, sn;
    raster_sky2_rotation(angle, &cs, &sn);
    for (int s = 0; s < nstars; ++s) {
        struct raster_sky2_vertex v[4];
        for (int i = 0; i < 4; ++i) {
            const struct raster_sky2_vertex *p = &stars[s * 4 + i];
            float y = cs * p->y - sn * p->z, z = sn * p->y + cs * p->z;
            v[i] = (struct raster_sky2_vertex){-z, y, p->x, 0, 0};
        }
        draw(frame, 8, v);
    }
}

const struct raster_sky2_vertex *raster_sky2_star_table(int *n)
{
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, build_stars);
    *n = nstars;
    return stars;
}
