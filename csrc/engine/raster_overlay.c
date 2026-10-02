#include "raster_overlay.h"
#include "raster_obs.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct point { float x, y, z, w, u, v, f; };   /* f: the eye distance, GL's fog coordinate */

static float bits_float(uint32_t bits)
{
    float f;
    memcpy(&f, &bits, sizeof f);
    return f;
}

/* Both passes use EntityRenderer's camera matrices. The mesher's vertices are
 * section local; its usual 1.000001 world-renderer scale is intentionally not
 * used for RenderGlobal's immediate damage geometry. */
static struct point project(const struct raster_overlay_in *in, double x, double y,
                            double z, float u, float v)
{
    float p[4] = {(float)(x - in->cam[0]), (float)(y - in->cam[1]),
                  (float)(z - in->cam[2]), 1.0f};
    float eye[4], clip[4];
    for (int r = 0; r < 4; ++r)
        eye[r] = in->mv[r] * p[0] + in->mv[4+r] * p[1] +
                 in->mv[8+r] * p[2] + in->mv[12+r];
    for (int r = 0; r < 4; ++r)
        clip[r] = in->proj[r] * eye[0] + in->proj[4+r] * eye[1] +
                  in->proj[8+r] * eye[2] + in->proj[12+r] * eye[3];
    if (clip[3] <= 0.0f) return (struct point){0, 0, 1, -1, u, v, 0};
    return (struct point){(clip[0] / clip[3] * 0.5f + 0.5f) * in->w,
                          (0.5f - clip[1] / clip[3] * 0.5f) * in->h,
                          clip[2] / clip[3] * 0.5f + 0.5f, clip[3], u, v,
                          sqrtf(eye[0] * eye[0] + eye[1] * eye[1] + eye[2] * eye[2])};
}

/* GL's fog factor at an eye distance (1 keeps the fragment's colour) */
static float fog_factor(const struct raster_overlay_in *in, float d)
{
    float f;
    if (!in->fog) return 1.0f;
    if (in->fogm == 9729) f = (in->foge - d) / (in->foge - in->fogs);
    else if (in->fogm == 2048) f = expf(-in->fogd * d);
    else if (in->fogm == 2049) { float e = in->fogd * d; f = expf(-e * e); }
    else return 1.0f;
    return f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
}

static unsigned char quant(float v)
{
    if (v <= 0) return 0;
    if (v >= 1) return 255;
    return (unsigned char)lrintf(v * 255.0f);
}

static void line(const struct raster_overlay_in *in, struct point a, struct point b)
{
    if (raster_rec_on()) {
        struct raster_obs_line l;
        memset(&l, 0, sizeof l);
        const float pa[5] = {a.x, a.y, a.z, a.w, a.f}, pb[5] = {b.x, b.y, b.z, b.w, b.f};
        memcpy(l.a, pa, sizeof pa);
        memcpy(l.b, pb, sizeof pb);
        l.fog_on = in->fog != NULL;
        for (int c = 0; in->fog && c < 3; ++c) l.fog[c] = in->fog[c];
        l.fogs = in->fogs; l.foge = in->foge; l.fogd = in->fogd; l.fogm = in->fogm;
        raster_rec_line(&l);
    }
    if (raster_rec_only()) return;
    if (a.w <= 0 || b.w <= 0) return;
    float dx = b.x - a.x, dy = b.y - a.y, len2 = dx * dx + dy * dy;
    if (len2 < 1e-12f) return;
    int x0 = (int)fmaxf(0, floorf(fminf(a.x, b.x) - 1.5f));
    int x1 = (int)fminf(in->w - 1, ceilf(fmaxf(a.x, b.x) + 1.5f));
    int y0 = (int)fmaxf(0, floorf(fminf(a.y, b.y) - 1.5f));
    int y1 = (int)fminf(in->h - 1, ceilf(fmaxf(a.y, b.y) + 1.5f));
    for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) {
        float px = x + 0.5f - a.x, py = y + 0.5f - a.y;
        float t = (px * dx + py * dy) / len2;
        if (t < 0 || t >= 1) continue; /* GL's diamond-exit endpoint rule */
        float cross = fabsf(px * dy - py * dx);
        if (cross > sqrtf(len2)) continue; /* width two pixels */
        float z = a.z + t * (b.z - a.z);
        size_t k = (size_t)y * in->w + x;
        if (z > in->depth[k]) continue;
        /* the black line fogged toward the fog colour, blended at alpha 0.4 */
        float qa = (1.0f - t) / a.w, qb = t / b.w;
        float fog = fog_factor(in, (qa * a.f + qb * b.f) / (qa + qb));
        for (int c = 0; c < 3; ++c) {
            float src = in->fog ? in->fog[c] * (1.0f - fog) : 0.0f;
            in->rgb[k * 3 + c] = quant(quant(src) / 255.0f * 0.4f + in->rgb[k * 3 + c] / 255.0f * 0.6f);
        }
    }
}

