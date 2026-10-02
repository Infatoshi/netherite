#define _POSIX_C_SOURCE 200809L
/* The fixed-function GL state of the world entity passes (raster_glx.h). The
 * lighting follows raster_hand.c: Mesa normalizes an infinite light's
 * direction in eye space, transforms the normal by the modelview's inverse
 * transpose and, with GL_RESCALE_NORMAL, scales it by the reciprocal length of
 * the inverse's third row; the lit colour is the scene ambient 0.4 plus 0.6
 * times each positive N.L, times glColor, clamped in the rasterizer. */
#include "raster_glx.h"
#include "renderstate.h"
#include "raster_obs.h"

#include <math.h>
#include <string.h>

const unsigned char glx_white[4] = {255, 255, 255, 255};

static void mat3_apply(const float *m, const float v[3], float out[3])
{
    for (int r = 0; r < 3; ++r) out[r] = m[r] * v[0] + m[4 + r] * v[1] + m[8 + r] * v[2];
}

static void normalize3(float v[3])
{
    float len = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (len > 0.0f)
        for (int i = 0; i < 3; ++i) v[i] /= len;
}

void glx_init(struct glx *g, const struct entity_raster_target *t, int brightness)
{
    memset(g, 0, sizeof *g);
    g->t = *t;
    g->t.material[0] = g->t.material[1] = g->t.material[2] = 1.0f;
    g->t.linear_depth = 1;
    g->t.depth_lequal = 1;
    memcpy(g->mv, t->mv, sizeof g->mv);
    static const double src[2][3] = {{0.20000000298023224, 1.0, -0.699999988079071},
                                     {-0.20000000298023224, 1.0, 0.699999988079071}};
    for (int i = 0; i < 2; ++i)
    {
        double len = (double)(float)sqrt(src[i][0] * src[i][0] + src[i][1] * src[i][1] +
                                         src[i][2] * src[i][2]);
        float l[3] = {(float)(src[i][0] / len), (float)(src[i][1] / len), (float)(src[i][2] / len)};
        mat3_apply(g->mv, l, g->light[i]);
        normalize3(g->light[i]);
    }
    g->color[0] = g->color[1] = g->color[2] = g->color[3] = 1.0f;
    g->lighting = 1;
    g->lb = brightness % 65536;
    g->ls = brightness / 65536;
}

/* The eye-space normal under GL_RESCALE_NORMAL (raster_hand.c's eye_normal). */
static void eye_normal(const float *m, const float n[3], float out[3])
{
    double a = m[0], b = m[4], cc = m[8], d = m[1], e = m[5], f = m[9], gg = m[2], h = m[6], k = m[10];
    double det = a * (e * k - f * h) - b * (d * k - f * gg) + cc * (d * h - e * gg);
    if (det == 0.0) { out[0] = n[0]; out[1] = n[1]; out[2] = n[2]; return; }
    float inv[3][3] = {
        {(float)((e * k - f * h) / det), (float)((cc * h - b * k) / det), (float)((b * f - cc * e) / det)},
        {(float)((f * gg - d * k) / det), (float)((a * k - cc * gg) / det), (float)((cc * d - a * f) / det)},
        {(float)((d * h - e * gg) / det), (float)((b * gg - a * h) / det), (float)((a * e - b * d) / det)}};
    for (int j = 0; j < 3; ++j) out[j] = n[0] * inv[0][j] + n[1] * inv[1][j] + n[2] * inv[2][j];
    float s = inv[2][0] * inv[2][0] + inv[2][1] * inv[2][1] + inv[2][2] * inv[2][2];
    if (s < 1e-12f) s = 1.0f;
    s = 1.0f / sqrtf(s);
    for (int j = 0; j < 3; ++j) out[j] *= s;
}