static void outline(const struct raster_overlay_in *in)
{
    double lo[3], hi[3];
    for (int j = 0; j < 3; ++j) {
        lo[j] = in->box[j] - (double)0.002F;
        hi[j] = in->box[j + 3] + (double)0.002F;
    }
    struct point p[8];
    for (int i = 0; i < 8; ++i)
        p[i] = project(in, i & 1 ? hi[0] : lo[0], i & 2 ? hi[1] : lo[1],
                       i & 4 ? hi[2] : lo[2], 0, 0);
    static const int edge[12][2] = {
        {0,1},{1,5},{5,4},{4,0}, {2,3},{3,7},{7,6},{6,2},
        {0,2},{1,3},{5,7},{4,6}
    };
    for (int i = 0; i < 12; ++i) line(in, p[edge[i][0]], p[edge[i][1]]);
}

static float tri_edge(struct point a, struct point b, float x, float y)
{
    return (x - a.x) * (b.y - a.y) - (y - a.y) * (b.x - a.x);
}

static int top_left(struct point a, struct point b)
{
    float dx = b.x - a.x, dy = b.y - a.y;
    return dy < 0 || (dy == 0 && dx > 0);
}

static void texcoord(struct point a, struct point b, struct point c, float area,
                     float x, float y, float *u, float *v)
{
    float wa = tri_edge(b, c, x, y) / area / a.w;
    float wb = tri_edge(c, a, x, y) / area / b.w;
    float wc = tri_edge(a, b, x, y) / area / c.w;
    float w = wa + wb + wc;
    *u = (wa * a.u + wb * b.u + wc * c.u) / w;
    *v = (wa * a.v + wb * b.v + wc * c.v) / w;
}