float glx_lit(const struct glx *g, const float n[3], int bytes)
{
    float q[3];
    for (int i = 0; i < 3; ++i)
        q[i] = bytes ? (float)(signed char)(int)(n[i] * 127.0f) / 127.0f : n[i];
    float e[3];
    eye_normal(g->mv, q, e);
    float d = 0.4f;
    for (int i = 0; i < 2; ++i)
    {
        float dot = e[0] * g->light[i][0] + e[1] * g->light[i][1] + e[2] * g->light[i][2];
        if (dot > 0.0f) d += 0.6f * dot;
    }
    return d;
}

static struct entity_clip_vertex vertex(const struct glx *g, const float p[3], float u, float v,
                                        float diffuse)
{
    struct entity_clip_vertex out;
    memset(&out, 0, sizeof out);
    float eye[4], w[4] = {p[0], p[1], p[2], 1.0f};
    for (int r = 0; r < 4; ++r)
        eye[r] = g->mv[r] * w[0] + g->mv[4 + r] * w[1] + g->mv[8 + r] * w[2] + g->mv[12 + r] * w[3];
    for (int r = 0; r < 4; ++r)
        out.clip[r] = g->t.proj[r] * eye[0] + g->t.proj[4 + r] * eye[1] +
                      g->t.proj[8 + r] * eye[2] + g->t.proj[12 + r] * eye[3];
    out.fogcoord = sqrtf(eye[0] * eye[0] + eye[1] * eye[1] + eye[2] * eye[2]);
    if (g->tex_matrix)
    {
        float tu = g->tm[0] * u + g->tm[4] * v + g->tm[12];
        float tv = g->tm[1] * u + g->tm[5] * v + g->tm[13];
        u = tu;
        v = tv;
    }
    out.u = u;
    out.v = v;
    out.diffuse = diffuse;
    out.color[0] = out.color[1] = out.color[2] = out.color[3] = 1.0f;
    out.alpha = g->color[3];
    out.light[0] = (float)g->lb;
    out.light[1] = (float)g->ls;
    return out;
}

void glx_quad(struct glx *g, const float p[4][3], const float uv[4][2], const float *n,
              const unsigned char *tex, int tw, int th)
{
    if (!tex) return;
    float d = g->lighting ? glx_lit(g, n ? n : g->normal, n != NULL) : 1.0f;
    struct entity_clip_vertex q[4];
    for (int k = 0; k < 4; ++k) q[k] = vertex(g, p[k], uv[k][0], uv[k][1], d);
    struct entity_raster_target t = g->t;
    for (int i = 0; i < 3; ++i) t.material[i] = g->color[i];
    if (!g->lighting) t.unlit = 1;
    raster_entity_quad(&t, tex, tw, th, q);
}

void glx_model_box(struct glx *g, float x, float y, float z, int dx, int dy, int dz, float grow,
                   int u, int v, int mirror, float scale, const unsigned char *tex, int tw, int th)
{
    float x0 = x - grow, y0 = y - grow, z0 = z - grow;
    float x1 = x + (float)dx + grow, y1 = y + (float)dy + grow, z1 = z + (float)dz + grow;
    if (mirror) { float s = x1; x1 = x0; x0 = s; }
    float p[8][3] = {{x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0},
                     {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}};
    static const int face[6][4] = {{5, 1, 2, 6}, {0, 4, 7, 3}, {5, 4, 0, 1},
                                   {2, 3, 7, 6}, {1, 0, 3, 2}, {4, 5, 6, 7}};
    int uv[6][4] = {{u + dz + dx, v + dz, u + dz + dx + dz, v + dz + dy},
                    {u, v + dz, u + dz, v + dz + dy},
                    {u + dz, v, u + dz + dx, v + dz},
                    {u + dz + dx, v + dz, u + dz + dx + dx, v},
                    {u + dz, v + dz, u + dz + dx, v + dz + dy},
                    {u + dz + dx + dz, v + dz, u + dz + dx + dz + dx, v + dz + dy}};
    for (int f = 0; f < 6; ++f)
    {
        /* TexturedQuad's corners: u2,v1 / u1,v1 / u1,v2 / u2,v2 on vertices
         * 0..3; flipFace reverses the array for a mirrored box */
        float cu[4] = {(float)uv[f][2] / (float)tw, (float)uv[f][0] / (float)tw,
                       (float)uv[f][0] / (float)tw, (float)uv[f][2] / (float)tw};
        float cv[4] = {(float)uv[f][1] / (float)th, (float)uv[f][1] / (float)th,
                       (float)uv[f][3] / (float)th, (float)uv[f][3] / (float)th};
        int idx[4];
        float qu[4], qv[4];
        for (int k = 0; k < 4; ++k)
        {
            int s = mirror ? 3 - k : k;
            idx[k] = face[f][s];
            qu[k] = cu[s];
            qv[k] = cv[s];
        }
        const float *a0 = p[idx[0]], *a1 = p[idx[1]], *a2 = p[idx[2]];
        double va[3], vb[3];
        for (int i = 0; i < 3; ++i)
        {
            va[i] = (double)a0[i] - (double)a1[i];
            vb[i] = (double)a2[i] - (double)a1[i];
        }
        double nn[3] = {vb[1] * va[2] - vb[2] * va[1], vb[2] * va[0] - vb[0] * va[2],
                        vb[0] * va[1] - vb[1] * va[0]};
        double len = (double)(float)sqrt(nn[0] * nn[0] + nn[1] * nn[1] + nn[2] * nn[2]);
        float nf[3] = {0, 0, 0};
        if (len >= 1.0E-4)
            for (int i = 0; i < 3; ++i) nf[i] = (float)(nn[i] / len);
        float qp[4][3], quv[4][2];
        for (int k = 0; k < 4; ++k)
        {
            for (int i = 0; i < 3; ++i) qp[k][i] = p[idx[k]][i] * scale;
            quv[k][0] = qu[k];
            quv[k][1] = qv[k];
        }
        glx_quad(g, qp, quv, nf, tex, tw, th);
    }
}

/* The quads are raster_eitem_quad's (raster_obs.c); a recorder takes the
 * item as one record (raster_rec_eitem: the device makes its quads) and the
 * C passes draw them unrecorded. */
void glx_item_in_2d(struct glx *g, float p1, float p2, float p3, float p4, int w, int h,
                    float thick, const unsigned char *tex, int tw, int th)
{
    if (!tex) return;
    struct raster_obs_eitem it;
    memset(&it, 0, sizeof it);
    it.p1 = p1; it.p2 = p2; it.p3 = p3; it.p4 = p4; it.thick = thick;
    it.w = w; it.h = h;
    /* glx_quad's lit factor of each face's Tessellator normal */
    static const float n[6][3] = {{0, 0, 1}, {0, 0, -1}, {-1, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    for (int i = 0; i < 6; ++i) it.diffuse[i] = g->lighting ? glx_lit(g, n[i], 1) : 1.0f;
    it.light[0] = (float)g->lb;
    it.light[1] = (float)g->ls;
    it.alpha = g->color[3];
    it.tex_matrix = g->tex_matrix;
    it.tm[0] = g->tm[0]; it.tm[1] = g->tm[4]; it.tm[2] = g->tm[12];
    it.tm[3] = g->tm[1]; it.tm[4] = g->tm[5]; it.tm[5] = g->tm[13];
    struct entity_raster_target t = g->t;
    for (int i = 0; i < 3; ++i) t.material[i] = g->color[i];
    if (!g->lighting) t.unlit = 1;
    int rec = raster_rec_on();
    if (rec)
    {
        struct raster_obs_estate st;
        const uint32_t *lm;
        raster_entity_estate(&t, &st, &lm);
        raster_rec_eitem(&st, lm, tex, tw, th, &it, g->mv, g->t.proj);
        if (raster_rec_only()) return;
    }
    struct raster_rec *held = rec ? raster_rec_hold() : NULL;
    for (int k = 0; k < raster_eitem_quads(&it); ++k)
    {
        struct entity_clip_vertex q[4];
        raster_eitem_quad(&it, g->mv, g->t.proj, k, q);
        raster_entity_quad(&t, tex, tw, th, q);
    }
    if (rec) raster_rec_restore(held);
}