static void crack_triangle(const struct raster_overlay_in *in,
                           struct point a, struct point b, struct point c)
{
    if (raster_rec_on()) {
        const struct point *q[3] = {&a, &b, &c};
        float p[3][7];
        for (int i = 0; i < 3; ++i) {
            p[i][0] = q[i]->x; p[i][1] = q[i]->y; p[i][2] = q[i]->z; p[i][3] = q[i]->w;
            p[i][4] = q[i]->u; p[i][5] = q[i]->v; p[i][6] = q[i]->f;
        }
        raster_rec_crack(p, in->fog, in->fogs, in->foge, in->fogd, in->fogm);
    }
    if (raster_rec_only()) return;
    if (a.w <= 0 || b.w <= 0 || c.w <= 0) return;
    float area = tri_edge(a, b, c.x, c.y);
    if (area <= 0) return;
    int x0 = (int)fmaxf(0, floorf(fminf(a.x, fminf(b.x, c.x))));
    int x1 = (int)fminf(in->w - 1, ceilf(fmaxf(a.x, fmaxf(b.x, c.x))));
    int y0 = (int)fmaxf(0, floorf(fminf(a.y, fminf(b.y, c.y))));
    int y1 = (int)fminf(in->h - 1, ceilf(fmaxf(a.y, fmaxf(b.y, c.y))));
    for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) {
        float sx = x + 0.5f, sy = y + 0.5f;
        float e0 = tri_edge(b, c, sx, sy), e1 = tri_edge(c, a, sx, sy),
              e2 = tri_edge(a, b, sx, sy);
        if (e0 < 0 || e1 < 0 || e2 < 0) continue;
        if ((e0 == 0 && !top_left(b, c)) || (e1 == 0 && !top_left(c, a)) ||
            (e2 == 0 && !top_left(a, b))) continue;
        float ba = e0 / area, bb = e1 / area, bc = e2 / area;
        size_t k = (size_t)y * in->w + x;
        float z = ba * a.z + bb * b.z + bc * c.z - 0.00002f;
        if (z > in->depth[k] + 1e-6f) continue;
        float u, v, ux, vx, uy, vy;
        texcoord(a, b, c, area, sx, sy, &u, &v);
        ux = uy = u; vx = vy = v;
        if (in->texture->levels && in->texture->min_filter == 9986) {
            texcoord(a, b, c, area, sx + 1.0f, sy, &ux, &vx);
            texcoord(a, b, c, area, sx, sy + 1.0f, &uy, &vy);
        }
        float rgba[4];
        raster_texture_sample(in->texture, u, v, ux, vx, uy, vy, rgba);
        if (rgba[3] * 0.5f <= 0.1f) continue;
        float qa = ba / a.w, qb = bb / b.w, qc = bc / c.w;
        float fog = fog_factor(in, (qa * a.f + qb * b.f + qc * c.f) / (qa + qb + qc));
        for (int ch = 0; ch < 3; ++ch) {
            float dst = in->rgb[k * 3 + ch] / 255.0f;
            float src = in->fog ? rgba[ch] * fog + in->fog[ch] * (1.0f - fog) : rgba[ch];
            in->rgb[k * 3 + ch] = quant(2.0f * src * dst);
        }
    }
}

static void crack(const struct raster_overlay_in *in)
{
    char name[32];
    snprintf(name, sizeof name, "destroy_stage_%d", in->stage);
    const struct rb_uv *icon = NULL;
    for (int i = 0; i < in->atlas->n; ++i)
        if (strcmp(in->atlas->name[i], name) == 0) { icon = &in->atlas->uv[i]; break; }
    if (!icon) return;
    rb_mesh_override_block(in->mesher, in->dx, in->dy, in->dz, icon);
    const struct rb_tess *t = in->mesher->t;
    int cx = in->dx >> 4, cy = in->dy >> 4, cz = in->dz >> 4;
    for (int i = 0; i + 3 < t->vertex_count; i += 4) {
        struct point q[4];
        for (int j = 0; j < 4; ++j) {
            const int32_t *r = t->raw + (size_t)(i + j) * 8;
            q[j] = project(in, cx * 16 + (double)bits_float((uint32_t)r[0]),
                           cy * 16 + (double)bits_float((uint32_t)r[1]),
                           cz * 16 + (double)bits_float((uint32_t)r[2]),
                           bits_float((uint32_t)r[3]), bits_float((uint32_t)r[4]));
        }
        crack_triangle(in, q[0], q[1], q[3]);
        crack_triangle(in, q[1], q[2], q[3]);
    }
}

void raster_overlay_draw(const struct raster_overlay_in *in)
{
    if (in->selected) outline(in);
    if (in->stage >= 0 && in->stage < 10 && in->mesher && in->atlas && in->texture)
        crack(in);
}

void raster_overlay_crack_triangle(const struct raster_overlay_in *in, const float p[3][7])
{
    struct point q[3];
    for (int i = 0; i < 3; ++i)
        q[i] = (struct point){p[i][0], p[i][1], p[i][2], p[i][3], p[i][4], p[i][5], p[i][6]};
    crack_triangle(in, q[0], q[1], q[2]);
}

void raster_overlay_line(const struct raster_overlay_in *in, const struct raster_obs_line *l)
{
    struct point a = {l->a[0], l->a[1], l->a[2], l->a[3], 0, 0, l->a[4]};
    struct point b = {l->b[0], l->b[1], l->b[2], l->b[3], 0, 0, l->b[4]};
    line(in, a, b);
}
